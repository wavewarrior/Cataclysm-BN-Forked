# Turn plan: one authority list

Vocabulary follows `GLOSSARY.md` (Turn plan, Turn step, Fork law, Plan law). Sibling of the frame-assembly module; parallels ADR-0003. Closes the deferred "turn-plan candidate" of `plans/coop-world-event-interpreter.md`.

## Problem (verified 2026-10-09)

The post-input turn cycle exists three times, kept aligned by prose:

- `game::post_action_world_step()` — `src/game_action.cpp:1833-2155` (~323 lines)
- `game::coop_client_turn_step()` — `src/game_action.cpp:2157-2241`, the avatar-local half, comment at `:2160`: _"statement-for-statement"_
- `game::do_turn()`'s inline sim block — `src/game.cpp:846-1330`, same steps, same perf-probe statics (`game.cpp:856-858` vs `game_action.cpp:1839-1841`), only `ZoneScopedN` names differ
- plus `coop_client_frame_step()` — `game_action.cpp:2243-2275`, the once-per-sync slice lifted out again

Consumers: solo `game.cpp:3496`; host `coop_server.cpp:473,512,533`; client catch-up `coop_client.cpp:1031`; driver `driver_loop.cpp:386,425`.

## Decisions (grilled 2026-10-09)

1. **Shape**: pure plan value + one executor, mirroring `frame_plan.{h,cpp}` (283 lines) and ADR-0003.
2. **Granularity**: existing call groups are steps (~30), not statements, not coarse phases.
3. **Scope**: the turn step only. Host-internal tick ordering (drain/snapshot sandwich, `coop_server.cpp:516-533`) stays in `coop_server`. `do_turn` keeps its input loop, game-over guard and time-scale guard; its inline sim block is absorbed.
4. **Skips are plan-time facts**: sleep-skip options (`game_action.cpp:1846-1851`) and gates resolve at plan time; every skipped step records a reason in the turn report. `try_activity_fixed_window_skip()` stays a pre-turn gate before the plan runs.
5. **Perf probe becomes executor bookkeeping**: per-step durations aggregated by group tag; the `[sim][perf]` debug.log line format stays compatible.
6. **Authority taxonomy**: three scopes per step — `world` / `avatar` / `frame`. Modes: solo = world+avatar per turn; host = all; client turn = avatar; client frame = frame.
7. **Seam**: `src/turn_plan.{h,cpp}` beside `frame_plan`; plan derivation and laws pure, no world needed.
8. **Caller surface**: one door `run_turn_plan(mode)`; `post_action_world_step`, `coop_client_turn_step`, `coop_client_frame_step` deleted from `game.h:232-243` (single-door precedent `9f69d57756`).
9. **Tracy zones**: executor emits a per-step zone preserving the `do_turn_*` / step names so profiling greps keep working.
10. **Migration**: commit 1 characterization tests through existing seams; commit 2+ single clean cutover, no wrappers, byte-compatible behavior; ordering hazards pinned first (floor caches before falling `game_action.cpp:1994-1996`, scent before `scent.update`, catch-up cap `coop_client.cpp:1028-1031`).
11. **Plan laws** (asserted on the value, no world): Fork law (client per-turn plan has no `world` step); calendar advance precedes every turn-reading step; frame-scope steps appear exactly once per sync, never inside the catch-up loop; every skipped step carries a reason.
12. **ADR-0004** recorded after cutover.

## Commit order

1. Characterization: in-proc coop parity cases extended; step-order probe capturing today's call sequence of all three copies; `[sim][perf]` line format pinned.
2. `turn_plan.{h,cpp}`: step registry with scopes + gates, plan derivation per mode, executor with zones/report/perf aggregation. Laws in `tests/turn_plan_test.cpp`.
3. Cutover: `do_turn` sim block, `post_action_world_step`, `coop_client_turn_step`, `coop_client_frame_step` → `run_turn_plan(mode)`; callers swept; old verbs deleted.
4. Docs: ADR-0004, GLOSSARY already updated.

## Out of scope

Host tick orchestration (`coop_world_tick`), input window, `do_turn`'s input loop, candidate 2 (lighting settings) and beyond.
