/**
 * The Trial's `attach_view` option, end to end through the CLI against the mock driver: what an
 * Episode answers when the Trial asks for a view on every response, and when the game refuses
 * the radius. Only external behaviour is asserted.
 */
import { assert, assertEquals } from "@std/assert"
import { join } from "@std/path"
import { jsonOut, makeFakeWorld, makeSandbox, pidsMatching, readTranscript } from "./testkit.ts"
import type { Sandbox } from "./testkit.ts"

async function withSandbox(body: (sandbox: Sandbox) => Promise<void>): Promise<void> {
  const world = await makeFakeWorld()
  const sandbox = await makeSandbox({ fixtureSources: { bairdford: world } })
  try {
    await body(sandbox)
  } finally {
    await sandbox.cleanup()
    await Deno.remove(world, { recursive: true })
  }
}

async function start(sandbox: Sandbox, toml: string) {
  const res = await sandbox.cli(["start", await sandbox.trial(`fixture = "bairdford"\n${toml}`)])
  assertEquals(res.code, 0, res.stderr)
  return jsonOut<{ session: string }>(res).session
}

async function step(sandbox: Sandbox, session: string, request: object) {
  const res = await sandbox.cli(["step", session, JSON.stringify(request)])
  assertEquals(res.code, 0, res.stderr)
  return jsonOut<Record<string, unknown>>(res)
}

/** Rows of the grid in the view a response carries; 0 when it carries none. */
function rows(response: Record<string, unknown>): number {
  const view = response.view
  if (typeof view !== "object" || view === null || !("grid" in view)) return 0
  return Array.isArray(view.grid) ? view.grid.length : 0
}

Deno.test("a Trial with attach_view gets a view on every response, from the first state", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, "attach_view = 2\n")
    for (
      const request of [{ cmd: "state" }, { cmd: "wait", turns: 2 }, { cmd: "move", dir: "n" }]
    ) {
      const response = await step(sandbox, session, request)
      assertEquals(rows(response), 5, `${request.cmd}: ${JSON.stringify(response)}`)
    }
    const stopped = await sandbox.cli(["stop", session])
    const { transcript } = jsonOut<{ transcript: string }>(stopped)

    const records = await readTranscript(transcript)
    const requests = records.flatMap((r) => r.request ? [r.request] : [])
    // The option is applied before the first state, so the Episode's first observation has it.
    assertEquals(requests.slice(0, 3).map((r) => r.cmd), ["ping", "attach_view", "state"])
    assertEquals(requests[1].radius, 2)
    const observations = records.flatMap((r) =>
      r.response && "turn" in r.response ? [r.response] : []
    )
    assert(observations.length >= 4, "the transcript holds the observations")
    for (const response of observations) assertEquals(rows(response), 5, JSON.stringify(response))
    assertEquals(await pidsMatching(join(sandbox.home, "episodes", session)), [])
  })
})

Deno.test("a Trial without attach_view gets no view and sends no attach request", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, "")
    assert(!("view" in await step(sandbox, session, { cmd: "state" })))
    const stopped = await sandbox.cli(["stop", session])
    const { transcript } = jsonOut<{ transcript: string }>(stopped)
    const cmds = (await readTranscript(transcript)).flatMap((r) => r.request ? [r.request.cmd] : [])
    assert(!cmds.includes("attach_view"), cmds.join(","))
  })
})

Deno.test("a radius the game refuses fails the boot and names the field", async () => {
  await withSandbox(async (sandbox) => {
    // The Trial accepts any whole number from 1; the game's own limit is 10.
    const refused = await sandbox.cli([
      "start",
      await sandbox.trial(`fixture = "bairdford"\nattach_view = 11\n`),
    ])
    assertEquals(refused.code, 2)
    assert(refused.stderr.includes("attach_view"), refused.stderr)
    assertEquals(await pidsMatching(join(sandbox.home, "episodes")), [])
  })
})

Deno.test("a Trial with attach_view of 0 or a fraction is a usage error before any game starts", async () => {
  await withSandbox(async (sandbox) => {
    for (const value of ["0", "2.5"]) {
      const res = await sandbox.cli([
        "start",
        await sandbox.trial(`fixture = "bairdford"\nattach_view = ${value}\n`),
      ])
      assert(res.code !== 0)
      assert(res.stderr.includes("attach_view"), res.stderr)
    }
    assertEquals(await pidsMatching(join(sandbox.home, "episodes")), [])
  })
})
