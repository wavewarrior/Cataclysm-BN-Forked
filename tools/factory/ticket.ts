/// A factory ticket is a GitHub issue whose body follows `.github/ISSUE_TEMPLATE/factory-ticket.md`.
/// This module is the single parser shared by the driver and the gate.

export const SECTIONS = [
  "Goal",
  "Acceptance",
  "Touches",
  "Test tags",
  "Episodes",
  "Depends on",
  "No test needed",
] as const
export type SectionName = (typeof SECTIONS)[number]

/// Sections a ticket cannot run without.
export const REQUIRED_SECTIONS: readonly SectionName[] = [
  "Goal",
  "Acceptance",
  "Touches",
  "Test tags",
]

export type Ticket = {
  number: number
  title: string
  body: string
  labels: string[]
  sections: Partial<Record<SectionName, string>>
}

/// Headings the `to-tickets` skill writes, accepted for the factory's own names.
const ALIASES: Record<string, SectionName> = {
  "what to build": "Goal",
  "acceptance criteria": "Acceptance",
  "blocked by": "Depends on",
}

/// Split a markdown body on its `## ` headings. Unknown headings are ignored.
export function parseSections(body: string): Partial<Record<SectionName, string>> {
  const out: Partial<Record<SectionName, string>> = {}
  let current: SectionName | undefined
  let lines: string[] = []
  const flush = () => {
    if (current !== undefined) out[current] = lines.join("\n").trim()
  }
  for (const line of body.replace(/\r\n/g, "\n").split("\n")) {
    const heading = line.match(/^##\s+(.+?)\s*$/)
    if (heading) {
      flush()
      const key = heading[1].toLowerCase()
      const name = SECTIONS.find((s) => s.toLowerCase() === key) ?? ALIASES[key]
      current = name
      lines = []
    } else if (current !== undefined) {
      lines.push(line)
    }
  }
  flush()
  return out
}

/// Section names that are required but absent or blank.
export function missingSections(sections: Ticket["sections"]): SectionName[] {
  return REQUIRED_SECTIONS.filter((name) => !sections[name]?.trim())
}

/// One entry per non-blank line; list bullets, backticks and trailing comments are stripped.
export function listItems(section: string | undefined): string[] {
  return (section ?? "")
    .split("\n")
    .map((l) => l.replace(/^\s*(?:[-*+]|\d+\.)\s+/, "").replace(/`/g, "").trim())
    .filter((l) => l.length > 0 && !l.startsWith("<!--"))
}

/// Issue numbers named in `## Depends on` (`#12`, `12`, `owner/repo#12`).
export function dependsOn(sections: Ticket["sections"]): number[] {
  return listItems(sections["Depends on"]).flatMap((item) => {
    const m = item.match(/#?(\d+)/)
    return m ? [Number(m[1])] : []
  })
}

export function makeTicket(issue: {
  number: number
  title: string
  body: string
  labels: string[]
}): Ticket {
  return { ...issue, sections: parseSections(issue.body) }
}

/// `factory/<issue>-<slug>`; the slug is lowercase ascii words from the title, at most 40 chars.
export function branchName(prefix: string, number: number, title: string): string {
  const slug = title
    .toLowerCase()
    .replace(/^[a-z]+(?:\([^)]*\))?!?:\s*/, "")
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "")
    .slice(0, 40)
    .replace(/-+$/, "")
  return `${prefix}${number}-${slug || "ticket"}`
}
