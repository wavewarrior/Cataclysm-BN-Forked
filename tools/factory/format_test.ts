import { assertEquals } from "@std/assert"
import { formatScript, formatTargets } from "./format.ts"

Deno.test("formatTargets gives src subdirectories and tests to clang-format, and src root files to astyle", () => {
  assertEquals(
    formatTargets([
      "src/lighting/bloom.cpp",
      "src/turn_plan.cpp",
      "src/turn_plan.h",
      "tests/turn_plan_test.cpp",
      "tools/format/fmt.h",
      "tools/clang-tidy-plugin/TextStyleCheck.cpp",
    ]),
    {
      clang: [
        "src/lighting/bloom.cpp",
        "tests/turn_plan_test.cpp",
        "tools/format/fmt.h",
        "tools/clang-tidy-plugin/TextStyleCheck.cpp",
      ],
      astyle: ["src/turn_plan.cpp", "src/turn_plan.h"],
    },
  )
})

Deno.test("formatTargets skips vendored, generated and non-C++ files", () => {
  assertEquals(
    formatTargets([
      "src/lua/api.cpp",
      "src/sol/sol.h",
      "src/third-party/x.h",
      "tests/catch/catch.hpp",
      "tools/clang-tidy-plugin/test/case.cpp",
      "src/fmtlib_format.h",
      "src/minicoro.h",
      "src/x.hpp",
      "docs/a.md",
    ]),
    { clang: [], astyle: [] },
  )
})

Deno.test("formatScript runs only the tools that have files and quotes paths for bash", () => {
  assertEquals(
    formatScript("/mnt/c/a b", { clang: ["src/x/it's.cpp"], astyle: [] }),
    "cd '/mnt/c/a b' && clang-format -i 'src/x/it'\\''s.cpp'",
  )
  assertEquals(formatScript("/mnt/c/x", { clang: [], astyle: [] }), "cd '/mnt/c/x'")
  assertEquals(
    formatScript("/mnt/c/x", { clang: ["tests/a.cpp"], astyle: ["src/b.cpp"] }),
    "cd '/mnt/c/x' && clang-format -i 'tests/a.cpp' && astyle --options=.astylerc -n 'src/b.cpp'",
  )
})
