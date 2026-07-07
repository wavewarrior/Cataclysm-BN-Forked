#!/usr/bin/env -S deno run --allow-run --allow-env --allow-read --allow-write
/**
 * Co-op two-process integration test harness.
 *
 * Spawns two separate cata_test-tiles processes:
 *   Process A (host):   runs the "[.][coop_role_host]"   Catch2 test
 *   Process B (client): runs the "[.][coop_role_client]" Catch2 test
 *
 * Each process has its own real game* g (two-world topology).  They
 * communicate over loopback TCP using the actual co-op wire protocol.
 *
 * Usage:
 *   deno task test:coop                          # auto-detects binary
 *   deno task test:coop -- /path/to/cata_test-tiles
 *   CBN_TEST_BIN=/path/to/cata_test-tiles deno task test:coop
 */

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

const TIMEOUT_MS = 180_000 // 180 s — two separate cold game data loads (~23 s each) + test runtime

/// Coordination files the C++ tests create in /tmp.  Cleaned before each run
/// so a crashed previous run cannot falsely satisfy a polling check.
const COORD_FILES = [
  "/tmp/coop_test_port.txt",
  "/tmp/coop_test_proxy_pos.txt",
  "/tmp/coop_test_terrain.txt",
]

const DEFAULT_BINS = [
  "out/build/osx-coop/tests/cata_test-tiles",    // macOS COOP preset (local E2E)
  "build-coop/tests/cata_test-tiles",             // CI Linux (ci-coop preset)
  "build/tests/cata_test-tiles",
  "out/build/linux-full/tests/cata_test-tiles",
]

function resolveTestBin(): string {
  const fromArg = Deno.args[0]
  if (fromArg) return fromArg
  const fromEnv = Deno.env.get("CBN_TEST_BIN")
  if (fromEnv) return fromEnv
  for (const candidate of DEFAULT_BINS) {
    try {
      Deno.statSync(candidate)
      return candidate
    } catch {
      /* try next */
    }
  }
  console.error("ERROR: cannot locate cata_test-tiles.")
  console.error("Build with COOP=ON first:")
  console.error("  cmake --preset osx-coop")
  console.error("  cmake --build --preset osx-coop &")
  console.error("Then re-run this script, or set CBN_TEST_BIN explicitly.")
  Deno.exit(1)
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

interface RoleHandle {
  name: string
  proc: Deno.ChildProcess
  stdout: string[]
  stderr: string[]
  /** Resolves when the process exits AND all output is drained. */
  done: Promise<number> // exit code
}

// ---------------------------------------------------------------------------
// Live-streaming process runner
// ---------------------------------------------------------------------------

async function streamLines(
  stream: ReadableStream<Uint8Array>,
  lines: string[],
  prefix: string,
): Promise<void> {
  const reader = stream.pipeThrough(new TextDecoderStream()).getReader()
  let buf = ""
  for (;;) {
    const { done, value } = await reader.read()
    if (done) break
    buf += value
    const parts = buf.split("\n")
    buf = parts.pop() ?? ""
    for (const ln of parts) {
      console.log(`${prefix} ${ln}`)
      lines.push(ln)
    }
  }
  if (buf) {
    console.log(`${prefix} ${buf}`)
    lines.push(buf)
  }
}

function spawnRole(bin: string, filter: string, name: string): RoleHandle {
  const padded = name.padEnd(6) // "host  " / "client"
  const cmd = new Deno.Command(bin, {
    args: [filter, `--user-dir=/tmp/coop_test_user_${name}`, "--use-colour", "no"],
    stdout: "piped",
    stderr: "piped",
    env: Deno.env.toObject(),
  })

  const proc = cmd.spawn()
  const stdout: string[] = []
  const stderr: string[] = []

  const done: Promise<number> = (async () => {
    const [status] = await Promise.all([
      proc.status,
      streamLines(proc.stdout, stdout, `[${padded}]    `),
      streamLines(proc.stderr, stderr, `[${padded}/err]`),
    ])
    return status.code
  })()

  return { name, proc, stdout, stderr, done }
}

// ---------------------------------------------------------------------------
// Timeout using Promise.withResolvers
// ---------------------------------------------------------------------------

function withTimeout<T>(promise: Promise<T>, ms: number, label: string): Promise<T> {
  const { promise: raced, resolve, reject } = Promise.withResolvers<T>()

  const timer = setTimeout(() => reject(new Error(`Timeout after ${ms}ms: ${label}`)), ms)

  promise.then(
    (v) => { clearTimeout(timer); resolve(v) },
    (e) => { clearTimeout(timer); reject(e) },
  )

  return raced
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

// 1. Sweep stale /tmp coordination files so a previous crashed run cannot
//    satisfy a file-poll check in the new run.
for (const f of COORD_FILES) {
  try { Deno.removeSync(f) } catch { /* didn't exist — ok */ }
}

const bin = resolveTestBin()
console.log(`[coop-harness] binary : ${bin}`)
console.log(`[coop-harness] timeout: ${TIMEOUT_MS}ms`)
console.log(`[coop-harness] Output is streamed live; both roles run simultaneously.`)
console.log()

// 2. Spawn both roles — port-file polling inside the C++ client handles sync.
const host = spawnRole(bin, "[.][coop_role_host]", "host")
const client = spawnRole(bin, "[.][coop_role_client]", "client")

// 3. Race against global timeout.  On timeout, kill both OS processes so the
//    next run isn't blocked by a lingering server holding the port.
let hostCode: number
let clientCode: number
try {
  ;[hostCode, clientCode] = await withTimeout(
    Promise.all([host.done, client.done]),
    TIMEOUT_MS,
    "host + client processes",
  )
} catch (err) {
  console.error(`\n[coop-harness] FATAL: ${err}`)
  console.error("[coop-harness] Killing both processes...")
  try { host.proc.kill("SIGKILL") } catch { /* already exited */ }
  try { client.proc.kill("SIGKILL") } catch { /* already exited */ }
  Deno.exit(1)
}

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------

const roles = [
  { name: "host", code: hostCode },
  { name: "client", code: clientCode },
]

let passed = true
for (const { name, code } of roles) {
  const ok = code === 0
  console.log(`\n=== ${name.toUpperCase()} [${ok ? "PASS" : "FAIL"}] (exit ${code}) ===`)
  if (!ok) passed = false
}

if (passed) {
  console.log("\n[coop-harness] ALL ROLES PASSED")
  Deno.exit(0)
} else {
  console.error("\n[coop-harness] ONE OR MORE ROLES FAILED")
  Deno.exit(1)
}
