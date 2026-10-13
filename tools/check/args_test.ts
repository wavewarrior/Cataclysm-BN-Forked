import { assertEquals, assertThrows } from "@std/assert"
import { parseArgs } from "./args.ts"
import { DEFAULT_BASE } from "./base.ts"

const PLAIN = { noFix: false, dryRun: false, help: false }

Deno.test("no arguments means the defaults: fix on, base unset, help off", () => {
  assertEquals(parseArgs([]), PLAIN)
  assertEquals(DEFAULT_BASE, "origin/feature/improvements")
})

Deno.test("flags parse independently of order and a value flag consumes its value", () => {
  assertEquals(parseArgs(["--no-fix", "--base", "origin/main"]), {
    ...PLAIN,
    baseFlag: "origin/main",
    noFix: true,
  })
  assertEquals(parseArgs(["--jobs", "8", "--dry-run"]), {
    ...PLAIN,
    jobs: 8,
    dryRun: true,
  })
  assertEquals(parseArgs(["-h"]).help, true)
})

Deno.test("a value flag without a usable value is rejected", () => {
  assertThrows(() => parseArgs(["--base"]), Error, "--base needs a value")
  assertThrows(() => parseArgs(["--base", "--no-fix"]), Error, "--base needs a value")
  assertThrows(() => parseArgs(["--jobs", "0"]), Error, "positive integer")
  assertThrows(() => parseArgs(["--jobs", "many"]), Error, "positive integer")
})

Deno.test("an unknown argument is rejected rather than ignored", () => {
  assertThrows(() => parseArgs(["--tier", "fast"]), Error, "unknown argument: --tier")
})
