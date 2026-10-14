# Check: the one gate

`deno task check` is the whole definition of done: one command, one exit code, run on the Executor
(the Linux side of the home box). There is no CI: this command is the gate. Implementation:
`tools/check/main.ts`; the step policy (ordering, guard matching, base resolution, event-log
writing) is pure and unit-tested next to it.

```sh
deno task check [--base <ref>] [--no-fix] [--jobs N] [--dry-run] [--macos]
```

`--base` defaults to `origin/feature/improvements`; `--no-fix` skips the auto-fix step; `--jobs`
defaults to the machine's core count; `--macos` selects the Mac compile gate below. Environment:
`CHECK_LOG_DIR` (default `./.check/`), `CHECK_SEED_DIR` (a configured build dir whose fetched
dependency sources to reuse), `CHECK_ALLOW_PROTECTED=1` (a human override for the guard).

## The steps, in order

| Step      | What happens                                                                                                                                                                         | Fails the check?                |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------- |
| guard     | the diff vs base must not touch `tools/check/**`, `tools/lane/**`, `.github/workflows/**`, `.githooks/**`, `.clang-format`, `.clang-tidy`, `tools/clang-tidy-plugin/**`, `AGENTS.md` | yes                             |
| autofix   | `deno task fmt:cpp` on changed C++ and the JSON formatter on changed JSON                                                                                                            | never (reports rewrites)        |
| tools     | `deno check`, `deno lint` and the matching `*_test.ts` for changed `tools/**` and `scripts/**` TypeScript, before the build                                                          | yes                             |
| configure | the `linux-fast` preset, offline when `CHECK_SEED_DIR` supplies the fetched dependency sources                                                                                       | yes                             |
| build     | `cataclysm-bn-tiles` and `cata_test-tiles` via the `linux-fast` build preset                                                                                                         | yes; later steps report skipped |
| tests     | `cata_test-tiles --rng-seed 1` with `CATA_TEST_FORK_JOBS=$(nproc)`: the Fork runner puts every case in its own child process                                                         | yes                             |
| tidy      | `deno task tidy:changed --base <ref>`: the correctness checks on changed lines only                                                                                                  | yes                             |
| co-op     | `deno task test:coop`, only when the diff touches `src/coop*` or `src/net*`                                                                                                          | yes                             |
| macos     | `--macos` only, on a Mac: both binaries on `osx-arm-slim`, no tests (see The macOS gate); replaces autofix to co-op                                                                  | yes                             |

Every step is timed, printed as one line, and appended as one JSON object to
`$CHECK_LOG_DIR/events.jsonl`, so a lane's run is readable after the fact and mid-run (`tail -f`).

## Lanes

`deno task lane <issue>` starts an agent on an issue: a git worktree at `$LANE_ROOT/lane-<n>`
(default `~/cbn/lanes`) on branch `lane/<n>-<slug>`, seeded dependency sources copied (`cp -a`, never
hard-linked) from `$CHECK_SEED_DIR`, configured `linux-fast` with `CCACHE_BASEDIR=$LANE_ROOT` so all
lanes share one compiler cache, and a detached tmux session `lane-<n>` running `$LANE_AGENT_CMD`
(default `omp`) with the issue body as its prompt, logging to `<worktree>/.check/lane.log`. Starting
refuses when the session already exists.

```sh
deno task lane 123          # start
deno task lane status       # every lane: session alive? worktree dirty?
deno task lane attach 123   # watch read-only (tmux attach -r); --write to type
deno task lane stop 123
```

There is no watcher, no label state machine and no stamps: the issue is the queue, `check` is the
gate, the human merges.

## Auto-fix and the stamp

Without `--no-fix`, `check` first rewrites formatting in the files the diff touches. A run that changed any
tracked file is never stamped, because the tested tree is no longer the commit. The lane's flow is: run
`check`, commit what auto-fix changed, then run `check --no-fix`; that second run writes the stamp.
A build must not modify tracked files either: `linux-fast` sets `LUA_DOCS_ON_BUILD=OFF` because the
post-build `deno task docs:gen` runs `deno fmt` over the whole tree. Regenerate the docs explicitly.

## The pre-push hook

A passing run writes the commit it tested to `.check/passed` (only when every step passed and the
tracked tree matched that commit). `.githooks/pre-push` refuses a push whose branch tip is not that
commit, so a push without a passing `check` fails locally. The stamp is written where `check` ran, on
the Linux executor; the flow is to copy it back to the clone you push from (`scp` the file, or let
the `deno task box` command do it) and push there. Compare the stamp with `git rev-parse HEAD`
first: they must match.

Skipping is deliberate and recorded: `CHECK_SKIP='why' git push` pushes anyway, prints the skip on
stderr, and appends time, user, ref, commit and reason to `.check/skips.log`. The log is local to the
clone, an audit trail and not an enforcement: read it before trusting a branch that arrived by skip.

Enable the hooks once per clone with `git config core.hooksPath .githooks`.

## The macOS gate

`deno task check --macos` is the opt-in compile gate for a change to a header or an `#include`. It
runs on a Mac only and runs three things: the guard, the tooling checks if `tools/**` or `scripts/**`
changed, and a build of both binaries on `osx-arm-slim`. It runs **no tests**: the macOS logic tier
and `[json]` fail at the base commit for reasons unrelated to a change, and would turn the gate red.
It writes `.check/macos-passed`, never `.check/passed`, so neither stamp can vouch for the other.
Why it exists: the Linux build never compiles `TILES` or `__APPLE__` branches, which is where an
include removal breaks first. The pre-push hook prints a note when a pushed header change has no
`macos-passed` for its tip. It never blocks.
