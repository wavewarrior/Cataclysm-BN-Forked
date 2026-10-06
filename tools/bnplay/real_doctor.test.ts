/**
 * `bnplay doctor` against the real game binary: it finds the driver flag by reading the binary and
 * starts no game doing so. (The determinism self-check boots two real games; run it by hand with
 * `bnplay doctor --self-check` under the game lock.)
 *
 * Environment (all optional), as in real_driver.test.ts:
 *   BNPLAY_BINARY   tiles binary (default out/build/osx-arm-slim/src/cataclysm-bn-tiles)
 *   BNPLAY_BASEPATH checkout the binary was built from (default this checkout)
 */
import { assertEquals, assertRejects } from "@std/assert"
import { fromFileUrl, join } from "@std/path"
import { makeSandbox } from "./testkit.ts"

const repo = fromFileUrl(new URL("../../", import.meta.url)).replace(/\/$/, "")
const binary = Deno.env.get("BNPLAY_BINARY") ??
  join(repo, "out/build/osx-arm-slim/src/cataclysm-bn-tiles")
const basepath = Deno.env.get("BNPLAY_BASEPATH") ?? repo

Deno.test({
  name: "doctor finds the driver flag in the real binary without starting a game",
  sanitizeOps: false,
  sanitizeResources: false,
  async fn() {
    const sandbox = await makeSandbox({ binary, env: { BNPLAY_BASEPATH: basepath } })
    try {
      const res = await sandbox.cli(["doctor"])
      // The preflight may fail on other counts (no fixture here, a stale binary); the flag is
      // what this test is about.
      const report = JSON.parse(res.stdout)
      const flag = report.checks.find((c: { name: string }) => c.name === "driver_flag")
      assertEquals(flag.status, "ok", JSON.stringify(flag))
      assertEquals(report.driver_available, true)
      // No Episode was started: the daemon never made an Episode directory.
      await assertRejects(() => Deno.stat(join(sandbox.home, "episodes")), Deno.errors.NotFound)
    } finally {
      await sandbox.cleanup()
    }
  },
})
