import { assertEquals } from "@std/assert"
import { classify, parseLinuxFailures, selected } from "./classify.ts"
import type { CaseFailure, QuarantineEntry } from "./types.ts"

const failure = (name: string): CaseFailure => ({ name, excerpt: "boom", invocation: "suite" })
const quarantined: QuarantineEntry = {
  name: "flaky_thing",
  expires: "2026-10-27",
  reason: "flaky",
}
const expired: QuarantineEntry = { name: "old_thing", expires: "2026-10-01", reason: "ancient" }
const TODAY = "2026-10-13"

Deno.test("a failure Linux passed and Quarantine does not know is platform-only", () => {
  const verdicts = classify([failure("metal_only")], new Set(), [], TODAY)
  assertEquals(verdicts[0].verdict, "platform-only")
})

Deno.test("a failure Linux saw too is not platform divergence", () => {
  const verdicts = classify([failure("shared_pain")], new Set(["shared_pain"]), [], TODAY)
  assertEquals(verdicts[0].verdict, "fails-everywhere")
})

Deno.test("inside its window, Quarantine excuses the failure even when Linux passed it", () => {
  const verdicts = classify([failure("flaky_thing")], new Set(), [quarantined], TODAY)
  assertEquals(verdicts[0].verdict, "quarantined")
  assertEquals(verdicts[0].entry?.expires, "2026-10-27")
})

Deno.test("past its deadline and still failing: expired, not platform-only", () => {
  const verdicts = classify([failure("old_thing")], new Set(), [expired], TODAY)
  assertEquals(verdicts[0].verdict, "expired-quarantine")
})

Deno.test("an expired entry whose case passes on Linux is still reported expired when it fails here", () => {
  const verdicts = classify([failure("old_thing")], new Set(["other"]), [expired], TODAY)
  assertEquals(verdicts[0].verdict, "expired-quarantine")
  assertEquals(selected(verdicts, "platform-only").length, 0)
})

Deno.test("mixed batches sort into their buckets", () => {
  const verdicts = classify(
    [failure("metal_only"), failure("shared_pain"), failure("flaky_thing"), failure("old_thing")],
    new Set(["shared_pain"]),
    [quarantined, expired],
    TODAY,
  )
  assertEquals(selected(verdicts, "platform-only").map((v) => v.failure.name), ["metal_only"])
  assertEquals(selected(verdicts, "fails-everywhere").length, 1)
  assertEquals(selected(verdicts, "quarantined").length, 1)
  assertEquals(selected(verdicts, "expired-quarantine").length, 1)
})

Deno.test("the fork runner's FAILED: blocks are Linux failures", () => {
  const log = [
    "1459 cases in forked children, 2 failed, wall 110 s (fork 3 s)",
    "",
    "FAILED: vehicle_ramp_test_60",
    "  exit code 1",
    "  --- the child's output from its first failure line ---",
    "  something",
    "FAILED: tileset audit test",
    "  died on signal 11 (Segmentation fault)",
  ].join("\n")
  assertEquals([...parseLinuxFailures(log)].sort(), ["tileset audit test", "vehicle_ramp_test_60"])
})

Deno.test("the fork runner's exoneration lines do not count as Linux failures", () => {
  const log = [
    "Re-ran the 3 failures in fresh processes (seed 1): 1 confirmed.",
    "  CPU-path difference (fails on CPU compute, passes on default): rolling_steering_turns_vehicle",
    "  NOT REPRODUCED, flaky or fork-induced (seed 1): some_flaky_case",
    "FAILED: rolling_steering_turns_vehicle",
    "FAILED: some_flaky_case",
    "FAILED: real_failure",
  ].join("\n")
  assertEquals([...parseLinuxFailures(log)], ["real_failure"])
})

Deno.test("the Catch2 console layout counts too", () => {
  const log = [
    "-------------------------------------------------------------------------------",
    "avatar can walk inside stopped vehicle",
    "-------------------------------------------------------------------------------",
    "C:\\repo\\tests\\avatar_test.cpp(42): FAILED:",
    "  REQUIRE( here->is_on_tile() )",
    "with expansion:",
    "  false",
  ].join("\n")
  assertEquals([...parseLinuxFailures(log)], ["avatar can walk inside stopped vehicle"])
})
