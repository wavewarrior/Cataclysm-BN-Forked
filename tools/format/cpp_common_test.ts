import { assertEquals } from "@std/assert"
import { isFormattableCpp, mergeRanges, parseHunkRanges } from "./cpp_common.ts"

Deno.test("parseHunkRanges reads +side ranges of a -U0 diff and merges neighbours", () => {
  const diff = [
    "--- a/src/foo.cpp",
    "+++ b/src/foo.cpp",
    "@@ -10 +10,2 @@ int f()",
    "+int a;",
    "+int b;",
    "@@ -20,2 +22 @@ void g()", // pure deletion: nothing survives
    "@@ -40 +50,3 @@",
    "+int c;",
    "+int d;",
    "+int e;",
  ].join("\n")
  // 10-11 and 22 are adjacent after merge? 10-11 then 22: gap -> separate; 50-52 separate.
  assertEquals(parseHunkRanges(diff), [[10, 11], [22, 22], [50, 52]])
})

Deno.test("parseHunkRanges treats a missing count as one line", () => {
  assertEquals(parseHunkRanges("@@ -1 +5 @@"), [[5, 5]])
})

Deno.test("mergeRanges folds overlapping and adjacent intervals", () => {
  assertEquals(mergeRanges([[9, 12], [1, 3], [4, 5], [20, 22]]), [[1, 5], [9, 12], [20, 22]])
})

Deno.test("isFormattableCpp keeps project C++ and skips vendored trees", () => {
  assertEquals(isFormattableCpp("src/map.cpp"), true)
  assertEquals(isFormattableCpp("tests/map_test.cpp"), true)
  assertEquals(isFormattableCpp("tools/format/format.cpp"), true)
  assertEquals(isFormattableCpp("src/lua/lapi.cpp"), false)
  assertEquals(isFormattableCpp("src/sol/sol.hpp"), false)
  assertEquals(isFormattableCpp("tests/catch/catch_all.cpp"), false)
  assertEquals(isFormattableCpp("tools/clang-tidy-plugin/test/determinism.cpp"), false)
  assertEquals(isFormattableCpp("data/json/foo.json"), false)
})
