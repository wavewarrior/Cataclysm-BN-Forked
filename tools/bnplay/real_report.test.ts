/**
 * The Trial's seed and turn limit, the oracles and the report against the real game binary, on a
 * clone of a real save: a clean run exits 0 with a passing built-in game-log check against the
 * fixture's baseline, and the turn limit ends the Episode by itself.
 *
 * Environment (all optional), as in real_fixtures.test.ts: BNPLAY_BINARY, BNPLAY_BASEPATH,
 * BNPLAY_SAVE.
 */
import { assert, assertEquals } from "@std/assert"
import { join } from "@std/path"
import { loadConfig } from "./config.ts"
import {
  jsonOut,
  makeSandbox,
  pidsMatching,
  readTranscript,
  realSave,
  requireCompiledShaders,
} from "./testkit.ts"

const config = loadConfig()
const { binary, basepath } = config
await requireCompiledShaders(config)

type Report = {
  verdict: string
  exit_code: number
  ended: string
  oracles: { name: string; result: string; first_fail?: { index: number } }[]
  boot_ms: number
  turns?: { first: number; last: number }
  transcript: string
}

Deno.test({
  name: "a real Episode runs to its turn limit and reports against the fixture baseline",
  sanitizeOps: false,
  sanitizeResources: false,
  async fn() {
    const sandbox = await makeSandbox({
      binary,
      env: {
        BNPLAY_BASEPATH: basepath,
        BNPLAY_BOOT_TIMEOUT_MS: "60000",
        BNPLAY_BASELINE_IDLE_MS: "2000",
      },
    })
    try {
      const added = await sandbox.cli(["fixture", "add", realSave()])
      assertEquals(added.code, 0, added.stderr)
      const fixture = jsonOut<{ fixture: string }>(added).fixture
      const baseline = await sandbox.cli(["fixture", "baseline", fixture])
      assertEquals(baseline.code, 0, baseline.stderr)

      const trial = await sandbox.trial(`
        fixture = "${fixture}"
        seed = 7
        turn_limit = 3
        wall_clock_limit_s = 150
        expected_commands = ["wait"]
        [[oracle]]
        name = "alive avatar"
        field = "hp"
        operator = "gt"
        value = 0
        [[oracle]]
        name = "impossible"
        field = "hp"
        operator = "lt"
        value = 0
        severity = "warn"
      `)
      const started = await sandbox.cli(["start", trial])
      assertEquals(started.code, 0, started.stderr)
      const session = jsonOut<{ session: string }>(started).session

      // The first action after load may complete a partial turn, so two waits cross three turns.
      const wait = ["step", session, '{"cmd":"wait","turns":2}']
      const first = await sandbox.cli(wait)
      assertEquals(first.code, 0, first.stderr)
      assertEquals(jsonOut(first).episode_ended, undefined)
      const second = await sandbox.cli(wait)
      assertEquals(second.code, 0, second.stderr)
      assertEquals(jsonOut(second).episode_ended, "turn_limit")

      const stop = await sandbox.cli(["stop", session])
      assertEquals(stop.code, 0, stop.stderr)
      const report = jsonOut<Report>(stop)
      assertEquals(report.ended, "turn_limit")
      assertEquals(report.verdict, "pass")
      const results = Object.fromEntries(report.oracles.map((o) => [o.name, o.result]))
      assertEquals(results, {
        alive: "pass",
        clean_exit: "pass",
        game_log: "pass",
        turn_counter: "pass",
        commands: "pass",
        "alive avatar": "pass",
        impossible: "warn",
      })
      assert(report.boot_ms > 0)
      assert(report.turns!.last - report.turns!.first >= 3, JSON.stringify(report.turns))

      const requests = (await readTranscript(report.transcript)).flatMap((r) =>
        r.request ? [r.request.cmd] : []
      )
      assertEquals(requests.slice(0, 3), ["ping", "seed", "state"])
      assertEquals(await pidsMatching(join(sandbox.home, "episodes", session)), [])
    } finally {
      await sandbox.cleanup()
    }
  },
})

Deno.test({
  name: "a real Episode counts from its pinned clock and a mid-Episode set_time spends no turns",
  sanitizeOps: false,
  sanitizeResources: false,
  async fn() {
    const sandbox = await makeSandbox({
      binary,
      env: {
        BNPLAY_BASEPATH: basepath,
        BNPLAY_BOOT_TIMEOUT_MS: "60000",
        BNPLAY_BASELINE_IDLE_MS: "2000",
      },
    })
    try {
      const added = await sandbox.cli(["fixture", "add", realSave()])
      assertEquals(added.code, 0, added.stderr)
      const fixture = jsonOut<{ fixture: string }>(added).fixture
      const baseline = await sandbox.cli(["fixture", "baseline", fixture])
      assertEquals(baseline.code, 0, baseline.stderr)

      // Every clock here is 3 turns past a multiple of 9, so the few turns played never reach the
      // game's 9-turn morale self-check (character_turn.cpp), which reports the fixture avatar's
      // "Stylish" morale as inconsistent and would fail game_log for a reason unrelated to the clock.
      const trial = await sandbox.trial(`
        fixture = "${fixture}"
        seed = 7
        time_of_day = "08:02"
        turn_limit = 3
        wall_clock_limit_s = 150
      `)
      const started = await sandbox.cli(["start", trial])
      assertEquals(started.code, 0, started.stderr)
      const session = jsonOut<{ session: string }>(started).session
      const step = async (request: object) => {
        const res = await sandbox.cli(["step", session, JSON.stringify(request)])
        assertEquals(res.code, 0, res.stderr)
        return jsonOut<Record<string, unknown>>(res)
      }

      const pinned = (await step({ cmd: "state" })).turn as number
      assertEquals(pinned % 86_400, 8 * 3_600 + 120, "the first state is at the pinned time of day")
      for (const time of ["20:02", "02:02"]) {
        const jumped = await step({ cmd: "set_time", time })
        assertEquals(jumped.time, time)
        assertEquals(jumped.episode_ended, undefined, `the jump to ${time} spent no turns`)
      }
      // The first action after load may complete a partial turn, so two waits cross three turns.
      assertEquals((await step({ cmd: "wait", turns: 2 })).episode_ended, undefined)
      assertEquals((await step({ cmd: "wait", turns: 2 })).episode_ended, "turn_limit")

      const stop = await sandbox.cli(["stop", session])
      assertEquals(stop.code, 0, stop.stdout + stop.stderr)
      const report = jsonOut<Report>(stop)
      assertEquals(report.ended, "turn_limit")
      assertEquals(report.verdict, "pass", JSON.stringify(report.oracles))
      assertEquals(report.oracles.find((o) => o.name === "turn_counter")?.result, "pass")
      // The ping carries no turn, so the Episode counts from the pinned clock, not the save's.
      assertEquals(report.turns?.first, pinned)
      const records = await readTranscript(report.transcript)
      const ping = records.find((r) => r.response?.ready === true)?.response
      assert(ping !== undefined && !("turn" in ping), JSON.stringify(ping))
      assertEquals(
        records.flatMap((r) => r.request ? [r.request.cmd] : []).slice(0, 4),
        ["ping", "seed", "set_time", "state"],
      )
      assertEquals(await pidsMatching(join(sandbox.home, "episodes", session)), [])
    } finally {
      await sandbox.cleanup()
    }
  },
})
