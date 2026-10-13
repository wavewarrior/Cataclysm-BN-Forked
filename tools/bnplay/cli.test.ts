/**
 * End to end through the real CLI, against a fake driver that speaks the JSON-line protocol.
 *
 * The fake stands in for the game binary: one request line in on the driver descriptor, one
 * response line out, with env switches to hang or die so the timeout and death paths are exercised
 * too. Like the real driver it reads and writes the descriptor with raw syscalls (the shim hands it
 * a socketpair, which is not seekable and so cannot be a buffered text stream). Everything here
 * runs through `main.ts` as a subprocess, so the detached holder, the state file handshake and the
 * socket transport are the real ones.
 */
import { assert, assertEquals, assertMatch } from "@std/assert"
import { join } from "@std/path"
import { REPO_ROOT } from "./config.ts"

const FAKE_DRIVER = `#!/usr/bin/env python3
"""Tiny stand-in for the game's driver: one JSON request line in, one response line out."""
import json, os, sys, time

args = sys.argv[1:]
fd = int(args[args.index("--driver-fd") + 1]) if "--driver-fd" in args else 0
hang = os.environ.get("FAKE_HANG") == "1"
die_on = os.environ.get("FAKE_DIE_ON")
seed = date = clock = None
turn = 0
buf = b""

def send(obj):
    os.write(fd, (json.dumps(obj) + "\\n").encode())

def request():
    global buf
    while b"\\n" not in buf:
        chunk = os.read(fd, 4096)
        if not chunk:
            return None
        buf += chunk
    line, buf = buf.split(b"\\n", 1)
    return json.loads(line)

while True:
    req = request()
    if req is None:
        break
    cmd, rid = req.get("cmd"), req.get("id")
    if die_on == cmd:
        sys.exit(3)
    if cmd == "ping":
        send({"id": rid, "status": "ok"})
    elif cmd == "state":
        send({"id": rid, "status": "ok", "turn": turn, "seed": seed, "date": date, "time": clock})
    elif cmd == "seed":
        seed = req.get("seed")
        send({"id": rid, "status": "ok", "seed": seed})
    elif cmd == "set_time":
        date, clock = req.get("date"), req.get("time")
        send({"id": rid, "status": "ok", "date": date, "time": clock, "turn": turn})
    elif cmd == "wait":
        if hang:
            time.sleep(600)
        turn += int(req.get("turns", 1))
        send({"id": rid, "status": "ok", "turn": turn, "time_passed": True})
    elif cmd == "quit":
        send({"id": rid, "status": "ok", "saved": True})
        break
    else:
        send({"id": rid, "status": "error", "error": "unknown cmd"})
`

const MAIN = join(REPO_ROOT, "tools", "bnplay", "main.ts")
const CONFIG = join(REPO_ROOT, "deno.jsonc")

type Cli = {
  run: (
    args: string[],
    env?: Record<string, string>,
  ) => Promise<{ code: number; out: string; err: string }>
  home: string
  save: string
  dispose: () => Promise<void>
}

async function setUp(env: Record<string, string> = {}): Promise<Cli> {
  const home = await Deno.makeTempDir({ prefix: "bnplay-cli-" })
  const fake = join(home, "fake_driver.py")
  await Deno.writeTextFile(fake, FAKE_DRIVER)
  await Deno.chmod(fake, 0o755)
  const save = join(home, "save-src")
  await Deno.mkdir(save, { recursive: true })
  await Deno.writeTextFile(join(save, "worldoptions.json"), "{}")
  const base = {
    BNPLAY_HOME: home,
    BNPLAY_BINARY: fake,
    BNPLAY_BOOT_TIMEOUT_MS: "15000",
    BNPLAY_IDLE_TIMEOUT_MS: "3600000",
    ...env,
  }
  return {
    home,
    save,
    async run(args, extra = {}) {
      const r = await new Deno.Command(Deno.execPath(), {
        args: [
          "run",
          "--config",
          CONFIG,
          "--allow-run",
          "--allow-env",
          "--allow-read",
          "--allow-write",
          "--allow-net",
          MAIN,
          ...args,
        ],
        cwd: REPO_ROOT,
        env: { ...base, ...extra },
        stdout: "piped",
        stderr: "piped",
      }).output()
      return {
        code: r.code,
        out: new TextDecoder().decode(r.stdout),
        err: new TextDecoder().decode(r.stderr),
      }
    },
    async dispose() {
      // Reap any holder the test left alive, then the tree.
      const sessions = await Array.fromAsync(Deno.readDir(join(home, "sessions"))).catch(() => [])
      for (const entry of sessions) {
        const state = JSON.parse(
          await Deno.readTextFile(join(home, "sessions", entry.name, "state.json")).catch(() =>
            "{}"
          ),
        )
        if (state.status === "ready" && typeof state.pid === "number") {
          try {
            Deno.kill(state.pid, "SIGKILL")
          } catch { /* already gone */ }
        }
      }
      await Deno.remove(home, { recursive: true }).catch(() => {})
    },
  }
}

async function stateOf(cli: Cli, session: string): Promise<Record<string, unknown>> {
  const text = await Deno.readTextFile(join(cli.home, "sessions", session, "state.json"))
  return JSON.parse(text)
}

async function start(cli: Cli, extraArgs: string[] = [], env: Record<string, string> = {}) {
  const r = await cli.run(["start", cli.save, ...extraArgs], env)
  assertEquals(r.code, 0, r.err)
  // `start` prints exactly one JSON line and nothing else, so a caller can parse stdout whole and
  // is never held open by the detached holder inheriting the pipe.
  const lines = r.out.trimEnd().split("\n")
  assertEquals(lines.length, 1, r.out)
  return JSON.parse(lines[0]) as Record<string, unknown>
}

Deno.test("start boots, step forwards, stop ends the session cleanly", async () => {
  const cli = await setUp()
  try {
    const started = await start(cli)
    const session = started.session as string
    assert(/^[0-9a-f]{8}$/.test(session))
    assert(typeof started.boot_ms === "number")
    const state = await stateOf(cli, session)
    assertEquals(state.status, "ready")
    assert(String(state.userdir).startsWith(cli.home))

    const stepped = await cli.run(["step", session, '{"cmd":"state"}'])
    assertEquals(stepped.code, 0)
    assertEquals(JSON.parse(stepped.out).status, "ok")

    const refused = await cli.run(["step", session, '{"cmd":"bogus"}'])
    assertEquals(refused.code, 1) // the game answered; an error answer is a result, not a failure
    assertEquals(JSON.parse(refused.out).status, "error")

    const stopped = await cli.run(["stop", session])
    assertEquals(stopped.code, 0, stopped.err)
    const summary = JSON.parse(stopped.out)
    assertEquals(summary.ended, "stopped")
    assertEquals(summary.exit_code, 0)
    assertEquals((await stateOf(cli, session)).status, "ended")

    const after = await cli.run(["step", session, '{"cmd":"state"}'])
    assertEquals(after.code, 2)
    assertMatch(after.err, /ended \(stopped\)/)
  } finally {
    await cli.dispose()
  }
})

Deno.test("start applies the seed and clock pins before reporting ready", async () => {
  const cli = await setUp()
  try {
    const started = await start(cli, ["--seed", "42", "--date", "0001-01-03", "--time", "08:30"])
    const stepped = await cli.run(["step", started.session as string, '{"cmd":"state"}'])
    const observed = JSON.parse(stepped.out)
    assertEquals(observed.seed, 42)
    assertEquals(observed.date, "0001-01-03")
    assertEquals(observed.time, "08:30")
    await cli.run(["stop", started.session as string])
  } finally {
    await cli.dispose()
  }
})

Deno.test("start refuses a directory that is not a world save", async () => {
  const cli = await setUp()
  try {
    const r = await cli.run(["start", join(cli.home, "nowhere")])
    assertEquals(r.code, 2)
    assertMatch(r.err, /no such save directory/)
    const r2 = await cli.run(["start", cli.home])
    assertEquals(r2.code, 2)
    assertMatch(r2.err, /not a world save/)
  } finally {
    await cli.dispose()
  }
})

Deno.test("a hung game is killed, and the session ends as timed out", async () => {
  const cli = await setUp({ BNPLAY_STEP_TIMEOUT_MS: "300", BNPLAY_TURN_TIMEOUT_MS: "0" })
  try {
    const started = await start(cli, [], { FAKE_HANG: "1" })
    const stepped = await cli.run(["step", started.session as string, '{"cmd":"wait","turns":1}'])
    assertEquals(stepped.code, 2)
    assertMatch(stepped.err, /process group was killed/)
    const state = await stateOf(cli, started.session as string)
    assertEquals(state.status, "ended")
    assertEquals(state.ended, "timeout")
  } finally {
    await cli.dispose()
  }
})

Deno.test("a game that dies mid-step ends the session as gone", async () => {
  const cli = await setUp()
  try {
    const started = await start(cli, [], { FAKE_DIE_ON: "state" })
    const stepped = await cli.run(["step", started.session as string, '{"cmd":"state"}'])
    assertEquals(stepped.code, 2)
    assertMatch(stepped.err, /the game died during cmd=state/)
    assertEquals((await stateOf(cli, started.session as string)).ended, "gone")
  } finally {
    await cli.dispose()
  }
})

Deno.test("an idle session is reaped by its holder", async () => {
  // Real time: the reaper is a detached process whose only knob is its idle budget, so the test
  // polls the state file the holder writes until it flips, with a generous ceiling.
  const cli = await setUp({ BNPLAY_IDLE_TIMEOUT_MS: "600" })
  try {
    const started = await start(cli)
    const session = started.session as string
    const deadline = Date.now() + 10_000
    let state: Record<string, unknown> = {}
    while (Date.now() < deadline) {
      state = await stateOf(cli, session)
      if (state.status === "ended") break
      await new Promise((resolve) => setTimeout(resolve, 100))
    }
    assertEquals(state.status, "ended")
    assertEquals(state.ended, "idle_timeout")
  } finally {
    await cli.dispose()
  }
})

Deno.test("usage mistakes are refused with exit 2", async () => {
  const cli = await setUp()
  try {
    assertEquals((await cli.run([])).code, 2)
    assertEquals((await cli.run(["frobnicate"])).code, 2)
    assertEquals((await cli.run(["start"])).code, 2)
    assertEquals((await cli.run(["start", cli.save, "--nope"])).code, 2)
    assertEquals((await cli.run(["step", "ab12cd34", "not json"])).code, 2)
    assertEquals((await cli.run(["step", "ab12cd34", '{"cmd":7}'])).code, 2)
    assertEquals((await cli.run(["stop"])).code, 2)
  } finally {
    await cli.dispose()
  }
})
