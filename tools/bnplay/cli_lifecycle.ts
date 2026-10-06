/**
 * End-to-end CLI suite, shared by the mock driver and the real game binary: an agent starts, steps
 * and stops an Episode from a shell and reads the transcript afterwards. It asserts external
 * behaviour only: exit codes, what the commands print, the transcript, the fixture and whether any
 * process is left behind.
 */
import { assert, assertEquals, assertNotEquals } from "@std/assert"
import {
  eventually,
  jsonOut,
  makeSandbox,
  pidsMatching,
  readTranscript,
  type SandboxOptions,
  treeSnapshot,
} from "./testkit.ts"

export type LifecycleTarget = {
  /** Game binary the daemon starts. */
  binary: string
  /** Directory copied into the fixture library as `fixture`; created fresh per run. */
  fixtureSource: () => Promise<string>
  /** Removes whatever `fixtureSource` created. */
  disposeFixtureSource?: (dir: string) => Promise<void>
  fixture: string
  env?: SandboxOptions["env"]
}

export function runCliLifecycle(name: string, target: LifecycleTarget): void {
  Deno.test({
    name: `cli lifecycle: ${name}`,
    sanitizeOps: false,
    sanitizeResources: false,
    async fn(t) {
      const source = await target.fixtureSource()
      const sandbox = await makeSandbox({
        binary: target.binary,
        fixtureSources: { [target.fixture]: source },
        env: target.env,
      })
      try {
        const fixtureBefore = await treeSnapshot(`${sandbox.fixtures}/${target.fixture}`)
        const trial = await sandbox.trial(
          `fixture = "${target.fixture}"\nseed = 7\nwall_clock_limit_s = 120\n`,
        )
        let session = ""
        let transcript = ""

        await t.step("start boots an Episode and returns a session id", async () => {
          const res = await sandbox.cli(["start", trial])
          assertEquals(res.code, 0, res.stderr)
          const out = jsonOut(res)
          assertEquals(typeof out.session, "string")
          assertEquals(typeof out.transcript, "string")
          session = out.session as string
          transcript = out.transcript as string
        })

        await t.step("step sends one command and prints the lean response", async () => {
          const ping = await sandbox.cli(["step", session, '{"cmd":"ping"}'])
          assertEquals(ping.code, 0, ping.stderr)
          assertEquals(jsonOut(ping).status, "ok")
          assertEquals(jsonOut(ping).ready, true)

          const state = await sandbox.cli(["step", session, '{"cmd":"state"}'])
          assertEquals(state.code, 0, state.stderr)
          const obs = jsonOut(state)
          assertEquals(obs.outcome, "completed")
          assertEquals(obs.time_passed, false)
          assert(Number.isInteger(obs.turn))
        })

        await t.step("a step that is not JSON is refused and the Episode keeps going", async () => {
          const bad = await sandbox.cli(["step", session, "not json"])
          assertEquals(bad.code, 2)
          assert(bad.stderr.includes("JSON"), bad.stderr)
          const state = await sandbox.cli(["step", session, '{"cmd":"state"}'])
          assertEquals(jsonOut(state).status, "ok")
        })

        await t.step("a driver error comes back as the response, not as a crash", async () => {
          const res = await sandbox.cli(["step", session, '{"cmd":"no_such_command"}'])
          assertEquals(res.code, 0, res.stderr)
          assertEquals(jsonOut(res).status, "error")
        })

        await t.step(
          "a wait past the per-request cap is interrupted at the cap, not killed as a hang",
          async () => {
            const res = await sandbox.cli(["step", session, '{"cmd":"wait","turns":1001}'])
            assertEquals(res.code, 0, res.stderr)
            const out = jsonOut(res)
            assertEquals(out.outcome, "interrupted")
            assertEquals(out.reason, "turn_cap")
            assertEquals(out.episode_ended, undefined)
          },
        )

        await t.step("stop ends the Episode and leaves no game process", async () => {
          const res = await sandbox.cli(["stop", session])
          assertEquals(res.code, 0, res.stderr)
          const out = jsonOut(res)
          assertEquals(out.ended, "stop")
          assertEquals(out.exit_code, 0)
          const userdir = `${sandbox.home}/episodes/${session}/userdir`
          assert(
            await eventually(async () => (await pidsMatching(userdir)).length === 0, 15_000),
            "game process left running after stop",
          )
        })

        await t.step("a stopped session refuses further steps", async () => {
          const res = await sandbox.cli(["step", session, '{"cmd":"state"}'])
          assertEquals(res.code, 2)
          assert(res.stderr.includes("ended"), res.stderr)
        })

        await t.step("the transcript records every request and response in order", async () => {
          const records = await readTranscript(transcript)
          const requests = records.flatMap((r) => r.request ? [r.request] : [])
          const responses = records.flatMap((r) => r.response ? [r.response] : [])
          assertEquals(requests.length, responses.length)
          assertEquals(
            requests.map((r) => r.cmd),
            // Boot: ping, the Trial's seed, the first state; then what the agent sent, then quit.
            [
              "ping",
              "seed",
              "state",
              "ping",
              "state",
              "state",
              "no_such_command",
              "wait",
              "quit",
            ],
          )
          for (const [i, req] of requests.entries()) assertEquals(responses[i].id, req.id)
        })

        await t.step("the source fixture is never modified", async () => {
          assertEquals(await treeSnapshot(`${sandbox.fixtures}/${target.fixture}`), fixtureBefore)
          assertNotEquals(Object.keys(fixtureBefore).length, 0)
        })
      } finally {
        await sandbox.cleanup()
        await target.disposeFixtureSource?.(source)
      }
    },
  })
}
