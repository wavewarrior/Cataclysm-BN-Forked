/**
 * The `fixture` commands end to end through the CLI and the daemon: `add` clones a save into the
 * fixture library, `baseline` boots the fixture on the mock driver and records the game-log lines
 * logged after readiness, `list` shows whether each baseline still matches its fixture.
 */
import { assert, assertEquals, assertNotEquals } from "@std/assert"
import { join } from "@std/path"
import {
  eventually,
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

type Entry = {
  fixture: string
  baseline: "missing" | "fresh" | "stale"
  message?: string
  stale?: string[]
  lines?: number
}

async function list(sandbox: Sandbox): Promise<Entry[]> {
  const res = await sandbox.cli(["fixture", "list"])
  assertEquals(res.code, 0, res.stderr)
  return jsonOut<{ fixtures: Entry[] }>(res).fixtures
}

Deno.test("fixture list shows nothing for an empty library", async () => {
  await withSave(async (sandbox) => {
    assertEquals(await list(sandbox), [])
  })
})

Deno.test("fixture list reports a fixture without a baseline as missing, not as empty", async () => {
  await withSave(async (sandbox, save) => {
    assertEquals((await sandbox.cli(["fixture", "add", save, "second"])).code, 0)
    assertEquals((await sandbox.cli(["fixture", "add", save, "first"])).code, 0)
    // Files and hidden directories in the library are not fixtures.
    await Deno.writeTextFile(join(sandbox.fixtures, "notes.txt"), "not a fixture\n")

    const entries = await list(sandbox)
    assertEquals(entries.map((e) => e.fixture), ["first", "second"])
    for (const entry of entries) {
      assertEquals(entry.baseline, "missing")
      assertEquals(entry.lines, undefined)
      assert(
        entry.message?.includes(`fixture baseline ${entry.fixture}`),
        `the message must say how to capture one: ${entry.message}`,
      )
    }
    assertEquals(await pidsMatching(sandbox.home), [])
  })
})

/** What the mock game logs (see mock_driver.py): while booting, once idle, while shutting down. */
type LogScript = { boot?: string[]; idle?: string[]; quit?: string[]; noLog?: boolean }

async function makeLoggedWorld(script: LogScript): Promise<string> {
  const dir = await makeFakeWorld()
  const scripted: [string, string[] | undefined][] = [
    ["mock_log_boot.txt", script.boot],
    ["mock_log_idle.txt", script.idle],
    ["mock_log_quit.txt", script.quit],
  ]
  for (const [file, lines] of scripted) {
    if (lines) await Deno.writeTextFile(join(dir, file), lines.join("\n") + "\n")
  }
  if (script.noLog) await Deno.writeTextFile(join(dir, "mock_log_none"), "")
  return dir
}

const NOISY: LogScript = {
  boot: ["ERROR : boot.cpp:1 [load] logged while booting"],
  idle: [
    "ERROR DEBUGMSG : tick.cpp:2 [tick] first idle noise",
    "WARNING : tick.cpp:3 [tick] second idle noise",
  ],
  quit: ["ERROR : exit.cpp:4 [bye] logged while shutting down"],
}

/** A sandbox whose games idle briefly for a baseline, beside a world save that logs `script`. */
async function withLoggedSave(
  script: LogScript,
  body: (sandbox: Sandbox, save: string) => Promise<void>,
  env: Record<string, string> = {},
): Promise<void> {
  const save = await makeLoggedWorld(script)
  const sandbox = await makeSandbox({ env: { BNPLAY_BASELINE_IDLE_MS: "700", ...env } })
  try {
    await body(sandbox, save)
  } finally {
    await sandbox.cleanup()
    await Deno.remove(save, { recursive: true })
  }
}

type Baselined = {
  fixture: string
  baseline: string
  lines: number
  sample: string[]
  path: string
  log: string
  idle_ms: number
}

async function baseline(sandbox: Sandbox, fixture: string, env?: Record<string, string>) {
  const res = await sandbox.cli(["fixture", "baseline", fixture], env)
  return { ...res, json: res.code === 0 ? jsonOut<Baselined>(res) : undefined }
}

async function addFixtureNamed(sandbox: Sandbox, save: string, name: string): Promise<void> {
  const res = await sandbox.cli(["fixture", "add", save, name])
  assertEquals(res.code, 0, res.stderr)
}

/** The line without the game's `HH:MM:SS.mmm` stamp, which differs on every run. */
function unstamped(line: string): string {
  return line.replace(/^\d\d:\d\d:\d\d\.\d{3} /, "")
}

Deno.test("fixture baseline records only the game-log lines logged after readiness", async () => {
  await withLoggedSave(NOISY, async (sandbox, save) => {
    await addFixtureNamed(sandbox, save, "noisy")
    const fixtureBefore = await treeSnapshot(join(sandbox.fixtures, "noisy"))

    const res = await baseline(sandbox, "noisy")
    assertEquals(res.code, 0, res.stderr)
    const recorded = res.json!
    assertEquals(recorded.fixture, "noisy")
    assertEquals(recorded.baseline, "fresh")
    assertEquals(recorded.lines, 2)
    assertEquals(recorded.sample.map(unstamped), NOISY.idle)

    // The game really did log all three kinds of line; only the idle ones were recorded.
    const gameLog = await Deno.readTextFile(recorded.log)
    for (const line of [...NOISY.boot!, ...NOISY.idle!, ...NOISY.quit!]) {
      assert(gameLog.includes(line), `the game log lacks ${line}`)
    }
    const record = JSON.parse(await Deno.readTextFile(recorded.path))
    assertEquals(record.lines.map(unstamped), NOISY.idle)

    // Capturing a baseline plays on a clone: the fixture itself is untouched, and no game is left.
    assertEquals(await treeSnapshot(join(sandbox.fixtures, "noisy")), fixtureBefore)
    assertEquals(await pidsMatching(sandbox.home), [])
    const [entry] = await list(sandbox)
    assertEquals(entry.baseline, "fresh")
    assertEquals(entry.lines, 2)
  })
})

Deno.test("a clean boot that logs nothing after readiness is an empty baseline, not a missing one", async () => {
  await withLoggedSave({ boot: NOISY.boot }, async (sandbox, save) => {
    await addFixtureNamed(sandbox, save, "quiet")
    assertEquals((await list(sandbox))[0].baseline, "missing")

    const res = await baseline(sandbox, "quiet")
    assertEquals(res.code, 0, res.stderr)
    assertEquals(res.json!.lines, 0)
    assertEquals(res.json!.baseline, "fresh")
    const [entry] = await list(sandbox)
    assertEquals(entry.baseline, "fresh")
    assertEquals(entry.lines, 0)
    assertEquals(entry.message, undefined)
  })
})

Deno.test("fixture list flags a baseline stale once the fixture or its mod set changes", async () => {
  await withLoggedSave(NOISY, async (sandbox, save) => {
    await addFixtureNamed(sandbox, save, "aging")
    assertEquals((await baseline(sandbox, "aging")).code, 0)
    const only = async () => (await list(sandbox))[0]
    assertEquals((await only()).baseline, "fresh")

    // The fixture changes: the baseline no longer describes it.
    const player = join(sandbox.fixtures, "aging", "player.sav")
    const original = await Deno.readTextFile(player)
    await Deno.writeTextFile(player, original + "a later save\n")
    const changed = await only()
    assertEquals(changed.baseline, "stale")
    assertEquals(changed.stale?.length, 1)
    assert(changed.stale![0].includes("fixture"), changed.stale![0])
    assert(changed.message?.includes("fixture baseline aging"), changed.message)

    // Put the bytes back and the baseline describes the fixture again.
    await Deno.writeTextFile(player, original)
    assertEquals((await only()).baseline, "fresh")

    // The mod set changes: stale, and the reason names the mod set.
    const mods = join(sandbox.fixtures, "aging", "mods.json")
    await Deno.writeTextFile(mods, '["dda"]\n')
    const modded = await only()
    assertEquals(modded.baseline, "stale")
    assertEquals(modded.stale?.length, 1)
    assert(modded.stale![0].includes("mod set"), modded.stale![0])
    assert(modded.stale![0].includes("dda"), modded.stale![0])

    // Both at once name both; an empty mod list is the same as having no mods.json.
    await Deno.writeTextFile(player, original + "again\n")
    assertEquals((await only()).stale?.length, 2)
    await Deno.writeTextFile(player, original)
    await Deno.remove(mods)
    assertEquals((await only()).baseline, "fresh")
    await Deno.writeTextFile(mods, "[ ]\n")
    assertEquals((await only()).baseline, "fresh", "an empty mod list is the same as no mods.json")

    // Refreshing records a new baseline against the changed fixture.
    await Deno.writeTextFile(player, original + "kept\n")
    assertEquals((await only()).baseline, "stale")
    assertEquals((await baseline(sandbox, "aging")).code, 0)
    assertEquals((await only()).baseline, "fresh")
  })
})

Deno.test("Finder's .DS_Store in a fixture does not make its baseline stale", async () => {
  await withLoggedSave(NOISY, async (sandbox, save) => {
    await addFixtureNamed(sandbox, save, "browsed")
    assertEquals((await baseline(sandbox, "browsed")).code, 0)
    const world = join(sandbox.fixtures, "browsed")
    await Deno.writeTextFile(join(world, ".DS_Store"), "viewed in Finder\n")
    await Deno.writeTextFile(join(world, "maps", ".DS_Store"), "viewed in Finder\n")
    assertEquals((await list(sandbox))[0].baseline, "fresh")
  })
})

Deno.test("fixture baseline fails clearly and records nothing when it cannot get one", async () => {
  await withLoggedSave(NOISY, async (sandbox) => {
    const unknown = await baseline(sandbox, "nothing-here")
    assertEquals(unknown.code, 2)
    assert(unknown.stderr.includes("nothing-here"), unknown.stderr)

    // The game wrote no debug.log, so there is nothing to base a baseline on.
    const silent = await makeLoggedWorld({ noLog: true })
    try {
      await addFixtureNamed(sandbox, silent, "silent")
      const res = await baseline(sandbox, "silent")
      assertEquals(res.code, 2)
      assert(res.stderr.includes("debug.log"), res.stderr)
      assertEquals((await list(sandbox))[0].baseline, "missing")
    } finally {
      await Deno.remove(silent, { recursive: true })
    }
    assertEquals(await pidsMatching(sandbox.home), [])
  })
})

Deno.test("a game that dies at boot leaves the previous baseline in place", async () => {
  await withLoggedSave(NOISY, async (sandbox, save) => {
    await addFixtureNamed(sandbox, save, "kept")
    assertEquals((await baseline(sandbox, "kept")).code, 0)

    // A daemon whose game binary dies at boot.
    await sandbox.cli(["shutdown"])
    const res = await baseline(sandbox, "kept", { BNPLAY_BINARY: "/usr/bin/false" })
    assertEquals(res.code, 2)
    assert(res.stderr.includes("boot"), res.stderr)
    const [entry] = await list(sandbox)
    assertEquals(entry.baseline, "fresh")
    assertEquals(entry.lines, 2)
    assertEquals(await pidsMatching(sandbox.home), [])
  })
})

Deno.test("a fixture that changes while its baseline is captured gets no baseline", async () => {
  await withLoggedSave(NOISY, async (sandbox, save) => {
    await addFixtureNamed(sandbox, save, "moving")
    const capturing = baseline(sandbox, "moving")
    // Wait until the game runs on its clone, then edit the fixture under it.
    const episodes = join(sandbox.home, "episodes")
    const cloned = async () => {
      try {
        for await (const e of Deno.readDir(episodes)) {
          // Each Episode plays on a world of its own in its user directory.
          const saves = join(episodes, e.name, "userdir", "save")
          for await (const _world of Deno.readDir(saves)) return true
        }
      } catch { /* no Episode directory yet */ }
      return false
    }
    assert(await eventually(cloned, 20_000), "the baseline never started a game")
    await Deno.writeTextFile(join(sandbox.fixtures, "moving", "player.sav"), "edited meanwhile\n")

    const res = await capturing
    assertEquals(res.code, 2)
    assert(res.stderr.includes("changed"), res.stderr)
    assertEquals((await list(sandbox))[0].baseline, "missing")
  }, { MOCK_BOOT_DELAY_S: "2" })
})

Deno.test("a baseline run counts against the session cap and is refused beyond it", async () => {
  await withLoggedSave(NOISY, async (sandbox, save) => {
    await addFixtureNamed(sandbox, save, "capped")
    const trial = await sandbox.trial(`fixture = "capped"\n`)
    const started = await sandbox.cli(["start", trial])
    assertEquals(started.code, 0, started.stderr)

    const res = await baseline(sandbox, "capped")
    assertEquals(res.code, 2)
    assert(res.stderr.includes("session limit"), res.stderr)
    assertEquals((await list(sandbox))[0].baseline, "missing")

    assertEquals((await sandbox.cli(["stop", jsonOut(started).session as string])).code, 0)
    assertEquals((await baseline(sandbox, "capped")).code, 0)
    assertEquals(await pidsMatching(sandbox.home), [])
  }, { BNPLAY_MAX_SESSIONS: "1" })
})
