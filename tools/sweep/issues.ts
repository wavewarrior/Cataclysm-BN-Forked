/// The GitHub side of a sweep: one issue per platform-only failure, deduped by exact title, plus
/// one summary issue per run. Pure planning (`planActions`) separated from the `gh` calls
/// (`execute`), so the dedupe policy is unit-tested without the network.
import type { Gh, IssueAction, Platform } from "./types.ts"

export const LABEL_PREFIX = "platform:"
export const TITLE_PREFIX = "[platform-sweep]"

export function labelFor(platform: Platform): string {
  return `${LABEL_PREFIX}${platform}`
}

export function caseTitle(caseName: string, platform: Platform): string {
  return `${TITLE_PREFIX} ${caseName} (${platform})`
}

export function summaryTitle(platform: Platform): string {
  return `${TITLE_PREFIX} summary (${platform})`
}

const fence = (text: string): string => {
  const longest = Math.max(0, ...[...text.matchAll(/`+/g)].map((m) => m[0].length))
  const ticks = "`".repeat(longest + 1)
  return `${ticks}\n${text}\n${ticks}`
}

/// The body of a per-case issue: what ran, over what range, and the log excerpt.
export function caseBody(input: {
  platform: Platform
  caseName: string
  rangeLabel: string
  rangeLines: readonly string[]
  invocation: string
  excerpt: string
}): string {
  const commits = input.rangeLines.map((l) => `- \`${l}\``).join("\n")
  return [
    `A platform sweep on \`${input.platform}\` found \`${input.caseName}\` failing where Linux \`check\` does not, and the case is not in Quarantine (\`tests/quarantine.h\`). Per ADR-0005 this is platform divergence, not a per-change gate failure: the sweep opens this issue and the normal loop picks it up.`,
    ``,
    `**Platform:** ${input.platform}`,
    `**Invocation:** \`${input.invocation}\``,
    `**Commits since the watermark:** ${input.rangeLabel}`,
    ``,
    commits === "" ? "(no new commits: the watermark is HEAD)" : commits,
    ``,
    `**Log excerpt:**`,
    ``,
    fence(
      input.excerpt === ""
        ? "(the report recorded no assertion text; see the sweep logs)"
        : input.excerpt,
    ),
    ``,
    `Opened by \`tools/sweep\`; the watermark advanced past this range, so a repeat sweep only comments here if the case still fails.`,
  ].join("\n")
}

/// The body of the run's summary issue: counts, the quarantined and expired entries, the
/// cross-platform failures the sweep saw but did not file.
export function summaryBody(input: {
  platform: Platform
  rangeLabel: string
  exitCodes: Readonly<Record<string, number>>
  platformOnly: readonly string[]
  quarantined: readonly { name: string; expires: string }[]
  expiredStillFailing: readonly { name: string; expires: string }[]
  failsEverywhere: readonly string[]
}): string {
  const list = (items: readonly string[]) =>
    items.length === 0 ? "none" : items.map((n) => `- \`${n}\``).join("\n")
  const codes = Object.entries(input.exitCodes)
    .map(([label, code]) => `\`${label}\` exit ${code}`)
    .join(", ")
  return [
    `Platform sweep on \`${input.platform}\` over ${input.rangeLabel}. Exit codes: ${codes}.`,
    ``,
    `## Platform-only failures (issues filed or updated)`,
    ``,
    list(input.platformOnly),
    ``,
    `## Quarantined (excused, inside their window)`,
    ``,
    input.quarantined.length === 0
      ? "none"
      : input.quarantined.map((e) => `- \`${e.name}\` until ${e.expires}`).join("\n"),
    ``,
    `## Quarantine EXPIRED and still failing here`,
    ``,
    input.expiredStillFailing.length === 0 ? "none" : input.expiredStillFailing
      .map((e) => `- \`${e.name}\` (expired ${e.expires}) — fix it or delete it`)
      .join("\n"),
    ``,
    `## Failing on Linux too (not filed: not platform divergence)`,
    ``,
    list(input.failsEverywhere),
    ``,
    `Written by \`tools/sweep\`.`,
  ].join("\n")
}

/// What to do about a set of platform-only failures, given the open issues already carrying the
/// platform label: comment on the exact-title match, create otherwise. One action per case.
export function planActions(
  failures: readonly { caseName: string; body: string }[],
  platform: Platform,
  openIssues: readonly { number: number; title: string }[],
): IssueAction[] {
  const byTitle = new Map(openIssues.map((issue) => [issue.title, issue.number]))
  return failures.map(({ caseName, body }) => {
    const title = caseTitle(caseName, platform)
    const number = byTitle.get(title)
    return number === undefined
      ? { kind: "create" as const, title, body }
      : { kind: "comment" as const, title, number, body }
  })
}

/// The summary action: comment when the summary issue is open, create when not.
export function planSummary(
  body: string,
  platform: Platform,
  openIssues: readonly { number: number; title: string }[],
): IssueAction {
  const title = summaryTitle(platform)
  const found = openIssues.find((issue) => issue.title === title)
  return found === undefined
    ? { kind: "create", title, body }
    : { kind: "comment", title, number: found.number, body }
}

/// Run planned actions against the injected `gh` and return one line per action for the console.
export async function execute(
  gh: Gh,
  actions: readonly IssueAction[],
  platform: Platform,
): Promise<string[]> {
  const lines: string[] = []
  for (const action of actions) {
    if (action.kind === "create") {
      const number = await gh.createIssue(action.title, action.body, labelFor(platform))
      lines.push(`opened #${number}: ${action.title}`)
    } else {
      await gh.commentIssue(action.number!, action.body)
      lines.push(`commented on #${action.number}: ${action.title}`)
    }
  }
  return lines
}
