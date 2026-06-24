# Rework Plan — Finish the Activity → `activity_actor` Migration

## Context

A system-review of the gameplay/engine core (rendering/lighting/UI are already being reworked
separately) ranked the **half-finished activity-system migration** as the best-ROI rework: it is
*Critical* in impact but *bounded* in scope — it finishes a migration the maintainers already chose,
rather than opening a new redesign.

**The problem.** Two activity dispatch systems run side by side. Every turn the engine branches:

```cpp
// src/player_activity.cpp  (do_turn, ~line 556; finish ~line 597; canceled similar)
if( actor ) { actor->do_turn( *this, p ); }
else        { type->call_do_turn( this, &p ); }   // legacy function-pointer map
```

The legacy side is two `std::map<activity_id, std::function<...>>` tables — `do_turn_functions` and
`finish_functions` (`src/activity_handlers.cpp:257` / `:320`). Consequences:

- **Dual mental model / dual bug surface** — a fix may need to land in both an actor and a legacy
  handler.
- **`repair_activity_hack`** — a documented stopgap (8 refs in `activity_handlers.cpp`, e.g. `:2446`,
  namespace `:2482`) that fakes items from vehicles/furniture *"until `repair_activity_actor` would be
  implemented"*, tracked by **GitHub issue #1612**.
- **Save-format scar** — old saves whose activity type now expects an actor are **force-cancelled**
  on load (`ACT_MIGRATION_CANCEL`, `savegame_json.cpp:239-282`): data loss, not migration.

**Definition of done.** `do_turn_functions` / `finish_functions` emptied and deleted; `call_do_turn` /
`call_finish` removed; the `if(actor) else legacy` branches deleted; `repair_activity_hack` gone (#1612
closed); `activity_type::load` validation updated to require an actor.

## Current state (recon)

- **157** `ACT_*` ids exist total; **21 already migrated** to actors (aim, build/construction, craft,
  dig, dig_channel, disassemble, drop, hacking, hacksaw, lockpick, longsalvage, move_items, oxytorch,
  pickup, stash, throw, toggle_gate, boltcutting, assist, autodrive, migration_cancel).
- **64 still on the legacy path** (unique `ACT_*` ids across 42 `do_turn` + 44 `finish` map entries).
  Many are trivial; a handful are genuinely complex (multi/zone, vehicle).
- Actors are registered in the `deserialize_functions` table at `src/activity_actor.cpp:2602`.

## Critical architectural fact (drives the whole recipe)

**There is no runtime `activity_id → actor` factory.** `deserialize_functions` (`activity_actor.cpp:2602`)
maps ids to `::deserialize` *for save-loading only*. At runtime an activity becomes an actor **only** if a
caller explicitly constructs the actor and passes it to `assign_activity(unique_ptr<player_activity>)`
(`character.cpp:10304`). The other overload — `assign_activity(activity_id, moves, index, pos, str)`
(`character.cpp:10298`) — builds an **actor-less** `player_activity` carrying the legacy `values`/
`str_values`/`coords` bags, which then hits the legacy `call_do_turn` branch.

Consequences that shape everything below:
- There are **~123 id-based `assign_activity(ACT_*)` / `player_activity(ACT_*)` call sites**. Migrating an
  id means converting **every** caller of that id to the actor overload.
- **Deleting a legacy handler is gated on converting *all* its call sites first.** Miss one and, post-
  delete, that activity silently no-ops (or `debugmsg`s) at runtime — not a compile error.
- **Lua binds the legacy id overload** (`assign_activity(activity_id,int,int,int,string)`,
  `catalua_bindings_creature.cpp:938`). Lua-assigned instances of a migrated id break unless shimmed
  (see open decision).
- **Legacy-parent → migrated-child trap (confirmed real):** `activity_item_handling.cpp:2323/2347/2670`
  assign ACT_BUTCHER_FULL (wave 2), ACT_CHOP_PLANKS/TREE/LOGS (wave 5) **by id** from zone/multi routines
  (wave 8). So a child migrated early still has a legacy caller alive until its parent's wave.
  **Resolution — deletion is data-gated, not wave-gated:** add+register the actor and convert callers as
  you reach them, but delete a legacy handler *only when* `rg "assign_activity\(\s*ACT_FOO|player_activity\(\s*ACT_FOO"`
  returns zero. A child may therefore keep its (now-unused-by-direct-callers) handler a few waves until
  its last parent caller is converted. The straggler guard (below) makes any premature deletion loud.
- **No separate legacy cancel hook exists** — there is no `canceled_functions` map; cancel cleanup (where
  any) lived inside `finish` or wasn't done. So actor `canceled()` is needed only in the rare cases where
  legacy code did teardown outside `finish`. Lowers per-activity burden.
- **`Character::cancel_activity()` has hardcoded activity-type special cases** (`character.cpp:10393-10411`)
  for `ACT_TRY_SLEEP` (removes sleep query) and `ACT_WAIT_STAMINA` (unwinds auto_resume backlog). These
  are effectively cancel-time teardown that lives outside any handler/actor. When those ids migrate, the
  special case must move into their actor's `canceled()`. Grep `character.cpp` and `player_activity.cpp`
  for `ACT_` branches when migrating any activity — type-punned cancel logic lives in unexpected places.

## The migration recipe (one pattern, applied repeatedly)

`boltcutting_activity_actor` (`activity_actor.cpp:1327-1400`, ~50 lines) is the clean template. For each
legacy activity:

1. **Add a subclass** in `src/activity_actor_definitions.h` implementing the pure virtuals from
   `activity_actor.h`: `get_type()`, `start()`, `do_turn()`, `finish()`, `serialize()` /
   `deserialize()`, `clone()`. Add `canceled()` only if the legacy path had cleanup. **All three of
   start/do_turn/finish are `= 0`** — a finish-only or do_turn-only legacy activity still needs trivial
   impls for the others (e.g. a do_turn that just drains `moves_left`).
2. **Port the body** from the matching `activity_handlers::*_do_turn` / `*_finish` into the actor.
   State smuggled in the deprecated bags becomes **typed members**, (de)serialized explicitly. The full
   bag surface (per `savegame_json.cpp:270-284`) is wider than just values/str_values — also `index`,
   `position`, `coords`, `coord_set`, `name`, `targets`, `placement`, `auto_resume`, **`monsters`**,
   **`tools_`**, **`assistants_ids_`**. The last three are the NPC/multi/crafting channels — wave 8
   activities lean on them, so audit which bag fields *this* activity actually uses and carry every one.
   Keep `moves_left` as the progress unit unless deliberately adopting `progress_counter` — mixing the
   two double-counts.
   **NPC fast-forward trap.** `npc::advance_job_progress()` (`npc.cpp:3210-3228`) directly decrements
   `activity->moves_left` to catch up out-of-bubble NPCs. An actor using `progress_counter` is invisible
   to this — the NPC silently never completes. Add a `fast_forward( int turns )` virtual on
   `activity_actor` (delegating to `progress_counter::mod_moves_left()` by default) and update
   `advance_job_progress` to call it on the actor path.
3. **Port resume semantics** — if the legacy activity relied on type+param matching to stack/auto-resume
   from the `backlog` (`character.cpp:10307` `can_resume_with`), implement `can_resume_with_internal`
   so queued/interrupted instances still resume.
4. **Find & convert ALL state touchpoints for this id** (the long pole, do exhaustively):
   - every `assign_activity(ACT_FOO, …)` and `player_activity(ACT_FOO, …)` construction site →
     actor overload;
   - every **external reader/writer** of that activity's bag slots (`act.values[n]` / `str_values` /
     `coords`) → the actor's typed members/accessors. There are **16 files** outside the activity core
     that poke these bags (incl. `npctalk_funcs.cpp`, `activity_item_handling.cpp`,
     `avatar_functions.cpp`, `game_inventory.cpp`, `iuse_actor.cpp`) — filter to the ones touching this id;
   - the **Lua** path for this id: grep in-repo `.lua` for `assign_activity( ACT_FOO` and convert those
     callers (decision A — no shim; out-of-repo mods using a migrated id are unsupported).
5. **Register** the actor in `deserialize_functions` (`activity_actor.cpp:2602`).
6. **Delete** the legacy `do_turn_functions` / `finish_functions` entries + free functions — **only after
   step 4 is provably exhaustive** (grep-zero gate, see verification).
7. **Verify** (see below) before moving on.

## Phased waves (simple → complex; each wave independently shippable & build-green)

| Wave | Activities | Why grouped |
|------|-----------|-------------|
| 1 — Terrain/timer warm-up | BURROW, PICKAXE, JACKHAMMER, CHURN, FILL_PIT, CLEAR_RUBBLE, HAND_CRANK, PRY_NAILS, FILL_LIQUID, PLANT_SEED, FERTILIZE_PLOT, FORAGE | Self-contained, near-identical to `dig`/`boltcutting`. Establishes the recipe + reviewer trust. |
| 2 — Butchery family | BUTCHER, BUTCHER_FULL, FIELD_DRESS, SKIN, QUARTER, DISMEMBER, DISSECT, BLEED | Share corpse logic → **one** `butchery_activity_actor` with a `butcher_type` param replaces 8 entries. |
| 3 — Item-action (**unblocks #1612**) | REPAIR_ITEM, MEND_ITEM, TOOLMOD_ADD, GUNMOD_ADD, RELOAD, WEAR, ARMOR_LAYERS | Implementing `repair_activity_actor` lets the whole `repair_activity_hack` namespace be deleted — the headline payoff. |
| 4 — Consume/menu | EAT_MENU, CONSUME_FOOD_MENU, CONSUME_DRINK_MENU, CONSUME_MEDS_MENU, FIRSTAID | UI-driven; share consume plumbing. |
| 5 — Wood | CHOP_TREE, CHOP_PLANKS, CHOP_LOGS | Small, related. |
| 6 — Misc/social/skill/spell + waits | READ, STUDY_SPELL, SPELLCASTING, TRAIN, TRAIN_SKILL, TRAIN_PET, PLAY_WITH_PET, MEDITATE, GAME, GENERIC_GAME, SOCIALIZE, VIBE, SHAVE, HAIRCUT, ATM, CRACKING, START_FIRE, FISH, ROBOT_CONTROL, OPERATION, MIND_SPLICER, MAKE_ZLAVE, MILK, FIND_MOUNT, TREE_COMMUNION, ADV_INVENTORY, WAIT, WAIT_NPC, WAIT_WEATHER, WAIT_STAMINA, TRAVELLING | Long tail of one-offs; mechanical once the recipe is muscle-memory. **Note:** WAIT_STAMINA and TRAVELLING are special-cased in `player_activity.cpp`'s do_turn stamina block — preserve that interaction; ADV_INVENTORY drives the advanced-inventory UI (it's a friend-class `game` consumer). |
| 7 — Vehicle | VEHICLE, VEHICLE_REPAIR, VEHICLE_DECONSTRUCTION, START_ENGINES, HOTWIRE_CAR | Reach into `vehicle` heavily; isolate so vehicle-system risk is contained. |
| 8 — Multi/zone/NPC | MULTIPLE_MINE/FISH/FARM/CONSTRUCTION/CHOP_TREES/CHOP_PLANKS/BUTCHER, MOVE_LOOT, TIDY_UP, FETCH_REQUIRED | Hardest: zone-manager + NPC-AI coupled, generate sub-activities. Do **last**, when the pattern is fully proven. |
| 9 — Rip-out | — | See below. |

### Straggler guard (add in Wave 1, remove in Wave 9)
Before deleting any handlers, make `call_do_turn` / `call_finish` emit a `debugmsg` (not a silent return)
when invoked for an id that *should* be an actor. This turns "missed a call site" from a silent in-game
no-op into a loud, testable failure during the whole transition. Removed when the legacy maps are deleted.

**Double-registration sweep.** `activity_type::check_consistency()` (`activity_type.cpp:173-209`) validates
that each id has *either* a legacy handler or an actor, but never warns if **both** exist. After each wave,
add a manual check: for every id in the wave, verify it isn't *also* still in the legacy maps post-migration
(`ACT_CRAFT` is the pre-existing orphan — `craft_do_turn` in `do_turn_functions` is dead code while the
`craft_activity_actor` handles runtime dispatch). Consider promoting this to a `debugmsg` in
`check_consistency()` once Wave 1 proves the pattern.

### Wave 9 — Rip-out (only after every id is migrated)
- Delete `do_turn_functions` / `finish_functions` and `activity_type::call_do_turn` / `call_finish`
  (`activity_handlers.cpp` + `activity_type.cpp:212+`).
- Delete the `if(actor) else …` branches in `player_activity.cpp` (do_turn / finish / canceled) — actor
  becomes mandatory.
- Update `activity_type::load` validation (`activity_type.cpp:180-183`) to require an actor and drop the
  `do_turn_functions.contains()` check.
- Remove the `repair_activity_hack` namespace and its `#include` (`activity_handlers.cpp:39-40`,
  `:2482-2666`).
- **Keep** the `ACT_MIGRATION_CANCEL` load-time fallback (`savegame_json.cpp:239-282`) as a safety net
  for pre-migration saves — but it no longer masks active dual-path code.

## Critical files
- `src/activity_actor_definitions.h` — new actor declarations (every wave).
- `src/activity_actor.cpp` — actor bodies + `deserialize_functions` registration (`:2602`).
- `src/activity_handlers.cpp` / `.h` — source of legacy bodies to port; shrinks to empty.
- `src/player_activity.cpp` / `.h` — dispatch branches removed in wave 9; deprecated state bags retire.
- `src/activity_type.cpp` — validation update in wave 9.
- `src/savegame_json.cpp` — confirm each actor round-trips; keep migration-cancel net.
- Construction-site callers per id (grep `ACT_<NAME>` across `src/`).

## Verification (per wave, not just at the end)
1. **Build green** — compile `cataclysm-bn-tiles`; confirm the *fresh* binary relinked (check mtime per
   the build-verify convention), not a stale copy.
2. **Grep-zero gate before any deletion** — for each id whose handler you delete:
   `rg "assign_activity\(\s*ACT_FOO|player_activity\(\s*ACT_FOO" src` must return zero (C++), and the Lua
   path must be resolved per the open decision. This is the hard gate that prevents silent no-ops.
3. **Straggler guard stays loud** — the `debugmsg` in `call_do_turn`/`call_finish` must not fire during
   any test run; if it does, a call site (or Lua) was missed.
4. **In-game eyeball** each migrated activity via the crash-repro harness
   (`lldb -b -o run -- out/build/osx-arm-slim/src/cataclysm-bn-tiles --world "Clara City"`): start it,
   watch progress, let it finish, confirm identical outcome to pre-migration. For NPC-capable activities,
   verify on an NPC too (multi/zone activities especially).
5. **Save/load round-trip mid-activity** — start, quicksave, quickload, confirm it resumes (proves the
   actor's `serialize`/`deserialize` carries the state the old bags used to) **and** that an interrupted
   instance auto-resumes from `backlog` (proves `can_resume_with_internal`).
6. **Regression tests** — run the existing `tests/player_activities_test.cpp`,
   `tests/crafting_test.cpp`, `tests/activity_fixed_window_test.cpp` each wave; add a save round-trip
   test for at least one migrated actor per wave.
7. **Wave 3 extra** — after deleting `repair_activity_hack`, repair an item *from a vehicle/furniture
   tile* (the exact case the hack existed for) and confirm it still works.

## Resolved decision — Lua compatibility: (A) Best-effort
**Lua mods can call** `assign_activity(activity_id, int, int, int, string)`
(`catalua_bindings_creature.cpp:938`), injecting state through the bags. Once an id is migrated, that path
produces an actor-less activity.

**Chosen: (A) Best-effort / break it.** For each migrated id: convert all in-tree callers (incl. any
in-repo Lua), and treat Lua-assigning a *migrated* activity by id as unsupported. The straggler guard
`debugmsg`s loudly if a mod does it, so breakage is visible, not silent. Rationale: this fork's mod
surface is small, and (A) avoids reintroducing the id→actor table we are deliberately removing.

Implications baked into the waves:
- The recipe's step-4 "Lua path" reduces to: grep in-repo `.lua` for `assign_activity( ACT_FOO` and
  convert those callers; no adapter/shim or new binding is built.
- The grep-zero deletion gate (verification §2) is **C++ only**; Lua misuse is caught at runtime by the
  straggler guard rather than blocking a handler deletion.
- **Document it once** in the modding/changelog notes: "Lua `assign_activity(id, …)` is for un-migrated
  legacy activities only; migrated activities must be driven through their gameplay entry points." Revisit
  with option (C) later only if a real mod need appears.

## Rough effort
Waves 1, 4, 5 are near-mechanical (~1–2 days each). Wave 2 (butchery, one actor for 8 ids) ~2–3 days.
Wave 3 (repair, unblocks #1612) ~3–4 days incl. the vehicle/furniture item plumbing the hack faked.
Wave 6 long tail ~1 week. Wave 7 (vehicle) ~3–4 days. Wave 8 (multi/zone/NPC) is the bulk — ~1–2 weeks,
highest regression risk. Wave 9 rip-out ~1–2 days once everything is green. Total ≈ 4–6 weeks, matching
the original "high but bounded" estimate.

## Sequencing note
Waves are ordered so the **recipe is proven on trivial activities (1) before the high-value blocker (3)
and the genuinely hard ones (7–8)**. Each wave is a self-contained PR: build-green, all legacy entries it
claims are deleted, dual-path branch untouched until wave 9. If scope must be cut, stopping after any
wave leaves a *working* game with strictly fewer legacy handlers than before.
