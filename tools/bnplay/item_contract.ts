/**
 * Item contract: stable item ids, `query inventory` / `query effects`, and the typed `pickup`,
 * `drop`, `wield`, `wear` and `take_off` commands with their outcome mapping. Shared by every
 * driver implementation and, like the other contracts, it asserts external behaviour only: what
 * a response contains, never the engine's internals. Every request carries the client's timeout,
 * so a hang is a failing test, and the session confirms no game process is left behind.
 *
 * Fixture assumption: the avatar wields something, wears more than one item, carries at least
 * one item that is not armour, and stands on a tile that can hold dropped items. The fixture
 * has nothing too heavy to pick up, so the "too heavy" refusal is not exercised here; refusals
 * are exercised through items the game will not wear and items that are not where the command
 * needs them.
 */
import { assert, assertEquals, assertExists, assertNotEquals } from "@std/assert"
import type { Driver, DriverResponse } from "./client.ts"
import { type ContractTarget, groupAlive, RESPONSE_CEILING_BYTES } from "./contract.ts"

/** One listed item: an id that is a string of digits, and the name the game shows for it. */
type Entry = { id: string; name: string }

type Inventory = {
  wielded: Entry | null
  worn: Entry[]
  items: Entry[]
  here: Entry[]
  res: DriverResponse
}

const ITEM_COMMANDS = ["pickup", "drop", "wield", "wear", "take_off"] as const

async function inventory(driver: Driver): Promise<Inventory> {
  const res = await driver.send({ cmd: "query", topic: "inventory" })
  assertEquals(res.status, "ok", JSON.stringify(res))
  return {
    wielded: res.wielded as Entry | null,
    worn: res.worn as Entry[],
    items: res.items as Entry[],
    here: res.here as Entry[],
    res,
  }
}

const idsOf = (inv: Inventory) => [
  ...(inv.wielded ? [inv.wielded.id] : []),
  ...inv.worn.map((e) => e.id),
  ...inv.items.map((e) => e.id),
  ...inv.here.map((e) => e.id),
]

const has = (entries: Entry[], id: string) => entries.some((e) => e.id === id)

/**
 * What a refusal must look like: nothing was spent, and a reason is given. `inLog` says whether
 * the reason is the game's own message, which the game logs, or the driver's explanation of a
 * command that cannot apply (an item that is not where the command needs it), which it does not.
 */
function assertRefused(res: DriverResponse, before: number, what: string, inLog: boolean) {
  assertEquals(res.status, "ok", `${what}: ${JSON.stringify(res)}`)
  assertEquals(res.outcome, "refused", what)
  assertEquals(res.time_passed, false, what)
  assertEquals(res.turn, before, what)
  assertEquals(typeof res.detail, "string", what)
  assert((res.detail as string).length > 0, `${what}: the refusal carries no message`)
  if (inLog) {
    assert(
      (res.new_messages as string[]).some((m) => m.includes(res.detail as string)),
      `${what}: the game's message is missing from new_messages`,
    )
  }
}

/** Registers the item contract under `name`. One game boot serves the whole session. */
export function runItemContract(name: string, target: ContractTarget): void {
  Deno.test({
    name: `driver item contract: ${name}`,
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

        await t.step(
          "query inventory lists what is carried with stable ids, in no time",
          async () => {
            const before = await turnOf()
            const inv = await inventory(driver)
            assertEquals(inv.res.topic, "inventory")
            assertEquals(inv.res.outcome, "completed")
            assertEquals(inv.res.time_passed, false)
            assertEquals(inv.res.turn, before)
            assert(inv.worn.length > 0, "the fixture avatar wears nothing")
            assert(inv.wielded !== null, "the fixture avatar wields nothing")
            assert(inv.items.length > 0, "the fixture avatar carries nothing")
            const ids = idsOf(inv)
            for (const entry of [...inv.worn, ...inv.items, ...inv.here, inv.wielded!]) {
              assert(
                /^[0-9]+$/.test(entry.id),
                `id ${JSON.stringify(entry.id)} is not a digit string`,
              )
              assert(entry.name.length > 0)
            }
            assertEquals(new Set(ids).size, ids.length, "two listed items share an id")
            // Asking again names the same items with the same ids.
            assertEquals(idsOf(await inventory(driver)), ids)
            assertEquals(await turnOf(), before)
          },
        )

        await t.step("query effects lists active effects, in no time", async () => {
          const before = await turnOf()
          const res = await driver.send({ cmd: "query", topic: "effects" })
          assertEquals(res.status, "ok")
          assertEquals(res.topic, "effects")
          assertEquals(res.outcome, "completed")
          assertEquals(res.time_passed, false)
          assertEquals(res.turn, before)
          assert(Array.isArray(res.effects))
          for (const effect of res.effects as Record<string, unknown>[]) {
            assertEquals(typeof effect.id, "string")
            assertEquals(typeof effect.intensity, "number")
          }
        })

        await t.step("query rejects a missing or unknown topic as a protocol error", async () => {
          const before = await turnOf()
          for (const bad of [{}, { topic: "overmap" }, { topic: 3 }]) {
            const res = await driver.send({ cmd: "query", ...bad })
            assertEquals(res.status, "error", JSON.stringify(bad))
            assertEquals(typeof res.error, "string")
          }
          assertEquals(await turnOf(), before)
        })

        await t.step("take_off then wear moves an item between worn and carried", async () => {
          const inv = await inventory(driver)
          const piece = inv.worn[inv.worn.length - 1]
          const turn = await turnOf()
          const off = await driver.send({ cmd: "take_off", item: piece.id })
          assertEquals(off.status, "ok", JSON.stringify(off))
          assertEquals(off.outcome, "completed")
          assertEquals(off.time_passed, true)
          assert((off.turn as number) >= turn)
          const bare = await inventory(driver)
          assert(!has(bare.worn, piece.id), "the item is still listed as worn")
          assert(has(bare.items, piece.id), "the item is not listed as carried")

          const on = await driver.send({ cmd: "wear", item: piece.id })
          assertEquals(on.status, "ok", JSON.stringify(on))
          assertEquals(on.outcome, "completed")
          assertEquals(on.time_passed, true)
          const dressed = await inventory(driver)
          assert(has(dressed.worn, piece.id), "the item is not listed as worn again")
          assert(!has(dressed.items, piece.id), "the item is still listed as carried")
        })

        await t.step("drop then pickup moves an item between carried and the ground", async () => {
          const inv = await inventory(driver)
          const thing = inv.items[0]
          const dropped = await driver.send({ cmd: "drop", item: thing.id })
          assertEquals(dropped.status, "ok", JSON.stringify(dropped))
          assertEquals(dropped.outcome, "completed")
          assertEquals(dropped.time_passed, true)
          const floor = await inventory(driver)
          assert(!has(floor.items, thing.id), "the dropped item is still listed as carried")
          assert(has(floor.here, thing.id), "the dropped item is not listed on the avatar's tile")

          // The ground cannot be dropped from, and nothing is spent finding that out.
          const turn = await turnOf()
          assertRefused(
            await driver.send({ cmd: "drop", item: thing.id }),
            turn,
            "drop from the ground",
            false,
          )
          assertRefused(
            await driver.send({ cmd: "wield", item: thing.id }),
            turn,
            "wield from the ground",
            false,
          )

          const up = await driver.send({ cmd: "pickup", item: thing.id })
          assertEquals(up.status, "ok", JSON.stringify(up))
          assertEquals(up.outcome, "completed")
          assertEquals(up.time_passed, true)
          const back = await inventory(driver)
          assert(has(back.items, thing.id), "the picked-up item is not listed as carried")
          assert(!has(back.here, thing.id), "the picked-up item is still listed on the ground")
        })

        await t.step(
          "an agent can take an item off, drop it, pick it up and wear it again",
          async () => {
            const piece = (await inventory(driver)).worn[0]
            const expectIn = async (place: "worn" | "items" | "here", why: string) => {
              const inv = await inventory(driver)
              assert(has(inv[place], piece.id), why)
              for (const other of ["worn", "items", "here"] as const) {
                if (other !== place) assert(!has(inv[other], piece.id), `${why}: also in ${other}`)
              }
            }
            const command = async (cmd: string) => {
              const res = await driver.send({ cmd, item: piece.id })
              assertEquals(res.outcome, "completed", `${cmd}: ${JSON.stringify(res)}`)
              assertEquals(res.time_passed, true, cmd)
            }
            await expectIn("worn", "worn at the start")
            await command("take_off")
            await expectIn("items", "carried after take_off")
            await command("drop")
            await expectIn("here", "on the ground after drop")
            await command("pickup")
            await expectIn("items", "carried after pickup")
            await command("wear")
            await expectIn("worn", "worn again after wear")
          },
        )

        await t.step("wield swaps the weapon, and wielding it again is no_effect", async () => {
          const inv = await inventory(driver)
          const old = inv.wielded!
          const next = inv.items[0]
          const res = await driver.send({ cmd: "wield", item: next.id })
          assertEquals(res.status, "ok", JSON.stringify(res))
          assertEquals(res.outcome, "completed", JSON.stringify(res))
          assertEquals(res.time_passed, true)
          const swapped = await inventory(driver)
          assertEquals(swapped.wielded?.id, next.id)
          assert(has(swapped.items, old.id), "the old weapon is not listed as carried")

          const turn = await turnOf()
          const again = await driver.send({ cmd: "wield", item: next.id })
          assertEquals(again.status, "ok")
          assertEquals(again.outcome, "no_effect")
          assertEquals(again.time_passed, false)
          assertEquals(again.turn, turn)

          assertEquals((await driver.send({ cmd: "wield", item: old.id })).outcome, "completed")
          assertEquals((await inventory(driver)).wielded?.id, old.id)
        })

        await t.step(
          "a valid id the game rejects is refused with the game's message",
          async () => {
            // Something carried that the game will not let the avatar wear.
            const inv = await inventory(driver)
            let refused: DriverResponse | undefined
            for (const entry of inv.items) {
              const turn = await turnOf()
              const res = await driver.send({ cmd: "wear", item: entry.id })
              assertEquals(res.status, "ok", JSON.stringify(res))
              if (res.outcome === "refused") {
                assertRefused(res, turn, `wear ${entry.name}`, true)
                refused = res
                break
              }
              // It was wearable after all: take it off again and try the next one.
              assertEquals(res.outcome, "completed")
              assertEquals(
                (await driver.send({ cmd: "take_off", item: entry.id })).outcome,
                "completed",
              )
            }
            assertExists(refused, "every carried item could be worn")

            // Commands whose item is not where the command needs it are refused the same way.
            const now = await inventory(driver)
            const carried = now.items[0]
            let turn = await turnOf()
            assertRefused(
              await driver.send({ cmd: "take_off", item: carried.id }),
              turn,
              "take_off unworn",
              true,
            )
            turn = await turnOf()
            assertRefused(
              await driver.send({ cmd: "pickup", item: carried.id }),
              turn,
              "pickup carried",
              false,
            )
            const worn = now.worn[0]
            turn = await turnOf()
            assertRefused(
              await driver.send({ cmd: "wear", item: worn.id }),
              turn,
              "wear worn",
              true,
            )
          },
        )

        await t.step(
          "a stale or invented id is a protocol error and never acts on another item",
          async () => {
            const before = await inventory(driver)
            const turn = await turnOf()
            const real = BigInt(before.items[0].id)
            const bad: unknown[] = [
              String(real + (1n << 40n)), // well-formed, never issued
              "0",
              "abc",
              "",
              "-1",
              before.items[0].id + "x",
              7, // ids travel as strings
              null,
            ]
            for (const command of ITEM_COMMANDS) {
              for (const item of bad) {
                const res = await driver.send({ cmd: command, item })
                assertEquals(res.status, "error", `${command} ${JSON.stringify(item)}`)
                assertEquals(typeof res.error, "string")
              }
              const missing = await driver.send({ cmd: command })
              assertEquals(missing.status, "error", `${command} without an item`)
            }
            // Nothing moved: same turn, same items in the same places.
            assertEquals(await turnOf(), turn)
            const after = await inventory(driver)
            assertEquals(after.wielded, before.wielded)
            assertEquals(after.worn, before.worn)
            assertEquals(after.items, before.items)
            assertEquals(after.here, before.here)
            assertNotEquals(idsOf(after).length, 0)
          },
        )

        await t.step("the protocol channel carries only protocol lines", () => {
          assertEquals(driver.noise, [])
        })

        await t.step("raw drop and wear stay denied", async () => {
          for (const action of ["drop", "wear"]) {
            const res = await driver.send({ cmd: "action", name: action }, 2_000)
            assertEquals(res.outcome, "unsupported", action)
            assertEquals(res.reason, "deny_list", action)
          }
        })

        await t.step("quit exits cleanly and leaves no game process", async () => {
          assertEquals((await driver.send({ cmd: "quit" })).status, "ok")
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
