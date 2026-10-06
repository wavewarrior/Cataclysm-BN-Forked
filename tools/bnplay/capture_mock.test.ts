/**
 * `capture` through the supervisor, end to end through the CLI against the mock driver: it goes
 * through `step` like any command, the frames land under the Episode's own artifact directory
 * whatever the agent asked for, and a windowless Episode refuses it.
 */
import { assert, assertEquals } from "@std/assert"
import { join } from "@std/path"
import { jsonOut, makeFakeWorld, makeSandbox, pidsMatching } from "./testkit.ts"
import type { Sandbox } from "./testkit.ts"

async function withSandbox(body: (sandbox: Sandbox) => Promise<void>): Promise<void> {
  const world = await makeFakeWorld()
  const sandbox = await makeSandbox({ fixtureSources: { bairdford: world } })
  try {
    await body(sandbox)
  } finally {
    await sandbox.cleanup()
    await Deno.remove(world, { recursive: true })
    assertEquals(await pidsMatching(sandbox.home), [])
  }
}

const WINDOWED = `fixture = "bairdford"\nmode = "windowed"\nwindow_size = [1024, 768]\n`

async function start(sandbox: Sandbox, toml: string) {
  const res = await sandbox.cli(["start", await sandbox.trial(toml)])
  assertEquals(res.code, 0, res.stderr)
  return jsonOut<{ session: string }>(res).session
}

async function step(sandbox: Sandbox, session: string, request: object) {
  const res = await sandbox.cli(["step", session, JSON.stringify(request)])
  assertEquals(res.code, 0, res.stderr)
  return jsonOut<Record<string, unknown>>(res)
}

type Capture = { frame: string; map: string; mode: string; width: number; height: number }

Deno.test("a capture goes through step and lands under the Episode's own artifact directory", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, WINDOWED)
    const captures = join(sandbox.home, "episodes", session, "captures")
    const elsewhere = await Deno.makeTempDir({ prefix: "bnplay-elsewhere-" })
    try {
      const final = await step(sandbox, session, { cmd: "capture" })
      assertEquals(final.outcome, "completed", JSON.stringify(final))
      const c = final.capture as Capture
      assertEquals(c.mode, "final")
      assertEquals(c.frame.startsWith(captures + "/"), true, c.frame)
      assertEquals(c.map.startsWith(captures + "/"), true, c.map)
      assert((await Deno.stat(c.frame)).size > 0 && (await Deno.stat(c.map)).size > 0)

      const state = await step(sandbox, session, { cmd: "capture", mode: "state" })
      assertEquals((state.capture as Capture).mode, "state")
      assertEquals((state.capture as Capture).frame.startsWith(captures + "/"), true)

      // Where the agent says the files go does not matter: they stay with the Episode.
      const asked = await step(sandbox, session, { cmd: "capture", dir: elsewhere })
      assertEquals((asked.capture as Capture).frame.startsWith(captures + "/"), true)
      assertEquals([...Deno.readDirSync(elsewhere)], [])

      // The report points at where they are.
      const stopped = await sandbox.cli(["stop", session])
      assertEquals(stopped.code, 0, stopped.stderr)
      assertEquals(jsonOut<{ captures?: string }>(stopped).captures, captures)
    } finally {
      await Deno.remove(elsewhere, { recursive: true })
    }
  })
})

Deno.test("a refusal from a window with no drawable comes back as the response, without files", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, WINDOWED)
    await step(sandbox, session, { cmd: "minimise" })
    const refused = await step(sandbox, session, { cmd: "capture" })
    assertEquals(refused.outcome, "refused", JSON.stringify(refused))
    assertEquals(refused.reason, "no_drawable")
    assertEquals("capture" in refused, false)
    const captures = join(sandbox.home, "episodes", session, "captures")
    const left = await Array.fromAsync(Deno.readDir(captures)).catch(() => [])
    assertEquals(left, [], "a refused capture left files")
    const stopped = await sandbox.cli(["stop", session])
    assertEquals(stopped.code, 0, stopped.stderr)
    // Nothing was captured, so the report names no capture directory.
    assertEquals("captures" in jsonOut(stopped), false)
  })
})

Deno.test("a windowless Episode refuses capture as a protocol error that says it needs the windowed mode", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, `fixture = "bairdford"\n`)
    const res = await step(sandbox, session, { cmd: "capture" })
    assertEquals(res.status, "error", JSON.stringify(res))
    assert(String(res.error).includes("windowed"), String(res.error))
    assertEquals((await sandbox.cli(["stop", session])).code, 0)
  })
})
