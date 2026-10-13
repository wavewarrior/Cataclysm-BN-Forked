// deno task fmt:cpp [files...] - format C++ with the single pinned clang-format.
//
// Style is auto-fixed and never a gate failure, so this command exits 0 when it
// runs and non-zero only when it could not run at all.
//
// By default only CHANGED lines are touched: with files, the lines of those
// files changed against the base (or the staged hunks with --staged); with no
// files, the C++ files changed against the base, each restricted to its changed
// hunks. Newly added files are formatted whole. Whole-file formatting of
// existing files is opt-in (--all, --whole-file) because the tree has not been
// reformatted with this tool yet.

import {
  allProjectCpp,
  changedCppFiles,
  changedLineRanges,
  CLANG_FORMAT_VERSION,
  DEFAULT_BASE,
  isAddedFile,
  isTracked,
  parseFlags,
  pinnedClangFormat,
} from "./cpp_common.ts"

interface Options {
  files: string[]
  base: string
  staged: boolean
  all: boolean
  wholeFile: boolean
  dryRun: boolean
  help: boolean
}

const USAGE = `usage: deno task fmt:cpp [options] [files...]

Formats C++ with the pinned clang-format ${CLANG_FORMAT_VERSION} (bootstrapped on
demand into .cache/clang-tools/ without sudo; override with \$CATA_CLANG_FORMAT).

Without files: formats the C++ files changed against the base, restricted to the
changed hunks. With files: formats only the lines of those files changed against
the base (or the staged hunks with --staged); newly added files are formatted whole.

options:
  --base <ref>      diff base for changed files/lines (default ${DEFAULT_BASE})
  --staged          use the staged diff (index vs HEAD) instead of the base
  --all             format every project C++ file, whole-file (mass reformat)
  --whole-file      with files, format whole files instead of changed hunks
  --dry-run         print the planned clang-format invocations without writing
  --help            show this message
`

export function parseOptions(argv: string[]): Options {
  const flags = parseFlags(argv, {
    strings: ["base"],
    booleans: ["staged", "all", "whole-file", "dry-run", "help"],
  })
  return {
    files: flags.args,
    base: flags.strings.base ?? DEFAULT_BASE,
    staged: flags.booleans.staged === true,
    all: flags.booleans.all === true,
    wholeFile: flags.booleans["whole-file"] === true,
    dryRun: flags.booleans["dry-run"] === true,
    help: flags.booleans.help === true,
  }
}

type Source = { kind: "staged" } | { kind: "base"; base: string }

interface Plan {
  file: string
  /** null = format the whole file; otherwise only these line ranges. */
  ranges: Array<[number, number]> | null
}

export async function plan(opts: Options): Promise<Plan[]> {
  const source: Source = opts.staged ? { kind: "staged" } : { kind: "base", base: opts.base }
  if (opts.all) {
    return (await allProjectCpp()).map((file) => ({ file, ranges: null }))
  }
  const files = opts.files.length > 0 ? opts.files : await changedCppFiles(opts.base)
  const plans: Plan[] = []
  for (const file of files) {
    if (opts.wholeFile || await isAddedFile(file, source)) {
      plans.push({ file, ranges: null })
      continue
    }
    let ranges: Array<[number, number]> = []
    try {
      ranges = await changedLineRanges(file, source)
    } catch { /* outside the repo or untracked: whole-file below */ }
    if (ranges.length > 0) {
      plans.push({ file, ranges })
    } else if (opts.files.length > 0 && !await isTracked(file)) {
      // Explicitly named and not tracked with changes: the whole file is the change.
      plans.push({ file, ranges: null })
    }
  }
  return plans
}

export async function run(opts: Options): Promise<number> {
  if (opts.help) {
    console.log(USAGE)
    return 0
  }
  const plans = await plan(opts)
  if (plans.length === 0) {
    console.log("fmt:cpp: nothing to format")
    return 0
  }
  if (opts.dryRun) {
    for (const entry of plans) {
      const scope = entry.ranges === null ? "whole file" : `lines ${JSON.stringify(entry.ranges)}`
      console.log(`fmt:cpp: would format ${entry.file} (${scope})`)
    }
    return 0
  }
  const tool = await pinnedClangFormat()
  for (const entry of plans) {
    const args = [
      "--style=file",
      "--fallback-style=none",
      "-i",
      ...(entry.ranges === null
        ? []
        : entry.ranges.flatMap(([start, end]) => [`--lines=${start}:${end}`])),
      entry.file,
    ]
    const result = await new Deno.Command(tool.bin, { args, stdout: "inherit", stderr: "inherit" })
      .output()
    if (!result.success) {
      console.error(`fmt:cpp: clang-format failed on ${entry.file}`)
      return result.code
    }
  }
  console.log(
    `fmt:cpp: formatted ${plans.length} file(s) with clang-format ${CLANG_FORMAT_VERSION}`,
  )
  return 0
}

if (import.meta.main) {
  try {
    Deno.exit(await run(parseOptions(Deno.args)))
  } catch (error) {
    console.error(`fmt:cpp: ${error instanceof Error ? error.message : String(error)}`)
    Deno.exit(1)
  }
}
