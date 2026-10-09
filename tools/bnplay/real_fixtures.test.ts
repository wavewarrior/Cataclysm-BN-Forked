/**
 * The fixture commands against the real game binary, on a copy-on-write clone of a real save:
 * `fixture add` leaves the save untouched, `fixture baseline` boots the game and keeps only what
 * it logged after it reported ready, `fixture list` goes stale when the mod set changes.
 *
 * Environment (all optional), as in real_driver.test.ts: BNPLAY_BINARY, BNPLAY_BASEPATH,
 * BNPLAY_SAVE.
 */
import { assert, assertEquals } from "@std/assert"
import { join } from "@std/path"
import { loadConfig } from "./config.ts"
import { linesInWindow } from "./gamelog.ts"
import { jsonOut, makeSandbox, pidsMatching, realSave, treeSnapshot } from "./testkit.ts"

const { binary, basepath } = loadConfig()

type Entry = { fixture: string; baseline: string; lines?: number; stale?: string[] }

Deno.test({
  name: "fixture baseline on the real game keeps only what it logged after readiness",
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
      const sourceSave = realSave()
      const saveBefore = await treeSnapshot(sourceSave)
      const added = await sandbox.cli(["fixture", "add", sourceSave])
      assertEquals(added.code, 0, added.stderr)
      const fixture = jsonOut<{ fixture: string }>(added).fixture
      assertEquals(await treeSnapshot(sourceSave), saveBefore)

      const list = async () =>
        jsonOut<{ fixtures: Entry[] }>(await sandbox.cli(["fixture", "list"])).fixtures[0]
      assertEquals((await list()).baseline, "missing")

      const res = await sandbox.cli(["fixture", "baseline", fixture])
      assertEquals(res.code, 0, res.stderr)
      const recorded = jsonOut<{ lines: number; log: string; path: string }>(res)

      // The game logs while it boots (its version, SDL and Lua banners); none of that is baseline.
      const gameLog = await Deno.readTextFile(recorded.log)
      for (const boot of ["SDL version used during compile", "LAPI version"]) {
        assert(gameLog.includes(boot), `the game log has no boot line ${boot}`)
      }
      // An empty baseline proves nothing unless the window can read this log at all: a window
      // wide enough to cover the whole run must return the boot lines the baseline left out.
      const everything = linesInWindow(gameLog, {
        fromMs: Date.now() - 3_600_000,
        toMs: Date.now() + 60_000,
      })
      for (const boot of ["SDL version used during compile", "LAPI version"]) {
        assert(everything.some((line) => line.includes(boot)), `the window lost ${boot}`)
      }
      const record = JSON.parse(await Deno.readTextFile(recorded.path))
      assertEquals(record.lines.length, recorded.lines)
      for (const line of record.lines as string[]) {
        assert(/^\d\d:\d\d:\d\d\.\d{3,4} /.test(line), `not a stamped game-log line: ${line}`)
        assert(!line.includes("SDL version") && !line.includes("LAPI version"), line)
      }

      // A fresh baseline, even if the game logged nothing at idle; then the mod set changes.
      const fresh = await list()
      assertEquals(fresh.baseline, "fresh")
      assertEquals(fresh.lines, recorded.lines)
      await Deno.writeTextFile(join(sandbox.fixtures, fixture, "mods.json"), '["dda"]\n')
      const stale = await list()
      assertEquals(stale.baseline, "stale")
      assert(stale.stale![0].includes("mod set"), stale.stale![0])

      assertEquals(await treeSnapshot(sourceSave), saveBefore)
      assert(
        (await sandbox.cli(["shutdown"])).code === 0,
        "the daemon did not shut down",
      )
      assertEquals(await pidsMatching(sandbox.home), [])
    } finally {
      await sandbox.cleanup()
    }
  },
})
