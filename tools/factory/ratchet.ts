/// The baseline ratchet: a full-suite run may fail only the cases listed in
/// `config.baselineFailures`, and that list may never grow.
///
/// Input is the output of `deno task test:progress` (scripts/test_progress.ts), which prints one
/// `FAIL <case name>` line per unexpected failure and a `stopped early:` line when the binary
/// crashed. Usage: deno run -A tools/factory/ratchet.ts --log <file> [--config <config.json>]
import { fromFileUrl } from "@std/path"

export type RatchetResult = {
  /// Every case reported failed in the run.
  failed: string[]
  /// Failed cases that are not in the baseline: these fail the gate.
  unexpected: string[]
  /// Baseline cases that passed this time (a human removes them from the baseline).
  fixed: string[]
  /// The binary died before finishing every case; a crash is never a pass.
  stoppedEarly: boolean
  /// True when the run produced a `Catch2:` totals line, i.e. it ran to the end.
  sawTotals: boolean
  ok: boolean
}

// deno-lint-ignore no-control-regex -- stripping ANSI colour escapes from captured output
const ANSI = /\x1b\[[0-9;]*[A-Za-z]/g

export function evaluateRun(output: string, baseline: readonly string[]): RatchetResult {
  const lines = output.replace(ANSI, "").split(/\r?\n/)
  const failed = [
    ...new Set(
      lines.flatMap((l) => (l.startsWith("FAIL ") ? [l.slice("FAIL ".length).trim()] : [])),
    ),
  ]
  const stoppedEarly = lines.some((l) => l.startsWith("stopped early:"))
  const sawTotals = lines.some((l) => l.startsWith("Catch2:"))
  const allowed = new Set(baseline)
  const unexpected = failed.filter((name) => !allowed.has(name))
  const fixed = baseline.filter((name) => !failed.includes(name))
  return {
    failed,
    unexpected,
    fixed,
    stoppedEarly,
    sawTotals,
    ok: unexpected.length === 0 && !stoppedEarly && sawTotals,
  }
}

/// Names in `head` that `base` does not list.
export function grown(base: readonly string[], head: readonly string[]): string[] {
  const had = new Set(base)
  return head.filter((name) => !had.has(name))
}

export function describe(r: RatchetResult): string {
  const out: string[] = []
  if (!r.sawTotals) out.push("no Catch2 totals line: the run did not finish")
  if (r.stoppedEarly) out.push("the test binary stopped early (crash or abort)")
  if (r.unexpected.length) {
    out.push(`${r.unexpected.length} unexpected failure(s):`, ...r.unexpected.map((n) => `  ${n}`))
  }
  if (r.fixed.length) {
    out.push(`baseline cases that now pass (remove from baselineFailures): ${r.fixed.join(", ")}`)
  }
  return out.length ? out.join("\n") : `ok: ${r.failed.length} failure(s), all in the baseline`
}

function option(args: string[], name: string): string | undefined {
  const i = args.indexOf(name)
  return i >= 0 ? args[i + 1] : undefined
}

if (import.meta.main) {
  const logPath = option(Deno.args, "--log")
  if (!logPath) {
    console.error("usage: ratchet.ts --log <test:progress output> [--config <config.json>]")
    Deno.exit(2)
  }
  const configPath = option(Deno.args, "--config") ??
    fromFileUrl(new URL("./config.json", import.meta.url))
  const baseline: string[] = JSON.parse(await Deno.readTextFile(configPath)).baselineFailures
  const result = evaluateRun(await Deno.readTextFile(logPath), baseline)
  console.log(describe(result))
  Deno.exit(result.ok ? 0 : 1)
}
