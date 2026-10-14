import { normalize } from "@std/path/posix"

// Which translation unit clang-tidy analyses a changed file through. Pure: tidy_changed.ts reads
// the compile database and runs git; the choice itself is here so it can be tested.

/// The repo-relative paths of every source file in a compile_commands.json, given the repo root.
/// A relative `file` entry is resolved against its `directory`.
export function compiledUnits(dbJson: string, root: string): Set<string> {
  const entries = JSON.parse(dbJson) as Array<{ file: string; directory: string }>
  const prefix = root.endsWith("/") ? root : `${root}/`
  const units = new Set<string>()
  for (const entry of entries) {
    const absolute = normalize(
      entry.file.startsWith("/") ? entry.file : `${entry.directory}/${entry.file}`,
    )
    if (absolute.startsWith(prefix)) units.add(absolute.slice(prefix.length))
  }
  return units
}

/// The first candidate the build actually compiles, or null. An empty `compiled` set means the
/// database could not be read for this root, so the first candidate stands rather than none.
export function chooseUnit(candidates: string[], compiled: Set<string>): string | null {
  if (compiled.size === 0) return candidates[0] ?? null
  return candidates.find((candidate) => compiled.has(candidate)) ?? null
}
