/**
 * The `fixture` commands end to end through the CLI and the daemon: `add` clones a save into the
 * fixture library, `baseline` boots the fixture on the mock driver and records the game-log lines
 * logged after readiness, `list` shows whether each baseline still matches its fixture.
 */
import { assert, assertEquals, assertNotEquals } from "@std/assert"
import { join } from "@std/path"
import {
  jsonOut,
  makeFakeWorld,
  makeSandbox,
  pidsMatching,
  type Sandbox,
  treeSnapshot,
} from "./testkit.ts"

/** Runs `body` in an empty sandbox (no fixtures yet) beside a fake world save to add. */
async function withSave(body: (sandbox: Sandbox, save: string) => Promise<void>): Promise<void> {
  const save = await makeFakeWorld()
  const sandbox = await makeSandbox()
  try {
    await body(sandbox, save)
  } finally {
    await sandbox.cleanup()
    await Deno.remove(save, { recursive: true })
  }
}

async function libraryEntries(sandbox: Sandbox): Promise<string[]> {
  const names: string[] = []
  for await (const entry of Deno.readDir(sandbox.fixtures)) names.push(entry.name)
  return names.sort()
}

Deno.test("fixture add clones a save into the library and never modifies the source", async () => {
  await withSave(async (sandbox, save) => {
    const before = await treeSnapshot(save)
    const res = await sandbox.cli(["fixture", "add", save])
    assertEquals(res.code, 0, res.stderr)

    const name = save.split("/").at(-1)!
    const added = jsonOut<{ fixture: string; path: string; baseline: string }>(res)
    assertEquals(added.fixture, name)
    assertEquals(added.path, join(sandbox.fixtures, name))
    assertEquals(added.baseline, "missing")
    assertEquals(await treeSnapshot(join(sandbox.fixtures, name)), before)
    assertEquals(await treeSnapshot(save), before)

    // A clone, not a link: writing into the fixture leaves the source alone.
    await Deno.writeTextFile(join(sandbox.fixtures, name, "player.sav"), "changed in the fixture\n")
    assertEquals(await treeSnapshot(save), before)
  })
})

Deno.test("fixture add takes the fixture name from its second argument and resolves a relative save", async () => {
  await withSave(async (sandbox, save) => {
    // The CLI runs in the sandbox directory, so a relative path must resolve against it.
    const relative = join(sandbox.dir, "local-save")
    await Deno.rename(save, relative)
    try {
      const res = await sandbox.cli(["fixture", "add", "local-save", "renamed"])
      assertEquals(res.code, 0, res.stderr)
      assertEquals(jsonOut(res).fixture, "renamed")
      assertEquals(await libraryEntries(sandbox), ["renamed"])
    } finally {
      await Deno.rename(relative, save)
    }
  })
})

Deno.test("fixture add refuses a name that is taken and leaves the fixture alone", async () => {
  await withSave(async (sandbox, save) => {
    assertEquals((await sandbox.cli(["fixture", "add", save, "taken"])).code, 0)
    await Deno.writeTextFile(join(sandbox.fixtures, "taken", "player.sav"), "edited\n")
    const before = await treeSnapshot(join(sandbox.fixtures, "taken"))

    const res = await sandbox.cli(["fixture", "add", save, "taken"])
    assertEquals(res.code, 2)
    assert(res.stderr.includes("taken") && res.stderr.includes("already"), res.stderr)
    assertEquals(await treeSnapshot(join(sandbox.fixtures, "taken")), before)
    assertEquals(await libraryEntries(sandbox), ["taken"])
  })
})

Deno.test("fixture add refuses what is not a world save, and adds nothing", async () => {
  await withSave(async (sandbox, save) => {
    const notASave = await Deno.makeTempDir({ prefix: "bnplay-notsave-" })
    try {
      await Deno.writeTextFile(join(notASave, "readme.txt"), "not a world\n")
      const empty = await sandbox.cli(["fixture", "add", notASave, "nope"])
      assertEquals(empty.code, 2)
      assert(empty.stderr.includes("worldoptions.json"), empty.stderr)

      const missing = await sandbox.cli([
        "fixture",
        "add",
        join(sandbox.dir, "no-such-dir"),
        "nope",
      ])
      assertEquals(missing.code, 2)
      assert(missing.stderr.includes("no-such-dir"), missing.stderr)

      for (const bad of ["../escape", ".hidden", "a/b", "with space"]) {
        const res = await sandbox.cli(["fixture", "add", save, bad])
        assertEquals(res.code, 2, `${bad}: ${res.stderr}`)
        assert(res.stderr.includes("name"), res.stderr)
      }
      assertEquals(await libraryEntries(sandbox), [])
      assertNotEquals((await treeSnapshot(save))["/worldoptions.json"], undefined)
    } finally {
      await Deno.remove(notASave, { recursive: true })
    }
  })
})
Deno.test("fixture add starts no game process", async () => {
  await withSave(async (sandbox, save) => {
    assertEquals((await sandbox.cli(["fixture", "add", save, "quiet"])).code, 0)
    assertEquals(await pidsMatching(sandbox.home), [])
  })
})
