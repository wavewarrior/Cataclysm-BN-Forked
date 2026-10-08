/** The CLI front end: turns a command line into the operation it asks for, from the shared table. */
import type { DaemonRequest } from "./ipc.ts"
import {
  buildRequest,
  flagName,
  OPERATION_NAMES,
  type OperationName,
  OPERATIONS,
  type Param,
  UsageError,
} from "./operations.ts"

function usageLine(name: OperationName): string {
  const op = OPERATIONS[name]
  const args = op.params.filter((p) => p.cli === "arg").map((p) => p.placeholder)
  const flags = op.params.filter((p) => p.cli === "flag").map((p) =>
    `[${flagName(p)}${p.kind === "boolean" ? "" : ` ${p.placeholder}${p.repeat ? " ..." : ""}`}]`
  )
  return ["  bnplay", ...op.cli, ...args, ...flags].join(" ")
}

/** One line per operation, then the commands that are not operations. */
export const USAGE = `usage:\n${[...OPERATION_NAMES.map(usageLine), "  bnplay mcp"].join("\n")}`

/** Parses one CLI value; the only structured kind is a JSON object. */
function parseValue(param: Param, text: string): unknown {
  if (param.kind !== "object") return text
  try {
    return JSON.parse(text)
  } catch {
    throw new UsageError(`the ${param.name} is not valid JSON: ${text}`)
  }
}

/** Turns the command line (without `mcp` and `daemon`) into the operation and its request. */
export function parseArgs(args: string[]): { name: OperationName; request: DaemonRequest } {
  const name = OPERATION_NAMES.find((n) => OPERATIONS[n].cli.every((word, i) => args[i] === word))
  if (name === undefined) throw new UsageError(USAGE)
  const op = OPERATIONS[name]
  const label = `bnplay ${op.cli.join(" ")}`
  const positional = op.params.filter((p) => p.cli === "arg")
  const raw: Record<string, unknown> = {}
  const rest = args.slice(op.cli.length)
  let taken = 0
  for (let i = 0; i < rest.length; i++) {
    const token = rest[i]
    if (token.startsWith("--")) {
      const flag = op.params.find((p) => p.cli === "flag" && flagName(p) === token)
      if (!flag) throw new UsageError(`${label}: unexpected argument ${token}\n${USAGE}`)
      if (flag.kind === "boolean") {
        raw[flag.name] = true
      } else if (i + 1 < rest.length) {
        const value = parseValue(flag, rest[++i])
        if (flag.repeat) {
          raw[flag.name] = [...(raw[flag.name] as unknown[] ?? []), value]
        } else {
          raw[flag.name] = value
        }
      } else {
        throw new UsageError(`${label}: ${token} needs a value\n${USAGE}`)
      }
    } else if (taken < positional.length) {
      raw[positional[taken].name] = parseValue(positional[taken], token)
      taken++
    } else {
      throw new UsageError(`${label}: unexpected argument ${token}\n${USAGE}`)
    }
  }
  try {
    return { name, request: buildRequest(name, raw) }
  } catch (e) {
    if (e instanceof UsageError) throw new UsageError(`bnplay ${e.message}\n${USAGE}`)
    throw e
  }
}
