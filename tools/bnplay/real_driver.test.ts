/**
 * Runs the driver contract suite against the real game binary on a clone of a fixture save.
 *
 * Environment (all optional):
 *   BNPLAY_BINARY   tiles binary (default out/build/osx-arm-slim/src/cataclysm-bn-tiles)
 *   BNPLAY_BASEPATH `--basepath` for the game, where its data lives (default this checkout)
 *   BNPLAY_SAVE     source save directory to clone (default the local Bairdford save)
 */
import { fromFileUrl, join } from "@std/path"
import { spawnDriver } from "./client.ts"
import { runCliLifecycle } from "./cli_lifecycle.ts"
import { type ContractTarget, runContract } from "./contract.ts"
import { runItemContract } from "./item_contract.ts"
import { runMenuContract } from "./menu_contract.ts"
import { runTimeContract } from "./time_contract.ts"

const repo = fromFileUrl(new URL("../../", import.meta.url)).replace(/\/$/, "")
const binary = Deno.env.get("BNPLAY_BINARY") ??
  join(repo, "out/build/osx-arm-slim/src/cataclysm-bn-tiles")
const basepath = Deno.env.get("BNPLAY_BASEPATH") ?? repo
const sourceSave = Deno.env.get("BNPLAY_SAVE") ??
  join(Deno.env.get("HOME") ?? "", "Library/Application Support/Cataclysm-BN/save/Bairdford")
const world = "Bairdford"

const target: ContractTarget = {
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

runContract("real binary", target)
runTimeContract("real binary", target)
runItemContract("real binary", target)
runMenuContract("real binary", target)

// The same Episode lifecycle through the CLI and the resident daemon, on a clone of the fixture.
runCliLifecycle("real binary", {
  binary,
  fixture: world,
  fixtureSource: () => Promise.resolve(sourceSave),
  env: { BNPLAY_BOOT_TIMEOUT_MS: "60000", BNPLAY_BASEPATH: basepath },
})
