import { assert, assertEquals, assertStringIncludes } from "@std/assert"
import { run, runTee, tail } from "./exec.ts"
import { testSummary } from "./main.ts"

/// These four tests spawn `sh` and write a temp log, so they are only declared when the runner was
/// granted the permissions: run them with `deno test -A tools/check tools/lane`. Bare
/// `deno test tools/check tools/lane` warns and runs the pure policy tests only.
const spawned = (
  await Promise.all(
    ["run", "env", "write"].map((name) =>
      Deno.permissions.query({ name: name as Deno.PermissionName })
    ),
  )
).every((status) => status.state === "granted")
if (!spawned) console.warn("exec_test: run/env/write not granted; the 4 IO tests are not declared")

const test = (name: string, fn: () => Promise<void>) => {
  if (spawned) Deno.test({ name, sanitizeResources: false, fn })
}

test("run captures both streams and reports a non-zero exit without throwing", async () => {
  const result = await run(["sh", "-c", "echo out; echo err >&2; exit 3"])
  assertEquals(result.code, 3)
  assertEquals(result.stdout.trim(), "out")
  assertEquals(result.stderr.trim(), "err")
})

test("an environment override of undefined removes the variable from the child", async () => {
  const shown = await run(["sh", "-c", 'printf "%s" "${PROBE_VAR-unset}"'], {
    env: { PROBE_VAR: undefined },
  })
  assertEquals(shown.stdout, "unset")
  const kept = await run(["sh", "-c", 'printf "%s" "${PROBE_VAR-unset}"'], {
    env: { PROBE_VAR: "kept" },
  })
  assertEquals(kept.stdout, "kept")
})

test("runTee returns the child's streams and appends them plus the command to the log", async () => {
  const dir = await Deno.makeTempDir({ prefix: "check_exec_" })
  try {
    const log = `${dir}/nested/build.log`
    const result = await runTee(["sh", "-c", "echo a; echo b >&2; exit 3"], log)
    assertEquals(result.code, 3)
    assertEquals(result.stdout.trim(), "a")
    assertEquals(result.stderr.trim(), "b")
    const written = await Deno.readTextFile(log)
    assertStringIncludes(written, "a")
    assertStringIncludes(written, "b")
    assertStringIncludes(written, "$ sh -c echo a; echo b >&2; exit 3")
    assert(written.trimEnd().endsWith("(exit 3)"), written)
  } finally {
    await Deno.remove(dir, { recursive: true })
  }
})

test("tail returns the last lines of a log or a note when there is none", async () => {
  const dir = await Deno.makeTempDir({ prefix: "check_tail_" })
  try {
    assertEquals(await tail(`${dir}/missing.log`, 3), `(no log at ${dir}/missing.log)`)
    await Deno.writeTextFile(`${dir}/x.log`, "one\n\ntwo\nthree\n")
    assertEquals(await tail(`${dir}/x.log`, 2), "two\nthree")
  } finally {
    await Deno.remove(dir, { recursive: true })
  }
})

Deno.test("the test summary prefers the fork runner's totals line over the last output line", () => {
  const forked = "Randomness seeded to: 1\n\n" +
    "1459 cases in forked children on the CPU compute path, 0 failed, wall 110 s (fork 28 s)\n"
  assertEquals(
    testSummary(forked, 0),
    "1459 cases in forked children on the CPU compute path, 0 failed, wall 110 s (fork 28 s) (exit 0)",
  )
  assertEquals(testSummary("boom\nAll tests failed\n", 1), "All tests failed (exit 1)")
  assertEquals(testSummary("", 2), "no output (exit 2)")
})
