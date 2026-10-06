/**
 * Menu, raw-action and refusal contract: the raw `action` passthrough, menus that report
 * `awaiting_input` and take raw `key` answers, the deny list (a data file the driver loads at
 * start) and the no-fiber guard. Shared by every driver implementation, and like the other
 * contracts it asserts external behaviour only. Every request carries the client's timeout, so a
 * hang is a failing test, and every test confirms no game process is left behind.
 */
import { assert, assertEquals } from "@std/assert"
import type { Driver, DriverResponse } from "./client.ts"
import { type ContractTarget, groupAlive } from "./contract.ts"

/** Actions that hung the driver (the first four) or silently did nothing (the last two). */
const INITIAL_DENY = ["craft", "drop", "eat", "apply", "wear", "read"]

/** A denied request must answer at once, not after a wait: well under the 10 s client timeout. */
const IMMEDIATE_MS = 2_000

const MENUS = ["inventory", "look", "map"]

function denyListFile(actions: string[]): string {
  const deny = actions.map((action) => ({ action, why: `contract test entry for ${action}` }))
  return JSON.stringify({ deny })
}

/** Registers the menu contract under `name`. Each test boots its own game. */
export function runMenuContract(name: string, target: ContractTarget): void {
  /** Starts a driver, optionally on a deny-list file holding `denyList`, and cleans up after. */
  const session = (
    title: string,
    denyList: string[] | undefined,
    fn: (driver: Driver, t: Deno.TestContext) => Promise<void>,
  ) => {
    Deno.test({
      name: `driver menu contract: ${name}: ${title}`,
      sanitizeOps: false,
      sanitizeResources: false,
      async fn(t) {
        const dir = await Deno.makeTempDir({ prefix: "bnplay-deny-" })
        let driver: Driver | undefined
        try {
          if (denyList) await Deno.writeTextFile(`${dir}/deny_list.json`, denyListFile(denyList))
          driver = await target.spawn(denyList ? { denyList: `${dir}/deny_list.json` } : undefined)
          const ping = await driver.send({ cmd: "ping" }, target.bootTimeoutMs)
          assertEquals(ping.status, "ok")
          await fn(driver, t)
          assertEquals(driver.noise, [], "the protocol channel carried non-protocol lines")
          // Whatever the test did, the driver must still serve.
          assertEquals((await driver.send({ cmd: "ping" })).status, "ok")
        } finally {
          await driver?.close()
          await Deno.remove(dir, { recursive: true })
        }
        if (driver) assert(!(await groupAlive(driver.pgid)), "process group left running")
      },
    })
  }

  const turnOf = async (driver: Driver) => (await driver.send({ cmd: "state" })).turn as number

  const assertUnsupported = (res: DriverResponse, reason: string, what: string) => {
    assertEquals(res.status, "ok", what)
    assertEquals(res.outcome, "unsupported", what)
    assertEquals(res.reason, reason, what)
    assertEquals(res.time_passed, false, what)
    assertEquals(res.prompt, null, what)
  }

  session(
    "menus report awaiting_input and a key answer closes them",
    undefined,
    async (driver, t) => {
      for (const menu of MENUS) {
        await t.step(`${menu}: opens, answers with ESC, passes no time`, async () => {
          const before = await turnOf(driver)
          const opened = await driver.send({ cmd: "action", name: menu })
          assertEquals(opened.status, "ok")
          assertEquals(opened.outcome, "awaiting_input")
          assertEquals(opened.boundary, "needs_input")
          assertEquals(opened.prompt, menu)
          assertEquals(opened.time_passed, false)
          assertEquals(opened.turn, before)

          const closed = await driver.send({ cmd: "key", key: "ESC" })
          assertEquals(closed.status, "ok")
          assertEquals(closed.outcome, "completed")
          assertEquals(closed.prompt, null)
          assertEquals(closed.time_passed, false)
          assertEquals(closed.turn, before)
          assertEquals(await turnOf(driver), before)
        })
      }

      await t.step(
        "while a menu is open state reports it and moves are protocol errors",
        async () => {
          const opened = await driver.send({ cmd: "action", name: "look" })
          assertEquals(opened.outcome, "awaiting_input")

          const state = await driver.send({ cmd: "state" })
          assertEquals(state.outcome, "awaiting_input")
          assertEquals(state.prompt, "look")
          assertEquals(state.time_passed, false)

          for (
            const req of [
              { cmd: "move", dir: "n" },
              { cmd: "wait", turns: 1 },
              { cmd: "action", name: "pause" },
            ]
          ) {
            const res = await driver.send(req)
            assertEquals(res.status, "error", JSON.stringify(req))
            assertEquals(typeof res.error, "string")
          }

          const bad = await driver.send({ cmd: "key", key: "no such key" })
          assertEquals(bad.status, "error", "an unknown key name is a protocol error")
          assertEquals((await driver.send({ cmd: "state" })).outcome, "awaiting_input")

          const closed = await driver.send({ cmd: "key", key: "ESC" })
          assertEquals(closed.outcome, "completed")
          assertEquals((await driver.send({ cmd: "state" })).outcome, "completed")
        },
      )

      await t.step("a key sent with no menu open is a protocol error", async () => {
        const before = await turnOf(driver)
        const res = await driver.send({ cmd: "key", key: "ESC" })
        assertEquals(res.status, "error")
        assertEquals(typeof res.error, "string")
        assertEquals(await turnOf(driver), before)
      })

      await t.step("key needs a usable key name", async () => {
        const opened = await driver.send({ cmd: "action", name: "inventory" })
        assertEquals(opened.outcome, "awaiting_input")
        for (const bad of [{}, { key: 5 }, { key: "" }]) {
          const res = await driver.send({ cmd: "key", ...bad })
          assertEquals(res.status, "error", JSON.stringify(bad))
        }
        assertEquals((await driver.send({ cmd: "key", key: "ESC" })).outcome, "completed")
      })
    },
  )

  session(
    "raw action rejects unknown names and takes the free ones",
    undefined,
    async (driver, t) => {
      await t.step("an unknown or missing action name is a protocol error", async () => {
        for (const bad of [{}, { name: "no_such_action" }, { name: 3 }]) {
          const res = await driver.send({ cmd: "action", ...bad })
          assertEquals(res.status, "error", JSON.stringify(bad))
        }
      })

      await t.step("a raw pause spends a turn like the typed wait", async () => {
        const before = await turnOf(driver)
        const res = await driver.send({ cmd: "action", name: "pause" })
        assertEquals(res.status, "ok")
        assertEquals(res.outcome, "completed")
        assertEquals(res.time_passed, true)
        assert((res.turn as number) >= before)
      })
    },
  )

  session("the deny list refuses the initial actions immediately", undefined, async (driver, t) => {
    for (const action of INITIAL_DENY) {
      await t.step(
        `raw ${action} is unsupported at once and the process keeps serving`,
        async () => {
          const before = await turnOf(driver)
          const res = await driver.send({ cmd: "action", name: action }, IMMEDIATE_MS)
          assertUnsupported(res, "deny_list", action)
          assertEquals(typeof res.detail, "string", "the entry's reason is reported")
          assertEquals(res.turn, before)
          assertEquals(await turnOf(driver), before)
        },
      )
    }
  })

  session(
    "the guard turns an unlisted blocking action into unsupported",
    ["look"],
    async (driver, t) => {
      await t.step("a listed action is refused from the file given at start", async () => {
        const res = await driver.send({ cmd: "action", name: "look" }, IMMEDIATE_MS)
        assertUnsupported(res, "deny_list", "look")
      })

      // These hang a driver with no guard: they are off this file's list on purpose.
      for (const action of ["craft", "drop", "eat", "apply"]) {
        await t.step(`unlisted raw ${action} is caught by the guard, not a hang`, async () => {
          const before = await turnOf(driver)
          const res = await driver.send({ cmd: "action", name: action }, IMMEDIATE_MS * 3)
          assertUnsupported(res, "blocking_read", action)
          assertEquals(res.turn, before)
          assertEquals(await turnOf(driver), before)
        })
      }

      await t.step("after a guard hit the game still answers menus and time", async () => {
        const opened = await driver.send({ cmd: "action", name: "inventory" })
        assertEquals(opened.outcome, "awaiting_input")
        assertEquals((await driver.send({ cmd: "key", key: "ESC" })).outcome, "completed")
        const waited = await driver.send({ cmd: "wait", turns: 2 })
        assertEquals(waited.outcome, "completed")
        assertEquals(waited.time_passed, true)
      })
    },
  )

  Deno.test({
    name: `driver menu contract: ${name}: editing the deny-list file frees an action, no rebuild`,
    sanitizeOps: false,
    sanitizeResources: false,
    async fn() {
      const dir = await Deno.makeTempDir({ prefix: "bnplay-deny-" })
      const file = `${dir}/deny_list.json`
      try {
        // One file, edited between two starts: each start reads it afresh.
        for (
          const { listed, look } of [
            { listed: ["craft", "look"], look: "unsupported" },
            { listed: ["craft"], look: "awaiting_input" },
          ]
        ) {
          await Deno.writeTextFile(file, denyListFile(listed))
          const driver = await target.spawn({ denyList: file })
          try {
            assertEquals((await driver.send({ cmd: "ping" }, target.bootTimeoutMs)).status, "ok")
            const res = await driver.send({ cmd: "action", name: "look" }, IMMEDIATE_MS)
            assertEquals(res.outcome, look, `look with ${JSON.stringify(listed)} listed`)
            if (look === "awaiting_input") {
              assertEquals((await driver.send({ cmd: "key", key: "ESC" })).outcome, "completed")
            }
            // The entry that stayed on the file is still refused.
            const craft = await driver.send({ cmd: "action", name: "craft" }, IMMEDIATE_MS)
            assertUnsupported(craft, "deny_list", "craft")
          } finally {
            await driver.close()
          }
          assert(!(await groupAlive(driver.pgid)), "process group left running")
        }
      } finally {
        await Deno.remove(dir, { recursive: true })
      }
    },
  })
}
