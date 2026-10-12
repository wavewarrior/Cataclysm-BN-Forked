/// Format the C++ files a ticket changed with the same clang-format and astyle the WSL gate runs.
/// The gate runs `build-scripts/format-cpp.sh` in WSL on a Linux copy; a worktree's `.git` pointer is a
/// Windows path, so that script cannot run inside a worktree. This mirrors its selection rules and runs
/// the pinned binaries directly on the Windows worktree path.
import { config } from "./config.ts"
import { git } from "./util.ts"
import { toWslPath } from "./wsl.ts"

export type FormatTargets = { clang: string[]; astyle: string[] }

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

/// Single-quote one shell word.
function shellQuote(word: string): string {
  return `'${word.replaceAll("'", `'\\''`)}'`
}

/// Repo-relative C++ files changed against `base`, including uncommitted and untracked files.
async function changedFiles(cwd: string, base: string): Promise<string[]> {
  const tracked = await git(cwd, "diff", "--name-only", "--diff-filter=ACMR", base)
  const untracked = await git(cwd, "ls-files", "--others", "--exclude-standard")
  return [...new Set([...tracked.split("\n"), ...untracked.split("\n")])].filter(Boolean)
}

/// Format the changed C++ files of the worktree at `cwd`. Returns the WSL command's exit code.
export async function runFormat(opts: { cwd: string; base: string }): Promise<number> {
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

if (import.meta.main) {
  const args = Deno.args
  const at = args.indexOf("--base")
  const base = at >= 0 ? args[at + 1] : `origin/${config.integrationBranch}`
  Deno.exit(await runFormat({ cwd: Deno.cwd(), base }))
}
