/// Thin wrappers over the herdr CLI (0.9.x). Every command prints JSON `{id, result}`.
import { delay } from "@std/async"
import { run } from "./util.ts"

export type HerdrResult = { code: number; json: Record<string, unknown> | undefined; text: string }

async function herdr(args: string[]): Promise<HerdrResult> {
  const r = await run(["herdr", ...args])
  const text = (r.stdout + r.stderr).trim()
  let json: Record<string, unknown> | undefined
  try {
    json = JSON.parse(r.stdout)
  } catch {
    json = undefined
  }
  return { code: r.code, json, text }
}

async function ok(args: string[]): Promise<Record<string, unknown>> {
  const r = await herdr(args)
  if (r.code !== 0 || !r.json) {
    throw new Error(`herdr ${args.join(" ")} failed (${r.code}): ${r.text}`)
  }
  return (r.json.result ?? {}) as Record<string, unknown>
}

/// For commands that print nothing on success (`pane run`, `workspace focus`); only the exit code counts.
async function act(args: string[]): Promise<void> {
  const r = await herdr(args)
  if (r.code !== 0) throw new Error(`herdr ${args.join(" ")} failed (${r.code}): ${r.text}`)
}
export async function paneRun(pane: string, command: string): Promise<void> {
  await act(["pane", "run", pane, command])
}

function field<T>(obj: unknown, ...path: string[]): T {
  let cur: unknown = obj
  for (const key of path) {
    if (typeof cur !== "object" || cur === null) throw new Error(`herdr: no ${path.join(".")}`)
    cur = (cur as Record<string, unknown>)[key]
  }
  if (cur === undefined) throw new Error(`herdr: no ${path.join(".")}`)
  return cur as T
}

/// Start the headless server when none is running.
export async function ensureServer(): Promise<void> {
  if ((await herdr(["workspace", "list"])).code === 0) return
  new Deno.Command("herdr", { args: ["server"], stdin: "null", stdout: "null", stderr: "null" })
    .spawn()
    .unref()
  for (let i = 0; i < 20; i++) {
    await delay(500)
    if ((await herdr(["workspace", "list"])).code === 0) return
  }
  throw new Error("herdr server did not start")
}

export type Workspace = { workspaceId: string; rootPane: string }

export async function worktreeCreate(opts: {
  cwd: string
  branch: string
  base: string
  path: string
  label: string
}): Promise<Workspace> {
  const result = await ok([
    "worktree",
    "create",
    "--cwd",
    opts.cwd,
    "--branch",
    opts.branch,
    "--base",
    opts.base,
    "--path",
    opts.path,
    "--label",
    opts.label,
    "--no-focus",
    "--trust-repository",
  ])
  return {
    workspaceId: field<string>(result, "workspace", "workspace_id"),
    rootPane: field<string>(result, "root_pane", "pane_id"),
  }
}

/// Split `pane`; the new pane's shell gets `env`. Returns the new pane id.
export async function paneSplit(
  pane: string,
  env: Record<string, string>,
  direction: "right" | "down" = "right",
): Promise<string> {
  const args = ["pane", "split", pane, "--direction", direction, "--no-focus"]
  for (const [k, v] of Object.entries(env)) args.push("--env", `${k}=${v}`)
  return field<string>(await ok(args), "pane", "pane_id")
}

export async function paneClose(pane: string): Promise<void> {
  await herdr(["pane", "close", pane])
}

export async function workspaceClose(workspace: string): Promise<void> {
  await herdr(["workspace", "close", workspace])
}

/// The id of the open workspace labelled `label`, if any.
export async function workspaceFind(label: string): Promise<string | undefined> {
  const list = (await ok(["workspace", "list"])).workspaces as {
    label?: string
    workspace_id: string
  }[]
  return list.find((w) => w.label === label)?.workspace_id
}

export async function workspaceFocus(workspace: string): Promise<void> {
  await act(["workspace", "focus", workspace])
}

/// Create and focus a workspace whose first pane is rooted at `cwd`.
export async function workspaceCreate(opts: { cwd: string; label: string }): Promise<Workspace> {
  const result = await ok([
    "workspace",
    "create",
    "--cwd",
    opts.cwd,
    "--label",
    opts.label,
    "--focus",
  ])
  return {
    workspaceId: field<string>(result, "workspace", "workspace_id"),
    rootPane: field<string>(result, "root_pane", "pane_id"),
  }
}

export async function agentStart(opts: {
  name: string
  pane: string
  /// Arguments after `--`, passed to the agent (omp).
  args: string[]
}): Promise<void> {
  await ok([
    "agent",
    "start",
    opts.name,
    "--kind",
    "omp",
    "--pane",
    opts.pane,
    "--timeout",
    "120000",
    "--",
    ...opts.args,
  ])
}

export type AgentOutcome = {
  /// `idle`/`done` mean the turn finished; `blocked` means it needs a human; `timeout` ran out.
  state: "idle" | "done" | "blocked" | "timeout" | "error"
  detail: string
}

/// Submit `text` and wait for the turn to finish, up to `timeoutMs`.
export async function agentPrompt(
  name: string,
  text: string,
  timeoutMs: number,
): Promise<AgentOutcome> {
  const r = await herdr([
    "agent",
    "prompt",
    name,
    text,
    "--wait",
    "--timeout",
    String(Math.max(1000, timeoutMs)),
  ])
  if (r.code === 0) {
    const state = String(
      (r.json?.result as Record<string, unknown> | undefined)?.agent_status ??
        (r.json?.result as Record<string, unknown> | undefined)?.status ?? "idle",
    )
    return {
      state: state === "blocked" ? "blocked" : state === "done" ? "done" : "idle",
      detail: r.text,
    }
  }
  if (/timeout/i.test(r.text)) return { state: "timeout", detail: r.text }
  if (/blocked/i.test(r.text)) return { state: "blocked", detail: r.text }
  return { state: "error", detail: r.text }
}

export async function agentRead(name: string, lines = 60): Promise<string> {
  const r = await herdr([
    "agent",
    "read",
    name,
    "--source",
    "recent-unwrapped",
    "--lines",
    String(lines),
  ])
  return r.text
}

export type AgentInfo = { name?: string; pane_id?: string; status?: string }

export async function agentList(): Promise<AgentInfo[]> {
  const result = await ok(["agent", "list"])
  return (result.agents as AgentInfo[] | undefined) ?? []
}
