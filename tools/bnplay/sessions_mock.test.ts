/** Session management against the mock driver: the concurrent-session cap and the idle reaper. */
import { assert, assertEquals } from "@std/assert"
import { delay } from "@std/async"
import { join } from "@std/path"
import {
  eventually,
  jsonOut,
  makeFakeWorld,
  makeSandbox,
  pidAlive,
  pidsMatching,
  readTranscript,
  type Sandbox,
} from "./testkit.ts"

async function withSandbox(
  body: (sandbox: Sandbox) => Promise<void>,
  env?: Record<string, string>,
): Promise<void> {
  const world = await makeFakeWorld()
  const sandbox = await makeSandbox({ fixtureSources: { bairdford: world }, env })
  try {
    await body(sandbox)
  } finally {
    await sandbox.cleanup()
    await Deno.remove(world, { recursive: true })
  }
}

async function startCli(sandbox: Sandbox) {
  return await sandbox.cli(["start", await sandbox.trial(`fixture = "bairdford"\n`)])
}

async function start(sandbox: Sandbox) {
  const res = await startCli(sandbox)
  assertEquals(res.code, 0, res.stderr)
  return jsonOut<{ session: string; transcript: string }>(res)
}

async function episodeDirs(sandbox: Sandbox): Promise<string[]> {
  const names: string[] = []
  for await (const e of Deno.readDir(join(sandbox.home, "episodes"))) names.push(e.name)
  return names.sort()
}

Deno.test("a start beyond the default cap of two sessions is refused and starts no process", async () => {
  await withSandbox(async (sandbox) => {
    const a = await start(sandbox)
    const b = await start(sandbox)
    const dirsBefore = await episodeDirs(sandbox)
    const pidsBefore = await pidsMatching(join(sandbox.home, "episodes"))
    assertEquals(dirsBefore.length, 2)

    const refused = await startCli(sandbox)
    assertEquals(refused.code, 2)
    assert(/session limit/i.test(refused.stderr), refused.stderr)
    assert(refused.stderr.includes("2"), refused.stderr)

    assertEquals(await episodeDirs(sandbox), dirsBefore)
    assertEquals(await pidsMatching(join(sandbox.home, "episodes")), pidsBefore)

    // A slot freed by `stop` is usable again.
    assertEquals((await sandbox.cli(["stop", a.session])).code, 0)
    const c = await start(sandbox)
    assertEquals((await startCli(sandbox)).code, 2)

    for (const s of [b.session, c.session]) await sandbox.cli(["stop", s])
  })
})

Deno.test("the session cap is configurable", async () => {
  await withSandbox(async (sandbox) => {
    const a = await start(sandbox)
    const refused = await startCli(sandbox)
    assertEquals(refused.code, 2)
    assert(/session limit/i.test(refused.stderr), refused.stderr)
    assert(refused.stderr.includes("1"), refused.stderr)
    await sandbox.cli(["stop", a.session])
  }, { BNPLAY_MAX_SESSIONS: "1" })
})

Deno.test("starts racing for the last slots never exceed the cap", async () => {
  await withSandbox(async (sandbox) => {
    // Warm the daemon so a daemon-spawn race cannot be what refuses a start.
    await sandbox.cli(["stop", "nosuch"])
    // Each boot takes 2 s, so all three starts are in flight together.
    const results = await Promise.all([startCli(sandbox), startCli(sandbox), startCli(sandbox)])
    const refused = results.filter((r) => r.code !== 0)
    assertEquals(results.length - refused.length, 2, results.map((r) => r.stderr).join("\n"))
    assertEquals(refused.length, 1)
    assertEquals(refused[0].code, 2)
    assert(/session limit/i.test(refused[0].stderr), refused[0].stderr)
    assertEquals((await episodeDirs(sandbox)).length, 2)
  }, { MOCK_BOOT_DELAY_S: "2" })
})

Deno.test("an idle session is reaped by process group, keeps its transcript and reports exit 2", async () => {
  await withSandbox(async (sandbox) => {
    const { session, transcript } = await start(sandbox)
    const spawned = await sandbox.cli(["step", session, '{"cmd":"spawn_child"}'])
    const child = jsonOut<{ child_pid: number }>(spawned).child_pid
    assert(pidAlive(child))
    // The client now goes away without ever calling stop, as a crashed one would.

    assert(
      await eventually(
        async () => (await pidsMatching(join(sandbox.home, "episodes", session))).length === 0,
        15_000,
      ),
      "the game was still running long after the idle timeout",
    )
    assert(await eventually(() => !pidAlive(child)), "grandchild survived the reaper")

    const records = await readTranscript(transcript)
    assert(
      records.some((r) => r.request?.cmd === "spawn_child"),
      "the transcript lost its requests",
    )
    const end = records.filter((r) => r.event === "end")
    assertEquals(end.length, 1)
    assertEquals(end[0].detail?.reason, "idle_timeout")
    assertEquals(end[0].detail?.exit_code, 2)

    const later = await sandbox.cli(["step", session, '{"cmd":"state"}'])
    assertEquals(later.code, 2)
    assert(/idle/i.test(later.stderr), later.stderr)

    const stop = await sandbox.cli(["stop", session])
    assertEquals(stop.code, 2, stop.stderr) // a reaped session is a harness error
    const summary = jsonOut(stop)
    assertEquals(summary.ended, "idle_timeout")
    assertEquals(summary.exit_code, 2)
    assertEquals(summary.transcript, transcript)
  }, { BNPLAY_IDLE_TIMEOUT_MS: "1500" })
})

Deno.test("a session in use is not reaped and a reaped session frees its slot", async () => {
  await withSandbox(async (sandbox) => {
    const busy = await start(sandbox)
    const idle = await start(sandbox)
    // Keep one session busy for longer than the idle timeout while the other one expires.
    const until = performance.now() + 4_000
    while (performance.now() < until) {
      const res = await sandbox.cli(["step", busy.session, '{"cmd":"state"}'])
      assertEquals(res.code, 0, res.stderr)
      await delay(300)
    }
    // The reaped session no longer counts against the cap, even before anyone stops it.
    const replacement = await start(sandbox)
    assertEquals(
      jsonOut(await sandbox.cli(["stop", idle.session])).ended,
      "idle_timeout",
    )
    for (const s of [busy.session, replacement.session]) await sandbox.cli(["stop", s])
  }, { BNPLAY_IDLE_TIMEOUT_MS: "2500" })
})

Deno.test("a long request is not idle time", async () => {
  await withSandbox(async (sandbox) => {
    const { session } = await start(sandbox)
    // The game answers after 3 s, longer than the idle timeout.
    const slow = await sandbox.cli(["step", session, '{"cmd":"sleep","seconds":3}'])
    assertEquals(slow.code, 0, slow.stderr)
    assertEquals(jsonOut(await sandbox.cli(["stop", session])).ended, "stop")
  }, { BNPLAY_IDLE_TIMEOUT_MS: "1500" })
})

Deno.test("the daemon remembers only the last few ended sessions, and their transcripts stay on disk", async () => {
  await withSandbox(async (sandbox) => {
    const ended: { session: string; transcript: string }[] = []
    for (let i = 0; i < 4; i++) {
      const episode = await start(sandbox)
      assertEquals((await sandbox.cli(["stop", episode.session])).code, 0)
      ended.push(episode)
    }
    // The next start forgets the oldest ended sessions beyond the two kept.
    const live = await start(sandbox)

    for (const forgotten of ended.slice(0, 2)) {
      const report = await sandbox.cli(["report", forgotten.session])
      assertEquals(report.code, 2)
      assert(report.stderr.includes("no session"), report.stderr)
      assert(await Deno.stat(forgotten.transcript).then(() => true, () => false), "transcript lost")
    }
    for (const kept of ended.slice(2)) {
      const report = await sandbox.cli(["report", kept.session])
      assertEquals(report.code, 0, report.stderr)
      assertEquals(jsonOut(report).ended, "stop")
    }
    assertEquals((await sandbox.cli(["stop", live.session])).code, 0)
  }, { BNPLAY_ENDED_SESSIONS_KEPT: "2" })
})
