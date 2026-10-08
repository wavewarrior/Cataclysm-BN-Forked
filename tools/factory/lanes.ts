/// Concurrency lanes: at most `config.maxLanes` tickets run at once, each holding a lock file
/// `lane-<n>.lock` that records what it is doing (so `status` and `stop` can find it).
import { join } from "@std/path"
import { config } from "./config.ts"
import { run } from "./util.ts"

export type LaneState = {
  lane: number
  issue: number
  pid: number
  startedAt: string
  branch?: string
  worktree?: string
  workspaceId?: string
  panes: string[]
}

export type Lane = { n: number; path: string; state: LaneState }

const lockPath = (n: number) => join(config.lockDir, `lane-${n}.lock`)

/// True when a process with this pid exists. Windows only needs `tasklist`; elsewhere signal 0.
export async function pidAlive(pid: number): Promise<boolean> {
  if (Deno.build.os === "windows") {
    const r = await run(["tasklist", "/FI", `PID eq ${pid}`, "/NH", "/FO", "CSV"])
    return r.stdout.includes(`"${pid}"`)
  }
  try {
    Deno.kill(pid, "SIGCONT")
    return true
  } catch {
    return false
  }
}

async function readState(path: string): Promise<LaneState | undefined> {
  try {
    return JSON.parse(await Deno.readTextFile(path)) as LaneState
  } catch {
    return undefined
  }
}

/// Take the first free lane for `issue`, clearing locks whose owner process is gone.
export async function acquireLane(issue: number): Promise<Lane | undefined> {
  await Deno.mkdir(config.lockDir, { recursive: true })
  for (let n = 1; n <= config.maxLanes; n++) {
    const path = lockPath(n)
    const held = await readState(path)
    if (held && !(await pidAlive(held.pid))) await Deno.remove(path).catch(() => {})
    try {
      const file = await Deno.open(path, { createNew: true, write: true })
      const state: LaneState = {
        lane: n,
        issue,
        pid: Deno.pid,
        startedAt: new Date().toISOString(),
        panes: [],
      }
      await file.write(new TextEncoder().encode(JSON.stringify(state, null, 2)))
      file.close()
      return { n, path, state }
    } catch (error) {
      if (!(error instanceof Deno.errors.AlreadyExists)) throw error
    }
  }
  return undefined
}

export async function updateLane(lane: Lane, patch: Partial<LaneState>): Promise<void> {
  lane.state = { ...lane.state, ...patch }
  await Deno.writeTextFile(lane.path, JSON.stringify(lane.state, null, 2))
}

export async function releaseLane(lane: Lane): Promise<void> {
  await Deno.remove(lane.path).catch(() => {})
}

/// Every lane lock currently on disk.
export async function readLanes(): Promise<LaneState[]> {
  const out: LaneState[] = []
  for (let n = 1; n <= config.maxLanes; n++) {
    const state = await readState(lockPath(n))
    if (state) out.push(state)
  }
  return out
}

export function lanePathFor(n: number): string {
  return lockPath(n)
}
