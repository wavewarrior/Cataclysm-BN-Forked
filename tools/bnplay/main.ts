#!/usr/bin/env -S deno run --allow-run --allow-env --allow-read --allow-write --allow-net
/**
 * bnplay: a thin CLI over the game's `--driver-fd` driver, for agents that explore the game.
 *
 *   bnplay start <save-dir> [--seed N] [--date YYYY-MM-DD] [--time HH:MM]
 *       Clone the save into a private session and boot the game windowless on it. Prints
 *       {"session":"ab12cd34","userdir":"...","log":"...","boot_ms":7110}.
 *   bnplay step <session> '{"cmd":"state"}'
 *       Send one driver request and print the driver's response line. Exit 0 when the game
 *       answered ok, 1 when it answered an error, 2 when the session could not be reached.
 *   bnplay stop <session>
 *       Ask the game to quit and tear the session down. Prints
 *       {"session":"ab12cd34","ended":"stopped","exit_code":0}.
 *   bnplay hold <session>
 *       Internal: the detached session holder `start` spawns. Never call it by hand.
 *
 * The game accepts: ping, state, seed, set_time, attach_view, view, query, run_scene, move, wait,
 * sleep, action, craft, melee, key. See src/driver_commands.cpp for the exact fields.
 */
import { join } from "@std/path"
import { loadConfig } from "./config.ts"
import type { Config } from "./config.ts"
import { stepTimeout } from "./step_timeout.ts"
import { hold } from "./holder.ts"
import {
  askHolder,
  killHolder,
  logPath,
  readState,
  sessionDir,
  SessionError,
  spawnHolder,
  validateSave,
  waitForBoot,
  writeState,
} from "./session.ts"
import type { DriverRequest } from "./client.ts"
import type { SessionState } from "./session.ts"

const USAGE = `usage:
  bnplay start <save-dir> [--seed N] [--date YYYY-MM-DD] [--time HH:MM]
  bnplay step <session> '{"cmd":"state"}'
  bnplay stop <session>`

class UsageError extends Error {}

type StartArgs = { save: string; seed?: number; date?: string; time?: string }

function parseStart(rest: string[]): StartArgs {
  const positional: string[] = []
  const args: StartArgs = { save: "" }
  for (let i = 0; i < rest.length; i++) {
    const flag = rest[i]
    if (!flag.startsWith("--")) {
      positional.push(flag)
      continue
    }
    const value = rest[++i]
    if (flag === "--seed") {
      if (value === undefined || !/^\d+$/.test(value)) throw new UsageError("--seed wants a number")
      args.seed = Number(value)
    } else if (flag === "--date") {
      if (value === undefined || !/^\d{4}-\d{2}-\d{2}$/.test(value)) {
        throw new UsageError("--date wants YYYY-MM-DD")
      }
      args.date = value
    } else if (flag === "--time") {
      if (value === undefined || !/^\d{2}:\d{2}$/.test(value)) {
        throw new UsageError("--time wants HH:MM")
      }
      args.time = value
    } else {
      throw new UsageError(`unknown flag ${flag}`)
    }
  }
  if (positional.length !== 1) throw new UsageError(USAGE)
  return { ...args, save: positional[0] }
}

async function start(config: Config, rest: string[]): Promise<number> {
  const args = parseStart(rest)
  const save = await validateSave(args.save)
  const session = crypto.randomUUID().replaceAll("-", "").slice(0, 8)
  const dir = sessionDir(config.home, session)
  await Deno.mkdir(dir, { recursive: true })
  const booting: SessionState = { session, status: "booting", save }
  if (args.seed !== undefined) booting.seed = args.seed
  if (args.date !== undefined) booting.date = args.date
  if (args.time !== undefined) booting.time = args.time
  await writeState(dir, booting)
  await spawnHolder(session, logPath(dir))
  // The holder clones the save before it boots, so allow slack beyond the boot timeout itself.
  const state = await waitForBoot(dir, config.bootTimeoutMs + 30_000)
  if (state.status === "ended") {
    throw new SessionError(
      `the game did not come up (${state.ended}${state.error ? `: ${state.error}` : ""}); see ${
        state.log ?? logPath(dir)
      }`,
    )
  }
  console.log(
    JSON.stringify({
      session: state.session,
      userdir: state.userdir,
      log: state.log,
      boot_ms: state.boot_ms,
    }),
  )
  return 0
}

async function step(config: Config, rest: string[]): Promise<number> {
  const [session, json] = rest
  if (session === undefined || json === undefined) throw new UsageError(USAGE)
  let request: DriverRequest
  try {
    request = JSON.parse(json)
  } catch {
    throw new UsageError(`the request is not JSON: ${json}`)
  }
  if (typeof request?.cmd !== "string") throw new UsageError('the request needs a "cmd" string')
  const dir = sessionDir(config.home, session)
  // The holder bounds the request itself; the front end gives it that budget plus slack for the
  // hop, so a holder wedged inside a hung game cannot strand this process.
  const reply = await askHolder(dir, { op: "step", request }, stepTimeout(config, request) + 10_000)
  if (!reply.ok) throw new SessionError(reply.error)
  console.log(JSON.stringify(reply.result))
  const answered = "status" in reply.result && reply.result.status === "ok"
  return answered ? 0 : 1
}

async function stop(config: Config, rest: string[]): Promise<number> {
  const session = rest[0]
  if (session === undefined) throw new UsageError(USAGE)
  const dir = sessionDir(config.home, session)
  const before = await readState(dir)
  if (before === undefined) throw new SessionError(`no such session: ${session}`)
  if (before.status === "ended") {
    console.log(JSON.stringify(summarise(before)))
    return 0
  }
  try {
    const reply = await askHolder(dir, { op: "stop" }, 30_000)
    if (!reply.ok) throw new SessionError(reply.error)
    console.log(JSON.stringify({ session, ...reply.result }))
    return 0
  } catch (e) {
    if (!(e instanceof SessionError)) throw e
    // A holder that will not answer (wedged inside a hung game, or dead with a stale socket):
    // kill it by the pid it recorded. The fd shim sees the closed pipe and reaps the game group.
    if (before.pid === undefined) throw e
    killHolder(before.pid)
    const after = await readState(dir)
    console.log(
      JSON.stringify({
        session,
        ended: after?.status === "ended" ? after.ended : "killed",
        note: `the holder did not answer stop and was killed: ${e.message}`,
      }),
    )
    return 0
  }
}

/** What a session that already ended reports back, from its state file. */
function summarise(state: SessionState): Record<string, unknown> {
  const out: Record<string, unknown> = { session: state.session, ended: state.ended ?? "unknown" }
  if (state.exit_code !== undefined) out.exit_code = state.exit_code
  if (state.error !== undefined) out.error = state.error
  return out
}

/** Sessions the state files still call ready, so a lost session id is not a dead end. */
async function liveSessions(home: string): Promise<string[]> {
  const entries = await Array.fromAsync(Deno.readDir(join(home, "sessions"))).catch(() => [])
  const lives: string[] = []
  for (const entry of entries) {
    const state = await readState(join(home, "sessions", entry.name))
    if (state?.status === "ready") lives.push(entry.name)
  }
  return lives
}

async function main(argv: string[]): Promise<number> {
  const [command, ...rest] = argv
  if (command === undefined || command === "help" || command === "--help") {
    console.log(USAGE)
    return command === undefined ? 2 : 0
  }
  const config = loadConfig()
  if (command === "start") return await start(config, rest)
  if (command === "step") return await step(config, rest)
  if (command === "stop") return await stop(config, rest)
  if (command === "hold") {
    if (rest[0] === undefined) throw new UsageError(USAGE)
    return await hold(rest[0])
  }
  throw new UsageError(`unknown command ${JSON.stringify(command)}\n${USAGE}`)
}

async function guarded(argv: string[]): Promise<number> {
  try {
    return await main(argv)
  } catch (e) {
    if (e instanceof UsageError) {
      console.error(`bnplay: ${e.message}`)
      return 2
    }
    if (e instanceof SessionError) {
      const lives = await liveSessions(loadConfig().home).catch(() => [])
      console.error(
        `bnplay: ${e.message}${lives.length > 0 ? ` (live sessions: ${lives.join(", ")})` : ""}`,
      )
      return 2
    }
    throw e
  }
}

if (import.meta.main) {
  Deno.exit(await guarded(Deno.args))
}
