# Agentic software factory

GitHub issues on the fork are the queue. `deno task factory run` takes `factory:ready` tickets, gives each its own
[herdr](https://herdr.dev) worktree workspace, runs an omp implementer, runs the hard gate, runs a read-only omp
reviewer, then pushes a `factory/<issue>-<slug>` branch and opens a **draft** PR into `feature/improvements`. You
approve a spec once and merge PRs; an agent can never push, merge or edit its own guardrails.

Design and rationale: `plans/agentic-software-factory.md`. Every number and the protected-path list live in
`tools/factory/config.ts` (`config.json` is generated from it with `deno task factory emit-config`).

## One-time setup (Windows)

1. `deno`, `gh` (logged in with `repo` + `workflow`), `herdr` (run `herdr integration install omp`) and `omp` on PATH.
   herdr's default pane shell is set to `cmd.exe` in `%APPDATA%\herdr\config.toml` (`[terminal] default_shell`).
2. `deno task factory install` (sets `core.hooksPath=.githooks` and `push.default=nothing` for every worktree of the
   clone) and `deno task factory labels`.
3. WSL lint lane: `wsl -d Ubuntu -u root -e bash <checkout>/tools/factory/wsl/setup.sh`. It installs LLVM 22, the cata
   plugin toolchain and a shadercross build under `~/bn-tidy`, matching what CI runs.
4. Branch protection on `feature/improvements` (human only; needs repo admin):

```sh
gh api -X PUT repos/wavewarrior/Cataclysm-BN-Forked/branches/feature/improvements/protection --input - <<'JSON'
{
  "required_status_checks": { "strict": false, "contexts": ["guard", "deno", "json", "tidy", "linux-tests"] },
  "enforce_admins": false,
  "required_pull_request_reviews": null,
  "restrictions": null,
  "allow_force_pushes": false,
  "allow_deletions": false
}
JSON
```

## From an idea to merged PRs

For a feature too big for one session:

1. `/wayfinder <idea>` charts a map of decision tickets on the tracker and resolves them one at a time. Its destination should be a spec.
2. When the map has no open tickets and no fog (`deno task factory map-status <map>` exits 0), run `/factory-launch <map or spec>`. It reads the plan, grounds it in the code, drafts tracer-bullet tickets with real `Touches` and `Test tags`, and asks you to approve the breakdown.
3. On approval it runs `deno task factory publish <slug> <file> --parent <map>` (validated first with `--dry-run`), then `deno task factory release <slug>`. The watcher picks the released tickets up (see below).
4. Each ticket becomes a draft PR; dependent tickets start only once their blockers' issues are closed, so you merge in order.

`publish` rejects, before creating anything: non-conventional titles, missing tests or test tags, protected paths in `Touches`, gameplay/render tickets without episodes, unknown dependencies and cycles.

## Day to day

- Write tickets with `.github/ISSUE_TEMPLATE/factory-ticket.md` (the `to-tickets` skill emits it). They start as
  `factory:draft` + `spec:<slug>`.
- `deno task factory release <slug>` flips the spec to `factory:ready`. That is the approval.
- `tools\factory\watch.cmd` (or `deno task factory watch [--interval 60] [--once]`) is the poller to keep running in a herdr cmd pane. Every interval it lists `factory:ready` tickets; when one can be picked up it runs a driver pass, otherwise it prints one idle line and waits. If another driver holds every implementer lane, it waits quietly without backing off. Ctrl+C finishes the current pass and exits; a second Ctrl+C exits at once, and tickets already running keep their panes (check `status`).
- `deno task factory run [--max N] [--issue N]` is the one-shot pass the watcher runs. `status` shows tickets, lane locks and herdr agents; `stop <issue>` closes a ticket's panes and marks it blocked.
- A ticket ends as a draft PR (`factory:review`) or `factory:blocked` with the reason in an issue comment. Worktrees
  and panes are never deleted automatically.

## What stops an agent

| Layer    | Where                                 | Enforces                                                              |
| -------- | ------------------------------------- | --------------------------------------------------------------------- |
| omp hook | `.omp/hooks/pre/factory-guard.ts`     | no push, `gh`, destructive git, `--no-verify`, protected-path writes  |
| pre-push | `.githooks/pre-push`, `check_push.ts` | only `factory/*`, only a commit with a passing full stamp and verdict |
| CI       | `factory-ci.yml`                      | protected paths (from the base ref), fmt/lint, JSON, tidy, full tests |

Both local layers are inert unless `FACTORY_LANE=1`, which the driver sets only in lane panes. A determined bash
session can still forge local files; CI and the human merge are the independent backstop.

## The gate

`deno task gate --tier fast|full [--base <ref>]` is the only definition of done. `fast` runs `clean-tree`,
`protected-paths`, `new-test-present`, `deno-checks`, `json-lint` and `wsl-lane`; `full` adds `build`, `catch2-tags`,
`baseline-ratchet` and `bnplay`. Logs are under `<gitdir>/factory/logs/`, the result in `gate-stamp.json`.

- `deno fmt` and `deno lint` judge only the files the change touches (the tree has pre-existing drift);
  `deno test` runs everything. The `repo-rules` lint plugin rejects skipped/focused tests and suppressions without a
  `-- reason`.
- C++: new or changed lines must pass `modernize-use-trailing-return-type`, `modernize-use-auto` and `cata-*`
  (clang-tidy `--line-filter`), and touched files must be clang-format/astyle clean.
- The ratchet runs `~[.]` once and fails on any failing case not listed in `config.baselineFailures`. That list may
  only shrink, and only a human edits it (it is protected): after a fix merges, remove the case name.

## Troubleshooting

- A lane's first build in a fresh worktree configures `out/msvc` and rebuilds with ccache; expect several minutes.
- `wsl-lane` serialises on `~/bn-tidy/lock`. Plugin and game configure output are in `~/bn-tidy/*.log`.
- Editing `tools/factory/**`, workflows, lint/format configs, `deno.jsonc` or `AGENTS.md` is a human task done outside
  a lane (`FACTORY_LANE` unset).
