/// The two runners. The real one builds and runs the suite natively; the fake one is scripted, so
/// `--runner fake` rehearses the whole sweep (range, classification, issues, watermark) without a
/// compiler, and the tests never touch the network or a build.
import { join } from "@std/path"
import { type Env, runTee, tail } from "../check/exec.ts"
import { logClaimsTestLink, stalenessProblem } from "./plan.ts"
import { crashExcerpt, parseCatchXml } from "./report.ts"
import type { CaseFailure, PlatformRun, SweepPlan } from "./types.ts"

/// The window-system variables a test process must not inherit on Linux; harmless elsewhere, and
/// the box's shared X connection has killed forked parents before (plan Phase 0).
const headlessEnv: Env = {
  DISPLAY: undefined,
  WAYLAND_DISPLAY: undefined,
  SDL_VIDEODRIVER: undefined,
}

async function statMtime(path: string): Promise<number | null> {
  try {
    return (await Deno.stat(path)).mtime?.getTime() ?? null
  } catch {
    return null
  }
}

/// Build, then run every invocation, then read the reports. Anything that prevented the suite from
/// having run at all (configure or build failure, missing or stale binary, a test process that died
/// before writing any report) is `completed: false`: the watermark must not move over code that was
/// never tested. A completed run carries the failed cases with their excerpts.
export async function realRunner(plan: SweepPlan, opts: {
  logDir: string
  jobs: number
  /** Epoch ms of the last commit that touched C++: the staleness floor. */
  cppFloorMs: number
}): Promise<PlatformRun> {
  const buildStarted = Date.now()
  const configured = await runTee(
    plan.configure,
    join(opts.logDir, "configure.log"),
  )
  if (configured.code !== 0) {
    return {
      completed: false,
      reason: `configure failed (exit ${configured.code}): ${
        (await tail(join(opts.logDir, "configure.log"), 5)).split("\n")[0] ?? ""
      }`,
    }
  }
  const build = plan.build.map((a) => a === "{jobs}" ? String(opts.jobs) : a)
  const built = await runTee(build, join(opts.logDir, "build.log"))
  if (built.code !== 0) {
    return {
      completed: false,
      reason: `build failed (exit ${built.code}): ${
        (await tail(join(opts.logDir, "build.log"), 5)).split("\n")[0] ?? ""
      }`,
    }
  }
  const mtime = await statMtime(plan.binary)
  if (mtime === null) {
    return { completed: false, reason: `no test binary at ${plan.binary} after a green build` }
  }
  const stale = stalenessProblem(
    mtime,
    buildStarted,
    logClaimsTestLink(built.stdout),
    opts.cppFloorMs,
  )
  if (stale !== null) return { completed: false, reason: stale }

  const failures: CaseFailure[] = []
  const exitCodes: Record<string, number> = {}
  for (const invocation of plan.tests) {
    const consoleLog = join(opts.logDir, `${invocation.label}.log`)
    const result = await runTee(invocation.args, consoleLog, { env: headlessEnv })
    exitCodes[invocation.label] = result.code
    let xml: string
    try {
      xml = await Deno.readTextFile(invocation.report)
    } catch {
      // No report at all: the process died before Catch2 wrote anything (init crash, missing DLL).
      return {
        completed: false,
        reason: `the ${invocation.label} run produced no report (exit ${result.code}): ${
          (await tail(consoleLog, 5)).split("\n").at(-1) ?? "no output"
        }`,
      }
    }
    const parsed = parseCatchXml(xml)
    failures.push(
      ...parsed.failures.map((f) => ({ ...f, invocation: invocation.label })),
    )
    if (parsed.crashed !== null) {
      failures.push({
        name: parsed.crashed,
        excerpt: crashExcerpt(result.stdout),
        invocation: invocation.label,
      })
    }
  }
  const nonzero = Object.values(exitCodes).some((code) => code !== 0)
  if (failures.length === 0 && nonzero) {
    // Non-zero exit with no failed case: the binary itself refused (bad args, an error logged
    // during init). Nothing was judged, so the watermark stays put.
    return {
      completed: false,
      reason: `the suite exited ${
        Object.entries(exitCodes).map(([l, c]) => `${l}=${c}`).join(" ")
      } without reporting any failed case`,
    }
  }
  return { completed: true, exitCodes, failures }
}

/// The scripted runner: reports exactly the names it was handed, as if each had failed one
/// assertion in the suite invocation. `dieBeforeReport` rehearses the harness-failure path that
/// must not move the watermark.
export function fakeRunner(
  failingNames: readonly string[],
  opts: { dieBeforeReport?: boolean } = {},
): (plan: SweepPlan) => Promise<PlatformRun> {
  return (plan: SweepPlan) => {
    if (opts.dieBeforeReport === true) {
      return Promise.resolve({
        completed: false,
        reason: "fake runner: the harness died before reporting",
      })
    }
    console.log(`fake runner: would run ${plan.tests.length} invocation(s) of ${plan.binary}`)
    const failures: CaseFailure[] = failingNames.map((name) => ({
      name,
      excerpt: `fake failure of ${name}: REQUIRE(false) at fake.cpp:1`,
      invocation: "suite",
    }))
    return Promise.resolve({
      completed: true,
      exitCodes: Object.fromEntries(
        plan.tests.map((t) => [t.label, failures.length === 0 ? 0 : 1]),
      ),
      failures,
    })
  }
}
