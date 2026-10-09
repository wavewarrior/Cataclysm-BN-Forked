/**
 * Oracles, the report and the exit codes, end to end through the CLI against the mock driver:
 * what a report contains, which exit code a run earns, and that the Trial's seed and turn limit
 * are applied. Only external behaviour is asserted.
 */
import { assert, assertEquals, assertNotEquals } from "@std/assert"
import { join } from "@std/path"
import { delay } from "@std/async"
import {
  jsonOut,
  makeFakeWorld,
  makeSandbox,
  pidsMatching,
  readTranscript,
  type Sandbox,
} from "./testkit.ts"

type Oracle = {
  name: string
  result: string
  first_fail?: { index: number; turn?: number; elapsed?: number; why: string }
}
type Report = {
  session: string
  verdict: string
  exit_code: number
  ended: string
  oracles: Oracle[]
  boot_ms: number
  requests: number
  latency_ms: { median: number; max: number }
  turns?: { first: number; last: number }
  transcript: string
  log: string
  notes?: string[]
}

/** What the fixture's game logs at idle: noise the baseline must absorb. */
const IDLE_NOISE = "ERROR : data/json/mods/noisy.json: a known problem of this mod set"

async function withSandbox(
  body: (sandbox: Sandbox) => Promise<void>,
  env: Record<string, string> = {},
  options: { baseline?: boolean; worldMarkers?: string[] } = {},
): Promise<void> {
  const world = await makeFakeWorld()
  await Deno.writeTextFile(join(world, "mock_log_idle.txt"), IDLE_NOISE + "\n")
  for (const marker of options.worldMarkers ?? []) await Deno.writeTextFile(join(world, marker), "")
  const sandbox = await makeSandbox({
    fixtureSources: { bairdford: world },
    env: { BNPLAY_BASELINE_IDLE_MS: "500", ...env },
  })
  try {
    if (options.baseline !== false) {
      const baseline = await sandbox.cli(["fixture", "baseline", "bairdford"])
      assertEquals(baseline.code, 0, baseline.stderr)
    }
    await body(sandbox)
  } finally {
    await sandbox.cleanup()
    await Deno.remove(world, { recursive: true })
  }
}

async function start(sandbox: Sandbox, toml = "") {
  const res = await sandbox.cli(["start", await sandbox.trial(`fixture = "bairdford"\n${toml}`)])
  assertEquals(res.code, 0, res.stderr)
  return jsonOut<{ session: string; transcript: string }>(res).session
}

async function step(sandbox: Sandbox, session: string, request: object) {
  const res = await sandbox.cli(["step", session, JSON.stringify(request)])
  return { ...res, json: res.code === 0 ? jsonOut(res) : undefined }
}

async function finish(sandbox: Sandbox, session: string, verb: "stop" | "report" = "stop") {
  const res = await sandbox.cli([verb, session])
  return { code: res.code, stderr: res.stderr, report: jsonOut<Report>(res) }
}

const oracle = (report: Report, name: string) => report.oracles.find((o) => o.name === name)

Deno.test("a clean run passes: exit 0, every oracle passes, numbers and paths in a compact report", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(
      sandbox,
      `expected_commands = ["wait"]
       [[oracle]]
       field = "hp"
       operator = "gt"
       value = 0
       [[oracle]]
       field = "new_messages"
       operator = "contains"
       value = "You die"
       mode = "never"`,
    )
    await step(sandbox, session, { cmd: "wait", turns: 3 })
    await step(sandbox, session, { cmd: "move", dir: "n" })
    await step(sandbox, session, { cmd: "log", text: "INFO : perf: 17ms 4096 bytes" })
    const { code, report } = await finish(sandbox, session)

    assertEquals(code, 0)
    assertEquals(report.exit_code, 0)
    assertEquals(report.verdict, "pass")
    assertEquals(report.ended, "stop")
    assertEquals(
      report.oracles.map((o) => [o.name, o.result]),
      [
        ["alive", "pass"],
        ["clean_exit", "pass"],
        ["game_log", "pass"],
        ["turn_counter", "pass"],
        ["commands", "pass"],
        ["always: hp gt 0", "pass"],
        ["never: new_messages contains You die", "pass"],
      ],
    )
    assert(report.boot_ms > 0)
    assert(report.latency_ms.max >= report.latency_ms.median && report.latency_ms.median > 0)
    assertEquals(report.turns, { first: 1000, last: 1003 })
    assertEquals(report.transcript.endsWith("transcript.jsonl"), true)
    assertEquals(report.log.endsWith("debug.log"), true)
    // "About 500 tokens": at four characters a token, comfortably below 2.5K characters.
    const size = JSON.stringify(report).length
    assert(size < 2_500, `the report is ${size} characters`)
    assertEquals(await pidsMatching(join(sandbox.home, "episodes", session)), [])
  })
})

Deno.test("a game that crashes on its way out after stop fails the Episode", async () => {
  await withSandbox(
    async (sandbox) => {
      const session = await start(sandbox)
      await step(sandbox, session, { cmd: "wait", turns: 3 })
      const { code, report } = await finish(sandbox, session)

      assertEquals(code, 1)
      assertEquals(report.verdict, "fail")
      assertEquals(report.ended, "stop")
      const exit = oracle(report, "clean_exit")
      assertEquals(exit?.result, "fail")
      assert(exit?.first_fail?.why.includes("after stop"), exit?.first_fail?.why)
      assertEquals(oracle(report, "alive")?.result, "pass")
    },
    {},
    { baseline: false, worldMarkers: ["mock_quit_crash"] },
  )
})

Deno.test("a failing oracle exits 1 and the report names the first failing request", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(
      sandbox,
      `[[oracle]]
       name = "stays healthy"
       field = "hp"
       operator = "gt"
       value = 50`,
    )
    await step(sandbox, session, { cmd: "wait", turns: 2 })
    const hurt = (await step(sandbox, session, { cmd: "hurt", amount: 60 })).json!
    await step(sandbox, session, { cmd: "hurt", amount: 30 })
    const { code, report } = await finish(sandbox, session)

    assertEquals(code, 1)
    assertEquals(report.verdict, "fail")
    const failed = oracle(report, "stays healthy")!
    assertEquals(failed.result, "fail")
    assertEquals(failed.first_fail?.index, hurt.id, "the first failing request, not the last")
    assertEquals(failed.first_fail?.turn, 1002)
    assertEquals(failed.first_fail?.elapsed, 2)
    assert(failed.first_fail?.why.includes("hp=40"), failed.first_fail?.why)
    // The index leads into the transcript, at the request that broke it.
    const records = await readTranscript(report.transcript)
    assertEquals(records.find((r) => r.request?.id === hurt.id)?.request?.cmd, "hurt")
  })
})

Deno.test("a warn oracle shows in the report and does not change the exit code", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(
      sandbox,
      `[[oracle]]
       name = "soft"
       field = "hp"
       operator = "gt"
       value = 50
       severity = "warn"`,
    )
    await step(sandbox, session, { cmd: "hurt", amount: 80 })
    const { code, report } = await finish(sandbox, session)
    assertEquals(code, 0)
    assertEquals(report.verdict, "pass")
    const soft = oracle(report, "soft")!
    assertEquals(soft.result, "warn")
    assertEquals(typeof soft.first_fail?.index, "number")
  })
})

Deno.test("never and by-turn-N modes", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(
      sandbox,
      `[[oracle]]
       name = "no menu"
       field = "prompt"
       operator = "eq"
       value = "map"
       mode = "never"
       [[oracle]]
       name = "reaches turn 1003 in time"
       field = "turn"
       operator = "ge"
       value = 1003
       mode = "by-turn-5"
       [[oracle]]
       name = "too slow"
       field = "turn"
       operator = "ge"
       value = 1050
       mode = "by-turn-5"
       [[oracle]]
       name = "never decided"
       field = "turn"
       operator = "ge"
       value = 1050
       mode = "by-turn-500"`,
    )
    await step(sandbox, session, { cmd: "wait", turns: 3 })
    const late = (await step(sandbox, session, { cmd: "wait", turns: 4 })).json!
    const open = (await step(sandbox, session, { cmd: "action", name: "map" })).json!
    const { code, report } = await finish(sandbox, session)

    assertEquals(oracle(report, "reaches turn 1003 in time")?.result, "pass")
    const slow = oracle(report, "too slow")!
    assertEquals(slow.result, "fail")
    assertEquals(slow.first_fail?.index, late.id, "decided by the first observation past turn 5")
    assertEquals(oracle(report, "no menu")?.first_fail?.index, open.id)
    // An Episode that ended before the deadline never showed the condition: that is a failure.
    assertEquals(oracle(report, "never decided")?.result, "fail")
    assertEquals(code, 1)
  })
})

Deno.test("the turn counter oracle catches a counter that goes backwards or moves silently", async () => {
  await withSandbox(async (sandbox) => {
    const back = await start(sandbox)
    await step(sandbox, back, { cmd: "wait", turns: 4 })
    const rewound = (await step(sandbox, back, { cmd: "rewind", turns: 2 })).json!
    const backReport = await finish(sandbox, back)
    assertEquals(backReport.code, 1)
    assertEquals(oracle(backReport.report, "turn_counter")?.result, "fail")
    assertEquals(oracle(backReport.report, "turn_counter")?.first_fail?.index, rewound.id)

    const silent = await start(sandbox)
    const glitch = (await step(sandbox, silent, { cmd: "glitch" })).json!
    const silentReport = await finish(sandbox, silent)
    assertEquals(silentReport.code, 1)
    const turn = oracle(silentReport.report, "turn_counter")!
    assertEquals(turn.first_fail?.index, glitch.id)
    assert(turn.first_fail?.why.includes("time_passed"), turn.first_fail?.why)
  })
})

Deno.test("expected commands that come back unsupported or no_effect fail the Episode", async () => {
  await withSandbox(async (sandbox) => {
    const unsupported = await start(sandbox, `expected_commands = ["action:craft"]`)
    const crafted = (await step(sandbox, unsupported, { cmd: "action", name: "craft" })).json!
    assertEquals(crafted.outcome, "unsupported")
    const first = await finish(sandbox, unsupported)
    assertEquals(first.code, 1)
    assertEquals(oracle(first.report, "commands")?.first_fail?.index, crafted.id)

    const nothing = await start(sandbox, `expected_commands = ["action"]`)
    const fidget = (await step(sandbox, nothing, { cmd: "action", name: "fidget" })).json!
    assertEquals(fidget.outcome, "no_effect")
    const second = await finish(sandbox, nothing)
    assertEquals(second.code, 1)
    assertEquals(oracle(second.report, "commands")?.first_fail?.index, fidget.id)

    // The same outcome on a command the Trial never listed is the agent's business.
    const unlisted = await start(sandbox, `expected_commands = ["wait"]`)
    await step(sandbox, unlisted, { cmd: "action", name: "craft" })
    assertEquals((await finish(sandbox, unlisted)).code, 0)
  })
})

Deno.test("the game log check ignores baseline lines and perf noise and fails only on new errors", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox)
    // Idle noise from the baseline is logged again during this Episode.
    await delay(400)
    await step(sandbox, session, { cmd: "log", text: "WARNING : a new warning is not an error" })
    await step(sandbox, session, { cmd: "log", text: "INFO : perf: 31ms" })
    const clean = await finish(sandbox, session, "stop")
    assertEquals(clean.code, 0, JSON.stringify(clean.report))
    assertEquals(oracle(clean.report, "game_log")?.result, "pass")

    const broken = await start(sandbox)
    await step(sandbox, broken, { cmd: "wait", turns: 1 })
    const logged = (await step(sandbox, broken, { cmd: "log", text: "ERROR : src/new.cpp:9 boom" }))
      .json!
    await step(sandbox, broken, { cmd: "wait", turns: 1 })
    const bad = await finish(sandbox, broken)
    assertEquals(bad.code, 1)
    const log = oracle(bad.report, "game_log")!
    assertEquals(log.result, "fail")
    assertEquals(log.first_fail?.index, logged.id)
    assert(log.first_fail?.why.includes("src/new.cpp:9 boom"), log.first_fail?.why)
  })
})

Deno.test("without a baseline the log check is skipped, visibly", async () => {
  await withSandbox(
    async (sandbox) => {
      const session = await start(sandbox)
      await step(sandbox, session, { cmd: "log", text: "ERROR : src/new.cpp:9 boom" })
      const { code, report } = await finish(sandbox, session)
      assertEquals(code, 0)
      assertEquals(oracle(report, "game_log")?.result, "skipped")
      assert(
        report.notes?.some((n) => n.includes("fixture baseline bairdford")),
        String(report.notes),
      )
    },
    {},
    { baseline: false },
  )
})

Deno.test("a game that hangs is killed and the report exits 2 naming the request", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox)
    const hung = await step(sandbox, session, { cmd: "hang" })
    assertEquals(hung.code, 2)
    const { code, report } = await finish(sandbox, session, "report")
    assertEquals(code, 2)
    assertEquals(report.verdict, "harness_error")
    assertEquals(report.ended, "hang")
    const alive = oracle(report, "alive")!
    assertEquals(alive.result, "fail")
    const records = await readTranscript(report.transcript)
    assertEquals(
      records.find((r) => r.request?.id === alive.first_fail?.index)?.request?.cmd,
      "hang",
    )
    assertEquals(await pidsMatching(join(sandbox.home, "episodes", session)), [])
  }, { BNPLAY_STEP_TIMEOUT_MS: "700" })
})

Deno.test("a game that dies is a harness error too", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox)
    const info = (await step(sandbox, session, { cmd: "info" })).json!
    Deno.kill(info.pid as number, "SIGKILL")
    const died = await step(sandbox, session, { cmd: "state" })
    assertEquals(died.code, 2)
    const { code, report } = await finish(sandbox, session)
    assertEquals(code, 2)
    assertEquals(report.ended, "driver_exit")
  })
})

Deno.test("hitting the wall-clock limit with no failed oracle is inconclusive (3), never a pass", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(
      sandbox,
      `wall_clock_limit_s = 2
       [[oracle]]
       name = "healthy"
       field = "hp"
       operator = "gt"
       value = 0
       [[oracle]]
       name = "by turn 10"
       field = "turn"
       operator = "ge"
       value = 1100
       mode = "by-turn-10"`,
    )
    await step(sandbox, session, { cmd: "wait", turns: 2 })
    await delay(2_500)
    const { code, report } = await finish(sandbox, session)
    assertEquals(report.ended, "wall_clock")
    assertEquals(code, 3)
    assertEquals(report.verdict, "inconclusive")
    assertEquals(report.exit_code, 3)
    assertEquals(oracle(report, "by turn 10")?.result, "inconclusive")
    assertEquals(oracle(report, "alive")?.result, "pass", "the watchdog kill is not the game dying")
    assertEquals(await pidsMatching(join(sandbox.home, "episodes", session)), [])
  })
})

Deno.test("a wall-clock ending fails (1) when an oracle failed and is inconclusive (3) otherwise", async () => {
  await withSandbox(async (sandbox) => {
    const failing = await start(
      sandbox,
      `wall_clock_limit_s = 2
       [[oracle]]
       field = "hp"
       operator = "gt"
       value = 90`,
    )
    await step(sandbox, failing, { cmd: "hurt", amount: 20 })
    await delay(2_500)
    const failed = await finish(sandbox, failing)
    assertEquals([failed.report.ended, failed.code], ["wall_clock", 1])

    const passing = await start(
      sandbox,
      `wall_clock_limit_s = 2
       [[oracle]]
       field = "turn"
       operator = "ge"
       value = 1002
       mode = "by-turn-5"`,
    )
    await step(sandbox, passing, { cmd: "wait", turns: 2 })
    await delay(2_500)
    const passed = await finish(sandbox, passing)
    assertEquals([passed.report.ended, passed.code], ["wall_clock", 3])
    assertEquals(passed.report.verdict, "inconclusive")
  })
})

Deno.test("a report asked for mid-run is provisional: nothing decisive yet is inconclusive", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox)
    await step(sandbox, session, { cmd: "wait", turns: 1 })
    const mid = await finish(sandbox, session, "report")
    assertEquals(mid.report.ended, "running")
    assertEquals(mid.code, 3)
    assertEquals(oracle(mid.report, "game_log")?.result, "skipped")
    // Asking does not end it.
    assertEquals((await step(sandbox, session, { cmd: "state" })).code, 0)
    assertEquals((await finish(sandbox, session)).code, 0)
    // The report of an ended Episode is the same every time.
    const again = await finish(sandbox, session, "report")
    assertEquals(again.code, 0)
    assertEquals(again.report.verdict, "pass")
  })
})

Deno.test("the Trial's seed is sent before the first state and a refused seed fails the boot", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, "seed = 4242\n")
    const { report } = await finish(sandbox, session)
    const requests = (await readTranscript(report.transcript)).flatMap((r) =>
      r.request ? [r.request] : []
    )
    assertEquals(requests.map((r) => r.cmd).slice(0, 3), ["ping", "seed", "state"])
    assertEquals(requests[1].seed, 4242)

    // The mock accepts seeds below 2^32; one past it is a refusal, and a boot failure.
    const refused = await sandbox.cli([
      "start",
      await sandbox.trial(`fixture = "bairdford"\nseed = 4294967296\n`),
    ])
    assertEquals(refused.code, 2)
    assert(refused.stderr.includes("seed"), refused.stderr)
    assertEquals(await pidsMatching(join(sandbox.home, "episodes")), [])
  })
})

Deno.test("the Trial's start date and time of day are pinned after the seed and before the first state", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(
      sandbox,
      'seed = 4242\nstart_date = "0002-03-10"\ntime_of_day = "08:30"\nturn_limit = 5\n',
    )
    const { report } = await finish(sandbox, session)
    const records = await readTranscript(report.transcript)
    const requests = records.flatMap((r) => r.request ? [r.request] : [])
    assertEquals(requests.map((r) => r.cmd).slice(0, 4), ["ping", "seed", "set_time", "state"])
    assertEquals(requests[2].date, "0002-03-10")
    assertEquals(requests[2].time, "08:30")

    // The mock's season is 91 days: year 2, season 3, day 10, 08:30.
    const pinned = (4 * 91 * 86_400) + 2 * 91 * 86_400 + 9 * 86_400 + 8 * 3_600 + 30 * 60
    const state = records.flatMap((r) => r.response ? [r.response] : [])[3]
    assertEquals(state.turn, pinned, "the first state is the pinned turn")
    assertEquals(report.turns?.first, pinned, "the Episode counts its turns from the pin")
  })
})

Deno.test("a time-of-day pin alone keeps the day, and a refused pin fails the boot", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, 'time_of_day = "06:00"\n')
    const { report } = await finish(sandbox, session)
    const requests = (await readTranscript(report.transcript)).flatMap((r) =>
      r.request ? [r.request] : []
    )
    assertEquals(requests.map((r) => r.cmd).slice(0, 3), ["ping", "set_time", "state"])
    assertEquals(requests[1].date, undefined)
    assertEquals(report.turns?.first, 6 * 3_600)

    // Day 92 passes the Trial parser (the game owns the season length) but not the game.
    const refused = await sandbox.cli([
      "start",
      await sandbox.trial(`fixture = "bairdford"\nstart_date = "0001-01-92"\n`),
    ])
    assertEquals(refused.code, 2)
    assert(refused.stderr.includes("start_date"), refused.stderr)
    assertEquals(await pidsMatching(join(sandbox.home, "episodes")), [])
  })
})

Deno.test("the Trial's turn limit ends the Episode after that many turns", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, "turn_limit = 10\n")
    const first = await step(sandbox, session, { cmd: "wait", turns: 6 })
    assertEquals(first.json?.episode_ended, undefined)
    const last = await step(sandbox, session, { cmd: "wait", turns: 6 })
    assertEquals(last.code, 0, last.stderr)
    assertEquals(last.json?.episode_ended, "turn_limit")
    assertEquals(last.json?.turn, 1012, "the step that crossed the limit is answered in full")

    const after = await step(sandbox, session, { cmd: "state" })
    assertEquals(after.code, 2)
    assert(after.stderr.includes("turn_limit"), after.stderr)
    const { code, report } = await finish(sandbox, session)
    assertEquals(report.ended, "turn_limit")
    assertEquals(code, 0, "a turn limit is a normal end")
    assertEquals(
      (await readTranscript(report.transcript)).findLast((r) => r.event)?.detail?.reason,
      "turn_limit",
    )
    assertEquals(await pidsMatching(join(sandbox.home, "episodes", session)), [])
  })
})

Deno.test("an idle-reaped session reports a harness error", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox)
    await delay(2_000)
    const { code, report } = await finish(sandbox, session)
    assertEquals(report.ended, "idle_timeout")
    assertEquals(code, 2)
    assertNotEquals(report.verdict, "pass")
  }, { BNPLAY_IDLE_TIMEOUT_MS: "1000" })
})
