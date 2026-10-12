import { assertEquals } from "@std/assert"
import { checkToolVersions, formatCommitFiles, formatScript, formatTargets } from "./format.ts"

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

Deno.test("checkToolVersions accepts the pinned astyle and clang-format major, and rejects anything else", () => {
  const pinned = { astyle: "Artistic Style Version 3.6.16\n", clangFormat: "Ubuntu clang-format version 22.1.3" }
  assertEquals(checkToolVersions(pinned), undefined)
  assertEquals(
    checkToolVersions({ ...pinned, astyle: "Artistic Style Version 3.6.15" })?.includes("need"),
    true,
  )
  assertEquals(
    checkToolVersions({ ...pinned, clangFormat: "Ubuntu clang-format version 21.1.8" })?.includes("major 22"),
    true,
  )
  assertEquals(checkToolVersions({ ...pinned, clangFormat: "" })?.includes("need major 22"), true)
})

Deno.test("formatCommitFiles keeps only modified files the formatter targets", () => {
  const targets = formatTargets(["src/a/b.cpp", "src/turn_plan.cpp", "docs/x.md"])
  assertEquals(
    formatCommitFiles(["src/a/b.cpp", "docs/x.md", "src/other/untouched.txt", "src/turn_plan.cpp"], targets),
    ["src/a/b.cpp", "src/turn_plan.cpp"],
  )
  assertEquals(formatCommitFiles(["docs/x.md"], targets), [])
})
