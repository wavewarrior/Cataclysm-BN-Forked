/**
 * The fixture library: a gitignored directory holding one world save per fixture, each a
 * copy-on-write clone of a save the developer owns. A fixture directory IS the world: an Episode
 * clones it again into its own user directory, so neither the source save nor the fixture is ever
 * played in place.
 *
 * A fixture's baseline lives beside the library, not inside the world (see baseline.ts).
 */
import { createHash } from "node:crypto"
import { basename, join } from "@std/path"
import { cloneTree, HarnessError } from "./episode.ts"
import { FIXTURE_NAME } from "./trial.ts"

/** Files the game itself takes as proof that a directory is a world (worldfactory.cpp). */
const WORLD_MARKERS = ["worldoptions.json", "master.gsav"]

export type AddedFixture = {
  fixture: string
  path: string
  /** The save as it was cloned, symlinks resolved. */
  source: string
}

/**
 * Clones `source` (a world save directory) into the library as `name` (default: the save's own
 * directory name). The source is only ever read.
 */
export async function addFixture(
  fixtures: string,
  source: string,
  name?: string,
): Promise<AddedFixture> {
  const real = await Deno.realPath(source).catch(() => {
    throw new HarnessError(`save directory ${source} not found`)
  })
  if (!(await Deno.stat(real)).isDirectory) {
    throw new HarnessError(`${source} is not a directory: a fixture is cloned from a world save`)
  }
  const fixture = name ?? basename(real)
  if (!FIXTURE_NAME.test(fixture)) {
    throw new HarnessError(
      `fixture name \`${fixture}\` must be a plain name (letters, digits, \`.\`, \`_\`, \`-\`, ` +
        `starting with a letter or digit); pass one as the second argument`,
    )
  }
  const found = await Promise.all(
    WORLD_MARKERS.map((marker) => Deno.lstat(join(real, marker)).then(() => true, () => false)),
  )
  if (!found.includes(true)) {
    throw new HarnessError(
      `${source} does not look like a world save: it has no ${WORLD_MARKERS.join(" or ")}`,
    )
  }
  const path = join(fixtures, fixture)
  if (await Deno.lstat(path).then(() => true, () => false)) {
    throw new HarnessError(
      `fixture ${fixture} already exists in ${fixtures}; remove ${path} to replace it`,
    )
  }

  await Deno.mkdir(fixtures, { recursive: true })
  // Clone beside the final place and rename, so a failed or interrupted clone never leaves a
  // half-copied world that a Trial could pick up.
  const staging = join(fixtures, `.adding-${crypto.randomUUID().slice(0, 8)}`)
  try {
    await cloneTree(real, staging)
    await Deno.rename(staging, path)
  } catch (e) {
    await Deno.remove(staging, { recursive: true }).catch(() => undefined)
    throw new HarnessError(
      `cloning ${source} into the fixture library failed: ${(e as Error).message}`,
    )
  }
  return { fixture, path, source: real }
}

/** What a baseline is a baseline OF: the fixture's files and the mods its world loads. */
export type FixtureIdentity = {
  /**
   * Hash over every file of the world by path, size and content, apart from `mods.json` (see
   * `mods`) and the `.DS_Store` files Finder drops into any folder it shows.
   */
  digest: string
  /** The world's mod list in a canonical form: `[]` when `mods.json` is absent or empty. */
  mods: string
}

const MODS_FILE = "mods.json"

async function fileDigest(path: string): Promise<string> {
  const hash = createHash("sha256")
  const file = await Deno.open(path)
  try {
    // Streamed: a world's map database can be far bigger than is worth holding in memory.
    const chunk = new Uint8Array(1 << 20)
    for (let n = await file.read(chunk); n !== null; n = await file.read(chunk)) {
      hash.update(chunk.subarray(0, n))
    }
  } finally {
    file.close()
  }
  return hash.digest("hex")
}

async function describeTree(root: string, relative = ""): Promise<string[]> {
  const entries: Deno.DirEntry[] = []
  for await (const entry of Deno.readDir(join(root, relative))) {
    if (entry.name !== ".DS_Store") entries.push(entry)
  }
  entries.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0))
  const described: string[] = []
  for (const entry of entries) {
    const path = relative === "" ? entry.name : `${relative}/${entry.name}`
    const full = join(root, path)
    if (entry.isDirectory) {
      described.push(`dir ${path}`, ...await describeTree(root, path))
    } else if (entry.isSymlink) {
      described.push(`link ${path} -> ${await Deno.readLink(full)}`)
    } else if (path !== MODS_FILE) {
      described.push(`file ${path} ${(await Deno.stat(full)).size} ${await fileDigest(full)}`)
    }
  }
  return described
}

async function modList(world: string): Promise<string> {
  let text: string
  try {
    text = await Deno.readTextFile(join(world, MODS_FILE))
  } catch (e) {
    if (e instanceof Deno.errors.NotFound) return "[]"
    throw e
  }
  try {
    const list: unknown = JSON.parse(text)
    if (Array.isArray(list) && list.every((mod) => typeof mod === "string")) {
      return JSON.stringify(list)
    }
  } catch { /* not JSON: compared as written */ }
  return text.trim()
}

export async function fixtureIdentity(world: string): Promise<FixtureIdentity> {
  const digest = createHash("sha256").update((await describeTree(world)).join("\n")).digest("hex")
  return { digest, mods: await modList(world) }
}

/** A fixture's baseline: the game-log lines logged after the driver reported ready, at idle. */
export type BaselineRecord = {
  fixture: string
  captured_at: string
  /** Identity of the fixture the baseline was captured from; staleness is a mismatch with it. */
  identity: FixtureIdentity
  boot_ms: number
  idle_ms: number
  /** Verbatim from debug.log, each with the game's own `HH:MM:SS.mmm` stamp. */
  lines: string[]
}

/** Where a fixture's baseline record lives: beside the worlds, never inside one. */
export function baselinePath(fixtures: string, fixture: string): string {
  return join(fixtures, ".baselines", `${fixture}.json`)
}

/** The recorded baseline, or undefined when none was ever captured (which is not an empty one). */
export async function readBaseline(
  fixtures: string,
  fixture: string,
): Promise<BaselineRecord | undefined> {
  const path = baselinePath(fixtures, fixture)
  let text: string
  try {
    text = await Deno.readTextFile(path)
  } catch (e) {
    if (e instanceof Deno.errors.NotFound) return undefined
    throw e
  }
  try {
    const record: BaselineRecord = JSON.parse(text)
    if (!Array.isArray(record.lines) || typeof record.identity?.digest !== "string") {
      throw new Error("it is not a baseline record")
    }
    return record
  } catch (e) {
    throw new HarnessError(`the baseline record ${path} is unreadable: ${(e as Error).message}`)
  }
}

/** Writes the record whole or not at all; returns where it went. */
export async function writeBaseline(fixtures: string, record: BaselineRecord): Promise<string> {
  const path = baselinePath(fixtures, record.fixture)
  await Deno.mkdir(join(fixtures, ".baselines"), { recursive: true })
  const staging = `${path}.${crypto.randomUUID().slice(0, 8)}.tmp`
  await Deno.writeTextFile(staging, JSON.stringify(record, null, 2) + "\n")
  await Deno.rename(staging, path)
  return path
}

export type FixtureStatus = {
  fixture: string
  /** `missing`: never captured. `fresh`: matches the fixture. `stale`: the fixture changed since. */
  baseline: "missing" | "fresh" | "stale"
  captured_at?: string
  /** How many lines the baseline holds; zero is a valid, clean baseline. */
  lines?: number
  /** Why a stale baseline no longer matches. */
  stale?: string[]
  /** What to do about a baseline that is not fresh. */
  message?: string
}

export async function fixtureStatus(fixtures: string, fixture: string): Promise<FixtureStatus> {
  const capture = `run \`bnplay fixture baseline ${fixture}\``
  let record: BaselineRecord | undefined
  try {
    record = await readBaseline(fixtures, fixture)
  } catch (e) {
    return {
      fixture,
      baseline: "stale",
      stale: [(e as Error).message],
      message: `${capture} to replace it`,
    }
  }
  if (!record) {
    return {
      fixture,
      baseline: "missing",
      message: `no baseline recorded for fixture ${fixture}; ${capture}`,
    }
  }

  const now = await fixtureIdentity(join(fixtures, fixture))
  const stale: string[] = []
  if (now.digest !== record.identity.digest) {
    stale.push("the fixture changed since the baseline was captured")
  }
  if (now.mods !== record.identity.mods) {
    stale.push(
      `the mod set changed since the baseline was captured (was ${record.identity.mods}, now ${now.mods})`,
    )
  }
  const described = { captured_at: record.captured_at, lines: record.lines.length }
  if (stale.length === 0) return { fixture, baseline: "fresh", ...described }
  return {
    fixture,
    baseline: "stale",
    ...described,
    stale,
    message: `the baseline no longer describes this fixture; ${capture} to refresh it`,
  }
}

/** Every fixture in the library with its baseline status, by name. */
export async function listFixtures(fixtures: string): Promise<FixtureStatus[]> {
  const names: string[] = []
  try {
    for await (const entry of Deno.readDir(fixtures)) {
      // Hidden entries are the library's own bookkeeping (baselines, a clone in progress).
      if (entry.isDirectory && FIXTURE_NAME.test(entry.name)) names.push(entry.name)
    }
  } catch (e) {
    if (!(e instanceof Deno.errors.NotFound)) throw e
  }
  names.sort()
  return await Promise.all(names.map((name) => fixtureStatus(fixtures, name)))
}
