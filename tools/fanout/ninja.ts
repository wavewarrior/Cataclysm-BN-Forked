/// Parsing of `ninja -t deps` output and `compile_commands.json` for `deno task fanout`.
/// Pure: everything takes text/objects in, no IO here.

/// One entry of `ninja -C <dir> -t deps`: an object file and its recorded dependencies.
export type NinjaDepsEntry = {
  /// The object path, exactly as ninja printed it (build-dir-relative).
  object: string
  /// Dependency paths as printed: build-dir-relative or absolute.
  deps: string[]
}

/// One entry of a CMake `compile_commands.json` (only the fields fanout reads).
export type CompileDbEntry = { directory: string; file: string; output?: string }

const DEPS_HEADER = /^(.*?): #deps \d+/

/// Parses the whole stdout of `ninja -t deps`. An entry starts at an unindented
/// `foo.o: #deps N, deps mtime ...` line; the indented lines below it are the dependencies.
/// A MISSING entry simply has no dep lines.
export function parseNinjaDeps(stdout: string): NinjaDepsEntry[] {
  const entries: NinjaDepsEntry[] = []
  let current: NinjaDepsEntry | null = null
  for (const line of stdout.split("\n")) {
    if (line === "") continue
    if (/^[ \t]/.test(line)) {
      current?.deps.push(line.trim())
      continue
    }
    const m = DEPS_HEADER.exec(line)
    if (!m) {
      current = null
      continue
    }
    current = { object: m[1], deps: [] }
    entries.push(current)
  }
  return entries
}

/// The checkout a compile database was generated in: the part of its `directory` before
/// `/out/build/` (a database may have been written in another checkout of the same tree),
/// or the first entry's directory when no preset layout is visible.
export function recordedRepoRoot(db: CompileDbEntry[]): string | null {
  for (const entry of db) {
    const dir = normalizeSlashes(entry.directory)
    const cut = dir.indexOf("/out/build/")
    if (cut > 0) return dir.slice(0, cut)
  }
  return db.length > 0 ? normalizeSlashes(db[0].directory) : null
}

/// Rewrites a database path recorded under `from` so it points into `to` instead.
export function remapRecordedPath(path: string, from: string | null, to: string): string {
  const p = normalizeSlashes(path)
  if (from !== null && (p === from || p.startsWith(`${from}/`))) return to + p.slice(from.length)
  return p
}

/// Joins a possibly relative path onto `baseDir`, collapsing `.`/`..` without ever popping
/// past the root (`/` or a Windows drive letter survive as unpopable anchors).
export function absolutize(path: string, baseDir: string): string {
  const p = normalizeSlashes(path)
  const joined = isAbsoluteLike(p) ? p : `${normalizeSlashes(baseDir)}/${p}`
  const drive = /^[A-Za-z]:(\/|$)/.test(joined) ? `${joined.slice(0, 2)}/` : ""
  const lead = drive === "" && joined.startsWith("/") ? "/" : ""
  const parts: string[] = []
  for (const part of (drive === "" ? joined : joined.slice(drive.length)).split("/")) {
    if (part === "" || part === ".") continue
    if (part === "..") {
      if (drive === "" && lead === "" && parts.length === 0) parts.push("..")
      else if (parts.length > 0) parts.pop()
    } else parts.push(part)
  }
  return (drive || lead) + parts.join("/")
}

/// The TUs whose ninja-recorded deps include `header`, as repo-relative paths.
///
/// A database written in another checkout (a shared or moved build dir) records that
/// checkout's root and its own absolute outputs; every recorded path is translated so it
/// resolves against THIS checkout and THIS build dir. An empty set means the deps log
/// cannot answer for this header, and the caller should fall back to the textual walk.
export function matchNinjaTuSet(input: {
  deps: NinjaDepsEntry[]
  db: CompileDbEntry[]
  buildDir: string
  root: string
  header: string
  sourcePrefixes: readonly string[]
}): Set<string> {
  const recordedRoot = recordedRepoRoot(input.db)
  const objectToFile = new Map<string, string>()
  for (const entry of input.db) {
    if (!entry.output) continue
    const file = remapRecordedPath(entry.file, recordedRoot, input.root)
    const rel = file.startsWith(`${input.root}/`) ? file.slice(input.root.length + 1) : file
    if (!rel.endsWith(".cpp") || !input.sourcePrefixes.some((p) => rel.startsWith(p))) continue
    // The output lives under the entry's own recorded directory; strip that prefix and
    // re-anchor at THIS build dir, wherever the database was generated.
    const dir = normalizeSlashes(entry.directory).replace(/\/+$/, "")
    const output = normalizeSlashes(entry.output)
    if (!output.startsWith(`${dir}/`)) continue
    objectToFile.set(absolutize(output.slice(dir.length + 1), input.buildDir), rel)
  }
  const needle = absolutize(input.header, input.root)
  const tus = new Set<string>()
  for (const entry of input.deps) {
    if (!entry.deps.some((dep) => absolutize(dep, input.buildDir) === needle)) continue
    const file = objectToFile.get(absolutize(entry.object, input.buildDir))
    if (file) tus.add(file)
  }
  return tus
}

function normalizeSlashes(path: string): string {
  return path.replaceAll("\\", "/")
}

function isAbsoluteLike(p: string): boolean {
  return p.startsWith("/") || /^[A-Za-z]:\//.test(p) || /^[A-Za-z]:$/.test(p)
}
