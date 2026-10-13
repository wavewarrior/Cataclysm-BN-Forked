# Check: the one gate

`deno task check` is the whole definition of done: one command, one exit code, run on the Executor
(the Linux side of the home box, or the GitHub runner in CI). Implementation: `tools/check/main.ts`;
the step policy (ordering, guard matching, base resolution, event-log writing) is pure and unit-tested
next to it.

```sh
deno task check [--base <ref>] [--no-fix] [--jobs N]
```

`--base` defaults to `origin/feature/improvements`; `--no-fix` skips the auto-fix step (CI runs with
it); `--jobs` defaults to the machine's core count. Environment: `CHECK_LOG_DIR` (default
`./.check/`), `CHECK_SEED_DIR` (a configured build dir whose fetched dependency sources to reuse),
`CHECK_ALLOW_PROTECTED=1` (a human override for the guard).

## The steps, in order

| Step      | What happens                                                                                                                                                                         | Fails the check?                |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------- |
| guard     | the diff vs base must not touch `tools/check/**`, `tools/lane/**`, `.github/workflows/**`, `.githooks/**`, `.clang-format`, `.clang-tidy`, `tools/clang-tidy-plugin/**`, `AGENTS.md` | yes                             |
| autofix   | `deno task fmt:cpp` on changed C++ and the JSON formatter on changed JSON                                                                                                            | never (reports rewrites)        |
| configure | the `linux-fast` preset, offline when `CHECK_SEED_DIR` supplies the fetched dependency sources                                                                                       | yes                             |
| build     | `cataclysm-bn-tiles` and `cata_test-tiles` via the `linux-fast` build preset                                                                                                         | yes; later steps report skipped |
| tests     | `cata_test-tiles --rng-seed 1` with `CATA_TEST_FORK_JOBS=$(nproc)`: the Fork runner puts every case in its own child process                                                         | yes                             |
| tidy      | `deno task tidy:changed --base <ref>`: the correctness checks on changed lines only                                                                                                  | yes                             |
| co-op     | `deno task test:coop`, only when the diff touches `src/coop*` or `src/net*`                                                                                                          | yes                             |

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
