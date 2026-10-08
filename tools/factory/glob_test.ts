import { assert, assertEquals } from "@std/assert"
import { globToRegExp, matchesAny, matchingGlobs, normalizePath } from "./glob.ts"

Deno.test("** matches nested paths and a bare directory prefix match requires a separator", () => {
  assert(matchesAny("tools/factory/gate.ts", ["tools/factory/**"]))
  assert(matchesAny("tools/factory/wsl/lane.sh", ["tools/factory/**"]))
  assert(!matchesAny("tools/factory-other/x.ts", ["tools/factory/**"]))
  assert(!matchesAny("tools/bnplay/x.ts", ["tools/factory/**"]))
})

Deno.test("literal globs match only the exact file and escape regex metacharacters", () => {
  assert(matchesAny(".clang-tidy", [".clang-tidy"]))
  assert(!matchesAny("xclang-tidy", [".clang-tidy"]))
  assert(!matchesAny(".clang-tidy.bak", [".clang-tidy"]))
  assert(!matchesAny("src/.clang-tidy", [".clang-tidy"]))
})

Deno.test("* stays inside one segment and **/ may match zero segments", () => {
  assert(matchesAny("src/a.cpp", ["src/*.cpp"]))
  assert(!matchesAny("src/lighting/a.cpp", ["src/*.cpp"]))
  assert(matchesAny("a.cpp", ["**/*.cpp"]))
  assert(matchesAny("src/lighting/a.cpp", ["**/*.cpp"]))
})

Deno.test("paths are normalised from windows separators and ./ prefixes", () => {
  assertEquals(normalizePath(".\\tools\\factory\\x.ts"), "tools/factory/x.ts")
  assert(matchesAny("tools\\factory\\x.ts", ["tools/factory/**"]))
  assert(matchesAny("./deno.jsonc", ["deno.jsonc"]))
})

Deno.test("matchingGlobs reports every glob that matched", () => {
  assertEquals(
    matchingGlobs("tools/clang-tidy-plugin/x.cpp", [
      "tools/clang-tidy-plugin/**",
      "tools/**",
      "src/**",
    ]),
    ["tools/clang-tidy-plugin/**", "tools/**"],
  )
  assertEquals(globToRegExp("a.b").test("axb"), false)
})
