/** Helpers shared by the bnplay tests: sandboxes, the CLI as a subprocess, process and tree checks. */
import { delay } from "@std/async"
import { dirname, fromFileUrl, join } from "@std/path"
import type { TranscriptRecord } from "./transcript.ts"

const here = dirname(fromFileUrl(import.meta.url))
export const REPO = dirname(dirname(here))
export const MOCK_DRIVER = join(here, "mock_driver.py")
const MAIN = join(here, "main.ts")

export type CliResult = { code: number; stdout: string; stderr: string }

export type Sandbox = {
  /** Resident daemon state (socket, Episodes). */
  home: string
  /** Fixture library holding the fixtures the Trials name. */
  fixtures: string
  /** Scratch directory for Trial files. */
  dir: string
  env: Record<string, string>
  /** Runs `bnplay <args>`; the daemon it may start inherits this sandbox's environment. */
  cli(args: string[], env?: Record<string, string>): Promise<CliResult>
  /** Writes a Trial file and returns its path. */
  trial(toml: string, name?: string): Promise<string>
  /** Stops the daemon (and every game it holds) and removes the sandbox. */
  cleanup(): Promise<void>
}

export type SandboxOptions = {
  /** Game binary the daemon starts; the mock driver by default. */
  binary?: string
  /** Fixtures to create: name -> directory copied copy-on-write into the fixture library. */
  fixtureSources?: Record<string, string>
  env?: Record<string, string>
}

/** Creates a fixture directory that looks like a tiny world save. */
export async function makeFakeWorld(): Promise<string> {
  const dir = await Deno.makeTempDir({ prefix: "bnplay-world-" })
  await Deno.writeTextFile(join(dir, "worldoptions.json"), "[]\n")
  await Deno.mkdir(join(dir, "maps"))
  await Deno.writeTextFile(join(dir, "maps", "m1.sav"), "map data\n")
  await Deno.writeTextFile(join(dir, "player.sav"), "player data\n")
  return dir
}

export async function makeSandbox(opts: SandboxOptions = {}): Promise<Sandbox> {
  const dir = await Deno.makeTempDir({ prefix: "bnplay-sb-" })
  const home = join(dir, "home")
  const fixtures = join(dir, "fixtures")
  await Deno.mkdir(fixtures)
  for (const [name, source] of Object.entries(opts.fixtureSources ?? {})) {
    const cp = await new Deno.Command("cp", { args: ["-cR", source, join(fixtures, name)] })
      .output()
    if (!cp.success) throw new Error(`cloning fixture ${name} failed`)
  }
  const env: Record<string, string> = {
    BNPLAY_HOME: home,
    BNPLAY_FIXTURES: fixtures,
    BNPLAY_BINARY: opts.binary ?? MOCK_DRIVER,
    BNPLAY_BASEPATH: REPO,
    ...opts.env,
  }
  let trials = 0
  const sandbox: Sandbox = {
    home,
    fixtures,
    dir,
    env,
    async cli(args, extra) {
      const out = await new Deno.Command(Deno.execPath(), {
        args: [
          "run",
          "--allow-run",
          "--allow-read",
          "--allow-net",
          "--allow-write",
          "--allow-env",
          "--config",
          join(REPO, "deno.jsonc"),
          MAIN,
          ...args,
        ],
        cwd: dir,
        env: { ...env, ...extra },
        clearEnv: false,
      }).output()
      const dec = new TextDecoder()
      let stderr = dec.decode(out.stderr)
      if (out.code !== 0 && stderr.includes("daemon")) {
        // A daemon-level failure: show the daemon's own log so a flake explains itself.
        stderr += await Deno.readTextFile(join(home, "daemon.log")).catch(() => "(no daemon.log)")
      }
      return { code: out.code, stdout: dec.decode(out.stdout), stderr }
    },
    async trial(toml, name) {
      const path = join(dir, name ?? `trial-${++trials}.toml`)
      await Deno.writeTextFile(path, toml)
      return path
    },
    async cleanup() {
      await sandbox.cli(["shutdown"]).catch(() => undefined)
      await Deno.remove(dir, { recursive: true })
    },
  }
  return sandbox
}

/** Pids of processes whose command line matches `pattern` (a regular expression for pgrep). */
export async function pidsMatching(pattern: string): Promise<number[]> {
  const out = await new Deno.Command("pgrep", {
    args: ["-f", pattern],
    stdout: "piped",
    stderr: "null",
  }).output()
  return new TextDecoder().decode(out.stdout).split("\n").filter(Boolean).map(Number)
}

export function pidAlive(pid: number): boolean {
  try {
    Deno.kill(pid, "SIGCONT")
    return true
  } catch {
    return false
  }
}

/** Waits until `predicate` holds or `timeoutMs` passes; returns the last value. */
export async function eventually(
  predicate: () => boolean | Promise<boolean>,
  timeoutMs = 5_000,
): Promise<boolean> {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    if (await predicate()) return true
    await delay(50)
  }
  return await predicate()
}

/** Relative path -> content for every file under `dir`, to compare a tree before and after. */
export async function treeSnapshot(dir: string): Promise<Record<string, string>> {
  const out: Record<string, string> = {}
  const walk = async (current: string) => {
    for await (const entry of Deno.readDir(current)) {
      const path = join(current, entry.name)
      if (entry.isDirectory) {
        await walk(path)
      } else {
        out[path.slice(dir.length)] = new TextDecoder("latin1").decode(await Deno.readFile(path))
      }
    }
  }
  await walk(dir)
  return out
}

/** Parses the single JSON object a CLI command printed on stdout. */
export function jsonOut<T extends object = Record<string, unknown>>(result: CliResult): T {
  return JSON.parse(result.stdout)
}

/** Reads a JSONL transcript into its records. */
export async function readTranscript(path: string): Promise<TranscriptRecord[]> {
  const text = await Deno.readTextFile(path)
  return text.split("\n").filter(Boolean).map((line) => JSON.parse(line))
}
