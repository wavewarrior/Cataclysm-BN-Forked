/**
 * How long one request may take before the game counts as hung.
 *
 * The rule: a request gets `stepTimeoutMs` plus `turnTimeoutMs` for every turn it may spend, up to
 * the driver's cap of 1000 turns a request. The turns it may spend are the `turns` of a `wait` or
 * the `max_turns` of a command that runs an activity; a command that can run the world but names
 * no limit may spend the whole cap; a command that never spends time gets `stepTimeoutMs` alone.
 * So a legitimate long wait or activity, which the driver itself ends at the cap with
 * `interrupted`/`turn_cap`, is never mistaken for a hang, and a game that stops answering a
 * `state` is still caught after `stepTimeoutMs`.
 */
import type { DriverRequest } from "./client.ts"
import type { Config } from "./config.ts"

/** The driver's cap on the turns one request may spend. */
export const TURN_CAP = 1000

/** Commands that read or configure the game and never spend game time. */
const TIMELESS: Record<string, true> = {
  ping: true,
  state: true,
  quit: true,
  seed: true,
  set_time: true,
  attach_view: true,
  view: true,
  query: true,
  run_scene: true,
  capture: true,
  move: true,
}

/** The turns a request may spend, at most the cap. */
export function turnBudget(request: DriverRequest): number {
  for (const field of ["turns", "max_turns"]) {
    const asked = request[field]
    if (typeof asked === "number" && Number.isInteger(asked) && asked >= 1) {
      return Math.min(asked, TURN_CAP)
    }
  }
  return Object.hasOwn(TIMELESS, request.cmd) ? 0 : TURN_CAP
}

/** The time, in milliseconds, the game has to answer `request`. */
export function stepTimeout(
  config: Pick<Config, "stepTimeoutMs" | "turnTimeoutMs">,
  request: DriverRequest,
): number {
  return config.stepTimeoutMs + turnBudget(request) * config.turnTimeoutMs
}
