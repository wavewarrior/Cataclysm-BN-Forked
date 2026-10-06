/**
 * bnplay: agent playtest supervisor.
 *
 *   bnplay start <trial.toml>            boot an Episode from a Trial; prints the session id
 *   bnplay step <session> '<json>'       send one command; prints the lean response
 *   bnplay stop <session>                end the Episode; prints where its transcript is
 *   bnplay shutdown                      end every Episode and stop the resident daemon
 *
 * A resident daemon keeps the games running between calls and is started on first use (see
 * config.ts for the environment that configures it). Failures print one line on stderr and exit 2.
 * `bnplay daemon` runs the daemon in the foreground.
 */
import { resolve } from "@std/path"
import type { DriverRequest } from "./client.ts"
import { loadConfig } from "./config.ts"
import { runDaemon } from "./daemon.ts"
import { call, type DaemonRequest, daemonRunning, ensureDaemon } from "./ipc.ts"

const USAGE = `usage:
  bnplay start <trial.toml>
  bnplay step <session> '<command json>'
  bnplay stop <session>
  bnplay shutdown`

class UsageError extends Error {}

/** Turns the command line into the daemon operation it asks for. */
function parseArgs(args: string[]): DaemonRequest {
  const [command, ...rest] = args
  const expect = (count: number) => {
    if (rest.length !== count) {
      throw new UsageError(`bnplay ${command} takes ${count} argument(s)\n${USAGE}`)
    }
  }
  switch (command) {
    case "start":
      expect(1)
      return { op: "start", trial: resolve(rest[0]) }
    case "step": {
      expect(2)
      let request: unknown
      try {
        request = JSON.parse(rest[1])
      } catch {
        throw new UsageError(`the step command is not valid JSON: ${rest[1]}`)
      }
      if (typeof request !== "object" || request === null || Array.isArray(request)) {
        throw new UsageError('the step command must be a JSON object, e.g. \'{"cmd":"state"}\'')
      }
      return { op: "step", session: rest[0], request: request as DriverRequest }
    }
    case "stop":
      expect(1)
      return { op: "stop", session: rest[0] }
    case "shutdown":
      expect(0)
      return { op: "shutdown" }
    default:
      throw new UsageError(USAGE)
  }
}

async function main(args: string[]): Promise<number> {
  if (args[0] === "daemon") {
    await runDaemon()
    return 0
  }
  const request = parseArgs(args)
  const { home } = loadConfig()
  if (request.op === "shutdown") {
    if (!(await daemonRunning(home))) {
      console.log(JSON.stringify({ daemon: "not running" }))
      return 0
    }
  } else {
    await ensureDaemon(home)
  }
  const reply = await call(home, request)
  if (!reply.ok) throw new Error(reply.error)
  console.log(JSON.stringify(reply.result))
  return 0
}

try {
  Deno.exit(await main(Deno.args))
} catch (e) {
  console.error(`bnplay: ${(e as Error).message}`)
  Deno.exit(2)
}
