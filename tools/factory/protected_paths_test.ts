import { assertEquals } from "@std/assert"
import { violations } from "./protected_paths.ts"

Deno.test("only files matching a protected glob are reported, with the glob that matched", () => {
  const globs = ["tools/factory/**", ".clang-tidy", "build-scripts/**"]
  assertEquals(
    violations(["src/a.cpp", ".clang-tidy", "tools/factory/gate.ts", "tools/bnplay/x.ts"], globs),
    [
      { file: ".clang-tidy", glob: ".clang-tidy" },
      { file: "tools/factory/gate.ts", glob: "tools/factory/**" },
    ],
  )
  assertEquals(violations([], globs), [])
})
