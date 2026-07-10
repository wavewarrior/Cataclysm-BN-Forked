## STATUS (completed 2026-07-03)
**100% DONE.** Both helpers extracted: `activate_combat_gear()` and `resolve_gun_mode()` declared in `npc.h` and implemented in `npcmove.cpp`. `method_of_attack()` reduced from **119 lines to 87 lines**. Build green (MSVC Release).

# NPC AI Cleanup — Plan

> Scope rewritten 2026-06-23 after review. The prior version proposed a
> utility-ranked `npc_goal` strategy pattern (Phase 1), a perception/AI split
> (Phase 3), and a `talk_function` dispatch-table (Phase 4). All three were
> dropped:
> - **Strategy pattern** — `npc::move()` (`npcmove.cpp:828-1103`) is a hand-ordered
>   priority *cascade* with early-return hard-overrides (dangerous fields,
>   explosives) and contextual gating (vehicle, attitude, follow distance). A flat
>   `vector<npc_goal>` ranked by `utility()` descending cannot express early-return
>   overrides without re-encoding all that context into every `utility()` call.
>   It also adds virtual dispatch + heap indirection on a per-NPC-per-turn hot path
>   (`game.cpp:6529`), directly opposing `SIM_PERFORMANCE_PLAN`.
> - **Perception/AI split** — its stated payoff ("what SIM_PERF Part 2 needs") is
>   false: Part 2 explicitly says *do not* separate perception/AI, and is already
>   shipped (commit `5315065c12`: `npc_lod_tier`, tier/budget/stride in
>   `game::npcmove()` and `npc::move()`).
> - **talk_function table** — already ships: `static_functions_map` built via the
>   `WRAP` macro at `npctalk.cpp:3119-3121`. Nothing to build.

## Context

`method_of_attack()` (`npcmove.cpp:1502-1620`, 118 lines) interleaves three
concerns in one function:
1. **Side-effecting combat prep** — `activate_combat_cbms()`, transforming worn
   COMBAT_NPC_USE armor, transforming the wielded weapon (`:1530-1554`).
2. **Gun-mode resolution** — picking/clearing `g_mode` against silent/ammo/dist
   gates (`:1567-1573`).
3. **The decision cascade** — alt-attack → wield-better → reach → shoot →
   avoid-friendly-fire → reload → melee → aim → undecided (`:1556-1619`), each
   returning an `npc_action` with debug logging.

The mixing makes the decision cascade hard to read (the actual AI logic) because
it is buried under gear-activation side effects. This is a maintainability
cleanup, **not** a perf change and **not** a behavior change.

## Approach

Single phase, one function, mechanical. No new files, no new types beyond two
private helpers on `npc`.

### Extract the two non-decision blocks

| New private method | Extracted from | Signature | Notes |
|---|---|---|---|
| `npc::activate_combat_gear()` | `:1530-1554` | `void` | Pure side effects: combat CBMs + worn/weapon transforms. No return. |
| `npc::resolve_gun_mode()` | `:1567-1573` | `gun_mode` | Returns the usable mode or a null `gun_mode()`. Takes the already-computed `can_use_gun`, `dist`, `use_silent` as params (or recomputes locally — pick one, don't pass 3 bools if recompute is cheap). |

After extraction, `method_of_attack()` reads as: target guard → compute
`dist`/`has_los`/`same_z`/`cur_recoil`/engagement flags → `activate_combat_gear()`
→ early alt-attack/wield returns → `g_mode = resolve_gun_mode(...)` → the
reach/shoot/reload/melee/aim cascade. ~70 lines, the cascade no longer buried.

### Do NOT

- Do not invent `select_target()` — `current_target()` already exists
  (`npc.h:940`) and returns `Creature*` (targets are mostly **monsters**, not
  `Character`).
- Do not split the decision cascade itself into per-branch functions — the
  ordering *is* the logic; keeping it one linear read is the point.
- Do not change any return value or early-return condition.

## Optional follow-on (only if the above lands clean)

A targeted C++23 pass **on the touched function only**:
- Trailing return type on the new helpers (match neighbours — file is mixed; do
  not sweep the whole file).
- The `worn` activation loop (`:1533`) can stay a range-for; no `ranges::`
  needed (it has side effects + early `invoke_item`).

Skip the file-wide "ranges/views/std::expected/options-struct" sweep from the old
plan — `npcmove.cpp` is per-turn hot-path code and a cosmetic sweep buys nothing
while risking behavior drift in the cascade.

## Verification

- Build green.
- **Behavior unchanged** is the hard requirement. The cascade returns identical
  `npc_action` for identical inputs. Best check: load a combat save, confirm an
  NPC with a gun still shoots/aims/reloads in the same situations (manual A/B —
  there is no NPC-combat unit test harness; do not claim one).
- `method_of_attack()` body drops from 118 to ~70 lines; the two helpers are
  each self-contained.

## Files

| File | Change |
|---|---|
| `src/npcmove.cpp` | Extract 2 helpers; slim `method_of_attack` |
| `src/npc.h` | Declare `activate_combat_gear()` + `resolve_gun_mode()` private |

## Effort: 0.5–1 day
