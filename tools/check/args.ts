/// Argument parsing for `deno task check`. Pure: no IO, no environment reads.
import { DEFAULT_BASE } from "./steps.ts"

export type CheckOptions = {
  /// `--base <ref>`: what to diff against.
  baseFlag?: string
  /// `--no-fix`: skip the auto-fix step (CI runs with it).
  noFix: boolean
  /// `--jobs N`: parallelism for the build and the forked test runner.
  jobs?: number
  /// `--dry-run`: run the guard and print the step plan, touch no compiler.
  dryRun: boolean
  /// `--macos`: the opt-in macOS compile gate (guard, tools, osx-arm-slim build); runs on a Mac.
  macos: boolean
  help: boolean
}

export const USAGE =
  `usage: deno task check [--base <ref>] [--no-fix] [--jobs N] [--dry-run] [--macos]

One command, one exit code: the whole definition of done for a change.
Steps, in order: guard, autofix, tools, build, tests, tidy, coop. A failed guard or
build skips what follows. Every step is timed, printed on one line and appended
to <CHECK_LOG_DIR>/events.jsonl (default ./.check/).

  --base <ref>   diff base for the guard, the auto-fix and tidy
                 (default ${DEFAULT_BASE})
  --no-fix       do not rewrite files
  --jobs N       build and test parallelism (default nproc)
  --dry-run      guard + step plan only, no configure, build or test
  --macos        on a Mac only: the compile gate for include and header changes. Runs guard,
                 tools and a build of both binaries on osx-arm-slim (the Linux build never
                 compiles TILES or __APPLE__ branches). Runs no tests. Writes
                 <CHECK_LOG_DIR>/macos-passed, never the box's \`passed\` stamp.
  --help         this text

Environment: CHECK_LOG_DIR, CHECK_SEED_DIR, CHECK_ALLOW_PROTECTED=1.`

function needsValue(args: readonly string[], index: number, flag: string): string {
  const value = args[index + 1]
  if (value === undefined || value.startsWith("-")) {
    throw new Error(`${flag} needs a value`)
  }
  return value
}

export function parseArgs(args: readonly string[]): CheckOptions {
  const options: CheckOptions = { noFix: false, dryRun: false, macos: false, help: false }
  for (let i = 0; i < args.length; i++) {
    const arg = args[i]
    switch (arg) {
      case "--base":
        options.baseFlag = needsValue(args, i, arg)
        i++
        break
      case "--jobs": {
        const raw = needsValue(args, i, arg)
        if (!/^[1-9][0-9]*$/.test(raw)) throw new Error(`--jobs must be a positive integer: ${raw}`)
        options.jobs = Number(raw)
        i++
        break
      }
      case "--no-fix":
        options.noFix = true
        break
      case "--macos":
        options.macos = true
        break
      case "--dry-run":
        options.dryRun = true
        break
      case "--help":
      case "-h":
        options.help = true
        break
      default:
        throw new Error(`unknown argument: ${arg}`)
    }
  }
  return options
}
