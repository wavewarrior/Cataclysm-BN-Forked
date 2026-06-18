# Orchestrator prompt — GI/perf remainder execution

Copy the fenced block below to the agent/orchestrator. It pins the plan doc
(`GI_PERF_REMAINDER_ONESHOT.md`, same folder) as source of truth and fails safe:
default scope is Task 0, visual steps are forced human handoffs, and the known
footguns (git add -A / rmlui files / struct-register drift / committing red) are
blocked.

**Before sending:** edit the `Scope for this run:` line. Default = `Task 0 only`.
Widen deliberately (e.g. `Task 0, then P5a, P5b`) — never say "all".

```
You are executing a pre-written implementation plan. Do NOT improvise scope.

SOURCE OF TRUTH (read it fully before doing anything):
/Users/nigel.fierens/dev-projects/Cataclysm-BN-Forked/docs/lighting/GI_PERF_REMAINDER_ONESHOT.md

Scope for this run: Task 0 only.   ← change to the task ID(s) you want, or "Task 0, then P5a, P5b". Never "all".

NON-NEGOTIABLE RULES (violating any = stop and report):
1. The doc is authoritative. Read it top-to-bottom first, including the "How to
   execute this as an agent" and "Orientation" blocks. Follow the named file paths,
   register layouts, and gotchas exactly. If the doc and the code disagree, STOP and
   report the discrepancy — do not guess.
2. You CANNOT see rendered frames. Any step that says "eyeball / run a world / play N
   turns / verify visually" is a HUMAN handoff: do the edit + build + gate, then STOP
   and tell me precisely what to look at. Never self-certify or invent a visual result.
3. Verification gates, in order, before any commit:
   a. Build: cmake --build out/build/osx-arm-slim --target cataclysm-bn-tiles
   b. Reflect gate (if any shader changed):
      cmake --build out/build/osx-arm-slim --target shader_reflect_check \
        && out/build/osx-arm-slim/tools/shader_check/shader_reflect_check
   c. Confirm the binary actually relinked (mtime newer than your edit) — a no-error
      build can skip relinking.
   If build or gate fails, fix or STOP — never commit red.
4. git: NEVER `git add -A` / `git add .`. Stage ONLY the explicit file list the task
   names. Specifically do NOT touch theme.rcss or any rmlui_* file (unrelated work).
   Use the commit message + exact Co-Authored-By trailer the doc specifies.
5. One task = one commit. Do not bundle tasks. Do not start the next task until the
   current one is committed (or you've stopped for human verify).
6. Do not redo anything in the doc's "already done" list. P3 (JFA) is multi-session —
   if in scope, land ONE sub-commit (P3.1) and stop for review.
7. Struct/shader discipline: if you change a cbuffer/params struct, keep the C++ struct
   and HLSL cbuffer field-order + size identical and update any static_assert. If you
   change a fragment storage-buffer register, update the C++ bind order in lockstep.

OUTPUT at each stop: what you changed (files), build result, gate result, and the
exact human action you need next (or "committed: <hash/subject>"). Be terse.

Begin: read the doc, then execute the scoped task.
```
