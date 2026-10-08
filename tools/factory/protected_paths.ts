/// Fails when a change touches a protected path.
///
/// Used by gate step 1 (against the checked-out config) and by the `guard` CI job, which runs the
/// copy from the BASE ref with the BASE config so a PR cannot weaken its own check:
///   deno run -A base/tools/factory/protected_paths.ts --config base/tools/factory/config.json \
///     --repo head --base origin/feature/improvements
import { fromFileUrl } from "@std/path"
import { matchingGlobs } from "./glob.ts"
import { run } from "./util.ts"

export type Violation = { file: string; glob: string }

/// Changed files that match a protected glob.
export function violations(files: readonly string[], globs: readonly string[]): Violation[] {
  return files.flatMap((file) => {
    const hit = matchingGlobs(file, globs)
    return hit.length > 0 ? [{ file, glob: hit[0] }] : []
  })
}

/// Files changed on `head` since it forked from `base` (three-dot), renames reported as both ends.
export async function changedFiles(repo: string, base: string, head = "HEAD"): Promise<string[]> {
  const r = await run(["git", "diff", "--name-status", "-z", "--no-renames", `${base}...${head}`], {
    cwd: repo,
  })
  if (r.code !== 0) throw new Error(`git diff ${base}...${head} failed: ${r.stderr.trim()}`)
  const parts = r.stdout.split("\0").filter((p) => p.length > 0)
  // -z --name-status alternates status, path.
  return parts.filter((_, i) => i % 2 === 1)
}

function option(args: string[], name: string): string | undefined {
  const i = args.indexOf(name)
  return i >= 0 ? args[i + 1] : undefined
}

if (import.meta.main) {
  const args = Deno.args
  const repo = option(args, "--repo") ?? "."
  const configPath = option(args, "--config") ??
    fromFileUrl(new URL("./config.json", import.meta.url))
  const base = option(args, "--base") ?? "origin/feature/improvements"
  const globs: string[] = JSON.parse(await Deno.readTextFile(configPath)).protectedPaths
  const bad = violations(await changedFiles(repo, base), globs)
  if (bad.length > 0) {
    console.error("protected paths changed (a lane may not edit these; a human must):")
    for (const v of bad) console.error(`  ${v.file}  (${v.glob})`)
    Deno.exit(1)
  }
  console.log(`protected-paths: ok (${globs.length} globs checked against ${base}...HEAD)`)
}
