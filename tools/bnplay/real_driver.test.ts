/**
 * Runs the driver contract suite against the real game binary on a clone of a fixture save.
 *
 * Environment (all optional):
 *   BNPLAY_BINARY   tiles binary (default out/build/osx-arm-slim/src/cataclysm-bn-tiles)
 *   BNPLAY_BASEPATH `--basepath` for the game, where its data lives (default this checkout)
 *   BNPLAY_SAVE     source save directory to clone (default the local Bairdford save)
 *   BNPLAY_WINDOWED unset: windowless only. `1`: also run every suite with a real game window
 *                   (visible, in a corner, no focus taken); `only`: just the windowed run. A
 *                   windowed run needs a display session, and puts a window on the desktop for
 *                   the seconds each suite takes; run it under the game lock, never unattended.
 *
 * Windowed boot, measured against the windowless baseline (osx-arm-slim, Bairdford, a 2x HiDPI
 * display, a busy machine; first ping and `footprint` physical footprint after boot):
 *   windowless  boot 7.2 to 8.0 s    footprint 810 to 860 MB
 *   windowed    boot 18.8 to 22.9 s  footprint 2.89 GB   (1280x720)
 * So a windowed Episode takes about 2.5 times as long to boot and about 3.4 times the memory: the
 * interface init, the GPU device and its lighting buffers come on top. Hence one windowed Episode
 * at a time.
 */
import { fromFileUrl, join } from "@std/path"
import { spawnDriver } from "./client.ts"
import { runActivityContract } from "./activity_contract.ts"
import { runCombatContract } from "./combat_contract.ts"
import { runCliLifecycle } from "./cli_lifecycle.ts"
import { type ContractTarget, runContract } from "./contract.ts"
import { runItemContract } from "./item_contract.ts"
import { runMenuContract } from "./menu_contract.ts"
import { runSceneContract } from "./scene_contract.ts"
import { runTimeContract } from "./time_contract.ts"
import type { WindowSize } from "./trial.ts"
import { runViewContract } from "./view_contract.ts"

const repo = fromFileUrl(new URL("../../", import.meta.url)).replace(/\/$/, "")
const binary = Deno.env.get("BNPLAY_BINARY") ??
  join(repo, "out/build/osx-arm-slim/src/cataclysm-bn-tiles")
const basepath = Deno.env.get("BNPLAY_BASEPATH") ?? repo
const sourceSave = Deno.env.get("BNPLAY_SAVE") ??
  join(Deno.env.get("HOME") ?? "", "Library/Application Support/Cataclysm-BN/save/Bairdford")
const world = "Bairdford"

/**
 * `window` launches the game windowed: a real window on the desktop, so only ever one at a time
 * and only when asked for (BNPLAY_WINDOWED, above).
 */
function makeTarget(window?: WindowSize): ContractTarget {
  return {
    bootTimeoutMs: 30_000,
    async spawn(opts) {
      const userdir = await Deno.makeTempDir({ prefix: "bnplay-" })
      await Deno.mkdir(join(userdir, "save"))
      // Copy-on-write clone: the source save is never modified.
      const cp = await new Deno.Command("cp", {
        args: ["-cR", sourceSave, join(userdir, "save", world)],
      }).output()
      if (!cp.success) throw new Error("cloning the fixture save failed")
      const driver = spawnDriver({
        binary,
        userdir,
        world,
        basepath,
        firstTimeoutMs: 30_000,
        denyList: opts?.denyList,
        scenesDir: opts?.scenesDir,
        window,
        stderr: Deno.env.get("BNPLAY_VERBOSE") ? "inherit" : "null",
      })
      const close = driver.close.bind(driver)
      driver.close = async () => {
        await close()
        await Deno.remove(userdir, { recursive: true })
      }
      return driver
    },
  }
}

function runSuites(name: string, target: ContractTarget): void {
  runContract(name, target)
  runTimeContract(name, target)
  runItemContract(name, target)
  runActivityContract(name, target)
  runCombatContract(name, target)
  runMenuContract(name, target)
  runViewContract(name, target, { inventoryIds: true })
  runSceneContract(name, target, { scenesLibrary: join(repo, "tools/visual_verify/scenes") })
}

const windowedRun = Deno.env.get("BNPLAY_WINDOWED")
if (windowedRun !== "only") runSuites("real binary", makeTarget())
// Deno runs tests one after the other, so at most one game window is ever open.
if (windowedRun) runSuites("real binary (windowed)", makeTarget({ width: 1024, height: 768 }))

// The same Episode lifecycle through the CLI and the resident daemon, on a clone of the fixture.
runCliLifecycle("real binary", {
  binary,
  fixture: world,
  fixtureSource: () => Promise.resolve(sourceSave),
  env: { BNPLAY_BOOT_TIMEOUT_MS: "60000", BNPLAY_BASEPATH: basepath },
})
