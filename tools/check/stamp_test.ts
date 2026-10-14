import { assertEquals } from "@std/assert"
import { dirtyLines, stampedCommit } from "./stamp.ts"

const clean = { code: 0, headAtStart: "aaa", headAtEnd: "aaa", dirtyAtStart: [], dirtyAtEnd: [] }

Deno.test("a passing run on a clean tree stamps the commit it tested", () => {
  assertEquals(stampedCommit(clean), "aaa")
})

Deno.test("a failing run never stamps", () => {
  assertEquals(stampedCommit({ ...clean, code: 1 }), null)
})

Deno.test("a tree that was dirty before the build proves nothing about the commit", () => {
  assertEquals(stampedCommit({ ...clean, dirtyAtStart: ["?? src/new.cpp"] }), null)
})

Deno.test("auto-fix rewriting files during the run voids the stamp", () => {
  assertEquals(stampedCommit({ ...clean, dirtyAtEnd: [" M src/map.cpp"] }), null)
})

Deno.test("a HEAD that moved mid-run voids the stamp", () => {
  assertEquals(stampedCommit({ ...clean, headAtEnd: "bbb" }), null)
})

Deno.test("an unreadable HEAD never stamps", () => {
  assertEquals(stampedCommit({ ...clean, headAtStart: "", headAtEnd: "" }), null)
})

Deno.test("the run's own log directory does not count as dirty, an untracked source file does", () => {
  const porcelain = "?? .check/events.jsonl\n?? src/new.cpp\n M tests/a.cpp\n"
  assertEquals(dirtyLines(porcelain, ".check"), ["?? src/new.cpp", " M tests/a.cpp"])
})
