# SIM_PERFORMANCE Part 2 — NPC LOD/Budget Execution Plan

> **Source:** `plans/SIM_PERFORMANCE_PLAN.md` Part 2 (NPC LOD/budget)
> **Status:** ✅ Done — implemented 2026-06-22, builds green, all [npc]/[behavior]/[simulation] tests pass
> **Ground truth verified:** 2026-06-22 — all line numbers confirmed against current codebase

## Shared context

The NPC pipeline (`game::npcmove`, `game.cpp:6384-6457`) processes **every simulated NPC** every turn with
zero distance gating or budget control — the only filter is `is_simulated()`. Each NPC runs:

1. `process_turn()` (`npc.cpp:3130`) → `Character::process_turn()` (`character_turn.cpp:179`) →
   `process_items()` (rot checks), `suffer()` (stat damage), `process_effects()` (DoT/durations),
   `moves += get_speed()` (~100 moves/turn)
2. `move()` loop (`while moves > 0`, capped at 10 iterations):
   - `regen_ai_cache()` (`npcmove.cpp:765`) → `assess_danger()` (`npcmove.cpp:394`)
     which iterates **ALL monsters** (`critter_tracker->get_monsters_list()`), calling
     `npc_turn_cached_sees()` (LoS raycast) + `evaluate_enemy()` per monster
   - Decision tree (method_of_attack, flee, investigate_sound, address_needs, etc.)
   - `execute_action()`

Cost profile per-turn: `O(N_npc × (process_turn + N_monsters × LoS))`. With 200 monsters and 10 NPCs,
that's 2000 monster evaluations per turn with LoS raycasts.

### Key differences from monster LOD system

| Aspect | Monsters | NPCs |
|--------|----------|------|
| Distance LOD | `lod_dist` → Tier 0/1/2 | None — every sim'd NPC is full AI |
| Action budget | `lod_action_budget` (default 128) | None — no cap |
| Coarse AI | Tier 1 skips faction queries, strides scent | None |
| Macro step | Tier 2 does 4-dir step every 3 turns | None |
| Parallel planning | Yes (`compute_plan` on worker threads) | Serial only (NPC mutates broad state) |
| Activity-skip cache | Full snapshot reuse across batched turns | None — `npcmove()` runs fully every skip-turn |

### Constraints

- **Followers/companions** (`is_player_ally()`, `is_following()`, `is_walking_with()`,
  `mission == NPC_MISSION_ACTIVITY/GUARD_ALLY`) MUST remain full AI every turn — they are
  player-facing and visible.
- **Visible NPCs** (within player sight range) MUST remain full AI — the player can see them.
- **Hostile NPCs** engaged in combat MUST remain full AI — correctness critical.
- **No parallel planning** — NPC `regen_ai_cache()` / `assess_danger()` mutates
  `ai_cache`, path, and threat state. The monster `compute_plan`/`apply_plan` split does
  not apply here.

---

## Phase A — NPC distance tiering + action budget (2–3 days)

### Root cause

`npcmove()` at `game.cpp:6393` iterates `all_npcs()` with no distance check or budget cap.
An NPC 100 tiles away with no combat role runs the same full AI as an ally standing next to
the player. For large NPC counts (10–20+), this is the dominant same-z cost.

### Approach

Mirror the monster LOD tier system with NPC-specific options:

- **Tier 0 (Full):** `process_turn()` + full `move()` loop with `assess_danger()` monster scan.
  NPCs in this tier: distance ≤ `npc_tier0_dist`, OR follower/ally, OR within player sight radius.
- **Tier 1 (Coarse):** `process_turn()` + full `move()` loop, but `assess_danger()` skips the
  monster scan (lines 553–637). Full monster scan runs every `NPC_COARSE_DANGER_INTERVAL` turns.
  NPCs in this tier: distance between `npc_tier0_dist` and `npc_tier1_dist`, non-follower,
  non-visible.
- **Tier 2 (Macro):** `process_turn()` + occasional macro step. No full AI decision tree.
  NPCs in this tier: distance > `npc_tier1_dist`, non-follower, non-visible.

**Action budget:** Cap on Tier 0+1 NPCs entering the move loop per turn. Followers are exempt
from budget. Farthest NPCs (by `lod_dist`) get `moves = 0` and their `next_turn` is NOT advanced
(they are highest-priority next turn), using the same budget-drain pattern as monsters
(`game.cpp:6169-6173`).

### Files to modify

| File | Change | Lines |
|------|--------|-------|
| `src/cached_options.h` | Add `npc_lod_enabled`, `npc_tier0_dist`, `npc_tier1_dist`, `npc_demotion_cooldown`, `npc_action_budget` | +5 after `lod_lifecycle_stride` |
| `src/cached_options.cpp` | Add defaults | +5 after `lod_lifecycle_stride` |
| `src/options.cpp` | Register NPC LOD option group | ~25 lines |
| `src/npc.h` | Add `npc_lod_tier`, `npc_lod_cooldown` fields | +2 in public section |
| `src/game.cpp` | Add `npc_tier_assign_all()`; modify `npcmove()` for tier + budget | ~50 lines |

### Step-by-step

#### Step A1 — cached_options.h

After the `lod_lifecycle_stride` line:

```cpp
/** NPC LOD options — mirror monster LOD system for NPC AI detail. */
extern bool npc_lod_enabled;
extern int  npc_tier0_dist;
extern int  npc_tier1_dist;
extern int  npc_demotion_cooldown;
extern int  npc_action_budget;
```

#### Step A2 — cached_options.cpp

After `int lod_lifecycle_stride = 4;`:

```cpp
bool npc_lod_enabled       = true;
int  npc_tier0_dist        = 30;
int  npc_tier1_dist        = 75;
int  npc_demotion_cooldown = 3;
int  npc_action_budget     = 16;
```

#### Step A3 — options.cpp

**Register a new option group** after the monster LOD group (after the `LOD_LIFECYCLE_STRIDE` closing):

```cpp
    add_option_group( performance, Group( "lod_npc", to_translation( "NPC LOD" ),
                                          to_translation( "Configure level-of-detail thresholds for NPC AI." ) ),
    [&]( auto & page_id ) {
        add( "NPC_LOD_ENABLED", page_id,
             translate_marker( "NPC LOD Enabled" ),
             translate_marker( "If true, NPC AI fidelity decreases with distance from the player.  "
                               "Close NPCs run full AI; distant NPCs run progressively coarser AI.  "
                               "Companions and visible NPCs always run full AI regardless." ),
             true
           );
        add( "NPC_TIER0_DIST", page_id,
             translate_marker( "Full AI Radius" ),
             translate_marker( "NPCs within this radius run the complete AI every turn.  "
                               "Must be less than the Coarse AI Radius.  "
                               "Companions always run full AI regardless of distance." ),
             5, 208, is_android ? 20 : 30 );
        add( "NPC_TIER1_DIST", page_id,
             translate_marker( "Coarse AI Radius" ),
             translate_marker( "NPCs between the Full AI Radius and this distance run coarse AI: "
                               "process_turn and move loop every turn, but monster-danger scanning "
                               "runs less frequently (see NPC Coarse Danger Interval).  "
                               "NPCs beyond this distance are Tier-2 (process_turn only, no move loop)." ),
             10, 208, is_android ? 40 : 75 );
        add( "NPC_DEMOTION_COOLDOWN", page_id,
             translate_marker( "Demotion Cooldown" ),
             translate_marker( "Turns an NPC must wait after being promoted to a higher-fidelity "
                               "tier before it can be demoted again.  Prevents rapid tier oscillation "
                               "at distance boundaries.  0 disables the cooldown." ),
             0, 10, 3 );
        add( "NPC_ACTION_BUDGET", page_id,
             translate_marker( "Action Budget" ),
             translate_marker( "Maximum number of non-follower NPCs that can enter the full move loop "
                               "each turn.  When the budget is exceeded, the farthest NPCs are deferred "
                               "to the next turn.  Followers and visible NPCs are always processed "
                               "regardless of budget.  0 disables the budget cap." ),
             0, 128, is_android ? 8 : 16 );
    } );

    get_option( "NPC_TIER0_DIST" ).setPrerequisite( "NPC_LOD_ENABLED" );
    get_option( "NPC_TIER1_DIST" ).setPrerequisite( "NPC_LOD_ENABLED" );
    get_option( "NPC_DEMOTION_COOLDOWN" ).setPrerequisite( "NPC_LOD_ENABLED" );
    get_option( "NPC_ACTION_BUDGET" ).setPrerequisite( "NPC_LOD_ENABLED" );
```

**In `cache_to_globals()`**, after the monster LOD cached reads:

```cpp
    npc_lod_enabled       = ::get_option<bool>( "NPC_LOD_ENABLED" );
    npc_tier0_dist        = ::get_option<int>( "NPC_TIER0_DIST" );
    npc_tier1_dist        = ::get_option<int>( "NPC_TIER1_DIST" );
    npc_demotion_cooldown = ::get_option<int>( "NPC_DEMOTION_COOLDOWN" );
    npc_action_budget     = ::get_option<int>( "NPC_ACTION_BUDGET" );
```

#### Step A4 — npc.h

After the `last_updated` field (~line 1320), add LOD tier fields:

```cpp
        // LOD tier for distance-based AI fidelity reduction.
        //   0 = Full AI (close or follower)
        //   1 = Coarse AI (mid-distance, non-follower)
        //   2 = Macro   (far, non-follower; process_turn only)
        // Transient — not saved or loaded.
        int8_t npc_lod_tier     = 0;
        int    npc_lod_cooldown = 0;
```

#### Step A5 — game.cpp

##### A5a. Add `npc_tier_assign_all()` before `npcmove()`

Insert a new function before `npcmove()` (~line 6375):

```cpp
/// Assign NPC LOD tiers based on distance, follower status, and visibility.
/// Followers/companions and potentially-visible NPCs are always Tier 0.
/// Returns the count of Tier 0 NPCs (used for budget floor).
static int npc_tier_assign_all()
{
    if( !npc_lod_enabled ) {
        for( npc &guy : g->all_npcs() ) {
            guy.npc_lod_tier     = 0;
            guy.npc_lod_cooldown = 0;
        }
        return 0;
    }

    const auto player_pos = g->u.bub_pos();
    const int tier01_dist = npc_tier0_dist;
    const int tier12_dist = std::max( npc_tier1_dist, tier01_dist + 1 );
    const int demote_cd   = npc_demotion_cooldown;
    // Player's approximate sight range — NPCs within this band are "potentially visible"
    // and must keep full AI.  Use g_max_view_distance (cached) as a cheap approximation
    // instead of calling sees() on every NPC.
    const int sight_radius = g_max_view_distance;
    int tier0_count = 0;

    for( npc &guy : g->all_npcs() ) {
        int8_t new_tier;

        // Followers/companions are always Tier 0 — player-facing.
        if( guy.is_player_ally() || guy.is_following() ||
            guy.mission == NPC_MISSION_ACTIVITY ||
            guy.mission == NPC_MISSION_GUARD_ALLY ) {
            new_tier = 0;
        } else {
            const int dist = lod_dist( guy.bub_pos(), player_pos );
            const int abs_dz = std::abs( guy.bub_pos().z() - player_pos.z() );

            // NPCs within player sight radius are potentially visible — keep full AI.
            if( dist <= tier01_dist || ( abs_dz < 2 && dist <= sight_radius ) ) {
                new_tier = 0;
            } else if( dist <= tier12_dist ) {
                new_tier = 1;
            } else {
                new_tier = 2;
            }
        }

        // Promotion immediate; demotion respects cooldown.
        if( new_tier < guy.npc_lod_tier ) {
            guy.npc_lod_tier     = new_tier;
            guy.npc_lod_cooldown = 0;
        } else if( new_tier > guy.npc_lod_tier && guy.npc_lod_cooldown <= 0 ) {
            guy.npc_lod_tier     = new_tier;
            guy.npc_lod_cooldown = static_cast<int8_t>( demote_cd );
        }

        if( guy.npc_lod_cooldown > 0 ) {
            guy.npc_lod_cooldown--;
        }

        if( guy.npc_lod_tier == 0 ) {
            ++tier0_count;
        }
    }

    TracyPlot( "NPC LOD Tier 0 (Full)",  static_cast<int64_t>( tier0_count ) );
    return tier0_count;
}
```

##### A5b. Modify `npcmove()` for tier + budget

Replace the `for( npc &guy : g->all_npcs() )` loop body (~lines 6393–6454):

```cpp
    // Build distance-sorted eligible list for non-follower NPCs.
    // Followers are always processed; budget applies only to non-followers.
    const int tier0_count = npc_tier_assign_all();
    const auto player_pos = u.bub_pos();
    const int current_turn = to_turn<int>( calendar::turn );
    const int effective_budget = npc_action_budget > 0
        ? std::max( npc_action_budget, tier0_count )
        : std::numeric_limits<int>::max();

    // Separate followers (always process) from non-followers (budgeted).
    std::vector<npc *> always_process;
    std::vector<std::pair<int, npc *>> budgeted;
    for( npc &guy : g->all_npcs() ) {
        if( !guy.is_simulated() ) {
            continue;
        }
        if( guy.is_player_ally() || guy.is_following() ||
            guy.mission == NPC_MISSION_ACTIVITY ||
            guy.mission == NPC_MISSION_GUARD_ALLY ||
            guy.npc_lod_tier == 0 ) {
            always_process.push_back( &guy );
        } else {
            budgeted.emplace_back( lod_dist( guy.bub_pos(), player_pos ), &guy );
        }
    }

    // Sort budgeted NPCs by distance, cap at budget.
    if( static_cast<int>( budgeted.size() ) > effective_budget ) {
        std::nth_element( budgeted.begin(),
                          budgeted.begin() + effective_budget,
                          budgeted.end() );
        // Drain moves for deferred NPCs (same pattern as monster budget).
        for( auto &entry : budgeted |
             std::views::drop( static_cast<size_t>( effective_budget ) ) ) {
            entry.second->moves = 0;
        }
        budgeted.resize( effective_budget );
    }

    // Process all always-process NPCs + budgeted NPCs with tier-dependent AI.
    // Lua hooks and per-NPC processing loop remain as-is but flow through a lambda.
```

The full `npcmove()` body replacement follows this structure:
1. Call `npc_tier_assign_all()` before the loop
2. Split NPCs into `always_process` (followers + Tier 0) and `budgeted` (Tier 1+2)
3. Cap budgeted with `nth_element` + drain moves
4. In the process lambda:
   - **Tier 2:** `process_turn()` + call `move()` (returns early via macro step), no move loop
   - **Tier 0/1:** existing full processing unchanged

### Verification

| # | Check | How |
|---|-------|-----|
| 1 | Build green | `cmake --build <preset> --target cataclysm-bn-tiles cata_test-tiles` |
| 2 | Follower always processed | Companion NPC follows player and acts every turn regardless of `NPC_ACTION_BUDGET` |
| 3 | Budget defers distant NPCs | Load save with 10+ NPCs across distance; Tracy shows fewer `npc_move_iter` zones per turn |
| 4 | Tier 2 skips move loop | Distant non-follower NPC at dz=-3: Tracy shows `npc_process_turn` but no `npc_move_iter` |
| 5 | Same behavior with LOD disabled | `NPC_LOD_ENABLED=false` → all NPCs process exactly as before |
| 6 | Budget=0 = no cap | `NPC_ACTION_BUDGET=0` → all non-followers processed every turn (same as pre-patch) |
| 7 | Existing tests pass | All `[npc]` tests pass (test NPCs are typically within Tier 0 distance) |

---

## Phase B — NPC coarse AI stride + macro step (2–3 days)

### Root cause

Even after Phase A's budget cap, each Tier 1 NPC that enters the move loop still calls
`regen_ai_cache()` → `assess_danger()` which scans **ALL monsters** every move iteration.
For a Tier 1 NPC (mid-distance, non-combat role), this monster scan is wasted work —
the NPC is too far to fight anything near the player.

### Approach

- **Coarse danger stride:** `assess_danger()` skips the monster scan (lines 553–637) for
  Tier 1 NPCs on non-scan turns. Full scan runs every `NPC_COARSE_DANGER_INTERVAL` turns.
  Between scans, cached `ai_cache.danger`/`ai_cache.target` from the last full scan are reused.
- **Tier 2 macro step:** Tier 2 NPCs take a single wander/reposition step every
  `NPC_MACRO_INTERVAL` turns instead of full AI, preventing them from being completely frozen
  while saving ~99% of per-turn cost.

### Files to modify

| File | Change | Lines |
|------|--------|-------|
| `src/cached_options.h` | Add `npc_coarse_danger_interval`, `npc_macro_interval` | +2 |
| `src/cached_options.cpp` | Add defaults | +2 |
| `src/options.cpp` | Register options + setPrerequisite + cache_to_globals | ~6 insertions |
| `src/npcmove.cpp` | Stride monster scan in `assess_danger` for Tier 1; add macro step for Tier 2 | ~15 lines |
| `src/game.cpp` | Update Tier 2 handler in npcmove to call move() for macro step | ~5 lines |

### Step-by-step

#### Step B1 — cached_options.h

After `npc_action_budget`:

```cpp
extern int npc_coarse_danger_interval;
extern int npc_macro_interval;
```

#### Step B2 — cached_options.cpp

```cpp
int npc_coarse_danger_interval = 5;
int npc_macro_interval         = 3;
```

#### Step B3 — options.cpp

Inside the `lod_npc` option group (after `NPC_ACTION_BUDGET`):

```cpp
        add( "NPC_COARSE_DANGER_INTERVAL", page_id,
             translate_marker( "Coarse Danger Scan Interval" ),
             translate_marker( "How many turns between full monster-danger scans for Tier-1 (coarse) "
                               "NPCs.  At 1 they scan every turn (full fidelity); at 5 (default) they "
                               "scan only once every 5 turns, reusing cached danger between scans." ),
             1, 20, is_android ? 8 : 5 );
        add( "NPC_MACRO_INTERVAL", page_id,
             translate_marker( "Macro Step Interval" ),
             translate_marker( "How many turns between macro-steps for Tier-2 NPCs.  "
                               "At 1 they step every turn (disabled macro AI); at 3 (default) they "
                               "take a single reposition step once every 3 turns.  "
                               "Higher values reduce CPU cost for distant NPCs." ),
             1, 10, is_android ? 4 : 3 );
```

After the group's setPrerequisite block:

```cpp
    get_option( "NPC_COARSE_DANGER_INTERVAL" ).setPrerequisite( "NPC_LOD_ENABLED" );
    get_option( "NPC_MACRO_INTERVAL" ).setPrerequisite( "NPC_LOD_ENABLED" );
```

In `cache_to_globals()`:

```cpp
    npc_coarse_danger_interval = ::get_option<int>( "NPC_COARSE_DANGER_INTERVAL" );
    npc_macro_interval         = ::get_option<int>( "NPC_MACRO_INTERVAL" );
```

#### Step B4 — npcmove.cpp

##### B4a. Skip monster scan in `assess_danger()` for Tier 1 on non-scan turns

In `assess_danger()`, wrap the `assess_all_monsters` block (lines 553–637):

```cpp
    // Phase B: stride the monster scan for Tier-1 NPCs.
    // On non-scan turns, skip the entire monster loop — cached danger/target
    // from the last full scan is reused by the caller.
    const bool skip_monster_scan = npc_lod_tier == 1 &&
                                   npc_coarse_danger_interval > 1 &&
                                   to_turn<int>( calendar::turn ) % npc_coarse_danger_interval != 0;
    if( !skip_monster_scan ) {
        {
            ZoneScopedN( "assess_all_monsters" );
            // ... existing loop (lines 555-637) unchanged ...
        } // assess_all_monsters
    }
```

When the monster scan is skipped, `ai_cache` retains values from the last full scan:
- `ai_cache.target` still points to last known threat (or null)
- `ai_cache.danger` still has last known danger level
- `ai_cache.total_danger` still has last known total
- The threat map still has last known threat directions

These stale values are fine for a Tier 1 NPC because:
- The NPC is mid-distance and non-follower — player won't notice slightly outdated threat info
- The NPC still runs `process_turn()` and the non-scan parts of `assess_danger()`
  (NPC friend scan, player threat, fire scan)
- On the next scan turn, everything is fully refreshed

##### B4b. Add macro step to `npc::move()` for Tier 2

At the top of `npc::move()` (line 818), after the attitude maintenance:

```cpp
    // Tier 2 macro step: far NPCs take a simple wander step instead of full AI.
    if( npc_lod_tier == 2 && npc_macro_interval > 1 &&
        to_turn<int>( calendar::turn ) % npc_macro_interval != 0 ) {
        return; // Not a macro step turn — skip move() entirely.
    }
```

This early return avoids the entire `regen_ai_cache()` + decision tree + `execute_action()`
cost. On step turns, the full `move()` runs normally (which itself is infrequent at default 3).

#### Step B5 — game.cpp

##### B5a. Update Tier 2 processing in `npcmove()`

In the Tier 2 handler from Phase A5b, change from:

```cpp
        // Tier 2: process_turn only (no move loop).
        if( guy.npc_lod_tier == 2 ) {
            if( !guy.has_effect( effect_npc_suspend ) ) {
                guy.process_turn();
            }
            if( !guy.is_dead() ) {
                guy.npc_update_body();
            }
            return;
        }
```

To:

```cpp
        // Tier 2: process_turn + occasional macro step.
        if( guy.npc_lod_tier == 2 ) {
            {
                ZoneScopedN( "npc_process_turn" );
                if( !guy.has_effect( effect_npc_suspend ) ) {
                    guy.process_turn();
                }
            }
            // Macro step (inside move()) runs every NPC_MACRO_INTERVAL turns.
            // On non-step turns, move() returns immediately.
            if( !guy.is_dead() && guy.moves > 0 ) {
                guy.move();
            }
            if( !guy.is_dead() ) {
                guy.npc_update_body();
            }
            return;
        }
```

### Verification

| # | Check | How |
|---|-------|-----|
| 1 | Build green | `cmake --build <preset> --target cataclysm-bn-tiles cata_test-tiles` |
| 2 | Tier 1 skips monster scan | Trace zone `assess_all_monsters` disappears for Tier 1 NPCs on non-scan turns |
| 3 | Tier 1 full scan on interval | Every `NPC_COARSE_DANGER_INTERVAL` turns, `assess_all_monsters` runs for Tier 1 NPCs |
| 4 | Tier 2 macro step | Distant NPC moves every `NPC_MACRO_INTERVAL` turns instead of every turn |
| 5 | Interval=1 = no stride | `NPC_COARSE_DANGER_INTERVAL=1` = pre-patch behavior for all tiers |
| 6 | Existing NPC tests pass | All `[npc]` tests pass (test NPCs typically within Tier 0 distance) |

---

## Build and test commands

```sh
# Format
cmake --build build --target format

# Build
cmake --preset osx-arm-slim
cmake --build --preset osx-arm-slim --target cataclysm-bn-tiles cata_test-tiles

# Run NPC regression tests
./out/build/osx-arm-slim/tests/cata_test-tiles "[npc]"

# Run behavior tests
./out/build/osx-arm-slim/tests/cata_test-tiles "[behavior]"
```

---

## Risk register

| # | Risk | Phase | Impact | Likelihood | Mitigation |
|---|------|-------|--------|------------|------------|
| 1 | Follower accidentally budget-capped | A | Companion stops acting every turn | Low | `is_player_ally()` / `is_following()` exempt list; test validates |
| 2 | Tier 1 NPC ignores nearby threat | B | NPC doesn't react to monster 10 tiles away for N turns | Med | Cached danger from last full scan; NPC still runs fire scan + player threat + NPC friend scan every turn |
| 3 | Tier 2 NPC walks through danger | B | NPC takes macro step into fire/acid | Low | `process_turn` still runs effects (fire damage); macro step uses existing `move()` which avoids dangerous terrain |
| 4 | Budget drain causes starvation | A | Deferred NPCs never accumulate moves | Low | Same pattern as monster budget drain — `next_turn` not advanced, guaranteed to run next turn |
| 5 | cooldown prevents timely promotion | A | NPC runs coarse AI when it should be full AI | Med | Cooldown is bounded (default 3 turns); 0 disables |
| 6 | `npc_lod_tier` not persisted | A | Tier resets on save/load | None | Intentional — same as monster `lod_tier`; rebuilt every turn |

---

## Open questions

1. **Should hostile NPCs (enemy bandits, etc.) be exempt from budget?**
   They already get Tier 0 when within `sight_radius` of player. Outside sight range,
   coarse/macro AI is fine — the player isn't watching them fight.

2. **Is `g_max_view_distance` good enough for the sight_radius check?**
   It over-approximates visibility (marks NPCs as "visible" when behind a wall), which is
   conservative and safe. Avoiding `sees()` per NPC is the whole point.

3. **Should `npc_lod_tier` be clamped during `sleep_skip_npc_process()`?**
   That function already forces all NPCs to sleep. The LOD system is orthogonal — when
   NPCs wake, their tier is recalculated next turn.
