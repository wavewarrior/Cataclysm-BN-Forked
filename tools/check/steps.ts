/// Step ordering, diff classification and the event record for `check`. Pure: no IO here, so the
/// whole policy is unit-testable without a compiler.
import { DEFAULT_BASE } from "./base.ts"
import { isFormattableCpp } from "../format/cpp_common.ts"
import type { CheckOptions } from "./args.ts"

export type StepId = "guard" | "autofix" | "tools" | "build" | "tests" | "tidy" | "coop"

/// The order `check` runs the steps in. A step may be planned away (nothing to fix, a co-op-free
/// change) but never reordered: everything after `build` needs the binaries `build` produced.
export const STEP_ORDER: readonly StepId[] = [
  "guard",
  "autofix",
  "tools",
  "build",
  "tests",
  "tidy",
  "coop",
]

/// A step that cannot run because something before it failed is reported skipped, never passed.
export type StepStatus = "pass" | "fail" | "skipped"

export type ChangedFiles = {
  /// C++ the clang-format auto-fix and the tidy step should see.
  cpp: string[]
  /// JSON the JSON formatter should rewrite.
  json: string[]
  /// Everything, for the guard and the co-op trigger.
  all: string[]
}

const COOP_OR_NET = /^src\/(?:coop|net)[^/]*\.(?:cpp|h|hpp)$/
const CPP = /^(?:src|tests)\/.*\.(?:cpp|h|hpp)$/
/// The Deno tooling the gate itself runs on: a change to it is checked before the slow steps.
export const TOOLING_TS = /^(?:tools|scripts)\/.*\.ts$/

/// The test files to run for changed tooling: each changed `*_test.ts`, and the `*_test.ts` beside
/// each changed module that has one (`present` holds the ones that exist). Never the module
/// itself: a script without an `import.meta.main` guard would run its body inside the gate.
export function testFilesFor(files: readonly string[], present: ReadonlySet<string>): string[] {
  const tests = new Set<string>()
  for (const file of files) {
    if (file.endsWith("_test.ts")) tests.add(file)
    else {
      const own = file.replace(/\.ts$/, "_test.ts")
      if (present.has(own)) tests.add(own)
    }
  }
  return [...tests].sort()
}

/// Bucket a list of changed repo-relative paths. Deleted files are filtered out by the caller.
export function classifyFiles(files: readonly string[]): ChangedFiles {
  return {
    cpp: files.filter((f) => CPP.test(f) && isFormattableCpp(f)),
    json: files.filter((f) => f.endsWith(".json")),
    all: [...files],
  }
}

/// Which steps run for this change. `dryRun` keeps only the guard; `noFix` drops the auto-fix; a
/// change that touches no `src/coop*` or `src/net*` file drops the co-op run.
export function planSteps(options: CheckOptions, diff: ChangedFiles): StepId[] {
  const planned: StepId[] = ["guard"]
  if (!options.noFix && !options.dryRun && (diff.cpp.length > 0 || diff.json.length > 0)) {
    planned.push("autofix")
  }
  if (options.dryRun) return planned
  if (diff.all.some((f) => TOOLING_TS.test(f))) planned.push("tools")
  planned.push("build", "tests", "tidy")
  if (diff.all.some((f) => COOP_OR_NET.test(f))) planned.push("coop")
  return planned
}

export type StepEvent = {
  /// ISO timestamp; the caller supplies it so this stays pure.
  at: string
  step: StepId
  status: StepStatus
  durationMs: number
  /// One line of what happened: counts, the runner's summary, or why it was skipped.
  detail: string
  /// The diff base, repeated on every line so one log can span several bases.
  base: string
}

/// The stdout line for a step: status, name, timing, detail.
export function formatStep(event: StepEvent): string {
  const seconds = (event.durationMs / 1000).toFixed(1)
  return `${event.status.toUpperCase().padEnd(7)} ${event.step.padEnd(7)} ${
    seconds.padStart(7)
  }s  ${event.detail}`
}

/// Any failure fails the run; skipped steps neither pass nor fail.
export function exitCode(events: readonly Pick<StepEvent, "status">[]): number {
  return events.some((e) => e.status === "fail") ? 1 : 0
}

export { DEFAULT_BASE }
