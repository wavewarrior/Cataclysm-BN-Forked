/** Runs the driver contract suites against the mock driver (no game, no display, no memory cost). */
import { spawnDriver } from "./client.ts"
import { type ContractTarget, runContract } from "./contract.ts"
import { runCombatContract } from "./combat_contract.ts"
import { runMenuContract } from "./menu_contract.ts"
import { MOCK_DRIVER } from "./testkit.ts"
import { runViewContract } from "./view_contract.ts"
import { runTimeContract } from "./time_contract.ts"

const target: ContractTarget = {
  bootTimeoutMs: 10_000,
  async spawn(opts) {
    const userdir = await Deno.makeTempDir({ prefix: "bnplay-mock-" })
    const driver = spawnDriver({
      binary: MOCK_DRIVER,
      userdir,
      world: "mock",
      basepath: userdir,
      denyList: opts?.denyList,
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

runContract("mock driver", target)
runTimeContract("mock driver", target)
runMenuContract("mock driver", target)
runCombatContract("mock driver", target)
runViewContract("mock driver", target)
