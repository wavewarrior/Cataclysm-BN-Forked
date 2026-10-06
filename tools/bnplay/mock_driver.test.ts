/** Runs the driver contract suites against the mock driver (no game, no display, no memory cost). */
import { spawnDriver } from "./client.ts"
import { type ContractTarget, runContract } from "./contract.ts"
import { runCombatContract } from "./combat_contract.ts"
import { runMenuContract } from "./menu_contract.ts"
import { runSceneContract } from "./scene_contract.ts"
import { MOCK_DRIVER } from "./testkit.ts"
import type { WindowSize } from "./trial.ts"
import { runViewContract } from "./view_contract.ts"
import { runTimeContract } from "./time_contract.ts"

/** A target whose drivers are launched windowed (the mock has no window; it checks the flag). */
function makeTarget(window?: WindowSize): ContractTarget {
  return {
    bootTimeoutMs: 10_000,
    async spawn(opts) {
      const userdir = await Deno.makeTempDir({ prefix: "bnplay-mock-" })
      const driver = spawnDriver({
        binary: MOCK_DRIVER,
        userdir,
        world: "mock",
        basepath: userdir,
        denyList: opts?.denyList,
        scenesDir: opts?.scenesDir,
        window,
        firstTimeoutMs: 10_000,
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

for (
  const [label, window] of [["mock driver", undefined], [
    "mock driver (windowed)",
    { width: 1024, height: 768 },
  ]] as const
) {
  const target = makeTarget(window)
  runContract(label, target)
  runTimeContract(label, target)
  runMenuContract(label, target)
  runCombatContract(label, target)
  runViewContract(label, target)
  runSceneContract(label, target)
}
