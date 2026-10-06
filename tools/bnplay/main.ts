/**
 * bnplay: agent playtest supervisor.
 *
 *   bnplay start <trial.toml>            boot an Episode from a Trial; prints the session id
 *   bnplay step <session> '<json>'       send one command; prints the lean response
 *   bnplay stop <session>                end the Episode; prints its report and exits with its verdict
 *   bnplay report <session>              print the report of an Episode (running or ended)
 *   bnplay fixture add <save> [name]     clone a world save into the fixture library
 *   bnplay fixture baseline <name>       boot the fixture and record its post-readiness game log
 *   bnplay fixture list                  show each fixture and whether its baseline is fresh
 *   bnplay shutdown                      end every Episode and stop the resident daemon
 *
 * A resident daemon keeps the games running between calls and is started on first use (see
 * config.ts for the environment that configures it). Failures print one line on stderr and exit 2.
 * `stop` and `report` exit with the verdict: 0 pass, 1 an oracle failed, 2 harness error (the game
 * failed to boot, hung or died, or was reaped), 3 inconclusive (the wall-clock limit ended the
 * Episode before any oracle reached a verdict that decides it).
 * `bnplay daemon` runs the daemon in the foreground.
 */
import { resolve } from "@std/path"
import type { DriverRequest } from "./client.ts"
import { loadConfig } from "./config.ts"
import { runDaemon } from "./daemon.ts"
import { call, type DaemonRequest, daemonState, ensureDaemon } from "./ipc.ts"

const USAGE = `usage:
  bnplay start <trial.toml>
  bnplay step <session> '<command json>'
  bnplay stop <session>
  bnplay report <session>
  bnplay fixture add <save-dir> [name]
  bnplay fixture baseline <name>
  bnplay fixture list
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
    case "report":
      expect(1)
      return { op: "report", session: rest[0] }
    case "fixture": {
      const [sub, ...args] = rest
      switch (sub) {
        case "add":
          if (args.length < 1 || args.length > 2) {
            throw new UsageError(
              `bnplay fixture add takes a save directory and an optional name\n${USAGE}`,
            )
          }
          return { op: "fixture_add", source: resolve(args[0]), name: args[1] }
        case "baseline":
          if (args.length !== 1) {
            throw new UsageError(`bnplay fixture baseline takes a fixture name\n${USAGE}`)
          }
          return { op: "fixture_baseline", name: args[0] }
        case "list":
          if (args.length !== 0) {
            throw new UsageError(`bnplay fixture list takes no argument\n${USAGE}`)
          }
          return { op: "fixture_list" }
        default:
          throw new UsageError(USAGE)
      }
    }
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
    if ((await daemonState(home)) === "absent") {
      console.log(JSON.stringify({ daemon: "not running" }))
      return 0
    }
  } else {
    await ensureDaemon(home)
  }
  const reply = await call(home, request)
  if (!reply.ok) throw new Error(reply.error)
  console.log(JSON.stringify(reply.result))
  const { result } = reply
  if (
    (request.op === "stop" || request.op === "report") && "exit_code" in result &&
    typeof result.exit_code === "number"
  ) {
    return result.exit_code
  }
  return 0
}

try {
  Deno.exit(await main(Deno.args))
} catch (e) {
  console.error(`bnplay: ${(e as Error).message}`)
  Deno.exit(2)
}
