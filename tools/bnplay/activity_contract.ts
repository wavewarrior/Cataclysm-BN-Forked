/**
 * Activity contract: the typed `eat`/`drink`, `use`, `read`, `reload`, `craft` and `sleep`
 * commands, their `outcome` mapping and the multi-turn rules (`max_turns`, the per-request turn
 * cap). Shared by every driver implementation that supports them and, like the other contracts,
 * it asserts external behaviour only. Every request carries the client's timeout, so a hang is a
 * failing test, and the session confirms no game process is left behind.
 *
 * Outcome mapping the steps pin down:
 *   completed    the command did something, or an activity it started ended by itself
 *   refused      the game's rules said no (or the item cannot take the command): nothing spent,
 *                `detail` says why
 *   no_effect    the command ran and nothing the player could see changed
 *   unsupported  the game asked a question only a menu could answer (`reason: blocking_read`)
 *   interrupted  an activity was stopped: by the game (`reason` monster_in_view, pain, noise,
 *                other) or by the turn limit (`reason: turn_cap`); `turns` says how long it ran
 *
 * Fixture assumptions: the avatar wields a pocket knife, carries a smartphone (several uses, one
 * named `transform`), a lighter (its use asks which way to light, which no menu can answer here)
 * and a plastic bottle of clean water, and is not thirsty enough to drink it without the game
 * asking "drink anyway?". It knows no recipe that needs what it does not have, and nothing
 * hostile is near. What the fixture cannot reach (a craft that succeeds, a reload that loads, a
 * book that is read, an interruption by a monster, pain or noise) is covered by the in-process
 * driver tests in `tests/driver_loop_test.cpp` and `tests/driver_items_test.cpp`.
 */
import { assert, assertEquals, assertExists } from "@std/assert"
import type { Driver, DriverResponse } from "./client.ts"
import { type ContractTarget, groupAlive, RESPONSE_CEILING_BYTES } from "./contract.ts"

/** The per-request turn cap. */
const TURN_CAP = 1000

type Entry = { id: string; name: string }

async function inventory(driver: Driver): Promise<{ wielded: Entry; items: Entry[] }> {
  const res = await driver.send({ cmd: "query", topic: "inventory" })
  assertEquals(res.status, "ok", JSON.stringify(res))
  return { wielded: res.wielded as Entry, items: res.items as Entry[] }
}

/** The carried item whose name contains `text`. */
function named(items: Entry[], text: string): Entry {
  const found = items.find((e) => e.name.includes(text))
  assertExists(found, `the fixture avatar carries no ${text}`)
  return found
}

/** A refusal: nothing spent, the turn did not move, and a reason is given. */
function assertRefused(res: DriverResponse, before: number, what: string) {
  assertEquals(res.status, "ok", `${what}: ${JSON.stringify(res)}`)
  assertEquals(res.outcome, "refused", `${what}: ${JSON.stringify(res)}`)
  assertEquals(res.time_passed, false, what)
  assertEquals(res.turn, before, what)
  assert(typeof res.detail === "string" && res.detail.length > 0, `${what}: no reason given`)
}

/** Registers the activity contract under `name`. One game boot serves the whole session. */
export function runActivityContract(name: string, target: ContractTarget): void {
  Deno.test({
    name: `driver activity contract: ${name}`,
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
      const turnOf = async () => (await driver.send({ cmd: "state" })).turn as number
      try {
        assertEquals((await driver.send({ cmd: "ping" }, target.bootTimeoutMs)).status, "ok")
        const inv = await inventory(driver)
        const knife = inv.wielded
        const phone = named(inv.items, "smartphone")
        const lighter = named(inv.items, "lighter")
        const water = named(inv.items, "water")

        await t.step("eat and drink refuse what is not food, with the game's words", async () => {
          const turn = await turnOf()
          for (const cmd of ["eat", "drink"]) {
            for (const thing of [knife, phone]) {
              const res = await driver.send({ cmd, item: thing.id })
              assertRefused(res, turn, `${cmd} ${thing.name}`)
              assert(
                (res.new_messages as string[]).some((m) => m.includes(res.detail as string)),
                `${cmd}: the game's message is missing from new_messages`,
              )
            }
          }
        })

        await t.step(
          "a drink the game would ask about is refused, and `anyway` answers yes",
          async () => {
            const turn = await turnOf()
            const before = await driver.send({ cmd: "state" })
            const refused = await driver.send({ cmd: "drink", item: water.id })
            assertRefused(refused, turn, "drink while not thirsty")

            const drunk = await driver.send({ cmd: "drink", item: water.id, anyway: true })
            assertEquals(drunk.status, "ok", JSON.stringify(drunk))
            assertEquals(drunk.outcome, "completed", JSON.stringify(drunk))
            assertEquals(drunk.time_passed, true)
            assert((drunk.turn as number) > turn, "drinking took no game time")
            assert((drunk.thirst as number) < (before.thirst as number), "it quenched nothing")
          },
        )

        await t.step(
          "use refuses an item with several uses, naming them, and runs the one asked for",
          async () => {
            const turn = await turnOf()
            const ambiguous = await driver.send({ cmd: "use", item: phone.id })
            assertRefused(ambiguous, turn, "use without a method")
            assert((ambiguous.detail as string).includes("transform"), "the uses are not named")

            const wrong = await driver.send({ cmd: "use", item: phone.id, method: "no_such_use" })
            assertRefused(wrong, turn, "use with an unknown method")

            // Something the player sees happen is `completed`, even when it costs no time.
            const on = await driver.send({ cmd: "use", item: phone.id, method: "transform" })
            assertEquals(on.status, "ok", JSON.stringify(on))
            assertEquals(on.outcome, "completed", JSON.stringify(on))
            assert((on.new_messages as string[]).length > 0, "the use said nothing")
            // Put it back as it was.
            const off = await driver.send({ cmd: "use", item: phone.id, method: "transform" })
            assertEquals(off.outcome, "completed", JSON.stringify(off))
          },
        )

        await t.step(
          "a use that asks a question no menu can answer is unsupported, not a hang",
          async () => {
            const turn = await turnOf()
            const res = await driver.send({ cmd: "use", item: lighter.id })
            assertEquals(res.status, "ok", JSON.stringify(res))
            assertEquals(res.outcome, "unsupported", JSON.stringify(res))
            assertEquals(res.reason, "blocking_read")
            assertEquals(res.time_passed, false)
            assertEquals(res.turn, turn)
            // The driver is still serving and nothing is left open.
            const after = await driver.send({ cmd: "state" })
            assertEquals(after.prompt, null)
            assertEquals(after.outcome, "completed")
          },
        )

        await t.step("read and reload refuse what cannot be read or reloaded", async () => {
          const turn = await turnOf()
          for (const cmd of ["read", "reload"]) {
            for (const thing of [knife, phone]) {
              assertRefused(
                await driver.send({ cmd, item: thing.id }),
                turn,
                `${cmd} ${thing.name}`,
              )
            }
          }
        })

        await t.step("craft refuses a recipe the avatar cannot make, naming why", async () => {
          const turn = await turnOf()
          // Known to the game, unknown to the avatar.
          const unknown = await driver.send({ cmd: "craft", recipe: "carver_off" })
          assertRefused(unknown, turn, "craft an unlearned recipe")
          // Learnable by sight, but the avatar lacks what it takes.
          const missing = await driver.send({ cmd: "craft", recipe: "pointy_stick" })
          assertRefused(missing, turn, "craft without the components")
        })

        await t.step(
          "an unknown recipe or item id is a protocol error that acts on nothing",
          async () => {
            const turn = await turnOf()
            const bad: [string, Record<string, unknown>][] = [
              ["craft", {}],
              ["craft", { recipe: "" }],
              ["craft", { recipe: 3 }],
              ["craft", { recipe: "no_such_recipe" }],
              ["eat", {}],
              ["eat", { item: "999999999999" }],
              ["drink", { item: "abc" }],
              ["use", { item: "0" }],
              ["read", { item: "-1" }],
              ["reload", { item: "" }],
            ]
            for (const [cmd, args] of bad) {
              const res = await driver.send({ cmd, ...args })
              assertEquals(res.status, "error", `${cmd} ${JSON.stringify(args)}`)
              assertEquals(typeof res.error, "string")
            }
            assertEquals(await turnOf(), turn)
          },
        )

        await t.step("max_turns that is not a whole number of at least 1 is an error", async () => {
          const turn = await turnOf()
          for (const max_turns of [0, -4, 1.5, "3", null]) {
            for (
              const req of [
                { cmd: "sleep" },
                { cmd: "craft", recipe: "pointy_stick" },
                { cmd: "read", item: knife.id },
                { cmd: "eat", item: water.id },
              ]
            ) {
              const res = await driver.send({ ...req, max_turns })
              assertEquals(res.status, "error", `${req.cmd} max_turns ${JSON.stringify(max_turns)}`)
            }
          }
          assertEquals(await turnOf(), turn)
        })

        await t.step("max_turns stops a sleep early and reports its progress", async () => {
          const before = await turnOf()
          const res = await driver.send({ cmd: "sleep", max_turns: 5 })
          assertEquals(res.status, "ok", JSON.stringify(res))
          assertEquals(res.outcome, "interrupted", JSON.stringify(res))
          assertEquals(res.reason, "turn_cap")
          assertEquals(res.turns, 5)
          assertEquals(res.time_passed, true)
          // The first turn of a loaded world may complete a turn already under way.
          const spent = (res.turn as number) - before
          assert(spent === 4 || spent === 5, `5 turns ran but the turn counter moved ${spent}`)
          assertEquals(typeof res.progress, "string")
          assert(
            (res.new_messages as string[]).some((m) => m.includes("trying to fall asleep")),
            "the sleep attempt is not in new_messages",
          )

          // Nothing is left running: the avatar is awake and one more turn is one turn.
          const effects = await driver.send({ cmd: "query", topic: "effects" })
          const ids = (effects.effects as { id: string }[]).map((e) => e.id)
          assert(!ids.includes("sleep"), "the avatar is still asleep")
          const turn = await turnOf()
          const waited = await driver.send({ cmd: "wait", turns: 1 })
          assertEquals(waited.outcome, "completed", JSON.stringify(waited))
          assertEquals(waited.turn, turn + 1)
        })

        await t.step("a sleep that ends at its limit leaves nothing behind", async () => {
          // Nothing is left running, so a second sleep starts afresh instead of being refused.
          const first = await driver.send({ cmd: "sleep", max_turns: 2 })
          assertEquals(first.outcome, "interrupted", JSON.stringify(first))
          const second = await driver.send({ cmd: "sleep", max_turns: 2 })
          assertEquals(second.outcome, "interrupted", JSON.stringify(second))
          assertEquals(second.turns, 2)
        })

        await t.step("an activity with no limit is cut at the per-request turn cap", async () => {
          const before = await turnOf()
          const res = await driver.send({ cmd: "sleep" }, 180_000)
          assertEquals(res.status, "ok", JSON.stringify(res))
          assertEquals(res.outcome, "interrupted", JSON.stringify(res))
          assertEquals(res.reason, "turn_cap")
          assertEquals(res.turns, TURN_CAP)
          const spent = (res.turn as number) - before
          assert(spent >= TURN_CAP - 1 && spent <= TURN_CAP, `cap run moved the turn by ${spent}`)
        })

        await t.step("a max_turns beyond the cap is cut at the cap", async () => {
          const res = await driver.send({ cmd: "sleep", max_turns: 5000 }, 180_000)
          assertEquals(res.outcome, "interrupted", JSON.stringify(res))
          assertEquals(res.reason, "turn_cap")
          assertEquals(res.turns, TURN_CAP)
        })

        await t.step("the protocol channel carries only protocol lines", () => {
          assertEquals(driver.noise, [])
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
