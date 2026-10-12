/// Format the C++ files a ticket changed with the same clang-format and astyle the WSL gate runs.
/// The gate runs `build-scripts/format-cpp.sh` in WSL on a Linux copy; a worktree's `.git` pointer is a
/// Windows path, so that script cannot run inside a worktree. This mirrors its selection rules and runs
/// the pinned binaries directly on the Windows worktree path.
import { config } from "./config.ts"
import { git, run } from "./util.ts"
import { toWslPath } from "./wsl.ts"

export type FormatTargets = { clang: string[]; astyle: string[] }

/// The versions the gate pins: `setup.sh` (astyle exact, clang-format by major) and `lane.sh`.
export const PINNED_ASTYLE = "Artistic Style Version 3.6.16"
export const PINNED_CLANG_FORMAT_MAJOR = 22

const CLANG_SKIP = [
  "src/lua/",
  "src/sol/",
  "src/third-party/",
  "tests/catch/",
  "tools/clang-tidy-plugin/test/",
]
const CLANG_SCOPE = ["tests/", "tools/format/", "tools/clang-tidy-plugin/"]
const CPP_FILE = /\.(cpp|h|hpp)$/
const ASTYLE_SKIP = ["src/minicoro.h"]
const ASTYLE_SCOPE = /^src\/[^/]+\.(cpp|h)$/

/// Pure: the files `build-scripts/format-cpp.sh` gives clang-format and astyle, from repo-relative paths.
export function formatTargets(files: readonly string[]): FormatTargets {
  const clang = files.filter((f) => {
    if (CLANG_SKIP.some((p) => f.startsWith(p)) || !CPP_FILE.test(f)) return false
    const inSrcSubdir = f.startsWith("src/") && f.slice(4).includes("/")
    return inSrcSubdir || CLANG_SCOPE.some((p) => f.startsWith(p))
  })
  const astyle = files.filter((f) =>
    ASTYLE_SCOPE.test(f) && !f.startsWith("src/fmtlib_") && !ASTYLE_SKIP.includes(f)
  )
  return { clang, astyle }
}

/// Pure: the bash command, run inside WSL, that formats `targets` in `wslDir`.
export function formatScript(wslDir: string, targets: FormatTargets): string {
  const steps = [`cd ${shellQuote(wslDir)}`]
  if (targets.clang.length > 0) {
    steps.push(`clang-format -i ${targets.clang.map(shellQuote).join(" ")}`)
  }
  if (targets.astyle.length > 0) {
    steps.push(`astyle --options=.astylerc -n ${targets.astyle.map(shellQuote).join(" ")}`)
  }
  return steps.join(" && ")
}

/// Pure: an error message when the tool versions are not the pinned ones, otherwise undefined.
/// `astyle` and `clangFormat` are the captured `--version` output.
export function checkToolVersions(opts: { astyle: string; clangFormat: string }): string | undefined {
  const astyle = opts.astyle.trim()
  if (astyle !== PINNED_ASTYLE) {
    return `astyle is "${astyle}", need "${PINNED_ASTYLE}"`
  }
  const major = Number(opts.clangFormat.match(/version (\d+)\./)?.[1])
  if (major !== PINNED_CLANG_FORMAT_MAJOR) {
    return `clang-format is "${opts.clangFormat.trim()}", need major ${PINNED_CLANG_FORMAT_MAJOR}`
  }
  return undefined
}

/// Pure: the modified files that format would touch, i.e. the ones in its target set.
export function formatCommitFiles(modified: readonly string[], targets: FormatTargets): string[] {
  const formattable = new Set([...targets.clang, ...targets.astyle])
  return modified.filter((f) => formattable.has(f))
}

/// Single-quote one shell word.
function shellQuote(word: string): string {
  return `'${word.replaceAll("'", `'\\''`)}'`
}

/// Repo-relative C++ files changed since the merge-base with `base` (the range the gate checks,
/// `${base}...HEAD`), including uncommitted and untracked files.
async function changedFiles(cwd: string, base: string): Promise<string[]> {
  const mergeBase = await git(cwd, "merge-base", base, "HEAD")
  const tracked = await git(cwd, "diff", "--name-only", "--diff-filter=d", mergeBase)
  const untracked = await git(cwd, "ls-files", "--others", "--exclude-standard")
  return [...new Set([...tracked.split("\n"), ...untracked.split("\n")])].filter(Boolean)
}

/// Run `--version` for both tools in WSL and compare against the pins. Returns an error, or undefined.
export async function checkFormatTools(): Promise<string | undefined> {
  const wsl = (tool: string) => run(["wsl", "-d", "Ubuntu", "-u", "root", "-e", tool, "--version"])
  const astyle = await wsl("astyle")
  const clangFormat = await wsl("clang-format")
  return checkToolVersions({
    astyle: astyle.stdout || astyle.stderr,
    clangFormat: clangFormat.stdout || clangFormat.stderr,
  })
}

/// Format the changed C++ files of the worktree at `cwd`. Returns the exit code (2 on a pin mismatch).
export async function runFormat(opts: { cwd: string; base: string }): Promise<number> {
  const versionError = await checkFormatTools()
  if (versionError !== undefined) {
    console.error(`format: ${versionError}`)
    return 2
  }
  const root = await git(opts.cwd, "rev-parse", "--show-toplevel")
  const targets = formatTargets(await changedFiles(opts.cwd, opts.base))
  if (targets.clang.length === 0 && targets.astyle.length === 0) {
    console.log("format: no changed C++ files to format")
    return 0
  }
  const script = formatScript(toWslPath(root), targets)
  const child = new Deno.Command("wsl", {
    args: ["-d", "Ubuntu", "-u", "root", "-e", "bash", "-c", script],
    cwd: opts.cwd,
    stdout: "inherit",
    stderr: "inherit",
  })
  return (await child.output()).code
}

/// Format the ticket's changed C++ files and commit what the formatter changed as a `style:` commit,
/// so the gate never sees unformatted code. Returns an error message, or undefined on success.
export async function formatAndCommit(opts: {
  cwd: string
  base: string
  ticket: number
}): Promise<string | undefined> {
  try {
    const code = await runFormat(opts)
    if (code !== 0) return `format failed (exit ${code})`
    const targets = formatTargets(await changedFiles(opts.cwd, opts.base))
    const working = (await git(opts.cwd, "diff", "--name-only")).split("\n")
    const untracked = (await git(opts.cwd, "ls-files", "--others", "--exclude-standard")).split("\n")
    const files = formatCommitFiles([...working, ...untracked].filter(Boolean), targets)
    if (files.length === 0) return undefined
    await git(opts.cwd, "add", "--", ...files)
    const message = await Deno.makeTempFile({ suffix: ".txt" })
    await Deno.writeTextFile(message, "style: format changed C++ files\n")
    const commit = await run(["git", "commit", "-F", message], {
      cwd: opts.cwd,
      env: { FACTORY_LANE: "1", FACTORY_ROLE: "implementer", FACTORY_TICKET: String(opts.ticket) },
    })
    await Deno.remove(message).catch(() => undefined)
    return commit.code === 0 ? undefined : `style commit failed:\n${commit.stderr.trim()}`
  } catch (error) {
    return `format step failed: ${error instanceof Error ? error.message : String(error)}`
  }
}

if (import.meta.main) {
  const args = Deno.args
  if (args.includes("--check")) {
    const error = await checkFormatTools()
    if (error !== undefined) {
      console.error(`format: ${error}`)
      Deno.exit(2)
    }
    console.log("format: pinned tools match")
    Deno.exit(0)
  }
  const at = args.indexOf("--base")
  const base = at >= 0 ? args[at + 1] : `origin/${config.integrationBranch}`
  Deno.exit(await runFormat({ cwd: Deno.cwd(), base }))
}
