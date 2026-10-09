/** The CLI front end end to end against the mock driver: lifecycle, isolation, watchdog, errors. */
import { assert, assertEquals, assertNotEquals } from "@std/assert"
import { join } from "@std/path"
import { runCliLifecycle } from "./cli_lifecycle.ts"
import { connectDaemon } from "./ipc.ts"
import {
  eventually,
  FAILING_BINARY,
  jsonOut,
  makeFakeWorld,
  makeSandbox,
  MOCK_DRIVER,
  pidAlive,
  pidsMatching,
  readTranscript,
  type Sandbox,
  setSuspended,
  treeSnapshot,
} from "./testkit.ts"

runCliLifecycle("mock driver", {
  binary: MOCK_DRIVER,
  fixture: "bairdford",
  fixtureSource: makeFakeWorld,
  disposeFixtureSource: (dir) => Deno.remove(dir, { recursive: true }),
  // A wait of a thousand turns takes the mock two seconds: far past the plain step timeout, well
  // inside the time a thousand turns are given.
  env: {
    MOCK_TURN_DELAY_MS: "2",
    BNPLAY_STEP_TIMEOUT_MS: "700",
    BNPLAY_TURN_TIMEOUT_MS: "20",
  },
})

type Info = { userdir: string; world: string }

/** Runs `body` in a sandbox holding one fake-world fixture named `bairdford`. */
async function withSandbox(
  body: (sandbox: Sandbox) => Promise<void>,
  env?: Record<string, string>,
  binary?: string,
): Promise<void> {
  const world = await makeFakeWorld()
  const sandbox = await makeSandbox({ fixtureSources: { bairdford: world }, env, binary })
  try {
    await body(sandbox)
  } finally {
    await sandbox.cleanup()
    await Deno.remove(world, { recursive: true })
  }
}

async function start(sandbox: Sandbox, toml = `fixture = "bairdford"\n`) {
  const res = await sandbox.cli(["start", await sandbox.trial(toml)])
  assertEquals(res.code, 0, res.stderr)
  return jsonOut<{ session: string; transcript: string }>(res)
}

async function step<T extends object = Record<string, unknown>>(
  sandbox: Sandbox,
  session: string,
  cmd: string,
) {
  const res = await sandbox.cli(["step", session, JSON.stringify({ cmd })])
  // The response is only JSON when the command succeeded; failures print an error on stderr.
  return { ...res, json: res.code === 0 ? jsonOut<T>(res) : undefined }
}

Deno.test("each Episode runs on its own clone and the source fixture is never touched", async () => {
  await withSandbox(async (sandbox) => {
    const fixtureBefore = await treeSnapshot(join(sandbox.fixtures, "bairdford"))
    const a = await start(sandbox)
    const b = await start(sandbox)
    assertNotEquals(a.session, b.session)

    const infoA = (await step<Info>(sandbox, a.session, "info")).json!
    const infoB = (await step<Info>(sandbox, b.session, "info")).json!
    assertNotEquals(infoA.userdir, infoB.userdir)
    for (const [info, episode] of [[infoA, a], [infoB, b]] as const) {
      assert(!String(info.userdir).startsWith(sandbox.fixtures))
      assertEquals(info.world, `bairdford-${episode.session}`)
    }
    assertNotEquals(infoA.world, infoB.world)

    // The game scribbles into its own clone of the world.
    assertEquals((await step(sandbox, a.session, "dirty")).json?.status, "ok")
    assertEquals(await treeSnapshot(join(sandbox.fixtures, "bairdford")), fixtureBefore)
    const cloneA = await treeSnapshot(join(infoA.userdir, "save", infoA.world))
    const cloneB = await treeSnapshot(join(infoB.userdir, "save", infoB.world))
    assertEquals(cloneA["/scribble"], "written by the mock\n")
    assertEquals(cloneB["/scribble"], undefined)
    assertEquals(cloneB["/player.sav"], fixtureBefore["/player.sav"])

    await sandbox.cli(["stop", a.session])
    await sandbox.cli(["stop", b.session])
  })
})

Deno.test("a caller-supplied request id cannot derail the Episode", async () => {
  await withSandbox(async (sandbox) => {
    const { session } = await start(sandbox)
    const res = await sandbox.cli(["step", session, '{"cmd":"ping","id":7}'])
    assertEquals(res.code, 0, res.stderr)
    assertEquals(jsonOut(res).status, "ok")
    await sandbox.cli(["stop", session])
  })
})

Deno.test("a hung game is killed by process group when the wall-clock limit expires", async () => {
  await withSandbox(async (sandbox) => {
    const { session, transcript } = await start(
      sandbox,
      `fixture = "bairdford"\nwall_clock_limit_s = 4\n`,
    )
    const child = (await step<{ child_pid: number }>(sandbox, session, "spawn_child")).json!
      .child_pid
    assert(await pidAlive(child))

    const started = performance.now()
    const hung = await step(sandbox, session, "hang")
    assertEquals(hung.code, 2)
    assert(hung.stderr.includes("wall_clock"), hung.stderr)
    assert(performance.now() - started < 12_000, "the watchdog did not fire in time")

    assert(await eventually(async () => !(await pidAlive(child))), "grandchild survived the kill")
    assertEquals(await pidsMatching(join(sandbox.home, "episodes", session)), [])

    const later = await step(sandbox, session, "state")
    assertEquals(later.code, 2)
    assert(later.stderr.includes("wall_clock"), later.stderr)

    const stop = await sandbox.cli(["stop", session])
    assertEquals(stop.code, 2, stop.stderr) // a hang the watchdog had to kill is a harness error
    assertEquals(jsonOut(stop).ended, "wall_clock")
    const events = (await readTranscript(transcript)).flatMap((r) => r.event ? [r] : [])
    assertEquals(events.at(-1)?.event, "end")
    assertEquals(events.at(-1)?.detail?.reason, "wall_clock")
  })
})

Deno.test("a request that gets no answer in time kills the game and ends the Episode", async () => {
  await withSandbox(async (sandbox) => {
    const { session, transcript } = await start(sandbox)
    const child = (await step<{ child_pid: number }>(sandbox, session, "spawn_child")).json!
      .child_pid

    const hung = await step(sandbox, session, "hang")
    assertEquals(hung.code, 2)
    assert(hung.stderr.includes("hang"), hung.stderr)
    assert(await eventually(async () => !(await pidAlive(child))), "grandchild survived the kill")
    assertEquals(await pidsMatching(join(sandbox.home, "episodes", session)), [])
    assertEquals(jsonOut(await sandbox.cli(["stop", session])).ended, "hang")
    // The unanswered request is paired with the failure that ended it, not left dangling.
    const records = await readTranscript(transcript)
    const hang = records.find((r) => r.request?.cmd === "hang")!
    const failure = records.find((r) => r.failure?.id === hang.request!.id)
    assert(failure, "the hung request has no failure record")
    assert(failure.failure!.message.includes("timeout"), failure.failure!.message)
  }, { BNPLAY_STEP_TIMEOUT_MS: "700" })
})

Deno.test("a game that dies at boot fails the start with exit 2 and starts no session", async () => {
  await withSandbox(
    async (sandbox) => {
      const res = await sandbox.cli(["start", await sandbox.trial(`fixture = "bairdford"\n`)])
      assertEquals(res.code, 2)
      assert(res.stderr.includes("boot"), res.stderr)
      assertEquals(await pidsMatching(sandbox.home), [])
    },
    undefined,
    FAILING_BINARY,
  )
})

Deno.test("a missing game binary fails the start with a clear error", async () => {
  await withSandbox(
    async (sandbox) => {
      const res = await sandbox.cli(["start", await sandbox.trial(`fixture = "bairdford"\n`)])
      assertEquals(res.code, 2)
      assert(res.stderr.includes("/nonexistent/game"), res.stderr)
    },
    undefined,
    "/nonexistent/game",
  )
})

Deno.test("a bad Trial is refused with a clear error and no game is started", async () => {
  await withSandbox(async (sandbox) => {
    const cases: [string, string][] = [
      [`seed = 1\n`, "fixture"],
      [`fixture = "bairdford"\nsedd = 1\n`, "sedd"],
      [`fixture = "not_in_library"\n`, "not_in_library"],
    ]
    for (const [toml, mention] of cases) {
      const res = await sandbox.cli(["start", await sandbox.trial(toml)])
      assertEquals(res.code, 2, toml)
      assert(res.stderr.includes(mention), `${toml} -> ${res.stderr}`)
    }
    const missing = await sandbox.cli(["start", join(sandbox.dir, "nope.toml")])
    assertEquals(missing.code, 2)
    assert(missing.stderr.includes("nope.toml"), missing.stderr)
    assertEquals(await pidsMatching(sandbox.home), [])
  })
})

Deno.test("a windowed Trial runs an Episode that answers like a windowless one", async () => {
  await withSandbox(async (sandbox) => {
    const windowed = await start(
      sandbox,
      `fixture = "bairdford"\nmode = "windowed"\nwindow_size = [1024, 768]\n`,
    )
    const windowless = await start(sandbox)
    const a = await step(sandbox, windowed.session, "state")
    const b = await step(sandbox, windowless.session, "state")
    assertEquals(a.code, 0, a.stderr)
    assertEquals(b.code, 0, b.stderr)
    assertEquals(a.json?.outcome, b.json?.outcome)
    assertEquals(a.json?.turn, b.json?.turn)
    for (const session of [windowed.session, windowless.session]) {
      assertEquals((await sandbox.cli(["stop", session])).code, 0)
    }
  })
})

Deno.test("a windowed Trial with a bad window size is a usage error before any game starts", async () => {
  await withSandbox(async (sandbox) => {
    const res = await sandbox.cli([
      "start",
      await sandbox.trial(`fixture = "bairdford"\nmode = "windowed"\nwindow_size = [10, 10]\n`),
    ])
    assertEquals(res.code, 2)
    assert(res.stderr.includes("window_size"), res.stderr)
    assertEquals(await pidsMatching(sandbox.home), [])
  })
})

Deno.test("clients that hang up before they are served do not take the daemon down", async () => {
  await withSandbox(async (sandbox) => {
    const { session } = await start(sandbox)
    await Promise.all(
      Array.from({ length: 100 }, () => connectDaemon(sandbox.home).then((conn) => conn.close())),
    )
    // The Episode, and the daemon holding it, are still there.
    assertEquals((await step(sandbox, session, "state")).json?.status, "ok")
  })
})

Deno.test("a slow daemon is never replaced and keeps its sessions", async () => {
  await withSandbox(async (sandbox) => {
    const { session } = await start(sandbox)
    const log = await Deno.readTextFile(join(sandbox.home, "daemon.log"))
    const pid = Number(/pid (\d+)/.exec(log)?.[1])
    assert(pid > 0, log)
    await setSuspended(pid, true) // accepts connections at the kernel level, answers nothing
    try {
      const res = await step(sandbox, session, "state")
      assertEquals(res.code, 2)
      assert(res.stderr.includes("not answering"), res.stderr)
    } finally {
      await setSuspended(pid, false)
    }
    // The same daemon, with the same Episode, answers again; no second daemon took its socket.
    assertEquals((await step(sandbox, session, "state")).json?.status, "ok")
    assertEquals(
      (await Deno.readTextFile(join(sandbox.home, "daemon.log"))).match(/listening on/g)?.length,
      1,
    )
  })
})

Deno.test("step and stop on an unknown session are refused", async () => {
  await withSandbox(async (sandbox) => {
    for (const args of [["step", "nosuch", '{"cmd":"ping"}'], ["stop", "nosuch"]]) {
      const res = await sandbox.cli(args)
      assertEquals(res.code, 2)
      assert(res.stderr.includes("nosuch"), res.stderr)
    }
  })
})

Deno.test("shutdown ends every live Episode and leaves no process behind", async () => {
  await withSandbox(async (sandbox) => {
    const { session } = await start(sandbox)
    assert((await pidsMatching(join(sandbox.home, "episodes", session))).length > 0)
    const res = await sandbox.cli(["shutdown"])
    assertEquals(res.code, 0, res.stderr)
    assert(
      await eventually(async () => (await pidsMatching(sandbox.home)).length === 0),
      "processes survived shutdown",
    )
  })
})
