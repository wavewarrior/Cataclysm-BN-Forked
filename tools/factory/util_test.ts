import { assert, assertEquals } from "@std/assert"
import { join } from "@std/path"
import { runToLogUntil } from "./util.ts"

Deno.test("runToLogUntil kills a long run at the first matching line", async () => {
  const dir = await Deno.makeTempDir()
  try {
    const script =
      "console.log('start'); await new Promise((r) => setTimeout(r, 300)); console.log('FAIL x'); await new Promise((r) => setTimeout(r, 60000)); console.log('la' + 'te')"
    const t0 = Date.now()
    const r = await runToLogUntil(
      ["deno", "eval", script],
      join(dir, "out.log"),
      (line) => line.startsWith("FAIL "),
    )
    const log = await Deno.readTextFile(join(dir, "out.log"))
    assertEquals(r.stopped, true)
    assert(log.includes("FAIL x"), log)
    assert(!log.includes("late"), log)
    assert(Date.now() - t0 < 30_000, "should not wait for the 60 s sleep")
  } finally {
    await Deno.remove(dir, { recursive: true })
  }
})

Deno.test("runToLogUntil lets a clean run finish and reports its exit code", async () => {
  const dir = await Deno.makeTempDir()
  try {
    const r = await runToLogUntil(
      ["deno", "eval", "console.log('ok'); Deno.exit(3)"],
      join(dir, "out.log"),
      (line) => line.startsWith("FAIL "),
    )
    assertEquals(r, { code: 3, stopped: false })
  } finally {
    await Deno.remove(dir, { recursive: true })
  }
})
