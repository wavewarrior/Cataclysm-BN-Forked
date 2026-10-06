/**
 * Wire-protocol contract suite, shared by every driver implementation (the real game binary now,
 * a mock driver later). It asserts external behaviour only: what a response contains, exit codes
 * and whether a process is left behind. Every request in it has a timeout.
 */
import { assert, assertEquals, assertExists } from "@std/assert"
import type { Driver } from "./client.ts"

export type ContractTarget = {
  /** Starts a fresh driver session; `denyList` names a deny-list data file to load instead. */
  spawn: (opts?: { denyList?: string }) => Promise<Driver>
  /** Boot-window ceiling for the first answer, in milliseconds. */
  bootTimeoutMs: number
}

/** Returns true when any process is still alive in the process group. */
export async function groupAlive(pgid: number): Promise<boolean> {
  const { code } = await new Deno.Command("pgrep", {
    args: ["-g", String(pgid)],
    stdout: "null",
    stderr: "null",
  }).output()
  return code === 0
}

/** Registers the contract tests under `name`. One game boot serves the whole session. */
export function runContract(name: string, target: ContractTarget): void {
  Deno.test({
    name: `driver contract: ${name}`,
    sanitizeOps: false,
    sanitizeResources: false,
    async fn(t) {
      const driver = await target.spawn()
      try {
        await t.step("boots and answers ping within the boot window", async () => {
          const res = await driver.send({ cmd: "ping" }, target.bootTimeoutMs)
          assertEquals(res.status, "ok")
          assertEquals(res.ready, true)
        })

        let turn = -1
        await t.step("state returns the lean observation", async () => {
          const res = await driver.send({ cmd: "state" })
          assertEquals(res.status, "ok")
          assertEquals(res.outcome, "completed")
          assertEquals(res.boundary, "turn_complete")
          assertEquals(res.time_passed, false)
          assertEquals(res.prompt, null)
          assert(Array.isArray(res.new_messages))
          assert(Number.isInteger(res.turn), "turn is an integer")
          for (const vital of ["hp", "pain", "stamina", "hunger", "thirst"]) {
            assert(typeof res[vital] === "number", `${vital} is a number`)
          }
          turn = res.turn as number
        })

        await t.step("state never takes game time", async () => {
          for (let i = 0; i < 3; i++) {
            const res = await driver.send({ cmd: "state" })
            assertEquals(res.turn, turn)
            assertEquals(res.time_passed, false)
          }
        })

        await t.step(
          "a malformed or unknown request returns an error line and serving continues",
          async () => {
            const garbage = await driver.sendRaw("this is not json", null)
            assertEquals(garbage.status, "error")
            assertEquals(garbage.id, null)
            assertEquals(typeof garbage.error, "string")

            // A readable id is echoed even when the rest of the request is unusable.
            const noCmd = await driver.sendRaw(JSON.stringify({ id: 9001 }), 9001)
            assertEquals(noCmd.status, "error")
            assertEquals(noCmd.id, 9001)

            const unknown = await driver.send({ cmd: "no_such_command" })
            assertEquals(unknown.status, "error")
            assertExists(unknown.error)

            const res = await driver.send({ cmd: "ping" })
            assertEquals(res.status, "ok")
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
