/**
 * Only one windowed session at a time, end to end through the CLI and the daemon against the mock
 * driver: a second windowed `start` is refused with a clear error, windowless sessions are
 * unaffected, and a windowed start works again once the window is gone.
 */
import { assert, assertEquals } from "@std/assert"
import { jsonOut } from "./testkit.ts"
import { start, step, stop, windowed, withSandbox } from "./renderer_testkit.ts"

Deno.test("only one windowed session runs at a time; windowless sessions are unaffected", async () => {
  await withSandbox(async (sandbox) => {
    const first = await start(sandbox, windowed())

    const second = await sandbox.cli(["start", await sandbox.trial(windowed())])
    assertEquals(second.code, 2)
    assert(second.stderr.includes("windowed"), second.stderr)
    assert(second.stderr.includes(first), `the error names the live session: ${second.stderr}`)

    // A windowless session boots beside it, and the refusal left no game behind.
    const plain = await start(sandbox, `fixture = "bairdford"\n`)
    assertEquals((await step(sandbox, plain, { cmd: "state" })).status, "ok")
    assertEquals((await step(sandbox, first, { cmd: "state" })).status, "ok")

    // Once the windowed session has ended, another may start.
    assertEquals((await stop(sandbox, first)).code, 0)
    const again = await start(sandbox, windowed())
    assertEquals((await stop(sandbox, again)).code, 0)
    assertEquals((await stop(sandbox, plain)).code, 0)
  })
})

Deno.test("two windowed starts at the same moment: one boots, one is refused", async () => {
  await withSandbox(async (sandbox) => {
    const trial = await sandbox.trial(windowed())
    const [a, b] = await Promise.all([
      sandbox.cli(["start", trial]),
      sandbox.cli(["start", trial]),
    ])
    assertEquals([a.code, b.code].sort(), [0, 2], `${a.stderr} ${b.stderr}`)
    const booted = a.code === 0 ? a : b
    assertEquals((await stop(sandbox, jsonOut<{ session: string }>(booted).session)).code, 0)
  }, { MOCK_BOOT_DELAY_S: "1" })
})
