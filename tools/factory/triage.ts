/// Triage of tickets that need a person: deterministic collectors (no LLM), `unblock`, and
/// `apply`, the only writer for the nightly triager. Effects arrive as deps so tests can stub gh.
import { config } from "./config.ts"
import {
  closeIssue,
  comment,
  type CommentInfo,
  editIssueBody,
  editLabels,
  getIssue,
  gh,
  type Issue,
  listComments,
  listIssues,
} from "./gh.ts"

const DAY_MS = 86_400_000
const HOUR_MS = 3_600_000
/// Comment authors whose words count as a human reply. Fork carries upstream committers, so
/// CONTRIBUTOR does not count: anyone must not steer the agent.
const HUMAN_ASSOCIATIONS = ["OWNER", "MEMBER", "COLLABORATOR"]
/// The driver's block comment, or a human's `stop`: both mark when a ticket was blocked.
const BLOCK_PREFIXES = ["factory: blocked", "factory: stopped"]
/// Every state a ticket can sit in without moving on by itself.
const STALE_LABELS = [
  "factory:draft",
  "factory:ready",
  "factory:in-progress",
  "factory:review",
  "factory:blocked",
]
/// Triage actions `apply` accepts; anything else is rejected as data.
const APPLY_ACTIONS = ["unblock-ready", "unblock-draft", "close", "comment", "needs-human"]
const NOTE_MAX = 4000

export type TriageDeps = {
  listIssues: (labels: string[]) => Promise<Issue[]>
  listComments: (number: number) => Promise<CommentInfo[]>
}

export type UnblockDeps = {
  getIssue: (number: number) => Promise<Issue>
  listComments: (number: number) => Promise<CommentInfo[]>
  editIssue: (number: number, body: string) => Promise<void>
  editLabels: (number: number, change: { add?: string[]; remove?: string[] }) => Promise<void>
  comment: (number: number, body: string) => Promise<void>
  login: () => Promise<string>
}

export type ApplyDeps = Omit<UnblockDeps, "login"> & {
  closeIssue: (number: number, note: string) => Promise<void>
}

export type ApplyInput = { number: number; action: string; note?: string }

export type ApplyRow = {
  number: number | null
  action: string | null
  ok: boolean
  problem?: string
}

export type NeedsHumanRow = {
  number: number
  title: string
  labels: string[]
  blockedAt: string
  awaitingSince: number
  humanReply: { author: string; at: string; body: string } | null
  url: string
}

export type StaleRow = {
  number: number
  title: string
  labels: string[]
  state: string
  createdAt: string
  updatedAt: string
  idleDays: number
  url: string
}

/// A human reply: a human association, and not the factory's own comments (the driver posts
/// through the owner's token, so they read as OWNER).
export function isHumanReply(c: CommentInfo): boolean {
  return HUMAN_ASSOCIATIONS.includes(c.association) && !c.body.trimStart().startsWith("factory:")
}

function newestHumanReply(comments: CommentInfo[]): CommentInfo | undefined {
  return comments.filter(isHumanReply).sort((a, b) =>
    Date.parse(b.publishedAt) - Date.parse(a.publishedAt)
  )[0]
}

/// One blocked ticket's row. `blockedAt` is the newest block marker (else the last update); a human
/// reply is a human comment posted after it.
export function needsHumanRow(
  issue: Issue,
  comments: CommentInfo[],
  now: number,
): NeedsHumanRow {
  const markers = comments
    .filter((c) => BLOCK_PREFIXES.some((p) => c.body.startsWith(p)))
    .map((c) => Date.parse(c.publishedAt))
  const blockedMs = markers.length > 0 ? Math.max(...markers) : Date.parse(issue.updatedAt)
  const reply = newestHumanReply(
    comments.filter((c) => Date.parse(c.publishedAt) > blockedMs),
  )
  return {
    number: issue.number,
    title: issue.title,
    labels: issue.labels,
    blockedAt: new Date(blockedMs).toISOString(),
    awaitingSince: Math.round(((now - blockedMs) / HOUR_MS) * 10) / 10,
    humanReply: reply ? { author: reply.author, at: reply.publishedAt, body: reply.body } : null,
    url: `https://github.com/${config.repo}/issues/${issue.number}`,
  }
}

/// Open tickets blocked on a person. Replied-to tickets come first, then longest-waiting first.
export async function needsHumanFeed(deps: TriageDeps, now: number): Promise<NeedsHumanRow[]> {
  const blocked = (await deps.listIssues(["factory:needs-human"])).filter((i) =>
    i.labels.includes("factory:blocked")
  )
  const rows: NeedsHumanRow[] = []
  for (const issue of blocked) {
    rows.push(needsHumanRow(issue, await deps.listComments(issue.number), now))
  }
  return rows.sort((a, b) =>
    Number(b.humanReply !== null) - Number(a.humanReply !== null) ||
    b.awaitingSince - a.awaitingSince
  )
}

/// Open tickets not updated for more than `staleDays` days (exactly N days is not stale).
/// Longest-idle first.
export async function staleSweep(
  deps: Pick<TriageDeps, "listIssues">,
  now: number,
  staleDays: number,
): Promise<StaleRow[]> {
  const byNumber = new Map<number, Issue>()
  for (const label of STALE_LABELS) {
    for (const issue of await deps.listIssues([label])) byNumber.set(issue.number, issue)
  }
  return [...byNumber.values()]
    .filter((i) => now - Date.parse(i.updatedAt) > staleDays * DAY_MS)
    .map((i) => ({
      number: i.number,
      title: i.title,
      labels: i.labels,
      state: i.state,
      createdAt: i.createdAt,
      updatedAt: i.updatedAt,
      idleDays: Math.round(((now - Date.parse(i.updatedAt)) / DAY_MS) * 10) / 10,
      url: `https://github.com/${config.repo}/issues/${i.number}`,
    }))
    .sort((a, b) => Date.parse(a.updatedAt) - Date.parse(b.updatedAt))
}

/// Clear a block so the ticket re-enters the queue (`ready`) or drafting (`draft`). Refuses a
/// ticket that is not blocked, so a stale command cannot quietly re-queue live work.
///
/// Guidance reaches the implementer through the issue body: the note (or, for `ready` without a
/// note, the newest human reply) is appended under `## Human guidance`. The ticket parser ignores
/// unknown sections, but `ticket.md` is the issue body verbatim, so the agent reads the section.
export async function unblock(
  deps: UnblockDeps,
  number: number,
  target: "ready" | "draft",
  opts: { note?: string } = {},
): Promise<void> {
  if (opts.note !== undefined && opts.note.trim() === "") {
    throw new Error("the note is empty; give words for the implementer or omit the note")
  }
  const issue = await deps.getIssue(number)
  if (!issue.labels.includes("factory:blocked")) {
    throw new Error(`#${number} is not blocked (no factory:blocked label); nothing to unblock`)
  }
  let note = opts.note
  if (note === undefined && target === "ready") {
    note = newestHumanReply(await deps.listComments(number))?.body
  }
  if (note !== undefined) {
    await deps.editIssue(number, `${issue.body}\n\n## Human guidance\n\n${note}\n`)
  }
  const by = await deps.login()
  await deps.editLabels(number, {
    add: [`factory:${target}`],
    remove: ["factory:blocked", "factory:needs-human"],
  })
  await deps.comment(number, `factory: unblocked by ${by} (${target}).`)
}

function parseApplyInput(raw: unknown): ApplyInput | string {
  if (typeof raw !== "object" || raw === null) return "entry must be an object"
  const number = "number" in raw ? raw.number : undefined
  const action = "action" in raw ? raw.action : undefined
  const note = "note" in raw ? raw.note : undefined
  if (typeof number !== "number" || !Number.isInteger(number)) return "number must be an integer"
  if (typeof action !== "string") return "action must be a string"
  if (note !== undefined && typeof note !== "string") return "note must be text"
  return { number, action, note }
}

/// Validate one proposed action against the live issue, then run it. Every rejection is a row.
async function applyOne(deps: ApplyDeps, raw: unknown, login: string): Promise<ApplyRow> {
  const parsed = parseApplyInput(raw)
  if (typeof parsed === "string") return { number: null, action: null, ok: false, problem: parsed }
  const { number, action, note } = parsed
  const reject = (problem: string): ApplyRow => ({ number, action, ok: false, problem })
  if (!APPLY_ACTIONS.includes(action)) return reject(`unknown action ${JSON.stringify(action)}`)
  if (note !== undefined && (typeof note !== "string" || note.length > NOTE_MAX)) {
    return reject(`note must be text of at most ${NOTE_MAX} characters`)
  }
  if (
    (action === "close" || action === "comment") && (note === undefined || note.trim() === "")
  ) {
    return reject(`${action} needs a non-empty note`)
  }
  let issue: Issue
  try {
    issue = await deps.getIssue(number)
  } catch {
    // Any lookup failure is reported as missing: the spec fixes this wording, and a transient gh
    // error reads the same way. The row says what was refused; the operator re-runs to tell apart.
    return reject("issue does not exist")
  }
  if (issue.state.toUpperCase() !== "OPEN") return reject("issue is not open")
  if (!issue.labels.some((l) => l.startsWith("factory:"))) {
    return reject("issue carries no factory: label")
  }
  try {
    switch (action) {
      case "unblock-ready":
      case "unblock-draft":
        await unblock(
          { ...deps, login: () => Promise.resolve(login) },
          number,
          action === "unblock-ready" ? "ready" : "draft",
          { note },
        )
        break
      case "close":
        await deps.closeIssue(number, note!)
        break
      case "comment":
        await deps.comment(number, `factory: triage — ${note}`)
        break
      case "needs-human":
        await deps.editLabels(number, {
          add: ["factory:blocked", "factory:needs-human"],
          remove: ["factory:ready", "factory:in-progress"],
        })
        await deps.comment(
          number,
          `factory: triage — ${note ?? "this ticket needs a human decision."}`,
        )
        break
    }
  } catch (error) {
    return reject(error instanceof Error ? error.message : String(error))
  }
  return { number, action, ok: true }
}

/// Apply triager proposals in order; each row says whether it ran or why it was refused.
export async function applyActions(
  deps: ApplyDeps,
  actions: unknown[],
  viewerLogin: string,
): Promise<ApplyRow[]> {
  const rows: ApplyRow[] = []
  for (const input of actions) rows.push(await applyOne(deps, input, viewerLogin))
  return rows
}

const liveDeps: TriageDeps & UnblockDeps & ApplyDeps = {
  listIssues,
  listComments,
  getIssue,
  editIssue: editIssueBody,
  editLabels,
  comment,
  closeIssue,
  login: async () => (await gh(["api", "user", "--jq", ".login"])).trim(),
}

function prettyNeedsHuman(rows: NeedsHumanRow[]): string {
  if (rows.length === 0) return "no blocked ticket is waiting on a human"
  return rows.map((r) => {
    const reply = r.humanReply
      ? `reply by ${r.humanReply.author} at ${r.humanReply.at}: ${r.humanReply.body.split("\n")[0]}`
      : "no human reply yet"
    return `#${r.number} ${r.title}\n  blocked ${r.awaitingSince}h; ${reply}\n  ${r.url}`
  }).join("\n")
}

function prettyStale(rows: StaleRow[]): string {
  if (rows.length === 0) return "no ticket is stale"
  return rows.map((r) =>
    `#${r.number} ${r.title}\n  idle ${r.idleDays}d [${r.labels.join(", ")}]\n  ${r.url}`
  ).join("\n")
}

/// `triage needs-human|stale`: read-only; JSON on stdout (default) or text with --pretty. Exit 0
/// even when nothing needs attention.
export async function triageCommand(
  kind: string,
  opts: { pretty: boolean; days: number },
): Promise<void> {
  const now = Date.now()
  if (kind === "needs-human") {
    const rows = await needsHumanFeed(liveDeps, now)
    console.log(opts.pretty ? prettyNeedsHuman(rows) : JSON.stringify(rows, null, 2))
  } else if (kind === "stale") {
    const rows = await staleSweep(liveDeps, now, opts.days)
    console.log(opts.pretty ? prettyStale(rows) : JSON.stringify(rows, null, 2))
  } else {
    console.error(`unknown triage kind "${kind}": use needs-human, stale or apply`)
    Deno.exit(2)
  }
}

export async function unblockCommand(
  number: number,
  target: "ready" | "draft",
  opts: { note?: string; noteFile?: string } = {},
): Promise<void> {
  try {
    if (opts.note !== undefined && opts.noteFile !== undefined) {
      throw new Error("give --note or --note-file, not both")
    }
    const note = opts.noteFile !== undefined ? await Deno.readTextFile(opts.noteFile) : opts.note
    await unblock(liveDeps, number, target, { note })
    console.log(`#${number} unblocked -> factory:${target}`)
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error))
    Deno.exit(1)
  }
}

/// `triage apply <file>`: the only writer for the nightly triager. Reads a JSON array of proposed
/// actions, validates each against the live issue, runs the valid ones, and prints one row per
/// action. Rejections are data (exit 0); an unreadable or malformed file exits 1.
export async function applyCommand(file: string | undefined): Promise<void> {
  if (file === undefined) {
    console.error("usage: triage apply <file.json>")
    Deno.exit(2)
  }
  let actions: unknown[]
  try {
    const parsed: unknown = JSON.parse(await Deno.readTextFile(file))
    if (!Array.isArray(parsed)) throw new Error("expected a JSON array of actions")
    actions = parsed
  } catch (error) {
    console.error(`cannot read ${file}: ${error instanceof Error ? error.message : error}`)
    Deno.exit(1)
  }
  const login = await liveDeps.login()
  console.log(JSON.stringify(await applyActions(liveDeps, actions, login), null, 2))
}
