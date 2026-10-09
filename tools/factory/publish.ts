/// Turn an approved breakdown into factory tickets: validate, order by dependency, render each body
/// in the factory-ticket template and create the issues (blockers first, so `## Depends on` can
/// name real issue numbers). Used by the `factory-launch` skill via `deno task factory publish`.
import { config } from "./config.ts"
import { matchesAny } from "./glob.ts"
import { isConventionalTitle } from "./ticket.ts"

export type TicketDraft = {
  /// Local handle other drafts use in `dependsOn`; never shown to GitHub.
  key: string
  /// Conventional-commit title: it becomes the PR title.
  title: string
  goal: string
  acceptance: string
  touches: string[]
  /// Catch2 tags or test names the gate must run.
  testTags: string[]
  /// bnplay Trials (`<trial.toml>` or `<trial.toml> <steps.jsonl>`); needed for gameplay/render.
  episodes?: string[]
  /// Keys of drafts that must be merged first, or existing issues as `#12`.
  dependsOn?: string[]
  noTestNeeded?: string
  /// Extra labels such as `gameplay` or `render`.
  labels?: string[]
}

const blank = (s: string | undefined) => !s || s.trim() === ""

/// Every reason the set cannot be published; empty means it can.
export function validateDrafts(
  drafts: readonly TicketDraft[],
  protectedGlobs: readonly string[] = config.protectedPaths,
): string[] {
  const problems: string[] = []
  const keys = new Set<string>()
  for (const d of drafts) {
    const at = `[${d.key || "?"}]`
    if (blank(d.key)) problems.push(`${at} a draft has no key`)
    else if (keys.has(d.key)) problems.push(`${at} duplicate key`)
    keys.add(d.key)
    if (!isConventionalTitle(d.title)) {
      problems.push(`${at} title must be a conventional-commit title (type(scope): summary)`)
    }
    if (blank(d.goal)) problems.push(`${at} goal is empty`)
    if (blank(d.acceptance)) problems.push(`${at} acceptance is empty`)
    if (d.touches.length === 0) problems.push(`${at} touches no paths`)
    if (d.testTags.length === 0) problems.push(`${at} names no test tags for the gate to run`)
    for (const t of d.touches) {
      const path = t.replace(/^\.\//, "").replace(/\/+$/, "")
      if (matchesAny(path, protectedGlobs) || matchesAny(`${path}/x`, protectedGlobs)) {
        problems.push(
          `${at} touches protected path ${t}: a lane can never change it; make it a human task`,
        )
      }
    }
    const needsEpisode = (d.labels ?? []).some((l) => l === "gameplay" || l === "render")
    if (needsEpisode && (d.episodes ?? []).length === 0) {
      problems.push(`${at} is labelled gameplay/render but lists no episodes`)
    }
  }
  for (const d of drafts) {
    for (const dep of d.dependsOn ?? []) {
      if (!/^#\d+$/.test(dep) && !keys.has(dep)) {
        problems.push(`[${d.key}] depends on unknown key ${dep}`)
      }
      if (dep === d.key) problems.push(`[${d.key}] depends on itself`)
    }
  }
  if (problems.length === 0 && orderDrafts(drafts) === undefined) {
    problems.push("the dependencies contain a cycle")
  }
  return problems
}

/// Blockers first (stable among independents), or undefined when there is a cycle.
export function orderDrafts(drafts: readonly TicketDraft[]): TicketDraft[] | undefined {
  const byKey = new Map(drafts.map((d) => [d.key, d]))
  const out: TicketDraft[] = []
  const state = new Map<string, "visiting" | "done">()
  let cycle = false
  const visit = (d: TicketDraft) => {
    const s = state.get(d.key)
    if (s === "done") return
    if (s === "visiting") {
      cycle = true
      return
    }
    state.set(d.key, "visiting")
    for (const dep of d.dependsOn ?? []) {
      const target = byKey.get(dep)
      if (target) visit(target)
    }
    state.set(d.key, "done")
    out.push(d)
  }
  for (const d of drafts) visit(d)
  return cycle ? undefined : out
}

const bullets = (items: readonly string[]) => items.map((i) => `- ${i}`).join("\n")

/// The issue body in the factory-ticket template. `numberOf` maps a dependency key to its issue.
export function renderBody(
  d: TicketDraft,
  opts: { parent?: number; numberOf: (key: string) => number },
): string {
  const parts = [
    ...(opts.parent ? [`Part of #${opts.parent}.\n`] : []),
    `## Goal\n\n${d.goal.trim()}\n`,
    `## Acceptance\n\n${d.acceptance.trim()}\n`,
    `## Touches\n\n${bullets(d.touches)}\n`,
    `## Test tags\n\n${bullets(d.testTags)}\n`,
  ]
  if ((d.episodes ?? []).length > 0) parts.push(`## Episodes\n\n${bullets(d.episodes!)}\n`)
  const deps = (d.dependsOn ?? []).map((k) => k.startsWith("#") ? k : `#${opts.numberOf(k)}`)
  if (deps.length > 0) parts.push(`## Depends on\n\n${bullets(deps)}\n`)
  if (!blank(d.noTestNeeded)) parts.push(`## No test needed\n\n${d.noTestNeeded!.trim()}\n`)
  return parts.join("\n")
}

export type Published = { key: string; number: number; url: string }

/// Create the issues in dependency order. `create` is injected so tests need no network.
export async function publishDrafts(opts: {
  drafts: readonly TicketDraft[]
  slug: string
  parent?: number
  create: (
    issue: { title: string; body: string; labels: string[] },
  ) => Promise<{ number: number; url: string }>
}): Promise<Published[]> {
  const problems = validateDrafts(opts.drafts)
  if (problems.length > 0) throw new Error(`cannot publish:\n- ${problems.join("\n- ")}`)
  const ordered = orderDrafts(opts.drafts)!
  const numbers = new Map<string, number>()
  const out: Published[] = []
  for (const d of ordered) {
    const body = renderBody(d, { parent: opts.parent, numberOf: (k) => numbers.get(k)! })
    const made = await opts.create({
      title: d.title,
      body,
      labels: ["factory:draft", `spec:${opts.slug}`, ...(d.labels ?? [])],
    })
    numbers.set(d.key, made.number)
    out.push({ key: d.key, ...made })
  }
  return out
}
