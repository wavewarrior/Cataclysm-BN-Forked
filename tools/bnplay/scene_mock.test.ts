/**
 * The Trial's `scene` option, end to end through the CLI against the mock driver: a Trial that
 * names a Scene runs it at the start and its structured result reaches the transcript and the
 * oracles; a Scene that fails is the Trial's result, not a crash; an unknown Scene refuses the
 * start. Only external behaviour is asserted.
 */
import { assert, assertEquals } from "@std/assert"
import { join } from "@std/path"
import {
  jsonOut,
  makeFakeWorld,
  makeSandbox,
  pidsMatching,
  readTranscript,
  type Sandbox,
} from "./testkit.ts"

type Report = {
  exit_code: number
  verdict: string
  transcript: string
  oracles: { name: string; result: string; first_fail?: { index: number; why: string } }[]
}

const SCENES: Record<string, string> = {
  builds_a_corridor: 'gdebug.log_info("CORRIDOR_RESULT walls=4")\nreturn true',
  cannot_build: 'gdebug.log_info("CANNOT_RESULT started")\nerror("no room for the corridor")',
}

async function withSandbox(body: (sandbox: Sandbox) => Promise<void>): Promise<void> {
  const world = await makeFakeWorld()
  const scenes = await Deno.makeTempDir({ prefix: "bnplay-scenes-" })
  for (const [name, source] of Object.entries(SCENES)) {
    await Deno.writeTextFile(join(scenes, `${name}.lua`), source)
  }
  const sandbox = await makeSandbox({
    fixtureSources: { bairdford: world },
    env: { BNPLAY_SCENES: scenes },
  })
  try {
    await body(sandbox)
  } finally {
    await sandbox.cleanup()
    await Deno.remove(world, { recursive: true })
    await Deno.remove(scenes, { recursive: true })
  }
}

async function start(sandbox: Sandbox, toml: string) {
  const res = await sandbox.cli(["start", await sandbox.trial(`fixture = "bairdford"\n${toml}`)])
  assertEquals(res.code, 0, res.stderr)
  return jsonOut<{ session: string }>(res).session
}

async function stop(sandbox: Sandbox, session: string) {
  const res = await sandbox.cli(["stop", session])
  return { code: res.code, report: jsonOut<Report>(res) }
}

Deno.test("a Trial's Scene runs after the seed and before the first state, and its result is in the transcript", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(
      sandbox,
      `seed = 5
       attach_view = 2
       scene = "builds_a_corridor"
       [[oracle]]
       name = "scene passed"
       field = "scene.status"
       operator = "eq"
       value = "passed"
       [[oracle]]
       name = "scene reported"
       field = "scene.lines"
       operator = "contains"
       value = "CORRIDOR_RESULT"`,
    )
    const { code, report } = await stop(sandbox, session)

    const records = await readTranscript(report.transcript)
    const requests = records.flatMap((r) => r.request ? [r.request] : [])
    assertEquals(requests.slice(0, 5).map((r) => r.cmd), [
      "ping",
      "seed",
      "attach_view",
      "run_scene",
      "state",
    ])
    assertEquals(requests[3].name, "builds_a_corridor")
    const answer = records.find((r) => r.response && "scene" in r.response)!.response!
    assertEquals(answer.scene, { status: "passed", lines: ["CORRIDOR_RESULT walls=4"] })
    // The oracles read the structured result, so a Trial can predicate on it directly.
    assertEquals(report.oracles.filter((o) => o.name.startsWith("scene")).map((o) => o.result), [
      "pass",
      "pass",
    ])
    assertEquals(code, 0)
    assertEquals(await pidsMatching(join(sandbox.home, "episodes", session)), [])
  })
})

Deno.test("a Trial without a scene sends no run_scene", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, "")
    const { report } = await stop(sandbox, session)
    const records = await readTranscript(report.transcript)
    const cmds = records.flatMap((r) => r.request ? [r.request.cmd] : [])
    assert(!cmds.includes("run_scene"), cmds.join(","))
  })
})

Deno.test("a failing Scene starts the Episode, and an oracle on it fails the run", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(
      sandbox,
      `scene = "cannot_build"
       [[oracle]]
       name = "scene passed"
       field = "scene.status"
       operator = "eq"
       value = "passed"`,
    )
    // The game is up and answering: the Scene's failure did not take it down.
    const state = await sandbox.cli(["step", session, JSON.stringify({ cmd: "state" })])
    assertEquals(state.code, 0, state.stderr)
    assertEquals(jsonOut(state).status, "ok")

    const { code, report } = await stop(sandbox, session)
    assertEquals(code, 1)
    assertEquals(report.verdict, "fail")
    const failed = report.oracles.find((o) => o.name === "scene passed")!
    assertEquals(failed.result, "fail")
    assert(failed.first_fail?.why.includes("failed"), failed.first_fail?.why)
    const records = await readTranscript(report.transcript)
    const answer = records.find((r) => r.response && "scene" in r.response)!.response!
    const scene = answer.scene as { status: string; lines: string[] }
    assertEquals(scene.status, "failed")
    assertEquals(scene.lines[0], "CANNOT_RESULT started")
    assert(scene.lines.at(-1)!.includes("no room for the corridor"), scene.lines.join("|"))
    // The failed Scene is the one run at the start: the request the oracle names.
    assertEquals(
      records.find((r) => r.request?.id === failed.first_fail?.index)?.request?.cmd,
      "run_scene",
    )
  })
})

Deno.test("a Trial naming a Scene the game does not have is refused before any game stays up", async () => {
  await withSandbox(async (sandbox) => {
    const res = await sandbox.cli([
      "start",
      await sandbox.trial('fixture = "bairdford"\nscene = "no_such_scene"\n'),
    ])
    assertEquals(res.code, 2)
    assert(res.stderr.includes("no_such_scene"), res.stderr)
    assertEquals(await pidsMatching(join(sandbox.home, "episodes")), [])
  })
})
