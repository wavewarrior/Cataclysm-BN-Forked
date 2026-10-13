// One-shot cleanup for the clang-tidy shrink: removes every `// *NOPAD*` marker
// and every NOLINT / NOLINTNEXTLINE comment naming a `cata-*` check that no
// longer exists in the plugin, under src/ and tests/.
//
// Whole-line comments are dropped; trailing comments are trimmed off the code
// line; a mixed list such as `NOLINT(readability-x, cata-text-style)` keeps the
// surviving names.
//
//   deno run -A tools/format/strip_obsolete_markers.ts --dry-run   # count per file
//   deno run -A tools/format/strip_obsolete_markers.ts             # rewrite

import { walk } from "@std/fs"

const KEPT_CHECKS = new Set(["cata-determinism", "cata-no-long", "cata-test-filename"])

/** True for a `cata-*` name the plugin no longer implements. */
export function isObsoleteCataCheck(name: string): boolean {
  return name.startsWith("cata-") && !KEPT_CHECKS.has(name)
}

const WHOLE_NOPAD = /^[ \t]*\/\/\s*\*NOPAD\*\s*$/
const TRAILING_NOPAD = /[ \t]*\/\/\s*\*NOPAD\*\s*$/
const NOLINT = /\b(NOLINTNEXTLINE|NOLINT)\(([^()]*)\)/g
/** A line left with nothing but comment punctuation is dropped outright. */
const COMMENT_RESIDUE = /^[ \t/*]*$/

export interface StripResult {
  text: string
  /** Markers removed (dropped comments and trimmed markers). */
  removals: number
}

/** Rewrite one file's contents; pure, so the unit test can drive it directly. */
export function stripText(source: string): StripResult {
  let removals = 0
  const out: string[] = []
  for (const line of source.split("\n")) {
    if (WHOLE_NOPAD.test(line)) {
      removals++
      continue
    }
    let text = line.replace(TRAILING_NOPAD, "")
    if (text !== line) removals++
    let cleaned = false
    text = text.replace(NOLINT, (full: string, keyword: string, list: string) => {
      const names = list.split(",").map((name) => name.trim()).filter((name) => name.length > 0)
      const kept = names.filter((name) => !isObsoleteCataCheck(name))
      if (kept.length === names.length) return full
      removals++
      cleaned = true
      return kept.length > 0 ? `${keyword}(${kept.join(", ")})` : ""
    })
    if (cleaned) {
      if (COMMENT_RESIDUE.test(text)) continue
      text = text.replace(/(\/\/|\/\*+\s*\*\/)[ \t]*$/, "").trimEnd()
    }
    out.push(text)
  }
  return { text: out.join("\n"), removals }
}

async function* projectCpp(roots: string[]): AsyncGenerator<string> {
  for (const root of roots) {
    for await (
      const entry of walk(root, {
        maxDepth: 10,
        includeDirs: false,
        match: [/\.(cpp|h|hpp)$/],
        skip: [/(^|\/)(lua|sol|third-party|catch)(\/|$)/],
      })
    ) {
      yield entry.path.replace(/^\.\//, "")
    }
  }
}

if (import.meta.main) {
  const dryRun = Deno.args.includes("--dry-run")
  const roots = Deno.args.filter((arg) => !arg.startsWith("-"))
  let total = 0
  for await (const file of projectCpp(roots.length > 0 ? roots : ["src", "tests"])) {
    const text = await Deno.readTextFile(file)
    if (!text.includes("*NOPAD*") && !text.includes("NOLINT")) continue
    const { text: stripped, removals } = stripText(text)
    if (removals === 0) continue
    total += removals
    console.log(`${file}: ${removals}`)
    if (!dryRun) await Deno.writeTextFile(file, stripped)
  }
  console.log(`${dryRun ? "would remove" : "removed"} ${total} obsolete marker(s)`)
}
