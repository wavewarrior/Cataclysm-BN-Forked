import { assertEquals } from "@std/assert"
import { guardViolations, PROTECTED_PATHS } from "./guard.ts"

Deno.test("a subtree rule protects everything below it and nothing beside it", () => {
  assertEquals(guardViolations(["tools/check/main.ts"]), ["tools/check/main.ts"])
  assertEquals(guardViolations(["tools/check/deep/nested/x.ts"]), ["tools/check/deep/nested/x.ts"])
  assertEquals(guardViolations(["tools/checkmate/x.ts"]), [])
  assertEquals(guardViolations(["src/map.cpp", "tests/map_test.cpp"]), [])
})

Deno.test("a bare rule protects exactly one file", () => {
  assertEquals(guardViolations([".clang-format"]), [".clang-format"])
  assertEquals(guardViolations([".clang-format.bak"]), [])
  assertEquals(guardViolations(["src/.clang-tidy"]), [])
  assertEquals(guardViolations(["AGENTS.md"]), ["AGENTS.md"])
})

Deno.test("every layer that judges a lane is on the list", () => {
  for (
    const file of [
      "tools/lane/main.ts",
      ".github/workflows/ci.yml",
      ".githooks/pre-commit",
      ".clang-tidy",
      "tools/clang-tidy-plugin/Module.cpp",
    ]
  ) {
    assertEquals(guardViolations([file]), [file], `${file} must be protected`)
  }
  assertEquals(PROTECTED_PATHS.length, 8)
})

Deno.test("paths match whatever separator and case git reports", () => {
  assertEquals(guardViolations(["tools\\check\\main.ts"]), ["tools\\check\\main.ts"])
  assertEquals(guardViolations(["./Tools/Check/main.ts"]), ["./Tools/Check/main.ts"])
  assertEquals(guardViolations([".Github/Workflows/ci.yml"]), [".Github/Workflows/ci.yml"])
})

Deno.test("violations come back in the order git reported them", () => {
  assertEquals(
    guardViolations(["AGENTS.md", "src/a.cpp", "AGENTS.md", ".clang-tidy"]),
    ["AGENTS.md", "AGENTS.md", ".clang-tidy"],
  )
})
