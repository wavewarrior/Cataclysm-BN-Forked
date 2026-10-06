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
 *   bnplay doctor [--fixture <name>] [--self-check]
 *                                        preflight: driver flag, binary freshness, fixture and
 *                                        baseline, stray driver processes, memory and swap;
 *                                        starts no game unless --self-check asks for the A/A
 *                                        determinism pair. Exit 0 healthy, 1 a check failed
 *   bnplay shutdown                      end every Episode and stop the resident daemon
 *
 * A resident daemon keeps the games running between calls and is started on first use (see
 * config.ts for the environment that configures it). Failures print one line on stderr and exit 2.
 * `stop` and `report` exit with the verdict: 0 pass, 1 an oracle failed, 2 harness error (the game
 * failed to boot, hung or died, or was reaped), 3 inconclusive (the wall-clock limit ended the
 * Episode before any oracle reached a verdict that decides it).
 * `bnplay daemon` runs the daemon in the foreground.
 *
 * MCP: `bnplay mcp` serves the same operations as typed tools (start, step, stop, report,
 * fixture_add, fixture_baseline, fixture_list, doctor, shutdown) over stdio, as a thin client of
 * the same daemon. Arguments are named like the CLI's (`step` takes `session` and `command`, an
 * object; `doctor` takes `fixture` and `self_check`). A result is the JSON the CLI prints, as
 * structured content; a refusal (what the CLI prints on stderr with exit 2) is a tool error. A
 * verdict (`exit_code` in the report) and `healthy: false` are results, not errors. Mount it from
 * an MCP host with the command
 *   deno run --allow-run --allow-env --allow-read --allow-write --allow-net \
 *     --config deno.jsonc tools/bnplay/main.ts mcp
 * or `deno task bnplay mcp`. Both front ends are generated from operations.ts.
 */
import { parseArgs, USAGE } from "./cli.ts"
import { runDaemon } from "./daemon.ts"
import { runMcp } from "./mcp.ts"
import { runRequest, UsageError } from "./operations.ts"

async function main(args: string[]): Promise<number> {
  if (args[0] === "daemon" || args[0] === "mcp") {
    if (args.length !== 1) throw new UsageError(USAGE)
    await (args[0] === "daemon" ? runDaemon() : runMcp())
    return 0
  }
  const { request } = parseArgs(args)
  const result = await runRequest(request)
  console.log(JSON.stringify(result))
  if (
    (request.op === "stop" || request.op === "report") && "exit_code" in result &&
    typeof result.exit_code === "number"
  ) {
    return result.exit_code
  }
  // A failed preflight check is a finding, printed whole, not a failure of the command itself.
  return request.op === "doctor" && "healthy" in result && result.healthy === false ? 1 : 0
}

try {
  Deno.exit(await main(Deno.args))
} catch (e) {
  console.error(`bnplay: ${(e as Error).message}`)
  Deno.exit(2)
}
