/**
 * Renderer oracles, end to end through the CLI and the daemon against the mock driver: a toggle
 * judged against a paired null, a restore that must hold, readiness from the log, and the Trial's
 * window size against the one the captures report. The mock draws real images (the left half as
 * blue as its render state says, plus as many flipped pixels of noise as it is told), so the
 * verdicts come from decoding real frames.
 */
import { assert, assertEquals } from "@std/assert"
import { readTranscript } from "./testkit.ts"
import {
  capture,
  oracle,
  play,
  start,
  step,
  stop,
  windowed,
  withSandbox,
} from "./renderer_testkit.ts"

const TRIPLET = `[[oracle]]
kind = "triplet"
original = "on"
toggled = "off"
restored = "on-again"
`

Deno.test("a toggle that clears twice the paired-null noise and restores identically passes triplet", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, windowed(TRIPLET))
    const answers = await play(sandbox, session, [
      { cmd: "render", state: 1, noise: 2000 },
      capture("on"),
      capture("on"),
      { cmd: "render", state: 0 },
      capture("off"),
      { cmd: "render", state: 1 },
      capture("on-again"),
    ])
    for (const a of answers) assertEquals(a.status, "ok", JSON.stringify(a))

    const { code, report } = await stop(sandbox, session)
    assertEquals(code, 0, JSON.stringify(report))
    assertEquals(report.verdict, "pass")
    assertEquals(oracle(report, "triplet: on").result, "pass")
    // The window is the Trial's, the frames a whole multiple of it (2x final composite).
    assertEquals(oracle(report, "window_size").result, "pass")
    // The report carries numbers and a directory, not images.
    assert(JSON.stringify(report).length < 3000, "the report is not compact")
    assert(report.notes?.some((n) => n.startsWith("triplet: on:") && /effect/.test(n)))
    // The tag is the supervisor's: the game never sees it.
    const sent = (await readTranscript(report.transcript)).filter((r) =>
      r.request?.cmd === "capture"
    )
    assertEquals(sent.length, 4)
    for (const r of sent) assertEquals("tag" in r.request!, false)
  })
})

Deno.test("a toggle the noise explains fails and says by how much", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, windowed(TRIPLET))
    // A paired null that differs by about 0.15 of the range: the toggle's 0.065 is lost in it.
    const answers = await play(sandbox, session, [
      { cmd: "render", state: 1, noise: 150000 },
      capture("on"),
      capture("on"),
      { cmd: "render", state: 0 },
      capture("off"),
      { cmd: "render", state: 1 },
      capture("on-again"),
    ])
    const { code, report } = await stop(sandbox, session)
    assertEquals(code, 1, JSON.stringify(report))
    assertEquals(report.verdict, "fail")
    const failed = oracle(report, "triplet: on")
    assertEquals(failed.result, "fail")
    assertEquals(failed.first_fail?.index, answers[4].id, "the failing capture is the toggled one")
    assert(/noise/.test(failed.first_fail!.why), failed.first_fail!.why)
    assert(failed.first_fail!.why.includes("2x"), failed.first_fail!.why)
  })
})

Deno.test("a toggle that changes nothing fails", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, windowed(TRIPLET))
    const answers = await play(sandbox, session, [
      { cmd: "render", state: 1, noise: 500 },
      capture("on"),
      capture("on"),
      capture("off"), // the render state never moved
      capture("on-again"),
    ])
    const { code, report } = await stop(sandbox, session)
    assertEquals(code, 1)
    const failed = oracle(report, "triplet: on")
    assertEquals(failed.result, "fail")
    assertEquals(failed.first_fail?.index, answers[3].id)
  })
})

Deno.test("a restore that silently failed is caught by the 1 -> 0 -> 1 triplet", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, windowed(TRIPLET))
    const answers = await play(sandbox, session, [
      { cmd: "render", state: 1, noise: 500 },
      capture("on"),
      capture("on"),
      { cmd: "render", state: 0, stuck: true },
      capture("off"),
      { cmd: "render", state: 1 }, // does not take
      capture("on-again"),
    ])
    const { code, report } = await stop(sandbox, session)
    assertEquals(code, 1)
    const failed = oracle(report, "triplet: on")
    assertEquals(failed.result, "fail")
    assertEquals(failed.first_fail?.index, answers[6].id, "the failing capture is the restored one")
    assert(/restore/.test(failed.first_fail!.why), failed.first_fail!.why)
    // The toggle itself did clear the noise: a diff_vs_null of the same frames would have passed.
  })
})

Deno.test("a restored frame that drifted from the original fails even when the toggle was real", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, windowed(TRIPLET))
    await play(sandbox, session, [
      { cmd: "render", state: 1, noise: 500 },
      capture("on"),
      capture("on"),
      { cmd: "render", state: 0 },
      capture("off"),
      { cmd: "render", state: 2 }, // a different state, not the original
      capture("on-again"),
    ])
    const { report } = await stop(sandbox, session)
    const failed = oracle(report, "triplet: on")
    assertEquals(failed.result, "fail")
    assert(/restore|drift/.test(failed.first_fail!.why), failed.first_fail!.why)
  })
})

Deno.test("paired_null judges the noise itself; each oracle reads only its own tags", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(
      sandbox,
      windowed(`[[oracle]]
kind = "paired_null"
name = "quiet"
original = "q"
[[oracle]]
kind = "paired_null"
name = "loud"
original = "l"
[[oracle]]
kind = "paired_null"
name = "loud but allowed"
original = "l"
max_noise = 0.2
`),
    )
    await play(sandbox, session, [
      { cmd: "render", noise: 2000 },
      capture("q"),
      capture("q"),
      { cmd: "render", noise: 30000 },
      capture("l"),
      capture("l"),
    ])
    const { code, report } = await stop(sandbox, session)
    assertEquals(code, 1)
    assertEquals(oracle(report, "quiet").result, "pass")
    assertEquals(oracle(report, "loud").result, "fail")
    assert(oracle(report, "loud").first_fail!.why.includes("max_noise"))
    assertEquals(oracle(report, "loud but allowed").result, "pass")
  })
})

Deno.test("diff_vs_null can look at a region of the frame", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(
      sandbox,
      windowed(`[[oracle]]
kind = "diff_vs_null"
name = "left"
original = "on"
toggled = "off"
region = [0, 0, 0.5, 1]
[[oracle]]
kind = "diff_vs_null"
name = "right"
original = "on"
toggled = "off"
region = [0.5, 0, 0.5, 1]
`),
    )
    await play(sandbox, session, [
      { cmd: "render", state: 1, noise: 1000 },
      capture("on"),
      capture("on"),
      { cmd: "render", state: 0 },
      capture("off"),
    ])
    const { code, report } = await stop(sandbox, session)
    assertEquals(code, 1)
    // The toggle changed the left half of the mock's frame, and only that.
    assertEquals(oracle(report, "left").result, "pass")
    assertEquals(oracle(report, "right").result, "fail")
  })
})

Deno.test("a state view (PNG) is judged like a final composite (BMP)", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, windowed(TRIPLET))
    await play(sandbox, session, [
      { cmd: "render", state: 1, noise: 500 },
      capture("on", "state"),
      capture("on", "state"),
      { cmd: "render", state: 0 },
      capture("off", "state"),
      { cmd: "render", state: 1 },
      capture("on-again", "state"),
    ])
    const { code, report } = await stop(sandbox, session)
    assertEquals(code, 0, JSON.stringify(report))
    assertEquals(oracle(report, "triplet: on").result, "pass")
    assertEquals(oracle(report, "window_size").result, "pass")
  })
})

Deno.test("frames of different sizes are never compared", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(
      sandbox,
      windowed(`[[oracle]]
kind = "paired_null"
original = "on"
`),
    )
    const answers = await play(sandbox, session, [
      capture("on", "final"),
      capture("on", "state"),
    ])
    const { code, report } = await stop(sandbox, session)
    assertEquals(code, 1)
    const failed = oracle(report, "paired_null: on")
    assertEquals(failed.result, "fail")
    assertEquals(failed.first_fail?.index, answers[1].id)
    assert(failed.first_fail!.why.includes("different sizes"), failed.first_fail!.why)
  })
})

Deno.test("readiness is read from the game's messages, not from the pixels", async () => {
  const ready = `[[oracle]]
kind = "diff_vs_null"
original = "on"
toggled = "off"
ready = "lighting settled"
`
  await withSandbox(async (sandbox) => {
    const careless = await start(sandbox, windowed(ready))
    const answers = await play(sandbox, careless, [
      { cmd: "render", state: 1, noise: 500, message: "lighting settled" },
      capture("on"),
      capture("on"), // the same state again: no new message is owed
      { cmd: "render", state: 0 }, // the toggle logged nothing
      capture("off"),
    ])
    const { code, report } = await stop(sandbox, careless)
    assertEquals(code, 1)
    const failed = oracle(report, "diff_vs_null: on")
    assertEquals(failed.result, "fail")
    assertEquals(failed.first_fail?.index, answers[4].id)
    assert(failed.first_fail!.why.includes("lighting settled"), failed.first_fail!.why)

    const patient = await start(sandbox, windowed(ready))
    await play(sandbox, patient, [
      { cmd: "render", state: 1, noise: 500, message: "lighting settled" },
      capture("on"),
      capture("on"),
      { cmd: "render", state: 0, message: "lighting settled" },
      capture("off"),
    ])
    const ok = await stop(sandbox, patient)
    assertEquals(ok.code, 0, JSON.stringify(ok.report))
    assertEquals(oracle(ok.report, "diff_vs_null: on").result, "pass")
  })
})

Deno.test("an oracle missing its captures is inconclusive while the Episode runs and fails when it ends", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, windowed(TRIPLET))
    await play(sandbox, session, [capture("on")])
    const running = await stop(sandbox, session, "report")
    assertEquals(oracle(running.report, "triplet: on").result, "inconclusive")
    assertEquals(running.report.exit_code, 3)
    const { code, report } = await stop(sandbox, session)
    assertEquals(code, 1)
    const failed = oracle(report, "triplet: on")
    assertEquals(failed.result, "fail")
    assert(failed.first_fail!.why.includes("needs"), failed.first_fail!.why)
  })
})

Deno.test("a warn capture oracle is reported without changing the exit code", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(
      sandbox,
      windowed(`[[oracle]]
kind = "paired_null"
original = "on"
severity = "warn"
max_noise = 0.001
`),
    )
    await play(sandbox, session, [{ cmd: "render", noise: 30000 }, capture("on"), capture("on")])
    const { code, report } = await stop(sandbox, session)
    assertEquals(oracle(report, "paired_null: on").result, "warn")
    assertEquals(code, 0)
  })
})

Deno.test("a window size the game does not report fails the run with a clear message", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, windowed())
    const answers = await play(sandbox, session, [
      { cmd: "render", report_window: "800x600" },
      { cmd: "capture" },
    ])
    const { code, report } = await stop(sandbox, session)
    assertEquals(code, 1, JSON.stringify(report))
    assertEquals(report.verdict, "fail")
    const failed = oracle(report, "window_size")
    assertEquals(failed.result, "fail")
    assertEquals(failed.first_fail?.index, answers[1].id)
    assert(failed.first_fail!.why.includes("640x384"), failed.first_fail!.why)
    assert(failed.first_fail!.why.includes("800x600"), failed.first_fail!.why)
  })
})

Deno.test("a frame that is not the window times a whole scale fails the run", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, windowed())
    await play(sandbox, session, [{ cmd: "render", scale: 1.5 }, { cmd: "capture" }])
    const { code, report } = await stop(sandbox, session)
    assertEquals(code, 1)
    const failed = oracle(report, "window_size")
    assertEquals(failed.result, "fail")
    assert(failed.first_fail!.why.includes("whole scale"), failed.first_fail!.why)
  })
})

Deno.test("a windowless Trial has no window to check and cannot declare capture oracles", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, `fixture = "bairdford"\n`)
    const { report } = await stop(sandbox, session)
    assertEquals(report.oracles.some((o) => o.name === "window_size"), false)

    const refused = await sandbox.cli([
      "start",
      await sandbox.trial(
        `fixture = "bairdford"\n[[oracle]]\nkind = "paired_null"\noriginal = "a"`,
      ),
    ])
    assertEquals(refused.code, 2)
    assert(refused.stderr.includes("windowed"), refused.stderr)
  })
})

Deno.test("a capture tag must be a plain tag", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, windowed())
    const bad = await step(sandbox, session, { cmd: "capture", tag: "two words" })
    assertEquals(bad.status, "error")
    assert(String(bad.error).includes("tag"), JSON.stringify(bad))
    // The refusal took no capture.
    const { report } = await stop(sandbox, session)
    assertEquals(report.captures, undefined)
  })
})
