/// The factory driver CLI: `deno task factory <command>`.
///   install            point git at .githooks and forbid bare pushes (idempotent, per clone)
///   emit-config        regenerate tools/factory/config.json from config.ts
///   labels             create the factory labels on the fork
///   release <slug>     approve a spec: every open `factory:draft` + `spec:<slug>` issue -> ready
///   run                work `factory:ready` tickets in up to maxLanes worktree lanes
///   status             tickets by label, lane locks and herdr agents
///   stop <issue>       close a ticket's panes and mark it blocked
import { Command } from "@cliffy/command"
import { fromFileUrl } from "@std/path"
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
import { git, mainRepoRoot } from "./util.ts"

const repoRoot = () => git(".", "rev-parse", "--show-toplevel")

async function install(): Promise<void> {
  const root = await repoRoot()
  // Per worktree, not shared: a shared core.hooksPath would also switch the hooks on for every
  // other worktree of the clone, including branches whose hooks predate these.
  await git(root, "config", "--local", "extensions.worktreeConfig", "true")
  await git(root, "config", "--worktree", "core.hooksPath", ".githooks")
  console.log(`installed in ${root}: core.hooksPath=.githooks (this worktree only)`)
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
  const repo = await mainRepoRoot(".")
  const running = new Set<Promise<void>>()
  let started = 0
  // `gh issue list` reads a search index that lags a label edit by seconds, so a ticket we just
  // claimed can still be listed as ready. Remember what this process started.
  const startedIssues = new Set<number>()
  while (started < opts.max) {
    // A named ticket is read directly: the label list is a search index that can lag an edit.
    const listed = opts.issue === undefined
      ? await listIssues(["factory:ready"])
      : [await getIssue(opts.issue)].filter((i) =>
        i.state === "OPEN" && i.labels.includes("factory:ready")
      )
    const ready = listed.filter((i) => !startedIssues.has(i.number))
    const next = await pickNext(ready, isClosed)
    if (!next) {
      if (started === 0) {
        console.log("no factory:ready ticket to run (or its dependencies are still open)")
      }
      break
    }
    const lane = await acquireLane(next.number)
    if (!lane) {
      if (running.size === 0) {
        throw new Error("no lane free and none running: see `status`, `stop <issue>`")
      }
      await Promise.race(running)
      continue
    }
    await claim(next)
    startedIssues.add(next.number)
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
}
