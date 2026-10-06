/**
 * The MCP front end end to end against the mock driver (the second seam): a host mounts
 * `bnplay mcp`, drives an Episode through the tools, and sees the results the CLI prints. Only
 * external behaviour is asserted: protocol replies, tool results, errors, exit codes and whether
 * any process is left behind.
 */
import { assert, assertEquals, assertNotEquals } from "@std/assert"
import { join } from "@std/path"
import { initialize, type McpClient, startMcp } from "./mcp_testkit.ts"
import {
  eventually,
  jsonOut,
  makeFakeWorld,
  makeSandbox,
  pidsMatching,
  type Sandbox,
} from "./testkit.ts"

/** Settings under which this machine's memory and swap cannot fail a preflight. */
const ROOMY = {
  BNPLAY_MIN_FREE_MEMORY_MB: "1",
  BNPLAY_MIN_FREE_SWAP_MB: "0",
  BNPLAY_BASELINE_IDLE_MS: "300",
  BNPLAY_SELFCHECK_IDLE_MS: "300",
}

type Report = {
  session: string
  verdict: string
  exit_code: number
  ended: string
  oracles: { name: string; result: string }[]
  transcript: string
}

/** Runs `body` with a sandbox, a fake-world save to add, and a mounted MCP server. */
async function withMcp(
  body: (sandbox: Sandbox, mcp: McpClient, save: string) => Promise<void>,
  env: Record<string, string> = {},
): Promise<void> {
  const save = await makeFakeWorld()
  const sandbox = await makeSandbox({ env: { ...ROOMY, ...env } })
  const mcp = startMcp(sandbox)
  try {
    await initialize(mcp)
    await body(sandbox, mcp, save)
  } finally {
    const exit = await mcp.close()
    await sandbox.cleanup()
    await Deno.remove(save, { recursive: true })
    assertEquals(exit.code, 0, exit.stderr)
  }
}

Deno.test("the server speaks MCP: handshake, tool list, ping, and refusals of what it lacks", async () => {
  const sandbox = await makeSandbox()
  const mcp = startMcp(sandbox)
  try {
    const hello = await initialize(mcp, "2024-11-05")
    assertEquals(hello.result?.protocolVersion, "2024-11-05")
    assertEquals((hello.result?.capabilities as { tools?: object }).tools, {})
    assertEquals((hello.result?.serverInfo as { name: string }).name, "bnplay")
    const unknown = await mcp.rpc("initialize", { protocolVersion: "1999-01-01" })
    assert(typeof unknown.result?.protocolVersion === "string")
    assertNotEquals(unknown.result?.protocolVersion, "1999-01-01")

    assertEquals((await mcp.rpc("ping")).result, {})

    const listed = (await mcp.rpc("tools/list")).result?.tools as {
      name: string
      description: string
      inputSchema: {
        type: string
        properties: Record<string, { type: string }>
        required: string[]
      }
    }[]
    const byName = Object.fromEntries(listed.map((t) => [t.name, t]))
    for (const tool of listed) {
      assert(tool.description.length > 0, tool.name)
      assertEquals(tool.inputSchema.type, "object", tool.name)
    }
    assertEquals(byName.step.inputSchema.required, ["session", "command"])
    assertEquals(byName.step.inputSchema.properties.command.type, "object")
    assertEquals(byName.doctor.inputSchema.properties.self_check.type, "boolean")
    assertEquals(byName.doctor.inputSchema.required, [])

    const noMethod = await mcp.rpc("resources/list")
    assertEquals(noMethod.error?.code, -32601)
    const noTool = await mcp.rpc("tools/call", { name: "no_such_tool", arguments: {} })
    assertEquals(noTool.error?.code, -32602)

    // A notification gets no reply: every message so far answers a request we made.
    await mcp.notify("notifications/cancelled", { requestId: 999 })
    await mcp.rpc("ping")
    const replies = mcp.received() as { jsonrpc: string; id: number }[]
    assertEquals(replies.map((r) => r.jsonrpc), replies.map(() => "2.0"))
    assertEquals(replies.map((r) => r.id), [1, 2, 3, 4, 5, 6, 7])
  } finally {
    const exit = await mcp.close()
    await sandbox.cleanup()
    assertEquals(exit.code, 0, exit.stderr)
  }
})

Deno.test("an Episode driven entirely through MCP tools: fixture, baseline, start, step, report, stop", async () => {
  await withMcp(async (sandbox, mcp, save) => {
    await Deno.writeTextFile(join(save, "mock_log_idle.txt"), "ERROR : noisy.json: known\n")

    const added = await mcp.call<{ fixture: string; baseline: string }>("fixture_add", {
      source: save,
      name: "bairdford",
    })
    assertEquals(added.isError, false, added.text)
    assertEquals(added.structured?.fixture, "bairdford")
    assertEquals(added.structured?.baseline, "missing")
    assertEquals(JSON.parse(added.text), added.structured, "text and structured content agree")

    const baseline = await mcp.call("fixture_baseline", { name: "bairdford" })
    assertEquals(baseline.isError, false, baseline.text)
    const listed = await mcp.call<{ fixtures: { fixture: string; baseline: string }[] }>(
      "fixture_list",
    )
    assertEquals(listed.structured?.fixtures.map((f) => [f.fixture, f.baseline]), [[
      "bairdford",
      "fresh",
    ]])

    const doctor = await mcp.call<{ driver_available: boolean; checks: { name: string }[] }>(
      "doctor",
      { fixture: "bairdford" },
    )
    assertEquals(doctor.isError, false, doctor.text)
    assertEquals(doctor.structured?.driver_available, true)
    assert(doctor.structured!.checks.length > 0)

    const trial = await sandbox.trial(
      `fixture = "bairdford"\nseed = 7\nexpected_commands = ["wait"]\n` +
        `[[oracle]]\nfield = "hp"\noperator = "gt"\nvalue = 0\n`,
    )
    const started = await mcp.call<{ session: string; transcript: string }>("start", { trial })
    assertEquals(started.isError, false, started.text)
    const session = started.structured!.session
    assert(session.length > 0)

    const waited = await mcp.call<{ status: string; turn?: number }>("step", {
      session,
      command: { cmd: "wait", turns: 3 },
    })
    assertEquals(waited.isError, false, waited.text)
    assertEquals(waited.structured?.status, "ok")

    // A driver error is the response, not a tool error.
    const bad = await mcp.call<{ status: string; error?: string }>("step", {
      session,
      command: { cmd: "no_such_command" },
    })
    assertEquals(bad.isError, false, bad.text)
    assertEquals(bad.structured?.status, "error")
    assert(bad.structured?.error?.includes("no_such_command"), bad.text)

    const midway = await mcp.call<Report>("report", { session })
    assertEquals(midway.isError, false, midway.text)
    assertEquals(midway.structured?.session, session)

    const stopped = await mcp.call<Report>("stop", { session })
    assertEquals(stopped.isError, false, stopped.text)
    assertEquals(stopped.structured?.verdict, "pass")
    assertEquals(stopped.structured?.exit_code, 0)
    assertEquals(stopped.structured?.ended, "stop")
    assertEquals(
      stopped.structured?.oracles.map((o) => o.result).every((r) => r === "pass"),
      true,
    )
    assertEquals(await pidsMatching(join(sandbox.home, "episodes", session)), [])

    // A stopped session refuses further steps, as a tool error.
    const after = await mcp.call("step", { session, command: { cmd: "state" } })
    assertEquals(after.isError, true)
    assert(after.text.includes(session), after.text)

    const down = await mcp.call("shutdown")
    assertEquals(down.isError, false, down.text)
    assert(await eventually(async () => (await pidsMatching(sandbox.home)).length === 0))
  })
})

Deno.test("a failed verdict and an unhealthy doctor are results; refusals are tool errors", async () => {
  await withMcp(async (sandbox, mcp, save) => {
    await mcp.call("fixture_add", { source: save, name: "bairdford" })
    assertEquals((await mcp.call("fixture_baseline", { name: "bairdford" })).isError, false)

    const trial = await sandbox.trial(
      `fixture = "bairdford"\n[[oracle]]\nname = "stays healthy"\nfield = "hp"\n` +
        `operator = "gt"\nvalue = 50\n`,
    )
    const session = (await mcp.call<{ session: string }>("start", { trial })).structured!.session
    await mcp.call("step", { session, command: { cmd: "hurt", amount: 60 } })
    const stopped = await mcp.call<Report>("stop", { session })
    assertEquals(stopped.isError, false, stopped.text)
    assertEquals(stopped.structured?.verdict, "fail")
    assertEquals(stopped.structured?.exit_code, 1)

    // The same report again, by session: a report of an ended Episode stays readable.
    const again = await mcp.call<Report>("report", { session })
    assertEquals(again.structured?.exit_code, 1)

    const noFixture = await mcp.call<{ healthy: boolean }>("doctor", { fixture: "nowhere" })
    assertEquals(noFixture.isError, false, noFixture.text)
    assertEquals(noFixture.structured?.healthy, false)

    const unknownSession = await mcp.call("report", { session: "no-such-session" })
    assertEquals(unknownSession.isError, true)
    assert(unknownSession.text.includes("no session no-such-session"), unknownSession.text)

    const badTrial = await mcp.call("start", { trial: join(sandbox.dir, "missing.toml") })
    assertEquals(badTrial.isError, true)
    assert(badTrial.text.includes("missing.toml"), badTrial.text)

    for (
      const [name, args] of [
        ["step", { session }],
        ["step", { session, command: "state" }],
        ["step", { session, command: ["state"] }],
        ["step", { session, command: { cmd: "state" }, extra: 1 }],
        ["start", {}],
        ["start", { trial: 5 }],
        ["doctor", { self_check: "yes" }],
        ["fixture_list", { name: "x" }],
      ] as const
    ) {
      const refused = await mcp.call(name, args)
      assertEquals(refused.isError, true, `${name} ${JSON.stringify(args)}: ${refused.text}`)
      assertEquals(refused.structured, undefined)
    }
    // Refusals leave the Episode machinery alone: a fresh start still works.
    const second = await mcp.call("start", { trial })
    assertEquals(second.isError, false, second.text)
    await mcp.call("shutdown")
  })
})

Deno.test("both front ends share one daemon and print the same results", async () => {
  await withMcp(async (sandbox, mcp, save) => {
    const cliAdd = await sandbox.cli(["fixture", "add", save, "bairdford"])
    assertEquals(cliAdd.code, 0, cliAdd.stderr)
    assertEquals((await mcp.call("fixture_baseline", { name: "bairdford" })).isError, false)

    const cliList = jsonOut(await sandbox.cli(["fixture", "list"]))
    assertEquals((await mcp.call("fixture_list")).structured, cliList)

    // Started from the CLI, stepped and ended through MCP; read back from the CLI.
    const trial = await sandbox.trial(`fixture = "bairdford"\n`)
    const started = await sandbox.cli(["start", trial])
    assertEquals(started.code, 0, started.stderr)
    const session = jsonOut<{ session: string }>(started).session
    const viaMcp = await mcp.call<Record<string, unknown>>("step", {
      session,
      command: { cmd: "state" },
    })
    assertEquals(viaMcp.isError, false, viaMcp.text)
    const viaCli = await sandbox.cli(["step", session, '{"cmd":"state"}'])
    assertEquals(viaCli.code, 0, viaCli.stderr)
    // Same command, same lean response, apart from the request ids the driver numbers.
    const { id: _a, ...mcpState } = viaMcp.structured as Record<string, unknown>
    const { id: _b, ...cliState } = jsonOut(viaCli)
    assertEquals(mcpState, cliState)

    const stopped = await mcp.call<Report>("stop", { session })
    const reported = await sandbox.cli(["report", session])
    assertEquals(reported.code, stopped.structured?.exit_code)
    assertEquals(jsonOut(reported), stopped.structured)

    // A refusal reads the same on both: what the CLI prints on stderr is the tool error.
    const cliRefused = await sandbox.cli(["report", "no-such-session"])
    const mcpRefused = await mcp.call("report", { session: "no-such-session" })
    assertEquals(cliRefused.code, 2)
    assertEquals(mcpRefused.isError, true)
    assertEquals(cliRefused.stderr.trim(), `bnplay: ${mcpRefused.text}`)

    const cliDoctor = await sandbox.cli(["doctor", "--fixture", "bairdford"])
    const mcpDoctor = await mcp.call<{ healthy: boolean }>("doctor", { fixture: "bairdford" })
    assertEquals(cliDoctor.code, mcpDoctor.structured?.healthy ? 0 : 1)
  })
})

Deno.test("an MCP host that goes away leaves the daemon and its sessions running", async () => {
  const save = await makeFakeWorld()
  const sandbox = await makeSandbox({ fixtureSources: { bairdford: save }, env: ROOMY })
  try {
    const first = startMcp(sandbox)
    await initialize(first)
    const trial = await sandbox.trial(`fixture = "bairdford"\n`)
    const session = (await first.call<{ session: string }>("start", { trial })).structured!.session
    assertEquals((await first.close()).code, 0)

    // A new host, a new server process, the same daemon: the Episode is still there.
    const second = startMcp(sandbox)
    await initialize(second)
    const state = await second.call("step", { session, command: { cmd: "state" } })
    assertEquals(state.isError, false, state.text)
    assertEquals((await second.call<Report>("stop", { session })).structured?.exit_code, 0)
    assertEquals((await second.close()).code, 0)
  } finally {
    await sandbox.cleanup()
    await Deno.remove(save, { recursive: true })
  }
})
