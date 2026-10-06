/**
 * Time, movement and outcome contract: `wait`, `move`, `new_messages`, `seed` and the response
 * size ceiling. Shared by every driver implementation that supports those commands, and, like
 * the base contract, it asserts external behaviour only. Never exact RNG values or world state.
 *
 * Fixture assumption: the avatar starts where every compass step is blocked and `move up` is
 * refused with a message (not on an up staircase).
 */
import { assert, assertEquals, assertExists } from "@std/assert"
import type { DriverResponse } from "./client.ts"
import { type ContractTarget, groupAlive, RESPONSE_CEILING_BYTES } from "./contract.ts"

const DIRECTIONS = ["n", "ne", "e", "se", "s", "sw", "w", "nw", "up"] as const
const OPPOSITE: Record<string, string> = {
  n: "s",
  ne: "sw",
  e: "w",
  se: "nw",
  s: "n",
  sw: "ne",
  w: "e",
  nw: "se",
  up: "down",
}

/** The message without its repeat suffix ("... x 4"), and its repeat count. */
function splitRepeat(message: string): { base: string; count: number } {
  const m = /^(.*) x (\d+)$/.exec(message)
  return m ? { base: m[1], count: Number(m[2]) } : { base: message, count: 1 }
}

/** Registers the time contract under `name`. One game boot serves the whole session. */
export function runTimeContract(name: string, target: ContractTarget): void {
  Deno.test({
    name: `driver time contract: ${name}`,
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
      try {
        const ping = await driver.send({ cmd: "ping" }, target.bootTimeoutMs)
        assertEquals(ping.status, "ok")

        let now = (await driver.send({ cmd: "state" })).turn as number
        const loadTurn = now
        /** Each step starts from the turn the driver reports, so one failure cannot cascade. */
        const sync = async () => {
          now = (await driver.send({ cmd: "state" })).turn as number
        }
        /** Time agrees with the flag once the first-action partial turn is behind us. */
        const assertTimeAgrees = (res: DriverResponse) => {
          if (res.time_passed) {
            assert((res.turn as number) > now, "time passed but turn did not move")
          } else assertEquals(res.turn, now)
          now = res.turn as number
        }

        await t.step(
          "the first action after load may complete a partial turn without moving the counter",
          async () => {
            const res = await driver.send({ cmd: "wait", turns: 1 })
            assertEquals(res.status, "ok")
            assertEquals(res.outcome, "completed")
            assertEquals(res.time_passed, true)
            // The counter may stay where it was or move by the turn that was spent: both are fine.
            assert((res.turn as number) >= loadTurn, "turn went backwards")
            assert((res.turn as number) <= loadTurn + 1, "more than one turn for one wait")
          },
        )

        await t.step("wait N advances N turns and reports time_passed", async () => {
          await sync()
          const res = await driver.send({ cmd: "wait", turns: 5 })
          assertEquals(res.status, "ok")
          assertEquals(res.outcome, "completed")
          assertEquals(res.time_passed, true)
          assertEquals(res.moved, false)
          assertEquals(res.boundary, "turn_complete")
          assertEquals(res.turn, now + 5)
          const after = await driver.send({ cmd: "state" })
          assertEquals(after.turn, res.turn)
        })

        await t.step("wait refuses unusable turn counts as protocol errors", async () => {
          await sync()
          for (const bad of [{}, { turns: 0 }, { turns: -3 }, { turns: 1.5 }, { turns: "3" }]) {
            const res = await driver.send({ cmd: "wait", ...bad })
            assertEquals(res.status, "error", JSON.stringify(bad))
            assertEquals(typeof res.error, "string")
          }
          assertEquals((await driver.send({ cmd: "state" })).turn, now)
        })

        await t.step(
          "move rejects a missing or unknown direction as a protocol error",
          async () => {
            await sync()
            for (const bad of [{}, { dir: "sideways" }, { dir: 3 }]) {
              const res = await driver.send({ cmd: "move", ...bad })
              assertEquals(res.status, "error", JSON.stringify(bad))
            }
            assertEquals((await driver.send({ cmd: "state" })).turn, now)
          },
        )

        let blockedDir: string | undefined
        /** A blocked direction whose bump also logs a message, for the repeat test. */
        let talkativeDir: string | undefined
        await t.step("a blocked move reports blocked, not moved, and takes no time", async () => {
          await sync()
          for (const dir of DIRECTIONS) {
            const res = await driver.send({ cmd: "move", dir })
            assertEquals(res.status, "ok")
            assertEquals(typeof res.moved, "boolean")
            if (res.moved) {
              // A step that happened is completed and costs time; then step back.
              assertEquals(res.outcome, "completed")
              assertEquals(res.time_passed, true)
              assertTimeAgrees(res)
              assertTimeAgrees(await driver.send({ cmd: "move", dir: OPPOSITE[dir] }))
            } else if (res.outcome === "blocked") {
              assertEquals(res.time_passed, false)
              assertTimeAgrees(res)
              blockedDir ??= dir
              if ((res.new_messages as string[]).length > 0) talkativeDir ??= dir
            } else {
              // Not a wall (a door, a creature): whatever happened, time and turn agree.
              assertTimeAgrees(res)
            }
          }
          assertExists(blockedDir, "no neighbouring tile blocked the avatar")
        })

        await t.step(
          "new_messages holds only what the action produced, including a repeated message",
          async () => {
            await sync()
            const dir = talkativeDir
            assertExists(dir, "no blocked move logged a message")
            const first = await driver.send({ cmd: "move", dir })
            assertEquals(first.outcome, "blocked")
            const firstMessages = first.new_messages as string[]
            assert(firstMessages.length > 0, "the blocked move produced no message")

            // An action that produces nothing carries nothing: the log is not replayed.
            const quiet = await driver.send({ cmd: "state" })
            assertEquals(quiet.new_messages, [])

            const again = await driver.send({ cmd: "move", dir })
            assertEquals(again.outcome, "blocked")
            const repeated = splitRepeat(firstMessages[firstMessages.length - 1])
            const seen = (again.new_messages as string[]).map(splitRepeat).find((m) =>
              m.base === repeated.base
            )
            assertExists(seen, "the repeated message was not reported")
            assertEquals(seen.count, repeated.count + 1)
            assertEquals((again.new_messages as string[]).length, 1)
          },
        )

        await t.step(
          "seed is accepted after load and reported back, without taking time",
          async () => {
            await sync()
            const res = await driver.send({ cmd: "seed", seed: 12345 })
            assertEquals(res.status, "ok")
            assertEquals(res.seed, 12345)
            assertEquals((await driver.send({ cmd: "state" })).turn, now)
            // The engine keeps serving after a reseed.
            const waited = await driver.send({ cmd: "wait", turns: 2 })
            assertEquals(waited.outcome, "completed")
            assertEquals(waited.turn, now + 2)
            for (const bad of [{}, { seed: -1 }, { seed: "7" }, { seed: 1.5 }, { seed: 2 ** 32 }]) {
              const err = await driver.send({ cmd: "seed", ...bad })
              assertEquals(err.status, "error", JSON.stringify(bad))
            }
          },
        )

        await t.step(
          "a wait longer than the per-request cap is interrupted at the cap",
          async () => {
            await sync()
            const res = await driver.send({ cmd: "wait", turns: 1001 }, 120_000)
            assertEquals(res.status, "ok")
            assertEquals(res.outcome, "interrupted")
            assertEquals(res.reason, "turn_cap")
            assertEquals(res.turn, now + 1000)
          },
        )

        await t.step(
          "set_time pins the date and the time of day, and bad values are refused",
          async () => {
            await sync()
            // Year 1, season 01, day 03, 08:30: two days and 8.5 hours after the calendar's start,
            // whatever the world's season length.
            const pinned = 2 * 86_400 + 8 * 3_600 + 30 * 60
            const res = await driver.send({ cmd: "set_time", date: "0001-01-03", time: "08:30" })
            assertEquals(res.status, "ok", JSON.stringify(res))
            assertEquals(res.turn, pinned)
            assertEquals(res.date, "0001-01-03")
            assertEquals(res.time, "08:30")
            assertEquals((await driver.send({ cmd: "state" })).turn, pinned)
            // Time runs on from the pin.
            assertEquals((await driver.send({ cmd: "wait", turns: 2 })).turn, pinned + 2)

            // One field alone keeps the other: the time of day, then the day.
            const dateOnly = await driver.send({ cmd: "set_time", date: "0001-01-05" })
            assertEquals(dateOnly.turn, 4 * 86_400 + 8 * 3_600 + 30 * 60 + 2)
            const timeOnly = await driver.send({ cmd: "set_time", time: "23:59" })
            assertEquals(timeOnly.turn, 4 * 86_400 + 23 * 3_600 + 59 * 60)

            await sync()
            const bad = [
              {},
              { date: "yesterday" },
              { date: "0001-05-01" },
              { date: "0001-00-01" },
              { date: "0000-01-01" },
              { date: "0001-01-00" },
              { date: "0001-01-99" },
              { date: 20260101 },
              { time: "24:00" },
              { time: "12:60" },
              { time: "noon" },
              { time: 1200 },
            ]
            for (const request of bad) {
              const err = await driver.send({ cmd: "set_time", ...request })
              assertEquals(err.status, "error", JSON.stringify(request))
            }
            assertEquals(
              (await driver.send({ cmd: "state" })).turn,
              now,
              "a refusal moves the clock",
            )
          },
        )

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
