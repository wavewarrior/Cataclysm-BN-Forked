/// check: one command, one exit code, the whole definition of done for a change.
///
///   deno task check [--base <ref>] [--no-fix] [--jobs N] [--dry-run]
///
/// Written for the Linux executor (WSL2 Ubuntu 24.04, LLVM 22 under /usr/lib/llvm-22/bin, 32
/// cores): step ordering, guard matching and the event record are pure and unit-tested in
/// steps_test.ts, guard_test.ts, args_test.ts and seed_test.ts; this file only wires them to git,
/// cmake, clang-format and the test binary.
///
/// Environment:
///   CHECK_LOG_DIR            where events.jsonl and the step logs go (default ./.check)
///   CHECK_SEED_DIR           a build dir whose _deps seed an offline configure
///   CHECK_ALLOW_PROTECTED=1  let a human run check over a change that edits protected paths
import { join, relative } from "@std/path"
import { DEFAULT_BASE, diffRange } from "./base.ts"
import { parseArgs, USAGE } from "./args.ts"
import { type Env, run, runTee, tail } from "./exec.ts"
import { guardViolations } from "./guard.ts"
import { seedArguments } from "./seed.ts"
import {
  classifyFiles,
  exitCode,
  formatStep,
  planSteps,
  type StepEvent,
  type StepId,
  type StepStatus,
} from "./steps.ts"

const PRESET = "linux-fast"
const TEST_BIN = `out/build/${PRESET}/tests/cata_test-tiles`
const GAME_BIN = `out/build/${PRESET}/src/cataclysm-bn-tiles`

const flag = (name: string): string | undefined => Deno.env.get(name) || undefined

/// What the pre-push hook trusts: the commit this run tested, written only when every step passed
/// and the working tree matched that commit (an edited tree proves nothing about the commit).
async function writePassStamp(logDir: string): Promise<void> {
  const dirty = (await run(["git", "status", "--porcelain", "--untracked-files=no"])).stdout.trim()
  if (dirty !== "") return
  const head = (await run(["git", "rev-parse", "HEAD"])).stdout.trim()
  if (head !== "") await Deno.writeTextFile(join(logDir, "passed"), head + "\n")
}

async function gitLines(args: string[]): Promise<string[]> {
  const result = await run(["git", ...args])
  if (result.code !== 0) throw new Error(`git ${args.join(" ")}: ${result.stderr.trim()}`)
  return result.stdout.split("\n").filter((l) => l.trim() !== "")
}

/// What this change touched: the diff against the base plus anything untracked. `--base HEAD`
/// compares the working tree with the commit it sits on, which is what a dry run wants.
async function changedFiles(base: string): Promise<string[]> {
  const tracked = await gitLines([
    "diff",
    "--name-only",
    "--diff-filter=d",
    ...(base === "HEAD" ? ["HEAD"] : [diffRange(base)]),
  ])
  const untracked = await gitLines(["ls-files", "--others", "--exclude-standard"])
  return [...new Set([...tracked, ...untracked])].sort()
}

/// Cheap content fingerprint, so the auto-fix can report how many files it actually rewrote.
async function fingerprint(file: string): Promise<string> {
  try {
    const text = await Deno.readTextFile(file)
    return `${text.length}:${text.slice(-64)}`
  } catch {
    return "unreadable"
  }
}

/// The line a human wants out of a test run: the fork runner's totals when it printed them,
/// otherwise the last thing the process said.
export function testSummary(stdout: string, code: number): string {
  const lines = stdout.split("\n").map((l) => l.trim()).filter((l) => l !== "")
  const totals = lines.findLast((l) => l.includes("cases in forked children"))
  return `${totals ?? lines.at(-1) ?? "no output"} (exit ${code})`
}

/// The window-system variables a forked test child must not share with its parent (plan: every
/// Linux test run and lane unsets these, or the parent core-dumps at exit).
const forkSafeEnv: Env = {
  DISPLAY: undefined,
  WAYLAND_DISPLAY: undefined,
  SDL_VIDEODRIVER: undefined,
}

async function seedEntries(seedDir: string): Promise<string[]> {
  const entries: string[] = []
  try {
    for await (const entry of Deno.readDir(join(seedDir, "_deps"))) {
      if (entry.isDirectory) entries.push(entry.name)
    }
  } catch {
    return []
  }
  return entries
}

async function main(): Promise<number> {
  const options = parseArgs(Deno.args)
  if (options.help) {
    console.log(USAGE)
    return 0
  }
  const base = options.baseFlag ?? flag("CHECK_BASE") ?? DEFAULT_BASE
  const logDir = flag("CHECK_LOG_DIR") ?? ".check"
  const jobs = options.jobs ?? navigator.hardwareConcurrency ?? 4
  const events: StepEvent[] = []
  const logPath = join(logDir, "events.jsonl")
  await Deno.mkdir(logDir, { recursive: true })

  const record = async (step: StepId, status: StepStatus, started: number, detail: string) => {
    const event: StepEvent = {
      at: new Date().toISOString(),
      step,
      status,
      durationMs: Date.now() - started,
      detail,
      base,
    }
    events.push(event)
    console.log(formatStep(event))
    await Deno.writeTextFile(logPath, JSON.stringify(event) + "\n", {
      create: true,
      append: true,
    })
  }

  const diff = classifyFiles(await changedFiles(base))
  const planned = planSteps(options, diff)
  const skipRest = async (reason: string, from: number) => {
    for (const step of planned.slice(from)) await record(step, "skipped", Date.now(), reason)
  }

  // (a) The guard.
  let started = Date.now()
  const violations = guardViolations(diff.all)
  if (violations.length > 0 && flag("CHECK_ALLOW_PROTECTED") !== "1") {
    await record("guard", "fail", started, `${violations.length} protected file(s) changed`)
    for (const file of violations) console.error(`  protected: ${file}`)
    await skipRest("the guard failed", 1)
    return exitCode(events)
  }
  const protectedNote = violations.length > 0
    ? `${violations.length} protected, allowed by CHECK_ALLOW_PROTECTED`
    : "none protected"
  await record("guard", "pass", started, `${diff.all.length} changed file(s), ${protectedNote}`)
  if (options.dryRun) {
    console.log(`dry run: planned ${planned.join(", ")}; base ${base}`)
    return 0
  }

  // (b) Auto-fix. Formatting is never a gate failure: a broken formatter is reported, not fatal.
  started = Date.now()
  if (planned.includes("autofix")) {
    const candidates = [...diff.cpp, ...diff.json]
    const before = await Promise.all(candidates.map(fingerprint))
    const warnings: string[] = []
    if (diff.cpp.length > 0) {
      const formatted = await run(["deno", "task", "fmt:cpp", ...diff.cpp])
      if (formatted.code !== 0) warnings.push(`fmt:cpp exit ${formatted.code}`)
    }
    if (diff.json.length > 0) {
      const formatted = await run(["bash", "build-scripts/format-json.sh", ...diff.json])
      if (formatted.code !== 0) warnings.push(`json formatter exit ${formatted.code}`)
    }
    const rewritten = (await Promise.all(candidates.map(fingerprint)))
      .filter((value, index) => value !== before[index]).length
    await record(
      "autofix",
      "pass",
      started,
      `${rewritten}/${candidates.length} file(s) rewritten${
        warnings.length > 0 ? `; warnings: ${warnings.join(", ")}` : ""
      }`,
    )
  }

  // (c) Configure (offline when a seed dir is given) and build both shipping binaries.
  started = Date.now()
  const seedDir = flag("CHECK_SEED_DIR")
  const seed = seedDir === undefined
    ? []
    : seedArguments(join(seedDir, "_deps"), await seedEntries(seedDir))
  const configured = await runTee(
    ["cmake", "--preset", PRESET, ...seed],
    join(logDir, "configure.log"),
  )
  const built = configured.code === 0
    ? await runTee(
      ["cmake", "--build", "--preset", PRESET, "-j", String(jobs)],
      join(logDir, "build.log"),
    )
    : { code: configured.code }
  if (configured.code !== 0 || built.code !== 0) {
    const log = configured.code !== 0 ? "configure.log" : "build.log"
    const first = (await tail(join(logDir, log), 5)).split("\n")[0] ?? ""
    await record("build", "fail", started, `exit ${built.code}: ${first.slice(0, 160)}`)
    await skipRest("the build failed", 2)
    return exitCode(events)
  }
  await record("build", "pass", started, `preset ${PRESET} with ${jobs} jobs`)

  // (d) The whole suite through the fork runner: one seed, every core on it.
  started = Date.now()
  const tests = await runTee([TEST_BIN, "--rng-seed", "1"], join(logDir, "tests.log"), {
    env: { ...forkSafeEnv, CATA_TEST_FORK_JOBS: String(jobs) },
  })
  await record(
    "tests",
    tests.code === 0 ? "pass" : "fail",
    started,
    testSummary(tests.stdout, tests.code),
  )
  if (tests.code !== 0) {
    await skipRest("the suite failed", 3)
    return exitCode(events)
  }

  // (e) Correctness tidy on the changed lines only, as its own task so the plugin stays swappable.
  started = Date.now()
  const tidy = await runTee(
    ["deno", "task", "tidy:changed", "--base", base],
    join(logDir, "tidy.log"),
  )
  await record("tidy", tidy.code === 0 ? "pass" : "fail", started, `exit ${tidy.code}`)

  // (f) The two-process co-op scenarios, only for a change under src/coop* or src/net*.
  if (planned.includes("coop")) {
    started = Date.now()
    const coop = await runTee(["deno", "task", "test:coop"], join(logDir, "coop.log"), {
      env: { ...forkSafeEnv, CBN_TEST_BIN: TEST_BIN, CATA_EXE: GAME_BIN },
    })
    await record("coop", coop.code === 0 ? "pass" : "fail", started, `exit ${coop.code}`)
  }

  const code = exitCode(events)
  if (code === 0) await writePassStamp(logDir)
  return code
}

if (import.meta.main) {
  try {
    const root = (await run(["git", "rev-parse", "--show-toplevel"])).stdout.trim()
    if (root !== "" && !relative(root, Deno.cwd()).startsWith("..")) Deno.chdir(root)
    Deno.exit(await main())
  } catch (error) {
    console.error(`check: ${error instanceof Error ? error.message : error}`)
    Deno.exit(2)
  }
}
