/// The only place factory numbers and lists live.
/// `tools/factory/config.json` is generated from this (`deno task factory emit-config`) so CI can
/// read the protected-path list from the BASE ref without importing TS.
export const config = {
  integrationBranch: "feature/improvements",
  repo: "wavewarrior/Cataclysm-BN-Forked",
  maxLanes: 2,
  /// Gate failures AND reviewer FAILs share this budget.
  maxFixRetries: 2,
  ticketWallClockMin: 90,
  models: {
    implementer: "anthropic/claude-sonnet-5-5",
    reviewer: "anthropic/claude-opus-5-5",
    workhorse: "anthropic/claude-haiku-5-5",
  },
  /// Branch is `factory/<issue>-<slug>`.
  branchPrefix: "factory/",
  /// Worktree directory is `wt-factory-<issue>`.
  worktreeRoot: "C:/WORK/GIT_REPOS",
  lockDir: "C:/WORK/factory-lanes",
  /// Paths an unattended lane may never change. Enforced by the omp hook, the gate and CI.
  protectedPaths: [
    "tools/factory/**",
    ".omp/hooks/**",
    ".githooks/**",
    ".github/workflows/**",
    ".clang-tidy",
    ".clang-format",
    ".astylerc",
    "deno.jsonc",
    "dprint.json",
    "AGENTS.md",
    ".omp/rules/**",
    "tools/clang-tidy-plugin/**",
    "build-scripts/**",
  ] as readonly string[],
  /// Exact Catch2 case names allowed to fail in the full run. May only shrink.
  baselineFailures: [] as readonly string[],
  /// New or changed C++ lines must satisfy these; legacy lines are exempt (clang-tidy --line-filter).
  lintOnNewLines: {
    checks: ["modernize-use-trailing-return-type", "modernize-use-auto", "cata-*"],
    warningsAsErrors: ["modernize-use-trailing-return-type", "modernize-use-auto", "cata-*"],
  },
  /// When true the driver refuses to open a PR unless the remote CI checks exist.
  requireCi: true,
}

export type FactoryConfig = typeof config
