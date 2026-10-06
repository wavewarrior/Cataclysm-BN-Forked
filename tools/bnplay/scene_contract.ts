/**
 * Scene contract: `run_scene <name>` runs a named Lua Scene (a script that builds a situation in
 * the world) at the turn the agent chooses and answers with a structured `scene` object. Shared
 * by every driver implementation and, like the other contracts, it asserts external behaviour
 * only. Every request carries the client's timeout, so a hang is a failing test, and the session
 * confirms no game process is left behind.
 *
 * What the steps pin down:
 *   run_scene  `{name}` is the file `<name>.lua` in the driver's Scenes directory. The answer is
 *              an ordinary observation plus `scene: {status, lines}`: `status` is `passed` when
 *              the Scene ran to its end and did not return `false`, `failed` otherwise; `lines`
 *              is what the Scene logged (`gdebug.log_info` and `print`), in order, and for a
 *              Scene that raised an error its last line is that error after `error: `. The
 *              lines belong to that run alone. It spends no time, and a failing Scene is a
 *              normal answer (`status: ok`, `scene.status: failed`): the driver carries on
 *   errors     a missing or malformed `name` (anything but letters, digits, `_` and `-`) and a
 *              name with no Scene file are protocol errors, and so is asking while a menu is
 *              open
 *
 * The in-process driver tests in `tests/driver_loop_test.cpp` cover what the fixture cannot
 * reach: a long result being cut, and the lighting Scenes' effect on the map.
 */
import { delay } from "@std/async"
import { assert, assertEquals } from "@std/assert"
import { join } from "@std/path"
import type { Driver, DriverResponse } from "./client.ts"
import { type ContractTarget, groupAlive } from "./contract.ts"

export type SceneContractOptions = {
  /**
   * The repository's Scene library (`tools/visual_verify/scenes`). When given, the lighting
   * Scenes in it are run as they are, on a fresh game of their own, and each must pass and
   * report its `*_RESULT` line.
   */
  scenesLibrary?: string
}

/** Written to the Scenes directory the contract starts its driver with. */
const SCENES: Record<string, string> = {
  contract_pass:
    'gdebug.log_info("CONTRACT_RESULT first")\nprint("CONTRACT_RESULT second")\nreturn true',
  contract_silent: "return true",
  contract_raises: 'gdebug.log_info("CONTRACT_RESULT before")\nerror("contract scene raised")',
  contract_false: 'gdebug.log_info("CONTRACT_RESULT checked")\nreturn false',
}

/** The lighting Scenes of the library, each with the line it reports its work in. */
const LIGHTING_SCENES: [string, string][] = [
  ["lightone", "LIGHTONE_RESULT"],
  ["lightmobs", "LIGHTMOBS_RESULT"],
  ["lightscene", "LIGHTSCENE_RESULT"],
  ["shadowtest", "SHADOWTEST_RESULT"],
]

type Scene = { status: string; lines: string[] }

function sceneOf(res: DriverResponse): Scene {
  assertEquals(res.status, "ok", JSON.stringify(res))
  const scene = res.scene as Scene | undefined
  assert(scene !== undefined && typeof scene === "object", `no scene: ${JSON.stringify(res)}`)
  assert(Array.isArray(scene.lines), `lines: ${JSON.stringify(scene)}`)
  return scene
}

/** Registers the Scene contract under `name`. One game boot serves the whole session. */
export function runSceneContract(
  name: string,
  target: ContractTarget,
  options: SceneContractOptions = {},
): void {
  Deno.test({
    name: `driver scene contract: ${name}`,
    sanitizeOps: false,
    sanitizeResources: false,
    async fn(t) {
      const dir = await Deno.makeTempDir({ prefix: "bnplay-scenes-" })
      for (const [scene, source] of Object.entries(SCENES)) {
        await Deno.writeTextFile(join(dir, `${scene}.lua`), source)
      }
      const driver = await target.spawn({ scenesDir: dir })
      const turnOf = async (d: Driver) => (await d.send({ cmd: "state" })).turn as number
      try {
        assertEquals((await driver.send({ cmd: "ping" }, target.bootTimeoutMs)).status, "ok")

        await t.step("a passing Scene reports passed and what it logged, in no time", async () => {
          const before = await turnOf(driver)
          const res = await driver.send({ cmd: "run_scene", name: "contract_pass" })
          const scene = sceneOf(res)
          assertEquals(scene.status, "passed")
          assertEquals(scene.lines, ["CONTRACT_RESULT first", "CONTRACT_RESULT second"])
          assertEquals(res.outcome, "completed")
          assertEquals(res.time_passed, false)
          assertEquals(res.turn, before)
          assertEquals(await turnOf(driver), before)
          // The result is its own member, not folded into the messages.
          assert(!JSON.stringify(res.new_messages).includes("CONTRACT_RESULT"))
          assert(!("scene" in await driver.send({ cmd: "state" })), "state carries a scene")
        })

        await t.step(
          "a second run reports its own lines only, and a quiet Scene none",
          async () => {
            assertEquals(
              sceneOf(await driver.send({ cmd: "run_scene", name: "contract_pass" })).lines,
              [
                "CONTRACT_RESULT first",
                "CONTRACT_RESULT second",
              ],
            )
            const silent = sceneOf(await driver.send({ cmd: "run_scene", name: "contract_silent" }))
            assertEquals(silent, { status: "passed", lines: [] })
          },
        )

        await t.step("a Scene that raises an error reports failed with its lines", async () => {
          const scene = sceneOf(await driver.send({ cmd: "run_scene", name: "contract_raises" }))
          assertEquals(scene.status, "failed")
          assertEquals(scene.lines[0], "CONTRACT_RESULT before")
          const last = scene.lines.at(-1)!
          assert(last.startsWith("error: ") && last.includes("contract scene raised"), last)
        })

        await t.step("a Scene that returns false reports failed", async () => {
          const scene = sceneOf(await driver.send({ cmd: "run_scene", name: "contract_false" }))
          assertEquals(scene, { status: "failed", lines: ["CONTRACT_RESULT checked"] })
        })

        await t.step("the driver carries on after a failing Scene", async () => {
          const res = await driver.send({ cmd: "wait", turns: 1 })
          assertEquals(res.status, "ok")
          assertEquals(res.outcome, "completed")
          assertEquals(
            sceneOf(await driver.send({ cmd: "run_scene", name: "contract_pass" })).status,
            "passed",
          )
        })

        await t.step("a missing, malformed or unknown name is a protocol error", async () => {
          const before = await turnOf(driver)
          const requests: Record<string, unknown>[] = [
            { cmd: "run_scene" },
            { cmd: "run_scene", name: null },
            { cmd: "run_scene", name: 3 },
            { cmd: "run_scene", name: "" },
            { cmd: "run_scene", name: "no_such_scene" },
            { cmd: "run_scene", name: "../contract_pass" },
            { cmd: "run_scene", name: "contract_pass.lua" },
            { cmd: "run_scene", name: "sub/contract_pass" },
          ]
          for (const req of requests) {
            const res = await driver.send(req as { cmd: string })
            assertEquals(res.status, "error", JSON.stringify(req))
            assertEquals(typeof res.error, "string", JSON.stringify(req))
            assert(!("scene" in res), JSON.stringify(req))
          }
          assertEquals(await turnOf(driver), before)
        })

        await t.step("a menu that is open refuses a Scene, and stays open", async () => {
          const opened = await driver.send({ cmd: "action", name: "look" })
          assertEquals(opened.outcome, "awaiting_input")
          const res = await driver.send({ cmd: "run_scene", name: "contract_pass" })
          assertEquals(res.status, "error")
          assert(String(res.error).includes("menu"), String(res.error))
          assertEquals((await driver.send({ cmd: "state" })).outcome, "awaiting_input")
          assertEquals((await driver.send({ cmd: "key", key: "ESC" })).outcome, "completed")
        })

        await t.step("the protocol channel carries only protocol lines", () => {
          assertEquals(driver.noise, [])
        })

        await t.step("quit exits cleanly and leaves no game process", async () => {
          assertEquals((await driver.send({ cmd: "quit" })).status, "ok")
          const exit = await Promise.race([driver.exited, delay(10_000).then(() => "timeout")])
          assertEquals(exit, 0)
        })
      } finally {
        await driver.close()
        await Deno.remove(dir, { recursive: true })
      }
      assert(!(await groupAlive(driver.pgid)), "process group left running")
    },
  })

  const library = options.scenesLibrary
  if (library === undefined) return
  Deno.test({
    name: `driver scene contract, lighting Scenes unchanged: ${name}`,
    sanitizeOps: false,
    sanitizeResources: false,
    async fn() {
      const driver = await target.spawn({ scenesDir: library })
      try {
        assertEquals((await driver.send({ cmd: "ping" }, target.bootTimeoutMs)).status, "ok")
        for (const [scene, tag] of LIGHTING_SCENES) {
          const res = await driver.send({ cmd: "run_scene", name: scene })
          const report = sceneOf(res)
          assertEquals(report.status, "passed", `${scene}: ${JSON.stringify(res)}`)
          assert(
            report.lines.some((line) => line.includes(tag)),
            `${scene}: ${JSON.stringify(report)}`,
          )
          assertEquals(res.time_passed, false, scene)
        }
        assertEquals((await driver.send({ cmd: "state" })).status, "ok")
        assertEquals((await driver.send({ cmd: "quit" })).status, "ok")
        assertEquals(await Promise.race([driver.exited, delay(10_000).then(() => "timeout")]), 0)
      } finally {
        await driver.close()
      }
      assert(!(await groupAlive(driver.pgid)), "process group left running")
    },
  })
}
