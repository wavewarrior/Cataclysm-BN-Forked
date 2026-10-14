/// The worktree plan: names, paths, commands and table rows. Pure, so the shape of the three
/// subcommands is unit-tested without git, herdr or cmake. Issue #198, decided in #150.

/// The preset a fresh Mac worktree configures. One constant until #149 defines the fast preset;
/// then only this line changes.
export const PRESET = "osx-arm-slim"

/// The branch types a worktree branch may start with.
export const BRANCH_TYPES = ["feat", "fix", "refactor", "chore", "build", "ci"] as const

/// The precompiled Metal shaders configure demands (`CMakeLists.txt` FATAL_ERRORs without them);
/// `/data/shaders/` is gitignored, so a fresh worktree never has them until they are copied.
/// The manifest the copy step checks the primary against; the refusal names what is missing.
export const EXPECTED_MSL = [
  "lm_ambient_compute.msl",
  "lm_clear_seen_compute.msl",
  "lm_clear_seen_view_compute.msl",
  "lm_color_raytrace_compute.msl",
  "lm_daylight_diffuse_compute.msl",
  "lm_fill_float_compute.msl",
  "lm_fill_uint_compute.msl",
  "lm_max_uint_compute.msl",
  "lm_pack_visibility_compute.msl",
  "lm_raytrace_compute.msl",
  "lm_seen_compute.msl",
  "lm_seen_walls_compute.msl",
  "lm_shift_float_compute.msl",
  "lm_shift_uint_compute.msl",
  "lm_sight_pairs_compute.msl",
  "lm_vehicle_optics_compute.msl",
  "lm_visibility_compute.msl",
  "test_compute.msl",
  "transparency_compute.msl",
]

/// Expected names the primary checkout lacks, sorted; empty when it has them all.
export function missingMsl(names: readonly string[]): string[] {
  const have = mslNames(names)
  return EXPECTED_MSL.filter((n) => !have.includes(n))
}

export type Spec = { type: string; issueId: number; slug: string }

const SPEC_RE = /^(feat|fix|refactor|chore|build|ci)\/([0-9]+)\/([a-z0-9][a-z0-9-]*)$/

/// Parse and normalize `<type>/<issue-id>/<slug>`; null for anything else.
export function parseSpec(spec: string): Spec | null {
  const m = SPEC_RE.exec(spec.toLowerCase())
  if (m === null) return null
  return { type: m[1], issueId: Number(m[2]), slug: m[3] }
}

export type NewCommand = { spec: Spec; base: string; path: string }

/// argv for `worktree new <type>/<issue-id>/<slug> [--base <ref>] [--path <dir>]`.
/// The base defaults to `feature/improvements`, the path to `<root>/<slug>`.
export function parseNewArgs(args: readonly string[], defaultRoot: string): NewCommand {
  const positional: string[] = []
  let base = "feature/improvements"
  let path = ""
  for (let i = 0; i < args.length; i++) {
    const flag = args[i]
    if (flag.startsWith("-")) {
      if (flag !== "--base" && flag !== "--path") {
        throw new Error(`worktree new: unknown flag "${flag}"`)
      }
      const value = args[++i]
      if (value === undefined || value.startsWith("-")) {
        throw new Error(`worktree new: ${flag} needs a value`)
      }
      if (flag === "--base") base = value
      else path = value
      continue
    }
    positional.push(flag)
  }
  if (positional.length !== 1) {
    throw new Error("worktree new: missing <type>/<issue-id>/<slug>")
  }
  const spec = parseSpec(positional[0])
  if (spec === null) {
    throw new Error(
      `worktree new: "${positional[0]}" is not <type>/<issue-id>/<slug> with type one of ${
        BRANCH_TYPES.join(", ")
      }`,
    )
  }
  if (path === "") path = `${defaultRoot}/${spec.slug}`
  return { spec, base, path }
}

/// The herdr call, run from the primary checkout: creation from a linked worktree hits herdr's
/// `linked_worktree_source` refusal (#198 Context).
export function herdrCreateArgs(cmd: NewCommand): string[] {
  return [
    "worktree",
    "create",
    "--branch",
    `${cmd.spec.type}/${cmd.spec.issueId}/${cmd.spec.slug}`,
    "--base",
    cmd.base,
    "--path",
    cmd.path,
    "--trust-repository",
  ]
}

/// The .msl files the copy step transfers, in sorted order.
export function mslNames(names: readonly string[]): string[] {
  return names.filter((n) => n.endsWith(".msl")).sort()
}

export type CopyPair = { from: string; to: string }

/// Primary `<shadersDir>/<name>` -> worktree `<shadersDir>/<name>`, for exactly the names the
/// primary has. An empty plan is the loud refusal the caller reports.
export function copyPlan(
  primaryPath: string,
  worktreePath: string,
  names: readonly string[],
): CopyPair[] {
  return mslNames(names).map((n) => ({
    from: `${primaryPath}/data/shaders/${n}`,
    to: `${worktreePath}/data/shaders/${n}`,
  }))
}

export type RowInput = {
  /** Absolute worktree path. */
  path: string
  /** Branch name, "" when detached. */
  branch: string
  /** Whether the checkout still exists on disk (a prunable entry does not). */
  present: boolean
  /** Whether this is the primary checkout. */
  primary: boolean
  /** Summed KiB of every `out/build/*` dir, null when there is none. */
  sizeKiB: number | null
  /** Seconds since the head commit, null when unknown. */
  ageSeconds: number | null
  /** Is the head an ancestor of the base; null when undecidable. */
  mergedIntoBase: boolean | null
}

export type Row = { name: string; branch: string; size: string; age: string; merged: string }

/// One `list` row. A missing checkout (a prunable entry) carries dashes instead of lying about a
/// tree that is gone.
export function listRow(input: RowInput): Row {
  const parts = input.path.replace(/\/+$/, "").split("/")
  const name = parts[parts.length - 1]
  const branch = input.branch === "" ? "(detached)" : input.branch
  if (!input.present) return { name, branch, size: "-", age: "-", merged: "-" }
  const merged = input.primary
    ? "primary"
    : input.mergedIntoBase === null
    ? "?"
    : input.mergedIntoBase
    ? "yes"
    : "no"
  return {
    name,
    branch,
    size: input.sizeKiB === null ? "-" : formatSize(input.sizeKiB),
    age: input.ageSeconds === null ? "-" : formatAge(input.ageSeconds),
    merged,
  }
}

/// KiB -> GiB with one decimal (MiB below a GiB), the units the disk conversation uses.
export function formatSize(kib: number): string {
  if (kib < 1048576) return `${Math.max(1, Math.round(kib / 1024))} MiB`
  return `${(kib / 1048576).toFixed(1)} GiB`
}

/// Seconds -> `37 m` / `5 h` / `12 d`, floored: an age never rounds up.
export function formatAge(seconds: number): string {
  if (seconds < 3600) return `${Math.max(1, Math.floor(seconds / 60))} m`
  if (seconds < 172800) return `${Math.floor(seconds / 3600)} h`
  return `${Math.floor(seconds / 86400)} d`
}

/// Bytes -> GiB with one decimal, for the before/after free-space lines.
export function formatGib(bytes: number): string {
  return `${(bytes / 1073741824).toFixed(1)} GiB`
}

const LIST_HEADERS = ["worktree", "branch", "size", "age", "merged"]

/// Fixed-width table of `listRow` output.
export function formatTable(rows: readonly Row[]): string {
  const cells = rows.map((r) => [r.name, r.branch, r.size, r.age, r.merged])
  const width = (col: number) =>
    Math.max(LIST_HEADERS[col].length, ...cells.map((r) => r[col].length))
  const line = (r: readonly string[]) =>
    r.map((cell, i) => cell.padEnd(width(i))).join("  ").trimEnd()
  return [line(LIST_HEADERS), ...cells.map(line)].join("\n")
}

/// What `remove` announces before deleting: the checkout plus its build dirs.
export function removePlan(worktreePath: string, buildDirs: readonly string[]): string[] {
  return [`worktree ${worktreePath}`, ...buildDirs.map((dir) => `build dir ${dir}`)]
}

/// Are `<gitDir>` and `<commonDir>` the same directory (the primary checkout)? Pure string compare
/// of the two `git rev-parse` outputs, so the linked-worktree refusal is testable.
export function gitDirsEqual(gitDir: string, commonDir: string): boolean {
  return gitDir.replace(/\/+$/, "") === commonDir.replace(/\/+$/, "")
}

/// One `git worktree list --porcelain` entry.
export type WorktreeEntry = {
  path: string
  head: string
  branch: string
  prunable: boolean
  gitdir: string
}

/// Parse `git worktree list --porcelain` output. A detached entry has no branch line; a prunable
/// one points at a checkout that is gone.
export function parseWorktreeList(out: string): WorktreeEntry[] {
  const entries: WorktreeEntry[] = []
  let cur: Partial<WorktreeEntry> = {}
  const flush = () => {
    if (cur.path !== undefined) {
      entries.push({
        head: "",
        branch: "",
        prunable: false,
        gitdir: "",
        path: cur.path,
        ...cur,
      })
    }
    cur = {}
  }
  for (const line of out.split("\n")) {
    if (line === "") {
      flush()
      continue
    }
    const sp = line.indexOf(" ")
    const key = sp === -1 ? line : line.slice(0, sp)
    const val = sp === -1 ? "" : line.slice(sp + 1)
    if (key === "worktree") {
      flush()
      cur.path = val
    } else if (key === "HEAD") cur.head = val
    else if (key === "branch") cur.branch = val.startsWith("refs/heads/") ? val.slice(11) : val
    else if (key === "gitdir") cur.gitdir = val
    else if (key === "prunable") cur.prunable = true
  }
  flush()
  return entries
}
