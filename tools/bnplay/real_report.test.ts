/**
 * The Trial's seed and turn limit, the oracles and the report against the real game binary, on a
 * clone of a real save: a clean run exits 0 with a passing built-in game-log check against the
 * fixture's baseline, and the turn limit ends the Episode by itself.
 *
 * Environment (all optional), as in real_fixtures.test.ts: BNPLAY_BINARY, BNPLAY_BASEPATH,
 * BNPLAY_SAVE.
 */
import { assert, assertEquals } from "@std/assert"
import { fromFileUrl, join } from "@std/path"
import { jsonOut, makeSandbox, pidsMatching, readTranscript } from "./testkit.ts"

const repo = fromFileUrl(new URL("../../", import.meta.url)).replace(/\/$/, "")
const binary = Deno.env.get("BNPLAY_BINARY") ??
  join(repo, "out/build/osx-arm-slim/src/cataclysm-bn-tiles")
const basepath = Deno.env.get("BNPLAY_BASEPATH") ?? repo
const sourceSave = Deno.env.get("BNPLAY_SAVE") ??
  join(Deno.env.get("HOME") ?? "", "Library/Application Support/Cataclysm-BN/save/Bairdford")

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
      const added = await sandbox.cli(["fixture", "add", sourceSave])
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
