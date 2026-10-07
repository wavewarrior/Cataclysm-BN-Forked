/**
 * `bnplay doctor`: the preflight, and the optional determinism self-check.
 *
 * The preflight never starts a game, and it never emulates the driver: it reads the binary, the
 * sources, the fixture library, the process table and the machine, and says what is wrong and what
 * to do about it. The self-check is the only part that boots games, and only on request.
 */
import { Buffer } from "node:buffer"
import { basename, dirname, join } from "@std/path"
import { delay } from "@std/async"
import type { DriverResponse } from "./client.ts"
import { type Config, IS_WINDOWS } from "./config.ts"
import { type Episode, HarnessError } from "./episode.ts"
import { fixtureStatus, listFixtures } from "./fixtures.ts"
import { type MachineSample, sampleMachine, windowsProcesses } from "./machine.ts"
import { FIXTURE_NAME, type Trial } from "./trial.ts"

/** The flag that makes the game open a real window (its value is `WxH`). */
const WINDOWED_FLAG = "--driver-windowed"

export type CheckStatus = "ok" | "warn" | "fail" | "skipped"

export type Check = {
  name: string
  status: CheckStatus
  summary: string
  /** What to do about a check that is not ok. */
  message?: string
  /** What the check found, where a list says more than the summary. */
  details?: unknown[]
}

/** The flag that makes the game serve the driver protocol. */
const DRIVER_FLAG = "--driver-fd"

/** What the agent can use instead while there is no driver (it is never emulated). */
const FALLBACK_WORKFLOWS = [
  "file-trigger observation: touch /tmp/cata_dump_trigger for a frame and map dump, " +
  "echo <0-17> > /tmp/cata_dbg_mode for a lighting debug view, " +
  "echo '<name> <value>' > /tmp/cata_knob for a lighting knob (skill cbn-headless-file-trigger-verification)",
  "the Windows harness: tools/visual_verify/vv.py (see tools/visual_verify/README.md)",
  "the test suite: cata_test-tiles (AGENTS.md) for the game, `deno task test:bnplay` for bnplay",
]

const CHUNK_BYTES = 4 * 1024 * 1024

/** Whether the file holds `needle` anywhere, read in chunks so a large binary is never held whole. */
async function fileContains(path: string, needle: string): Promise<boolean> {
  const bytes = Buffer.from(needle)
  const buffer = new Uint8Array(CHUNK_BYTES + bytes.length)
  const file = await Deno.open(path, { read: true })
  try {
    let carried = 0
    for (;;) {
      const read = await file.read(buffer.subarray(carried))
      if (read === null) return false
      const filled = carried + read
      if (Buffer.from(buffer.buffer, 0, filled).includes(bytes)) return true
      // A flag split across two chunks is found by keeping the tail of this one.
      carried = Math.min(bytes.length - 1, filled)
      buffer.copyWithin(0, filled - carried, filled)
    }
  } finally {
    file.close()
  }
}

async function isFile(path: string): Promise<boolean> {
  return await Deno.stat(path).then((s) => s.isFile, () => false)
}

async function driverFlagCheck(config: Config): Promise<Check> {
  const name = "driver_flag"
  const unavailable = (summary: string, fix: string): Check => ({
    name,
    status: "fail",
    summary,
    message: `${fix} bnplay does not emulate the driver; until it is available use the existing ` +
      `workflows: ${FALLBACK_WORKFLOWS.join("; ")}`,
    details: FALLBACK_WORKFLOWS,
  })
  if (!(await isFile(config.binary))) {
    return unavailable(
      `the driver is unavailable: game binary not found at ${config.binary}`,
      "Build cataclysm-bn-tiles (AGENTS.md has the build rules) or point BNPLAY_BINARY at one.",
    )
  }
  if (!(await fileContains(config.binary, DRIVER_FLAG))) {
    return unavailable(
      `the driver is unavailable: ${config.binary} has no ${DRIVER_FLAG} flag`,
      "This binary predates the driver; rebuild from a tree that has it, or point BNPLAY_BINARY at one.",
    )
  }
  return { name, status: "ok", summary: `${config.binary} has the ${DRIVER_FLAG} flag` }
}

const SOURCE_EXTENSIONS = [".cpp", ".cc", ".c", ".h", ".hpp", ".mm"]

type SourceFile = { path: string; mtime: Date }

/** The most recently modified C++ source (or CMake list) under `dir`. */
async function newestSource(dir: string): Promise<SourceFile | undefined> {
  let newest: SourceFile | undefined
  for await (const entry of Deno.readDir(dir)) {
    const path = join(dir, entry.name)
    let found: SourceFile | undefined
    if (entry.isDirectory) {
      found = await newestSource(path)
    } else if (
      entry.isFile &&
      (entry.name === "CMakeLists.txt" || SOURCE_EXTENSIONS.some((e) => entry.name.endsWith(e)))
    ) {
      const mtime = (await Deno.stat(path)).mtime
      if (mtime) found = { path, mtime }
    }
    if (found && (!newest || found.mtime > newest.mtime)) newest = found
  }
  return newest
}

async function binaryFreshnessCheck(config: Config): Promise<Check> {
  const name = "binary_fresh"
  const binary = await Deno.stat(config.binary).catch(() => undefined)
  if (!binary?.isFile || !binary.mtime) {
    return { name, status: "skipped", summary: "there is no binary to date" }
  }
  const sources = join(config.basepath, "src")
  let newest: SourceFile | undefined
  try {
    newest = await newestSource(sources)
  } catch {
    return {
      name,
      status: "warn",
      summary: `no sources at ${sources}, so the binary cannot be compared with them`,
      message: "Point BNPLAY_BASEPATH at the checkout the binary was built from.",
    }
  }
  if (!newest) {
    return {
      name,
      status: "warn",
      summary: `no C++ sources under ${sources}`,
      message: "Point BNPLAY_BASEPATH at the checkout the binary was built from.",
    }
  }
  if (newest.mtime <= binary.mtime) {
    return {
      name,
      status: "ok",
      summary: `the binary (built ${binary.mtime.toISOString()}) is newer than every source`,
    }
  }
  // The standard build tree keeps the binary at <build dir>/src/cataclysm-bn-tiles.
  const buildDir = basename(dirname(config.binary)) === "src"
    ? dirname(dirname(config.binary))
    : undefined
  return {
    name,
    status: "fail",
    summary: `the binary (built ${binary.mtime.toISOString()}) is older than ${newest.path} ` +
      `(modified ${newest.mtime.toISOString()})`,
    message: "A stale binary tests code that is no longer there. Rebuild " +
      `${buildDir ? `with cmake --build ${buildDir} --target cataclysm-bn-tiles` : "it"} ` +
      "(AGENTS.md has the build rules) before trusting any Episode. " +
      "If you only switched branches and changed nothing, touching the binary is not a fix: rebuild.",
  }
}

async function fixtureChecks(config: Config, fixture?: string): Promise<Check[]> {
  const add = "bnplay fixture add <save-dir>"
  if (fixture !== undefined) {
    const found = FIXTURE_NAME.test(fixture) &&
      await Deno.stat(join(config.fixtures, fixture)).then((s) => s.isDirectory, () => false)
    if (!found) {
      return [{
        name: "fixture",
        status: "fail",
        summary: `fixture ${fixture} not found in ${config.fixtures}`,
        message: `Add it: ${add} ${fixture}`,
      }]
    }
  }
  const all = await listFixtures(config.fixtures)
  const statuses = fixture === undefined ? all : [await fixtureStatus(config.fixtures, fixture)]
  if (statuses.length === 0) {
    return [{
      name: "fixture",
      status: "fail",
      summary: `the fixture library ${config.fixtures} is empty`,
      message: `Add a world save: ${add} [name]`,
    }]
  }
  const names = statuses.map((s) => s.fixture)
  const bad = statuses.filter((s) => s.baseline !== "fresh")
  return [
    { name: "fixture", status: "ok", summary: `fixtures present: ${names.join(", ")}` },
    bad.length === 0
      ? { name: "baseline", status: "ok", summary: `every baseline is fresh (${names.join(", ")})` }
      : {
        name: "baseline",
        status: "fail",
        summary: bad.map((s) => `${s.fixture}: ${s.baseline}`).join("; "),
        message: bad.map((s) => s.message).join("; "),
        details: bad,
      },
  ]
}

type DriverProcess = { pid: number; elapsed: string; command: string }

const PS_LINE = /^\s*(\d+)\s+(\S+)\s+(.*)$/

/**
 * Every process whose command line names the game binary, from `ps` (Windows: CIM, with the age
 * in seconds), so a game run through a wrapper or an interpreter counts too.
 */
async function gameProcesses(config: Config): Promise<DriverProcess[]> {
  const executable = basename(config.binary).replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
  if (IS_WINDOWS) {
    // Windows file names ignore case, and a quoted argument ends in `"`.
    const named = new RegExp(`(^|[\\s/\\\\"])${executable}(["\\s]|$)`, "i")
    return (await windowsProcesses()).filter((p) => named.test(p.command)).map((p) => ({
      pid: p.pid,
      elapsed: `${p.age}s`,
      command: p.command,
    }))
  }
  const out = await new Deno.Command("ps", {
    args: ["-axo", "pid=,etime=,command="],
    stdout: "piped",
  }).output()
  const named = new RegExp(`(^|[\\s/])${executable}(\\s|$)`)
  const found: DriverProcess[] = []
  for (const line of new TextDecoder().decode(out.stdout).split("\n")) {
    const m = PS_LINE.exec(line)
    if (m && named.test(m[3])) found.push({ pid: Number(m[1]), elapsed: m[2], command: m[3] })
  }
  return found
}

/** Game processes serving the driver protocol that are not one of this daemon's Episodes. */
async function strayDrivers(config: Config, liveUserdirs: string[]): Promise<DriverProcess[]> {
  return (await gameProcesses(config)).filter((p) =>
    p.command.includes(DRIVER_FLAG) &&
    // A live Episode runs on a user directory of its own under this daemon's home.
    !liveUserdirs.some((dir) => p.command.includes(dir))
  )
}

async function strayCheck(config: Config, liveUserdirs: string[]): Promise<Check> {
  const name = "stray_processes"
  const strays = await strayDrivers(config, liveUserdirs)
  if (strays.length === 0) {
    return { name, status: "ok", summary: "no stray driver processes" }
  }
  return {
    name,
    status: "fail",
    summary: `${strays.length} stray driver process(es), not owned by this daemon: ` +
      strays.map((s) => `pid ${s.pid} (running ${s.elapsed})`).join(", "),
    message: "Each holds about 1 GB and only one game should run at a time. If nobody else is " +
      `using them, end them: ${
        IS_WINDOWS
          ? strays.map((s) => `taskkill /F /PID ${s.pid}`).join(" & ")
          : `kill -KILL ${strays.map((s) => s.pid).join(" ")}`
      }`,
    details: strays,
  }
}

/** The session type `launchctl managername` names for a graphical login. */
const GRAPHICAL_SESSION = "Aqua"

/**
 * A windowed game needs the window server of a logged-in desktop: over ssh or from a background
 * service the process has no display and the game cannot open its window.
 */
async function displaySessionCheck(config: Config): Promise<Check> {
  const name = "display_session"
  if (IS_WINDOWS) {
    // An interactive logon names its session (Console, RDP-Tcp#N); a service has none.
    const session = Deno.env.get("SESSIONNAME")
    return session
      ? { name, status: "ok", summary: `an interactive Windows session (${session})` }
      : {
        name,
        status: "fail",
        summary: "no interactive Windows session (SESSIONNAME is unset)",
        message:
          "A windowed game needs the logged-in desktop; a windowless Trial needs no display.",
      }
  }
  let session: string
  try {
    const out = await new Deno.Command(config.launchctl, {
      args: ["managername"],
      stdout: "piped",
      stderr: "null",
    }).output()
    session = new TextDecoder().decode(out.stdout).trim()
  } catch (e) {
    return {
      name,
      status: "fail",
      summary: `the session type could not be read (${config.launchctl} managername): ${
        (e as Error).message
      }`,
      message: "A windowed Trial needs a graphical login session; point BNPLAY_LAUNCHCTL at " +
        "launchctl, or run bnplay from a terminal on the logged-in desktop.",
    }
  }
  if (session === GRAPHICAL_SESSION) {
    return { name, status: "ok", summary: `a graphical login session (${session})` }
  }
  return {
    name,
    status: "fail",
    summary: `no display session: launchctl reports \`${
      session || "nothing"
    }\`, not ${GRAPHICAL_SESSION}`,
    message: "A windowed game opens a real window, which needs a graphical login session. Run " +
      "bnplay from a terminal on the logged-in desktop, not over ssh or from a background service; " +
      "a windowless Trial needs no display.",
  }
}

/**
 * A game window nobody here owns: an interactive game, or a windowed driver that is not one of
 * this daemon's Episodes. A second window can occlude the Trial's, and a wrong frame silently
 * corrupts a paired-null comparison.
 */
async function strayWindowCheck(config: Config, liveUserdirs: string[]): Promise<Check> {
  const name = "stray_windows"
  const strays = (await gameProcesses(config)).filter((p) =>
    (!p.command.includes(DRIVER_FLAG) || p.command.includes(WINDOWED_FLAG)) &&
    !liveUserdirs.some((dir) => p.command.includes(dir))
  )
  if (strays.length === 0) return { name, status: "ok", summary: "no stray game windows" }
  return {
    name,
    status: "fail",
    summary: `${strays.length} game process(es) with a window, not owned by this daemon: ` +
      strays.map((s) => `pid ${s.pid} (running ${s.elapsed})`).join(", "),
    message: "A game window other than the Trial's can occlude it, and a capture of an occluded " +
      `window is refused. Close the game, or if nobody is using it: ${
        IS_WINDOWS
          ? strays.map((s) => `taskkill /F /PID ${s.pid}`).join(" & ")
          : `kill -KILL ${strays.map((s) => s.pid).join(" ")}`
      }`,
    details: strays,
  }
}

/** The two lighting shader sources the windowed game cannot start without. */
const SHADER_SOURCES = ["emitter_glow.vert.hlsl", "emitter_glow.frag.hlsl"]
const SHADER_DIR = join("data", "shaders", "lighting", "src")

/** The checkout `basepath` is a worktree of, when it is one: where the shaders actually live. */
async function mainCheckout(basepath: string): Promise<string | undefined> {
  try {
    const out = await new Deno.Command("git", {
      args: ["-C", basepath, "rev-parse", "--path-format=absolute", "--git-common-dir"],
      stdout: "piped",
      stderr: "null",
    }).output()
    const common = new TextDecoder().decode(out.stdout).trim()
    return out.success && basename(common) === ".git" ? dirname(common) : undefined
  } catch {
    return undefined
  }
}

/**
 * `emitter_glow.vert.hlsl` and `emitter_glow.frag.hlsl` are gitignored (`/data/shaders/`), were
 * never committed, and no build step generates them: they are hand-written sources that exist in
 * the original checkout only, so a fresh clone or worktree fails windowed init without them.
 */
async function shaderSourceCheck(config: Config): Promise<Check> {
  const name = "shader_sources"
  const dir = join(config.basepath, SHADER_DIR)
  const missing: string[] = []
  for (const file of SHADER_SOURCES) {
    if (!(await isFile(join(dir, file)))) missing.push(file)
  }
  if (missing.length === 0) {
    return { name, status: "ok", summary: `${SHADER_SOURCES.join(" and ")} exist under ${dir}` }
  }
  const main = await mainCheckout(config.basepath)
  const from = main && join(main, SHADER_DIR) !== dir
    ? `the main checkout at ${join(main, SHADER_DIR)}/`
    : "the main checkout (the original working tree of this repo, under data/shaders/lighting/src/)"
  return {
    name,
    status: "fail",
    summary: `missing under ${dir}: ${missing.join(", ")}`,
    message:
      `Windowed init fails without them. They are untracked: /data/shaders/ is gitignored and ` +
      "these two files were never committed, and no build step generates them, so a fresh clone or " +
      `worktree does not have them. Copy them from ${from} or have them tracked by a separate ` +
      "repo fix.",
  }
}

/** The checks only a windowed Trial needs; a windowless Trial never sees them. */
async function windowedChecks(config: Config, liveUserdirs: string[]): Promise<Check[]> {
  return [
    await displaySessionCheck(config),
    await strayWindowCheck(config, liveUserdirs),
    await shaderSourceCheck(config),
  ]
}

const mb = (n: number) => `${Math.round(n)} MB`

function machineChecks(config: Config, machine: MachineSample): Check[] {
  const checks: Check[] = []
  const unreadable = (what: string) =>
    machine.unreadable?.find((u) => u.startsWith(what)) ?? `${what}: not read`
  if (machine.memory_available_mb === undefined) {
    checks.push({
      name: "memory",
      status: "warn",
      summary: `available memory could not be read (${unreadable("memory")})`,
    })
  } else if (machine.memory_available_mb < config.minFreeMemoryMb) {
    checks.push({
      name: "memory",
      status: "fail",
      summary: `only ${mb(machine.memory_available_mb)} of memory is available ` +
        `(at least ${mb(config.minFreeMemoryMb)} wanted)`,
      message: "A game holds about 1 GB. Close applications or wait for builds to finish, then " +
        "rerun doctor (BNPLAY_MIN_FREE_MEMORY_MB moves the line).",
    })
  } else {
    checks.push({
      name: "memory",
      status: "ok",
      summary: `${mb(machine.memory_available_mb)} of memory is available`,
    })
  }
  if (machine.swap_free_mb === undefined) {
    checks.push({
      name: "swap",
      status: "warn",
      summary: `swap could not be read (${unreadable("swap")})`,
    })
  } else if (machine.swap_free_mb < config.minFreeSwapMb) {
    checks.push({
      name: "swap",
      status: "fail",
      summary: `swap is nearly exhausted: ${mb(machine.swap_free_mb)} free of ` +
        `${mb(machine.swap_total_mb!)} (at least ${mb(config.minFreeSwapMb)} wanted)`,
      message: "Starting a game now risks stalling the machine, and an Episode started under " +
        "memory pressure is the one most likely to diverge. Wait for builds to finish or close " +
        "applications, then rerun doctor (BNPLAY_MIN_FREE_SWAP_MB moves the line).",
    })
  } else {
    checks.push({
      name: "swap",
      status: "ok",
      summary: `${mb(machine.swap_free_mb)} of swap is free of ${mb(machine.swap_total_mb!)}`,
    })
  }
  return checks
}

/** The seed both Episodes of the self-check start from. */
const SELF_CHECK_SEED = 4242

/** The steps each Episode takes after its idle gap; the first `wait` is the first world step. */
const AFTER_IDLE: { cmd: string; [key: string]: unknown }[] = [
  { cmd: "state" },
  { cmd: "wait", turns: 1 },
  { cmd: "wait", turns: 5 },
  { cmd: "state" },
]

type Observation = { cmd: string; response: Record<string, unknown> }

export type Divergence = {
  /** Index of the first step whose responses differ, and the command it answered. */
  step: number
  cmd: string
  differences: { field: string; a: unknown; b: unknown }[]
}

export type SelfCheck =
  | {
    ran: true
    diverged: boolean
    fixture: string
    seed: number
    idle_ms: number
    steps: number
    first_divergence?: Divergence
    transcripts: [string, string]
    machine: { before: MachineSample; after: MachineSample }
    message: string
  }
  | { ran: false; reason: string; machine?: MachineSample }

/** Runs one short Episode and returns what the game answered at each step. */
async function selfCheckEpisode(
  config: Config,
  boot: (trial: Trial) => Promise<Episode>,
  fixture: string,
): Promise<{ observations: Observation[]; transcript: string }> {
  const episode = await boot({
    fixture,
    seed: SELF_CHECK_SEED,
    wallClockLimitS: Math.ceil((config.bootTimeoutMs + config.selfCheckIdleMs) / 1000) + 60,
    expectedCommands: [],
    oracles: [],
    rendererOracles: [],
  })
  const observations: Observation[] = []
  try {
    const take = async (request: { cmd: string; [key: string]: unknown }) => {
      const { id: _id, ...response }: DriverResponse = await episode.step(request)
      if (response.status !== "ok") {
        throw new HarnessError(
          `self-check: \`${request.cmd}\` failed: ${JSON.stringify(response)}`,
        )
      }
      observations.push({ cmd: request.cmd, response })
    }
    await take({ cmd: "seed", seed: SELF_CHECK_SEED })
    await take({ cmd: "state" })
    // The divergences that were seen had real time between load and the first world step.
    await delay(config.selfCheckIdleMs)
    for (const request of AFTER_IDLE) await take(request)
  } finally {
    await episode.stop()
  }
  const { ended, exit_code } = episode.summary()
  if (ended !== "stop" || exit_code !== 0) {
    throw new HarnessError(
      `self-check: the game did not exit cleanly (ended: ${ended}, exit code ${exit_code}; ` +
        `transcript: ${episode.transcriptPath})`,
    )
  }
  return { observations, transcript: episode.transcriptPath }
}

function firstDivergence(a: Observation[], b: Observation[]): Divergence | undefined {
  for (let step = 0; step < Math.max(a.length, b.length); step++) {
    const left = a[step]?.response ?? {}
    const right = b[step]?.response ?? {}
    const differences = [...new Set([...Object.keys(left), ...Object.keys(right)])]
      .filter((field) => JSON.stringify(left[field]) !== JSON.stringify(right[field]))
      .map((field) => ({ field, a: left[field], b: right[field] }))
    if (differences.length > 0) {
      return { step, cmd: (a[step] ?? b[step]).cmd, differences }
    }
  }
}

const SELF_CHECK_NOTE = "Only what the driver reports is compared (turn, vitals, messages, " +
  "outcome); it exposes no RNG state or hash. bnplay promises invariants, not replay, so a " +
  "divergence is information about this machine now, not a failure."

/**
 * Runs the same short Episode twice, one after the other (never two games at once), with the same
 * seed and an idle gap before the first world step, and reports whether they diverged beside the
 * machine's load and swap: the divergences seen so far came with memory pressure.
 */
async function selfCheck(
  config: Config,
  fixture: string,
  boot: (trial: Trial) => Promise<Episode>,
): Promise<SelfCheck> {
  const before = await sampleMachine()
  try {
    const a = await selfCheckEpisode(config, boot, fixture)
    const b = await selfCheckEpisode(config, boot, fixture)
    const after = await sampleMachine()
    const divergence = firstDivergence(a.observations, b.observations)
    return {
      ran: true,
      diverged: divergence !== undefined,
      fixture,
      seed: SELF_CHECK_SEED,
      idle_ms: config.selfCheckIdleMs,
      steps: a.observations.length,
      first_divergence: divergence,
      transcripts: [a.transcript, b.transcript],
      machine: { before, after },
      message: divergence
        ? `The same-seed Episodes diverged at step ${divergence.step} (${divergence.cmd}). ` +
          `${SELF_CHECK_NOTE} Compare load and swap below with a clean run.`
        : `The same-seed Episodes agreed on every step. ${SELF_CHECK_NOTE}`,
    }
  } catch (e) {
    if (!(e instanceof HarnessError)) throw e
    return { ran: false, reason: e.message, machine: await sampleMachine() }
  }
}

export type DoctorOptions = {
  /** Check only this fixture; the self-check plays it. */
  fixture?: string
  /** The Trial the run is for: a windowed one adds the checks a game window needs. */
  trial?: Trial
  selfCheck: boolean
  /** User directories of the Episodes the daemon holds, which are not strays. */
  liveUserdirs: string[]
  /** Boots an Episode for the self-check; it counts against the session cap. */
  boot: (trial: Trial) => Promise<Episode>
}

export type DoctorReport = {
  healthy: boolean
  driver_available: boolean
  checks: Check[]
  machine: MachineSample
  self_check?: SelfCheck
}

/** The fixture the self-check plays: the named one, or the library's only one. */
async function selfCheckFixture(config: Config, named?: string): Promise<string | SelfCheck> {
  if (named !== undefined) return named
  const names = (await listFixtures(config.fixtures)).map((f) => f.fixture)
  if (names.length === 1) return names[0]
  return {
    ran: false,
    reason: names.length === 0
      ? "the self-check needs a fixture to play and the library is empty: bnplay fixture add <save-dir>"
      : `the self-check needs --fixture <name> (the library holds ${names.join(", ")})`,
  }
}

export async function runDoctor(config: Config, options: DoctorOptions): Promise<DoctorReport> {
  const machine = await sampleMachine()
  const checks = [
    await driverFlagCheck(config),
    await binaryFreshnessCheck(config),
    ...await fixtureChecks(config, options.fixture),
    await strayCheck(config, options.liveUserdirs),
    ...(options.trial?.window ? await windowedChecks(config, options.liveUserdirs) : []),
    ...machineChecks(config, machine),
  ]
  const report: DoctorReport = {
    healthy: checks.every((c) => c.status !== "fail"),
    driver_available: checks[0].status === "ok",
    checks,
    machine,
  }
  if (!options.selfCheck) return report

  // Every precondition the self-check cannot do without, in the order they would bite. Memory
  // and swap are deliberately not among them: that is the condition it exists to look at.
  const blocked = ["driver_flag", "fixture", "stray_processes"]
    .map((name) => checks.find((c) => c.name === name)!)
    .find((c) => c.status === "fail")
  let result: SelfCheck
  if (blocked) {
    result = {
      ran: false,
      reason: `the self-check did not run: ${blocked.summary}. ${blocked.message ?? ""}`.trim(),
    }
  } else {
    const fixture = await selfCheckFixture(config, options.fixture)
    result = typeof fixture === "string" ? await selfCheck(config, fixture, options.boot) : fixture
  }
  report.self_check = result
  report.healthy &&= result.ran
  return report
}
