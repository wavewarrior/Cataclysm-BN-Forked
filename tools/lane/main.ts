/// lane: one issue, one worktree, one tmux session. Nothing else.
///
///   deno task lane <issue>            start a lane for an issue
///   deno task lane status             every lane: session alive? worktree dirty? how far did it get?
///   deno task lane attach <issue>     watch it (read-only; --write to type)
///   deno task lane stop <issue>       kill the session (the worktree and branch stay)
///
/// A lane is `git worktree add` plus a seeded offline configure plus a detached tmux session running
/// `$LANE_AGENT_CMD` (default `omp`) with the issue body as its prompt. No queue, no labels, no
/// stamps: monitoring is ssh, `attach`, and the lane's own `.check/events.jsonl`.
///
/// Environment:
///   LANE_ROOT       worktree parent directory (default ~/cbn/lanes)
///   LANE_AGENT_CMD  the agent to run (default `omp`)
///   LANE_SEED_DIR   build dir whose _deps seed an offline configure (required to start)
///   LANE_BASE       ref the lane branch starts from (default origin/feature/improvements)
import { join } from "@std/path"
import { type Env, run } from "../check/exec.ts"
import {
  agentCommand,
  BUILD_DIR,
  configureArguments,
  type Issue,
  laneBranch,
  laneEnv,
  lanePath,
  lanePrompt,
  parseIssue,
  parseLaneArgs,
  seedCopyCommand,
  tmuxSessionName,
  tmuxStartArgs,
} from "./spec.ts"

const USAGE = `usage: deno task lane <issue> | status | attach <issue> [--write] | stop <issue>

  <issue>          start lane-<issue>: worktree, seeded configure, detached tmux session lane-<issue>
  status           every lane: session alive? worktree dirty? which step did its check reach?
  attach <issue>   watch the session read-only (--write to type into it)
  stop <issue>     kill the session; the worktree and branch stay

Environment: LANE_ROOT (default ~/cbn/lanes), LANE_AGENT_CMD (default omp),
LANE_SEED_DIR (a configured build dir whose _deps seed the configure), LANE_BASE.`

const setting = (name: string, fallback: string): string => Deno.env.get(name) || fallback

async function requireGit(args: string[], cwd?: string): Promise<string> {
  const result = await run(["git", ...args], { cwd })
  if (result.code !== 0) {
    throw new Error(`git ${args.join(" ")}: ${result.stderr.trim().split("\n")[0]}`)
  }
  return result.stdout
}

async function sessionExists(session: string): Promise<boolean> {
  return (await run(["tmux", "has-session", "-t", `=${session}`])).code === 0
}

async function exists(path: string): Promise<boolean> {
  try {
    await Deno.stat(path)
    return true
  } catch {
    return false
  }
}

async function fetchIssue(issue: number): Promise<Issue> {
  const result = await run([
    "gh",
    "issue",
    "view",
    String(issue),
    "--repo",
    "wavewarrior/Cataclysm-BN-Forked",
    "--json",
    "number,title,body",
  ])
  if (result.code !== 0) throw new Error(`gh issue view ${issue}: ${result.stderr.trim()}`)
  return parseIssue(result.stdout)
}

async function seedEntries(seedDeps: string): Promise<string[]> {
  const entries: string[] = []
  try {
    for await (const entry of Deno.readDir(seedDeps)) {
      if (entry.isDirectory) entries.push(entry.name)
    }
  } catch {
    return []
  }
  return entries
}

async function start(issue: number): Promise<number> {
  const laneRoot = setting("LANE_ROOT", `${Deno.env.get("HOME")}/cbn/lanes`)
  const seedDir = Deno.env.get("LANE_SEED_DIR")
  if (seedDir === undefined) {
    console.error(
      "lane: LANE_SEED_DIR must name a configured build dir whose _deps seed the configure",
    )
    return 2
  }
  const session = tmuxSessionName(issue)
  if (await sessionExists(session)) {
    console.error(`lane: session ${session} exists; deno task lane attach ${issue} to watch it`)
    return 1
  }
  const worktree = lanePath(laneRoot, issue)
  if (await exists(worktree)) {
    console.error(`lane: ${worktree} exists; remove it before starting the lane again`)
    return 1
  }
  const detail = await fetchIssue(issue)
  const branch = laneBranch(issue, detail.title)
  const base = setting("LANE_BASE", "origin/feature/improvements")

  await Deno.mkdir(laneRoot, { recursive: true })
  console.log(`lane ${issue}: worktree ${worktree} on ${branch} from ${base}`)
  await requireGit(["worktree", "add", "-b", branch, worktree, base])

  // A fresh configure clones its dependencies off the network and stalls doing it, so copy them
  // from the seed build and point FetchContent at the lane's own copy: `cp -a`, never a link,
  // because a build writes inside those sources.
  const seedDeps = join(seedDir, "_deps")
  const entries = await seedEntries(seedDeps)
  const laneDeps = join(worktree, BUILD_DIR, "_deps")
  await Deno.mkdir(join(worktree, BUILD_DIR), { recursive: true })
  const copied = await run(seedCopyCommand(seedDeps, laneDeps))
  if (copied.code !== 0) throw new Error(`seeding failed: ${copied.stderr.trim()}`)

  const env: Env = laneEnv(laneRoot)
  console.log(`lane ${issue}: configuring (seeded, offline)`)
  const configured = await run(configureArguments(laneDeps, entries), { cwd: worktree, env })
  if (configured.code !== 0) throw new Error(`configure failed:\n${configured.stderr.slice(-2000)}`)

  const checkDir = join(worktree, ".check")
  await Deno.mkdir(checkDir, { recursive: true })
  const promptFile = join(checkDir, "prompt.md")
  const logFile = join(checkDir, "lane.log")
  await Deno.writeTextFile(promptFile, lanePrompt(detail))
  const command = agentCommand(setting("LANE_AGENT_CMD", "omp"), promptFile, logFile)
  const started = await run(tmuxStartArgs(session, worktree, command), { env })
  if (started.code !== 0) throw new Error(`tmux: ${started.stderr.trim()}`)
  console.log(`lane ${issue}: started ${session}, logging to ${logFile}`)
  console.log(`  watch it: deno task lane attach ${issue}`)
  return 0
}

/// The last event the lane's own check wrote, so `status` says how far it got.
async function lastEvent(path: string): Promise<string> {
  try {
    const lines = (await Deno.readTextFile(path)).trim().split("\n").filter((l) => l !== "")
    const last = JSON.parse(lines.at(-1) ?? "{}") as { step?: string; status?: string }
    return `${last.step ?? "?"}/${last.status ?? "?"}`
  } catch {
    return "no check yet"
  }
}

async function status(): Promise<number> {
  const laneRoot = setting("LANE_ROOT", `${Deno.env.get("HOME")}/cbn/lanes`)
  const listed = await run(["tmux", "list-sessions", "-F", "#{session_name}"])
  const running = new Set(listed.code === 0 ? listed.stdout.split("\n") : [])
  const lanes: string[] = []
  try {
    for await (const entry of Deno.readDir(laneRoot)) {
      if (entry.isDirectory && /^lane-\d+$/.test(entry.name)) lanes.push(entry.name)
    }
  } catch {
    console.log(`no lanes under ${laneRoot}`)
    return 0
  }
  for (const lane of lanes.sort((a, b) => Number(a.slice(5)) - Number(b.slice(5)))) {
    const issue = lane.slice("lane-".length)
    const worktree = join(laneRoot, lane)
    const branch = (await requireGit(["branch", "--show-current"], worktree).catch(() => "?"))
      .trim()
    const dirty = (await requireGit(["status", "--porcelain"], worktree).catch(() => "")).trim()
    const events = await lastEvent(join(worktree, ".check", "events.jsonl"))
    const state = running.has(`lane-${issue}`) ? "running" : "stopped"
    console.log(
      `#${issue.padEnd(6)} ${state}  ${branch.padEnd(46)} ${
        dirty === "" ? "clean" : `${dirty.split("\n").length} dirty`
      }  ${events}`,
    )
  }
  return 0
}

async function attach(issue: number, writable: boolean): Promise<number> {
  const session = tmuxSessionName(issue)
  if (!(await sessionExists(session))) {
    console.error(`lane: no session ${session}`)
    return 1
  }
  // Read-only unless asked for otherwise: watching a lane must not type into it by accident.
  const args = writable ? ["attach", "-t", session] : ["attach", "-r", "-t", session]
  const status = await new Deno.Command("tmux", {
    args,
    stdin: "inherit",
    stdout: "inherit",
    stderr: "inherit",
  }).spawn().status
  return status.code
}

async function stop(issue: number): Promise<number> {
  const session = tmuxSessionName(issue)
  if ((await run(["tmux", "kill-session", "-t", `=${session}`])).code !== 0) {
    console.error(`lane: no session ${session}`)
    return 1
  }
  console.log(`lane ${issue}: stopped ${session} (worktree and branch kept)`)
  return 0
}

async function main(): Promise<number> {
  const command = parseLaneArgs(Deno.args)
  switch (command.kind) {
    case "help":
      console.log(USAGE)
      return 0
    case "status":
      return await status()
    case "attach":
      return await attach(command.issue, command.writable)
    case "stop":
      return await stop(command.issue)
    case "start":
      return await start(command.issue)
  }
}

if (import.meta.main) {
  try {
    Deno.exit(await main())
  } catch (error) {
    console.error(`lane: ${error instanceof Error ? error.message : error}`)
    Deno.exit(2)
  }
}
