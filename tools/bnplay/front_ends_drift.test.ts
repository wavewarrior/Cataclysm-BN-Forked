/**
 * The CLI and the MCP server must offer the same operations. This fails when an operation exists
 * in one front end and not the other, or when the two refuse the same bad call differently.
 */
import { assert, assertEquals } from "@std/assert"
import { initialize, startMcp } from "./mcp_testkit.ts"
import { makeSandbox } from "./testkit.ts"

type Tool = { name: string; inputSchema: { required: string[] } }

/** `bnplay fixture add <save-dir> [name]` -> `fixture add`: the words before the first argument. */
function commandWords(usageLine: string): string[] {
  const words = usageLine.trim().split(/\s+/).slice(1)
  const end = words.findIndex((w) => /^[<['\[]/.test(w))
  return end === -1 ? words : words.slice(0, end)
}

Deno.test("the CLI and the MCP server offer exactly the same operations", async () => {
  const sandbox = await makeSandbox()
  const mcp = startMcp(sandbox)
  try {
    await initialize(mcp)
    const tools = (await mcp.rpc("tools/list")).result?.tools as Tool[]

    const usage = await sandbox.cli([])
    assertEquals(usage.code, 2)
    const cliCommands = usage.stderr.split("\n").filter((l) => l.trim().startsWith("bnplay "))
      .map((l) => commandWords(l).join("_"))
      // `mcp` is the server itself and `daemon` the resident process: not operations.
      .filter((name) => name !== "mcp" && name !== "daemon")

    assertEquals([...cliCommands].sort(), tools.map((t) => t.name).sort())

    // Each operation refuses a call with its required arguments missing the same way in both.
    for (const tool of tools.filter((t) => t.inputSchema.required.length > 0)) {
      const viaMcp = await mcp.call(tool.name, {})
      assertEquals(viaMcp.isError, true, tool.name)
      const viaCli = await sandbox.cli(tool.name.split("_"))
      assertEquals(viaCli.code, 2, tool.name)
      assert(
        viaCli.stderr.includes(viaMcp.text),
        `${tool.name}: CLI says ${viaCli.stderr}; MCP says ${viaMcp.text}`,
      )
    }
  } finally {
    const exit = await mcp.close()
    await sandbox.cleanup()
    assertEquals(exit.code, 0, exit.stderr)
  }
})
