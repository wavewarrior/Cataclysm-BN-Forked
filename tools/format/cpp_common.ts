// Shared helpers for the C++ formatting and tidy tasks: pinned tool bootstrap,
// changed-file enumeration and changed-line range extraction.
//
// Style is auto-fixed and never a gate failure; the correctness tidy checks run
// on changed lines only, so both consumers need the same "what changed" math.

import { walk } from "@std/fs"
import { join } from "@std/path"

export const CLANG_FORMAT_VERSION = "22.1.8"
export const CLANG_TIDY_VERSION = "22.1.8"
export const DEFAULT_BASE = "origin/feature/improvements"

/** Directories whose C++ is vendored or formatter-sensitive: never reformatted. */
const EXCLUDED_PREFIXES = [
  "src/lua/",
  "src/sol/",
  "src/third-party/",
  "tests/catch/",
  "tools/clang-tidy-plugin/test/",
  "src/fmtlib_",
  "src/minicoro.h",
]

const CPP_SUFFIXES = [".cpp", ".h", ".hpp"]

export function isFormattableCpp(file: string): boolean {
  if (!CPP_SUFFIXES.some((suffix) => file.endsWith(suffix))) return false
  return !EXCLUDED_PREFIXES.some((prefix) => file.startsWith(prefix))
}

async function run(args: string[], cwd?: string): Promise<string> {
  const result = await new Deno.Command("git", {
    args,
    cwd,
    stdout: "piped",
    stderr: "piped",
  }).output()
  if (!result.success) {
    const err = new TextDecoder().decode(result.stderr)
    throw new Error(`git ${args.join(" ")} failed: ${err}`)
  }
  return new TextDecoder().decode(result.stdout)
}

/** Files whose tracked content differs from `base`, filtered to project C++. */
export async function changedCppFiles(base: string): Promise<string[]> {
  const out = await run(["diff", "--name-only", "--diff-filter=ACMRTUXB", base])
  return [...new Set(out.split("\n").filter((f) => f.length > 0 && isFormattableCpp(f)))].sort()
}

/** Staged (index vs HEAD) C++ files, filtered to project C++. */
export async function stagedCppFiles(): Promise<string[]> {
  const out = await run(["diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"])
  return out.split("\0").filter((f) => f.length > 0 && isFormattableCpp(f)).sort()
}

/** Parse `@@ ... +start,count @@` headers of a `-U0` unified diff into [start,end] line pairs. */
export function parseHunkRanges(diffText: string): Array<[number, number]> {
  const ranges: Array<[number, number]> = []
  for (const line of diffText.split("\n")) {
    const match = /^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@/.exec(line)
    if (!match) continue
    const start = Number(match[1])
    const count = match[2] === undefined ? 1 : Number(match[2])
    if (count === 0) continue // pure deletion: no surviving lines to format
    ranges.push([start, start + count - 1])
  }
  return mergeRanges(ranges)
}

/** Merge overlapping/adjacent [start,end] ranges, sorted. */
export function mergeRanges(ranges: Array<[number, number]>): Array<[number, number]> {
  const sorted = [...ranges].sort((a, b) => a[0] - b[0] || a[1] - b[1])
  const merged: Array<[number, number]> = []
  for (const [start, end] of sorted) {
    const last = merged[merged.length - 1]
    if (last && start <= last[1] + 1) last[1] = Math.max(last[1], end)
    else merged.push([start, end])
  }
  return merged
}

/** Changed-line ranges for one file, from the staged index or against a base ref. */
export async function changedLineRanges(
  file: string,
  source: { kind: "staged" } | { kind: "base"; base: string },
): Promise<Array<[number, number]>> {
  const args = source.kind === "staged"
    ? ["diff", "--cached", "-U0", "--", file]
    : ["diff", "-U0", source.base, "--", file]
  return parseHunkRanges(await run(args))
}

/** True when the path is tracked by the repository containing the cwd. */
export async function isTracked(file: string): Promise<boolean> {
  const result = await new Deno.Command("git", {
    args: ["ls-files", "--error-unmatch", "--", file],
    stdout: "null",
    stderr: "null",
  }).output()
  return result.success
}

/** True when the file is added (status A) against HEAD (staged) or the base. */
export async function isAddedFile(
  file: string,
  source: { kind: "staged" } | { kind: "base"; base: string },
): Promise<boolean> {
  const args = source.kind === "staged"
    ? ["diff", "--cached", "--name-only", "--diff-filter=A", "--", file]
    : ["diff", "--name-only", "--diff-filter=A", source.base, "--", file]
  return (await run(args)).trim().length > 0
}

export interface Flags {
  /** Positional arguments, in order. */
  args: string[]
  /** Values of `--flag value` / `--flag=value` string options, keyed without dashes. */
  strings: Record<string, string>
  /** Presence of `--flag` boolean options. */
  booleans: Record<string, boolean>
}

/** Minimal CLI parser for the two C++ tool tasks: string options, boolean flags. */
export function parseFlags(argv: string[], spec: { strings: string[]; booleans: string[] }): Flags {
  const flags: Flags = { args: [], strings: {}, booleans: {} }
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i]
    if (!arg.startsWith("--")) {
      flags.args.push(arg)
      continue
    }
    const eq = arg.indexOf("=")
    const name = (eq >= 0 ? arg.slice(2, eq) : arg.slice(2)).replace(/^-+/, "")
    if (spec.strings.includes(name)) {
      const value = eq >= 0 ? arg.slice(eq + 1) : argv[++i]
      if (value === undefined) throw new Error(`--${name} requires a value`)
      flags.strings[name] = value
    } else if (spec.booleans.includes(name)) {
      flags.booleans[name] = true
    } else {
      throw new Error(`unknown option ${arg}`)
    }
  }
  return flags
}

interface ManagedTool {
  /** Directory holding the tool installation (also usable as a build-dir hint). */
  dir: string
  bin: string
}

/**
 * Path to the pinned clang-format, bootstrapped on demand without sudo.
 * Prefers `$CATA_CLANG_FORMAT`; falls back to a managed venv under `.cache/clang-tools/`
 * created with `uv` (preferred) or `python3 -m venv` + pip.
 */
export async function pinnedClangFormat(): Promise<ManagedTool> {
  const fromEnv = Deno.env.get("CATA_CLANG_FORMAT")
  if (fromEnv) return { dir: "", bin: fromEnv }
  return await managedBinary("clang-format", CLANG_FORMAT_VERSION)
}

/** Path to clang-tidy for the plugin: `$CATA_CLANG_TIDY`, PATH, or the pinned managed copy. */
export async function pinnedClangTidy(): Promise<ManagedTool> {
  const fromEnv = Deno.env.get("CATA_CLANG_TIDY")
  if (fromEnv) return { dir: "", bin: fromEnv }
  return await managedBinary("clang-tidy", CLANG_TIDY_VERSION)
}

async function managedBinary(packageName: string, version: string): Promise<ManagedTool> {
  const dir = join(Deno.cwd(), ".cache", "clang-tools", `${packageName}-${version}`)
  const bin = join(dir, "bin", packageName)
  try {
    await Deno.stat(bin)
    return { dir, bin }
  } catch { /* bootstrap below */ }
  await Deno.mkdir(dir, { recursive: true })
  const python = join(dir, "bin", "python")
  const uv = await which("uv")
  if (uv) {
    await expectOk(["venv", dir], "uv")
    await expectOk(
      ["pip", "install", "--python", python, `${packageName}==${version}`],
      "uv",
    )
  } else {
    await expectOk(["-m", "venv", dir], "python3")
    await expectOk(
      ["-m", "pip", "install", "--upgrade", "pip", `${packageName}==${version}`],
      python,
    )
  }
  try {
    await Deno.stat(bin)
  } catch {
    throw new Error(
      `bootstrapped ${packageName}==${version} but ${bin} is missing; ` +
        `install it manually and set CATA_CLANG_${packageName.toUpperCase().replace(/-/g, "_")}`,
    )
  }
  return { dir, bin }
}

async function which(name: string): Promise<string | null> {
  try {
    const result = await new Deno.Command(name, {
      args: ["--version"],
      stdout: "null",
      stderr: "null",
    })
      .output()
    return result.success ? name : null
  } catch {
    return null
  }
}

async function expectOk(args: string[], command: string): Promise<void> {
  const result = await new Deno.Command(command, { args, stdout: "inherit", stderr: "inherit" })
    .output()
  if (!result.success) {
    throw new Error(
      `${command} ${args.join(" ")} failed (${result.code}); cannot bootstrap pinned LLVM tools`,
    )
  }
}

/** Major version reported by `<bin> --version`, or null when unparseable. */
export async function toolMajorVersion(bin: string): Promise<number | null> {
  const result = await new Deno.Command(bin, {
    args: ["--version"],
    stdout: "piped",
    stderr: "piped",
  })
    .output()
  const text = new TextDecoder().decode(result.stdout) + new TextDecoder().decode(result.stderr)
  const match = /version (\d+)\./.exec(text)
  return match ? Number(match[1]) : null
}

/** All project C++ files under src/ and tests/ (used by --all style sweeps). */
export async function allProjectCpp(): Promise<string[]> {
  const files: string[] = []
  for (const root of ["src", "tests"]) {
    for await (
      const entry of walk(root, {
        maxDepth: 10,
        includeDirs: false,
        skip: [/(^|\/)(lua|sol|third-party|catch)(\/|$)/],
      })
    ) {
      const rel = entry.path.replace(/^\.\//, "")
      if (isFormattableCpp(rel)) files.push(rel)
    }
  }
  return files.sort()
}
