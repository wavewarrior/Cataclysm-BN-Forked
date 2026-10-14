/// The pass stamp the pre-push hook trusts. Pure: main.ts gathers the facts and writes the file.

export interface StampFacts {
  /// The exit code `check` is about to return.
  code: number
  /// `git rev-parse HEAD` when the run started, and again when it finished.
  headAtStart: string
  headAtEnd: string
  /// `git status --porcelain` lines (tracked changes and untracked, not ignored files) before the
  /// build and after the last step. Untracked counts: the `src/` and `tests/` `.cpp` globs compile
  /// an untracked file into the binary the run tests, though the commit lacks it.
  dirtyAtStart: string[]
  dirtyAtEnd: string[]
}

/// The commit to record, or null when the run proves nothing about a commit: a failing run, a tree
/// that differed from HEAD before or after (auto-fix rewrites files; uncommitted edits get tested),
/// or a HEAD that moved mid-run.
export function stampedCommit(facts: StampFacts): string | null {
  if (facts.code !== 0) return null
  if (facts.headAtStart === "" || facts.headAtStart !== facts.headAtEnd) return null
  if (facts.dirtyAtStart.length > 0 || facts.dirtyAtEnd.length > 0) return null
  return facts.headAtStart
}

/// Porcelain lines that count against a stamp: everything except the run's own log directory.
export function dirtyLines(porcelain: string, logDir: string): string[] {
  const own = `${logDir.replace(/^\.\//, "").replace(/\/$/, "")}/`
  return porcelain
    .split("\n")
    .map((line) => line.trimEnd())
    .filter((line) => line !== "" && !line.slice(3).startsWith(own))
}
