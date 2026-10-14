/// `deno task fanout <header>`: report a header's fan-out over the include graph.
/// See `USAGE` in args.ts; the pure graph logic lives in graph.ts, the ninja-deps parsing and
/// matching in ninja.ts, so the tests can drive everything without a build dir.
import { walk } from "@std/fs"
import { join, relative, resolve } from "@std/path"
import { parseArgs, USAGE } from "./args.ts"
import { buildGraph, fanoutOf, normalizePath } from "./graph.ts"
import { type CompileDbEntry, matchNinjaTuSet, parseNinjaDeps, recordedRepoRoot } from "./ninja.ts"

const SOURCE_PREFIXES = ["src/", "tests/"]
const SOURCE_FILE = /\.(h|hpp|cpp)$/

if (import.meta.main) {
  await run()
}

async function run(): Promise<never | void> {
  const args = parseArgs(Deno.args)
  if (args.help) {
    console.log(USAGE)
    Deno.exit(0)
  }
  if (args.header === null) {
    console.error(USAGE)
    Deno.exit(1)
  }

  const repoRoot = resolve(args.root ?? await gitToplevel())
  const files = await collectSources(repoRoot)
  if (files.size === 0) {
    console.error(`fanout: no ${SOURCE_PREFIXES.join(" or ")} sources found under ${repoRoot}`)
    Deno.exit(1)
  }
  const header = normalizePath(args.header)
  if (!files.has(header)) {
    console.error(`fanout: ${header} is not a .h/.hpp file under ${SOURCE_PREFIXES.join(" or ")}`)
    Deno.exit(1)
  }

  const graph = buildGraph(files)
  const textFanout = fanoutOf(graph, header)

  let source = "textual include walk"
  let tus = textFanout.tus
  let buildDirUsed: string | null = null
  let buildDirNote: string | null = null
  if (!args.forceText) {
    const buildDir = args.buildDir ?? (await discoverBuildDir(repoRoot))
    if (buildDir !== null) {
      const counted = await ninjaTuSet(buildDir, repoRoot, header)
      if (counted === null) {
        buildDirNote =
          `build dir ${buildDir} unusable (no compile_commands.json, or ninja is missing and its deps log cannot be read); TU counts come from the textual walk`
      } else {
        tus = [...counted.tus].sort()
        source = `ninja deps (${buildDir})`
        buildDirUsed = buildDir
        buildDirNote = counted.note
      }
    }
  }

  const srcTus = tus.filter((f) => f.startsWith("src/"))
  const testTus = tus.filter((f) => f.startsWith("tests/"))

  if (args.porcelain) {
    console.log(`${header}\t${tus.length}\t${srcTus.length}\t${testTus.length}\t${source}`)
  } else if (args.json) {
    const report = {
      header,
      source,
      buildDir: buildDirUsed,
      buildDirNote,
      totals: { tus: tus.length, src: srcTus.length, tests: testTus.length },
      textualTotals: {
        tus: textFanout.tus.length,
        src: textFanout.srcTus.length,
        tests: textFanout.testTus.length,
      },
      directIncluders: textFanout.directIncluders,
      propagators: textFanout.propagators,
      unresolvedIncludes: graph.missing.length,
      tus,
    }
    console.log(JSON.stringify(report, null, 2))
  } else {
    console.log(
      `${header}: ${tus.length} TUs reach it (src ${srcTus.length}, tests ${testTus.length})`,
    )
    console.log(`  source: ${source}`)
    if (buildDirNote) console.log(`  note: ${buildDirNote}`)
    console.log(`  direct includers: ${textFanout.directIncluders.length}`)
    console.log(`  propagating headers: ${textFanout.propagators.length}`)
    if (graph.missing.length > 0) {
      const first = graph.missing[0]
      console.log(
        `  unresolved quoted includes: ${graph.missing.length} (e.g. ${first[0]} -> ${first[1]})`,
      )
    }
    if (buildDirUsed !== null) {
      console.log(`  textual walk says: ${textFanout.tus.length} TUs (kept for comparability)`)
    }
  }
}

async function gitToplevel(): Promise<string> {
  try {
    const out = await new Deno.Command("git", {
      args: ["rev-parse", "--show-toplevel"],
      stdout: "piped",
      stderr: "null",
    }).output()
    if (out.success) return new TextDecoder().decode(out.stdout).trim()
  } catch { /* not a git checkout; fall through */ }
  return Deno.cwd()
}

/// Every `.h`/`.hpp`/`.cpp` under `src/` and `tests/`, keyed repo-relative, text decoded.
async function collectSources(root: string): Promise<Map<string, string>> {
  const out = new Map<string, string>()
  const jobs: Promise<void>[] = []
  for (const prefix of SOURCE_PREFIXES) {
    const dir = join(root, prefix.slice(0, -1))
    try {
      await Deno.stat(dir)
    } catch {
      continue
    }
    for await (const entry of walk(dir, { includeDirs: false, match: [SOURCE_FILE] })) {
      jobs.push(
        Deno.readTextFile(entry.path).then(
          (text) => {
            out.set(normalizePath(relative(root, entry.path)), text)
          },
          () => {/* unreadable (binary or vanished): stay out of the graph */},
        ),
      )
    }
  }
  await Promise.all(jobs)
  return out
}

/// The first `out/build/*` preset that has a compile database, or null.
async function discoverBuildDir(root: string): Promise<string | null> {
  const base = join(root, "out", "build")
  const names: string[] = []
  try {
    for await (const entry of Deno.readDir(base)) if (entry.isDirectory) names.push(entry.name)
  } catch {
    return null
  }
  names.sort()
  for (const name of names) {
    try {
      await Deno.stat(join(base, name, "compile_commands.json"))
      return join(base, name)
    } catch { /* preset without a database */ }
  }
  return null
}

/// The TUs whose ninja-recorded deps include `header`, plus a disclosure note about the
/// build dir (foreign checkout, deps-log age), or null when the build dir cannot answer.
async function ninjaTuSet(
  buildDir: string,
  root: string,
  header: string,
): Promise<{ tus: Set<string>; note: string | null } | null> {
  let db: CompileDbEntry[]
  try {
    db = JSON.parse(await Deno.readTextFile(join(buildDir, "compile_commands.json")))
  } catch {
    return null
  }
  let deps
  try {
    const run = await new Deno.Command("ninja", {
      args: ["-C", buildDir, "-t", "deps"],
      stdout: "piped",
      stderr: "null",
    }).output()
    if (!run.success) return null
    deps = parseNinjaDeps(new TextDecoder().decode(run.stdout))
  } catch {
    return null
  }
  const tus = matchNinjaTuSet({ deps, db, buildDir, root, header, sourcePrefixes: SOURCE_PREFIXES })
  // An empty answer means the deps log does not describe this checkout; the textual walk can.
  if (tus.size === 0) return null
  const notes: string[] = []
  const recordedRoot = recordedRepoRoot(db)
  if (recordedRoot !== null && recordedRoot !== root) {
    notes.push(`database was recorded in ${recordedRoot}`)
  }
  try {
    const mtime = (await Deno.stat(join(buildDir, ".ninja_deps"))).mtime
    if (mtime) notes.push(`deps log dated ${mtime.toISOString().slice(0, 10)}`)
  } catch { /* no deps log stat: nothing to disclose */ }
  return { tus, note: notes.length > 0 ? `${notes.join("; ")}: counts may lag the tree` : null }
}
