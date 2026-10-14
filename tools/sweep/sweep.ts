/// The sweep orchestration: watermark in, commit range, runner, classification, issues, watermark
/// out. Everything external (runner, gh, dates, paths) is injected, so the whole policy is
/// unit-tested with a temp git repo, a scripted runner and a recording fake gh.
import { join } from "@std/path"
import { run } from "../check/exec.ts"
import { type Classified, classify, parseLinuxFailures, selected } from "./classify.ts"
import { parseQuarantine } from "./quarantine.ts"
import { caseBody, execute, labelFor, planActions, planSummary, summaryBody } from "./issues.ts"
import { FALLBACK_COMMITS, renderRange } from "./range.ts"
import type { CommitRange, Gh, Platform, Runner, SweepPlan } from "./types.ts"
import { advanceWatermark, parseWatermark, watermarkPath } from "./watermark.ts"

export type SweepOutcome = {
  /// `ran`: the suite ran (whatever it found); `could-not-run`: build/harness failure, watermark
  /// untouched; `dry-run`: nothing was touched at all.
  status: "ran" | "could-not-run" | "dry-run"
  reason: string
  range: CommitRange
  verdicts: Classified[]
  /// What was (or, in a dry run, would be) done on GitHub.
  issueLines: string[]
  watermarkAdvanced: string | null
}

export type SweepDeps = {
  platform: Platform
  repoRoot: string
  /// Where watermarks live (default `<repoRoot>/.sweep`).
  sweepDir: string
  plan: SweepPlan
  runner: Runner
  gh: Gh
  /// `--since`: overrides the watermark for this run.
  since?: string
  dryRun: boolean
  /// Path of the Linux `check` test log to subtract; an absent file means every failure counts as
  /// platform-only, which the summary says out loud.
  linuxLog: string
  /// ISO date, injected so expiry behaviour is deterministic under test.
  today: string
  log: (line: string) => void
}

async function gitText(args: string[], cwd: string): Promise<string | null> {
  const result = await run(["git", ...args], { cwd })
  return result.code === 0 ? result.stdout : null
}

/// The commits this sweep covers. A watermark git cannot resolve, or one that is not an ancestor
/// of HEAD (a rewritten branch), degrades to the last `FALLBACK_COMMITS` commits with the range
/// label saying so; the sweep still runs and still moves the watermark to HEAD afterwards.
export async function commitRange(
  repoRoot: string,
  watermark: string | null,
): Promise<{ range: CommitRange; head: string }> {
  const headOut = await gitText(["rev-parse", "HEAD"], repoRoot)
  if (headOut === null) throw new Error("git rev-parse HEAD failed: is this a git repository?")
  const head = headOut.trim()
  let logArgs: string[]
  let contiguous = true
  if (watermark === null) {
    logArgs = ["log", "--format=%h %s", `--max-count=${FALLBACK_COMMITS}`]
    contiguous = false
  } else {
    const known = await gitText(["rev-parse", "--verify", "-q", `${watermark}^{commit}`], repoRoot)
    const ancestor = known !== null &&
      (await run(["git", "merge-base", "--is-ancestor", watermark, "HEAD"], { cwd: repoRoot }))
          .code === 0
    contiguous = ancestor
    logArgs = ancestor
      ? ["log", "--format=%h %s", `${watermark}..HEAD`]
      : ["log", "--format=%h %s", `--max-count=${FALLBACK_COMMITS}`]
  }
  const subjects = ((await gitText(logArgs, repoRoot)) ?? "").split("\n").filter((l) =>
    l.trim() !== ""
  )
  return {
    range: renderRange(subjects, subjects.length, contiguous ? watermark : null, head, contiguous),
    head,
  }
}

/// One sweep, end to end. Operational failures never throw: a failed build or a dead harness comes
/// back as `could-not-run` with the reason, and only a completed run touches the watermark.
export async function runSweep(deps: SweepDeps): Promise<SweepOutcome> {
  const { platform, repoRoot, sweepDir, log } = deps
  const path = watermarkPath(sweepDir, platform)
  let stored: string | null = null
  try {
    stored = parseWatermark(await Deno.readTextFile(path))
  } catch {
    log(`no watermark at ${path}: this platform has never been swept`)
  }
  const watermark = deps.since ?? stored
  const { range, head } = await commitRange(repoRoot, watermark)
  log(`platform ${platform}: sweeping ${range.label}`)

  if (deps.dryRun) {
    return {
      status: "dry-run",
      reason:
        `would configure, build, run ${deps.plan.tests.length} invocation(s), file issues, write ${
          head.slice(0, 10)
        } to ${path}`,
      range,
      verdicts: [],
      issueLines: [],
      watermarkAdvanced: null,
    }
  }

  const started = Date.now()
  const run_ = await deps.runner(deps.plan)
  if (!run_.completed) {
    log(`the sweep could not run: ${run_.reason}`)
    return {
      status: "could-not-run",
      reason: run_.reason,
      range,
      verdicts: [],
      issueLines: [],
      watermarkAdvanced: null,
    }
  }
  const seconds = ((Date.now() - started) / 1000).toFixed(0)
  log(`the run completed in ${seconds}s, ${run_.failures.length} failed case(s)`)

  let linuxFailures = new Set<string>()
  let haveBaseline = true
  try {
    linuxFailures = parseLinuxFailures(await Deno.readTextFile(deps.linuxLog))
  } catch {
    haveBaseline = false
    log(`no Linux baseline at ${deps.linuxLog}: every failure will count as platform-only`)
  }
  const quarantine = parseQuarantine(
    await Deno.readTextFile(join(repoRoot, "tests", "quarantine.h")),
  )
  const verdicts = classify(run_.failures, linuxFailures, quarantine, deps.today)
  const platformOnly = selected(verdicts, "platform-only")
  const expired = selected(verdicts, "expired-quarantine")
  const quarantined = selected(verdicts, "quarantined")
  const everywhere = selected(verdicts, "fails-everywhere")
  for (const v of platformOnly) log(`platform-only: ${v.failure.name}`)
  for (const v of expired) log(`quarantine EXPIRED and still failing: ${v.failure.name}`)

  const bodies = platformOnly.map((v) => ({
    caseName: v.failure.name,
    body: caseBody({
      platform,
      caseName: v.failure.name,
      rangeLabel: range.label,
      rangeLines: range.lines,
      invocation: v.failure.invocation,
      excerpt: v.failure.excerpt,
    }),
  }))
  const open = await deps.gh.openIssues(labelFor(platform))
  const actions = planActions(bodies, platform, open)
  const summary = planSummary(
    summaryBody({
      platform,
      rangeLabel: range.label,
      exitCodes: run_.exitCodes,
      platformOnly: platformOnly.map((v) => v.failure.name),
      quarantined: quarantined.map((v) => ({ name: v.failure.name, expires: v.entry!.expires })),
      expiredStillFailing: expired.map((v) => ({
        name: v.failure.name,
        expires: v.entry!.expires,
      })),
      failsEverywhere: everywhere.map((v) => v.failure.name),
    }),
    platform,
    open,
  )
  const issueLines = await execute(deps.gh, [...actions, summary], platform)
  for (const line of issueLines) log(line)
  if (!haveBaseline) log("note: no Linux baseline log was read; the summary says so too")

  const advanced = advanceWatermark(true, head, stored)
  if (advanced !== null) {
    await Deno.mkdir(sweepDir, { recursive: true })
    await Deno.writeTextFile(path, `${advanced}\n`)
    log(`watermark advanced to ${advanced}`)
  }
  return {
    status: "ran",
    reason: "the sweep ran",
    range,
    verdicts,
    issueLines,
    watermarkAdvanced: advanced,
  }
}
