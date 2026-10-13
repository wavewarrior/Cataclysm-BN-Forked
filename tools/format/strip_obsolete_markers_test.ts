import { assertEquals } from "@std/assert"
import { stripText } from "./strip_obsolete_markers.ts"

Deno.test("stripText drops whole-line NOPAD markers and blank lines survive", () => {
  const source = [
    "int foo() {}",
    "// *NOPAD*",
    "",
    "int bar() {} // *NOPAD*",
    "",
  ].join("\n")
  const { text, removals } = stripText(source)
  assertEquals(removals, 2)
  assertEquals(text, ["int foo() {}", "", "int bar() {}", ""].join("\n"))
})

Deno.test("stripText trims trailing NOLINT comments naming deleted checks", () => {
  const { text, removals } = stripText("  int p; //NOLINT(cata-xy)")
  assertEquals(removals, 1)
  assertEquals(text, "  int p;")
})

Deno.test("stripText drops whole-line NOLINT comments", () => {
  const { text, removals } = stripText(
    ["void f();", "// NOLINTNEXTLINE(cata-use-point-apis)"].join("\n"),
  )
  assertEquals(removals, 1)
  assertEquals(text, "void f();")
})

Deno.test("stripText keeps surviving names in mixed lists", () => {
  const { text, removals } = stripText(
    "int x; // NOLINTNEXTLINE(readability-magic-numbers, cata-text-style)",
  )
  assertEquals(removals, 1)
  assertEquals(text, "int x; // NOLINTNEXTLINE(readability-magic-numbers)")
})

Deno.test("stripText keeps the three surviving cata checks", () => {
  const source = [
    "int a; // NOLINT(cata-determinism)",
    "int b; // NOLINT(cata-no-long)",
    "int c; // NOLINT(cata-test-filename)",
  ].join("\n")
  const { text, removals } = stripText(source)
  assertEquals(removals, 0)
  assertEquals(text, source)
})

Deno.test("stripText leaves unrelated NOLINTs and prose untouched", () => {
  const source = [
    "int x; // NOLINT(readability-function-cognitive-complexity)",
    "// the cata-xy check was removed in 2026",
  ].join("\n")
  const { text, removals } = stripText(source)
  assertEquals(removals, 0)
  assertEquals(text, source)
})

Deno.test("stripText rewrites a real temp file end to end", async () => {
  const dir = await Deno.makeTempDir()
  try {
    const file = `${dir}/sample.cpp`
    const original = [
      "#include <string>",
      "std::string &f() // *NOPAD*",
      "{",
      "    return s; // NOLINT(cata-use-point-apis)",
      "}",
      "",
    ].join("\n")
    await Deno.writeTextFile(file, original)
    const { text, removals } = stripText(original)
    assertEquals(removals, 2)
    await Deno.writeTextFile(file, text)
    assertEquals(
      await Deno.readTextFile(file),
      [
        "#include <string>",
        "std::string &f()",
        "{",
        "    return s;",
        "}",
        "",
      ].join("\n"),
    )
  } finally {
    await Deno.remove(dir, { recursive: true })
  }
})
