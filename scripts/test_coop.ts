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

const TIMEOUT_MS = 45_000 // 45 s — allows slow CI machines

const DEFAULT_BINS = [
  "build-coop/tests/cata_test-tiles",
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
  console.error("Set CBN_TEST_BIN or pass path as first argument.")
  Deno.exit(1)
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

interface ProcessResult {
  role: string
  exitCode: number
  stdout: string
  stderr: string
}

// ---------------------------------------------------------------------------
// Process runner — cmd.output() spawns, waits, and collects all output.
// ---------------------------------------------------------------------------

async function runRole(bin: string, filter: string, role: string): Promise<ProcessResult> {
  const cmd = new Deno.Command(bin, {
    args: [filter, "--user-dir", `test_user_dir_coop_${role}`, "--use-colour", "no"],
    stdout: "piped",
    stderr: "piped",
    env: Deno.env.toObject(),
  })

  const output = await cmd.output()
  return {
    role,
    exitCode: output.code,
    stdout: new TextDecoder().decode(output.stdout),
    stderr: new TextDecoder().decode(output.stderr),
  }
}

// ---------------------------------------------------------------------------
// Delay using Promise.withResolvers (project rule: no new Promise callbacks)
// ---------------------------------------------------------------------------


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

const bin = resolveTestBin()
console.log(`[coop-harness] binary : ${bin}`)
console.log(`[coop-harness] timeout: ${TIMEOUT_MS}ms`)

// Host starts immediately; client is delayed so the host can bind its socket
// before the client attempts to connect.
const hostPromise = runRole(bin, "[.][coop_role_host]", "host")

// Both roles start simultaneously.  The port-file polling inside the C++ client
// role (read_port_file) handles synchronisation — no fixed delay needed.
const clientPromise = runRole(bin, "[.][coop_role_client]", "client")

let results: ProcessResult[]
try {
  results = await withTimeout(
    Promise.all([hostPromise, clientPromise]),
    TIMEOUT_MS,
    "host + client processes",
  )
} catch (err) {
  console.error(`[coop-harness] FATAL: ${err}`)
  Deno.exit(1)
}

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------

let passed = true

for (const result of results!) {
  const ok = result.exitCode === 0
  const status = ok ? "PASS" : "FAIL"
  console.log(`\n=== ${result.role.toUpperCase()} [${status}] (exit ${result.exitCode}) ===`)
  if (result.stdout) console.log(result.stdout)
  if (result.stderr) console.error(result.stderr)
  if (!ok) passed = false
}

if (passed) {
  console.log("\n[coop-harness] ALL ROLES PASSED")
  Deno.exit(0)
} else {
  console.error("\n[coop-harness] ONE OR MORE ROLES FAILED")
  Deno.exit(1)
}
