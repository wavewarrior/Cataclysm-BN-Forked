import { assertEquals, assertThrows } from "@std/assert"
import { dirname, fromFileUrl, join } from "@std/path"
import { buildGraph, fanoutOf, parseIncludes, resolveInclude } from "./graph.ts"
import { parseArgs } from "./args.ts"
import {
  absolutize,
  matchNinjaTuSet,
  parseNinjaDeps,
  recordedRepoRoot,
  remapRecordedPath,
} from "./ninja.ts"

/// A small include graph exercising counts, a cycle, directory resolution, a self-include,
/// a commented-out include, an angle include and a missing include.
///
///   src/alpha.h  -> beta.h            src/beta.h -> alpha.h, gamma.h   (cycle alpha<->beta)
///   src/gamma.h  -> (nothing)         src/delta.h -> delta.h           (self-include)
///   src/a.cpp    -> alpha.h, nope.h   (nope.h is missing)
///   src/sub/b.cpp-> beta.h, local.h   (beta.h resolves via src/, local.h via the directory)
///   src/sub/local.h -> gamma.h
///   tests/t.cpp  -> gamma.h, <vector> and a commented-out include that must not count
const FIXTURE: Record<string, string> = {
  "src/alpha.h": '#pragma once\n#include "beta.h"\n',
  "src/beta.h": '#pragma once\n#include "alpha.h"\n#include "gamma.h"\n',
  "src/gamma.h": "#pragma once\n",
  "src/delta.h": '#pragma once\n#include "delta.h"\n',
  "src/a.cpp": '#include "alpha.h"\n#include "nope.h"\nint main() {}\n',
  "src/sub/b.cpp": '#include "beta.h"\n#include "local.h"\n',
  "src/sub/local.h": '#include "gamma.h"\n',
  "tests/t.cpp": '#include "gamma.h"\n#include <vector>\n// #include "gamma.h"\n',
}

function fixtureGraph() {
  return buildGraph(new Map(Object.entries(FIXTURE)))
}

Deno.test("the fixture walk counts TUs split src/tests, with direct includers and propagators", () => {
  const fanout = fanoutOf(fixtureGraph(), "src/gamma.h")
  assertEquals(fanout.tus, ["src/a.cpp", "src/sub/b.cpp", "tests/t.cpp"])
  assertEquals(fanout.srcTus, ["src/a.cpp", "src/sub/b.cpp"])
  assertEquals(fanout.testTus, ["tests/t.cpp"])
  assertEquals(fanout.directIncluders, ["src/beta.h", "src/sub/local.h", "tests/t.cpp"])
  assertEquals(fanout.propagators, ["src/alpha.h", "src/beta.h", "src/sub/local.h"])
})

Deno.test("the alpha<->beta cycle terminates and reaches each side once", () => {
  const fanout = fanoutOf(fixtureGraph(), "src/alpha.h")
  assertEquals(fanout.tus, ["src/a.cpp", "src/sub/b.cpp"])
  assertEquals(fanout.directIncluders, ["src/a.cpp", "src/beta.h"])
  const beta = fanoutOf(fixtureGraph(), "src/beta.h")
  assertEquals(beta.tus, ["src/a.cpp", "src/sub/b.cpp"])
})

Deno.test("a missing include is recorded, not fatal, and adds no edge", () => {
  const graph = fixtureGraph()
  assertEquals(graph.missing, [["src/a.cpp", "nope.h"]])
  assertEquals(graph.includes.get("src/a.cpp"), ["src/alpha.h"])
})

Deno.test("a self-include drops its own edge and reaches nobody", () => {
  const graph = fixtureGraph()
  assertEquals(graph.includes.get("src/delta.h"), [])
  assertEquals(fanoutOf(graph, "src/delta.h").tus, [])
})

Deno.test("parsing keeps quoted includes only, strips line comments, allows whitespace", () => {
  assertEquals(
    parseIncludes('#\tinclude\t"a.h"\n  # include "b.h"\n#include <c.h>\n// #include "d.h"\n'),
    ["a.h", "b.h"],
  )
})

Deno.test("resolution prefers the includer's directory, then src/, and honours .. segments", () => {
  const known = new Set(["src/dup.h", "src/sub/dup.h", "src/common/util.h", "src/x.h"])
  assertEquals(resolveInclude("dup.h", "src/sub/b.cpp", known), "src/sub/dup.h")
  assertEquals(resolveInclude("dup.h", "src/a.cpp", known), "src/dup.h")
  assertEquals(resolveInclude("../common/util.h", "src/sub/b.cpp", known), "src/common/util.h")
  assertEquals(resolveInclude("x.h", "src/sub/b.cpp", known), "src/x.h")
  assertEquals(resolveInclude("gone.h", "src/a.cpp", known), null)
})

Deno.test("argument parsing takes one header and the documented flags", () => {
  const args = parseArgs(["src/map.h", "--json"])
  assertEquals(args.header, "src/map.h")
  assertEquals(args.json, true)
  assertEquals(args.forceText, false)
  assertEquals(parseArgs(["--text", "--build-dir", "out/build/x", "./src/m.h"]).header, "src/m.h")
  assertEquals(parseArgs([]).header, null)
  assertEquals(parseArgs(["--help"]).help, true)
  assertEquals(parseArgs(["--porcelain", "src/m.h"]).porcelain, true)
  assertThrows(() => parseArgs(["--nope"]))
  assertThrows(() => parseArgs(["a.h", "b.h"]))
  assertThrows(() => parseArgs(["--root"]))
})

Deno.test("ninja deps output parses into objects with their dependency lines", () => {
  const stdout = [
    "src/CMakeFiles/app.dir/a.cpp.o: #deps 3, deps mtime 123 (VALID)",
    "    ../../src/map.h",
    "    /opt/sdk/header.h",
    "    ../../src/thing.h",
    "",
    "src/CMakeFiles/app.dir/b.cpp.o: #deps 1, deps mtime 0 (MISSING)",
    "tests/CMakeFiles/test.dir/c.cpp.o: #deps 1, deps mtime 9 (VALID)",
    "    ../../src/map.h",
  ].join("\n")
  const entries = parseNinjaDeps(stdout)
  assertEquals(entries.length, 3)
  assertEquals(entries[0].object, "src/CMakeFiles/app.dir/a.cpp.o")
  assertEquals(entries[0].deps, ["../../src/map.h", "/opt/sdk/header.h", "../../src/thing.h"])
  assertEquals(entries[1].deps, [])
  assertEquals(entries[2].object, "tests/CMakeFiles/test.dir/c.cpp.o")
})

Deno.test("a compile database from another checkout is remapped onto this one", () => {
  const db = [{
    directory: "/old/checkout/out/build/linux-fast",
    file: "/old/checkout/src/map.cpp",
    output: "/old/checkout/out/build/linux-fast/src/CMakeFiles/app.dir/map.cpp.o",
  }]
  assertEquals(recordedRepoRoot(db), "/old/checkout")
  assertEquals(
    remapRecordedPath(db[0].file, "/old/checkout", "/new/checkout"),
    "/new/checkout/src/map.cpp",
  )
  assertEquals(
    remapRecordedPath(db[0].output, "/old/checkout", "/new/checkout"),
    "/new/checkout/out/build/linux-fast/src/CMakeFiles/app.dir/map.cpp.o",
  )
  assertEquals(remapRecordedPath("/keep/me", "/old/checkout", "/new/checkout"), "/keep/me")
  assertEquals(recordedRepoRoot([{ directory: "/plain/build/dir", file: "x" }]), "/plain/build/dir")
})

Deno.test("the ninja matcher finds TUs through a foreign-checkout database", () => {
  const db = [
    {
      directory: "/old/checkout/out/build/preset",
      file: "/old/checkout/src/map.cpp",
      output: "/old/checkout/out/build/preset/src/CMakeFiles/app.dir/map.cpp.o",
    },
    {
      directory: "/old/checkout/out/build/preset",
      file: "/old/checkout/tests/map_test.cpp",
      output: "/old/checkout/out/build/preset/tests/CMakeFiles/test.dir/map_test.cpp.o",
    },
    {
      directory: "/old/checkout/out/build/preset",
      file: "/old/checkout/src/other.cpp",
      output: "/old/checkout/out/build/preset/src/CMakeFiles/app.dir/other.cpp.o",
    },
    { directory: "/old/checkout/out/build/preset", file: "/old/checkout/src/nolib.cpp" },
  ]
  const deps = parseNinjaDeps([
    "src/CMakeFiles/app.dir/map.cpp.o: #deps 2, deps mtime 1 (VALID)",
    "    ../../../src/map.h",
    "    /opt/sdk/thing.h",
    "tests/CMakeFiles/test.dir/map_test.cpp.o: #deps 1, deps mtime 1 (VALID)",
    "    /new/checkout/src/map.h",
    "src/CMakeFiles/app.dir/other.cpp.o: #deps 1, deps mtime 1 (VALID)",
    "    ../../../src/other.h",
  ].join("\n"))
  const tus = matchNinjaTuSet({
    deps,
    db,
    buildDir: "/new/checkout/out/build/preset",
    root: "/new/checkout",
    header: "src/map.h",
    sourcePrefixes: ["src/", "tests/"],
  })
  assertEquals([...tus].sort(), ["src/map.cpp", "tests/map_test.cpp"])
})

Deno.test("absolutize keeps roots, drives and escapes", () => {
  assertEquals(absolutize("../../src/map.h", "/bld/preset"), "/src/map.h")
  assertEquals(absolutize("a/b.o", "/bld"), "/bld/a/b.o")
  assertEquals(absolutize("/x/../y.h", "/bld"), "/y.h")
  assertEquals(absolutize("C:\\src\\map.h", "/bld"), "C:/src/map.h")
  assertEquals(absolutize("../../map.h", "C:/bld/preset"), "C:/map.h")
  assertEquals(absolutize("../up.h", "bld"), "up.h")
  assertEquals(absolutize("../../up.h", "bld"), "../up.h")
})

Deno.test("the CLI walks a fixture root textually and reports JSON", async () => {
  const dir = await Deno.makeTempDir({ prefix: "fanout_fixture_" })
  try {
    for (const [path, text] of Object.entries(FIXTURE)) {
      const full = join(dir, path)
      await Deno.mkdir(dirname(full), { recursive: true })
      await Deno.writeTextFile(full, text)
    }
    const main = join(dirname(fromFileUrl(import.meta.url)), "main.ts")
    const run = await new Deno.Command(Deno.execPath(), {
      args: ["run", "-A", main, "--root", dir, "--text", "--json", "src/gamma.h"],
      stdout: "piped",
      stderr: "piped",
    }).output()
    assertEquals(run.code, 0, new TextDecoder().decode(run.stderr))
    const report = JSON.parse(new TextDecoder().decode(run.stdout))
    assertEquals(report.source, "textual include walk")
    assertEquals(report.totals, { tus: 3, src: 2, tests: 1 })
    assertEquals(report.unresolvedIncludes, 1)
    assertEquals(report.tus, ["src/a.cpp", "src/sub/b.cpp", "tests/t.cpp"])

    const porcelain = await new Deno.Command(Deno.execPath(), {
      args: ["run", "-A", main, "--root", dir, "--text", "--porcelain", "src/gamma.h"],
      stdout: "piped",
      stderr: "piped",
    }).output()
    assertEquals(porcelain.code, 0, new TextDecoder().decode(porcelain.stderr))
    assertEquals(
      new TextDecoder().decode(porcelain.stdout).trim(),
      "src/gamma.h\t3\t2\t1\ttextual include walk",
    )
    const gone = await new Deno.Command(Deno.execPath(), {
      args: ["run", "-A", main, "--root", dir, "--text", "src/gone.h"],
      stdout: "piped",
      stderr: "piped",
    }).output()
    assertEquals(gone.code, 1)
  } finally {
    await Deno.remove(dir, { recursive: true })
  }
})
