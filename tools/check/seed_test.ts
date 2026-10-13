import { assertEquals } from "@std/assert"
import { seedArguments, seedSourceDirs } from "./seed.ts"

const ENTRIES = [
  "box2d-src",
  "rmlui-src",
  "sdl3-src",
  "sdl3_shadercross-src",
  "download",
  "box2d-build",
]

Deno.test("only the -src directories are FetchContent sources", () => {
  assertEquals(seedSourceDirs(ENTRIES), [
    "box2d-src",
    "rmlui-src",
    "sdl3-src",
    "sdl3_shadercross-src",
  ])
})

Deno.test("each seeded dependency gets its uppercased source dir and the disconnect flag closes the list", () => {
  assertEquals(seedArguments("/lane/out/build/linux-fast/_deps", ENTRIES), [
    "-DFETCHCONTENT_SOURCE_DIR_BOX2D=/lane/out/build/linux-fast/_deps/box2d-src",
    "-DFETCHCONTENT_SOURCE_DIR_RMLUI=/lane/out/build/linux-fast/_deps/rmlui-src",
    "-DFETCHCONTENT_SOURCE_DIR_SDL3=/lane/out/build/linux-fast/_deps/sdl3-src",
    "-DFETCHCONTENT_SOURCE_DIR_SDL3_SHADERCROSS=/lane/out/build/linux-fast/_deps/sdl3_shadercross-src",
    "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
  ])
})

Deno.test("an empty seed directory still forbids the network", () => {
  assertEquals(seedArguments("/x", []), ["-DFETCHCONTENT_FULLY_DISCONNECTED=ON"])
})
