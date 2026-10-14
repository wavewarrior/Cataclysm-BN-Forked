/// The CLI end to end against a throwaway git repo: a fake herdr that is `git worktree add` in
/// a trench coat, a fake cmake that records its arguments, and a primary checkout carrying the
/// .msl manifest. Covers the copy step, the refusals (linked worktree, missing .msl, dirty tree,
/// unpushed commits), the list table and the clean removal. The real configure smoke run is
/// manual, see the issue.
import { assert, assertMatch, assertStringIncludes } from "@std/assert"
import { join } from "@std/path"
import { EXPECTED_MSL } from "./spec.ts"

/// Declared only with run/env/read/write granted, like tools/check/exec_test.ts.
const granted = (
  await Promise.all(
    ["run", "env", "read", "write"].map((name) =>
      Deno.permissions.query({ name: name as Deno.PermissionName })
    ),
  )
).every((s) => s.state === "granted")
if (!granted) {
  console.warn("main_test: run/env/read/write not granted; the IO tests are not declared")
}

const MAIN = join(import.meta.dirname!, "main.ts")

const HERDR_STUB = `#!/bin/sh
# Fake herdr: parse the flags the real CLI takes, do what herdr does for a source checkout.
branch=""; base=""; path=""
while [ $# -gt 0 ]; do
  case "$1" in
    --branch) branch="$2"; shift 2 ;;
    --base) base="$2"; shift 2 ;;
    --path) path="$2"; shift 2 ;;
    *) shift ;;
  esac
done
echo "herdr-stub $branch $base $path" >> "$STUB_LOG"
mkdir -p "$(dirname "$path")"
git worktree add -b "$branch" "$path" "$base"
`

const CMAKE_STUB = `#!/bin/sh
echo "cmake-stub $*" >> "$STUB_LOG"
echo "fake configure ok"
exit 0
`

type Temp = { dir: string; primary: string; wtRoot: string; log: string }

async function sh(cmd: string[], cwd: string): Promise<string> {
  const r = await new Deno.Command(cmd[0], {
    args: cmd.slice(1),
    cwd,
    stdout: "piped",
    stderr: "piped",
  }).output()
  if (r.code !== 0) throw new Error(`${cmd.join(" ")}: ${new TextDecoder().decode(r.stderr)}`)
  return new TextDecoder().decode(r.stdout)
}

const git = (
  ...args: string[]
) => ["git", "-c", "user.name=T", "-c", "user.email=t@example.invalid", ...args]

/// A primary checkout on feature/improvements with an origin remote; `withShaders` decides
/// whether it carries the .msl manifest the copy step demands.
async function setUp(withShaders: boolean): Promise<Temp> {
  const dir = await Deno.realPath(await Deno.makeTempDir({ prefix: "wt_main_" }))
  const primary = join(dir, "primary")
  const wtRoot = join(dir, "worktrees")
  const log = join(dir, "stub.log")
  await Deno.mkdir(primary)
  await sh(git("init", "-b", "feature/improvements", primary), dir)
  await Deno.writeTextFile(join(primary, ".gitignore"), "/data/shaders/\nout/\n.check/\n")
  await Deno.writeTextFile(join(primary, "README.md"), "fixture\n")
  if (withShaders) {
    await Deno.mkdir(join(primary, "data", "shaders"), { recursive: true })
    for (const name of EXPECTED_MSL) {
      await Deno.writeTextFile(join(primary, "data", "shaders", name), `// ${name}\n`)
    }
  }
  await sh(git("add", "."), primary)
  await sh(git("commit", "-m", "init"), primary)
  await sh(git("init", "--bare", join(dir, "origin.git")), dir)
  await sh(git("remote", "add", "origin", join(dir, "origin.git")), primary)
  await sh(git("push", "-u", "origin", "feature/improvements"), primary)
  const herdr = join(dir, "herdr")
  const cmake = join(dir, "cmake")
  await Deno.writeTextFile(herdr, HERDR_STUB)
  await Deno.writeTextFile(cmake, CMAKE_STUB)
  await Deno.chmod(herdr, 0o755)
  await Deno.chmod(cmake, 0o755)
  return { dir, primary, wtRoot, log }
}

async function cli(temp: Temp, args: string[], cwd?: string) {
  const r = await new Deno.Command("deno", {
    args: ["run", "-A", MAIN, ...args],
    cwd: cwd ?? temp.primary,
    env: {
      HERDR_CMD: join(temp.dir, "herdr"),
      CMAKE_CMD: join(temp.dir, "cmake"),
      WORKTREE_ROOT: temp.wtRoot,
      STUB_LOG: temp.log,
    },
    stdout: "piped",
    stderr: "piped",
  }).output()
  return {
    code: r.code,
    out: new TextDecoder().decode(r.stdout),
    err: new TextDecoder().decode(r.stderr),
  }
}

/// One fixture per test: setUp, body, teardown, whatever the body did.
function test(name: string, withShaders: boolean, fn: (t: Temp) => Promise<void>) {
  if (!granted) return
  Deno.test({
    name,
    sanitizeResources: false,
    fn: async () => {
      const temp = await setUp(withShaders)
      try {
        await fn(temp)
      } finally {
        await Deno.remove(temp.dir, { recursive: true }).catch(() => {})
      }
    },
  })
}

test(
  "new creates via herdr, copies every .msl, configures, and prints the build command",
  true,
  async (temp) => {
    const r = await cli(temp, ["new", "feat/0/smoke"])
    if (r.code !== 0) throw new Error(`exit ${r.code}: ${r.err}`)
    assertStringIncludes(r.out, "copied 19 data/shaders/*.msl")
    assertStringIncludes(r.out, "cmake --preset osx-arm-slim succeeded")
    assertStringIncludes(r.out, "deno task build")
    const stubLog = await Deno.readTextFile(temp.log)
    assertStringIncludes(
      stubLog,
      `herdr-stub feat/0/smoke feature/improvements ${join(temp.wtRoot, "smoke")}`,
    )
    assertStringIncludes(stubLog, "cmake-stub --preset osx-arm-slim")
    for (const name of EXPECTED_MSL) {
      assertStringIncludes(
        await Deno.readTextFile(join(temp.wtRoot, "smoke", "data", "shaders", name)),
        name,
      )
    }
    assertStringIncludes(
      await sh(git("branch", "--list", "feat/0/smoke"), temp.primary),
      "feat/0/smoke",
    )
  },
)

test(
  "new refuses when the primary lacks the .msl files, naming every one of them",
  false,
  async (temp) => {
    const r = await cli(temp, ["new", "feat/0/smoke"])
    assert(r.code !== 0, "must not succeed")
    assertStringIncludes(r.err, "lacks 19 of the 19")
    assertStringIncludes(r.err, "lm_seen_compute.msl")
    assertStringIncludes(r.err, "transparency_compute.msl")
  },
)

test("new refuses to start from a linked worktree", true, async (temp) => {
  const linked = join(temp.dir, "linked")
  await sh(
    git("worktree", "add", "-b", "chore/9/linked", linked, "feature/improvements"),
    temp.primary,
  )
  const r = await cli(temp, ["new", "feat/0/smoke"], linked)
  assert(r.code !== 0, "must not succeed")
  assertStringIncludes(r.err, "linked worktree")
  assertStringIncludes(r.err, temp.primary)
})

test("new refuses a path that already exists", true, async (temp) => {
  await Deno.mkdir(join(temp.wtRoot, "smoke"), { recursive: true })
  const r = await cli(temp, ["new", "feat/0/smoke"])
  assert(r.code !== 0, "must not succeed")
  assertStringIncludes(r.err, "exists")
})

test("list shows branch, size, age and merged state for every worktree", true, async (temp) => {
  const created = await cli(temp, ["new", "feat/1/merged"])
  if (created.code !== 0) throw new Error(`new failed: ${created.err}`)
  await sh(
    git("worktree", "add", "-b", "fix/2/ahead", join(temp.wtRoot, "ahead"), "feature/improvements"),
    temp.primary,
  )
  await Deno.writeTextFile(join(temp.wtRoot, "ahead", "note.txt"), "work in progress\n")
  await sh(git("add", "note.txt"), join(temp.wtRoot, "ahead"))
  await sh(git("commit", "-m", "ahead"), join(temp.wtRoot, "ahead"))
  await Deno.mkdir(join(temp.wtRoot, "merged", "out", "build", "osx-arm-slim"), { recursive: true })
  await Deno.writeTextFile(
    join(temp.wtRoot, "merged", "out", "build", "osx-arm-slim", "blob.bin"),
    "x".repeat(2 << 20),
  )
  const r = await cli(temp, ["list"])
  if (r.code !== 0) throw new Error(`exit ${r.code}: ${r.err}`)
  const lines = r.out.trim().split("\n")
  assertStringIncludes(lines[0], "worktree")
  assertStringIncludes(lines[0], "merged")
  const mergedLine = lines.find((l) => l.includes("feat/1/merged"))!
  const aheadLine = lines.find((l) => l.includes("fix/2/ahead"))!
  assertMatch(mergedLine, /\byes\b/, "a worktree at the base head is merged")
  assertMatch(mergedLine, /\d+(\.\d)? (MiB|GiB)/, "the 2 MiB blob dir shows a size")
  assertMatch(aheadLine, /\bno\b/, "a commit ahead of the base is not merged")
  assertMatch(aheadLine, /^ahead\s+\S+\s+-\s+\d+ (m|h|d)/, "no build dir prints as a dash")
})

test(
  "list marks a vanished checkout with dashes instead of invented numbers",
  true,
  async (temp) => {
    await cli(temp, ["new", "feat/3/gone"])
    await Deno.remove(join(temp.wtRoot, "gone"), { recursive: true })
    const r = await cli(temp, ["list"])
    const line = r.out.split("\n").find((l) => l.includes("gone"))!
    assertMatch(line, /gone\s+feat\/3\/gone\s+-\s+-\s+-/, `expected dashed columns in: ${line}`)
  },
)

test("remove refuses a dirty worktree and leaves it standing", true, async (temp) => {
  await cli(temp, ["new", "fix/4/dirty"])
  await Deno.writeTextFile(join(temp.wtRoot, "dirty", "README.md"), "unstaged edit\n")
  const r = await cli(temp, ["remove", "dirty"])
  assert(r.code !== 0, "must refuse")
  assertStringIncludes(r.err, "dirty")
  await Deno.stat(join(temp.wtRoot, "dirty"))
})

test("remove refuses commits that are in no remote and not in the base", true, async (temp) => {
  await cli(temp, ["new", "fix/5/lost"])
  await Deno.writeTextFile(join(temp.wtRoot, "lost", "work.txt"), "precious\n")
  await sh(git("add", "work.txt"), join(temp.wtRoot, "lost"))
  await sh(git("commit", "-m", "unpushed work"), join(temp.wtRoot, "lost"))
  const r = await cli(temp, ["remove", "lost"])
  assert(r.code !== 0, "must refuse")
  assertStringIncludes(r.err, "1 commit(s)")
  await Deno.stat(join(temp.wtRoot, "lost"))
})

test(
  "remove deletes a clean pushed worktree and its build dir, keeps the branch, shows free space",
  true,
  async (temp) => {
    await cli(temp, ["new", "chore/6/pushed"])
    await Deno.writeTextFile(join(temp.wtRoot, "pushed", "done.txt"), "done\n")
    await sh(git("add", "done.txt"), join(temp.wtRoot, "pushed"))
    await sh(git("commit", "-m", "pushed work"), join(temp.wtRoot, "pushed"))
    await sh(git("push", "origin", "chore/6/pushed"), join(temp.wtRoot, "pushed"))
    await Deno.mkdir(join(temp.wtRoot, "pushed", "out", "build", "osx-arm-slim"), {
      recursive: true,
    })
    await Deno.writeTextFile(
      join(temp.wtRoot, "pushed", "out", "build", "osx-arm-slim", "big.o"),
      "x".repeat(1 << 20),
    )
    const r = await cli(temp, ["remove", "pushed"])
    if (r.code !== 0) throw new Error(`exit ${r.code}: ${r.err}`)
    assertStringIncludes(r.out, `will delete: worktree ${join(temp.wtRoot, "pushed")}`)
    assertStringIncludes(
      r.out,
      `will delete: build dir ${join(temp.wtRoot, "pushed", "out", "build", "osx-arm-slim")}`,
    )
    assertMatch(r.out, /free space: [\d.]+ GiB -> [\d.]+ GiB/)
    assertStringIncludes(r.out, "branch chore/6/pushed kept")
    try {
      await Deno.stat(join(temp.wtRoot, "pushed"))
      throw new Error("the worktree must be gone")
    } catch (e) {
      assert(e instanceof Deno.errors.NotFound, "the worktree must be gone")
    }
    assertStringIncludes(
      await sh(git("branch", "--list", "chore/6/pushed"), temp.primary),
      "chore/6/pushed",
    )
  },
)

test("remove refuses an unknown slug", true, async (temp) => {
  const r = await cli(temp, ["remove", "never-existed"])
  assert(r.code !== 0, "must refuse")
  assertStringIncludes(r.err, "no worktree named never-existed")
})
