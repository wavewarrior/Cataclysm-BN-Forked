/**
 * Front end to daemon transport: one JSON object per line over a unix socket in the daemon's home.
 * The CLI and (later) the MCP server are thin clients of the same operations.
 */
import { delay } from "@std/async"
import { dirname, fromFileUrl, join } from "@std/path"
import type { DriverRequest } from "./client.ts"
import { REPO_ROOT, socketPath } from "./config.ts"

export type DaemonRequest =
  | { op: "ping" }
  | { op: "start"; trial: string }
  | { op: "step"; session: string; request: DriverRequest }
  | { op: "stop"; session: string }
  | { op: "shutdown" }

export type DaemonReply = { ok: true; result: object } | { ok: false; error: string }

/** Yields complete lines from a byte stream. */
export async function* readLines(stream: ReadableStream<Uint8Array>): AsyncGenerator<string> {
  const decoder = new TextDecoder()
  let buffer = ""
  for await (const chunk of stream) {
    buffer += decoder.decode(chunk, { stream: true })
    let newline: number
    while ((newline = buffer.indexOf("\n")) >= 0) {
      const line = buffer.slice(0, newline).trim()
      buffer = buffer.slice(newline + 1)
      if (line) yield line
    }
  }
}

/**
 * Sends one request to the daemon and returns its reply. Rejects when no daemon answers; with
 * `timeoutMs` it also rejects when the daemon does not reply in time.
 */
export async function call(
  home: string,
  request: DaemonRequest,
  timeoutMs?: number,
): Promise<DaemonReply> {
  const conn = await Deno.connect({ transport: "unix", path: socketPath(home) })
  const timer = timeoutMs === undefined ? undefined : setTimeout(() => conn.close(), timeoutMs)
  try {
    await conn.write(new TextEncoder().encode(JSON.stringify(request) + "\n"))
    for await (const line of readLines(conn.readable)) return JSON.parse(line)
    throw new Error("the daemon closed the connection without answering")
  } finally {
    clearTimeout(timer)
    try {
      conn.close()
    } catch { /* already closed by the readable's end or the timer */ }
  }
}

/** A daemon that does not answer a ping within this long counts as not running. */
const PING_TIMEOUT_MS = 2_000

/** True when a daemon owns this home and answers a ping. */
export async function daemonRunning(home: string): Promise<boolean> {
  try {
    return (await call(home, { op: "ping" }, PING_TIMEOUT_MS)).ok
  } catch {
    return false
  }
}

const MAIN = join(dirname(fromFileUrl(import.meta.url)), "main.ts")
const DAEMON_START_TIMEOUT_MS = 20_000

/** Starts the resident daemon in its own session when none is listening, and waits for it. */
export async function ensureDaemon(home: string): Promise<void> {
  if (await daemonRunning(home)) return
  await Deno.mkdir(home, { recursive: true })
  // Own session, so closing the shell that started it does not take the games down with it; its
  // stdout and stderr go to the daemon log so a crash leaves a trace.
  const daemon = new Deno.Command("/usr/bin/python3", {
    args: [
      "-c",
      "import os, sys\n" +
      "os.setsid()\n" +
      "fd = os.open(sys.argv[1], os.O_WRONLY | os.O_CREAT | os.O_APPEND)\n" +
      "os.dup2(fd, 1)\n" +
      "os.dup2(fd, 2)\n" +
      "os.execv(sys.argv[2], sys.argv[2:])",
      join(home, "daemon.log"),
      Deno.execPath(),
      "run",
      "--allow-run",
      "--allow-read",
      "--allow-net",
      "--allow-write",
      "--allow-env",
      "--config",
      join(REPO_ROOT, "deno.jsonc"),
      MAIN,
      "daemon",
    ],
    cwd: REPO_ROOT,
    stdin: "null",
    stdout: "null",
    stderr: "null",
  }).spawn()
  daemon.unref()
  const deadline = Date.now() + DAEMON_START_TIMEOUT_MS
  while (Date.now() < deadline) {
    if (await daemonRunning(home)) return
    await delay(100)
  }
  throw new Error(`the bnplay daemon did not start; see ${join(home, "daemon.log")}`)
}
