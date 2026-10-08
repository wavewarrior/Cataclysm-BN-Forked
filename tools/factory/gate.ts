/// The single definition of "done" for a factory lane.
///
///   deno task gate [--tier fast|full] [--base <ref>]
///
/// Runs every step with a captured log under `<gitdir>/factory/logs/`, prints one line per step and
/// writes `<gitdir>/factory/gate-stamp.json`. The omp hook, the pre-push hook and CI all call or
/// mirror this file; nothing else decides whether a ticket is finished.
import { Command } from "@cliffy/command"
import { exists } from "@std/fs"
import { join } from "@std/path"
import { config } from "./config.ts"
import { changedFiles, violations } from "./protected_paths.ts"
import { describe as describeRun, evaluateRun, grown } from "./ratchet.ts"
import { listItems, type Ticket } from "./ticket.ts"
import {
  factoryDir,
  fmtSeconds,
  git,
  mainRepoRoot,
  run,
  runToLog,
  runToLogUntil,
  tailFile,
} from "./util.ts"
import { runWslLane } from "./wsl.ts"

export type Tier = "fast" | "full"

export type Stamp = {
  headSha: string
  baseSha: string
  tier: Tier
  ok: boolean
  finishedAt: string
  failedSteps: string[]
  /// Failed steps whose cause is the tooling, not the change (see StepResult.infra).
  infraSteps: string[]
  ticket: number | null
}

/// `infra`: the failure is in the factory's tooling or machine, not in the change; retrying the
/// implementer cannot fix it, so the driver blocks the ticket at once.
export type StepResult = { ok: boolean; note?: string; infra?: boolean }

export type GateContext = {
  cwd: string
  tier: Tier
  base: string
  headSha: string
  baseSha: string
  factoryDir: string
  /// From `<gitdir>/factory/ticket.json`, written by the driver; absent for a manual run.
  ticket: Ticket | undefined
  logPath: (step: string) => string
  /// Run a command, appending to the step's log. Returns the exit code.
  exec: (step: string, cmd: string[], env?: Record<string, string>) => Promise<number>
}

export type Step = {
  name: string
  tier: Tier
  /// Minutes-long: skipped once any earlier step has failed, so a cheap failure returns fast.
  slow?: boolean
  run: (ctx: GateContext) => Promise<StepResult>
}

const GIT_BASH = "C:/Program Files/Git/bin/bash.exe"
const BUILD_PRESET = "win-rel"
const BUILD_DIR = "out/msvc"
const KEEP_CWD = { BN_KEEP_CWD: "1" }

const needTicket = (ctx: GateContext): StepResult | undefined =>
  ctx.ticket ? undefined : { ok: false, note: "no ticket.json: run the gate through the driver" }

/// The newest `cata_test-tiles.exe` under this worktree's own build dir. A root-level or
/// other-worktree leftover would silently test code that no longer exists, so never look there.
export async function findTestExe(cwd: string): Promise<string | undefined> {
  const root = join(cwd, BUILD_DIR)
  if (!(await exists(root))) return undefined
  let best: { path: string; mtime: number } | undefined
  const walk = async (dir: string, depth: number): Promise<void> => {
    if (depth > 4) return
    for await (const entry of Deno.readDir(dir)) {
      const path = join(dir, entry.name)
      if (entry.isDirectory && !["_deps", "CMakeFiles", "vcpkg_installed"].includes(entry.name)) {
        await walk(path, depth + 1)
      } else if (entry.isFile && entry.name.toLowerCase() === "cata_test-tiles.exe") {
        const mtime = (await Deno.stat(path)).mtime?.getTime() ?? 0
        if (!best || mtime > best.mtime) best = { path, mtime }
      }
    }
  }
  await walk(root, 0)
  return best?.path
}

const STEPS: Step[] = [
  {
    name: "clean-tree",
    tier: "fast",
    run: async (ctx) => {
      const status = await git(ctx.cwd, "status", "--porcelain")
      if (
        status === "" && (await git(ctx.cwd, "rev-list", "--count", `${ctx.base}..HEAD`)) === "0"
      ) {
        return {
          ok: false,
          note: "the branch has no commits beyond the base; nothing was implemented",
        }
      }
      return status === ""
        ? { ok: true }
        : { ok: false, note: `uncommitted or untracked files; commit them first:\n${status}` }
    },
  },
  {
    name: "protected-paths",
    tier: "fast",
    run: async (ctx) => {
      const bad = violations(await changedFiles(ctx.cwd, ctx.base), config.protectedPaths)
      return bad.length === 0
        ? { ok: true }
        : { ok: false, note: bad.map((v) => `${v.file} (${v.glob})`).join("\n") }
    },
  },
  {
    name: "new-test-present",
    tier: "fast",
    run: async (ctx) => {
      const files = await changedFiles(ctx.cwd, ctx.base)
      const touchesSrc = files.some((f) => f.startsWith("src/"))
      const touchesTests = files.some((f) =>
        f.startsWith("tests/") || f.startsWith("data/mods/TEST_DATA/")
      )
      if (!touchesSrc || touchesTests) return { ok: true }
      const reason = ctx.ticket?.sections["No test needed"]?.trim()
      return reason ? { ok: true, note: `no test needed: ${reason}` } : {
        ok: false,
        note:
          "src/** changed without tests/**; add a test or a '## No test needed' section on the ticket",
      }
    },
  },
  {
    name: "deno-checks",
    tier: "fast",
    run: async (ctx) => {
      // fmt and lint look at the files this change touches: the tree has pre-existing drift
      // (60 unformatted files, 4 lint findings) that a lane must neither fix nor be blamed for.
      // `deno test` runs everything. One at a time: they share a log.
      const changed: string[] = []
      for (const f of await changedFiles(ctx.cwd, ctx.base)) {
        if (await exists(join(ctx.cwd, f), { isFile: true })) changed.push(f)
      }
      const fmtFiles = changed.filter((f) => /\.(?:ts|tsx|js|jsonc?|md|ya?ml)$/.test(f))
      const lintFiles = changed.filter((f) => /\.(?:ts|tsx|js)$/.test(f))
      const codes: number[] = []
      for (
        const [files, cmd] of [
          [fmtFiles, ["fmt", "--check"]],
          [lintFiles, ["lint"]],
          [[], ["test", "-A"]],
        ] as [string[], string[]][]
      ) {
        if (cmd[0] !== "test" && files.length === 0) continue
        const log = ctx.logPath("deno-checks")
        const r = await run(["deno", ...cmd, ...files], { cwd: ctx.cwd })
        await Deno.writeTextFile(
          log,
          `$ deno ${cmd.join(" ")} ${files.join(" ")}\n${r.stdout}${r.stderr}\n[exit ${r.code}]\n`,
          { append: true },
        )
        // Files outside deno.jsonc's include list are skipped; none left is not a failure.
        codes.push(/No target files found/.test(r.stderr) ? 0 : r.code)
      }
      return { ok: codes.every((c) => c === 0) }
    },
  },
  {
    name: "json-lint",
    tier: "fast",
    slow: true,
    run: async (ctx) => {
      // Unchanged JSON was validated when it landed; checking all of it costs ~2.5 minutes a run.
      const changed = (await changedFiles(ctx.cwd, ctx.base)).filter((f) => f.endsWith(".json"))
      if (changed.length === 0) return { ok: true, note: "no JSON changed" }
      if (!(await exists(GIT_BASH))) {
        return { ok: false, infra: true, note: `${GIT_BASH} not found` }
      }
      // lint-json.sh prefers `python3`, and the Microsoft Store alias in WindowsApps answers to
      // it but fails on every file. Drop WindowsApps and put a real interpreter first.
      const path = [
        "C:\\Python312",
        ...(Deno.env.get("PATH") ?? "").split(";").filter((p) => p && !p.includes("WindowsApps")),
      ].join(";")
      const code = await ctx.exec("json-lint", [GIT_BASH, "build-scripts/lint-json.sh"], {
        PATH: path,
      })
      if (code === 0) return { ok: true }
      // Only a failure on a file this change touched is the implementer's to fix.
      const log = await Deno.readTextFile(ctx.logPath("json-lint")).catch(() => "")
      const failing = new Set([...log.matchAll(/^FAILED: (.+)$/gm)].map((m) => m[1].trim()))
      const mine = changed.filter((f) => failing.has(f))
      return mine.length > 0
        ? { ok: false, note: `invalid JSON in: ${mine.join(", ")}` }
        : { ok: false, infra: true, note: "lint-json failed on files this change did not touch" }
    },
  },
  {
    name: "wsl-lane",
    tier: "fast",
    slow: true,
    run: async (ctx) => {
      const touched = (await changedFiles(ctx.cwd, ctx.base)).some((f) =>
        /\.(?:cpp|h|hpp|json)$/.test(f)
      )
      if (!touched) return { ok: true, note: "no C++ or JSON changed" }
      const code = await runWslLane({
        cwd: ctx.cwd,
        base: ctx.base,
        factoryDir: ctx.factoryDir,
        logPath: ctx.logPath("wsl-lane"),
      })
      // lane.sh exits 1 for findings and 2 when its own setup broke (fetch, plugin, configure).
      return { ok: code === 0, infra: code === 2 }
    },
  },
  {
    name: "build",
    tier: "full",
    run: async (ctx) => {
      // The lane's own out/ dir. Re-run configure every time (cheap once cached) so the format
      // and Lua-doc targets stay OFF: LUA_DOCS_ON_BUILD runs `deno task docs:gen`, which reformats
      // the whole tracked tree after every build.
      const cfg = await ctx.exec("build", [
        "cmd",
        "/c",
        "C:\\WORK\\bnenv.bat",
        "cmake",
        "--preset",
        "win",
        "-DCATA_FORMAT_TARGETS=OFF",
        "-DLUA_DOCS_ON_BUILD=OFF",
      ], KEEP_CWD)
      if (cfg !== 0) return { ok: false, infra: true, note: "cmake configure failed" }
      const code = await ctx.exec(
        "build",
        ["cmd", "/c", "C:\\WORK\\bnbuild.bat", BUILD_PRESET],
        KEEP_CWD,
      )
      if (code !== 0) return { ok: false }
      const dirty = await git(ctx.cwd, "status", "--porcelain")
      return dirty === ""
        ? { ok: true }
        : { ok: false, infra: true, note: `the build modified the tree:\n${dirty}` }
    },
  },
  {
    name: "catch2-tags",
    tier: "full",
    run: async (ctx) => {
      const missing = needTicket(ctx)
      if (missing) return missing
      const tags = listItems(ctx.ticket!.sections["Test tags"])
      if (tags.length === 0) return { ok: false, note: "the ticket lists no '## Test tags'" }
      const exe = await findTestExe(ctx.cwd)
      if (!exe) return { ok: false, note: `no cata_test-tiles.exe under ${BUILD_DIR}` }
      for (const tag of tags) {
        await Deno.remove(join(ctx.cwd, "test_user_dir"), { recursive: true }).catch(() => {})
        const { code } = await runToLogUntil(
          [
            "deno",
            "task",
            "test:progress",
            "--exe",
            exe,
            "--log",
            join(ctx.factoryDir, "logs", "catch2-tag.log"),
            tag,
            "--rng-seed",
            "1",
          ],
          ctx.logPath("catch2-tags"),
          (line) => line.startsWith("FAIL "),
          { cwd: ctx.cwd },
        )
        const log = await Deno.readTextFile(ctx.logPath("catch2-tags")).catch(() => "")
        const ran = [...log.matchAll(/^(\d+) test cases;/gm)].at(-1)?.[1]
        if (ran === undefined || Number(ran) === 0) {
          return { ok: false, note: `tag ${tag} matched no test cases (typo?)` }
        }
        if (code !== 0) return { ok: false, note: `tag ${tag} failed` }
      }
      return { ok: true, note: `tags: ${tags.join(" ")}` }
    },
  },
  {
    name: "baseline-ratchet",
    tier: "full",
    run: async (ctx) => {
      const exe = await findTestExe(ctx.cwd)
      if (!exe) return { ok: false, note: `no cata_test-tiles.exe under ${BUILD_DIR}` }
      await Deno.remove(join(ctx.cwd, "test_user_dir"), { recursive: true }).catch(() => {})
      await runToLogUntil(
        [
          "deno",
          "task",
          "test:progress",
          "--exe",
          exe,
          "--log",
          join(ctx.factoryDir, "logs", "catch2-full.log"),
          "~[.]",
          "--rng-seed",
          "1",
        ],
        ctx.logPath("baseline-ratchet"),
        // Stop at the first failure that is not in the baseline: the rest of a 12-minute run
        // cannot change the verdict.
        (line) =>
          line.startsWith("FAIL ") && !config.baselineFailures.includes(line.slice(5).trim()),
        { cwd: ctx.cwd },
      )
      const log = await Deno.readTextFile(ctx.logPath("baseline-ratchet"))
      const result = evaluateRun(log, config.baselineFailures)
      const base = await run(["git", "show", `${ctx.base}:tools/factory/config.json`], {
        cwd: ctx.cwd,
      })
      const added = base.code === 0
        ? grown(JSON.parse(base.stdout).baselineFailures, config.baselineFailures)
        : []
      const notes = [describeRun(result)]
      if (added.length) notes.push(`baselineFailures grew versus ${ctx.base}: ${added.join(", ")}`)
      return { ok: result.ok && added.length === 0, note: notes.join("\n") }
    },
  },
  {
    name: "bnplay",
    tier: "full",
    run: async (ctx) => {
      const needs = ctx.ticket?.labels.some((l) => l === "gameplay" || l === "render")
      if (!needs) return { ok: true, note: "ticket is not gameplay/render" }
      const trials = listItems(ctx.ticket?.sections["Episodes"])
      if (trials.length === 0) {
        return { ok: false, note: "gameplay/render ticket lists no '## Episodes'" }
      }
      // The fixture library is gitignored, so a fresh worktree has none: use the main checkout's.
      // Home, binary and basepath default to this worktree, so each lane runs its own daemon.
      const env = {
        BNPLAY_FIXTURES: join(await mainRepoRoot(ctx.cwd), "tools", "bnplay", "fixtures"),
      }
      const bn = async (...args: string[]) => {
        const r = await run(["deno", "task", "bnplay", ...args], { cwd: ctx.cwd, env })
        await Deno.writeTextFile(
          ctx.logPath("bnplay"),
          `bnplay ${args.join(" ")}\n${r.stdout}${r.stderr}\n`,
          {
            append: true,
          },
        )
        return r
      }
      try {
        if ((await bn("doctor")).code !== 0) {
          return { ok: false, note: "bnplay doctor is unhealthy" }
        }
        // An entry is `<trial.toml>` or `<trial.toml> <steps.jsonl>`: one driver request per line is
        // sent with `bnplay step` before `stop`. Without steps this only proves boot and clean exit.
        for (const entry of trials) {
          const [trial, stepsFile] = entry.split(/\s+/)
          const started = await bn("start", trial)
          const session = started.code === 0
            ? JSON.parse(started.stdout.trim().split("\n").at(-1)!).session
            : undefined
          if (!session) return { ok: false, note: `could not start Episode ${trial}` }
          if (stepsFile) {
            const lines = (await Deno.readTextFile(join(ctx.cwd, stepsFile))).split(/\r?\n/).filter(
              (l) => l.trim(),
            )
            for (const line of lines) {
              const stepped = await bn("step", String(session), line)
              if (stepped.code !== 0) {
                await bn("stop", String(session))
                return { ok: false, note: `Episode ${trial}: step refused: ${line}` }
              }
            }
          }
          // 0 pass; 1 oracle failed, 2 harness error, 3 inconclusive all fail the gate.
          const stopped = await bn("stop", String(session))
          if (stopped.code !== 0) {
            return { ok: false, note: `Episode ${trial} verdict ${stopped.code}` }
          }
        }
      } finally {
        await bn("shutdown")
      }
      return { ok: true, note: `${trials.length} Episode(s) passed` }
    },
  },
]

export async function loadTicket(dir: string): Promise<Ticket | undefined> {
  try {
    return JSON.parse(await Deno.readTextFile(join(dir, "ticket.json"))) as Ticket
  } catch {
    return undefined
  }
}

export type GateOptions = {
  cwd: string
  tier: Tier
  base: string
  /// Run just these steps (CI reuses single steps). Writes no stamp: a partial run proves nothing.
  only?: string[]
}

/// Run the gate; returns the stamp (also written to disk).
export async function runGate(opts: GateOptions): Promise<Stamp> {
  if (opts.only) {
    const unknown = opts.only.filter((name) => !STEPS.some((s) => s.name === name))
    if (unknown.length > 0) {
      throw new Error(
        `unknown gate step(s): ${unknown.join(", ")}; known: ${
          STEPS.map((s) => s.name).join(", ")
        }`,
      )
    }
  }
  const fdir = await factoryDir(opts.cwd)
  const logs = join(fdir, "logs")
  // A partial run must not destroy the evidence of a full one.
  if (!opts.only) {
    await Deno.remove(logs, { recursive: true }).catch(() => {})
    await Deno.remove(join(fdir, "gate-stamp.json")).catch(() => {})
  }
  await Deno.mkdir(logs, { recursive: true })
  const ctx: GateContext = {
    cwd: opts.cwd,
    tier: opts.tier,
    base: opts.base,
    headSha: await git(opts.cwd, "rev-parse", "HEAD"),
    baseSha: await git(opts.cwd, "rev-parse", opts.base),
    factoryDir: fdir,
    ticket: await loadTicket(fdir),
    logPath: (step) => join(logs, `${step}.log`),
    exec: (step, cmd, env) => runToLog(cmd, join(logs, `${step}.log`), { cwd: opts.cwd, env }),
  }
  const failed: string[] = []
  const infra: string[] = []
  for (const step of STEPS) {
    if (opts.only) {
      if (!opts.only.includes(step.name)) continue
    } else if (step.tier === "full" && opts.tier === "fast") continue
    if ((step.tier === "full" || step.slow) && failed.length > 0) {
      console.log(`SKIP ${step.name} (an earlier step failed)`)
      continue
    }
    const t0 = Date.now()
    let result: StepResult
    try {
      result = await step.run(ctx)
    } catch (error) {
      result = { ok: false, note: `internal error: ${error}` }
    }
    const took = fmtSeconds(Date.now() - t0)
    console.log(
      `${result.ok ? "PASS" : "FAIL"} ${step.name} ${took}${
        result.note && result.ok ? `  (${result.note.split("\n")[0]})` : ""
      }`,
    )
    if (!result.ok) {
      failed.push(step.name)
      if (result.infra) infra.push(step.name)
      if (result.note) console.log(result.note)
      console.log(await tailFile(ctx.logPath(step.name), 60))
    }
  }
  const stamp: Stamp = {
    headSha: ctx.headSha,
    baseSha: ctx.baseSha,
    tier: opts.tier,
    ok: failed.length === 0,
    finishedAt: new Date().toISOString(),
    failedSteps: failed,
    infraSteps: infra,
    ticket: ctx.ticket?.number ?? null,
  }
  if (!opts.only) {
    await Deno.writeTextFile(join(fdir, "gate-stamp.json"), JSON.stringify(stamp, null, 2))
  }
  return stamp
}

if (import.meta.main) {
  await new Command()
    .name("gate")
    .description("Run the factory gate for the current worktree.")
    .option("--tier <tier:string>", "fast (no game build) or full", { default: "fast" })
    .option("--only <steps:string>", "comma-separated step names to run alone (no stamp)")
    .option("--base <ref:string>", "ref to diff against", {
      default: `origin/${config.integrationBranch}`,
    })
    .action(async ({ tier, base, only }) => {
      if (tier !== "fast" && tier !== "full") {
        throw new Error(`--tier must be fast or full, got ${tier}`)
      }
      const stamp = await runGate({
        cwd: Deno.cwd(),
        tier,
        base,
        only: only?.split(",").map((s) => s.trim()),
      })
      console.log(
        stamp.ok ? `GATE PASS (${tier})` : `GATE FAIL (${tier}): ${stamp.failedSteps.join(", ")}`,
      )
      Deno.exit(stamp.ok ? 0 : 1)
    })
    .parse(Deno.args)
}
