/// Argument parsing for `deno task sweep`. Pure: no IO, no environment reads.
import type { Platform } from "./types.ts"

export type SweepArgs = {
  /// `--platform windows|macos`, required unless `--help`.
  platform?: Platform
  /// `--since <sha>`: pretend the watermark says this, for one run.
  since?: string
  /// `--dry-run`: print the plan, touch no issue and no watermark.
  dryRun: boolean
  /// `--runner fake`: script the test run instead of building and running the suite.
  runner: "real" | "fake"
  /// Names the fake runner reports as failing, for rehearsals.
  fakeFailures: string[]
  /// `--jobs N`: build parallelism.
  jobs: number
  /// `--log-dir`: where the sweep's logs and Catch2 reports go (default `.sweep/logs`).
  logDir?: string
  /// `--linux-log <path>`: the Linux `check` test log to compare failures against.
  linuxLog?: string
  help: boolean
}

export const USAGE = `usage: deno task sweep --platform windows|macos [--since <sha>] [--dry-run]
                     [--runner real|fake] [--fake-failures a,b] [--jobs N]
                     [--log-dir DIR] [--linux-log FILE]

A platform sweep: build natively, run the whole suite in-process, and open one
GitHub issue per platform-only failure. The commit range is whatever the
watermark (.sweep/<platform>.watermark) has not covered yet; a run that
completed moves the watermark to HEAD, a run that could not happen does not.

  --platform <p>        required: windows or macos
  --since <sha>         start here instead of at the watermark
  --dry-run             no build, no issues, no watermark write
  --runner fake         do not build or test; report --fake-failures instead
  --fake-failures a,b   case names the fake runner reports as failing
  --jobs N              build parallelism (default: all cores)
  --log-dir DIR         default .sweep/logs
  --linux-log FILE      Linux check log to subtract; default <sweep dir>/linux-check.log
  --help                this text

The sweep never blocks anything: exit 0 means the run happened, whatever it
found; exit 1 means it could not run and the watermark stayed put.`

const PLATFORMS: readonly string[] = ["windows", "macos"]

function needsValue(args: readonly string[], index: number, flag: string): string {
  const value = args[index + 1]
  if (value === undefined || value.startsWith("-")) {
    throw new Error(`${flag} needs a value`)
  }
  return value
}

export function parseArgs(args: readonly string[]): SweepArgs {
  const parsed: SweepArgs = {
    dryRun: false,
    runner: "real",
    fakeFailures: [],
    jobs: Math.max(1, navigator.hardwareConcurrency ?? 4),
    help: false,
  }
  for (let i = 0; i < args.length; i++) {
    const arg = args[i]
    switch (arg) {
      case "--platform": {
        const value = needsValue(args, i, arg)
        if (!PLATFORMS.includes(value)) {
          throw new Error(`--platform must be windows or macos, got: ${value}`)
        }
        parsed.platform = value as Platform
        i++
        break
      }
      case "--since": {
        const value = needsValue(args, i, arg)
        if (!/^[0-9a-fA-F]{7,40}$/.test(value)) {
          throw new Error(`--since must be a commit sha, got: ${value}`)
        }
        parsed.since = value
        i++
        break
      }
      case "--jobs": {
        const raw = needsValue(args, i, arg)
        if (!/^[1-9][0-9]*$/.test(raw)) throw new Error(`--jobs must be a positive integer: ${raw}`)
        parsed.jobs = Number(raw)
        i++
        break
      }
      case "--runner": {
        const value = needsValue(args, i, arg)
        if (value !== "real" && value !== "fake") {
          throw new Error(`--runner must be real or fake, got: ${value}`)
        }
        parsed.runner = value
        i++
        break
      }
      case "--fake-failures": {
        const raw = needsValue(args, i, arg)
        parsed.fakeFailures = raw.split(",").map((s) => s.trim()).filter((s) => s !== "")
        i++
        break
      }
      case "--log-dir":
        parsed.logDir = needsValue(args, i, arg)
        i++
        break
      case "--linux-log":
        parsed.linuxLog = needsValue(args, i, arg)
        i++
        break
      case "--dry-run":
        parsed.dryRun = true
        break
      case "--help":
      case "-h":
        parsed.help = true
        break
      default:
        throw new Error(`unknown argument: ${arg}`)
    }
  }
  if (!parsed.help && parsed.platform === undefined) throw new Error("--platform is required")
  return parsed
}
