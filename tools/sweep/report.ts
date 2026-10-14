/// Reads the Catch2 XML report a sweep invocation wrote. The XML reporter is the one format whose
/// `TestCase name=` attribute is the exact case name: the JUnit reporter joins section names with
/// `/` and several real case names contain `/`, so it cannot be trusted to identify a case.
///
/// Shape (catch_amalgamated.cpp `XmlReporter`): one `<TestCase name="..." tags="...">` per case,
/// closed by `<OverallResult success="true|false"/>`; failed assertions appear inside as
/// `<Expression success="false" type="REQUIRE" ...>` with `<Original>`/`<Expanded>` children, or
/// as `<Exception>` / `<FatalErrorCondition>`. A case that crashes the process never gets its
/// `OverallResult`, so an unterminated final `TestCase` block is a crash in that case.

/// A failed case as the report knows it; the runner adds which invocation reported it.
export type ParsedFailure = { name: string; excerpt: string }

const CASE_START = /<TestCase\b([^>]*)>/
const NAME_ATTR = /\bname="([^"]*)"/

const ENTITIES: Record<string, string> = {
  amp: "&",
  lt: "<",
  gt: ">",
  quot: '"',
  apos: "'",
}

function decode(text: string): string {
  return text.replace(/&(#x?[0-9a-fA-F]+|[a-zA-Z]+);/g, (whole, code: string) => {
    if (code.startsWith("#x") || code.startsWith("#X")) {
      return String.fromCodePoint(parseInt(code.slice(2), 16))
    }
    if (code.startsWith("#")) return String.fromCodePoint(Number(code.slice(1)))
    return ENTITIES[code] ?? whole
  })
}

/// The lines worth quoting from one failed case's XML: each failed assertion's macro, expression,
/// expansion and location, plus any exception text. Deliberately dumb and generous: an issue body
/// with a few lines of noise beats one that lost the reason.
function excerptOf(block: string): string {
  const lines: string[] = []
  const ASSERTION = /<(Expression|Exception|FatalErrorCondition)\b([^>]*)>([\s\S]*?)<\/\1>/g
  for (const match of block.matchAll(ASSERTION)) {
    const [, tag, attrs, inner] = match
    if (tag === "Expression" && !/\bsuccess="false"/.test(attrs)) continue
    const file = /\bfilename="([^"]*)"/.exec(attrs)?.[1]
    const line = /\bline="(\d+)"/.exec(attrs)?.[1]
    const macro = /\btype="([^"]*)"/.exec(attrs)?.[1]
    const original = /<Original>([\s\S]*?)<\/Original>/.exec(inner)?.[1]
    const expanded = /<Expanded>([\s\S]*?)<\/Expanded>/.exec(inner)?.[1]
    const where = file !== undefined ? ` (${file}:${line ?? "?"})` : ""
    if (tag === "Expression") {
      lines.push(`${macro ?? "assertion"} FAILED${where}: ${decode(original ?? "").trim()}`)
      if (expanded !== undefined) lines.push(`  expanded: ${decode(expanded).trim()}`)
    } else {
      const text = decode(inner.replace(/<\/?(Original|Expanded)>/g, " ").trim())
      lines.push(`${tag}${where}: ${text.split("\n")[0]}`)
    }
  }
  return lines.slice(0, 12).join("\n")
}

/// The failed cases in a Catch2 XML report, with excerpts. `crashed` names the case the process
/// died inside, when the report ends mid-case.
export function parseCatchXml(xml: string): { failures: ParsedFailure[]; crashed: string | null } {
  const failures: ParsedFailure[] = []
  let crashed: string | null = null
  const blocks = xml.split(CASE_START)
  // split() alternates: preamble, attrs, block, attrs, block, ...
  for (let i = 1; i < blocks.length; i += 2) {
    const name = NAME_ATTR.exec(blocks[i])
    if (name === null) continue
    const block = blocks[i + 1] ?? ""
    const overall = /<OverallResult\b[^>]*\bsuccess="(true|false)"/.exec(block)
    if (overall === null) {
      // The last unterminated case is the one the process died in; anything earlier means the
      // report itself is corrupt, which the caller learns through `crashed` too.
      if (i + 2 >= blocks.length) crashed = decode(name[1])
      continue
    }
    if (overall[1] === "false") {
      failures.push({ name: decode(name[1]), excerpt: excerptOf(block) })
    }
  }
  return { failures, crashed }
}

/// The excerpt for a case that crashed the run: the tail of the console log, since the XML has
/// nothing for it.
export function crashExcerpt(consoleLog: string, lines = 20): string {
  const kept = consoleLog.split("\n").filter((l) => l.trim() !== "").slice(-lines)
  return `the test process died inside this case; last console output:\n${kept.join("\n")}`
}
