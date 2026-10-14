/// Argument parsing for `deno task fanout`. Pure: no IO, no environment reads.

export type FanoutArgs = {
  /// The header to report on, repo-relative (`src/map.h`), or null with `--help`.
  header: string | null
  /// `--json`: machine-readable output on stdout.
  json: boolean
  /// `--porcelain`: one tab-separated line (header, totals, source) for hook scripts.
  porcelain: boolean
  /// `--text`: never consult a build dir; the textual include walk decides the TU counts.
  forceText: boolean
  /// `--build-dir <dir>`: use this build dir for the ninja-deps path instead of discovering one.
  buildDir: string | null
  /// `--root <dir>`: treat this directory as the repo root (used by the tool's own tests).
  root: string | null
  help: boolean
}

export const USAGE = `usage: deno task fanout <header> [--json] [--porcelain] [--text]
                     [--build-dir DIR] [--root DIR]

Reports a header's fan-out: the src/ and tests/ .cpp translation units that reach
<header> by transitive #include, split src/tests, plus its direct includers and the
headers that propagate it.

  <header>            repo-relative header path, e.g. src/map.h
  --json              emit one JSON object instead of the human report
  --porcelain         one tab-separated line: header, total TUs, src, tests, source
  --text              skip the build-dir path; textual include walk only
  --build-dir DIR     use DIR's compile_commands.json + ninja deps for the TU count
  --root DIR          repo root to scan (default: the git toplevel)
  --help              this text

TU counts prefer compile_commands.json + \`ninja -t deps\` when a configured build dir
exists (any out/build/* with a compile_commands.json); otherwise they come from the
textual include walk. The output names the source used. Direct includers and
propagating headers always come from the textual walk: ninja deps cannot tell a
direct include from a propagated one.

Exit codes: 0 success; 1 bad arguments or unknown header.`

const BOOL_FLAGS = new Set(["--json", "--porcelain", "--text", "--help"])
const VALUE_FLAGS = new Set(["--build-dir", "--root"])

export function parseArgs(args: readonly string[]): FanoutArgs {
  const parsed: FanoutArgs = {
    header: null,
    json: false,
    porcelain: false,
    forceText: false,
    buildDir: null,
    root: null,
    help: false,
  }
  for (let i = 0; i < args.length; i++) {
    const arg = args[i]
    if (BOOL_FLAGS.has(arg)) {
      if (arg === "--json") parsed.json = true
      else if (arg === "--porcelain") parsed.porcelain = true
      else if (arg === "--text") parsed.forceText = true
      else parsed.help = true
    } else if (VALUE_FLAGS.has(arg)) {
      const value = args[++i]
      if (value === undefined) throw new Error(`${arg} needs a value`)
      if (arg === "--build-dir") parsed.buildDir = value
      else parsed.root = value
    } else if (arg.startsWith("-")) {
      throw new Error(`unknown flag ${arg}`)
    } else if (parsed.header !== null) {
      throw new Error(`expected exactly one header, got a second positional ${arg}`)
    } else {
      parsed.header = arg.replaceAll("\\", "/").replace(/^\.\//, "")
    }
  }
  return parsed
}
