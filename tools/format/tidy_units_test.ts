import { assertEquals } from "@std/assert"
import { chooseUnit, compiledUnits } from "./tidy_units.ts"

const db = JSON.stringify([
  { file: "/repo/tests/test_main.cpp", directory: "/repo/out/build/x" },
  { file: "../../../../src/map.cpp", directory: "/repo/out/build/x/src" },
  { file: "/repo/out/build/x/_deps/sdl3/a.c", directory: "/repo/out/build/x" },
  { file: "/elsewhere/other.cpp", directory: "/elsewhere" },
])

Deno.test("compiled units are the repo-relative sources; a relative entry is resolved", () => {
  const units = compiledUnits(db, "/repo")
  assertEquals(units.has("tests/test_main.cpp"), true)
  assertEquals(units.has("src/map.cpp"), true)
  assertEquals(units.has("out/build/x/_deps/sdl3/a.c"), true)
  assertEquals(units.has("/elsewhere/other.cpp"), false)
})

Deno.test("an includer the build does not compile is skipped for one it does", () => {
  const compiled = new Set(["tests/test_main.cpp"])
  assertEquals(
    chooseUnit(["tests/quarantine_test.cpp", "tests/test_main.cpp"], compiled),
    "tests/test_main.cpp",
  )
})

Deno.test("no compiled candidate means no unit, not a guess", () => {
  assertEquals(chooseUnit(["tests/orphan_test.cpp"], new Set(["tests/test_main.cpp"])), null)
})

Deno.test("an unreadable database keeps the first candidate", () => {
  assertEquals(chooseUnit(["tests/a.cpp", "tests/b.cpp"], new Set()), "tests/a.cpp")
  assertEquals(chooseUnit([], new Set()), null)
})
