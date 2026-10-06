/** A minimal MCP host for the tests: spawns `bnplay mcp` and speaks JSON-RPC to it over stdio. */
import { assertEquals } from "@std/assert"
import { readLines } from "./ipc.ts"
import { bnplayDenoArgs, type Sandbox } from "./testkit.ts"

export type RpcReply = {
  result?: Record<string, unknown>
  error?: { code: number; message: string }
}

/** What a tools/call returns, in the shape a host sees it. */
export type ToolOutcome<T extends object = Record<string, unknown>> = {
  isError: boolean
  /** The text content: the result as JSON, or the refusal message. */
  text: string
  structured?: T
}

export type McpClient = {
  /** Sends a request and waits for the reply with its id. */
  rpc(method: string, params?: object): Promise<RpcReply>
  notify(method: string, params?: object): Promise<void>
  /** Calls a tool; the outcome is the tool result, never a protocol error. */
  call<T extends object = Record<string, unknown>>(
    name: string,
    args?: unknown,
  ): Promise<ToolOutcome<T>>
  /** Every line the server wrote to stdout so far, parsed; each must be a JSON-RPC message. */
  received(): unknown[]
  /** Closes stdin as a host does on exit; the server must then exit cleanly. */
  close(): Promise<{ code: number; stderr: string }>
}

export function startMcp(sandbox: Sandbox): McpClient {
  const child = new Deno.Command(Deno.execPath(), {
    args: bnplayDenoArgs(["mcp"]),
    cwd: sandbox.dir,
    env: sandbox.env,
    stdin: "piped",
    stdout: "piped",
    stderr: "piped",
  }).spawn()
  const stdin = child.stdin.getWriter()
  const encoder = new TextEncoder()
  const messages: unknown[] = []
  const waiting = new Map<number, (reply: RpcReply) => void>()
  const stderr = new Response(child.stderr).text()
  const reading = (async () => {
    for await (const line of readLines(child.stdout)) {
      const message = JSON.parse(line)
      messages.push(message)
      if (typeof message.id === "number") waiting.get(message.id)?.(message)
    }
  })()
  let nextId = 1
  const send = (message: object) =>
    stdin.write(encoder.encode(JSON.stringify({ jsonrpc: "2.0", ...message }) + "\n"))
  const client: McpClient = {
    rpc(method, params) {
      const id = nextId++
      const reply = new Promise<RpcReply>((resolve) => waiting.set(id, resolve))
      send({ id, method, params })
      return reply
    },
    notify: (method, params) => send({ method, params }),
    async call(name, args) {
      const reply = await client.rpc("tools/call", { name, arguments: args })
      assertEquals(reply.error, undefined, `${name}: protocol error`)
      const { isError, content, structuredContent } = reply.result as {
        isError: boolean
        content: { type: string; text: string }[]
        structuredContent?: Record<string, unknown>
      }
      assertEquals(content[0].type, "text")
      return { isError, text: content[0].text, structured: structuredContent as never }
    },
    received: () => messages,
    async close() {
      await stdin.close()
      const status = await child.status
      await reading
      return { code: status.code, stderr: await stderr }
    },
  }
  return client
}

/** Performs the handshake a host does before using tools. */
export async function initialize(client: McpClient, protocolVersion = "2025-06-18") {
  const reply = await client.rpc("initialize", {
    protocolVersion,
    capabilities: {},
    clientInfo: { name: "bnplay-test", version: "0" },
  })
  await client.notify("notifications/initialized")
  return reply
}
