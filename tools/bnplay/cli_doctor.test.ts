/**
 * `bnplay doctor` end to end through the CLI and the daemon, against the mock driver: each preflight
 * finding with its actionable message, the driver-unavailable path, the rule that no game starts
 * unless the determinism self-check is asked for, and the self-check's A/A report.
 */
import { assert, assertEquals } from "@std/assert"
import { basename, dirname, join } from "@std/path"
import { makeFakeWorld, makeSandbox, MOCK_DRIVER, pidsMatching, type Sandbox } from "./testkit.ts"

type Check = {
  name: string
  status: "ok" | "warn" | "fail" | "skipped"
  summary: string
  message?: string
}

type Machine = {
  load_average?: number[]
  swap_free_mb?: number
  swap_used_mb?: number
  swap_total_mb?: number
}

type SelfCheck =
  | {
    ran: true
    diverged: boolean
    seed: number
    idle_ms: number
    fixture: string
    first_divergence?: { step: number; cmd: string; differences: { field: string }[] }
    transcripts: string[]
    machine: { before: Machine; after: Machine }
    message: string
  }
  | { ran: false; reason: string }

type Report = {
  healthy: boolean
  driver_available: boolean
  checks: Check[]
  machine: Machine
  self_check?: SelfCheck
}

type Rig = {
  sandbox: Sandbox
  /** The game binary: a private copy of the mock, so strays are told apart per test. */
  binary: string
  /** The checkout the binary claims to be built from. */
  source: string
  /** Name of the fixture added to the library. */
  fixture: string
  doctor(args?: string[], env?: Record<string, string>): Promise<{ code: number; report: Report }>
}

/** Settings under which this machine's real memory and swap cannot fail a preflight. */
const ROOMY = {
  BNPLAY_MIN_FREE_MEMORY_MB: "1",
  BNPLAY_MIN_FREE_SWAP_MB: "0",
  BNPLAY_SELFCHECK_IDLE_MS: "400",
  BNPLAY_BASELINE_IDLE_MS: "200",
}

type RigOptions = {
  /** Skip `fixture baseline`. */
  noBaseline?: boolean
  /** Marker files the mock reads from the world (see mock_driver.py). */
  worldMarkers?: string[]
  /** Skip `fixture add`. */
  noFixture?: boolean
  /** Text of the binary; the mock by default. */
  binaryText?: string
  /** Leave out the lighting shader sources a windowed game cannot start without. */
  noShaders?: boolean
  /** What `launchctl managername` says: `Aqua` is a graphical login session. */
  session?: string
  env?: Record<string, string>
}

async function withRig(opts: RigOptions, body: (rig: Rig) => Promise<void>): Promise<void> {
  const dir = await Deno.makeTempDir({ prefix: "bnplay-doctor-" })
  const binary = join(dir, `game-${crypto.randomUUID().slice(0, 8)}`)
  if (opts.binaryText === undefined) {
    await Deno.copyFile(MOCK_DRIVER, binary)
  } else {
    await Deno.writeTextFile(binary, opts.binaryText)
  }
  await Deno.chmod(binary, 0o755)
  const source = join(dir, "checkout")
  await Deno.mkdir(join(source, "src", "lighting"), { recursive: true })
  await Deno.writeTextFile(join(source, "src", "main.cpp"), "int main() {}\n")
  await Deno.writeTextFile(join(source, "src", "lighting", "gpu.cpp"), "void gpu() {}\n")
  if (!opts.noShaders) {
    const shaders = join(source, "data", "shaders", "lighting", "src")
    await Deno.mkdir(shaders, { recursive: true })
    for (const name of ["emitter_glow.vert.hlsl", "emitter_glow.frag.hlsl"]) {
      await Deno.writeTextFile(join(shaders, name), "// shader\n")
    }
  }
  const launchctl = join(dir, "launchctl")
  await Deno.writeTextFile(launchctl, `#!/bin/sh\necho ${opts.session ?? "Aqua"}\n`)
  await Deno.chmod(launchctl, 0o755)
  // The binary was built after every source was last touched.
  const now = Date.now() / 1000
  await Deno.utime(join(source, "src", "main.cpp"), now - 120, now - 120)
  await Deno.utime(join(source, "src", "lighting", "gpu.cpp"), now - 90, now - 90)
  await Deno.utime(binary, now - 30, now - 30)

  const sandbox = await makeSandbox({
    binary,
    env: { ...ROOMY, BNPLAY_BASEPATH: source, BNPLAY_LAUNCHCTL: launchctl, ...opts.env },
  })
  const world = await makeFakeWorld()
  try {
    for (const marker of opts.worldMarkers ?? []) {
      await Deno.writeTextFile(join(world, marker), "")
    }
    const fixture = "town"
    if (!opts.noFixture) {
      const added = await sandbox.cli(["fixture", "add", world, fixture])
      assertEquals(added.code, 0, added.stderr)
      if (!opts.noBaseline) {
        const baseline = await sandbox.cli(["fixture", "baseline", fixture])
        assertEquals(baseline.code, 0, baseline.stderr)
      }
    }
    await body({
      sandbox,
      binary,
      source,
      fixture,
      async doctor(args = [], env) {
        const res = await sandbox.cli(["doctor", ...args], env)
        return { code: res.code, report: JSON.parse(res.stdout) }
      },
    })
  } finally {
    await sandbox.cleanup()
    await Deno.remove(world, { recursive: true })
    await Deno.remove(dir, { recursive: true })
  }
}

function check(report: Report, name: string): Check {
  const found = report.checks.find((c) => c.name === name)
  assert(found, `the report has no ${name} check: ${report.checks.map((c) => c.name)}`)
  return found
}

async function episodeDirs(sandbox: Sandbox): Promise<string[]> {
  const names: string[] = []
  try {
    for await (const e of Deno.readDir(join(sandbox.home, "episodes"))) names.push(e.name)
  } catch (e) {
    if (!(e instanceof Deno.errors.NotFound)) throw e
  }
  return names.sort()
}

Deno.test("doctor passes on a healthy setup and starts no game", async () => {
  await withRig({}, async (rig) => {
    const episodesBefore = await episodeDirs(rig.sandbox)
    const { code, report } = await rig.doctor()
    assertEquals(code, 0)
    assertEquals(report.healthy, true)
    assertEquals(report.driver_available, true)
    for (
      const name of [
        "driver_flag",
        "binary_fresh",
        "fixture",
        "baseline",
        "stray_processes",
        "memory",
        "swap",
      ]
    ) {
      assertEquals(check(report, name).status, "ok", JSON.stringify(check(report, name)))
    }
    // Machine state is always reported, readable by the agent.
    assertEquals(report.machine.load_average?.length, 3)
    assert(typeof report.machine.swap_free_mb === "number")
    // The self-check is off by default, and nothing was started.
    assertEquals(report.self_check, undefined)
    assertEquals(await episodeDirs(rig.sandbox), episodesBefore)
    assertEquals(await pidsMatching(rig.binary), [])
  })
})

Deno.test("doctor reports a stale binary and names the newer source and the rebuild", async () => {
  await withRig({}, async (rig) => {
    // A source edited after the binary was built.
    const edited = join(rig.source, "src", "lighting", "gpu.cpp")
    const later = Date.now() / 1000 + 60
    await Deno.utime(edited, later, later)

    const { code, report } = await rig.doctor()
    assertEquals(code, 1)
    assertEquals(report.healthy, false)
    const stale = check(report, "binary_fresh")
    assertEquals(stale.status, "fail")
    assert(stale.summary.includes(edited), stale.summary)
    assert(stale.message?.includes("Rebuild"), stale.message)
    // Staleness is its own finding: the driver flag is still there.
    assertEquals(check(report, "driver_flag").status, "ok")

    // Rebuilt: the binary is newer than the edit again.
    await Deno.utime(rig.binary, later + 60, later + 60)
    const fixed = await rig.doctor()
    assertEquals(fixed.code, 0)
    assertEquals(check(fixed.report, "binary_fresh").status, "ok")
  })
})

Deno.test("doctor reports a missing or stale baseline and a missing fixture, each with the command to fix it", async () => {
  await withRig({ noBaseline: true }, async (rig) => {
    const missing = await rig.doctor()
    assertEquals(missing.code, 1)
    const baseline = check(missing.report, "baseline")
    assertEquals(baseline.status, "fail")
    assert(baseline.message?.includes(`bnplay fixture baseline ${rig.fixture}`), baseline.message)

    assertEquals((await rig.sandbox.cli(["fixture", "baseline", rig.fixture])).code, 0)
    assertEquals((await rig.doctor()).code, 0)

    // The fixture changes after its baseline was taken.
    const player = join(rig.sandbox.fixtures, rig.fixture, "player.sav")
    await Deno.writeTextFile(player, (await Deno.readTextFile(player)) + "a later save\n")
    const stale = await rig.doctor()
    assertEquals(stale.code, 1)
    assert(
      check(stale.report, "baseline").message?.includes(`bnplay fixture baseline ${rig.fixture}`),
      JSON.stringify(check(stale.report, "baseline")),
    )

    const absent = await rig.doctor(["--fixture", "nowhere"])
    assertEquals(absent.code, 1)
    const fixture = check(absent.report, "fixture")
    assertEquals(fixture.status, "fail")
    assert(fixture.message?.includes("bnplay fixture add"), fixture.message)
  })
})

Deno.test("doctor reports an empty fixture library", async () => {
  await withRig({ noFixture: true }, async (rig) => {
    const { code, report } = await rig.doctor()
    assertEquals(code, 1)
    const fixture = check(report, "fixture")
    assertEquals(fixture.status, "fail")
    assert(fixture.message?.includes("bnplay fixture add"), fixture.message)
  })
})

Deno.test("doctor reports stray driver processes but not the daemon's own Episodes", async () => {
  await withRig({}, async (rig) => {
    // A live Episode of this daemon is not a stray.
    const trial = await rig.sandbox.trial(`fixture = "${rig.fixture}"\n`)
    const started = await rig.sandbox.cli(["start", trial])
    assertEquals(started.code, 0, started.stderr)
    const session = JSON.parse(started.stdout).session as string
    const own = await rig.doctor()
    assertEquals(check(own.report, "stray_processes").status, "ok")
    assertEquals((await rig.sandbox.cli(["stop", session])).code, 0)

    // A driver process nobody owns: the game's command line, run by hand.
    const stray = new Deno.Command("/bin/sh", {
      args: ["-c", "read line", "sh", rig.binary, "--userdir", "/tmp/nobody/", "--driver-fd", "3"],
      stdin: "piped",
      stdout: "null",
      stderr: "null",
    }).spawn()
    try {
      const { code, report } = await rig.doctor()
      assertEquals(code, 1)
      const found = check(report, "stray_processes")
      assertEquals(found.status, "fail")
      assert(found.summary.includes(`pid ${stray.pid}`), found.summary)
      assert(found.message?.includes("kill -KILL"), found.message)
    } finally {
      stray.kill("SIGKILL")
      await stray.status
    }
    assertEquals(check((await rig.doctor()).report, "stray_processes").status, "ok")
  })
})

Deno.test("doctor reports low memory and swap with what to do", async () => {
  // The daemon reads its thresholds when it starts, so each setting gets a daemon of its own.
  await withRig({ env: { BNPLAY_MIN_FREE_MEMORY_MB: "999999999" } }, async (rig) => {
    const memory = await rig.doctor()
    assertEquals(memory.code, 1)
    const low = check(memory.report, "memory")
    assertEquals(low.status, "fail")
    assert(/only \d+ MB of memory is available/.test(low.summary), low.summary)
    assert(low.message?.includes("Close applications"), low.message)
    assertEquals(check(memory.report, "swap").status, "ok")
  })
  await withRig({ env: { BNPLAY_MIN_FREE_SWAP_MB: "999999999" } }, async (rig) => {
    const swap = await rig.doctor()
    assertEquals(swap.code, 1)
    const low = check(swap.report, "swap")
    assertEquals(low.status, "fail")
    assert(/swap is nearly exhausted: \d+(\.\d+)? MB free of/.test(low.summary), low.summary)
    assert(low.message?.includes("rerun doctor"), low.message)
  })
})

const WORKFLOWS = ["file-trigger", "Windows harness", "test suite"]

Deno.test("doctor says the driver is unavailable when the binary lacks the flag, and names the existing workflows", async () => {
  const oldGame = "#!/bin/sh\necho 'an older build: --userdir --world --basepath'\n"
  await withRig({ binaryText: oldGame, noBaseline: true }, async (rig) => {
    const episodesBefore = await episodeDirs(rig.sandbox)
    const { code, report } = await rig.doctor()
    assertEquals(code, 1)
    assertEquals(report.driver_available, false)
    const driver = check(report, "driver_flag")
    assertEquals(driver.status, "fail")
    assert(driver.summary.includes("driver is unavailable"), driver.summary)
    assert(driver.message?.includes("does not emulate the driver"), driver.message)
    for (const workflow of WORKFLOWS) {
      assert(driver.message?.includes(workflow), `the message does not name ${workflow}`)
    }
    // Nothing was started, and nothing pretends to be the driver.
    assertEquals(await episodeDirs(rig.sandbox), episodesBefore)
  })
})

Deno.test("doctor says the driver is unavailable when there is no binary at all", async () => {
  await withRig({}, async (rig) => {
    await Deno.remove(rig.binary)
    const { code, report } = await rig.doctor()
    assertEquals(code, 1)
    assertEquals(report.driver_available, false)
    const driver = check(report, "driver_flag")
    assert(driver.summary.includes(rig.binary), driver.summary)
    for (const workflow of WORKFLOWS) assert(driver.message?.includes(workflow), driver.message)
    assertEquals(check(report, "binary_fresh").status, "skipped")
  })
})

Deno.test("the self-check plays an A/A pair with one seed and an idle gap, and reports load and swap", async () => {
  await withRig({}, async (rig) => {
    const { code, report } = await rig.doctor(["--self-check"])
    assertEquals(code, 0)
    const self = report.self_check
    assert(self?.ran, JSON.stringify(self))
    assertEquals(self.diverged, false)
    assertEquals(self.fixture, rig.fixture)
    assertEquals(self.first_divergence, undefined)
    assert(self.message.includes("agreed"), self.message)

    // Two Episodes, ended, one after the other.
    assertEquals(self.transcripts.length, 2)
    const records = await Promise.all(
      self.transcripts.map(async (path) =>
        (await Deno.readTextFile(path)).trim().split("\n").map((l) => JSON.parse(l))
      ),
    )
    const [a, b] = records
    for (const transcript of records) {
      const ended = transcript.at(-1)
      assertEquals(ended.event, "end")
      assertEquals(ended.detail.reason, "stop")
      // The same seed, then real idle time before the first world step.
      const seed = transcript.find((r) => r.request?.cmd === "seed")
      assertEquals(seed.request.seed, self.seed)
      const ready = transcript.find((r) => r.event === "ready")
      const firstStep = transcript.find((r) => r.request?.cmd === "wait")
      assert(firstStep.ms - ready.ms >= self.idle_ms - 10, `${firstStep.ms - ready.ms} ms idle`)
    }
    assertEquals(
      a.find((r) => r.request?.cmd === "seed").request.seed,
      b.find((r) => r.request?.cmd === "seed").request.seed,
    )

    // Load and swap, before and after, beside the verdict.
    for (const machine of [self.machine.before, self.machine.after]) {
      assertEquals(machine.load_average?.length, 3)
      for (const load of machine.load_average ?? []) assert(load >= 0)
      assert(typeof machine.swap_free_mb === "number", JSON.stringify(machine))
      assert(typeof machine.swap_used_mb === "number", JSON.stringify(machine))
    }
    // No game left running, and the Episodes are not kept as sessions.
    assertEquals(await pidsMatching(rig.binary), [])
    assertEquals(await pidsMatching(rig.sandbox.home), [])
    for (const transcript of self.transcripts) {
      const gone = await rig.sandbox.cli(["step", basename(dirname(transcript)), '{"cmd":"state"}'])
      assertEquals(gone.code, 2)
      assert(gone.stderr.includes("no session"), gone.stderr)
    }
  })
})

Deno.test("the self-check reports where same-seed Episodes diverged", async () => {
  await withRig({ worldMarkers: ["mock_diverge"] }, async (rig) => {
    const { code, report } = await rig.doctor(["--self-check"])
    // A divergence is information about the machine, not a failed preflight.
    assertEquals(code, 0)
    assertEquals(report.healthy, true)
    const self = report.self_check
    assert(self?.ran, JSON.stringify(self))
    assertEquals(self.diverged, true)
    assertEquals(self.first_divergence?.cmd, "wait")
    assertEquals(self.first_divergence?.differences.map((d) => d.field), ["pain"])
    assert(self.message.includes("diverged"), self.message)
    // Load and swap come with the divergence.
    assertEquals(self.machine.before.load_average?.length, 3)
    assert(typeof self.machine.after.swap_free_mb === "number")
  })
})

Deno.test("the self-check does not run without a driver, on a stray game, or beyond the session cap", async () => {
  const oldGame = "#!/bin/sh\necho 'an older build'\n"
  await withRig({ binaryText: oldGame, noBaseline: true }, async (rig) => {
    const episodesBefore = await episodeDirs(rig.sandbox)
    const { code, report } = await rig.doctor(["--self-check"])
    assertEquals(code, 1)
    const self = report.self_check
    assertEquals(self?.ran, false)
    assert(self && !self.ran && self.reason.includes("driver is unavailable"), JSON.stringify(self))
    assertEquals(await episodeDirs(rig.sandbox), episodesBefore)
  })

  await withRig({}, async (rig) => {
    const stray = new Deno.Command("/bin/sh", {
      args: ["-c", "read line", "sh", rig.binary, "--userdir", "/tmp/nobody/", "--driver-fd", "3"],
      stdin: "piped",
      stdout: "null",
      stderr: "null",
    }).spawn()
    try {
      const episodesBefore = await episodeDirs(rig.sandbox)
      const { code, report } = await rig.doctor(["--self-check"])
      assertEquals(code, 1)
      const self = report.self_check
      assert(self && !self.ran && self.reason.includes("stray"), JSON.stringify(self))
      assertEquals(await episodeDirs(rig.sandbox), episodesBefore)
    } finally {
      stray.kill("SIGKILL")
      await stray.status
    }
  })

  // The self-check's games count against the session cap like any other.
  await withRig({ env: { BNPLAY_MAX_SESSIONS: "1" } }, async (rig) => {
    const trial = await rig.sandbox.trial(`fixture = "${rig.fixture}"\n`)
    const started = await rig.sandbox.cli(["start", trial])
    assertEquals(started.code, 0, started.stderr)
    const { code, report } = await rig.doctor(["--self-check"])
    assertEquals(code, 1)
    const self = report.self_check
    assert(self && !self.ran && self.reason.includes("session limit"), JSON.stringify(self))
    assertEquals((await rig.sandbox.cli(["stop", JSON.parse(started.stdout).session])).code, 0)
  })
})

Deno.test("the self-check needs --fixture when the library holds several fixtures", async () => {
  await withRig({}, async (rig) => {
    const second = await makeFakeWorld()
    try {
      assertEquals((await rig.sandbox.cli(["fixture", "add", second, "village"])).code, 0)
      assertEquals((await rig.sandbox.cli(["fixture", "baseline", "village"])).code, 0)
      const ambiguous = await rig.doctor(["--self-check"])
      const self = ambiguous.report.self_check
      assert(self && !self.ran && self.reason.includes("--fixture"), JSON.stringify(self))
      assertEquals(ambiguous.code, 1)

      const named = await rig.doctor(["--self-check", "--fixture", "village"])
      assertEquals(named.code, 0)
      assert(named.report.self_check?.ran)
      assertEquals(named.report.self_check.fixture, "village")
    } finally {
      await Deno.remove(second, { recursive: true })
    }
  })
})

Deno.test("doctor refuses arguments it does not know", async () => {
  await withRig({ noFixture: true }, async (rig) => {
    const res = await rig.sandbox.cli(["doctor", "--selfcheck"])
    assertEquals(res.code, 2)
    assert(res.stderr.includes("--selfcheck"), res.stderr)
    const bare = await rig.sandbox.cli(["doctor", "--fixture"])
    assertEquals(bare.code, 2)
  })
})

const WINDOWED_CHECKS = ["display_session", "stray_windows", "shader_sources"]

async function windowedTrial(rig: Rig): Promise<string> {
  return await rig.sandbox.trial(`fixture = "${rig.fixture}"\nmode = "windowed"\n`)
}

async function windowlessTrial(rig: Rig): Promise<string> {
  return await rig.sandbox.trial(`fixture = "${rig.fixture}"\n`)
}

Deno.test("doctor passes a windowed Trial on a graphical session with no stray windows and the shader sources", async () => {
  await withRig({}, async (rig) => {
    const { code, report } = await rig.doctor(["--trial", await windowedTrial(rig)])
    assertEquals(code, 0, JSON.stringify(report.checks))
    for (const name of WINDOWED_CHECKS) assertEquals(check(report, name).status, "ok", name)
  })
})

Deno.test("doctor makes the windowed checks for windowed Trials only", async () => {
  // Everything a windowed game needs is missing, and a windowless Trial does not care.
  await withRig({ noShaders: true, session: "Background" }, async (rig) => {
    const stray = new Deno.Command("/bin/sh", {
      args: ["-c", "read line", "sh", rig.binary, "--userdir", "/tmp/nobody/"],
      stdin: "piped",
      stdout: "null",
      stderr: "null",
    }).spawn()
    try {
      for (const args of [[], ["--trial", await windowlessTrial(rig)]]) {
        const { code, report } = await rig.doctor(args)
        assertEquals(code, 0, JSON.stringify(report.checks))
        for (const name of WINDOWED_CHECKS) {
          assertEquals(report.checks.some((c) => c.name === name), false, `${name} ${args}`)
        }
      }
      const windowed = await rig.doctor(["--trial", await windowedTrial(rig)])
      assertEquals(windowed.code, 1)
      for (const name of WINDOWED_CHECKS) assertEquals(check(windowed.report, name).status, "fail")
    } finally {
      stray.kill("SIGKILL")
      await stray.status
    }
  })
})

Deno.test("doctor reports a missing display session and what it means", async () => {
  await withRig({ session: "Background" }, async (rig) => {
    const { code, report } = await rig.doctor(["--trial", await windowedTrial(rig)])
    assertEquals(code, 1)
    const found = check(report, "display_session")
    assertEquals(found.status, "fail")
    assert(found.summary.includes("Background"), found.summary)
    assert(found.message?.includes("login session"), found.message)
    assertEquals(check(report, "stray_windows").status, "ok")
  })
})

Deno.test("doctor reports a stray game window, an interactive game or a windowed driver nobody owns", async () => {
  await withRig({}, async (rig) => {
    const spawn = (...flags: string[]) =>
      new Deno.Command("/bin/sh", {
        args: ["-c", "read line", "sh", rig.binary, "--userdir", "/tmp/nobody/", ...flags],
        stdin: "piped",
        stdout: "null",
        stderr: "null",
      }).spawn()
    const trial = await windowedTrial(rig)
    const interactive = spawn()
    try {
      const { code, report } = await rig.doctor(["--trial", trial])
      assertEquals(code, 1)
      const found = check(report, "stray_windows")
      assertEquals(found.status, "fail")
      assert(found.summary.includes(`pid ${interactive.pid}`), found.summary)
      assert(found.message?.includes("kill -KILL"), found.message)
      // The checks that are not about windows are not touched.
      assertEquals(check(report, "stray_processes").status, "ok")
    } finally {
      interactive.kill("SIGKILL")
      await interactive.status
    }
    const windowedDriver = spawn("--driver-fd", "3", "--driver-windowed", "640x384")
    try {
      const { report } = await rig.doctor(["--trial", trial])
      assertEquals(check(report, "stray_windows").status, "fail")
    } finally {
      windowedDriver.kill("SIGKILL")
      await windowedDriver.status
    }
    assertEquals(check((await rig.doctor(["--trial", trial])).report, "stray_windows").status, "ok")
  })
})

Deno.test("a windowed Episode of this daemon is not a stray window", async () => {
  await withRig({}, async (rig) => {
    const trial = await windowedTrial(rig)
    const started = await rig.sandbox.cli(["start", trial])
    assertEquals(started.code, 0, started.stderr)
    try {
      const { report } = await rig.doctor(["--trial", trial])
      assertEquals(check(report, "stray_windows").status, "ok")
    } finally {
      await rig.sandbox.cli(["stop", JSON.parse(started.stdout).session])
    }
  })
})

Deno.test("doctor says the lighting shader sources are untracked and where to copy them from", async () => {
  await withRig({ noShaders: true }, async (rig) => {
    const { code, report } = await rig.doctor(["--trial", await windowedTrial(rig)])
    assertEquals(code, 1)
    const found = check(report, "shader_sources")
    assertEquals(found.status, "fail")
    for (const name of ["emitter_glow.vert.hlsl", "emitter_glow.frag.hlsl"]) {
      assert(found.summary.includes(name), found.summary)
    }
    const message = found.message ?? ""
    // What is true: gitignored, never committed, no build step makes them; copy or track them.
    assert(message.includes("untracked"), message)
    assert(message.includes("no build step"), message)
    assert(message.includes("main checkout"), message)
    assert(message.includes("data/shaders/lighting/src"), message)
    assert(message.includes("tracked by a separate repo fix"), message)
  })
})

Deno.test("doctor names only the shader source that is missing", async () => {
  await withRig({}, async (rig) => {
    const frag = join(rig.source, "data", "shaders", "lighting", "src", "emitter_glow.frag.hlsl")
    await Deno.remove(frag)
    const { report } = await rig.doctor(["--trial", await windowedTrial(rig)])
    const found = check(report, "shader_sources")
    assertEquals(found.status, "fail")
    assert(found.summary.includes("emitter_glow.frag.hlsl"), found.summary)
    assertEquals(found.summary.includes("emitter_glow.vert.hlsl"), false, found.summary)
  })
})
