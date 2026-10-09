---
name: factory-launch
description: "Turn a finished wayfinder map (or a spec) into factory tickets, get the breakdown approved, release them and start the factory so the feature is built as draft PRs. Use after wayfinder has charted the way, or when a spec exists."
disable-model-invocation: true
---

Hand a **finished plan** to the software factory. The input is a wayfinder **map** (issue labelled `wayfinder:map`) whose destination is a spec, or a **spec** (issue or file). The output is `factory:ready` tickets being worked by `deno task factory run`, each ending as a draft PR for the human to merge.

You are the last planning step. You do **not** implement, push or merge anything yourself, and you never put a protected path in a ticket (see `tools/factory/config.ts`).

## 1. Resolve the input

The user passes a map (URL or number) or a spec (path, URL or number). If they pass nothing, ask which with the `ask` tool.

**A map.** Run `deno task factory map-status <map>`. It exits 0 only when the map has no open child tickets and no text under **Not yet specified**.

- Non-zero: the way is not clear yet. Say which tickets are still open and what fog remains, and stop. Offer `/wayfinder <map>` to keep resolving; do not launch an unfinished map.
- Zero: load the map low-resolution (Destination, Notes, Decisions so far) and follow the links you need. If the destination is a spec that already exists, read it. If the decisions are made but no spec has been written, call `/to-spec` first and use its result.

**A spec.** Read it in full, with its comments.

Everything the tickets need must come from the map or spec. If a decision the build depends on is missing, stop and say so rather than guessing.

## 2. Ground it in the code

Factory tickets, unlike `to-tickets` output, **must** name real paths and tests, because the gate checks them. Explore the repo to find:

- the files each slice will edit (`Touches`);
- the Catch2 tags or test names that exercise it (`Test tags`); a tag that matches no test fails the gate, so check they exist or that the slice itself adds the test under a tag it names;
- the glossary and ADRs for the area, so titles use the project's vocabulary.

## 3. Draft the breakdown

Slice the work into **tracer-bullet** tickets exactly as `/to-tickets` describes (vertical, each green and verifiable alone, prefactor first, expand-contract for wide refactors), with these factory constraints:

- **One lane, one ticket.** Each ticket is implemented by a small model in a fresh context and then must pass a gate that costs about 15 minutes (full build plus the whole test suite). Do not over-slice: prefer fewer, meatier slices over many trivial ones.
- **Self-contained.** The implementer sees only its ticket and the repo. Put the decisions it needs in `goal` and `acceptance`; link the map or spec by number, not by pasting.
- **Titles are conventional commits** (`feat(scope): ...`), because they become PR titles.
- **Tests.** A ticket that changes `src/` must add or change tests in `tests/`, or carry a `noTestNeeded` reason.
- **Blocking.** `dependsOn` lists the keys of tickets that must be merged first. A dependent ticket is only picked once its blockers' issues are closed, so a chain merges in order.
- **Gameplay or render changes** get the label `gameplay` or `render` and `episodes` (a bnplay Trial, optionally followed by a steps file). Without them the gate only checks the build and tests.
- **Never touch protected paths** (`tools/factory/**`, `.omp/**`, `.githooks/**`, `.github/workflows/**`, lint and format configs, `deno.jsonc`, `AGENTS.md`, `docs/agents/**`, `.agents/skills/**`, `.claude/skills/**`, `tools/clang-tidy-plugin/**`, `build-scripts/**`). If the plan needs one, list it as a **human task** in your summary instead of a ticket.

## 4. Get approval

Present the breakdown as a numbered list (title, blocked by, what it delivers, paths, tags) and put the decisions to the user with the `ask` tool, never as chat questions: granularity, blocking edges, merge or split, and any human tasks. Iterate until approved. This is the approval point: nothing is created before it.

## 5. Publish as drafts

Pick a short kebab-case `<slug>` for the feature. Write the approved breakdown as JSON to `out/factory/<slug>-tickets.json` (`out/` is gitignored; create the directory), an array of:

```json
{
  "key": "short-local-handle",
  "title": "feat(scope): imperative summary",
  "goal": "what changes and why",
  "acceptance": "the observable result",
  "touches": ["src/foo.cpp", "tests/foo_test.cpp"],
  "testTags": ["[foo]"],
  "episodes": ["tools/bnplay/trials/x.trial.toml"],
  "dependsOn": ["other-key", "#123"],
  "noTestNeeded": "only when src/ changes without tests/",
  "labels": ["gameplay"]
}
```

Then:

```sh
deno task factory publish <slug> out/factory/<slug>-tickets.json --parent <map-or-spec-issue> --dry-run
deno task factory publish <slug> out/factory/<slug>-tickets.json --parent <map-or-spec-issue>
```

The dry run reports every problem (unconventional title, missing tests, protected path, unknown dependency, cycle) before anything is created. Fix and re-run until it is clean. Publishing creates the issues blockers-first as `factory:draft` + `spec:<slug>` and writes the real issue numbers into each `## Depends on`. Report the created tickets by title with their links.

## 6. Release and run

Ask once more with `ask`: release `<slug>` and start the factory now? On yes:

1. `deno task factory release <slug>` flips every draft of that spec to `factory:ready`.
2. Make sure a watcher is polling: if none runs in a pane, start `tools\factory\watch.cmd` in a new herdr cmd pane. The watcher picks the released tickets up on its own; do not wait on it or poll it from here. If one already runs, releasing is enough.

Tell the user how to follow it: `deno task factory status` for tickets, lanes and agents; draft PRs arrive on `feature/improvements` and the issues move to `factory:review`; a ticket that cannot finish is `factory:blocked` with the reason in a comment. Merging stays with the human.

## Rules

- Never create tickets before the user approves the breakdown.
- Never launch a map that still has open tickets or fog.
- Never edit protected paths, push, merge, or open PRs yourself.
- If any command fails, report its output and stop; do not work around the factory.
