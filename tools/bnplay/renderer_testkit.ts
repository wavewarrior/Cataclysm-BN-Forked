/**
 * Helpers of the windowed tests that run against the mock driver: a sandbox with a baselined
 * fixture, a small windowed Trial, and the commands that play an Episode through the CLI.
 */
import { assert, assertEquals } from "@std/assert"
import { jsonOut, makeFakeWorld, makeSandbox, pidsMatching, type Sandbox } from "./testkit.ts"

export type Oracle = {
  name: string
  result: string
  first_fail?: { index: number; turn?: number; why: string }
}
export type Report = {
  verdict: string
  exit_code: number
  oracles: Oracle[]
  notes?: string[]
  transcript: string
  captures?: string
}
export type Step = Record<string, unknown> & { id: number; status: string; outcome?: string }

export async function withSandbox(
  body: (sandbox: Sandbox) => Promise<void>,
  env: Record<string, string> = {},
): Promise<void> {
  const world = await makeFakeWorld()
  const sandbox = await makeSandbox({
    fixtureSources: { bairdford: world },
    env: { BNPLAY_BASELINE_IDLE_MS: "300", ...env },
  })
  try {
    const baseline = await sandbox.cli(["fixture", "baseline", "bairdford"])
    assertEquals(baseline.code, 0, baseline.stderr)
    await body(sandbox)
  } finally {
    await sandbox.cleanup()
    await Deno.remove(world, { recursive: true })
    assertEquals(await pidsMatching(sandbox.home), [])
  }
}

/** A windowed Trial of a small window: the frames stay quick to draw and to decode. */
export const windowed = (oracles = "") =>
  `fixture = "bairdford"\nmode = "windowed"\nwindow_size = [640, 384]\n${oracles}`

export async function start(sandbox: Sandbox, toml: string): Promise<string> {
  const res = await sandbox.cli(["start", await sandbox.trial(toml)])
  assertEquals(res.code, 0, res.stderr)
  return jsonOut<{ session: string }>(res).session
}

export async function step(sandbox: Sandbox, session: string, request: object): Promise<Step> {
  const res = await sandbox.cli(["step", session, JSON.stringify(request)])
  assertEquals(res.code, 0, res.stderr)
  return jsonOut<Step>(res)
}

/** Sends a series of commands; returns each answer. */
export async function play(
  sandbox: Sandbox,
  session: string,
  requests: object[],
): Promise<Step[]> {
  const answers: Step[] = []
  for (const request of requests) answers.push(await step(sandbox, session, request))
  return answers
}

export async function stop(sandbox: Sandbox, session: string, verb = "stop") {
  const res = await sandbox.cli([verb, session])
  return { code: res.code, report: jsonOut<Report>(res) }
}

export const oracle = (report: Report, name: string) => {
  const found = report.oracles.find((o) => o.name === name)
  assert(found, `no ${name} oracle in ${JSON.stringify(report.oracles)}`)
  return found
}

export const capture = (tag: string, mode?: string) => ({
  cmd: "capture",
  tag,
  ...(mode ? { mode } : {}),
})
