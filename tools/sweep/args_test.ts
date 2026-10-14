import { assertEquals, assertThrows } from "@std/assert"
import { parseArgs } from "./args.ts"

Deno.test("a sweep names its platform and defaults the rest", () => {
  const options = parseArgs(["--platform", "windows"])
  assertEquals(options.platform, "windows")
  assertEquals(options.dryRun, false)
  assertEquals(options.runner, "real")
  assertEquals(options.since, undefined)
  assertEquals(options.fakeFailures, [])
})

Deno.test("the flags the contract promises all parse", () => {
  const options = parseArgs([
    "--platform",
    "macos",
    "--since",
    "abc1234def",
    "--dry-run",
    "--runner",
    "fake",
    "--fake-failures",
    "a case, another case",
    "--jobs",
    "8",
  ])
  assertEquals(options.platform, "macos")
  assertEquals(options.since, "abc1234def")
  assertEquals(options.dryRun, true)
  assertEquals(options.runner, "fake")
  assertEquals(options.fakeFailures, ["a case", "another case"])
  assertEquals(options.jobs, 8)
})

Deno.test("nonsense is refused, not guessed at", () => {
  assertThrows(() => parseArgs([]), Error, "--platform is required")
  assertThrows(() => parseArgs(["--platform", "linux"]), Error, "must be windows or macos")
  assertThrows(() => parseArgs(["--platform", "windows", "--since", "not a sha!!"]), Error, "sha")
  assertThrows(() => parseArgs(["--platform", "windows", "--bogus"]), Error, "unknown argument")
  assertThrows(() => parseArgs(["--platform"]), Error, "needs a value")
})
