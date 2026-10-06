/**
 * The operations bnplay offers, defined once. The CLI (cli.ts) and the MCP server (mcp.ts) are both
 * generated from this table: command words, parameters, descriptions, input validation and the
 * daemon request each one becomes. A new operation is added here and appears in both front ends;
 * the table is keyed by the daemon's request type, so an operation the daemon serves but no front
 * end offers does not compile.
 */
import { resolve } from "@std/path"
import type { DriverRequest } from "./client.ts"
import { loadConfig } from "./config.ts"
import { call, type DaemonRequest, daemonState, ensureDaemon } from "./ipc.ts"

/** A caller's input that is not valid for the operation it names. */
export class UsageError extends Error {}

export type ParamKind = "string" | "boolean" | "object" | "path"

export type Param = {
  /** Name of the MCP argument; the CLI flag is `--name` with `_` written as `-`. */
  name: string
  kind: ParamKind
  description: string
  required: boolean
  /** CLI only: a positional argument (in declaration order) or a `--flag`. */
  cli: "arg" | "flag"
  /** CLI only: how the usage line writes the argument. */
  placeholder?: string
}

export type OperationName = Exclude<DaemonRequest["op"], "ping">

export type Operation<K extends OperationName = OperationName> = {
  /** CLI command words; the MCP tool is named after the table key. */
  cli: string[]
  description: string
  params: Param[]
  /** Builds the daemon request from input that `validateInput` has already checked. */
  request(input: Record<string, unknown>): Extract<DaemonRequest, { op: K }>
}

const string = (input: Record<string, unknown>, key: string) => input[key] as string

export const OPERATIONS: { [K in OperationName]: Operation<K> } = {
  start: {
    cli: ["start"],
    description:
      "Boot an Episode from a Trial file and return its session id. The game boots in 7 to 10 " +
      "seconds; a start beyond the session cap is refused. Follow with step, then stop.",
    params: [{
      name: "trial",
      kind: "path",
      description: "Path to the Trial TOML file",
      required: true,
      cli: "arg",
      placeholder: "<trial.toml>",
    }],
    request: (a) => ({ op: "start", trial: string(a, "trial") }),
  },
  step: {
    cli: ["step"],
    description: "Send one driver command to a running Episode and return the lean response. " +
      'Example command: {"cmd":"state"}. A driver error comes back as the response.',
    params: [
      {
        name: "session",
        kind: "string",
        description: "Session id returned by start",
        required: true,
        cli: "arg",
        placeholder: "<session>",
      },
      {
        name: "command",
        kind: "object",
        description: 'The driver command as a JSON object, e.g. {"cmd":"state"}',
        required: true,
        cli: "arg",
        placeholder: "'<command json>'",
      },
    ],
    request: (a) => ({
      op: "step",
      session: string(a, "session"),
      request: a.command as DriverRequest,
    }),
  },
  stop: {
    cli: ["stop"],
    description:
      "End the Episode and return its report. The report's exit_code is the verdict: 0 pass, " +
      "1 an oracle failed, 2 harness error, 3 inconclusive.",
    params: [{
      name: "session",
      kind: "string",
      description: "Session id returned by start",
      required: true,
      cli: "arg",
      placeholder: "<session>",
    }],
    request: (a) => ({ op: "stop", session: string(a, "session") }),
  },
  report: {
    cli: ["report"],
    description:
      "Return the report of an Episode, running or ended. The report's exit_code is the verdict: " +
      "0 pass, 1 an oracle failed, 2 harness error, 3 inconclusive.",
    params: [{
      name: "session",
      kind: "string",
      description: "Session id returned by start",
      required: true,
      cli: "arg",
      placeholder: "<session>",
    }],
    request: (a) => ({ op: "report", session: string(a, "session") }),
  },
  fixture_add: {
    cli: ["fixture", "add"],
    description: "Clone a world save into the fixture library (copy-on-write) as a named fixture.",
    params: [
      {
        name: "source",
        kind: "path",
        description: "Directory of the world save to clone",
        required: true,
        cli: "arg",
        placeholder: "<save-dir>",
      },
      {
        name: "name",
        kind: "string",
        description: "Fixture name (default: the save directory's name)",
        required: false,
        cli: "arg",
        placeholder: "[name]",
      },
    ],
    request: (a) => ({
      op: "fixture_add",
      source: string(a, "source"),
      name: a.name as string | undefined,
    }),
  },
  fixture_baseline: {
    cli: ["fixture", "baseline"],
    description:
      "Boot a fixture and record its post-readiness game log as the baseline its oracles need. " +
      "Refresh it whenever the fixture or its mod set changes.",
    params: [{
      name: "name",
      kind: "string",
      description: "Fixture name",
      required: true,
      cli: "arg",
      placeholder: "<name>",
    }],
    request: (a) => ({ op: "fixture_baseline", name: string(a, "name") }),
  },
  fixture_list: {
    cli: ["fixture", "list"],
    description: "List the fixture library and whether each fixture's baseline is fresh.",
    params: [],
    request: () => ({ op: "fixture_list" }),
  },
  doctor: {
    cli: ["doctor"],
    description:
      "Preflight: driver flag in the binary, binary freshness, fixture and baseline, stray driver " +
      "processes, memory and swap. Starts no game unless self_check asks for the A/A determinism " +
      "pair. A failed check is reported in the result (healthy: false), not as an error.",
    params: [
      {
        name: "fixture",
        kind: "string",
        description: "Fixture whose presence and baseline to check",
        required: false,
        cli: "flag",
        placeholder: "<name>",
      },
      {
        name: "self_check",
        kind: "boolean",
        description: "Also boot two Episodes from the same seed and compare them",
        required: false,
        cli: "flag",
      },
    ],
    request: (a) => ({
      op: "doctor",
      fixture: a.fixture as string | undefined,
      self_check: a.self_check === true,
    }),
  },
  shutdown: {
    cli: ["shutdown"],
    description: "End every Episode and stop the resident daemon.",
    params: [],
    request: () => ({ op: "shutdown" }),
  },
}

export const OPERATION_NAMES = Object.keys(OPERATIONS) as OperationName[]

/** `--self-check` for the parameter `self_check`. */
export function flagName(param: Param): string {
  return `--${param.name.replaceAll("_", "-")}`
}

function isPlainObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value)
}

/**
 * Checks `raw` (named arguments, from the CLI parser or an MCP call) against the operation's
 * parameters and returns the normalised input: paths resolved against the working directory.
 */
export function validateInput(name: OperationName, raw: unknown): Record<string, unknown> {
  const op = OPERATIONS[name]
  const label = op.cli.join(" ")
  if (!isPlainObject(raw)) throw new UsageError(`${label}: the arguments must be an object`)
  for (const key of Object.keys(raw)) {
    if (!op.params.some((p) => p.name === key)) {
      throw new UsageError(`${label}: unexpected argument ${key}`)
    }
  }
  const input: Record<string, unknown> = {}
  for (const param of op.params) {
    const value = raw[param.name]
    if (value === undefined || value === null) {
      if (param.required) throw new UsageError(`${label}: missing ${param.name}`)
      continue
    }
    switch (param.kind) {
      case "string":
      case "path":
        if (typeof value !== "string" || value === "") {
          throw new UsageError(`${label}: ${param.name} must be a non-empty string`)
        }
        input[param.name] = param.kind === "path" ? resolve(value) : value
        break
      case "boolean":
        if (typeof value !== "boolean") {
          throw new UsageError(`${label}: ${param.name} must be a boolean`)
        }
        input[param.name] = value
        break
      case "object":
        if (!isPlainObject(value)) {
          throw new UsageError(
            `${label}: ${param.name} must be a JSON object, e.g. {"cmd":"state"}`,
          )
        }
        input[param.name] = value
        break
    }
  }
  return input
}

/** Validates `raw` and builds the daemon request for the operation. */
export function buildRequest(name: OperationName, raw: unknown): DaemonRequest {
  return OPERATIONS[name].request(validateInput(name, raw))
}

/** Daemon starts are serialised: concurrent MCP calls must not each start a daemon. */
let ensuring: Promise<void> = Promise.resolve()

/**
 * Runs one request against the resident daemon, starting it first unless the request is a
 * shutdown (which must not start a daemon just to stop it). Returns the result object; a daemon
 * refusal rejects with its message.
 */
export async function runRequest(request: DaemonRequest): Promise<object> {
  const { home } = loadConfig()
  if (request.op === "shutdown") {
    if ((await daemonState(home)) === "absent") return { daemon: "not running" }
  } else {
    const ready = ensuring.then(() => ensureDaemon(home))
    ensuring = ready.catch(() => undefined)
    await ready
  }
  const reply = await call(home, request)
  if (!reply.ok) throw new Error(reply.error)
  return reply.result
}
