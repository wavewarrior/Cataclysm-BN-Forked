import { assertEquals } from "@std/assert"
import { findEntry, isExpired, parseQuarantine } from "./quarantine.ts"

const SOURCE = `
inline constexpr auto quarantine_entries = std::to_array<quarantine_entry>({
    {
        .name = "a case with \\"quotes\\" inside",
        .expires = "2026-10-27",
        .reason = "escapes unescape correctly",
    },
    {
        .name = "rolling_steering_turns_vehicle",
        .expires = "2026-10-20",
        .reason = "fails on some seeds",
    },
});
`

Deno.test("the C++ initializer list parses into name/expires/reason", () => {
  const entries = parseQuarantine(SOURCE)
  assertEquals(entries.length, 2)
  assertEquals(entries[0].name, 'a case with "quotes" inside')
  assertEquals(entries[0].expires, "2026-10-27")
  assertEquals(entries[1].name, "rolling_steering_turns_vehicle")
})

Deno.test("the real tests/quarantine.h parses", async () => {
  const source = await Deno.readTextFile("tests/quarantine.h")
  const entries = parseQuarantine(source)
  assertEquals(entries.length >= 1, true)
  assertEquals(entries.every((e) => /^\d{4}-\d{2}-\d{2}$/.test(e.expires)), true)
})

Deno.test("expiry is string order, the same rule split_by_quarantine uses", () => {
  const entry = { name: "x", expires: "2026-10-20", reason: "r" }
  assertEquals(isExpired(entry, "2026-10-21"), true)
  assertEquals(isExpired(entry, "2026-10-20"), false)
  assertEquals(isExpired(entry, "2026-10-19"), false)
})

Deno.test("lookup is by exact name", () => {
  const entries = parseQuarantine(SOURCE)
  assertEquals(findEntry(entries, "rolling_steering_turns_vehicle")?.expires, "2026-10-20")
  assertEquals(findEntry(entries, "rolling_steering"), undefined)
})
