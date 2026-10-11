/// Triage of tickets that need a person: deterministic collectors (no LLM) for a nightly triager,
/// and `unblock`, the one verb here that writes. Effects arrive as deps so tests can stub gh.
import { config } from "./config.ts"
import {
  comment,
  type CommentInfo,
  editLabels,
  getIssue,
  gh,
  type Issue,
  listComments,
  listIssues,
} from "./gh.ts"

const DAY_MS = 86_400_000
const HOUR_MS = 3_600_000
/// Comment authors whose words count as a human reply. NONE, BOT and MANNEQUIN do not.
const HUMAN_ASSOCIATIONS = ["OWNER", "MEMBER", "COLLABORATOR", "CONTRIBUTOR"]
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

export type TriageDeps = {
  listIssues: (labels: string[]) => Promise<Issue[]>
  listComments: (number: number) => Promise<CommentInfo[]>
}

export type UnblockDeps = {
  getIssue: (number: number) => Promise<Issue>
  editLabels: (number: number, change: { add?: string[]; remove?: string[] }) => Promise<void>
  comment: (number: number, body: string) => Promise<void>
  login: () => Promise<string>
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
  const replies = comments
    .filter((c) =>
      HUMAN_ASSOCIATIONS.includes(c.association) && Date.parse(c.publishedAt) > blockedMs
    )
    .sort((a, b) => Date.parse(b.publishedAt) - Date.parse(a.publishedAt))
  const reply = replies[0]
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
export async function unblock(
  deps: UnblockDeps,
  number: number,
  target: "ready" | "draft",
): Promise<void> {
  const issue = await deps.getIssue(number)
  if (!issue.labels.includes("factory:blocked")) {
    throw new Error(`#${number} is not blocked (no factory:blocked label); nothing to unblock`)
  }
  const by = await deps.login()
  await deps.editLabels(number, {
    add: [`factory:${target}`],
    remove: ["factory:blocked", "factory:needs-human"],
  })
  await deps.comment(number, `factory: unblocked by ${by} (${target}).`)
}

const liveDeps: TriageDeps & UnblockDeps = {
  listIssues,
  listComments,
  getIssue,
  editLabels,
  comment,
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
    console.error(`unknown triage kind "${kind}": use needs-human or stale`)
    Deno.exit(2)
  }
}

export async function unblockCommand(number: number, target: "ready" | "draft"): Promise<void> {
  try {
    await unblock(liveDeps, number, target)
    console.log(`#${number} unblocked -> factory:${target}`)
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error))
    Deno.exit(1)
  }
}
