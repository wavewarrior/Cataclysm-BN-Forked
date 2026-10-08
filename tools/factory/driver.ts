/// One ticket from `factory:ready` to a draft PR (or `factory:blocked`). The driver, never an
/// agent, creates worktrees, runs the gate, pushes and opens PRs.
import { delay } from "@std/async"
import { join } from "@std/path"
import { config } from "./config.ts"
import { type Stamp } from "./gate.ts"
import { comment, createDraftPr, editLabels, type Issue } from "./gh.ts"
import {
  agentPrompt,
  agentRead,
  agentStart,
  paneClose,
  paneSplit,
  worktreeCreate,
} from "./herdr.ts"
import { type Lane, updateLane } from "./lanes.ts"
import { branchName, dependsOn, makeTicket, missingSections } from "./ticket.ts"
import { factoryDir, git, run, runToLog, tailFile } from "./util.ts"

// ---------- pure helpers (unit tested) ----------

export type Verdict = "PASS" | "FAIL"

/// `VERDICT: PASS` / `VERDICT: FAIL` on the first line of a review file; anything else is none.
export function parseVerdict(text: string): Verdict | undefined {
  const m = text.split(/\r?\n/, 1)[0].trim().match(/^VERDICT:\s*(PASS|FAIL)$/)
  return m ? (m[1] as Verdict) : undefined
}

/// The lowest-numbered open `factory:ready` issue whose `## Depends on` issues are all closed.
export async function pickNext(
  ready: Issue[],
  isClosed: (issue: number) => Promise<boolean>,
): Promise<Issue | undefined> {
  for (const issue of [...ready].sort((a, b) => a.number - b.number)) {
    const deps = dependsOn(makeTicket(issue).sections)
    const open = await Promise.all(deps.map(async (d) => !(await isClosed(d))))
    if (!open.some(Boolean)) return issue
  }
  return undefined
}

/// A conventional-commit PR title; an issue title without a type gets `chore:`.
export function prTitle(title: string): string {
  return /^(?:feat|fix|refactor|chore|build|ci|test|docs|perf|style|revert)(?:\([^)]+\))?!?: \S/
      .test(title)
    ? title
    : `chore: ${title}`
}

export function implementerPrompt(issue: Issue, ticketPath: string): string {
  return `/skill:implement Implement GitHub issue #${issue.number} (${issue.title}). The full ticket is in ${ticketPath}: read it first and follow it exactly. Add or update the tests it names, commit your work to the current branch with conventional-commit messages, and stop. Do not push, open PRs or use gh. Do not build and do not run cata_test: this shell has no compiler environment, and the gate builds and runs the ticket's test tags for you.`
}

export function reviewerPrompt(opts: {
  issue: Issue
  ticketPath: string
  reviewPath: string
  base: string
}): string {
  return `/skill:interrogate Review the change on this branch for GitHub issue #${opts.issue.number} (ticket: ${opts.ticketPath}). Diff: git diff ${opts.base}...HEAD. Interrogate it adversarially first, then apply the code-review skill, then blast-radius when src/ changed a header with more than 10 usages. You are read-only. Write your result to ${opts.reviewPath} (the only file you may create). Its first line must be exactly "VERDICT: PASS" or "VERDICT: FAIL", followed by numbered findings, each with file:line. PASS only when there is no finding the author must fix.`
}

export type FailedStep = { name: string; tail: string }

export function gateFailurePrompt(steps: FailedStep[]): string {
  const body = steps.map((s) => `### ${s.name}\n${s.tail}`).join("\n\n")
  return `The gate failed. Fix only what failed, commit, and stop. Do not push or use gh.\n\n${body}`
}

export function reviewFailurePrompt(findings: string): string {
  return `The reviewer returned FAIL. Fix the findings below, commit, and stop. Do not push or use gh.\n\n${findings}`
}

function fillSection(template: string, heading: string, text: string): string {
  const lines = template.split("\n")
  const i = lines.findIndex((l) => l.startsWith(heading))
  if (i < 0) return template
  lines.splice(i + 1, 0, "", text)
  return lines.join("\n")
}

export function renderPrBody(opts: {
  template: string
  issue: Issue
  stamp: Stamp
  verdict: string
  findings: string
}): string {
  const { issue, stamp } = opts
  let body = opts.template
  body = fillSection(body, "## Purpose of change", `Closes #${issue.number}.\n\n${issue.title}`)
  body = fillSection(
    body,
    "## Describe the solution",
    "Implemented by an unattended factory lane from the ticket; see the commits.",
  )
  body = fillSection(
    body,
    "## Testing",
    [
      `Full gate passed at \`${stamp.headSha.slice(0, 10)}\` (base \`${
        stamp.baseSha.slice(0, 10)
      }\`, ${stamp.finishedAt}).`,
      `Reviewer: ${opts.verdict}`,
      opts.findings.trim()
        ? `\n<details><summary>Review</summary>\n\n${opts.findings.trim()}\n\n</details>`
        : "",
    ].join("\n"),
  )
  return body.replace("- [ ] This PR used AI assistance.", "- [x] This PR used AI assistance.")
}

// ---------- process ----------

export type TicketResult = { outcome: "pr"; url: string } | { outcome: "blocked"; reason: string }

const wslFree = (text: string) => text.replaceAll("\\", "/")

/// PATH for lane shells: formatters the staged-only pre-commit hook needs, if installed.
function laneEnv(ticket: number, role: "implementer" | "reviewer"): Record<string, string> {
  const extras = [
    "C:\\Python312\\Scripts",
    "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\Llvm\\x64\\bin",
  ].filter((p) => {
    try {
      return Deno.statSync(p).isDirectory
    } catch {
      return false
    }
  })
  const path = [...extras, Deno.env.get("PATH") ?? ""].join(";")
  return {
    FACTORY_LANE: "1",
    FACTORY_ROLE: role,
    FACTORY_TICKET: String(ticket),
    PATH: path,
  }
}

export type DriverOptions = { repo: string }

/// Run one already-claimed ticket to completion. Never throws: failures become `blocked`.
export async function runTicket(
  issue: Issue,
  lane: Lane,
  opts: DriverOptions,
): Promise<TicketResult> {
  const deadline = Date.now() + config.ticketWallClockMin * 60_000
  const remaining = () => deadline - Date.now()
  const n = issue.number
  const started = Date.now()
  const say = (msg: string) =>
    console.log(`#${n} +${((Date.now() - started) / 60000).toFixed(1)}m ${msg}`)

  const block = async (reason: string): Promise<TicketResult> => {
    console.log(`#${n} blocked: ${reason}`)
    await editLabels(n, { add: ["factory:blocked"], remove: ["factory:in-progress"] }).catch(
      () => {},
    )
    await comment(n, `factory: blocked.\n\n${reason}`).catch(() => {})
    return { outcome: "blocked", reason }
  }

  try {
    const ticket = makeTicket(issue)
    const missing = missingSections(ticket.sections)
    if (missing.length > 0) {
      return await block(`ticket is missing required section(s): ${missing.join(", ")}`)
    }

    const branch = branchName(config.branchPrefix, n, issue.title)
    const base = `origin/${config.integrationBranch}`
    const path = `${config.worktreeRoot}/wt-factory-${n}`
    await git(opts.repo, "fetch", "origin", config.integrationBranch)

    // Worktree + panes.
    const wt = await worktreeCreate({
      cwd: opts.repo,
      branch,
      base,
      path,
      label: `#${n} ${issue.title}`.slice(0, 60),
    })
    await updateLane(lane, {
      branch,
      worktree: path,
      workspaceId: wt.workspaceId,
      panes: [wt.rootPane],
    })

    // Gitignored files the build needs; hooks path; the ticket for the gate and the agents.
    await Deno.copyFile(
      join(opts.repo, "CMakeUserPresets.json"),
      join(path, "CMakeUserPresets.json"),
    )
      .catch(() => {})
    const install = await run(["deno", "run", "-A", "tools/factory/main.ts", "install"], {
      cwd: path,
    })
    if (install.code !== 0) return await block(`factory install failed: ${install.stderr.trim()}`)
    const fdir = await factoryDir(path)
    const ticketPath = wslFree(join(fdir, "ticket.md"))
    await Deno.writeTextFile(join(fdir, "ticket.json"), JSON.stringify(ticket, null, 2))
    await Deno.writeTextFile(join(fdir, "ticket.md"), `# #${n} ${issue.title}\n\n${issue.body}\n`)

    // Implementer.
    const implName = `impl-${n}`
    const implPane = await paneSplit(wt.rootPane, laneEnv(n, "implementer"))
    await updateLane(lane, { panes: [wt.rootPane, implPane] })
    await delay(1500) // let the shell reach its prompt
    await agentStart({
      name: implName,
      pane: implPane,
      args: [
        "--config",
        wslFree(join(path, "tools/factory/omp-implementer.yml")),
        "--model",
        config.models.implementer,
      ],
    })
    let outcome = await agentPrompt(implName, implementerPrompt(issue, ticketPath), remaining())
    if (outcome.state !== "idle" && outcome.state !== "done") {
      return await block(
        `implementer ended ${outcome.state}: ${outcome.detail}\n${await agentRead(implName)}`,
      )
    }

    say("implementer done; running the gate")
    // Gate + review loop; gate failures and reviewer FAILs share one retry budget.
    let retries = 0
    let verdict: Verdict = "FAIL"
    let findings = ""
    let stamp: Stamp | undefined
    while (true) {
      if (remaining() <= 0) {
        return await block(`wall clock of ${config.ticketWallClockMin} min exceeded`)
      }
      say(`gate attempt ${retries + 1}`)
      const gateRun = await runToLog(
        ["deno", "task", "gate", "--tier", "full", "--base", base],
        join(fdir, "gate-run.log"),
        { cwd: path, env: { FACTORY_TICKET: String(n) } },
      )
      stamp = await Deno.readTextFile(join(fdir, "gate-stamp.json")).then(
        (t) => JSON.parse(t) as Stamp,
        () => undefined,
      )
      let feedback: string
      // A tooling failure cannot be fixed by the implementer; retrying only burns minutes.
      if (stamp && stamp.infraSteps.length > 0) {
        const steps = await Promise.all(stamp.infraSteps.map(async (name) => ({
          name,
          tail: await tailFile(join(fdir, "logs", `${name}.log`), 40),
        })))
        return await block(
          `the gate broke in its own tooling (not the change): ${stamp.infraSteps.join(", ")}.\n\n${
            steps.map((s) => `### ${s.name}\n${s.tail}`).join("\n\n")
          }`,
        )
      }
      if (gateRun !== 0 || !stamp?.ok) {
        const steps = await Promise.all(
          (stamp?.failedSteps ?? ["gate"]).map(async (name) => ({
            name,
            tail: await tailFile(join(fdir, "logs", `${name}.log`), 60),
          })),
        )
        if (retries >= config.maxFixRetries) {
          return await block(
            `the gate failed ${retries + 1} times. Last failure:\n\n${
              steps.map((s) => `### ${s.name}\n${s.tail}`).join("\n\n")
            }`,
          )
        }
        feedback = gateFailurePrompt(steps)
      } else {
        say("gate passed; reviewing")
        const review = await runReview({ issue, path, fdir, implPane, base, ticketPath, remaining })
        if (review.kind === "error") return await block(review.reason)
        verdict = review.verdict
        findings = review.text
        if (verdict === "PASS") break
        if (retries >= config.maxFixRetries) {
          return await block(
            `the reviewer said FAIL ${retries + 1} times. Last review:\n\n${findings}`,
          )
        }
        feedback = reviewFailurePrompt(findings)
      }
      retries++
      const headBefore = await git(path, "rev-parse", "HEAD")
      outcome = await agentPrompt(implName, feedback, remaining())
      if (outcome.state !== "idle" && outcome.state !== "done") {
        return await block(`implementer ended ${outcome.state} while fixing: ${outcome.detail}`)
      }
      // No new commit means the same tree would fail the same gate: stop rather than re-run it.
      if ((await git(path, "rev-parse", "HEAD")) === headBefore) {
        return await block(
          `the implementer made no commit after feedback; the gate would fail the same way.\n\n${feedback}`,
        )
      }
    }

    say(`review ${verdict}; pushing`)
    // Success: the driver pushes (pre-push re-verifies stamp + verdict) and opens the draft PR.
    const push = await run(["git", "push", "origin", `${branch}:${branch}`], {
      cwd: path,
      env: { FACTORY_LANE: "1" },
    })
    if (push.code !== 0) return await block(`git push failed:\n${push.stderr.trim()}`)
    const template = await Deno.readTextFile(join(path, ".github/pull_request_template.md"))
    const url = await createDraftPr({
      head: branch,
      title: prTitle(issue.title),
      body: renderPrBody({ template, issue, stamp: stamp!, verdict, findings }),
    })
    await editLabels(n, { add: ["factory:review"], remove: ["factory:in-progress"] })
    await comment(n, `factory: draft PR ${url} (gate passed, reviewer ${verdict}).`)
    console.log(`#${n} -> ${url}`)
    return { outcome: "pr", url }
  } catch (error) {
    return await block(`driver error: ${error instanceof Error ? error.message : error}`)
  }
}

type ReviewOutcome =
  | { kind: "verdict"; verdict: Verdict; text: string }
  | { kind: "error"; reason: string }

async function runReview(opts: {
  issue: Issue
  path: string
  fdir: string
  implPane: string
  base: string
  ticketPath: string
  remaining: () => number
}): Promise<ReviewOutcome> {
  const n = opts.issue.number
  const sha = await git(opts.path, "rev-parse", "HEAD")
  const reviewPath = join(opts.fdir, `review-${sha}.md`)
  await Deno.remove(reviewPath).catch(() => {})
  const name = `rev-${n}`
  const pane = await paneSplit(opts.implPane, laneEnv(n, "reviewer"), "down")
  await delay(1500)
  await agentStart({
    name,
    pane,
    args: [
      "--config",
      wslFree(join(opts.path, "tools/factory/omp-reviewer.yml")),
      "--model",
      config.models.reviewer,
      "--thinking",
      "high",
    ],
  })
  const prompt = reviewerPrompt({
    issue: opts.issue,
    ticketPath: opts.ticketPath,
    reviewPath: wslFree(reviewPath),
    base: opts.base,
  })
  // One nudge if the reviewer finished without writing its verdict file.
  for (
    const text of [
      prompt,
      `You did not write ${
        wslFree(reviewPath)
      }. Write it now; the first line must be "VERDICT: PASS" or "VERDICT: FAIL".`,
    ]
  ) {
    const outcome = await agentPrompt(name, text, opts.remaining())
    if (outcome.state !== "idle" && outcome.state !== "done") {
      return { kind: "error", reason: `reviewer ended ${outcome.state}: ${outcome.detail}` }
    }
    const written = await Deno.readTextFile(reviewPath).catch(() => undefined)
    if (written !== undefined) {
      const verdict = parseVerdict(written)
      if (verdict) {
        await paneClose(pane)
        return { kind: "verdict", verdict, text: written }
      }
    }
  }
  return { kind: "error", reason: `the reviewer never wrote a valid verdict to ${reviewPath}` }
}
