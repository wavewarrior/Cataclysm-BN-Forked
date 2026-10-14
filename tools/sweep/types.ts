/// The shared shapes of a platform sweep. No IO here.

/// The two shipped platforms a sweep runs natively on. Linux is the executor, not a sweep target.
export type Platform = "windows" | "macos"

/// One entry of `tests/quarantine.h`, parsed out of the C++ source.
export type QuarantineEntry = { name: string; expires: string; reason: string }

/// A case that failed, with enough of the log to explain itself and which invocation said so.
export type CaseFailure = { name: string; excerpt: string; invocation: string }

/// One Catch2 invocation of the sweep: the full suite, plus the render tier on Windows.
export type TestInvocation = {
  /// `suite` or `render`: names the log and the junit report.
  label: string
  /// Arguments for the test binary, junit report path included.
  args: string[]
  /// The junit XML this invocation writes; its absence means the run did not complete.
  report: string
}

/// What a platform needs: the presets to configure and build, the binary to expect, the runs.
export type SweepPlan = {
  platform: Platform
  configure: string[]
  build: string[]
  /// Relative to the repo root, forward slashes on every platform.
  binary: string
  tests: TestInvocation[]
}

/// The result of one platform run. `completed: false` is a run that could not happen (a build
/// error, a missing binary, a test process that died before writing its report): the sweep
/// reports it and leaves the watermark alone. A completed run advances the watermark even when
/// cases failed, because the failures are the point of the sweep.
export type PlatformRun =
  | { completed: false; reason: string }
  | { completed: true; exitCodes: Record<string, number>; failures: CaseFailure[] }

/// A test runner: the real one builds and runs the suite, the fake one is scripted by the tests
/// and by `--runner fake`.
export type Runner = (plan: SweepPlan) => Promise<PlatformRun>

/// An open issue as `gh` lists it.
export type IssueRef = { number: number; title: string }

/// What the sweep does to GitHub for one failure or for the run as a whole.
export type IssueAction =
  | { kind: "create"; title: string; body: string }
  | { kind: "comment"; title: string; number: number; body: string }

/// The GitHub side, injected so no test ever reaches the network or the `gh` CLI.
export type Gh = {
  /// Open issues carrying `label`.
  openIssues(label: string): Promise<IssueRef[]>
  createIssue(title: string, body: string, label: string): Promise<number>
  commentIssue(number: number, body: string): Promise<void>
}

/// The commits a sweep covers, already rendered for an issue body.
export type CommitRange = {
  /// One line for the issue body, e.g. `a1b2c3d4..d4c3b2a1 (3 commits)`.
  label: string
  /// `sha subject` lines, newest first, capped.
  lines: string[]
  total: number
  /// False when the watermark is not an ancestor of HEAD (a rewritten branch): the range is then
  /// "recent commits up to HEAD" and the sweep says so.
  contiguous: boolean
}
