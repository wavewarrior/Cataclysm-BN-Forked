/// The pure half of `deno task fanout`: an include graph built from file contents, with no IO.
/// Keys are repo-relative POSIX paths (`src/map.h`); values are the file texts.

export type Graph = {
  /// For every file, the graph files it directly includes (self-edges dropped).
  includes: Map<string, string[]>
  /// Includes that named a quoted path no resolver rule could find, as `[includer, spec]`.
  missing: [string, string][]
}

/// Builds the reverse edge map: for every file, the files that directly include it.
export function reverseEdges(graph: Graph): Map<string, string[]> {
  const rev = new Map<string, string[]>()
  for (const [from, tos] of graph.includes) {
    for (const to of tos) {
      const list = rev.get(to)
      if (list) list.push(from)
      else rev.set(to, [from])
    }
  }
  return rev
}

/// Who reaches `target` by walking reverse include edges, transitively, with a visited set so
/// cycles terminate. The target itself is never in the result.
export function reachers(rev: Map<string, string[]>, target: string): Set<string> {
  const seen = new Set<string>([target])
  const out = new Set<string>()
  const stack = [target]
  while (stack.length > 0) {
    for (const inc of rev.get(stack.pop()!) ?? []) {
      if (!seen.has(inc)) {
        seen.add(inc)
        out.add(inc)
        stack.push(inc)
      }
    }
  }
  return out
}

export type Fanout = {
  header: string
  /// Translation units (`.cpp` under `src/` or `tests/`) that reach the header.
  tus: string[]
  srcTus: string[]
  testTus: string[]
  /// Files that include the header directly (headers and TUs alike).
  directIncluders: string[]
  /// Headers among the reachers: they carry the header into every TU above them.
  propagators: string[]
}

/// The fan-out report for one header over one graph.
export function fanoutOf(graph: Graph, header: string): Fanout {
  const rev = reverseEdges(graph)
  const who = reachers(rev, header)
  const tus = [...who].filter((f) => f.endsWith(".cpp")).sort()
  return {
    header,
    tus,
    srcTus: tus.filter((f) => f.startsWith("src/")),
    testTus: tus.filter((f) => f.startsWith("tests/")),
    directIncluders: (rev.get(header) ?? []).slice().sort(),
    propagators: [...who].filter((f) => f.endsWith(".h") || f.endsWith(".hpp")).sort(),
  }
}

const LINE_COMMENT = /\/\/[^\n]*/g
const QUOTED_INCLUDE = /^[ \t]*#[ \t]*include[ \t]*"([^"]+)"/

/// The quoted include specs of one file, line comments stripped, angle includes ignored.
export function parseIncludes(text: string): string[] {
  const out: string[] = []
  for (const line of text.replace(LINE_COMMENT, "").split("\n")) {
    const m = QUOTED_INCLUDE.exec(line)
    if (m) out.push(m[1])
  }
  return out
}

/// Resolves one quoted spec from `includer`: the includer's directory first, then `src/`.
/// Returns the repo-relative path, or null when neither candidate is a known file.
export function resolveInclude(
  spec: string,
  includer: string,
  known: ReadonlySet<string>,
): string | null {
  const dir = includer.includes("/") ? includer.slice(0, includer.lastIndexOf("/")) : ""
  for (const base of [dir, "src"]) {
    const joined = normalizePath(base ? `${base}/${spec}` : spec)
    if (known.has(joined)) return joined
  }
  return null
}

export function normalizePath(path: string): string {
  const parts: string[] = []
  for (const part of path.replaceAll("\\", "/").split("/")) {
    if (part === "" || part === ".") continue
    if (part === "..") parts.pop()
    else parts.push(part)
  }
  return parts.join("/")
}

/// Builds the graph from `name -> text` pairs: parse, resolve, drop self-edges, record misses.
export function buildGraph(files: ReadonlyMap<string, string>): Graph {
  const known = new Set(files.keys())
  const includes = new Map<string, string[]>()
  const missing: [string, string][] = []
  for (const [path, text] of files) {
    const edges: string[] = []
    for (const spec of parseIncludes(text)) {
      const resolved = resolveInclude(spec, path, known)
      if (resolved === null) missing.push([path, spec])
      else if (resolved !== path && !edges.includes(resolved)) edges.push(resolved)
    }
    includes.set(path, edges)
  }
  return { includes, missing }
}
