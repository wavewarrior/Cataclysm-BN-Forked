/// Windows-side driver of the WSL lint lane (tools/factory/wsl/lane.sh).
import { dirname, fromFileUrl, join } from "@std/path"
import { git, run, runToLog } from "./util.ts"

export type LineRange = [number, number]
export type LineFilter = { name: string; lines: LineRange[] }[]

const CPP = /\.(?:cpp|h|hpp)$/

/// Parse `git diff -U0` output into clang-tidy's `--line-filter` shape. Only added/changed lines
/// count (the `+c,d` side of a hunk); pure deletions and non-C++ files are dropped.
export function lineFilterFromDiff(diff: string): LineFilter {
  const files = new Map<string, LineRange[]>()
  let current: string | undefined
  for (const line of diff.split(/\r?\n/)) {
    const file = line.match(/^\+\+\+ (?:b\/(.+)|\/dev\/null)$/)
    if (file) {
      current = file[1] !== undefined && CPP.test(file[1]) ? file[1] : undefined
      continue
    }
    const hunk = line.match(/^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@/)
    if (hunk && current !== undefined) {
      const start = Number(hunk[1])
      const count = hunk[2] === undefined ? 1 : Number(hunk[2])
      if (count === 0) continue
      const ranges = files.get(current) ?? []
      ranges.push([start, start + count - 1])
      files.set(current, ranges)
    }
  }
  return [...files].map(([name, lines]) => ({ name, lines }))
}

/// `C:\a\b` or `C:/a/b` -> `/mnt/c/a/b`.
export function toWslPath(path: string): string {
  const m = path.replaceAll("\\", "/").match(/^([A-Za-z]):\/(.*)$/)
  return m ? `/mnt/${m[1].toLowerCase()}/${m[2]}` : path
}

export type WslLaneOptions = {
  /// The lane worktree.
  cwd: string
  /// Ref to diff against, e.g. `origin/feature/improvements`.
  base: string
  /// Directory for the generated input files (the lane's `<gitdir>/factory`).
  factoryDir: string
  logPath: string
}

/// Run the lane and return its exit code. Writes affected/changed/line-filter files first.
export async function runWslLane(opts: WslLaneOptions): Promise<number> {
  const { cwd, base, factoryDir, logPath } = opts
  const sha = await git(cwd, "rev-parse", "HEAD")
  const branch = await git(cwd, "rev-parse", "--abbrev-ref", "HEAD")
  const commonDir = await git(cwd, "rev-parse", "--path-format=absolute", "--git-common-dir")

  const diff = await git(
    cwd,
    "diff",
    "-U0",
    "--no-color",
    `${base}...HEAD`,
    "--",
    "*.cpp",
    "*.h",
    "*.hpp",
  )
  const filter = lineFilterFromDiff(diff)
  const lineFilterPath = join(factoryDir, "line-filter.json")
  const changedPath = join(factoryDir, "changed-files.txt")
  const affectedPath = join(factoryDir, "affected-files.txt")
  await Deno.writeTextFile(lineFilterPath, JSON.stringify(filter))
  await Deno.writeTextFile(changedPath, filter.map((f) => f.name).join("\n"))

  const affected = await run(
    ["deno", "task", "affected-files", "--base", base, "--head", "HEAD", "--output", affectedPath],
    { cwd },
  )
  if (affected.code !== 0) {
    await Deno.writeTextFile(logPath, `affected-files failed:\n${affected.stderr}`)
    return 1
  }
  // An empty diff leaves no output file; there is nothing to lint.
  await Deno.writeTextFile(affectedPath, await Deno.readTextFile(affectedPath).catch(() => ""))

  const lane = toWslPath(
    join(dirname(fromFileUrl(import.meta.url)), "wsl", "lane.sh"),
  )
  return await runToLog([
    "wsl",
    "-d",
    "Ubuntu",
    "-u",
    "root",
    "-e",
    "bash",
    lane,
    toWslPath(commonDir),
    branch,
    sha,
    toWslPath(affectedPath),
    toWslPath(lineFilterPath),
    toWslPath(changedPath),
  ], logPath)
}
