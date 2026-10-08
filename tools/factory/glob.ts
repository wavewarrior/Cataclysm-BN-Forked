// Dependency-free glob matching shared by the omp hook (runs under Bun), the gate and CI.
// Supported syntax: `**` (any depth, including none), `*` (inside one path segment), literals.

/// Normalise to forward slashes with no leading `./`.
export function normalizePath(path: string): string {
  return path.replaceAll("\\", "/").replace(/^\.\//, "")
}

/// Compile a repo-relative glob to an anchored RegExp.
export function globToRegExp(glob: string): RegExp {
  const g = normalizePath(glob)
  let out = ""
  for (let i = 0; i < g.length; i++) {
    const c = g[i]
    if (c === "*") {
      if (g[i + 1] === "*") {
        i++
        // `**/` matches zero or more whole segments; a trailing `**` matches the rest.
        if (g[i + 1] === "/") {
          i++
          out += "(?:.*/)?"
        } else {
          out += ".*"
        }
      } else {
        out += "[^/]*"
      }
    } else {
      out += c.replace(/[.+^${}()|[\]\\?]/g, "\\$&")
    }
  }
  // NTFS is case-insensitive: `TOOLS/Factory/x.ts` is the protected file.
  return new RegExp(`^${out}$`, "i")
}

/// True when `path` (repo-relative) matches any glob.
export function matchesAny(path: string, globs: readonly string[]): boolean {
  const p = normalizePath(path)
  return globs.some((glob) => globToRegExp(glob).test(p))
}

/// The globs that match `path`, for error messages.
export function matchingGlobs(path: string, globs: readonly string[]): string[] {
  const p = normalizePath(path)
  return globs.filter((glob) => globToRegExp(glob).test(p))
}
