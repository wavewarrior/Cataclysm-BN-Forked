import { assertRejects } from "@std/assert"
import { runGate } from "./gate.ts"

Deno.test("a mistyped --only step is an error, never a silent pass", async () => {
  await assertRejects(
    () => runGate({ cwd: ".", tier: "fast", base: "origin/x", only: ["deno-check"] }),
    Error,
    "unknown gate step(s): deno-check",
  )
})
