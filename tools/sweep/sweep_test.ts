/// End-to-end tests for the sweep policy: a temp git repo, a scripted runner, a recording fake gh.
/// Nothing here builds, runs the game, or touches the network.
import { assertEquals } from "@std/assert"
import { join } from "@std/path"
import { run } from "../check/exec.ts"
import { runSweep, type SweepDeps } from "./sweep.ts"
import type { Gh, IssueRef, PlatformRun, Runner, SweepPlan } from "./types.ts"
import { watermarkPath } from "./watermark.ts"

const QUARANTINE = `
inline constexpr auto quarantine_entries = std::to_array<quarantine_entry>({
    {
        .name = "quarantined case",
        .expires = "2026-10-27",
        .reason = "flaky everywhere",
    },
    {
        .name = "expired case",
        .expires = "2026-10-01",
        .reason = "was flaky",
    },
});
`

const git = async (cwd: string, ...args: string[]): Promise<string> => {
  const result = await run(["git", ...args], { cwd })
  if (result.code !== 0) throw new Error(`git ${args.join(" ")}: ${result.stderr}`)
  return result.stdout.trim()
}

/// A repo with `count` commits, each touching src/ so the history is real.
async function tempRepo(count: number): Promise<{ root: string; shas: string[] }> {
  const root = await Deno.makeTempDir({ prefix: "sweep-test-" })
  await git(root, "init", "-b", "main")
  await git(root, "config", "user.email", "sweep@test.local")
  await git(root, "config", "user.name", "sweep test")
  await Deno.mkdir(join(root, "tests"), { recursive: true })
  await Deno.mkdir(join(root, "src"), { recursive: true })
  await Deno.writeTextFile(join(root, "tests", "quarantine.h"), QUARANTINE)
  const shas: string[] = []
  for (let i = 0; i < count; i++) {
    await Deno.writeTextFile(join(root, "src", `f${i}.cpp`), `// commit ${i}\n`)
    await git(root, "add", "-A")
    await git(root, "commit", "-m", `commit ${i}`)
    shas.push(await git(root, "rev-parse", "HEAD"))
  }
  return { root, shas }
}

const fakeGh = (initial: IssueRef[] = []) => {
  const open = [...initial]
  const created: { title: string; body: string; label: string }[] = []
  const comments: { number: number; body: string }[] = []
  const gh: Gh = {
    openIssues: () => Promise.resolve(open.map(({ number, title }) => ({ number, title }))),
    createIssue: (title, body, label) => {
      created.push({ title, body, label })
      open.push({ number: 100 + created.length, title })
      return Promise.resolve(100 + created.length)
    },
    commentIssue: (number, body) => {
      comments.push({ number, body })
      return Promise.resolve()
    },
  }
  return { gh, created, comments }
}

const planFor = (root: string): SweepPlan => ({
  platform: "macos",
  configure: ["true"],
  build: ["true"],
  binary: join(root, "cata_test-tiles"),
  tests: [{ label: "suite", args: [], report: join(root, "suite.xml") }],
})

const scripted = (run_: PlatformRun): Runner => () => Promise.resolve(run_)

const depsOver = (
  root: string,
  overrides: Partial<SweepDeps>,
): SweepDeps => {
  const gh = fakeGh()
  return {
    platform: "macos",
    repoRoot: root,
    sweepDir: join(root, ".sweep"),
    plan: planFor(root),
    runner: scripted({ completed: true, exitCodes: { suite: 0 }, failures: [] }),
    gh: gh.gh,
    dryRun: false,
    linuxLog: join(root, "missing-linux.log"),
    today: "2026-10-13",
    log: () => {},
    ...overrides,
  }
}

const readWatermark = async (root: string): Promise<string | null> => {
  try {
    return (await Deno.readTextFile(watermarkPath(join(root, ".sweep"), "macos"))).trim()
  } catch {
    return null
  }
}

Deno.test("a completed clean sweep writes HEAD to the watermark", async () => {
  const { root, shas } = await tempRepo(2)
  const outcome = await runSweep(depsOver(root, {}))
  assertEquals(outcome.status, "ran")
  assertEquals(await readWatermark(root), shas[1])
  await Deno.remove(root, { recursive: true })
})

Deno.test("a run that could not happen leaves the watermark exactly where it was", async () => {
  const { root, shas } = await tempRepo(3)
  // Seed the watermark at commit 1.
  await Deno.mkdir(join(root, ".sweep"), { recursive: true })
  await Deno.writeTextFile(watermarkPath(join(root, ".sweep"), "macos"), `${shas[0]}\n`)
  const outcome = await runSweep(
    depsOver(root, {
      runner: scripted({ completed: false, reason: "the build failed" }),
    }),
  )
  assertEquals(outcome.status, "could-not-run")
  assertEquals(outcome.reason, "the build failed")
  assertEquals(await readWatermark(root), shas[0])
  await Deno.remove(root, { recursive: true })
})

Deno.test("a completed run WITH failures still advances: failures are the product", async () => {
  const { root, shas } = await tempRepo(2)
  const outcome = await runSweep(
    depsOver(root, {
      runner: scripted({
        completed: true,
        exitCodes: { suite: 1 },
        failures: [{ name: "metal only", excerpt: "REQUIRE(false)", invocation: "suite" }],
      }),
    }),
  )
  assertEquals(outcome.status, "ran")
  assertEquals(await readWatermark(root), shas[1])
  await Deno.remove(root, { recursive: true })
})

Deno.test("the plan's acceptance scenario: skip three commits, then one sweep covers all three and clears", async () => {
  const { root, shas } = await tempRepo(1)
  await Deno.mkdir(join(root, ".sweep"), { recursive: true })
  await Deno.writeTextFile(watermarkPath(join(root, ".sweep"), "macos"), `${shas[0]}\n`)
  // Three commits land while no sweep runs.
  for (let i = 1; i <= 3; i++) {
    await Deno.writeTextFile(join(root, "src", `later${i}.cpp`), `// later ${i}\n`)
    await git(root, "add", "-A")
    await git(root, "commit", "-m", `later commit ${i}`)
  }
  const head = await git(root, "rev-parse", "HEAD")
  const gh = fakeGh()
  const outcome = await runSweep(depsOver(root, { gh: gh.gh }))
  // Covers all three...
  assertEquals(outcome.range.total, 3)
  assertEquals(outcome.range.contiguous, true)
  assertEquals(outcome.range.label.includes(`${shas[0].slice(0, 10)}..`), true)
  // ...and clears the watermark to HEAD.
  assertEquals(await readWatermark(root), head)
  // A second immediate sweep covers nothing new.
  const second = await runSweep(depsOver(root, { gh: fakeGh().gh }))
  assertEquals(second.range.total, 0)
  await Deno.remove(root, { recursive: true })
})

Deno.test("classification decides issues: platform-only files, quarantine and Linux do not", async () => {
  const { root } = await tempRepo(1)
  await Deno.writeTextFile(
    join(root, "missing-linux.log"),
    "FAILED: linux also fails\n",
  )
  const gh = fakeGh()
  const outcome = await runSweep(
    depsOver(root, {
      linuxLog: join(root, "missing-linux.log"),
      gh: gh.gh,
      runner: scripted({
        completed: true,
        exitCodes: { suite: 1 },
        failures: [
          { name: "metal only", excerpt: "REQUIRE(a==b)", invocation: "suite" },
          { name: "linux also fails", excerpt: "x", invocation: "suite" },
          { name: "quarantined case", excerpt: "x", invocation: "suite" },
          { name: "expired case", excerpt: "x", invocation: "suite" },
        ],
      }),
    }),
  )
  // One case issue (metal only) plus the summary.
  assertEquals(gh.created.length, 2)
  assertEquals(gh.created[0].title, "[platform-sweep] metal only (macos)")
  assertEquals(gh.created[0].label, "platform:macos")
  assertEquals(gh.created[1].title, "[platform-sweep] summary (macos)")
  assertEquals(gh.created[1].body.includes("- `linux also fails`"), true)
  assertEquals(gh.created[1].body.includes("- `quarantined case` until 2026-10-27"), true)
  assertEquals(gh.created[1].body.includes("- `expired case` (expired 2026-10-01)"), true)
  assertEquals(outcome.issueLines.length, 2)
  await Deno.remove(root, { recursive: true })
})

Deno.test("dedupe: an open issue with the exact title gets a comment, not a twin", async () => {
  const { root } = await tempRepo(1)
  const gh = fakeGh([{ number: 55, title: "[platform-sweep] metal only (macos)" }])
  await runSweep(
    depsOver(root, {
      gh: gh.gh,
      runner: scripted({
        completed: true,
        exitCodes: { suite: 1 },
        failures: [{ name: "metal only", excerpt: "again", invocation: "suite" }],
      }),
    }),
  )
  assertEquals(gh.created.map((c) => c.title), ["[platform-sweep] summary (macos)"])
  assertEquals(gh.comments.length, 1)
  assertEquals(gh.comments[0].number, 55)
  await Deno.remove(root, { recursive: true })
})

Deno.test("a dry run touches nothing: no watermark, no issues", async () => {
  const { root, shas } = await tempRepo(2)
  const gh = fakeGh()
  const outcome = await runSweep(
    depsOver(root, { dryRun: true, gh: gh.gh, since: shas[0] }),
  )
  assertEquals(outcome.status, "dry-run")
  assertEquals(outcome.range.total, 1)
  assertEquals(await readWatermark(root), null)
  assertEquals(gh.created.length, 0)
  assertEquals(gh.comments.length, 0)
  await Deno.remove(root, { recursive: true })
})

Deno.test("--since overrides the watermark for one run without breaking it", async () => {
  const { root, shas } = await tempRepo(3)
  await Deno.mkdir(join(root, ".sweep"), { recursive: true })
  await Deno.writeTextFile(watermarkPath(join(root, ".sweep"), "macos"), `${shas[2]}\n`)
  const outcome = await runSweep(depsOver(root, { since: shas[0] }))
  assertEquals(outcome.range.total, 2)
  // The watermark still advances to HEAD, because the run completed.
  assertEquals(await readWatermark(root), shas[2])
  await Deno.remove(root, { recursive: true })
})

Deno.test("a watermark git cannot resolve degrades to recent history, loudly", async () => {
  const { root } = await tempRepo(2)
  await Deno.mkdir(join(root, ".sweep"), { recursive: true })
  await Deno.writeTextFile(
    watermarkPath(join(root, ".sweep"), "macos"),
    "1234567890abcdef1234567890abcdef12345678\n",
  )
  const outcome = await runSweep(depsOver(root, {}))
  assertEquals(outcome.range.contiguous, false)
  assertEquals(outcome.range.total, 2)
  assertEquals(outcome.range.label.includes("most recent"), true)
  await Deno.remove(root, { recursive: true })
})
