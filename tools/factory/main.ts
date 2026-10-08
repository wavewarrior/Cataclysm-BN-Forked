/// The factory driver CLI: `deno task factory <command>`.
///   install            point git at .githooks and forbid bare pushes (idempotent, per clone)
///   emit-config        regenerate tools/factory/config.json from config.ts
///   labels             create the factory labels on the fork
///   release <slug>     approve a spec: every open `factory:draft` + `spec:<slug>` issue -> ready
///   run                work `factory:ready` tickets in up to maxLanes worktree lanes
///   status             tickets by label, lane locks and herdr agents
///   stop <issue>       close a ticket's panes and mark it blocked
import { Command } from "@cliffy/command"
import { fromFileUrl, join } from "@std/path"
import { config } from "./config.ts"
import { pickNext, runTicket } from "./driver.ts"
import {
  actionsEnabled,
  comment,
  editLabels,
  ensureLabel,
  getIssue,
  isClosed,
  type Issue,
  LABELS,
  listIssues,
} from "./gh.ts"
import { agentList, ensureServer, workspaceClose } from "./herdr.ts"
import { acquireLane, lanePathFor, readLanes, releaseLane } from "./lanes.ts"
import { git, run } from "./util.ts"

const repoRoot = () => git(".", "rev-parse", "--show-toplevel")

async function install(): Promise<void> {
  const root = await repoRoot()
  // Local config lives in the common .git/config, so every worktree shares it.
  await git(root, "config", "--local", "core.hooksPath", ".githooks")
  await git(root, "config", "--local", "push.default", "nothing")
  console.log("installed: core.hooksPath=.githooks, push.default=nothing")
}

async function emitConfig(): Promise<void> {
  const path = fromFileUrl(new URL("./config.json", import.meta.url))
  await Deno.writeTextFile(path, JSON.stringify(config, null, 2) + "\n")
  console.log(`wrote ${path}`)
}

async function release(slug: string): Promise<void> {
  const drafts = await listIssues(["factory:draft", `spec:${slug}`])
  for (const issue of drafts) {
    await editLabels(issue.number, { add: ["factory:ready"], remove: ["factory:draft"] })
    console.log(`#${issue.number} ready: ${issue.title}`)
  }
  if (drafts.length === 0) console.log(`no open factory:draft issues labelled spec:${slug}`)
}

async function claim(issue: Issue): Promise<void> {
  await editLabels(issue.number, { add: ["factory:in-progress"], remove: ["factory:ready"] })
}

async function runLoop(opts: { max: number; issue?: number }): Promise<void> {
  if (config.requireCi && !(await actionsEnabled())) {
    throw new Error("GitHub Actions is disabled on the fork; refusing to open PRs without CI")
  }
  await ensureServer()
  const repo = await repoRoot()
  const running = new Set<Promise<void>>()
  let started = 0
  while (started < opts.max) {
    const ready = opts.issue !== undefined
      ? (await listIssues(["factory:ready"])).filter((i) => i.number === opts.issue)
      : await listIssues(["factory:ready"])
    const next = await pickNext(ready, isClosed)
    if (!next) break
    const lane = await acquireLane(next.number)
    if (!lane) {
      if (running.size === 0) {
        throw new Error("no lane free and none running: see `status`, `stop <issue>`")
      }
      await Promise.race(running)
      continue
    }
    await claim(next)
    started++
    console.log(`lane ${lane.n}: #${next.number} ${next.title}`)
    const job: Promise<void> = runTicket(next, lane, { repo })
      .then((r) => console.log(`lane ${lane.n}: #${next.number} ${r.outcome}`))
      .finally(() => releaseLane(lane))
      .finally(() => running.delete(job))
    running.add(job)
  }
  await Promise.all(running)
}

async function status(): Promise<void> {
  for (
    const label of ["factory:ready", "factory:in-progress", "factory:review", "factory:blocked"]
  ) {
    const issues = await listIssues([label])
    console.log(`${label} (${issues.length})`)
    for (const i of issues) console.log(`  #${i.number} ${i.title}`)
  }
  console.log("lanes:")
  for (const lane of await readLanes()) {
    console.log(
      `  lane ${lane.lane}: #${lane.issue} pid ${lane.pid} ${lane.branch ?? ""} ${
        lane.worktree ?? ""
      }`,
    )
  }
  await ensureServer()
  console.log("herdr agents:")
  for (const a of await agentList()) console.log(`  ${JSON.stringify(a)}`)
}

async function stop(issueNumber: number): Promise<void> {
  for (const lane of await readLanes()) {
    if (lane.issue !== issueNumber) continue
    if (lane.workspaceId) await workspaceClose(lane.workspaceId)
    await Deno.remove(lanePathFor(lane.lane)).catch(() => {})
    console.log(`closed lane ${lane.lane} (worktree ${lane.worktree} left in place)`)
  }
  const issue = await getIssue(issueNumber)
  await editLabels(issue.number, {
    add: ["factory:blocked"],
    remove: ["factory:ready", "factory:in-progress"],
  })
  await comment(issue.number, "factory: stopped by a human.")
}

if (import.meta.main) {
  await new Command()
    .name("factory")
    .description("Agentic software factory driver.")
    .command("install", "Wire git hooks for this clone.").action(install)
    .command("emit-config", "Regenerate tools/factory/config.json.").action(emitConfig)
    .command("labels", "Create the factory labels on the fork.").action(async () => {
      for (const l of LABELS) await ensureLabel(l)
      console.log(`${LABELS.length} labels ensured`)
    })
    .command("release <spec-slug:string>", "Approve a spec: draft tickets become ready.")
    .action((_, slug) => release(slug))
    .command("run", "Work ready tickets.")
    .option("--max <n:number>", "stop after starting this many tickets", { default: 1000 })
    .option("--issue <n:number>", "only this issue")
    .action(({ max, issue }) => runLoop({ max, issue }))
    .command("status", "Show tickets, lanes and agents.").action(status)
    .command("stop <issue:number>", "Stop a ticket and mark it blocked.")
    .action((_, issue) => stop(issue))
    .parse(Deno.args)
  // Keep `run` honest about non-zero worktree state.
  void run
  void join
}
