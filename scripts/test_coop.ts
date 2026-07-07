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
 * A second control socket (OS-assigned port) carries test signals.
 *
 * Usage:
 *   deno task test:coop                          # auto-detects binary, movement scenario
 *   COOP_SCENARIO=pickup deno task test:coop     # run a different scenario
 *   deno task test:coop -- /path/to/cata_test-tiles
 *   CBN_TEST_BIN=/path/to/cata_test-tiles deno task test:coop
 */

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

const GLOBAL_TIMEOUT_MS = 300_000  // 300 s hard cap — two cold data loads (~50 s each) + scenario

/// Per-phase soft deadlines.  A phase that exceeds its deadline emits a
/// warning rather than killing the test — the global timeout is the hard gate.
const PHASE_TIMEOUTS_MS = {
  handshake:  60_000,  // "handshake complete" must appear in BOTH roles' stderr within 60 s
  scenario:   120_000, // scenario-specific assertion within 120 s of handshake
}

/// Log lines that mark phase transitions (searched in streamed stderr).
/// NOTE: host logs "[coop] handshake complete"; client logs "[coop] client handshake complete".
///       The common substring "handshake complete" matches both.
const PHASE_MARKERS = {
  handshake: "handshake complete",
  listening: "[coop] listening on port",
}

/// Scenario to run — passed through to both processes via COOP_SCENARIO env var.
const COOP_SCENARIO = Deno.env.get("COOP_SCENARIO") ?? "movement"

/// Coordination files the C++ tests create in /tmp.  Cleaned before each run.
const COORD_FILES = [
  "/tmp/coop_test_port.txt",
  "/tmp/coop_test_ctrl_port.txt",
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
  /** Resolves when the handshake phase marker is seen in stderr. */
  handshakeSeen: Promise<void>
}

// ---------------------------------------------------------------------------
// Live-streaming process runner
// ---------------------------------------------------------------------------

async function streamLines(
  stream: ReadableStream<Uint8Array>,
  lines: string[],
  prefix: string,
  phaseCallbacks: Map<string, () => void>,
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
      // Fire any phase callbacks whose marker is found in this line.
      for (const [marker, cb] of phaseCallbacks) {
        if (ln.includes(marker)) {
          cb()
          phaseCallbacks.delete(marker)
        }
      }
    }
  }
  if (buf) {
    console.log(`${prefix} ${buf}`)
    lines.push(buf)
  }
}

function spawnRole(bin: string, filter: string, name: string): RoleHandle {
  const padded = name.padEnd(6) // "host  " / "client"
  const env = { ...Deno.env.toObject(), COOP_SCENARIO }
  const cmd = new Deno.Command(bin, {
    args: [filter, `--user-dir=/tmp/coop_test_user_${name}`, "--use-colour", "no"],
    stdout: "piped",
    stderr: "piped",
    env,
  })

  const proc = cmd.spawn()
  const stdout: string[] = []
  const stderr: string[] = []

  // Resolve when the handshake marker appears in stderr.
  const { promise: handshakeSeen, resolve: resolveHandshake } =
    Promise.withResolvers<void>()
  const phaseCallbacks = new Map<string, () => void>([
    [PHASE_MARKERS.handshake, resolveHandshake],
  ])

  const done: Promise<number> = (async () => {
    const [status] = await Promise.all([
      proc.status,
      streamLines(proc.stdout, stdout, `[${padded}]    `, new Map()),
      streamLines(proc.stderr, stderr, `[${padded}/err]`, phaseCallbacks),
    ])
    return status.code
  })()

  return { name, proc, stdout, stderr, done, handshakeSeen }
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

// 1. Sweep stale coordination files.
for (const f of COORD_FILES) {
  try { Deno.removeSync(f) } catch { /* didn't exist — ok */ }
}

const bin = resolveTestBin()
console.log(`[coop-harness] binary   : ${bin}`)
console.log(`[coop-harness] scenario : ${COOP_SCENARIO}`)
console.log(`[coop-harness] timeout  : ${GLOBAL_TIMEOUT_MS}ms`)
console.log(`[coop-harness] Output is streamed live; both roles run simultaneously.`)
console.log()

// 2. Spawn both roles.
const host = spawnRole(bin, "[.][coop_role_host]", "host")
const client = spawnRole(bin, "[.][coop_role_client]", "client")

// 3. Phase watcher: warn if handshake takes too long (soft deadline).
withTimeout(
  Promise.all([host.handshakeSeen, client.handshakeSeen]),
  PHASE_TIMEOUTS_MS.handshake,
  "handshake phase",
).catch(() => {
  console.warn(
    `[coop-harness] WARN: handshake not seen within ${PHASE_TIMEOUTS_MS.handshake}ms — processes may be stuck`,
  )
})

// 4. Race against hard global timeout.
let hostCode: number
let clientCode: number
try {
  ;[hostCode, clientCode] = await withTimeout(
    Promise.all([host.done, client.done]),
    GLOBAL_TIMEOUT_MS,
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
