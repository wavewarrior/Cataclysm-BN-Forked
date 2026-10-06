/**
 * The fixture library: a gitignored directory holding one world save per fixture, each a
 * copy-on-write clone of a save the developer owns. A fixture directory IS the world: an Episode
 * clones it again into its own user directory, so neither the source save nor the fixture is ever
 * played in place.
 *
 * A fixture's baseline lives beside the library, not inside the world (see baseline.ts).
 */
import { basename, join } from "@std/path"
import { HarnessError, run } from "./episode.ts"
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
 * directory name, which is also the game's world name). The source is only ever read.
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
    await run("cp", ["-cR", real, staging])
    await Deno.rename(staging, path)
  } catch (e) {
    await Deno.remove(staging, { recursive: true }).catch(() => undefined)
    throw new HarnessError(
      `cloning ${source} into the fixture library failed: ${(e as Error).message}`,
    )
  }
  return { fixture, path, source: real }
}

export type FixtureStatus = {
  fixture: string
  /** `missing`: never captured. `fresh`: matches the fixture. `stale`: the fixture changed since. */
  baseline: "missing" | "fresh" | "stale"
  /** What to do about a baseline that is not fresh. */
  message?: string
}

/** Where a fixture's baseline record lives: beside the worlds, never inside one. */
export function baselinePath(fixtures: string, fixture: string): string {
  return join(fixtures, ".baselines", `${fixture}.json`)
}

export async function fixtureStatus(fixtures: string, fixture: string): Promise<FixtureStatus> {
  const recorded = await Deno.lstat(baselinePath(fixtures, fixture)).then(() => true, () => false)
  if (!recorded) {
    return {
      fixture,
      baseline: "missing",
      message:
        `no baseline recorded for fixture ${fixture}; run \`bnplay fixture baseline ${fixture}\``,
    }
  }
  return { fixture, baseline: "fresh" }
}
