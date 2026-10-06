/**
 * Combat contract: the typed `melee`, `fire` and `smash` commands, their targets (a compass
 * direction `dir`, or a tile offset `pos` = `[dx, dy]` from the avatar, east and south positive)
 * and their `outcome` mapping. Shared by every driver implementation that supports them and, like
 * the other contracts, it asserts external behaviour only. Every request carries the client's
 * timeout, so a hang is a failing test, and the session confirms no game process is left behind.
 *
 * Outcome mapping the steps pin down:
 *   completed    the command acted: an attack was made, a shot fired, something bashed. Time
 *                passed (`time_passed`), and `new_messages` carries whatever the game said
 *   refused      nothing to act on, or the game's rules said no (no gun, not loaded, an ally):
 *                nothing spent, `detail` says why
 *   unsupported  the game asked a question only a menu could answer (`reason: blocking_read`),
 *                such as smashing with an item that might shatter
 *   died         the avatar is dead (any command, any response): the Episode is over
 * A bad target (neither or both of `dir` and `pos`, an unknown direction, `pos` that is not two
 * whole numbers, the avatar's own tile, out of reach) and a bad `max_turns` are protocol errors.
 *
 * Fixture assumptions: the avatar wields a pocket knife (not a gun, nothing that shatters) and is
 * enclosed by vehicle walls, with no creature on any adjacent tile. What the fixture cannot reach
 * (an attack that lands or kills, a shot that is fired, a smash that is refused or needs a menu
 * answer, the avatar's death) is covered by the in-process driver tests in
 * `tests/driver_loop_test.cpp`, and the supervisor's handling of `died` by `died_mock.test.ts`.
 * The game's message log is not wired in those tests, so `new_messages` of a landed attack is
 * not asserted anywhere; the log itself is covered by the time contract.
 */
import { assert, assertEquals } from "@std/assert"
import type { Driver, DriverResponse } from "./client.ts"
import { type ContractTarget, groupAlive, RESPONSE_CEILING_BYTES } from "./contract.ts"

const COMMANDS = ["melee", "fire", "smash"] as const

/** Targets no command accepts. */
const BAD_TARGETS: Record<string, unknown>[] = [
  {},
  { dir: "sideways" },
  { dir: "up" },
  { dir: 3 },
  { dir: "e", pos: [1, 0] },
  { pos: [] },
  { pos: [1] },
  { pos: [1, 2, 3] },
  { pos: [1.5, 0] },
  { pos: "e" },
  { pos: [0, 0] },
  { pos: [0, "x"] },
]

/** Registers the combat contract under `name`. One game boot serves the whole session. */
export function runCombatContract(name: string, target: ContractTarget): void {
  Deno.test({
    name: `driver combat contract: ${name}`,
    sanitizeOps: false,
    sanitizeResources: false,
    async fn(t) {
      const driver = await target.spawn()
      const send = driver.send.bind(driver)
      driver.send = async (req, timeoutMs) => {
        const res = await send(req, timeoutMs)
        const bytes = new TextEncoder().encode(JSON.stringify(res)).length
        assert(bytes <= RESPONSE_CEILING_BYTES, `${req.cmd} response is ${bytes} bytes`)
        return res
      }
      const turnOf = async (d: Driver) => (await d.send({ cmd: "state" })).turn as number
      try {
        const ping = await driver.send({ cmd: "ping" }, target.bootTimeoutMs)
        assertEquals(ping.status, "ok")

        await t.step("a missing, malformed or unusable target is a protocol error", async () => {
          const turn = await turnOf(driver)
          for (const cmd of COMMANDS) {
            for (const bad of BAD_TARGETS) {
              const res = await driver.send({ cmd, ...bad })
              assertEquals(res.status, "error", `${cmd} ${JSON.stringify(bad)}`)
              assertEquals(typeof res.error, "string")
            }
          }
          assertEquals(await turnOf(driver), turn, "a rejected request spends no time")
        })

        await t.step("a target out of reach is a protocol error", async () => {
          const turn = await turnOf(driver)
          for (
            const req of [
              { cmd: "melee", pos: [2, 0] },
              { cmd: "melee", pos: [0, -2] },
              { cmd: "smash", pos: [1, 2] },
              { cmd: "fire", pos: [5000, 0] },
              { cmd: "fire", pos: [0, -5000] },
            ]
          ) {
            const res = await driver.send(req)
            assertEquals(res.status, "error", JSON.stringify(req))
          }
          assertEquals(await turnOf(driver), turn)
        })

        await t.step("max_turns must be a whole number of at least 1", async () => {
          const turn = await turnOf(driver)
          for (const cmd of COMMANDS) {
            for (const bad of [0, -1, 1.5, "3"]) {
              const res = await driver.send({ cmd, dir: "e", max_turns: bad })
              assertEquals(res.status, "error", `${cmd} max_turns ${JSON.stringify(bad)}`)
            }
          }
          assertEquals(await turnOf(driver), turn)
        })

        /** A refusal: nothing spent, the turn did not move, and a reason is given. */
        const assertRefused = (res: DriverResponse, turn: number, what: string) => {
          assertEquals(res.status, "ok", JSON.stringify(res))
          assertEquals(res.outcome, "refused", `${what}: ${JSON.stringify(res)}`)
          assertEquals(res.time_passed, false, what)
          assertEquals(res.turn, turn, what)
          assertEquals(typeof res.detail, "string", what)
          assert((res.detail as string).length > 0, `${what} gives no reason`)
        }

        await t.step("melee at a tile with no creature is refused and takes no time", async () => {
          const turn = await turnOf(driver)
          for (const aim of [{ dir: "e" }, { dir: "nw" }, { pos: [-1, 1] }]) {
            assertRefused(await driver.send({ cmd: "melee", ...aim }), turn, JSON.stringify(aim))
          }
        })

        await t.step("fire without a gun is refused by direction and by position", async () => {
          const turn = await turnOf(driver)
          for (const aim of [{ dir: "n" }, { pos: [3, 0] }, { pos: [0, -9] }]) {
            assertRefused(await driver.send({ cmd: "fire", ...aim }), turn, JSON.stringify(aim))
          }
        })

        await t.step("smash acts, spends time and moves nothing", async () => {
          await turnOf(driver)
          for (const aim of [{ dir: "n" }, { pos: [0, -1] }]) {
            const res = await driver.send({ cmd: "smash", ...aim })
            assertEquals(res.status, "ok", JSON.stringify(res))
            assertEquals(res.outcome, "completed", JSON.stringify(res))
            assertEquals(res.time_passed, true)
            assertEquals(res.moved, false)
            assertEquals(res.boundary, "turn_complete")
            assert(Array.isArray(res.new_messages))
          }
        })

        await t.step("a combat command while a menu is open is a protocol error", async () => {
          const menu = await driver.send({ cmd: "action", name: "inventory" })
          assertEquals(menu.outcome, "awaiting_input")
          for (const cmd of COMMANDS) {
            const res = await driver.send({ cmd, dir: "e" })
            assertEquals(res.status, "error", cmd)
          }
          const closed = await driver.send({ cmd: "key", key: "ESC" })
          assertEquals(closed.status, "ok")
          assertEquals(closed.prompt, null)
        })

        await t.step("quit exits cleanly and leaves no game process", async () => {
          const res = await driver.send({ cmd: "quit" })
          assertEquals(res.status, "ok")
          const exit = await Promise.race([
            driver.exited,
            new Promise<"timeout">((resolve) => setTimeout(() => resolve("timeout"), 10_000)),
          ])
          assertEquals(exit, 0)
        })
      } finally {
        await driver.close()
      }
      assert(!(await groupAlive(driver.pgid)), "process group left running")
    },
  })
}
