import { assertEquals } from "@std/assert"
import { advanceWatermark, parseWatermark, watermarkPath } from "./watermark.ts"

Deno.test("the watermark file is .sweep/<platform>.watermark", () => {
  assertEquals(watermarkPath("/repo/.sweep", "windows"), "/repo/.sweep/windows.watermark")
  assertEquals(watermarkPath("/repo/.sweep", "macos"), "/repo/.sweep/macos.watermark")
})

Deno.test("the watermark is the first sha-shaped line; junk reads as never-swept", () => {
  assertEquals(parseWatermark("abc1234def5678\nwritten by tools/sweep\n"), "abc1234def5678")
  assertEquals(parseWatermark("  abc1234  "), "abc1234")
  assertEquals(parseWatermark(""), null)
  assertEquals(parseWatermark("not a sha"), null)
  assertEquals(parseWatermark("zzc1234"), null)
  assertEquals(parseWatermark("abc12\nmore"), null)
})

Deno.test("a completed run advances to HEAD; an incomplete one never does", () => {
  assertEquals(advanceWatermark(true, "deadbeefcafe", null), "deadbeefcafe")
  assertEquals(advanceWatermark(true, "deadbeefcafe", "aabbccddee"), "deadbeefcafe")
  assertEquals(advanceWatermark(false, "deadbeefcafe", "aabbccddee"), null)
  assertEquals(advanceWatermark(false, "deadbeefcafe", null), null)
})

Deno.test("advancing to where the watermark already is writes nothing", () => {
  assertEquals(advanceWatermark(true, "deadbeef", "deadbeef"), null)
  assertEquals(advanceWatermark(true, "DEADBEEF", "deadbeef"), null)
})
