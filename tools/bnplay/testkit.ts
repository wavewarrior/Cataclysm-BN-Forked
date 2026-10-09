/** Helpers shared by the bnplay tests: sandboxes, the CLI as a subprocess, process and tree checks. */
import { delay } from "@std/async"
import { dirname, fromFileUrl, join } from "@std/path"
import { IS_WINDOWS } from "./config.ts"
import { cloneTree } from "./episode.ts"
import { windowsProcesses } from "./machine.ts"
import type { TranscriptRecord } from "./transcript.ts"

const here = dirname(fromFileUrl(import.meta.url))
export const REPO = dirname(dirname(here))
export const MOCK_DRIVER = join(here, "mock_driver.py")
/**
 * A game binary that exits at once with a failure, whatever its arguments (Windows: `where`,
 * which rejects the game's flags).
 */
export const FAILING_BINARY = IS_WINDOWS
  ? join(Deno.env.get("SystemRoot") ?? "C:\\Windows", "System32", "where.exe")
  : "/usr/bin/false"
const MAIN = join(here, "main.ts")

/**
 * The save the real-binary tests clone: BNPLAY_SAVE, else the macOS game's Bairdford save. Windows
 * keeps saves wherever the game's user directory is, so there it must be named.
 */
export function realSave(): string {
  const save = Deno.env.get("BNPLAY_SAVE")
  if (save) return save
  if (IS_WINDOWS) throw new Error("set BNPLAY_SAVE to the Bairdford save directory to clone")
  return join(Deno.env.get("HOME") ?? "", "Library/Application Support/Cataclysm-BN/save/Bairdford")
}

/** Arguments of the `deno` process that runs `bnplay <args>`. */
export function bnplayDenoArgs(args: string[]): string[] {
  return [
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
  ]
}

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
    await cloneTree(source, join(fixtures, name))
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
        args: bnplayDenoArgs(args),
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
      // A daemon that logged an unexpected failure is a failing test, not a swallowed error.
      const log = await Deno.readTextFile(join(home, "daemon.log")).catch(() => "")
      await Deno.remove(dir, { recursive: true })
      const bad = log.split("\n").filter((l) =>
        /unhandled rejection|uncaught error|connection failed|internal error/.test(l)
      )
      if (bad.length > 0) throw new Error(`the daemon logged failures:\n${bad.join("\n")}`)
    },
  }
  return sandbox
}

/**
 * Pids of processes whose command line matches `pattern` (a regular expression for pgrep; on
 * Windows, where CIM lists the command lines, a path contained in it, ignoring case).
 */
export async function pidsMatching(pattern: string): Promise<number[]> {
  if (IS_WINDOWS) {
    const needle = pattern.toLowerCase()
    return (await windowsProcesses())
      .filter((p) => p.command.toLowerCase().includes(needle))
      .map((p) => p.pid)
  }
  const out = await new Deno.Command("pgrep", {
    args: ["-f", pattern],
    stdout: "piped",
    stderr: "null",
  }).output()
  return new TextDecoder().decode(out.stdout).split("\n").filter(Boolean).map(Number)
}

export async function pidAlive(pid: number): Promise<boolean> {
  if (IS_WINDOWS) {
    // Windows has no signal 0 or SIGCONT to probe with; `tasklist` prints the pid as a CSV field.
    const out = await new Deno.Command("tasklist", {
      args: ["/FI", `PID eq ${pid}`, "/NH", "/FO", "CSV"],
      stdout: "piped",
      stderr: "null",
    }).output()
    return new TextDecoder().decode(out.stdout).includes(`"${pid}"`)
  }
  try {
    Deno.kill(pid, "SIGCONT")
    return true
  } catch {
    return false
  }
}

/**
 * Freezes (`true`) or thaws (`false`) a process: SIGSTOP and SIGCONT, on Windows
 * `NtSuspendProcess` and `NtResumeProcess`. A frozen server still accepts connections in the
 * kernel and answers nothing.
 */
export async function setSuspended(pid: number, suspended: boolean): Promise<void> {
  if (!IS_WINDOWS) {
    Deno.kill(pid, suspended ? "SIGSTOP" : "SIGCONT")
    return
  }
  const call = suspended ? "NtSuspendProcess" : "NtResumeProcess"
  const script = "Add-Type -Name Nt -Namespace Bnplay -MemberDefinition '" +
    `[DllImport("ntdll.dll")] public static extern int ${call}(IntPtr process);'; ` +
    `$status = [Bnplay.Nt]::${call}([System.Diagnostics.Process]::GetProcessById(${pid}).Handle); ` +
    'if ($status -ne 0) { throw "NTSTATUS $status" }'
  const out = await new Deno.Command("powershell", {
    args: ["-NoProfile", "-Command", script],
    stdout: "null",
    stderr: "piped",
  }).output()
  if (!out.success) {
    throw new Error(`${call}(${pid}) failed: ${new TextDecoder().decode(out.stderr)}`)
  }
}

/**
 * Starts a process that names `args` on its command line and waits on its stdin, the way a game
 * started by hand shows up in the process table. Kill it when done.
 */
export function spawnIdle(args: string[]): Deno.ChildProcess {
  const [command, prefix] = IS_WINDOWS
    ? ["python", ["-c", "import sys; sys.stdin.readline()"]]
    : ["/bin/sh", ["-c", "read line", "sh"]]
  return new Deno.Command(command, {
    args: [...prefix, ...args],
    stdin: "piped",
    stdout: "null",
    stderr: "null",
  }).spawn()
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

/**
 * Relative path (`/`-separated, with a leading `/`) -> content for every file under `dir`, to
 * compare a tree before and after.
 */
export async function treeSnapshot(dir: string): Promise<Record<string, string>> {
  const out: Record<string, string> = {}
  const walk = async (current: string, relative: string) => {
    for await (const entry of Deno.readDir(current)) {
      const path = join(current, entry.name)
      const key = `${relative}/${entry.name}`
      if (entry.isDirectory) {
        await walk(path, key)
      } else {
        out[key] = new TextDecoder("latin1").decode(await Deno.readFile(path))
      }
    }
  }
  await walk(dir, "")
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
