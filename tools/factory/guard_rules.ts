// Pure policy for a factory lane's tool calls. No runtime imports: the omp hook runs this under
// Bun and the tests run it under Deno. A blocked call returns a reason, an allowed call undefined.
import { config } from "./config.ts"
import { matchingGlobs, normalizePath } from "./glob.ts"

export type Role = "implementer" | "reviewer"

export type ToolCall = {
  toolName: string
  input: Record<string, unknown>
}

export type GuardContext = {
  /// Worktree root (omp's cwd).
  cwd: string
  /// Absolute per-worktree git dir; stamps and verdicts live in `<gitDir>/factory/`.
  gitDir: string
  role: Role
  ticket: string
  protectedPaths?: readonly string[]
}

const PATH_WRITE_TOOLS: Record<string, true> = {
  edit: true,
  write: true,
  ast_edit: true,
  notebook: true,
}

const FORBIDDEN_BASH: { re: RegExp; why: string }[] = [
  {
    re: /\bgit(?:\s+(?:-[Cc]\s+\S+|-\S+))*\s+push\b/,
    why: "git push is done by the factory driver only",
  },
  {
    re:
      /\bgit(?:\s+(?:-[Cc]\s+\S+|-\S+))*\s+(?:reset\s+--hard|clean\s+-\w*f|branch\s+-D|checkout\s+\.|restore\s+\.)/,
    why: "destructive git command",
  },
  { re: /--no-verify/, why: "hooks may not be bypassed" },
  { re: /-c\s+core\.hooksPath/, why: "hooks may not be redirected" },
  {
    re: /(?:^|[;&|(`"'\n]\s*|[\\/])gh(?:\.exe)?(?:['"]?\s|['"]?$)/,
    why: "the gh CLI is not available to a lane",
  },
  { re: /api\.github\.com/, why: "the GitHub API is not available to a lane" },
]

const WRITE_VERBS =
  /(?:>>?|\btee\b|\bsed\s+-\w*i|\bperl\s+-\w*i|\bmv\b|\bcp\b|\brm\b|\bdel\b|\brmdir\b|\bren\b|\btouch\b|\btruncate\b|\bgit\s+checkout\s+--|\bgit\s+restore\b|\bSet-Content\b|\bOut-File\b|\bAdd-Content\b|\bRemove-Item\b|\bNew-Item\b|\bCopy-Item\b|\bMove-Item\b|\bwriteFile|\bwriteTextFile|\bwrite_text\b|\.write\s*\(|\bopen\s*\([^)]*['"][wax]|\bunlink\b|\brename\b|\bcopyFile\b|\bshutil\b)/i

/// Extra verbs that change repository state; a reviewer may not use any of them.
const REVIEWER_STATE_VERBS =
  /\bgit\s+(?:add|commit|checkout|switch|reset|stash|merge|rebase|apply|am|cherry-pick|revert|rm|mv|tag|branch|worktree)\b/

const REVIEW_FILE = /^review-[0-9a-f]{40}\.md$/

/// Strip redirections that do not write files (`2>&1`, `>/dev/null`, `>nul`).
function withoutHarmlessRedirects(command: string): string {
  return command.replace(/\d*>&\d+/g, " ").replace(/\d*>>?\s*(?:\/dev\/null|nul)\b/gi, " ")
}

/// The literal prefix of a glob, up to its first wildcard: the text whose presence in a command
/// means the command mentions that protected area.
function staticPrefix(glob: string): string {
  const star = glob.indexOf("*")
  return normalizePath(star < 0 ? glob : glob.slice(0, star)).replace(/\/$/, "")
}

function mentionsProtected(command: string, globs: readonly string[]): string | undefined {
  const text = normalizePath(command).replaceAll("\\", "/")
  return globs.map(staticPrefix).find((prefix) => {
    const escaped = prefix.replace(/[.+^${}()|[\]\\?]/g, "\\$&")
    return prefix.length > 0 && new RegExp(`(?<![\\w.-])${escaped}`).test(text)
  })
}

function resolveInside(path: string, cwd: string): string {
  const abs = /^(?:[a-zA-Z]:)?[\\/]/.test(path) ? path : `${cwd}/${path}`
  const parts: string[] = []
  for (const part of normalizePath(abs).split("/")) {
    if (part === "..") parts.pop()
    else if (part !== "." && part !== "") parts.push(part)
  }
  const drive = /^[a-zA-Z]:$/.test(parts[0] ?? "")
  return (drive ? "" : "/") + parts.join("/")
}

function samePath(a: string, b: string): boolean {
  return resolveInside(a, "/").toLowerCase() === resolveInside(b, "/").toLowerCase()
}

/// Paths an edit-like tool call will touch. Covers the derived `path`/`paths` fields and the
/// raw hashline patch text (`[path#TAG]` headers, `MV dest`).
export function pathsOfToolCall(call: ToolCall): string[] {
  const out: string[] = []
  const { input } = call
  for (const key of ["path", "file_path", "filePath", "notebook_path", "dest", "destination"]) {
    if (typeof input[key] === "string") out.push(input[key])
  }
  for (const key of ["paths", "files"]) {
    const v = input[key]
    if (Array.isArray(v)) out.push(...v.filter((x): x is string => typeof x === "string"))
  }
  for (const key of ["input", "patch", "content_diff"]) {
    const text = input[key]
    if (typeof text !== "string") continue
    for (const m of text.matchAll(/^\[([^\]\r\n]+?)(?:#[0-9A-Fa-f]{4})?\]\s*$/gm)) out.push(m[1])
    for (const m of text.matchAll(/^\s*MV\s+("[^"]+"|'[^']+'|\S+)/gm)) {
      out.push(m[1].replace(/^["']|["']$/g, ""))
    }
  }
  return out
}

function blockReason(rule: string, ctx: GuardContext): string {
  return `factory-guard: ${rule}; request a human change via a comment on issue #${ctx.ticket}`
}

function checkPathWrite(call: ToolCall, ctx: GuardContext, globs: readonly string[]) {
  const root = resolveInside(ctx.cwd, "/")
  const factoryDir = resolveInside(`${ctx.gitDir}/factory`, "/")
  for (const raw of pathsOfToolCall(call)) {
    const abs = resolveInside(raw, ctx.cwd)
    const lower = abs.toLowerCase()
    if (lower === factoryDir.toLowerCase() || lower.startsWith(`${factoryDir.toLowerCase()}/`)) {
      const isReview = ctx.role === "reviewer" && call.toolName === "write" &&
        samePath(abs, `${factoryDir}/${abs.split("/").pop()}`) &&
        REVIEW_FILE.test(abs.split("/").pop() ?? "")
      if (isReview) continue
      return blockReason(`${raw} holds gate stamps and verdicts and is driver-owned`, ctx)
    }
    if (lower !== root.toLowerCase() && !lower.startsWith(`${root.toLowerCase()}/`)) {
      return blockReason(`${raw} is outside the lane worktree`, ctx)
    }
    const rel = abs.slice(root.length + 1)
    if (rel === ".git" || rel.startsWith(".git/")) {
      return blockReason(`${raw} is inside .git`, ctx)
    }
    const hit = matchingGlobs(rel, globs)
    if (hit.length > 0) return blockReason(`protected path ${rel} (${hit[0]})`, ctx)
    if (ctx.role === "reviewer") {
      return blockReason(`the reviewer may only write its own review file, not ${rel}`, ctx)
    }
  }
  return undefined
}

function checkShell(command: string, ctx: GuardContext, globs: readonly string[]) {
  for (const { re, why } of FORBIDDEN_BASH) {
    if (re.test(command)) return blockReason(`${why} (${re})`, ctx)
  }
  const stripped = withoutHarmlessRedirects(command)
  const writes = WRITE_VERBS.test(stripped)
  const protectedHit = mentionsProtected(command, globs)
  if (writes && protectedHit) {
    return blockReason(`a write verb touching protected path ${protectedHit}`, ctx)
  }
  if (ctx.role === "reviewer" && (writes || REVIEWER_STATE_VERBS.test(stripped))) {
    return blockReason("the reviewer is read-only; its only output is its review file", ctx)
  }
  return undefined
}

/// Decide a tool call for a factory lane. Returns the block reason, or undefined to allow.
export function evaluateToolCall(call: ToolCall, ctx: GuardContext): string | undefined {
  const globs = ctx.protectedPaths ?? config.protectedPaths
  if (Object.hasOwn(PATH_WRITE_TOOLS, call.toolName)) {
    if (ctx.role === "reviewer" && call.toolName !== "write") {
      return blockReason(`the reviewer may not use ${call.toolName}`, ctx)
    }
    return checkPathWrite(call, ctx, globs)
  }
  if (call.toolName === "bash") {
    return checkShell(String(call.input.command ?? ""), ctx, globs)
  }
  if (call.toolName === "eval") {
    if (ctx.role === "reviewer") return blockReason("the reviewer may not use eval", ctx)
    return checkShell(String(call.input.code ?? call.input.input ?? ""), ctx, globs)
  }
  return undefined
}
