/**
 * View contract: the `view` command (an ASCII terrain grid with a legend, and a separate list of
 * creatures and items with offsets and ids) and the `attach_view` option that adds the same view
 * to every observation. Shared by every driver implementation and, like the other contracts, it
 * asserts external behaviour only. Every request carries the client's timeout, so a hang is a
 * failing test, and the session confirms no game process is left behind.
 *
 * What the steps pin down:
 *   view         a window of `radius` tiles around the avatar (default 5, at most 10): `grid` is
 *                2 * radius + 1 rows of as many characters, north at the top, the avatar `@` in
 *                the middle, and every character of it is a key of `legend`, which says what it
 *                stands for. `creatures` and `items` list what is in view, nearest first, each
 *                with `dx`, `dy` (east and south positive) from the avatar, a `name` and an `id`
 *                (an item's id is the one `query inventory` and `here` report; a creature's is
 *                its type). A list the driver cut says so in a top-level `truncated: true`.
 *                It spends no time, and works while a menu is open as well as when none is
 *   attach_view  `{radius: N}` adds the same view, as `view`, to every later observation, a
 *                `query` answer included; `{radius: 0}` stops. A radius that is not a whole
 *                number from 0 to 10 is a protocol error. With the view attached, every
 *                response still stays within the size ceiling: a view that would not fit beside
 *                the rest of the response looks at a smaller radius (the view's own `radius`
 *                says how far), and a response that cut anything says `truncated: true`
 *
 * The in-process driver tests in `tests/driver_view_test.cpp` cover what the Bairdford fixture
 * cannot reach: terrain classes, creatures and items in view, and lists long enough to be cut.
 */
import { delay } from "@std/async"
import { assert, assertEquals } from "@std/assert"
import type { Driver, DriverResponse } from "./client.ts"
import { type ContractTarget, groupAlive } from "./contract.ts"

/** Response ceiling: about 1.5K tokens of compact JSON, taken at 4 bytes a token. */
const RESPONSE_CEILING_BYTES = 6000

const MAX_RADIUS = 10

type Legend = Record<string, string>
type Located = { id: string; name: string; dx: number; dy: number }

/** Asserts `view` (a `view` response, or the `view` member of an observation) is well formed. */
function assertView(view: Record<string, unknown>, radius: number, what: string) {
  assertEquals(view.radius, radius, `${what}: radius`)
  const grid = view.grid as string[]
  assert(Array.isArray(grid), `${what}: grid is not an array`)
  assertEquals(grid.length, 2 * radius + 1, `${what}: grid rows`)
  for (const row of grid) {
    assertEquals(typeof row, "string", what)
    assertEquals(row.length, 2 * radius + 1, `${what}: a grid row has the wrong width`)
  }
  assertEquals(grid[radius][radius], "@", `${what}: the avatar is not in the middle`)

  const legend = view.legend as Legend
  assertEquals(typeof legend, "object", `${what}: legend`)
  for (const [symbol, meaning] of Object.entries(legend)) {
    assertEquals(symbol.length, 1, `${what}: legend key ${JSON.stringify(symbol)}`)
    assertEquals(typeof meaning, "string", what)
    assert(meaning.length > 0, `${what}: legend ${symbol} has no meaning`)
  }
  for (const row of grid) {
    for (const symbol of row) {
      assert(
        symbol in legend,
        `${what}: ${JSON.stringify(symbol)} is on the grid, not in the legend`,
      )
    }
  }

  for (const list of ["creatures", "items"] as const) {
    const entries = view[list] as (Located & Record<string, unknown>)[]
    assert(Array.isArray(entries), `${what}: ${list} is not an array`)
    let farthest = 0
    for (const entry of entries) {
      assertEquals(typeof entry.id, "string", `${what}: ${list} id`)
      assert(entry.id.length > 0, `${what}: ${list} entry without an id`)
      assertEquals(typeof entry.name, "string", `${what}: ${list} name`)
      assert(entry.name.length > 0, `${what}: ${list} entry without a name`)
      assert(Number.isInteger(entry.dx) && Number.isInteger(entry.dy), `${what}: ${list} offsets`)
      const distance = Math.max(Math.abs(entry.dx), Math.abs(entry.dy))
      assert(distance <= radius, `${what}: ${list} entry outside the window`)
      assert(distance >= farthest, `${what}: ${list} are not nearest first`)
      farthest = distance
    }
  }
  for (const item of view.items as Located[]) {
    assert(/^[0-9]+$/.test(item.id), `${what}: item id ${JSON.stringify(item.id)}`)
  }
  const ids = (view.items as Located[]).map((item) => item.id)
  assertEquals(new Set(ids).size, ids.length, `${what}: two listed items share an id`)
  for (const creature of view.creatures as { hostile?: unknown }[]) {
    assertEquals(typeof creature.hostile, "boolean", `${what}: creature hostile`)
  }
}

/** A cut list is announced by a top-level `truncated: true`, and by nothing else. */
function assertTruncatedFlag(res: DriverResponse, what: string) {
  if ("truncated" in res) assertEquals(res.truncated, true, `${what}: truncated`)
}

export type ViewContractOptions = {
  /**
   * The driver has item commands: check that the items a view lists on the avatar's own tile are
   * the ones `query inventory` lists as `here`, with the same ids and names.
   */
  inventoryIds?: boolean
}

/** Registers the view contract under `name`. One game boot serves the whole session. */
export function runViewContract(
  name: string,
  target: ContractTarget,
  options: ViewContractOptions = {},
): void {
  Deno.test({
    name: `driver view contract: ${name}`,
    sanitizeOps: false,
    sanitizeResources: false,
    async fn(t) {
      const driver = await target.spawn()
      // Every response of every step must stay within the size ceiling.
      const send = driver.send.bind(driver)
      driver.send = async (req, timeoutMs) => {
        const res = await send(req, timeoutMs)
        const bytes = new TextEncoder().encode(JSON.stringify(res)).length
        assert(bytes <= RESPONSE_CEILING_BYTES, `${req.cmd} response is ${bytes} bytes`)
        return res
      }
      const turnOf = async (d: Driver) => (await d.send({ cmd: "state" })).turn as number
      try {
        assertEquals((await driver.send({ cmd: "ping" }, target.bootTimeoutMs)).status, "ok")

        await t.step("view returns a grid, a legend and entity lists, in no time", async () => {
          const before = await turnOf(driver)
          const res = await driver.send({ cmd: "view" })
          assertEquals(res.status, "ok", JSON.stringify(res))
          assertEquals(res.outcome, "completed")
          assertEquals(res.time_passed, false)
          assertEquals(res.turn, before)
          assertView(res, 5, "view")
          assertTruncatedFlag(res, "view")
          assert(!("view" in res), "a view response does not carry itself again")
          assertEquals(await turnOf(driver), before)
        })

        await t.step("view asks for its radius, from 1 to 10", async () => {
          for (const radius of [1, 2, 7, MAX_RADIUS]) {
            const res = await driver.send({ cmd: "view", radius })
            assertEquals(res.status, "ok", `radius ${radius}: ${JSON.stringify(res)}`)
            assertView(res, radius, `radius ${radius}`)
            assertTruncatedFlag(res, `radius ${radius}`)
          }
        })

        await t.step(
          "a radius that is not a whole number from 1 to 10 is a protocol error",
          async () => {
            const before = await turnOf(driver)
            for (const radius of [0, -1, MAX_RADIUS + 1, 2.5, "3", null]) {
              const res = await driver.send({ cmd: "view", radius })
              assertEquals(res.status, "error", `radius ${JSON.stringify(radius)}`)
              assertEquals(typeof res.error, "string")
            }
            assertEquals(await turnOf(driver), before)
          },
        )

        await t.step("view works while a menu is open, and leaves the menu open", async () => {
          const before = await turnOf(driver)
          const opened = await driver.send({ cmd: "action", name: "look" })
          assertEquals(opened.outcome, "awaiting_input")
          const res = await driver.send({ cmd: "view", radius: 3 })
          assertEquals(res.status, "ok", JSON.stringify(res))
          assertView(res, 3, "view in a menu")
          assertEquals(res.outcome, "awaiting_input")
          assertEquals(res.prompt, "look")
          assertEquals(res.time_passed, false)
          assertEquals(res.turn, before)
          assertEquals((await driver.send({ cmd: "state" })).outcome, "awaiting_input")
          assertEquals((await driver.send({ cmd: "key", key: "ESC" })).outcome, "completed")
          assertEquals(await turnOf(driver), before)
        })

        await t.step("a view is not attached unless asked for", async () => {
          for (const req of [{ cmd: "state" }, { cmd: "wait", turns: 1 }]) {
            assert(!("view" in await driver.send(req)), `${req.cmd} carries a view by itself`)
          }
        })

        await t.step(
          "attach_view rejects a radius that is not a whole number from 0 to 10",
          async () => {
            const before = await turnOf(driver)
            for (const radius of [-1, MAX_RADIUS + 1, 2.5, "3", null, undefined]) {
              const res = await driver.send({ cmd: "attach_view", radius })
              assertEquals(res.status, "error", `radius ${JSON.stringify(radius)}`)
              assertEquals(typeof res.error, "string")
            }
            assertEquals(await turnOf(driver), before)
            assert(!("view" in await driver.send({ cmd: "state" })), "a refused attach took effect")
          },
        )

        await t.step("attach_view adds the view to every observation, in no time", async () => {
          const before = await turnOf(driver)
          const attached = await driver.send({ cmd: "attach_view", radius: 2 })
          assertEquals(attached.status, "ok", JSON.stringify(attached))
          assertEquals(await turnOf(driver), before, "attaching takes no time")

          const answers: [string, DriverResponse][] = [
            ["state", await driver.send({ cmd: "state" })],
            ["wait", await driver.send({ cmd: "wait", turns: 2 })],
            ["move", await driver.send({ cmd: "move", dir: "n" })],
            ["action", await driver.send({ cmd: "action", name: "pause" })],
          ]
          for (const [what, res] of answers) {
            assertEquals(res.status, "ok", `${what}: ${JSON.stringify(res)}`)
            assertView(res.view as Record<string, unknown>, 2, `attached to ${what}`)
            // The lean fields stay where they were.
            assertEquals(typeof res.turn, "number", what)
            assertEquals(typeof res.outcome, "string", what)
          }
          // A cut list inside the attached view is announced on the response too.
          for (const [what, res] of answers) {
            const view = res.view as { truncated?: unknown }
            if (view.truncated !== undefined) {
              assertEquals(view.truncated, true, what)
              assertEquals(res.truncated, true, `${what}: the response does not say it was cut`)
            }
          }
          // A view command asks for its own window, whatever is attached.
          const asked = await driver.send({ cmd: "view", radius: 4 })
          assertView(asked, 4, "view with a view attached")
          assert(!("view" in asked), "a view response carries a second view")
        })

        await t.step("the attached view is the same window a view command returns", async () => {
          const state = await driver.send({ cmd: "state" })
          const asked = await driver.send({ cmd: "view", radius: 2 })
          const attached = state.view as Record<string, unknown>
          for (const member of ["radius", "grid", "legend", "creatures", "items"]) {
            assertEquals(attached[member], asked[member], member)
          }
        })

        await t.step(
          "with the widest view attached every response stays within the ceiling",
          async () => {
            assertEquals(
              (await driver.send({ cmd: "attach_view", radius: MAX_RADIUS })).status,
              "ok",
            )
            // The size ceiling is asserted by the wrapper on every response.
            for (
              const req of [{ cmd: "state" }, { cmd: "wait", turns: 3 }, { cmd: "move", dir: "e" }]
            ) {
              const res = await driver.send(req)
              assertView(res.view as Record<string, unknown>, MAX_RADIUS, `widest ${req.cmd}`)
            }
          },
        )

        if (options.inventoryIds) {
          await t.step(
            "a query answer carries the view too, a smaller one if it must",
            async () => {
              for (const topic of ["inventory", "effects"]) {
                const res = await driver.send({ cmd: "query", topic })
                assertEquals(res.status, "ok", JSON.stringify(res))
                const view = res.view as Record<string, unknown>
                assert(view, `query ${topic} carries no view`)
                const radius = view.radius as number
                assert(radius >= 1 && radius <= MAX_RADIUS, `query ${topic}: radius ${radius}`)
                assertView(view, radius, `attached to query ${topic}`)
                // A window made smaller than asked for says so.
                if (radius < MAX_RADIUS) assertEquals(res.truncated, true, `query ${topic}`)
              }
            },
          )
        }

        await t.step("attach_view with radius 0 stops attaching", async () => {
          assertEquals((await driver.send({ cmd: "attach_view", radius: 0 })).status, "ok")
          assert(!("view" in await driver.send({ cmd: "state" })))
          assert(!("view" in await driver.send({ cmd: "wait", turns: 1 })))
        })

        if (options.inventoryIds) {
          await t.step(
            "items on the avatar's tile carry the ids query inventory reports",
            async () => {
              const inventory = await driver.send({ cmd: "query", topic: "inventory" })
              const carried = (inventory.items as { id: string }[])[0]
              assert(carried, "the fixture avatar carries nothing to drop")
              const dropped = await driver.send({ cmd: "drop", item: carried.id })
              assertEquals(dropped.outcome, "completed", JSON.stringify(dropped))

              const here = (await driver.send({ cmd: "query", topic: "inventory" }))
                .here as { id: string; name: string }[]
              assert(here.some((e) => e.id === carried.id), "the dropped item is not in `here`")
              const view = await driver.send({ cmd: "view", radius: 3 })
              assertTruncatedFlag(view, "view of the avatar's tile")
              const underfoot = (view.items as Located[]).filter((i) => i.dx === 0 && i.dy === 0)
              const byId = (a: { id: string }, b: { id: string }) => a.id.localeCompare(b.id)
              assertEquals(
                underfoot.map(({ id, name }) => ({ id, name })).sort(byId),
                here.map(({ id, name }) => ({ id, name })).sort(byId),
              )
            },
          )
        }

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
      }
      assert(!(await groupAlive(driver.pgid)), "process group left running")
    },
  })
}
