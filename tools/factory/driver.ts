/// One ticket from `factory:ready` to a draft PR (or `factory:blocked`). The driver, never an
/// agent, creates worktrees, runs the gate, pushes and opens PRs.
import { delay } from "@std/async"
import { join } from "@std/path"
import { config } from "./config.ts"
import { type Stamp } from "./gate.ts"
import {
  comment,
  createDraftPr,
  createIssue,
  editIssueBody,
  editLabels,
  type Issue,
  listIssues,
} from "./gh.ts"
import {
  agentPrompt,
  agentRead,
  agentStart,
  paneClose,
  paneSplit,
  workspaceClose,
  worktreeCreate,
  type WorktreeEntry,
  worktreeList,
  worktreeOpen,
} from "./herdr.ts"
import { type Lane, updateLane } from "./lanes.ts"
import {
  branchName,
  dependsOn,
  isConventionalTitle,
  listItems,
  makeTicket,
  missingSections,
  type Ticket,
} from "./ticket.ts"
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

/// The blocked ticket a follow-up unblocks (its `## Follow-up of` section), or undefined.
export function followUpOf(sections: Ticket["sections"]): number | undefined {
  const m = listItems(sections["Follow-up of"])[0]?.match(/#?(\d+)/)
  return m ? Number(m[1]) : undefined
}

/// `body` with the `## Depends on` (or `## Blocked by`) item for `from` pointing at `to`. Other
/// sections and items stay as written.
export function rewireDependsOn(body: string, from: number, to: number): string {
  let inDependsOn = false
  return body.split("\n").map((line) => {
    if (/^## /.test(line)) inDependsOn = /^##\s+(depends on|blocked by)\s*$/i.test(line)
    const item = line.match(/^(\s*(?:[-*+]|\d+\.)\s+)(.*)$/)
    if (!inDependsOn || !item) return line
    const m = item[2].match(/#?(\d+)/)
    if (!m || Number(m[1]) !== from) return line
    return item[1] + item[2].replace(m[0], m[0].startsWith("#") ? `#${to}` : `${to}`)
  }).join("\n")
}

/// The body of the follow-up that unblocks `issue`: the blocked ticket's acceptance, touches and
/// test tags, with the failure to fix last (its text may contain `#` lines).
export function followUpBody(opts: { issue: Issue; ticket: Ticket; reason: string }): string {
  const { issue, ticket, reason } = opts
  return [
    `## Goal\n\nFix the failures that blocked #${issue.number} (${issue.title}). Keep its behaviour.\n`,
    `## Acceptance\n\nThe acceptance of #${issue.number} holds:\n\n${
      ticket.sections.Acceptance ?? ""
    }\n`,
    `## Touches\n\n${ticket.sections.Touches ?? ""}\n`,
    `## Test tags\n\n${ticket.sections["Test tags"] ?? ""}\n`,
    `## Follow-up of\n\n- #${issue.number}\n`,
    `## Gate output\n\n${reason}\n`,
  ].join("\n")
}

/// A conventional-commit PR title; an issue title without a type gets `chore:`.
export function prTitle(title: string): string {
  return isConventionalTitle(title) ? title : `chore: ${title}`
}

/// Labels a blocked ticket gets. `needsHuman` adds `factory:needs-human`: a retry cannot fix it.
export function blockLabels(needsHuman: boolean): string[] {
  return needsHuman ? ["factory:blocked", "factory:needs-human"] : ["factory:blocked"]
}

/// The comment a blocked ticket receives; a needs-human stop says so on its first line.
export function blockComment(reason: string, needsHuman: boolean): string {
  const head = needsHuman ? "factory: blocked (needs human)." : "factory: blocked."
  return `${head}\n\n${reason}`
}

/// The herdr workspace label for ticket `n`. Lookups match on `workspacePrefix`, so a retitled
/// ticket still finds the workspace an earlier attempt opened.
export function workspacePrefix(n: number): string {
  return `#${n} `
}

export function workspaceLabel(n: number, title: string): string {
  return `${workspacePrefix(n)}${title}`.slice(0, 60)
}

export function implementerPrompt(issue: Issue, ticketPath: string, base: string): string {
  return `/skill:implement Implement GitHub issue #${issue.number} (${issue.title}). The full ticket is in ${ticketPath}: read it first and follow it exactly. Add or update the tests it names. Before you commit, format every C++ file you changed by running \`deno run -A tools/factory/format.ts --base ${base}\` from the worktree root; it rewrites whole files with the gate's pinned clang-format and astyle, so do not hand-format. Then commit your work to the current branch with conventional-commit messages, and stop. Do not push, open PRs or use gh. Do not build and do not run cata_test: this shell has no compiler environment, and the gate builds and runs the ticket's test tags for you.`
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

export function gateFailurePrompt(steps: FailedStep[], base: string): string {
  const body = steps.map((s) => `### ${s.name}\n${s.tail}`).join("\n\n")
  return `The gate failed. Fix only what failed. Before you commit, format the C++ files you changed with \`deno run -A tools/factory/format.ts --base ${base}\` from the worktree root. Then commit and stop. Do not push or use gh.\n\n${body}`
}

export function reviewFailurePrompt(findings: string, base: string): string {
  return `The reviewer returned FAIL. Fix the findings below. Before you commit, format the C++ files you changed with \`deno run -A tools/factory/format.ts --base ${base}\` from the worktree root. Then commit and stop. Do not push or use gh.\n\n${findings}`
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

/// Block a ticket with a follow-up that carries the failure, so its dependents move to the follow-up
/// instead of waiting on a blocked ticket. A follow-up never spawns another follow-up.
async function blockWithFollowUp(opts: {
  issue: Issue
  ticket: Ticket
  reason: string
  needsHuman?: boolean
  block: (reason: string, opts?: { needsHuman?: boolean }) => Promise<TicketResult>
}): Promise<TicketResult> {
  const { issue, ticket, reason, needsHuman = false, block } = opts
  if (followUpOf(ticket.sections) !== undefined) return await block(reason, { needsHuman })
  let followUp: { number: number; url: string }
  try {
    followUp = await createIssue({
      title: `fix: unblock #${issue.number}`,
      body: followUpBody({ issue, ticket, reason }),
      labels: ["factory:ready"],
    })
  } catch (error) {
    const why = error instanceof Error ? error.message : String(error)
    return await block(`${reason}\n\nno follow-up ticket was created: ${why}`, { needsHuman })
  }
  const open = await listIssues([]).catch(() => undefined)
  const moved: number[] = []
  const stuck: number[] = []
  for (const dependent of open ?? []) {
    if (!dependsOn(makeTicket(dependent).sections).includes(issue.number)) continue
    try {
      await editIssueBody(
        dependent.number,
        rewireDependsOn(dependent.body, issue.number, followUp.number),
      )
      moved.push(dependent.number)
    } catch {
      stuck.push(dependent.number)
    }
  }
  const notes = [`Follow-up: #${followUp.number} ${followUp.url}`]
  if (open === undefined) notes.push("Could not list open issues, so no dependent was moved.")
  if (moved.length) {
    notes.push(
      `Dependents now wait on #${followUp.number}: ${moved.map((n) => `#${n}`).join(", ")}.`,
    )
  }
  if (stuck.length) {
    notes.push(
      `Could not move these dependents; move them by hand: ${
        stuck.map((n) => `#${n}`).join(", ")
      }.`,
    )
  }
  return await block(`${reason}\n\n${notes.join("\n")}`)
}

/// Lower-case, forward-slash form of a worktree path, so herdr's `\\?\` paths compare equal to plain ones.
export function normalizeWorktreePath(path: string): string {
  return path.replaceAll("\\", "/").replace(/^\/\/\?\//, "").replace(/\/+$/, "").toLowerCase()
}

export type WorktreePlan =
  | { action: "create" }
  | { action: "reuse"; closeWorkspace?: string }
  | { action: "conflict"; reason: string }

/// What a dependency is doing, for explaining a hold: closed, else the factory label it carries, else open.
export type DepState =
  | "closed"
  | "ready"
  | "review"
  | "blocked"
  | "in-progress"
  | "open"
  | "missing"

/// The state of one dependency. `undefined` (no such issue) -> missing.
export function depStateOf(issue: Issue | undefined): DepState {
  if (!issue) return "missing"
  if (issue.state.toUpperCase() === "CLOSED") return "closed"
  if (issue.labels.includes("factory:ready")) return "ready"
  if (issue.labels.includes("factory:blocked")) return "blocked"
  if (issue.labels.includes("factory:review")) return "review"
  if (issue.labels.includes("factory:in-progress")) return "in-progress"
  return "open"
}

export type Hold = { issue: number; waitingOn: { number: number; state: DepState }[] }

/// Each ready ticket (ascending number) with at least one dependency that is not closed. Calls `state` once per distinct dependency.
export async function holdsOf(
  ready: Issue[],
  state: (n: number) => Promise<DepState>,
): Promise<Hold[]> {
  const ordered = [...ready].sort((a, b) => a.number - b.number)
  const deps = ordered.map((issue) => ({
    issue: issue.number,
    on: dependsOn(makeTicket(issue).sections),
  }))
  const distinct = [...new Set(deps.flatMap((d) => d.on))]
  const states = new Map(
    await Promise.all(distinct.map(async (n) => [n, await state(n)] as const)),
  )
  return deps.flatMap(({ issue, on }) => {
    const waitingOn = on
      .filter((n) => states.get(n) !== "closed")
      .map((n) => ({ number: n, state: states.get(n) ?? "missing" }))
    return waitingOn.length > 0 ? [{ issue, waitingOn }] : []
  })
}

const HOLD_WORDS: Record<DepState, string> = {
  closed: "closed",
  ready: "queued",
  review: "in review, PR awaiting merge",
  blocked: "blocked",
  "in-progress": "in progress",
  open: "open",
  missing: "not found",
}

/// "#172 waits on #170 (blocked), #171 (blocked); #173 waits on #172 (queued)". "" for no holds.
export function describeHolds(holds: Hold[]): string {
  return holds
    .map((h) =>
      `#${h.issue} waits on ${
        h.waitingOn.map((w) => `#${w.number} (${HOLD_WORDS[w.state]})`).join(", ")
      }`
    )
    .join("; ")
}

/// Reuse the checkout of `want.branch` at `want.path` if it exists; refuse a path or branch held by something else; else create. Never deletes.
export function planWorktree(
  entries: WorktreeEntry[],
  want: { path: string; branch: string },
): WorktreePlan {
  const wantPath = normalizeWorktreePath(want.path)
  const atPath = entries.find((e) => normalizeWorktreePath(e.path) === wantPath)
  if (atPath) {
    if (atPath.branch === want.branch) {
      return { action: "reuse", closeWorkspace: atPath.openWorkspaceId }
    }
    const on = atPath.branch ?? "detached HEAD"
    return {
      action: "conflict",
      reason: `worktree ${want.path} is on branch ${on}, not ${want.branch}; remove it by hand`,
    }
  }
  const onBranch = entries.find((e) => e.branch === want.branch)
  if (onBranch) {
    return {
      action: "conflict",
      reason: `branch ${want.branch} is already checked out at ${onBranch.path}`,
    }
  }
  return { action: "create" }
}

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

  const block = async (
    reason: string,
    opts: { needsHuman?: boolean } = {},
  ): Promise<TicketResult> => {
    const needsHuman = opts.needsHuman ?? false
    console.log(`#${n} blocked: ${reason}`)
    await editLabels(n, { add: blockLabels(needsHuman), remove: ["factory:in-progress"] }).catch(
      () => {},
    )
    await comment(n, blockComment(reason, needsHuman)).catch(() => {})
    return { outcome: "blocked", reason }
  }

  try {
    const ticket = makeTicket(issue)
    const missing = missingSections(ticket.sections)
    if (missing.length > 0) {
      return await block(
        `ticket is missing required section(s): ${missing.join(", ")}`,
        { needsHuman: true },
      )
    }

    const branch = branchName(config.branchPrefix, n, issue.title)
    const base = `origin/${config.integrationBranch}`
    const path = `${config.worktreeRoot}/wt-factory-${n}`
    await git(opts.repo, "fetch", "origin", config.integrationBranch)

    // Worktree + panes. A blocked ticket's checkout may still exist: resume it, do not die on `already exists`.
    const plan = planWorktree(await worktreeList(opts.repo), { path, branch })
    if (plan.action === "conflict") return await block(plan.reason)
    if (plan.action === "reuse" && plan.closeWorkspace) await workspaceClose(plan.closeWorkspace)
    const label = workspaceLabel(n, issue.title)
    const wt = plan.action === "reuse"
      ? await worktreeOpen({ cwd: opts.repo, path, label })
      : await worktreeCreate({ cwd: opts.repo, branch, base, path, label })
    if (plan.action === "reuse") console.log(`#${n} resuming the existing worktree ${path}`)
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
    let outcome = await agentPrompt(
      implName,
      implementerPrompt(issue, ticketPath, base),
      remaining(),
    )
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
          { needsHuman: true },
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
          return await blockWithFollowUp({
            issue,
            ticket,
            block,
            needsHuman: true,
            reason: `the gate failed ${retries + 1} times. Last failure:\n\n${
              steps.map((s) => `### ${s.name}\n${s.tail}`).join("\n\n")
            }`,
          })
        }
        feedback = gateFailurePrompt(steps, base)
      } else {
        say("gate passed; reviewing")
        const review = await runReview({ issue, path, fdir, implPane, base, ticketPath, remaining })
        if (review.kind === "error") return await block(review.reason)
        verdict = review.verdict
        findings = review.text
        if (verdict === "PASS") break
        if (retries >= config.maxFixRetries) {
          return await blockWithFollowUp({
            issue,
            ticket,
            block,
            needsHuman: true,
            reason: `the reviewer said FAIL ${retries + 1} times. Last review:\n\n${findings}`,
          })
        }
        feedback = reviewFailurePrompt(findings, base)
      }
      retries++
      const headBefore = await git(path, "rev-parse", "HEAD")
      outcome = await agentPrompt(implName, feedback, remaining())
      if (outcome.state !== "idle" && outcome.state !== "done") {
        return await block(
          `implementer ended ${outcome.state} while fixing: ${outcome.detail}`,
        )
      }
      // No new commit means the same tree would fail the same gate: stop rather than re-run it.
      if ((await git(path, "rev-parse", "HEAD")) === headBefore) {
        return await blockWithFollowUp({
          issue,
          ticket,
          block,
          needsHuman: true,
          reason:
            `the implementer made no commit after feedback; the gate would fail the same way.\n\n${feedback}`,
        })
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
