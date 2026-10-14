// deno task tidy:changed / deno task fix:tidy
//
// tidy:changed runs the three correctness clang-tidy checks (cata-determinism,
// cata-no-long, cata-test-filename) over the C++ files changed against the base,
// restricted to the changed lines via --line-filter, and exits non-zero on any
// finding. The plugin is loaded from the build directory; if it is missing this
// fails loudly with the exact build command, never passes silently.
//
// fix:tidy (--fix) runs the style autofixers (modernize-use-trailing-return-type,
// modernize-use-auto) over the same changed lines. Style is guidance, not a gate:
// fixes are applied and the command exits 0 even when it changed something.

import { join, resolve } from "@std/path"
import {
  changedCppFiles,
  changedLineRanges,
  CLANG_TIDY_VERSION,
  DEFAULT_BASE,
  parseFlags,
  pinnedClangTidy,
  toolMajorVersion,
} from "./cpp_common.ts"
import { chooseUnit, compiledUnits, serviceability } from "./tidy_units.ts"

const CORRECTNESS_CHECKS = "cata-determinism,cata-no-long,cata-test-filename"
const STYLE_FIX_CHECKS = "modernize-use-trailing-return-type,modernize-use-auto"
const DEFAULT_BUILD_DIR = "out/build/linux-fast"
const CONCURRENCY = 4

/// Every `.cpp` under `src/` or `tests/` that includes `header` by its file name.
async function includingUnits(header: string): Promise<string[]> {
  const name = header.split("/").pop() ?? header
  const found = await new Deno.Command("git", {
    args: ["grep", "-l", "-F", `#include "${name}"`, "--", "src/*.cpp", "tests/*.cpp"],
    stdout: "piped",
    stderr: "null",
  }).output()
  return new TextDecoder().decode(found.stdout).split("\n").filter((line) => line !== "")
}

const USAGE = `usage: deno task tidy:changed [options]
       deno task fix:tidy [options]

Runs clang-tidy ${CLANG_TIDY_VERSION} over the C++ files changed against the base,
restricted to the changed lines (git diff -U0).

  tidy:changed  checks ${CORRECTNESS_CHECKS}; exit non-zero on any finding.
                Requires the Cata clang-tidy plugin in the build directory.
  fix:tidy      applies ${STYLE_FIX_CHECKS} with --fix; never fails on findings.

options:
  --base <ref>        diff base (default ${DEFAULT_BASE})
  --build-dir <dir>   CMake build dir with compile_commands.json and, for
                      tidy:changed, the plugin (default ${DEFAULT_BUILD_DIR})
  --help              show this message
`

interface Options {
  base: string
  buildDir: string
  fix: boolean
  help: boolean
}

function parseOptions(argv: string[]): Options {
  const flags = parseFlags(argv, {
    strings: ["base", "build-dir"],
    booleans: ["fix", "help"],
  })
  return {
    base: flags.strings.base ?? DEFAULT_BASE,
    buildDir: flags.strings["build-dir"] ?? DEFAULT_BUILD_DIR,
    fix: flags.booleans.fix === true,
    help: flags.booleans.help === true,
  }
}

async function exists(path: string): Promise<boolean> {
  try {
    await Deno.stat(path)
    return true
  } catch {
    return false
  }
}

async function findPlugin(buildDir: string): Promise<string | null> {
  const dir = join(buildDir, "tools", "clang-tidy-plugin")
  for (const name of ["libCataAnalyzerPlugin.so", "libCataAnalyzerPlugin.dylib"]) {
    const path = join(dir, name)
    if (await exists(path)) return path
  }
  return null
}

interface TidyResult {
  file: string
  code: number
  output: string
}

interface TidyOutcome extends TidyResult {
  /** clang-tidy refused to analyse the file (missing compile command / crash). */
  skipped: boolean
}

async function tidyOne(
  tool: string,
  argsBase: string[],
  file: string,
  ranges: Array<[number, number]>,
): Promise<TidyOutcome> {
  const lineFilter = JSON.stringify([{ name: file, lines: ranges }])
  const result = await new Deno.Command(tool, {
    args: [...argsBase, `--line-filter=${lineFilter}`, file],
    stdout: "piped",
    stderr: "piped",
  }).output()
  const output = new TextDecoder().decode(result.stdout) + new TextDecoder().decode(result.stderr)
  // clang-tidy exits 0 when it refuses to analyse a file; never let that pass silently.
  const skipped = /Compile command not found|Error while processing/.test(output)
  return { file, code: result.code, output, skipped }
}

export async function run(argv: string[]): Promise<number> {
  const opts = parseOptions(argv)
  if (opts.help) {
    console.log(USAGE)
    return 0
  }
  const buildDir = resolve(opts.buildDir)
  const compileDb = join(buildDir, "compile_commands.json")
  const configureHint = `Configure it with:\n` +
    `  cmake --preset linux-fast\n` +
    `(or pass --build-dir pointing at a configured build directory)`
  if (!await exists(compileDb)) {
    console.error(`tidy: no compile database at ${compileDb}.`)
    console.error(configureHint)
    return 2
  }
  const compiled = compiledUnits(await Deno.readTextFile(compileDb), Deno.realPathSync(Deno.cwd()))

  let pluginArg: string[] = []
  if (!opts.fix) {
    const plugin = await findPlugin(buildDir)
    if (plugin === null) {
      console.error(`tidy: the Cata clang-tidy plugin is missing from ${buildDir}.`)
      console.error(
        `Build it with:\n  cmake -S tools/clang-tidy-plugin -B ${
          join(buildDir, "tools", "clang-tidy-plugin")
        } -G Ninja ` +
          `&& cmake --build ${join(buildDir, "tools", "clang-tidy-plugin")}`,
      )
      console.error(
        `(standalone, so it does not collide with the clang targets the shadercross build brings; ` +
          `needs LLVM/Clang ${CLANG_TIDY_VERSION} dev packages)`,
      )
      return 2
    }
    pluginArg = [`--load=${plugin}`]
  }

  const tool = await pinnedClangTidy()
  if (!opts.fix) {
    const major = await toolMajorVersion(tool.bin)
    const wantedMajor = Number(CLANG_TIDY_VERSION.split(".")[0])
    if (major === null || major !== wantedMajor) {
      console.error(
        `tidy: ${tool.bin} is clang-tidy ${major ?? "unknown"}; the plugin needs major ` +
          `version ${wantedMajor} (clang-tidy ${CLANG_TIDY_VERSION}). Install it or set CATA_CLANG_TIDY.`,
      )
      return 2
    }
  }

  const allChanged = await changedCppFiles(opts.base)
  const targets = allChanged.filter((f) => f.startsWith("src/") || f.startsWith("tests/"))
  if (targets.length === 0) {
    console.log(
      `${opts.fix ? "fix:tidy" : "tidy:changed"}: no changed C++ files against ${opts.base}`,
    )
    return 0
  }

  // clang-tidy analyses translation units; a changed header rides along on its sibling TU, or
  // when it has none (a header-only file) on a TU that includes it, with --line-filter confining
  // findings to the changed header lines.
  const jobs: Array<{ tu: string; name: string; ranges: Array<[number, number]> }> = []
  const unserviceable: string[] = []
  const skipped: string[] = []
  for (const file of targets) {
    const ranges = await changedLineRanges(file, { kind: "base", base: opts.base })
    if (ranges.length === 0) continue
    const isHeader = file.endsWith(".h") || file.endsWith(".hpp")
    const sibling = isHeader ? file.replace(/\.hpp?$/, ".cpp") : file
    const candidates = isHeader ? [sibling, ...await includingUnits(file)] : [file]
    const present: string[] = []
    for (const candidate of candidates) if (await exists(candidate)) present.push(candidate)
    const tu = chooseUnit(present, compiled)
    const verdict = serviceability(file, isHeader, tu, compiled)
    if (verdict === "run" && tu !== null) jobs.push({ tu, name: file, ranges })
    else if (verdict === "skip") skipped.push(file)
    else unserviceable.push(file)
  }
  for (const file of skipped) console.log(`tidy: skipped ${file}: not compiled in this build`)
  if (unserviceable.length > 0) {
    console.error(`tidy: no translation unit to analyse the changes in:`)
    for (const file of unserviceable) console.error(`  ${file}`)
    return 2
  }
  if (jobs.length === 0) {
    console.log(
      `${
        opts.fix ? "fix:tidy" : "tidy:changed"
      }: no changed lines in C++ files against ${opts.base}`,
    )
    return 0
  }

  const checks = opts.fix ? STYLE_FIX_CHECKS : CORRECTNESS_CHECKS
  const argsBase = [
    `-p`,
    buildDir,
    "--quiet",
    ...pluginArg,
    `--checks=-*,${checks}`,
    ...(opts.fix ? ["--fix"] : ["--warnings-as-errors=*"]),
  ]

  let failed = 0
  let findings = 0
  for (let i = 0; i < jobs.length; i += CONCURRENCY) {
    const batch = jobs.slice(i, i + CONCURRENCY)
    const results = await Promise.all(
      batch.map((job) => tidyOne(tool.bin, argsBase, job.tu, job.ranges)),
    )
    for (let j = 0; j < results.length; j++) {
      const result = results[j]
      const name = batch[j].name
      const diagnostics = result.output
        .split("\n")
        .filter((line) => /^\S+:\d+:\d+: (warning|error):/.test(line))
      if (diagnostics.length > 0) {
        findings += diagnostics.length
        console.log(diagnostics.join("\n"))
      }
      if (result.skipped || result.code !== 0) {
        failed++
        console.error(
          `tidy: ${name} ${
            result.skipped
              ? "was not analysed (missing from the compile database?)"
              : `exited ${result.code}`
          }`,
        )
        if (!result.skipped) console.error(result.output)
      }
    }
  }

  if (failed > 0) {
    console.error(`tidy: ${failed} file(s) could not be analysed with ${buildDir}`)
    return 2
  }
  if (opts.fix) {
    console.log(`fix:tidy: applied ${findings} style fix(es) across ${jobs.length} file(s)`)
    return 0
  }
  if (findings > 0) {
    console.error(`tidy:changed: ${findings} finding(s)`)
    return 1
  }
  console.log(`tidy:changed: clean (${jobs.length} file(s) checked against ${opts.base})`)
  return 0
}

if (import.meta.main) {
  try {
    Deno.exit(await run(Deno.args))
  } catch (error) {
    console.error(`tidy: ${error instanceof Error ? error.message : String(error)}`)
    Deno.exit(1)
  }
}
