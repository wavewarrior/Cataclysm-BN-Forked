/// Turns a `git diff -U0` into clang-tidy's `--line-filter` JSON, so lint failures can only come
/// from lines this change added or edited. Used by the WSL lane and the `tidy` CI job.
///   deno run -A tools/factory/line_filter.ts --base <ref> --output <file> [--changed <file>]
import { git } from "./util.ts"

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

function option(args: string[], name: string): string | undefined {
  const i = args.indexOf(name)
  return i >= 0 ? args[i + 1] : undefined
}

if (import.meta.main) {
  const base = option(Deno.args, "--base")
  const output = option(Deno.args, "--output")
  if (!base || !output) {
    console.error("usage: line_filter.ts --base <ref> --output <file> [--changed <file>]")
    Deno.exit(2)
  }
  const diff = await git(
    ".",
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
  await Deno.writeTextFile(output, JSON.stringify(filter))
  const changed = option(Deno.args, "--changed")
  if (changed) await Deno.writeTextFile(changed, filter.map((f) => f.name).join("\n"))
  console.log(`line filter: ${filter.length} file(s)`)
}
