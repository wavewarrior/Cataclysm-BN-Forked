/**
 * Capturing a fixture's baseline: boot it on a clone, let the game idle briefly after it reports
 * ready, quit it, and keep the game-log lines logged in between.
 *
 * Only that stretch counts. Lines logged while loading (a mod-heavy world logs thousands of JSON
 * errors there) come before readiness, and the supervisor's later check compares an Episode's
 * post-readiness lines against this baseline. The game flushes its log as it exits, so the log is
 * read after the quit, and the stretch is cut out by the game's own timestamps (see gamelog.ts).
 */
import { delay } from "@std/async"
import { join } from "@std/path"
import type { Config } from "./config.ts"
import { type Episode, HarnessError } from "./episode.ts"
import { baselinePath, fixtureIdentity, writeBaseline } from "./fixtures.ts"
import { linesInWindow } from "./gamelog.ts"
import { FIXTURE_NAME, type Trial } from "./trial.ts"

/** Lines of the baseline echoed back to the caller; the record on disk has all of them. */
const SAMPLE_LINES = 5

/** Allowance on top of boot and idle for the quit and the exit before the watchdog fires. */
const SHUTDOWN_ALLOWANCE_S = 30

export type CapturedBaseline = {
  fixture: string
  baseline: "fresh"
  captured_at: string
  lines: number
  sample: string[]
  boot_ms: number
  idle_ms: number
  /** The baseline record. */
  path: string
  /** The whole debug.log of the capturing run, boot and shutdown lines included. */
  log: string
  transcript: string
}

/**
 * Captures and records `fixture`'s baseline. `boot` starts the Episode the capture runs on (the
 * daemon decides how it counts against its session cap); the Episode is always ended before this
 * returns, and a capture that fails records nothing and keeps the previous baseline.
 */
export async function captureBaseline(
  config: Config,
  fixture: string,
  boot: (trial: Trial) => Promise<Episode>,
): Promise<CapturedBaseline> {
  if (!FIXTURE_NAME.test(fixture)) {
    throw new HarnessError(`fixture name \`${fixture}\` is not a plain fixture name`)
  }
  const world = join(config.fixtures, fixture)
  if (!(await Deno.stat(world).then((s) => s.isDirectory, () => false))) {
    throw new HarnessError(`fixture ${fixture} not found in ${config.fixtures}`)
  }
  if (!(await Deno.stat(config.binary).then((s) => s.isFile, () => false))) {
    throw new HarnessError(`game binary not found: ${config.binary}`)
  }

  const identity = await fixtureIdentity(world)
  const episode = await boot({
    fixture,
    wallClockLimitS: Math.ceil((config.bootTimeoutMs + config.baselineIdleMs) / 1000) +
      SHUTDOWN_ALLOWANCE_S,
    expectedCommands: [],
    oracles: [],
    rendererOracles: [],
  })
  let quitAt = Date.now()
  try {
    await delay(config.baselineIdleMs)
    quitAt = Date.now()
    const quit = await episode.step({ cmd: "quit" })
    if (quit.status !== "ok") {
      throw new HarnessError(`the game refused to quit: ${JSON.stringify(quit)}`)
    }
  } finally {
    // Idempotent: waits for the game to exit after the quit, or ends it if the capture failed.
    await episode.stop()
  }
  const { ended, exit_code } = episode.summary()
  if (ended !== "stop" || exit_code !== 0) {
    throw new HarnessError(
      `the game did not exit cleanly after the idle (ended: ${ended}, exit code ${exit_code}), ` +
        `so its log may be incomplete; no baseline recorded (transcript: ${episode.transcriptPath})`,
    )
  }

  let log: string
  try {
    log = await Deno.readTextFile(episode.debugLogPath)
  } catch {
    throw new HarnessError(
      `the game wrote no debug.log at ${episode.debugLogPath}; no baseline recorded`,
    )
  }
  const lines = linesInWindow(log, { fromMs: episode.readyAt, toMs: quitAt })

  // The capture played on a clone, but a fixture edited meanwhile is not the one the lines came from.
  const after = await fixtureIdentity(world)
  if (after.digest !== identity.digest || after.mods !== identity.mods) {
    throw new HarnessError(
      `fixture ${fixture} changed while its baseline was captured; no baseline recorded. ` +
        `Run the baseline again once nothing edits the fixture`,
    )
  }

  const captured_at = new Date().toISOString()
  await writeBaseline(config.fixtures, {
    fixture,
    captured_at,
    identity,
    boot_ms: episode.bootMs,
    idle_ms: config.baselineIdleMs,
    lines,
  })
  return {
    fixture,
    baseline: "fresh",
    captured_at,
    lines: lines.length,
    sample: lines.slice(0, SAMPLE_LINES),
    boot_ms: episode.bootMs,
    idle_ms: config.baselineIdleMs,
    path: baselinePath(config.fixtures, fixture),
    log: episode.debugLogPath,
    transcript: episode.transcriptPath,
  }
}
