/// The build and test plan per platform: which presets to run, which binary must appear, which
/// Catch2 invocations make up the sweep. Everything except `readUserPresets` and `lastCppCommitMs`
/// is pure.
import { join } from "@std/path"
import { run } from "../check/exec.ts"
import type { Platform, SweepPlan, TestInvocation } from "./types.ts"

/// The macOS preset lineage (GLOSSARY: Mac): the owner's workstation, Metal, never an executor.
export const MACOS_PRESET = "osx-arm-slim"

/// The manifest presets for Windows. The box overrides the configure preset in
/// `CMakeUserPresets.json` because the vendored DXC object paths blow past MAX_PATH under the
/// manifest's long `binaryDir`; `pickWindowsPlan` prefers that override when it declares one.
export const WINDOWS_MANIFEST_PRESET = "windows-tiles-sounds-x64-msvc"
export const WINDOWS_MANIFEST_BUILD_PRESET = "windows-msvc-relwithdebinfo"
export const WINDOWS_USER_BUILD_PRESET = "win-rel"

const MANIFEST_BINARY_DIR = "out/build"

type PresetDoc = {
  configurePresets?: { name?: string; binaryDir?: string }[]
  buildPresets?: { name?: string; configurePreset?: string }[]
}

/// A preset file we cannot parse falls back to the manifest, so a malformed override is noticed
/// as "the sweep used the manifest presets" rather than guessed at.
function safeParse(json: string): PresetDoc {
  try {
    return JSON.parse(json) as PresetDoc
  } catch {
    return {}
  }
}

/// Where a configure preset puts its build tree, with `${sourceDir}` resolved.
function binaryDirOf(doc: PresetDoc, name: string, repoRoot: string): string {
  const declared = doc.configurePresets?.find((p) => p.name === name)?.binaryDir
  const relative = declared ?? `${MANIFEST_BINARY_DIR}/${name}`
  return join(repoRoot, relative.replace("${sourceDir}/", ""))
}

/// The Windows plan: the box's `win-rel` build preset when `CMakeUserPresets.json` declares one,
/// the manifest presets otherwise. `userPresets` is the file's contents, or null when absent.
export function pickWindowsPlan(
  userPresets: string | null,
  repoRoot: string,
): { configure: string[]; build: string[]; binary: string } {
  const doc = userPresets === null ? {} : safeParse(userPresets)
  const named = doc.buildPresets?.find((p) => p.name === WINDOWS_USER_BUILD_PRESET)
  const buildPreset = named === undefined
    ? WINDOWS_MANIFEST_BUILD_PRESET
    : WINDOWS_USER_BUILD_PRESET
  const configurePreset = named?.configurePreset ?? WINDOWS_MANIFEST_PRESET
  const binaryDir = binaryDirOf(doc, configurePreset, repoRoot)
  return {
    configure: ["cmake", "--preset", configurePreset],
    build: ["cmake", "--build", "--preset", buildPreset, "-j", "{jobs}"],
    binary: join(binaryDir, "tests", "RelWithDebInfo", "cata_test-tiles.exe"),
  }
}

export function pickMacosPlan(repoRoot: string): {
  configure: string[]
  build: string[]
  binary: string
} {
  return {
    configure: ["cmake", "--preset", MACOS_PRESET],
    build: ["cmake", "--build", "--preset", MACOS_PRESET, "-j", "{jobs}"],
    binary: join(repoRoot, MANIFEST_BINARY_DIR, MACOS_PRESET, "tests", "cata_test-tiles"),
  }
}

/// The Catch2 invocations of a sweep: the whole suite in-process, plus the Render tier on Windows
/// only (ADR-0006: the 4090 is the Reference renderer; the Mac gets no render verdict). Each writes
/// a Catch2 XML report, whose exact `TestCase name=` attribute is what the failure parser reads;
/// the console reporter stays on stdout so a human watching the task sees progress.
export function invocations(
  platform: Platform,
  binary: string,
  logDir: string,
  userDir: string,
): TestInvocation[] {
  const common = ["--rng-seed", "1", `--user-dir=${userDir}`, "-D", "--reporter", "console"]
  const specs: { label: string; extra: string[] }[] = [{ label: "suite", extra: [] }]
  if (platform === "windows") specs.push({ label: "render", extra: ["[render]"] })
  return specs.map(({ label, extra }) => {
    const report = join(logDir, `${label}.xml`)
    return {
      label,
      report,
      args: [binary, ...common, ...extra, "--reporter", `XML::out=${report}`],
    }
  })
}

/// Assemble the whole plan for a platform. `userPresets` is passed in rather than read here so the
/// choice is unit-testable without a filesystem.
export function sweepPlan(
  platform: Platform,
  repoRoot: string,
  logDir: string,
  userPresets: string | null,
): SweepPlan {
  const picked = platform === "windows"
    ? pickWindowsPlan(userPresets, repoRoot)
    : pickMacosPlan(repoRoot)
  return {
    platform,
    configure: picked.configure,
    build: picked.build,
    binary: picked.binary,
    tests: invocations(platform, picked.binary, logDir, join(logDir, "userdir")),
  }
}

/// `CMakeUserPresets.json` is machine-local and git-ignored: it is what makes the Windows build
/// work on the box at all, so the sweep reads it instead of assuming. Null when the file is absent.
export async function readUserPresets(repoRoot: string): Promise<string | null> {
  try {
    return await Deno.readTextFile(join(repoRoot, "CMakeUserPresets.json"))
  } catch {
    return null
  }
}

/// Did the build really produce a fresh test binary? A killed ninja or a build preset without the
/// test target leaves a stale binary that would happily "pass" a sweep of code nobody compiled, so
/// the sweep checks mtime instead of trusting the exit code. A binary older than the last commit
/// that touched C++ cannot contain that commit's code; a build that claimed to link the target must
/// have touched the binary. A docs-only HEAD therefore never trips this check.
export function stalenessProblem(
  binaryMtimeMs: number,
  buildStartedMs: number,
  buildClaimedLink: boolean,
  lastCppCommitMs: number,
): string | null {
  if (buildClaimedLink && binaryMtimeMs < buildStartedMs - 1000) {
    return "the build linked the test target but its mtime predates the build"
  }
  if (binaryMtimeMs < lastCppCommitMs) {
    return `the test binary (${
      new Date(binaryMtimeMs).toISOString()
    }) predates the last C++ commit (${
      new Date(lastCppCommitMs).toISOString()
    }): it cannot contain the code being swept`
  }
  return null
}

/// True when a build log shows the test target being linked, i.e. the build did real work.
export function logClaimsTestLink(log: string): boolean {
  return /Linking C\S* executable .*cata_test-tiles/.test(log)
}

/// `git log -1 --format=%ct -- src tests`: when C++ last changed, in epoch milliseconds. The
/// staleness baseline: a commit that only touched docs cannot make a binary stale.
export async function lastCppCommitMs(cwd: string): Promise<number> {
  const result = await run(["git", "log", "-1", "--format=%ct", "--", "src", "tests"], { cwd })
  if (result.code !== 0) throw new Error(`git log: ${result.stderr.trim()}`)
  return Number(result.stdout.trim()) * 1000
}
