// Issue #196: a Debug configure on macOS redirects binaries to the repo root,
// where stale copies masquerade as current ones. The guard module refuses that
// combination at configure time. These tests drive the module through
// `cmake -P` (script mode), so they need no game configure: -D flags stand in
// for what project() would set, and the assertions observe only the exit code
// and the emitted message.
import { assert, assertEquals, assertStringIncludes } from "@std/assert"
import { join } from "@std/path"
import { fromFileUrl } from "@std/path"

const repoRoot = fromFileUrl(new URL("..", import.meta.url))
const guardModule = join(repoRoot, "CMakeModules", "CataGuardDebugRedirect.cmake")
const topLevelCMakeLists = join(repoRoot, "CMakeLists.txt")

async function runCmakeScript(
  defs: string[],
  script: string,
): Promise<{ code: number; output: string }> {
  // -D must precede -P; after it cmake treats the remaining argv as script args.
  const proc = new Deno.Command("cmake", {
    args: [...defs, "-P", script],
    cwd: repoRoot,
    stdout: "piped",
    stderr: "piped",
  })
  const { code, stdout, stderr } = await proc.output()
  return { code, output: new TextDecoder().decode(stdout) + new TextDecoder().decode(stderr) }
}

// A preset that sets Debug and can run on macOS would hit the guard at configure time. The
// `lint` preset did exactly that: the documented JSON formatting loop died at configure. A child
// that inherits Debug is the same trap, so build type and condition resolve through `inherits`
// (the child's own value wins, then the parents in order, as CMake resolves them).
type Preset = {
  name: string
  inherits?: string | string[]
  condition?: { lhs?: string; rhs?: string }
  cacheVariables?: Record<string, unknown>
}

function resolve<T>(
  preset: Preset,
  byName: Map<string, Preset>,
  pick: (preset: Preset) => T | undefined,
): T | undefined {
  const own = pick(preset)
  if (own !== undefined) return own
  const parents = [preset.inherits ?? []].flat()
  for (const parent of parents) {
    const found = resolve(byName.get(parent) as Preset, byName, pick)
    if (found !== undefined) return found
  }
  return undefined
}

Deno.test("no configure preset that can run on macOS builds as Debug, inherited or not", async () => {
  const presets = JSON.parse(await Deno.readTextFile(join(repoRoot, "CMakePresets.json")))
    .configurePresets as Preset[]
  const byName = new Map(presets.map((preset) => [preset.name, preset]))
  const offenders: string[] = []
  for (const preset of presets) {
    const type = resolve(preset, byName, (p) => p.cacheVariables?.CMAKE_BUILD_TYPE)
    const condition = resolve(preset, byName, (p) => p.condition)
    const windowsOnly = condition?.lhs === "${hostSystemName}" && condition?.rhs === "Windows"
    if (type === "Debug" && !windowsOnly) offenders.push(preset.name)
  }
  assertEquals(offenders, [], "these presets resolve to Debug without a Windows-only condition")
})

Deno.test("guard refuses Debug on Apple, naming the osx-arm-slim preset", async () => {
  const { code, output } = await runCmakeScript(
    ["-DAPPLE=ON", "-DCMAKE_BUILD_TYPE=Debug"],
    guardModule,
  )
  assertEquals(code, 1, `expected FATAL_ERROR, got exit ${code}: ${output}`)
  assertStringIncludes(output, "osx-arm-slim")
  assertStringIncludes(output, "repo root")
})

Deno.test("guard lets RelWithDebInfo through on Apple", async () => {
  const { code, output } = await runCmakeScript(
    ["-DAPPLE=ON", "-DCMAKE_BUILD_TYPE=RelWithDebInfo"],
    guardModule,
  )
  assertEquals(code, 0, output)
})

Deno.test("guard lets Release through on Apple", async () => {
  const { code, output } = await runCmakeScript(
    ["-DAPPLE=ON", "-DCMAKE_BUILD_TYPE=Release"],
    guardModule,
  )
  assertEquals(code, 0, output)
})

Deno.test("guard is inert without APPLE, even for Debug", async () => {
  // Stands in for Windows/Linux: same build type, no APPLE -> the existing
  // Debug redirect stays available, byte-identical behaviour. `cmake -P`
  // predefines APPLE from the host, so the wrapper unsets it first.
  const dir = await Deno.makeTempDir({ prefix: "cbn196-" })
  const wrapper = join(dir, "simulate-other-platform.cmake")
  await Deno.writeTextFile(wrapper, `unset(APPLE)\ninclude("${guardModule}")\n`)
  const { code, output } = await runCmakeScript(["-DCMAKE_BUILD_TYPE=Debug"], wrapper)
  assertEquals(code, 0, output)
})

Deno.test("top-level CMakeLists includes the guard before the Debug branch", async () => {
  const text = await Deno.readTextFile(topLevelCMakeLists)
  assert(
    /^include\(CataGuardDebugRedirect\)$/m.test(text),
    "CMakeLists.txt must include(CataGuardDebugRedirect) so the guard runs on every configure",
  )
  // The guard must fire before the Debug branch it guards, not after it.
  const includeAt = text.indexOf("include(CataGuardDebugRedirect)")
  const debugBranchAt = text.indexOf('if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")')
  assert(debugBranchAt >= 0, "the Debug build-type branch disappeared from CMakeLists.txt")
  assert(includeAt < debugBranchAt, "guard must be included before the Debug build-type branch")
})
