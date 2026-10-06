/** Runs the driver contract suite against the mock driver (no game, no display, no memory cost). */
import { spawnDriver } from "./client.ts"
import { runContract } from "./contract.ts"
import { MOCK_DRIVER } from "./testkit.ts"

runContract("mock driver", {
  bootTimeoutMs: 10_000,
  async spawn() {
    const userdir = await Deno.makeTempDir({ prefix: "bnplay-mock-" })
    const driver = spawnDriver({
      binary: MOCK_DRIVER,
      userdir,
      world: "mock",
      basepath: userdir,
      firstTimeoutMs: 10_000,
    })
    const close = driver.close.bind(driver)
    driver.close = async () => {
      await close()
      await Deno.remove(userdir, { recursive: true })
    }
    return driver
  },
})
