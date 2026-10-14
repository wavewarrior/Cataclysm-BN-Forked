/// worktree: the Mac worktree lifecycle (#198, decided in #150).
///
///   deno task worktree new <type>/<issue-id>/<slug> [--base <ref>] [--path <dir>]
///   deno task worktree list
///   deno task worktree remove <slug>
///
/// `new` runs `herdr worktree create` from the primary checkout (creation from a linked worktree
/// hits herdr's `linked_worktree_source` refusal), copies the gitignored `data/shaders/*.msl` that
/// configure FATAL_ERRORs without, configures the preset named by the `PRESET` constant, and stops
/// there printing the build command: the build queue (#146) is what enqueues the first build.
/// `list` shows every worktree with branch, build-dir size, last-commit age and merged state.
/// `remove` announces the checkout plus its build dirs, refuses a dirty tree or commits that are
/// in no remote and not in the base, and never deletes the branch. Safety rules follow the
/// cbn-repo-disk-cleanup skill: sizes before deletion, nothing being built inside.
///
/// Environment: WORKTREE_BASE (default feature/improvements), HERDR_CMD (default herdr),
/// CMAKE_CMD (default cmake).
import { join, resolve } from "@std/path"
import { run } from "../check/exec.ts"
import {
  copyPlan,
  EXPECTED_MSL,
  formatGib,
  formatTable,
  gitDirsEqual,
  herdrCreateArgs,
  listRow,
  missingMsl,
  parseNewArgs,
  parseWorktreeList,
  PRESET,
  removePlan,
  type Row,
  type WorktreeEntry,
} from "./spec.ts"

const USAGE = `usage: deno task worktree new <type>/<issue-id>/<slug> [--base <ref>] [--path <dir>]
       deno task worktree list
       deno task worktree remove <slug>

  new      herdr worktree create from the primary checkout, copy data/shaders/*.msl, configure
  list     every worktree: branch, build-dir size, last-commit age, merged into the base?
  remove   delete a worktree checkout and its out/ build dirs; refuses dirty or unpushed;
           the branch is never deleted

Environment: WORKTREE_ROOT, WORKTREE_BASE, HERDR_CMD, CMAKE_CMD.`

const setting = (name: string, fallback: string): string => Deno.env.get(name) || fallback

async function gitOrThrow(args: string[], cwd?: string): Promise<string> {
  const result = await run(["git", ...args], { cwd })
  if (result.code !== 0) {
    throw new Error(`git ${args.join(" ")}: ${result.stderr.trim().split("\n")[0]}`)
  }
  return result.stdout
}

/// The primary checkout's top level, refusing a linked worktree: herdr creates from the source
/// checkout only, and the .msl copy reads from here.
async function primaryRoot(): Promise<string> {
  const gitDir = (await gitOrThrow(["rev-parse", "--path-format=absolute", "--git-dir"])).trim()
  const common = (await gitOrThrow([
    "rev-parse",
    "--path-format=absolute",
    "--git-common-dir",
  ])).trim()
  if (!gitDirsEqual(gitDir, common)) {
    throw new Error(
      `worktree: this is a linked worktree (${gitDir}); run it from the primary checkout ${common}`,
    )
  }
  return (await gitOrThrow(["rev-parse", "--show-toplevel"])).trim()
}

async function exists(path: string): Promise<boolean> {
  try {
    await Deno.stat(path)
    return true
  } catch {
    return false
  }
}

async function worktreeEntries(root: string): Promise<WorktreeEntry[]> {
  return parseWorktreeList(await gitOrThrow(["worktree", "list", "--porcelain"], root))
}

/// Summed KiB of a worktree's `out/build`, or null when it has none.
async function buildSizeKiB(worktree: string): Promise<number | null> {
  const out = await run(["du", "-sk", `${worktree}/out/build`], { cwd: worktree })
  if (out.code !== 0) return null
  const kib = Number(out.stdout.split("\t")[0])
  return Number.isFinite(kib) ? kib : null
}

async function headAgeSeconds(head: string, root: string): Promise<number | null> {
  const out = await run(["git", "show", "-s", "--format=%ct", head], { cwd: root })
  if (out.code !== 0) return null
  const commitMs = Number(out.stdout.trim()) * 1000
  return Number.isFinite(commitMs) ? Math.max(0, Date.now() - commitMs) / 1000 : null
}

async function mergedInto(head: string, base: string, root: string): Promise<boolean | null> {
  const out = await run(["git", "merge-base", "--is-ancestor", head, base], { cwd: root })
  if (out.code === 0) return true
  return out.code === 1 ? false : null
}

/// Free bytes on the volume holding `path`, via `df -kP` (1024-block units).
async function freeBytes(path: string): Promise<number | null> {
  const out = await run(["df", "-kP", path])
  if (out.code !== 0) return null
  const avail = Number(out.stdout.split("\n")[1]?.split(/\s+/)[3])
  return Number.isFinite(avail) ? avail * 1024 : null
}

async function cmdNew(args: string[]): Promise<number> {
  const root = await primaryRoot()
  const defaultRoot = setting(
    "WORKTREE_ROOT",
    resolve(join(root, "..", "Cataclysm-BN-Forked-worktrees")),
  )
  const cmd = parseNewArgs(args, defaultRoot)
  if (await exists(cmd.path)) {
    console.error(`worktree new: ${cmd.path} exists; remove it first`)
    return 1
  }
  const shaderDir = join(root, "data", "shaders")
  const names: string[] = []
  try {
    for await (const e of Deno.readDir(shaderDir)) names.push(e.name)
  } catch { /* absent: the refusal below names every expected file */ }
  const missing = missingMsl(names)
  if (missing.length > 0) {
    console.error(
      `worktree new: the primary checkout ${root} lacks ${missing.length} of the ${EXPECTED_MSL.length} required data/shaders/*.msl: ${
        missing.join(", ")
      }. Configure FATAL_ERRORs without them; obtain them from a machine that generates shaders.`,
    )
    return 2
  }
  console.log(
    `worktree new: herdr worktree create ${cmd.spec.type}/${cmd.spec.issueId}/${cmd.spec.slug} from ${cmd.base} at ${cmd.path}`,
  )
  const created = await run([setting("HERDR_CMD", "herdr"), ...herdrCreateArgs(cmd)], {
    cwd: root,
  })
  if (created.code !== 0) {
    console.error(`worktree new: herdr failed (exit ${created.code}):\n${created.stderr.trim()}`)
    return 1
  }
  if (!await exists(cmd.path)) {
    console.error(`worktree new: herdr exited 0 but ${cmd.path} is not there`)
    return 1
  }
  const pairs = copyPlan(root, cmd.path, names)
  await Deno.mkdir(join(cmd.path, "data", "shaders"), { recursive: true })
  for (const pair of pairs) await Deno.copyFile(pair.from, pair.to)
  console.log(`worktree new: copied ${pairs.length} data/shaders/*.msl`)
  console.log(`worktree new: configuring preset ${PRESET} (first configure fetches dependencies)`)
  const configured = await run([setting("CMAKE_CMD", "cmake"), "--preset", PRESET], {
    cwd: cmd.path,
  })
  if (configured.code !== 0) {
    console.error(
      `worktree new: cmake --preset ${PRESET} failed (exit ${configured.code}); the worktree stays for inspection:\n${
        configured.stderr.slice(-2000)
      }`,
    )
    return 1
  }
  console.log(`worktree new: cmake --preset ${PRESET} succeeded`)
  console.log(
    `worktree new: the build queue (#146) does not exist yet; start the first build by hand:`,
  )
  console.log(`  cd ${cmd.path} && deno task build`)
  return 0
}

async function cmdList(): Promise<number> {
  const root = await primaryRoot()
  const base = setting("WORKTREE_BASE", "feature/improvements")
  const rows: Row[] = []
  for (const entry of await worktreeEntries(root)) {
    const primary = entry.path === root
    const present = !entry.prunable && await exists(entry.path)
    const sizeKiB = present ? await buildSizeKiB(entry.path) : null
    const ageSeconds = present ? await headAgeSeconds(entry.head, root) : null
    const merged = present && !primary ? await mergedInto(entry.head, base, root) : null
    rows.push(listRow({
      path: entry.path,
      branch: entry.branch,
      present,
      primary,
      sizeKiB,
      ageSeconds,
      mergedIntoBase: merged,
    }))
  }
  console.log(formatTable(rows))
  return 0
}

/// True when a ninja process is running with its cwd inside the worktree: never delete a tree
/// something is writing into (cbn-repo-disk-cleanup step 3, scoped to this worktree).
async function buildingInside(worktree: string): Promise<boolean> {
  const pids = (await run(["pgrep", "-x", "ninja"])).stdout.split("\n").filter((p) =>
    /^\d+$/.test(p)
  )
  for (const pid of pids) {
    const out = await run(["lsof", "-p", pid, "-a", "-d", "cwd", "-Fn"])
    if (out.stdout.split("\n").some((l) => l.startsWith("n") && l.slice(1).startsWith(worktree))) {
      return true
    }
  }
  return false
}

/// Commits reachable from `head` that are in no remote-tracking ref and not in `base`; null when
/// there is nothing to compare against.
async function unpushedCount(head: string, base: string, root: string): Promise<number | null> {
  const remotes = (await run(["git", "for-each-ref", "--format=%(refname)", "refs/remotes"], {
    cwd: root,
  })).stdout.split("\n").filter((l) => l !== "")
  const not: string[] = []
  if (
    (await run(["git", "rev-parse", "--verify", "--quiet", `${base}^{commit}`], { cwd: root }))
      .code === 0
  ) not.push(base)
  not.push(...remotes)
  if (not.length === 0) return null
  const out = await run(["git", "rev-list", "--count", head, "--not", ...not], { cwd: root })
  return out.code === 0 ? Number(out.stdout.trim()) : null
}

async function cmdRemove(args: string[]): Promise<number> {
  if (args.length !== 1 || args[0].startsWith("-")) {
    throw new Error("worktree remove takes exactly one <slug>")
  }
  const slug = args[0]
  const root = await primaryRoot()
  const base = setting("WORKTREE_BASE", "feature/improvements")
  const entries = await worktreeEntries(root)
  const matches = entries.filter((e) =>
    e.path !== root && (e.path === slug || e.path.endsWith(`/${slug}`))
  )
  if (matches.length === 0) {
    console.error(`worktree remove: no worktree named ${slug} (deno task worktree list)`)
    return 1
  }
  if (matches.length > 1) {
    console.error(
      `worktree remove: ${slug} is ambiguous among ${matches.map((m) => m.path).join(", ")}`,
    )
    return 1
  }
  const entry = matches[0]
  if (entry.prunable || !await exists(entry.path)) {
    console.error(
      `worktree remove: ${entry.path} is already gone; git worktree prune cleans the record`,
    )
    return 1
  }
  if (await buildingInside(entry.path)) {
    console.error(`worktree remove: a ninja is running inside ${entry.path}; stop the build first`)
    return 1
  }
  const status = (await gitOrThrow(["status", "--porcelain"], entry.path)).trim()
  if (status !== "") {
    console.error(
      `worktree remove: ${entry.path} is dirty, refusing. First lines:\n${
        status.split("\n").slice(0, 5).join("\n")
      }`,
    )
    return 1
  }
  const unpushed = await unpushedCount(entry.head, base, root)
  if (unpushed === null) {
    console.error(
      `worktree remove: cannot tell whether ${
        entry.head.slice(0, 8)
      } is pushed (no base ref, no remotes); refusing to guess`,
    )
    return 1
  }
  if (unpushed > 0) {
    console.error(
      `worktree remove: ${
        entry.branch || entry.head.slice(0, 8)
      } has ${unpushed} commit(s) in no remote and not in ${base}; push or merge first`,
    )
    return 1
  }
  const buildDirs: string[] = []
  try {
    for await (const e of Deno.readDir(join(entry.path, "out", "build"))) {
      if (e.isDirectory) buildDirs.push(join(entry.path, "out", "build", e.name))
    }
  } catch { /* no build dirs at all is fine */ }
  for (const line of removePlan(entry.path, buildDirs)) console.log(`  will delete: ${line}`)
  const before = await freeBytes(entry.path)
  const removed = await run(["git", "worktree", "remove", entry.path], { cwd: root })
  if (removed.code !== 0) {
    console.error(`worktree remove: git refused (exit ${removed.code}): ${removed.stderr.trim()}`)
    return 1
  }
  const after = await freeBytes(resolve(entry.path, ".."))
  if (before !== null && after !== null) {
    console.log(
      `free space: ${formatGib(before)} -> ${formatGib(after)} (+${formatGib(after - before)})`,
    )
  }
  if (entry.branch !== "") console.log(`worktree remove: branch ${entry.branch} kept`)
  return 0
}

async function main(): Promise<number> {
  const [verb, ...rest] = Deno.args
  switch (verb) {
    case undefined:
    case "help":
    case "--help":
      console.log(USAGE)
      return 0
    case "new":
      return await cmdNew(rest)
    case "list":
      return await cmdList()
    case "remove":
      return await cmdRemove(rest)
    default:
      console.log(USAGE)
      console.error(`worktree: unknown command "${verb}"`)
      return 2
  }
}

if (import.meta.main) {
  try {
    Deno.exit(await main())
  } catch (error) {
    console.error(`worktree: ${error instanceof Error ? error.message : error}`)
    Deno.exit(2)
  }
}
