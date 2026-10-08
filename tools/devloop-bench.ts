#!/usr/bin/env -S deno run --allow-all
/**
 * Dev-loop baseline benchmark (wayfinder ticket #137).
 *
 * Hygiene baked in:
 *   - scenarios change file CONTENT (ccache hashes content; touch alone is a
 *     cache hit), then restore the saved bytes afterwards
 *   - link scenarios delete the executables so only the link reruns
 *   - the idle gate compares load1 against the machine's own core count and
 *     checks foreign builders; OUR build raising load is expected, so no
 *     discard on raw load>8 (a foreign builder appearing is reported, not
 *     silently fatal)
 *   - per-TU times parsed from .ninja_log rows appended during the run
 *   - every rep appended to the results file the moment it finishes
 *
 * Usage: deno run --allow-all tools/devloop-bench.ts <scenario> [--reps N]
 * scenarios: leaf | hub-vehicle | hub-map | test-edit | link | link-cold | worktree
 * Summary:   deno run --allow-all tools/devloop-bench.ts report
 */
import { cores, envRecord, idleGate, sh } from "./devloop-lib.ts"

const ROOT = new URL("..", import.meta.url).pathname.replace(/\/$/, "")
const BUILD = `${ROOT}/out/build/osx-arm-slim`
const BIN = `${BUILD}/src/cataclysm-bn-tiles`
const TBIN = `${BUILD}/tests/cata_test-tiles`
const OUT = "/tmp/wf/baseline.jsonl"
const PRESET = "osx-arm-slim"

type Rec = Record<string, unknown>

async function append(rec: Rec): Promise<void> {
  await Deno.mkdir("/tmp/wf", { recursive: true })
  await Deno.writeTextFile(OUT, JSON.stringify(rec) + "\n", { create: true, append: true })
}

function logSize(): number {
  try {
    return Deno.statSync(`${BUILD}/.ninja_log`).size
  } catch {
    return 0
  }
}

/** Rows appended to .ninja_log after byte offset `from` (v7: start end mtime out hash). */
function ninjaRows(
  from: number,
): { rows: Array<{ out: string; dur_s: number }>; compacted: boolean } {
  const buf = Deno.readFileSync(`${BUILD}/.ninja_log`)
  const compacted = buf.byteLength < from
  const text = new TextDecoder().decode(compacted ? buf : buf.subarray(from))
  const rows: Array<{ out: string; dur_s: number }> = []
  for (const line of text.split("\n")) {
    if (!line || line.startsWith("#")) continue
    const f = line.split("\t")
    if (f.length < 4) continue
    rows.push({ out: f[3], dur_s: (Number(f[1]) - Number(f[0])) / 1000 })
  }
  return { rows, compacted }
}

/** ccache delta lines for the rep, proving cold vs hit. */
async function ccacheStats(): Promise<string[]> {
  const r = await sh(["ccache", "-s"])
  return r.out.split("\n").filter((l) => /hit|miss|store|recache|zeroed/i.test(l)).map((l) =>
    l.trim()
  )
}

/** Strip any nonce line a previously killed run left behind. */
async function healNonces(files: string[]): Promise<void> {
  for (const f of files) {
    try {
      const text = await Deno.readTextFile(f)
      const cleaned = text.split("\n").filter((l) => !l.startsWith("// devloop-bench nonce")).join(
        "\n",
      )
      if (cleaned !== text) {
        await Deno.writeTextFile(f, cleaned)
        console.error(`healed leftover nonce in ${f}`)
      }
    } catch { /* file may not exist in this scenario */ }
  }
}

/** `recache` forces CCACHE_RECACHE=1: a comment nonce is stripped by the
 * preprocessor fallback and would measure a hit, so timed builds must bypass
 * reads. Entries are overwritten, not purged; other worktrees unaffected. */
async function timedBuild(
  scenario: string,
  rep: number,
  targets: string[],
  recache = false,
): Promise<Rec> {
  const logFrom = logSize()
  await sh(["ccache", "-z"])
  const start = await envRecord()
  const t0 = performance.now()
  const build = ["cmake", "--build", "--preset", PRESET, ...targets.flatMap((t) => ["--target", t])]
  const r = await sh(recache ? ["env", "CCACHE_RECACHE=1", ...build] : build)
  const wall = (performance.now() - t0) / 1000
  const end = await envRecord()
  const { rows, compacted } = ninjaRows(logFrom)
  const objs = rows.filter((x) => x.out.endsWith(".o"))
  const rec: Rec = {
    scenario,
    rep,
    wall_s: Math.round(wall * 10) / 10,
    edges: rows.length,
    tu_count: objs.length,
    tu_sum_s: Math.round(objs.reduce((s, x) => s + x.dur_s, 0) * 10) / 10,
    log_compacted: compacted,
    tu_times: objs.map((x): [string, number] => [x.out.split("/").at(-1) ?? "?", x.dur_s]).sort((
      a,
      b,
    ) => b[1] - a[1]).slice(0, 12),
    ccache: await ccacheStats(),
    load_start: start,
    load_end: end,
    ok: r.code === 0,
    tail: r.out.split("\n").slice(-5).join("\n"),
  }
  await append(rec)
  console.log(`${scenario} rep${rep}: ${rec.wall_s}s edges=${rows.length} ok=${r.code === 0}`)
  return rec
}

const BENCH_FILES = [
  "src/game_movement.cpp",
  "tests/crafting_test.cpp",
  "src/vehicle.h",
  "src/map.h",
]

/**
 * Require the file to be unmodified in git (never measure on top of another
 * session's edit), append a nonce line, run `body`, restore saved bytes, then
 * resync the tree untimed so the NEXT timed rep starts from a quiet tree.
 */
async function withContentEdit(file: string, body: () => Promise<Rec>): Promise<Rec> {
  const st = await sh(["git", "status", "--porcelain", "--", file], ROOT)
  if (st.out.trim() !== "") {
    throw new Error(`${file} dirty in git (${st.out.trim()}); refusing to measure`)
  }
  const orig = await Deno.readFile(file)
  const nonce = new TextEncoder().encode(`// devloop-bench nonce ${Date.now()} ${Math.random()}\n`)
  try {
    await Deno.writeFile(file, new Uint8Array([...orig, ...nonce]))
    return await body()
  } finally {
    await Deno.writeFile(file, orig)
    const t = await timedBuild("resync", 0, ["cataclysm-bn-tiles", "cata_test-tiles"])
    void t
  }
}

export { append, BIN, BUILD, healNonces, logSize, PRESET, ROOT, TBIN, timedBuild, withContentEdit }

/* ---------------- scenarios ---------------- */

async function scenLeaf(reps: number): Promise<void> {
  for (let i = 0; i < reps; i++) {
    await idleGate()
    await withContentEdit(
      `${ROOT}/src/game_movement.cpp`,
      () => timedBuild("leaf-game-only", i, ["cataclysm-bn-tiles"], true),
    )
    await idleGate()
    await withContentEdit(
      `${ROOT}/src/game_movement.cpp`,
      () => timedBuild("leaf-both", i, ["cataclysm-bn-tiles", "cata_test-tiles"], true),
    )
  }
}

async function scenHub(header: string, name: string, reps: number): Promise<void> {
  for (let i = 0; i < reps; i++) {
    await idleGate()
    await withContentEdit(
      `${ROOT}/src/${header}`,
      () => timedBuild(name, i, ["cataclysm-bn-tiles", "cata_test-tiles"], true),
    )
  }
}

async function scenTestEdit(reps: number): Promise<void> {
  for (let i = 0; i < reps; i++) {
    await idleGate()
    await withContentEdit(
      `${ROOT}/tests/crafting_test.cpp`,
      () => timedBuild("test-edit", i, ["cata_test-tiles"], true),
    )
  }
}

/** Force relink only: delete the executables, rebuild both targets. */
async function scenLink(reps: number, cold: boolean): Promise<void> {
  for (let i = 0; i < reps; i++) {
    await idleGate()
    await sh(["rm", "-f", BIN, TBIN])
    if (cold) await sh(["rm", "-rf", `${BUILD}/lto.cache`])
    await timedBuild(cold ? "link-cold" : "link-warm", i, ["cataclysm-bn-tiles", "cata_test-tiles"])
  }
}

async function scenWorktree(reps: number): Promise<void> {
  const wt = "/tmp/wf/bench-wt"
  const bd = `${wt}/out/build/${PRESET}`
  for (let i = 0; i < reps; i++) {
    await idleGate()
    await sh(["rm", "-rf", wt])
    const t0 = performance.now()
    const add = await sh(["git", "worktree", "add", "--detach", wt, "HEAD"], ROOT)
    if (add.code !== 0) throw new Error(`worktree add failed: ${add.out.slice(-400)}`)
    // The preset consumes precompiled shader exports; the gitignored files
    // under data/shaders (.msl and some .hlsl) must travel with the worktree
    // or configure/build diverge from the main checkout (#140 finding).
    await sh(["mkdir", "-p", `${wt}/data/shaders`])
    await sh(["cp", "-R", `${ROOT}/data/shaders/.`, `${wt}/data/shaders/`])
    const cfg = await sh(["cmake", `--preset=${PRESET}`, "-B", bd, "-S", wt], wt)
    const cfg_s = (performance.now() - t0) / 1000
    if (cfg.code !== 0) {
      await append({
        scenario: "worktree-configure-FAILED",
        rep: i,
        tail: cfg.out.split("\n").slice(-15).join("\n"),
      })
      throw new Error("worktree configure failed")
    }
    await append({ scenario: "worktree-configure", rep: i, wall_s: Math.round(cfg_s * 10) / 10 })
    const t1 = performance.now()
    const b = await sh(["cmake", "--build", bd, "--target", "cataclysm-bn-tiles"], wt)
    const build_s = (performance.now() - t1) / 1000
    await append({
      scenario: "worktree-first-binary",
      rep: i,
      wall_s: Math.round(build_s * 10) / 10,
      ok: b.code === 0,
      tail: b.out.split("\n").slice(-5).join("\n"),
    })
    await sh(["git", "worktree", "remove", "--force", wt], ROOT)
  }
}

function report(): void {
  const text = Deno.readTextFileSync(OUT)
  const rows = text.split("\n").filter((l) => l.trim()).map((l) => JSON.parse(l) as Rec)
  const by: Record<string, number[]> = {}
  for (const r of rows) {
    if (r.ok === false || r.scenario === "resync") continue
    const k = String(r.scenario)
    ;(by[k] ??= []).push(Number(r.wall_s))
  }
  for (const [k, v] of Object.entries(by)) {
    v.sort((a, b) => a - b)
    console.log(`${k.padEnd(24)} n=${v.length} ${v.map((x) => `${x}s`).join(" / ")}`)
  }
}

const args = Deno.args
const repIdx = args.indexOf("--reps")
const reps = repIdx >= 0 ? parseInt(args[repIdx + 1], 10) : 2
if (args[0] !== "report") {
  await healNonces(BENCH_FILES.map((f) => `${ROOT}/${f}`))
  await sh(["ccache", "-z"])
  console.error("ccache counters zeroed before scenarios")
}
switch (args[0]) {
  case "report":
    report()
    break
  case "leaf":
    await scenLeaf(reps)
    break
  case "hub-vehicle":
    await scenHub("vehicle.h", "hub-vehicle-both", reps)
    break
  case "hub-map":
    await scenHub("map.h", "hub-map-both", reps)
    break
  case "test-edit":
    await scenTestEdit(reps)
    break
  case "link":
    await scenLink(reps, false)
    break
  case "link-cold":
    await scenLink(reps, true)
    break
  case "worktree":
    await scenWorktree(reps)
    break
  default:
    console.error(
      "usage: devloop-bench.ts <leaf|hub-vehicle|hub-map|test-edit|link|link-cold|worktree|report> [--reps N]",
    )
    Deno.exit(2)
}
