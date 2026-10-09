/// Thin wrappers over the GitHub CLI. Only the driver uses these; a lane's hook blocks `gh`.
import { config } from "./config.ts"
import { run } from "./util.ts"

const GH_WINDOWS = "C:\\Program Files\\GitHub CLI\\gh.exe"

function ghBinary(): string {
  if (Deno.build.os !== "windows") return "gh"
  try {
    Deno.statSync(GH_WINDOWS)
    return GH_WINDOWS
  } catch {
    return "gh"
  }
}

export async function gh(args: string[], stdin?: string): Promise<string> {
  const r = await run([ghBinary(), ...args], { stdin })
  if (r.code !== 0) throw new Error(`gh ${args.join(" ")} failed (${r.code}): ${r.stderr.trim()}`)
  return r.stdout
}

export type Issue = { number: number; title: string; body: string; labels: string[]; state: string }

type RawIssue = {
  number: number
  title: string
  body: string | null
  labels: { name: string }[]
  state: string
}

const FIELDS = "number,title,body,labels,state"

function toIssue(raw: RawIssue): Issue {
  return {
    number: raw.number,
    title: raw.title,
    body: raw.body ?? "",
    labels: raw.labels.map((l) => l.name),
    state: raw.state,
  }
}

export async function listIssues(labels: string[], state = "open"): Promise<Issue[]> {
  const args = [
    "issue",
    "list",
    "--repo",
    config.repo,
    "--state",
    state,
    "--limit",
    "200",
    "--json",
    FIELDS,
  ]
  for (const l of labels) args.push("--label", l)
  return (JSON.parse(await gh(args)) as RawIssue[]).map(toIssue)
}

export async function getIssue(number: number): Promise<Issue> {
  const out = await gh(["issue", "view", String(number), "--repo", config.repo, "--json", FIELDS])
  return toIssue(JSON.parse(out))
}

export async function isClosed(number: number): Promise<boolean> {
  return (await getIssue(number)).state.toUpperCase() === "CLOSED"
}

export async function editLabels(
  number: number,
  change: { add?: string[]; remove?: string[] },
): Promise<void> {
  const args = ["issue", "edit", String(number), "--repo", config.repo]
  for (const l of change.add ?? []) args.push("--add-label", l)
  for (const l of change.remove ?? []) args.push("--remove-label", l)
  await gh(args)
}

export async function comment(number: number, body: string): Promise<void> {
  await gh(["issue", "comment", String(number), "--repo", config.repo, "--body-file", "-"], body)
}

export type PrOptions = { head: string; title: string; body: string }

/// Open a draft PR into the integration branch; returns its URL.
export async function createDraftPr(opts: PrOptions): Promise<string> {
  const out = await gh([
    "pr",
    "create",
    "--repo",
    config.repo,
    "--draft",
    "--base",
    config.integrationBranch,
    "--head",
    opts.head,
    "--title",
    opts.title,
    "--body-file",
    "-",
  ], opts.body)
  return out.trim().split(/\s+/).at(-1) ?? ""
}

export async function addPrLabel(pr: string, label: string): Promise<void> {
  await gh(["pr", "edit", pr, "--repo", config.repo, "--add-label", label])
}

export async function actionsEnabled(): Promise<boolean> {
  const out = await gh(["api", `repos/${config.repo}/actions/permissions`])
  return JSON.parse(out).enabled === true
}

/// Labels the factory uses; created once with `deno task factory labels`.
export const LABELS: { name: string; color: string; description: string }[] = [
  { name: "factory:draft", color: "ededed", description: "Ticket written, spec not yet approved" },
  { name: "factory:ready", color: "0e8a16", description: "Approved; the driver may pick it up" },
  { name: "factory:in-progress", color: "fbca04", description: "A lane is working on it" },
  { name: "factory:review", color: "1d76db", description: "Draft PR open; awaiting the human" },
  { name: "factory:blocked", color: "b60205", description: "The driver stopped; see the comment" },
  { name: "gameplay", color: "c5def5", description: "Needs a bnplay Episode to verify" },
  { name: "render", color: "c5def5", description: "Renderer change; needs a bnplay Episode" },
]

export async function ensureLabel(label: { name: string; color: string; description: string }) {
  await gh([
    "label",
    "create",
    label.name,
    "--repo",
    config.repo,
    "--color",
    label.color,
    "--description",
    label.description,
    "--force",
  ])
}

/// Create an issue; returns its number and URL.
export async function createIssue(opts: {
  title: string
  body: string
  labels: string[]
}): Promise<{ number: number; url: string }> {
  const args = ["issue", "create", "--repo", config.repo, "--title", opts.title, "--body-file", "-"]
  for (const l of opts.labels) args.push("--label", l)
  const url = (await gh(args, opts.body)).trim().split(/\s+/).at(-1) ?? ""
  return { number: Number(url.split("/").at(-1)), url }
}

/// Create the label if it does not exist yet (spec:<slug> labels are made on demand).
export async function ensureSpecLabel(name: string): Promise<void> {
  await ensureLabel({ name, color: "ededed", description: "Factory spec group" })
}

export type MapStatus = {
  title: string
  labels: string[]
  openChildren: { number: number; title: string }[]
  /// The map body's `## Not yet specified` text, trimmed (fog still on the map).
  fog: string
}

/// Whether a wayfinder map has reached its destination: no open child tickets and no fog.
export async function mapStatus(number: number): Promise<MapStatus> {
  const issue = await getIssue(number)
  const children = JSON.parse(
    await gh(["api", "--paginate", `repos/${config.repo}/issues/${number}/sub_issues`]),
  ) as { number: number; title: string; state: string }[]
  const fog = issue.body.match(/## Not yet specified\s*([\s\S]*?)(?=\n## |$)/)?.[1]
    ?.replace(/<!--[\s\S]*?-->/g, "").trim() ?? ""
  return {
    title: issue.title,
    labels: issue.labels,
    openChildren: children.filter((c) => c.state === "open").map(({ number, title }) => ({
      number,
      title,
    })),
    fog,
  }
}
