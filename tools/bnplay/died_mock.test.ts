/**
 * Avatar death through the supervisor, against the mock driver: a response whose outcome is
 * `died` is answered in full, ends the Episode as a normal end, and leaves no game behind.
 * Only external behaviour is asserted.
 */
import { assert, assertEquals } from "@std/assert"
import { join } from "@std/path"
import {
  type CliResult,
  jsonOut,
  makeFakeWorld,
  makeSandbox,
  pidsMatching,
  readTranscript,
} from "./testkit.ts"

type Report = { verdict: string; ended: string; exit_code: number; transcript: string }

async function withEpisode(
  toml: string,
  body: (
    cli: (args: string[]) => Promise<CliResult>,
    session: string,
    home: string,
  ) => Promise<void>,
): Promise<void> {
  const world = await makeFakeWorld()
  const sandbox = await makeSandbox({ fixtureSources: { bairdford: world } })
  try {
    const started = await sandbox.cli([
      "start",
      await sandbox.trial(`fixture = "bairdford"\n${toml}`),
    ])
    assertEquals(started.code, 0, started.stderr)
    const { session } = jsonOut<{ session: string }>(started)
    await body((args) => sandbox.cli(args), session, sandbox.home)
  } finally {
    await sandbox.cleanup()
    await Deno.remove(world, { recursive: true })
  }
}

Deno.test("the avatar dying ends the Episode on the response that says so", async () => {
  await withEpisode("", async (cli, session, home) => {
    const alive = await cli(["step", session, JSON.stringify({ cmd: "melee", dir: "e" })])
    assertEquals(alive.code, 0, alive.stderr)
    assertEquals(jsonOut(alive).outcome, "refused", "an ordinary response does not end the Episode")
    assertEquals(jsonOut(alive).episode_ended, undefined)

    const death = await cli(["step", session, JSON.stringify({ cmd: "hurt", amount: 100 })])
    assertEquals(death.code, 0, death.stderr)
    assertEquals(jsonOut(death).outcome, "died")
    assertEquals(jsonOut(death).hp, 0)
    assertEquals(jsonOut(death).episode_ended, "died", "the step that killed is answered in full")

    const after = await cli(["step", session, JSON.stringify({ cmd: "state" })])
    assertEquals(after.code, 2)
    assert(after.stderr.includes("died"), after.stderr)

    const stopped = await cli(["stop", session])
    const report = jsonOut<Report>(stopped)
    assertEquals(report.ended, "died")
    assertEquals(stopped.code, 0, "the avatar's death is a normal end, not a harness error")
    assertEquals(report.verdict, "pass")
    assertEquals(
      (await readTranscript(report.transcript)).findLast((r) => r.event)?.detail?.reason,
      "died",
    )
    assertEquals(await pidsMatching(join(home, "episodes", session)), [])
  })
})

Deno.test("a Trial's oracles still judge a run that ended in death", async () => {
  await withEpisode(
    `[[oracle]]
     name = "stays alive"
     field = "outcome"
     operator = "eq"
     value = "died"
     mode = "never"`,
    async (cli, session) => {
      await cli(["step", session, JSON.stringify({ cmd: "hurt", amount: 100 })])
      const stopped = await cli(["stop", session])
      const report = jsonOut<Report & { oracles: { name: string; result: string }[] }>(stopped)
      assertEquals(report.ended, "died")
      assertEquals(stopped.code, 1)
      assertEquals(report.verdict, "fail")
      assertEquals(report.oracles.find((o) => o.name === "stays alive")?.result, "fail")
    },
  )
})
