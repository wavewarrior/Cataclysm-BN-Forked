/// sweep: a platform sweep (GLOSSARY: Platform sweep) — build natively, run the whole suite
/// in-process, file one issue per platform-only failure, move the watermark.
///
///   deno task sweep --platform windows [--since <sha>] [--dry-run] [--runner fake]
///
/// The Windows sweep runs from a scheduled task on the box (tools/sweep/windows/install-task.ps1);
/// the macOS one is opt-in: the owner runs it when they feel like it. Neither ever blocks a lane:
/// `check` stays the only gate. The policy lives in the sibling modules, each unit-tested with
/// injected fakes; this file only wires them to git, cmake, the test binary and `gh`.
///
/// Environment:
///   SWEEP_REPO      file issues against this repo (default wavewarrior/Cataclysm-BN-Forked)
///   SWEEP_DIR       where the watermarks live (default <repo>/.sweep)
///   SWEEP_LINUX_LOG the Linux check test log to subtract (default <SWEEP_DIR>/linux-check.log,
///                   then .check/tests.log, the log `check` leaves behind)
import { join, relative } from "@std/path"
import { run } from "../check/exec.ts"
import { parseArgs, USAGE } from "./args.ts"
import { ghCli, repoFromEnv } from "./github.ts"
import { lastCppCommitMs, readUserPresets, sweepPlan } from "./plan.ts"
import { fakeRunner, realRunner } from "./runner.ts"
import { runSweep } from "./sweep.ts"
import { todayIso } from "./quarantine.ts"
import type { Gh, Runner } from "./types.ts"

/// Where the Linux baseline lives: the box pushes its `check` test log here after each green-ish
/// run; the first readable candidate wins. A sweep with no baseline still runs, but says in every
/// body that it could not exclude cross-platform failures.
async function linuxLog(candidates: string[]): Promise<string> {
  for (const path of candidates) {
    try {
      await Deno.stat(path)
      return path
    } catch {
      // try the next candidate
    }
  }
  return candidates[0]
}

async function main(): Promise<number> {
  const options = parseArgs(Deno.args)
  if (options.help) {
    console.log(USAGE)
    return 0
  }
  if (options.platform === undefined) {
    console.error("sweep: --platform windows|macos is required\n")
    console.error(USAGE)
    return 2
  }
  const repoRoot = (await run(["git", "rev-parse", "--show-toplevel"])).stdout.trim()
  if (repoRoot === "") throw new Error("not inside a git repository")
  const sweepDir = Deno.env.get("SWEEP_DIR") || join(repoRoot, ".sweep")
  const logDir = options.logDir ?? join(sweepDir, "logs")
  await Deno.mkdir(logDir, { recursive: true })

  const userPresets = options.platform === "windows" ? await readUserPresets(repoRoot) : null
  const plan = sweepPlan(options.platform, repoRoot, logDir, userPresets)
  const log = (line: string) => console.log(`[sweep ${options.platform}] ${line}`)
  log(`plan: ${plan.configure.join(" ")} && ${plan.build.join(" ")}`)
  for (const test of plan.tests) log(`  run: ${test.args.join(" ")}`)

  let runner: Runner
  let gh: Gh
  if (options.runner === "fake") {
    runner = fakeRunner(options.fakeFailures)
    gh = fakeGh(log)
  } else {
    const cppFloorMs = await lastCppCommitMs(repoRoot)
    runner = (p) => realRunner(p, { logDir, jobs: options.jobs, cppFloorMs })
    gh = ghCli(repoFromEnv())
  }

  const baseline = options.linuxLog ??
    await linuxLog([
      Deno.env.get("SWEEP_LINUX_LOG") ?? join(sweepDir, "linux-check.log"),
      join(repoRoot, ".check", "tests.log"),
    ])

  const outcome = await runSweep({
    platform: options.platform,
    repoRoot,
    sweepDir,
    plan,
    runner,
    gh,
    since: options.since,
    dryRun: options.dryRun,
    linuxLog: baseline,
    today: todayIso(),
    log,
  })
  console.log(`sweep ${options.platform}: ${outcome.status} — ${outcome.reason}`)
  if (outcome.status === "could-not-run") return 1
  return 0
}

/// The GitHub face for `--runner fake`: records instead of calling gh, so a rehearsal on a laptop
/// (or the box, before the label exists) exercises the whole pipeline without touching GitHub.
function fakeGh(log: (line: string) => void): Gh {
  const opened = new Map<string, number>()
  let next = 9000
  return {
    openIssues: () =>
      Promise.resolve([...opened.entries()].map(([title, number]) => ({ number, title }))),
    createIssue(title: string, _body: string, _label: string): Promise<number> {
      const number = ++next
      opened.set(title, number)
      log(`fake gh: would open "${title}" (#${number} simulated)`)
      return Promise.resolve(number)
    },
    commentIssue(number: number, _body: string): Promise<void> {
      log(`fake gh: would comment on #${number}`)
      return Promise.resolve()
    },
  }
}

if (import.meta.main) {
  try {
    const root = (await run(["git", "rev-parse", "--show-toplevel"])).stdout.trim()
    if (root !== "" && !relative(root, Deno.cwd()).startsWith("..")) Deno.chdir(root)
    Deno.exit(await main())
  } catch (error) {
    console.error(`sweep: ${error instanceof Error ? error.message : error}`)
    Deno.exit(2)
  }
}
