// Confines an unattended factory lane. Inert unless FACTORY_LANE=1 (normal sessions unaffected).
// Policy lives in tools/factory/guard_rules.ts; a throwing handler blocks the tool (fail closed).
import { execFileSync } from "node:child_process"
import { evaluateToolCall, type Role } from "../../../tools/factory/guard_rules.ts"

type HookApi = {
  on: (
    event: "tool_call",
    handler: (
      event: { toolName: string; input: Record<string, unknown> },
      ctx: { cwd: string },
    ) => Promise<{ block: true; reason: string } | undefined>,
  ) => void
}

export default function factoryGuard(pi: HookApi): void {
  if (process.env.FACTORY_LANE !== "1") return
  const role: Role = process.env.FACTORY_ROLE === "reviewer" ? "reviewer" : "implementer"
  const ticket = process.env.FACTORY_TICKET ?? "?"
  // Resolved once at start: the lane's own git dir (per worktree), where stamps and verdicts live.
  let gitDir: string | undefined
  try {
    gitDir = execFileSync("git", ["rev-parse", "--absolute-git-dir"], {
      cwd: process.cwd(),
      encoding: "utf8",
    }).trim()
  } catch {
    gitDir = undefined
  }

  pi.on("tool_call", (event, ctx) => {
    try {
      if (gitDir === undefined) {
        return Promise.resolve({ block: true, reason: "factory-guard: cannot resolve git dir" })
      }
      const reason = evaluateToolCall(
        { toolName: event.toolName, input: event.input ?? {} },
        { cwd: ctx.cwd, gitDir, role, ticket },
      )
      return Promise.resolve(reason === undefined ? undefined : { block: true, reason })
    } catch (error) {
      return Promise.resolve({ block: true, reason: `factory-guard: internal error: ${error}` })
    }
  })
}
