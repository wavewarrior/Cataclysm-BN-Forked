/// The protected-path guard: a lane may not edit the machinery that judges it.
///
/// Matching is deliberately dumb: an entry ending in `/**` protects that whole subtree, any other
/// entry protects exactly that file. Everything is lowercased first because the same tree is
/// edited on NTFS, where case carries no meaning.

/// What an unattended lane must never change: `check` itself, the lane runner, CI, the hooks, the
/// lint and format configs, and the file that tells agents what is allowed.
export const PROTECTED_PATHS = [
  "tools/check/**",
  "tools/lane/**",
  ".github/workflows/**",
  ".githooks/**",
  ".clang-format",
  ".clang-tidy",
  "tools/clang-tidy-plugin/**",
  "AGENTS.md",
] as const

type Rule = { subtree?: string; exact?: string }

const rules: Rule[] = PROTECTED_PATHS.map((entry) => {
  const rule = entry.toLowerCase().replaceAll("\\", "/")
  return rule.endsWith("/**") ? { subtree: rule.slice(0, -2) } : { exact: rule }
})

/// The changed files a lane is forbidden to touch, in the order git reported them.
export function guardViolations(files: readonly string[]): string[] {
  return files.filter((file) => {
    const path = file.toLowerCase().replaceAll("\\", "/").replace(/^\.\//, "")
    return rules.some((rule) =>
      rule.subtree !== undefined ? path.startsWith(rule.subtree) : path === rule.exact
    )
  })
}
