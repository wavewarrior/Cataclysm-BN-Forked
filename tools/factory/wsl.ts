/// Windows-side driver of the WSL lint lane (tools/factory/wsl/lane.sh).
import { dirname, fromFileUrl, join } from "@std/path"
import { lineFilterFromDiff } from "./line_filter.ts"
import { git, run, runToLog } from "./util.ts"

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
  const changedJsonPath = join(factoryDir, "changed-json.txt")
  await Deno.writeTextFile(
    changedJsonPath,
    await git(cwd, "diff", "--name-only", "--diff-filter=d", `${base}...HEAD`, "--", "*.json"),
  )

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
    toWslPath(changedJsonPath),
  ], logPath)
}
