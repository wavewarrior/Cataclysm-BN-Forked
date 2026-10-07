/**
 * The `compare` operation end to end through the daemon against the mock driver: frames written by
 * real `capture` calls of windowed Episodes, compared through the CLI and the MCP tool, with the
 * verdict, the exit code and the refusals a caller can hit.
 */
import { assert, assertEquals } from "@std/assert"
import { initialize, startMcp } from "./mcp_testkit.ts"
import { capture, start, type Step, step, stop, windowed, withSandbox } from "./renderer_testkit.ts"
import type { Sandbox } from "./testkit.ts"

/** The frame a capture answer names. */
function frameOf(res: Step): string {
  const made: unknown = res.capture
  assert(typeof made === "object" && made !== null && "frame" in made, JSON.stringify(res))
  const { frame } = made
  assert(typeof frame === "string", JSON.stringify(res))
  return frame
}

/** Captures `n` frames of the current render state, oldest first. */
async function framesOf(sandbox: Sandbox, session: string, n: number): Promise<string[]> {
  const paths = []
  for (let i = 0; i < n; i++) paths.push(frameOf(await step(sandbox, session, capture(`s${i}`))))
  return paths
}

const flags = (name: string, files: string[]) => files.flatMap((f) => [`--${name}`, f])

/** What the operation prints, of the parts this test reads. */
type Gate = {
  verdict: string
  exit_code: number
  mask: { frames: number; px: number }
  worst: {
    changed_px: number
    pct: number
    max_delta: number
    bbox: number[] | null
    file: string
  }
}

/** Runs `bnplay compare` on these frames; returns the printed verdict and the exit code. */
async function compare(
  sandbox: Sandbox,
  base: string[],
  test: string[],
  extra: string[] = [],
): Promise<{ code: number; result: Gate }> {
  const res = await sandbox.cli([
    "compare",
    ...flags("base", base),
    ...flags("test", test),
    ...extra,
  ])
  return { code: res.code, result: JSON.parse(res.stdout) as Gate }
}

Deno.test("compare passes two Episodes of one binary and fails a planted change", async () => {
  await withSandbox(async (sandbox) => {
    // Episode A: three frames of the unchanged state, each a little noisy, as a real scene is.
    const first = await start(sandbox, windowed())
    await step(sandbox, first, { cmd: "render", noise: 40 })
    const base = await framesOf(sandbox, first, 3)
    const self = await compare(sandbox, base, [base[0]])
    assertEquals(self.code, 0, JSON.stringify(self.result))
    assertEquals(self.result.verdict, "pass")
    assertEquals(self.result.exit_code, 0)
    assertEquals(self.result.mask.frames, 3)
    assert(self.result.mask.px > 0, `the noise masked nothing: ${self.result.mask.px}`)
    assertEquals(self.result.worst.changed_px, 0, "a frame of the masked state is no change")
    await stop(sandbox, first)

    // Episode B: the same binary, the same state, a noisier frame pump.
    const second = await start(sandbox, windowed())
    await step(sandbox, second, { cmd: "render", noise: 60 })
    const calm = await framesOf(sandbox, second, 3)
    await stop(sandbox, second)
    const across = await compare(sandbox, base, calm)
    assertEquals(across.code, 0, JSON.stringify(across.result))
    assertEquals(across.result.verdict, "pass")
    assert(
      across.result.worst.changed_px <= 1500,
      `across two Episodes: ${across.result.worst.changed_px} changed pixels`,
    )

    // Episode C: the planted change. The render state goes up, so the left half of every frame
    // shifts by 100 of blue: half the frame, far beyond any noise.
    const third = await start(sandbox, windowed())
    await step(sandbox, third, { cmd: "render", noise: 60, state: 2 })
    const [planted] = await framesOf(sandbox, third, 1)
    const toggled = await compare(sandbox, base, [planted])
    assertEquals(toggled.code, 1, JSON.stringify(toggled.result))
    assertEquals(toggled.result.verdict, "fail")
    assertEquals(toggled.result.exit_code, 1)
    assert(
      toggled.result.worst.changed_px > 5000,
      `only ${toggled.result.worst.changed_px} changed`,
    )
    assert(toggled.result.worst.max_delta > 0, JSON.stringify(toggled.result.worst))
    assertEquals(toggled.result.worst.bbox?.length, 4)
    assert(toggled.result.worst.pct > 0)
    assertEquals(toggled.result.worst.file, planted)
    // The gate is a parameter: a threshold at the count itself passes.
    const generous = await compare(
      sandbox,
      base,
      [planted],
      ["--max-changed", String(toggled.result.worst.changed_px)],
    )
    assertEquals(generous.code, 0, JSON.stringify(generous.result))
    assertEquals(generous.result.verdict, "pass")
    await stop(sandbox, third)
  })
})

Deno.test("compare refuses a two-frame mask, a size mismatch and a missing argument", async () => {
  await withSandbox(async (sandbox) => {
    const session = await start(sandbox, windowed())
    const base = await framesOf(sandbox, session, 3)
    // The same window at half the scale: a final composite of a different size.
    await step(sandbox, session, { cmd: "render", scale: 1 })
    const [odd] = await framesOf(sandbox, session, 1)
    await stop(sandbox, session)

    const tooFew = await sandbox.cli([
      "compare",
      ...flags("base", base.slice(0, 2)),
      "--test",
      base[2],
    ])
    assertEquals(tooFew.code, 2, tooFew.stdout)
    assert(tooFew.stderr.includes("at least 3 frames of the unchanged state"), tooFew.stderr)

    const mismatch = await sandbox.cli(["compare", ...flags("base", base), "--test", odd])
    assertEquals(mismatch.code, 2, mismatch.stdout)
    assert(
      mismatch.stderr.includes("frames of different sizes are never compared"),
      mismatch.stderr,
    )

    const mcp = startMcp(sandbox)
    try {
      await initialize(mcp)
      const short = await mcp.call("compare", { base: base.slice(0, 2), test: [base[2]] })
      assertEquals(short.isError, true)
      assert(short.text.includes("at least 3 frames of the unchanged state"), short.text)
      const sized = await mcp.call("compare", { base, test: [odd] })
      assertEquals(sized.isError, true)
      assert(sized.text.includes("frames of different sizes are never compared"), sized.text)
      const missing = await mcp.call("compare", { base })
      assertEquals(missing.isError, true)
      assert(missing.text.includes("missing test"), missing.text)
    } finally {
      await mcp.close()
    }
  })
})
