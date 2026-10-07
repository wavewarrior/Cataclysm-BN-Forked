/**
 * The MCP front end: a Model Context Protocol server on stdio (newline-delimited JSON-RPC 2.0)
 * exposing every bnplay operation as a typed tool, generated from the shared table in
 * operations.ts. It is a thin client of the same resident daemon the CLI uses, so sessions
 * started through one front end can be stepped, reported and stopped through the other.
 *
 * Hand-rolled rather than the SDK: the surface is `initialize`, `ping`, `tools/list` and
 * `tools/call`, which is far less code than the dependency tree an npm SDK would add to a
 * repo script that is otherwise `@std` only. A host mounts it with the command
 * `deno run <permissions> tools/bnplay/main.ts mcp` (see main.ts).
 *
 * stdout carries protocol messages only; diagnostics go to stderr.
 */
import { readLines } from "./ipc.ts"
import {
  buildRequest,
  OPERATION_NAMES,
  type OperationName,
  OPERATIONS,
  type Param,
  runRequest,
} from "./operations.ts"

const SUPPORTED_VERSIONS = ["2025-06-18", "2025-03-26", "2024-11-05"]

type JsonRpcId = string | number | null
type JsonRpcRequest = { jsonrpc: "2.0"; id?: JsonRpcId; method: string; params?: unknown }

const PARAM_TYPES: Record<Param["kind"], string> = {
  string: "string",
  path: "string",
  boolean: "boolean",
  object: "object",
  paths: "array",
  number: "integer",
}

/** The tool descriptors of `tools/list`, one per operation. */
export function listTools(): object[] {
  return OPERATION_NAMES.map((name) => {
    const op = OPERATIONS[name]
    return {
      name,
      description: op.description,
      inputSchema: {
        type: "object",
        properties: Object.fromEntries(op.params.map((p) => [p.name, {
          type: PARAM_TYPES[p.kind],
          ...(p.kind === "paths" ? { items: { type: "string" } } : {}),
          description: p.description,
        }])),
        required: op.params.filter((p) => p.required).map((p) => p.name),
        additionalProperties: false,
      },
      outputSchema: { type: "object" },
    }
  })
}

type ToolResult = {
  content: { type: "text"; text: string }[]
  structuredContent?: object
  isError: boolean
}

/**
 * Runs one tool call. A refusal (bad arguments, an unknown session, a full cap) is a tool error
 * carrying the message the CLI would print; a failed verdict or an unhealthy doctor is a result.
 */
export async function callTool(name: OperationName, args: unknown): Promise<ToolResult> {
  try {
    const result = await runRequest(buildRequest(name, args ?? {}))
    return {
      content: [{ type: "text", text: JSON.stringify(result) }],
      structuredContent: result,
      isError: false,
    }
  } catch (e) {
    return { content: [{ type: "text", text: (e as Error).message }], isError: true }
  }
}

function isRequest(message: unknown): message is JsonRpcRequest {
  return typeof message === "object" && message !== null && "method" in message &&
    typeof message.method === "string"
}

/** Answers one JSON-RPC request; returns undefined for notifications, which get no reply. */
async function respond(message: JsonRpcRequest): Promise<object | undefined> {
  if (message.id === undefined) return undefined
  const reply = (result: object) => ({ jsonrpc: "2.0", id: message.id, result })
  const fail = (code: number, text: string) => ({
    jsonrpc: "2.0",
    id: message.id,
    error: { code, message: text },
  })
  const params = (message.params ?? {}) as Record<string, unknown>
  switch (message.method) {
    case "initialize": {
      const asked = params.protocolVersion
      return reply({
        protocolVersion: SUPPORTED_VERSIONS.includes(asked as string)
          ? asked
          : SUPPORTED_VERSIONS[0],
        capabilities: { tools: {} },
        serverInfo: { name: "bnplay", version: "1.0.0" },
      })
    }
    case "ping":
      return reply({})
    case "tools/list":
      return reply({ tools: listTools() })
    case "tools/call": {
      const name = params.name
      if (typeof name !== "string" || !OPERATION_NAMES.includes(name as OperationName)) {
        return fail(-32602, `unknown tool ${JSON.stringify(name)}`)
      }
      return reply(await callTool(name as OperationName, params.arguments))
    }
    default:
      return fail(-32601, `method not found: ${message.method}`)
  }
}

/** Serves MCP on stdin and stdout until stdin closes. The daemon and its games outlive it. */
export async function runMcp(): Promise<void> {
  const out = Deno.stdout.writable.getWriter()
  const encoder = new TextEncoder()
  const write = (message: object) => out.write(encoder.encode(JSON.stringify(message) + "\n"))
  const inFlight = new Set<Promise<void>>()
  for await (const line of readLines(Deno.stdin.readable)) {
    if (line.trim() === "") continue
    let message: unknown
    try {
      message = JSON.parse(line)
    } catch {
      await write({ jsonrpc: "2.0", id: null, error: { code: -32700, message: "parse error" } })
      continue
    }
    if (!isRequest(message)) continue
    // Concurrent: a start takes seconds and must not hold up a ping or another session's step.
    const task = respond(message).then((reply) => reply && write(reply)).catch((e) => {
      console.error(`bnplay mcp: ${(e as Error).stack ?? e}`)
    }).then(() => void inFlight.delete(task))
    inFlight.add(task)
  }
  await Promise.all(inFlight)
}
