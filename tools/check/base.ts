/// The diff base `check` compares against when `--base` is absent, and the range syntax it uses.
/// Its own file so the argument parser's help text and the runner read one value.
export const DEFAULT_BASE = "origin/feature/improvements"

/// Three-dot range: what this change added since it forked from the base.
export function diffRange(base: string, head = "HEAD"): string {
  return `${base}...${head}`
}
