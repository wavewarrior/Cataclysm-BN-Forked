/// The lane plan: names, paths and the commands a lane is started with. Pure, so the shape of a
/// lane is unit-tested without git, gh or tmux.
import { seedArguments } from "../check/seed.ts"

/// Where a lane lives: `$LANE_ROOT/lane-<n>` (default `~/cbn/lanes`).
export function lanePath(laneRoot: string, issue: number): string {
  return `${laneRoot}/lane-${issue}`
}

/// `lane/<n>-<slug>`, the branch a lane works on.
export function laneBranch(issue: number, title: string): string {
  return `lane/${issue}-${slugify(title)}`
}

/// Lowercase kebab-case, at most 40 characters, cut at a word boundary.
export function slugify(title: string): string {
  const words = title.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "").split("-")
  let slug = ""
  for (const word of words) {
    const next = slug === "" ? word : `${slug}-${word}`
    if (next.length > 40) break
    slug = next
  }
  return slug === "" ? "lane" : slug
}

/// The build directory the `linux-fast` configure preset creates inside a worktree.
export const BUILD_DIR = "out/build/linux-fast"

/// Copy the seeded FetchContent sources into a lane's build directory. `cp -a`, never a hard-link
/// copy: a build writes inside those sources, so a shared inode lets one lane change another's
/// compiler input.
export function seedCopyCommand(seedDeps: string, buildDir: string): string[] {
  return ["cp", "-a", seedDeps, buildDir]
}

/// Configure arguments for a lane: one seeded source dir per dependency plus the flag that
/// forbids the network.
export function configureArguments(seedDeps: string, entries: readonly string[]): string[] {
  return ["cmake", "--preset", "linux-fast", ...seedArguments(seedDeps, entries)]
}

/// ccache keys on the absolute path unless told otherwise, so with the base set to the lane root
/// every lane hits the cache warmed by the lanes before it.
export function laneEnv(laneRoot: string): Record<string, string> {
  return { CCACHE_BASEDIR: laneRoot }
}

/// The command the lane's tmux session runs: the agent with the issue body as its prompt, its whole
/// transcript appended to the lane log. `LANE_AGENT_CMD` takes the prompt as one argument, which is
/// how `omp` and the other CLI agents take an initial prompt.
export function agentCommand(agentCmd: string, promptFile: string, logFile: string): string {
  return `${agentCmd} "$(cat '${promptFile}')" >>'${logFile}' 2>&1`
}

/// `tmux new-session` arguments for a detached lane session that starts in the worktree.
export function tmuxStartArgs(session: string, worktree: string, command: string): string[] {
  return ["tmux", "new-session", "-d", "-s", session, "-c", worktree, "sh", "-c", command]
}

export function tmuxSessionName(issue: number): string {
  return `lane-${issue}`
}

export type Issue = { number: number; title: string; body: string }

/// The `gh issue view --json` field list a lane needs.
export const ISSUE_FIELDS = "number,title,body"

/// Pull the fields out of `gh issue view --json` output. Throws with the raw payload rather than
/// inventing a prompt when gh printed something else.
export function parseIssue(json: string): Issue {
  const parsed = JSON.parse(json) as Partial<Record<keyof Issue, unknown>>
  const { number, title, body } = parsed
  if (typeof number !== "number" || typeof title !== "string" || typeof body !== "string") {
    throw new Error(`unexpected gh issue view output: ${json.slice(0, 200)}`)
  }
  return { number, title, body }
}

/// The lane's prompt: the issue body with the title on top, so the agent knows what it is doing
/// even when the body is only a template stub.
export function lanePrompt(issue: Issue): string {
  return `# Issue #${issue.number}: ${issue.title}\n\n${issue.body}\n`
}

export type LaneCommand =
  | { kind: "start"; issue: number }
  | { kind: "status" }
  | { kind: "attach"; issue: number; writable: boolean }
  | { kind: "stop"; issue: number }
  | { kind: "help" }

const NUMBER = /^[1-9][0-9]*$/

/// The argv dispatch, pure so all four forms are tested without git, gh or tmux. A bare issue
/// number starts a lane; `status`, `attach` and `stop` name themselves.
export function parseLaneArgs(args: readonly string[]): LaneCommand {
  const [command, ...rest] = args
  if (command === undefined || command === "--help" || command === "-h") return { kind: "help" }
  switch (command) {
    case "status":
      expectNoArgs(rest, command)
      return { kind: "status" }
    case "attach": {
      const writable = rest.at(-1) === "--write"
      const operand = writable ? rest.slice(0, -1) : rest
      return { kind: "attach", issue: expectIssue(operand, command), writable }
    }
    case "stop":
      return { kind: "stop", issue: expectIssue(rest, command) }
    default:
      if (!NUMBER.test(command)) throw new Error(`unknown lane command: ${command}`)
      expectNoArgs(rest, command)
      return { kind: "start", issue: Number(command) }
  }
}

function expectIssue(args: readonly string[], command: string): number {
  const raw = args.length === 1 ? args[0] : undefined
  if (raw === undefined || !NUMBER.test(raw)) {
    throw new Error(`${command} needs one issue number, got: ${args.join(" ") || "(nothing)"}`)
  }
  return Number(raw)
}

function expectNoArgs(args: readonly string[], command: string): void {
  if (args.length > 0) throw new Error(`${command} takes no arguments, got: ${args.join(" ")}`)
}
