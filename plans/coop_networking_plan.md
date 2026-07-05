# Co-op Networking Plan

**Status:** Track A complete (A1–A5.4). B1–B4 Phase 1 complete. B3 Phases 1–4 complete. B4 Phase 2 (resolve_hit/emit_visuals separation) in progress. Track C: C1 complete (PICKUP manifest). C2 complete (DROP, TERRAIN_CHANGE for doors, smash bash with debris). C2 construction deferral: build completions not yet wired — items consumed/produced during construction not propagated; next resync will correct terrain but not item state.
**Goal:** Real-time 2-player co-op where the client plays their own character with full action parity to single-player — pick up items, go upstairs, craft, interact, everything the host can do. Server-authoritative world state; client-side prediction for responsiveness.

---

## Current State Audit


What **exists** vs what is **not yet implemented** (re-baselined against actual code 2026-07-04):

| System | Status | Notes |
|---|---|---|
| 1 Hz accumulator main loop | ✅ done | `main.cpp:625` `IDLE_TICK_INTERVAL_MS = 1000.0` |
| Proxy NPC on host | ✅ done | `spawn_proxy_npc()`, tagged `COOP_PROXY_CHAR_ID` |
| Full submap blast sync | ✅ done | `build_and_send_sync()` — 5×5 grid, periodic/on-shift |
| Monster stable-ID delta sync | ✅ done | `monster_id_map_`, pointer-keyed |
| Client position reconciliation | ✅ done | ±5-tile drift snap in `apply_sync`; upgraded to seq-based replay by A2 |
| `both_idle()` fast-forward | ✅ done | reads `client_is_idle_` atomic |
| `client_status` idle packet | ✅ done | sent every tick from client |
| Fiber system for host modals | ✅ done | `coop_fiber.h/cpp`, minicoro-backed |
| Input-driven tick (A1) | ✅ done | `main.cpp:631–703`: coalescing window + idle floor |
| Coalescing window (A1) | ✅ done | `COALESCE_WINDOW_MS = 16.0`; resets idle accumulator |
| World transfer on join (A5.1) | ✅ done | `start_host`/`start_join` fully implemented; forced sync on join |
| `seq` on actions (A2) | ✅ done | `pending_actions_` ring buffer; `next_seq_`; sent with each action |
| Ring-buffer reconciliation (A2) | ✅ done | `coop_reconcile_pos()` deferred reconciliation; wall-flicker gate in `handle_action.cpp` |
| Proxy action drain (A2 bugfix) | ✅ done | was `while(moves>0)` — proxy never gets moves from `post_action_world_step` (npcmove zeroes them); replaced with one-action-per-tick unconditional drain |
| Proxy movement fidelity (A2 bugfix) | ✅ done | was `move_to()` (pathfinds, stumbles diagonally) → `setpos()` (exact client-authoritative placement) |
| npcmove contract fix | ✅ done | `is_coop_remote` early return lacked `set_moves(0)`; game loop spun 10×, fired reboot/teleport; fixed per npcmove.cpp:790 contract |
| WorldMutationLog (A3) | ✅ done | all 8 hooks confirmed: submap ter/furn, Creature::setpos, monster/npc/Character die, map::add_item, sub_add_field, field_changed/expired in process_fields |
| Delta event stream (A4) | ✅ done | A4a terrain+furniture+field delta; A4b hash resync: client computes FNV-1a, sends `resync_request` on mismatch; server `force_resync_` → forced full sync |
| Activity yield cap (A5.2) | ✅ done | `game::execute_activity_fixed_window_skip`: sync every 10 skipped turns |
| Ranged lag compensation (A5.3) | ✅ done | `resolve_fire_at_seq(proxy,seq,tx,ty,tz)`: snapshot lookup → creature reposition → `fire_gun` → restore; creature_moved filtered from delta stream so temp moves invisible to client |
| FIRE execution on proxy | ✅ done | `ranged::fire_gun(*proxy, target_bub)` with lag-comp snapshot; unarmed proxy consumes action |
| PICKUP on proxy | ❌ deferred | phase 9 note in execute_client_action |
| Vertical move on proxy | ❌ deferred | phase 10 note |
| SLEEP/CRAFT relay | ✅ done (functional) | moves consumed; `client_is_idle_` drives `both_idle()`; proxy animation cosmetic |
---

## Test Infrastructure (built 2026-07-04)

All COOP-enabled code now has CI coverage. The test suite runs in the `coop_tests` CI job (`CMakePresets.json`: `ci-coop` preset, `COOP=ON`).

| File | Tag | Description |
|---|---|---|
| `src/coop_packets.h/cpp` | — | Pure packet builders/parsers (`world_seed`, `action`, `sync` header). Architectural seam: decouples JSON protocol from `g→` state. Used by A4 delta stream. |
| `src/coop_reconcile.h/cpp` | — | Pure `coop_reconcile_pos()`. Deferred reconciliation: discard confirmed → reset to server pos → replay pending. |
| `src/coop_mutation_log.h/cpp` | — | Per-tick world mutation event buffer (A3). Thread-local singleton; null in SP = zero overhead. FNV-1a rolling hash for A4 integrity check. |
| `tests/coop_reconcile_test.cpp` | `[coop][reconcile]` | Direction convention anchored to `tripoint_north` etc. (non-circular). Operator+ round-trip. All 8 directions, boundary, fallback. |
| `tests/coop_packets_test.cpp` | `[coop][packets]` | `world_seed` / `action` round-trips. `parse_sync_header` extracts `last_seq`. Missing field → `-1`. Wrong type → `nullopt`. |
| `tests/coop_net_test.cpp` | `[coop][net]` | `coop_net::send`/`recv`/`poll` over real loopback socket. 4-byte BE framing. Multi-payload round-trip. Poll before/after send. |
| `tests/coop_integration_test.cpp` | `[.][coop_role_host]` `[.][coop_role_client]` | Two-process integration: separate `cata_test-tiles` instances, each with own `game* g`. Port-file coordination. Full handshake → world_seed → initial sync → action ticks → reconciliation. |
| `scripts/test_coop.ts` | — | Deno harness for the two-process test. Spawns both roles simultaneously; port-file polling handles synchronisation. |
| `.github/workflows/matrix.yml` | `coop_tests` job | Builds `ci-coop` (COOP=ON + `cataclysm-bn-tiles` + `cata_test-tiles`), runs `[coop]` tests, then Deno harness. |


## Architecture Summary

Three independent rates, explicitly separated:

| Rate | Mechanism | Notes |
|---|---|---|
| Render | `redraw_invalidated()` every outer loop iteration | Uncapped, unchanged |
| Active tick | Input-driven: fires immediately when either player acts | Coalescing window prevents double calendar advance |
| Idle floor | ~1 Hz accumulator | Keeps fire/monsters/weather alive. Runs during host menus (Option A) |

**Core principle:** Responsiveness comes from client-side prediction, NOT from cranking the world-sim rate. `calendar::turn` advances exactly once per `coop_game_tick()` call regardless of how many players acted — cranking tick rate causes monsters/fire/hunger to race at N× speed.

### The Coalescing Window

Either player's action opens a ~16ms window. Both players' inputs are collected. ONE `coop_game_tick()` fires when the window closes. Prevents calendar doubling when both players act simultaneously while keeping both players responsive.

```
Client presses W ──┐
                   ├── 16ms window ──► ONE coop_game_tick()
Host presses W ────┘                     calendar +1, monmove() once
```

**Why 16ms:** ~1 video frame at 60fps. Any two actions within one frame are "simultaneous" to a human. A coalescing tick resets the idle accumulator — the two paths are mutually exclusive.

**Rejected alternative — global turn gate:** Nobody acts until both players submit their action for the turn. Stalls world on any latency spike; host can't look around while waiting.

### Client Simulation Split

- **Client owns:** `process_turn()` for its own avatar (hunger, effects, stamina, sleep healing)
- **Server owns:** everything else — monster positions, combat outcomes, terrain, item pickups, move validity

Full GGPO rollback requires deterministic sim across 500+ interdependent systems. That is a multi-year prerequisite, not a co-op feature. **Do not pursue.**

### Fast-Forward Cap

`MAX_CATCH_UP = 3` ticks per outer-loop iteration on host. Client caps at 3 `process_turn()` calls in `apply_sync()`. **These must always match.** If raised, raise both together. At 1 Hz floor this yields ~3 game-minutes/real-second during sleep — fast but visible.

### What is and is NOT a co-op prerequisite

Track A (co-op) depends only on Step A3 (WorldMutationLog) for full desync safety. Steps B1–B5 (command pattern rework) are code-health improvements that make co-op cleaner but do NOT block delivery.

---

## Architectural Decisions (resolved)

| Decision | Resolution | Rationale |
|---|---|---|
| Transport: TCP only vs TCP+UDP | TCP only for Track A/B. UDP for positional prediction is Track C. | Coalescing window (16ms) absorbs TCP variability. WAN jitter deferred. |
| Client simulation depth | Thin client for Track A (client owns only process_turn()). **Track C target: full parity** — client runs own character with all actions, proxy mirrors on host. | Full rollback deferred; per-action authoritative sync achieves parity without deterministic sim. |
| Fast-forward tick rate | `MAX_CATCH_UP = 3`, matched on both host and client. | Prevents blocking render loop; must be raised symmetrically if at all. |
| 2-player vs N-player | Hardcode 2-player for Track A/B, extensible data structures now. | `client_sock_` → `clients_[0].sock`; adding client N is iteration, not surgery. |
| Client persistence | **Revised**: client has own character + save file (Track C). | Track A used guest session (Option A). Track C promotes client to full participant. |
| Host modal behavior | Option A: world stays alive, host character stands idle/vulnerable. | Fiber system already implements this. Option B (pause world) is bad UX. |

---

## Recommended Implementation Order

| Step | Depends on | Effort | What it unlocks |
|---|---|---|---|
| ~~A5.1: world transfer~~ | ✅ done | — | — |
| ~~A1: input-driven tick~~ | ✅ done | — | — |
| ~~A2: seq + ring buffer~~ | ✅ done | — | — |
| **A3: mutation log hooks** | nothing (additive) | 1 week | events exist; lands as own PR in parallel with A2 |
| **A4: delta stream** | A3 | 3 days | efficient sync; replaces full-submap blast |
| **A5.2: activity yield cap** | nothing | 1 day | sleep/craft don't lock out client |
| **A5.3: ranged lag comp** | A2 | 2 days | shooting resolves correctly |
| **B1–B5** | each other | 8–10 weeks | clean architecture; not co-op critical |

**Minimum viable co-op (playable end-to-end):** ✅ A2 complete  
**Desync-safe co-op:** add A3 + A4  
**Complete co-op:** add A5.2 + A5.3

---

## Track A — Co-op Delivery

### Step A5.1 — World Transfer on Join *(done)*
**Status:** ✅ Fully implemented. `start_host()` calls `send_world_seed`, `spawn_proxy_npc`, `send_initial_sync`; `start_join()` calls `receive_world_seed`, `apply_world_seed_to_avatar`. No stub remains.
**Files:** `src/coop_menu.cpp`, `src/coop_server.cpp`, `src/coop_client.cpp`

*(Stale planning notes removed — A5.1 and A1 are fully implemented in the codebase.)*

---

### Step A1 — Main Loop Rework *(done)*
**Status:** ✅ Implemented at `main.cpp:621–703`. Coalescing window (`COALESCE_WINDOW_MS = 16.0`) + idle floor (`IDLE_TICK_INTERVAL_MS = 1000.0`). Host input pushes directly to `pending_action_queue_`; client actions pending open the window; mutually exclusive paths. `both_idle()` fast-forward unchanged.

---


### Step A2 — Sequence Numbers + Ring-Buffer Reconciliation *(done)*
**Files:** `src/coop_client.cpp`, `src/coop_client.h`, `src/coop_proto.h`, `src/coop_server.cpp`  
**Effort:** 2 days  
**Depends on:** A1 (actions now fire at input rate; ring buffer needs to handle bursts)

**Why crude ±5-tile snap is insufficient:** Without seq, when the server says "proxy is at (10,5)", the client doesn't know which action that corresponds to. Actions seq=48,49 may still be in-flight. With seq: "confirmed through seq=47, proxy at (10,5)" → replay seq=48,49 on top of (10,5).

**Wire format additions (JSON, backward compatible):**

Action packet gains `seq`:
```json
{ "t": 11, "d": { "seq": 47, "key": "MOVE_N", "ctx": "" } }
```

Sync packet gains `last_seq`:
```json
{ "t": 20, "turn": 1234, "last_seq": 47, "tiles": [...], "monsters": [...], ... }
```

**Client ring buffer** (convert existing `action_q_` in `coop_client.h:40`):
```cpp
struct pending_action {
    uint32_t seq;
    std::string key;
    std::string ctx_json;
};
std::deque<pending_action> pending_actions_; // replay buffer, not drain queue
uint32_t next_seq_ = 1;
```

Actions are pushed with incrementing `seq` and kept until `seq <= last_seq` confirmation arrives.

**Reconciliation on sync receive:**
1. Discard confirmed: erase all `pending_actions_` with seq ≤ `last_seq`
2. Reset position to server's `proxy_ax/ay/az`
3. Replay remaining pending actions in order — each re-runs `apply_predicted_action()` (tile collision only, no world sim)

**Ring buffer overflow:** cap at 32 entries (~500ms at 60fps). If overflow: drop oldest unconfirmed. Manifests as a teleport snap — rare, acceptable.

**What client predicts:** local player position only  
**What stays server-authoritative:** monster positions, combat outcomes, terrain changes, item pickups, move validity

**Move economy:** proxy drains actions while `proxy->moves > 0`. Natural move budget governs — no artificial cap. One `process_turn()` per `coop_game_tick()`.

- [ ] Add `next_seq_` counter to `coop_client`
- [ ] Stamp outgoing actions with `seq`; keep in `pending_actions_` deque until confirmed
- [ ] Server reads `seq` from action packet and echoes `last_seq` in every sync
- [ ] Client reconciliation: discard confirmed → reset pos → replay pending
- [ ] Replace ±5-tile crude snap with seq-based replay (keep snap as emergency fallback for > 20 tile drift)
- [ ] Combat: client predicts animation only (swing, muzzle flash), waits for server for outcomes

---

### Step A3 — WorldMutationLog Foundation
**Files:** new `src/coop_mutation_log.h`, `src/submap.cpp`, `src/creature.cpp`, `src/monster.cpp`, `src/npc.cpp`, `src/character.cpp`, `src/map_field.cpp`  
**Effort:** 1 week

Additive hooks at canonical mutation chokepoints. Nothing breaks. Log is a per-tick buffer cleared after each sync. Zero overhead in single-player (`current()` returns null).

**New header `src/coop_mutation_log.h`:**
```cpp
struct coop_world_event {
    coop_event_type type;
    tripoint_abs_ms pos;
    int value;       // ter_id, field intensity, hp, etc.
    int creature_id; // for creature events
    std::string str; // mtype_id for creature_spawned; empty otherwise
};

struct coop_mutation_log {
    static auto current() -> coop_mutation_log*; // thread-local; null in SP = zero overhead
    auto push(coop_world_event e) -> void;
    auto flush() -> std::vector<coop_world_event>; // returns and clears buffer
    auto hash() const -> uint64_t;                 // running hash for integrity check
private:
    std::vector<coop_world_event> events_;
    uint64_t running_hash_ = 0;
};
```

**Thread safety:** all world simulation is single-threaded on the main thread. The IO thread (receiver_thread_) never touches the log. No mutex needed.

**Hook shapes:**

```cpp
// submap::set_ter() — 5 lines added:
if (auto* log = coop_mutation_log::current()) {
    log->push({ coop_event_type::terrain_changed, abs_ms_pos(p), new_ter.id().to_i() });
}

// Creature::setpos() — virtual, covers all callsites:
if (auto* log = coop_mutation_log::current()) {
    log->push({ coop_event_type::creature_moved, abs_pos(), creature_net_id() });
}
```

| Hook location | Event type | Position available | Effort |
|---|---|---|---|
| `submap::set_ter()` | `TerrainChanged` | `abs_sub_` + local point | 1 hour |
| `submap::set_furn()` | `FurnitureChanged` | same | 1 hour |
| `Creature::setpos()` virtual | `CreatureMoved` | argument | 1 hour |
| `monster::die()`, `npc::die()`, `Character::die()` | `CreatureDied` | member pos | 1 hour |
| `map::add_item()` | `ItemSpawned` | argument | 1 hour |
| `sub_add_field()` helper | `FieldCreated` | `SubTile` has pos | half day |
| `process_fields()` intensity/age loop | `FieldChanged`, `FieldExpired` | `pos`/`local` at call site | 2–3 days |

**Verified chokepoints (grep-confirmed):**
- `submap::set_ter()` — true single chokepoint. `map::ter_set()` calls it; `map_field.cpp` fire/fungus bypasses also call it directly. Raw `ter[][]` writes only in deserialization (covered by initial-join full sync) and `set_all_ter()` mapgen-only paths.
- `submap::set_furn()` — same pattern. `map_field.cpp:1249,1467` bypasses also route through it.
- `Creature::setpos()` — virtual dispatch covers all `game.cpp` callsites. No raw position-field mutations found.
- `monster::die()` `monster.cpp:2519`, `npc::die()` `npc.cpp:2647`, `Character::die()` `character.cpp:4302` — three clean virtual overrides.
- `map::add_field()` — canonical for field creation from outside `map_field.cpp`.
- `sub_add_field()` — internal helper used for all field-spread creation inside `process_fields()`.

**Field position threading problem (the 2–3 day item):**  
`set_field_intensity()` / `set_field_age()` signatures carry no position — 30+ callsites in `map_field.cpp`. The position is available in the `process_fields()` caller loop at `pos`/`local`. Fix: add a position-aware overload or thread position via a context parameter through the call chain. Do not add raw `intensity =` bypasses.

**Event types (add to `coop_proto.h`):**
```cpp
enum class coop_event_type : uint8_t {
    terrain_changed  = 1,
    furniture_changed= 2,
    creature_moved   = 3,
    creature_died    = 4,
    creature_spawned = 5,
    creature_hp      = 6,
    item_spawned     = 7,
    item_removed     = 8,
    field_created    = 9,
    field_changed    = 10,
    field_expired    = 11,
    turn_advanced    = 12,
};
```

**Integrity hash:** per-tick hash of all events in the buffer. Client compares locally; mismatch triggers a `resync_request` packet (new `coop_pkt::resync_request = 25`). This backstops any missed event paths and replaces the 60s safety net as the primary desync detector.

- [ ] Add `src/coop_mutation_log.h` and `.cpp`
- [ ] Add `coop_pkt::resync_request = 25` to `coop_proto.h`
- [ ] Hook `submap::set_ter()` and `submap::set_furn()`
- [ ] Hook `Creature::setpos()` virtual
- [ ] Hook `monster::die()`, `npc::die()`, `Character::die()`
- [ ] Hook `map::add_item()`
- [ ] Hook `sub_add_field()`
- [ ] Thread position through `process_fields()` into `set_field_intensity()` / `set_field_age()`

---

### Step A4 — Delta Event Stream + Integrity Hash
**Files:** `src/coop_server.cpp`, `src/coop_client.cpp`  
**Effort:** 3 days  
**Depends on:** A3

**Before A4 — current `build_and_send_sync()`:**  
Serializes up to 25 submaps × ~4KB each = up to 100KB per tick. At input-driven rates this is 1-2 MB/sec of mostly-unchanged terrain data.

**After A4 — proposed `build_and_send_delta()`:**
```json
{
  "t": 20, "turn": 1234, "last_seq": 47,
  "hash": 3829471928,
  "events": [
    { "ev": 1, "x": 1000, "y": 500, "z": 0, "v": 142 },
    { "ev": 3, "x": 1002, "y": 497, "z": 0, "cid": 15 },
    { "ev": 4, "x": 1003, "y": 499, "z": 0, "cid": 8 }
  ],
  "monsters": [...],
  "proxy_ax": 1000, "proxy_ay": 500, "proxy_az": 0,
  "host_ax": 998, "host_ay": 502, "host_az": 0
}
```

Typical size: < 2KB/tick. Integers only — no submap JSON.

**Client event application:**
```cpp
for (const auto& ev : events) {
    switch (ev.type) {
        case terrain_changed:  g->m.ter_set(bub_from_abs(ev.pos), ter_id(ev.value)); break;
        case creature_moved:   /* update tracked creature pos */; break;
        case creature_died:    g->despawn_monster(*find_by_id(ev.creature_id)); break;
        // ...
    }
}
```

**Full submap sync triggers (demoted to three cases):**
1. Initial join (A5 world transfer)
2. `resync_request` packet received — client detected hash mismatch
3. 30-second safety net (`TILE_RESYNC_INTERVAL = 30`, already exists)

- [ ] Replace `build_and_send_sync()` full-submap blast with `build_and_send_delta()` that flushes mutation log
- [ ] Add `hash` field to sync packet — log's rolling hash
- [ ] Client: after applying events, compute local hash; send `resync_request` on mismatch
- [ ] Server: handle `resync_request` → call forced `build_and_send_sync(force_full_=true)`
- [ ] Full submap sync demoted to: initial join, hash mismatch, 30s safety-net fallback
- [ ] Typical packet size target: < 2KB/tick vs 10–100KB+ for submap blasts

---

### Step A5 — Remaining Gaps
**Files:** `src/coop_server.cpp`, `src/activity_actor.cpp`, `src/coop_menu.cpp`  
**Effort:** ~1 week total (split below)

#### A5.2 — Activity Yield Cap (1 day)

**Problem:** If host goes to sleep (8-hour sleep activity), `try_activity_fixed_window_skip()` fast-forwards through all 480 turns in a burst. Client is locked out while the host's sleep resolves in a wall-clock instant.

**Fix:** `activity_actor` base class yields every N turns maximum (proposed N=10). Each yield fires a sync + brief pause. Sleep feels fast; client catches up 10 turns per yield.

- [ ] Add `turns_since_yield_` counter to `activity_actor` base
- [ ] Call `coop_yield()` (no-op in SP, fires sync+sleep in co-op) every 10 turns in `do_turn()` virtual
- [ ] Verify: host sleep at 1-Hz floor advances at ~3 game-min/real-sec; client catches up each yield

#### A5.3 — Ranged Lag Compensation (2 days, depends on A2)

**Problem:** Client fires at a zombie. Action packet takes 50ms to reach server. In 50ms the zombie moved. Server resolves hit against current position — wrong.

**Fix:** Server maintains a rolling history of entity positions keyed by `seq` (~10 ticks = kilobytes). FIRE actions carry `seq=N`; server resolves hit against positions at tick N.

```cpp
struct entity_snapshot {
    uint32_t seq;
    std::vector<std::pair<int, tripoint_abs_ms>> creature_positions; // creature_id → pos
};
std::deque<entity_snapshot> position_history_; // ~10 entries; pop when > window
```

Cost: 10 snapshots × 50 creatures × 16 bytes = ~8KB. Negligible.

On each tick: push snapshot. On FIRE with seq=N: find snapshot ≤ N, resolve trajectory against those positions.

- [ ] Add `position_history_` rolling deque to `coop_server`
- [ ] Push snapshot each tick before draining client actions
- [ ] `execute_client_action()` FIRE case: look up snapshot at action's seq, resolve trajectory
- [ ] Pop snapshots older than 10 ticks

#### A5.4 — `both_idle()` fast-forward verification (half day)

Already fixed in current code (reads `client_is_idle_` atomic). Verify end-to-end:
- [ ] Host sleeps, client sleeps → fast-forward fires, both advance together
- [ ] Host sleeps, client active → fast-forward does NOT fire
- [ ] Fast-forward limited to `MAX_CATCH_UP = 3`; client `process_turn()` catch-up also capped at 3

---

## Track B — Codebase Rework (parallel, ~8–10 weeks)

Not a co-op prerequisite. Runs alongside or after Track A ships. Each step independently mergeable.

**Core pattern:** separate intent from execution. AI/input decides *what* → produces a command object → pipeline validates + executes + emits events.

```
Current:  AI decides → immediately mutates world → nobody notified
Target:   AI decides → Command → validate → execute → emit → observers
```

Once B1–B4 exist, the WorldMutationLog hooks (A3) become redundant — events flow naturally from the command executor. The hooks remain as a compatibility layer until B3 (player commands) is complete.

### Step B1 — Monster Command Pattern
**Files:** `src/monmove.cpp`, new `src/monster_cmd.h`  
**Effort:** 1 week

```cpp
struct monster_cmd {
    enum class type { move, melee, ranged, flee, wait, special } kind;
    tripoint_bub_ms target_pos;
    Creature* target_creature;
    std::string special_id;
};

auto monster::decide_action(const game_context& ctx) -> monster_cmd; // pure, no mutation
auto execute_monster_cmd(monster& mon, const monster_cmd& cmd) -> void; // emits events
```

- [ ] Monster AI produces `MonsterCmd` (move, melee, flee, wait, special)
- [ ] Separate `execute(monster&, MonsterCmd)` function — all world mutation happens here
- [ ] Mutation log gets events naturally through execution (no bolted-on hooks needed)
- [ ] AI logic and execution logic independently testable

### Step B2 — NPC Command Pattern
**Files:** `src/npcmove.cpp`, new `src/npc_cmd.h`  
**Effort:** 2 weeks

- [ ] Same pattern as B1, more complex AI tree
- [ ] NPC pathfinding / combat / activity / talk all produce commands
- [ ] Fixes the "NPC logic is horrendous to work in" complaint

### Step B3 — Player Action Command Factory
**Files:** `src/handle_action.cpp`, new `src/player_cmd.h`  
**Effort:** 2 weeks

- [ ] `handle_action.cpp` giant switch → command factory returning `std::variant<MoveCmd, MeleeCmd, FireCmd, UseItemCmd, ...>`
- [ ] Simulation pipeline validates and executes
- [ ] Enables action replay, deterministic testing

### Step B4 — Ranged Combat Stage Split
**Files:** `src/ranged.cpp`, new `src/fire_cmd.h`  
**Effort:** 1 week

Separates ranged into three explicit stages:
1. `resolve_trajectory()` — pure, no side effects; client can run this for visual prediction
2. `resolve_hit(trajectory, world_state)` — server only, authoritative
3. `emit_visuals(trajectory, hit_result)` — both sides, no game state

- [ ] Client runs stages 1 + 3 immediately for feel
- [ ] Server runs stages 1 + 2 + 3 authoritatively
- [ ] Lag compensation history (from A5) plugs into stage 2

### Step B5 — `game.cpp` Decomposition
**Files:** `src/game.cpp` (16,000+ lines)  
**Effort:** 1–2 weeks, follows naturally from B1–B4

Once command/pipeline split exists, `game.cpp` decomposes into:
- `src/sim_pipeline.cpp` — validate/execute/emit
- `src/action_dispatch.cpp` — player input → command
- `src/world_tick.cpp` — per-turn world sim
- `src/game.cpp` — top-level loop and game state only

---

## Track C — Full Client Parity (client plays own character like single-player)

**Goal:** Client plays their own persistent character with full SP action parity.

### Architecture decision: Option B (client avatar authoritative)

| | Option A: Teach NPC proxy all avatar actions | Option B: Client avatar authoritative |
|---|---|---|
| Model | Proxy NPC gains pickup/stairs/craft/etc. | Client's `g->u` runs locally; host reconciles world + character state |
| Backlog | Reimplement every avatar action on NPCs (endless) | World-mutation propagation + client→host character delta sync |
| Proxy role | Full avatar mirror | Collision/visibility placeholder only |
| **Chosen?** | ❌ | ✅ |

**Option B is consistent with Track A's existing infrastructure:**
- WorldMutationLog (A3) + delta stream (A4) already propagate HOST→CLIENT world changes
- Option B adds CLIENT→HOST mutations (item pickup, door open, terrain mod from client)
- Client's character state (inventory, HP, skills) synced CLIENT→HOST each tick (new direction)
- Proxy NPC never needs `npc::pickup`, NPC stair-nav, or NPC crafting

**What the proxy DOES still need (minimal):** position mirroring (done), collision placeholder,
visibility calculation for the host's LOS system.

### Track C work queue (Option B specific)

| Step | What it is | What it is NOT | Effort |
|---|---|---|---|
| **C1: Client→host item mutations** | Client picks up item locally (already works); sends item IDs + positions to host; host removes from its map | NOT npc::pickup | 1 week |
| **C2: Client→host terrain mutations** ✅ | Door opens (TERRAIN_CHANGE), items dropped (DROP manifest), smash bash (terrain + debris). Construction completion: deferred — `complete_construction()` hook needed for build-finish terrain and item changes. | NOT npc::interact | 2 weeks |
| **C3: Client→host character delta** | Client sends inventory/stat/effect changes to host each tick (new sync direction) | NOT host-authoritative character | 2 weeks |
| **C4: Vertical movement** | Client z-changes → host updates proxy z + triggers tile sync for new level | NOT NPC stair navigation | 1 week |
| **C5: Long activity sync** | When client starts crafting/sleeping, host fast-forwards appropriately (extends A5.2) | NOT NPC activity actors | 2 weeks |
| **C6: Client character persistence** | Client saves own character file; host loads on join; rejoining restores state | NOT host stores client char | 3 weeks |

**Immediate next step: C1** — item pickup is the most visible blocked action and the
simplest world-mutation propagation case. The pattern established in C1 extends to C2.

**Note:** B4 Phase 2 (ranged stage split) does NOT advance Track C.

## Known Anti-Patterns (do not repeat)

| Anti-pattern | Source | Problem |
|---|---|---|
| Global turn gate | CoQ community | Stalls world on any latency spike |
| Client-side monster authority | Project Zomboid zombie-host | Desync, teleporting enemies, host framerate affects all players |
| Full GGPO rollback | — | Requires deterministic sim (months of prerequisite); 10MB state × 60fps = impractical |
| Cranking idle floor to 20 Hz | — | `calendar::turn` advances 20× wall-clock; fire/hunger/bleed race |
| Draining N proxy actions per tick with moves refresh | — | Proxy moves N tiles while monsters move 1; breaks move economy |
| Full submap blast every tick | — | 10–100KB/tick, blocks higher tick rates |
| `proxy->activity` check in `both_idle()` | Current code | Always false (CRAFT/SLEEP stubbed); **fixed** — now reads `client_is_idle_` atomic |
| Mismatched fast-forward caps | — | Host advances 10 turns, client process_turn capped at 3 → avatar stat desync |
| Raising `MAX_CATCH_UP` on one side only | — | See above; always raise host and client caps together |

---

## Research Sources

- Gabriel Gambetta, "Fast-Paced Multiplayer" parts 1–4: https://www.gabrielgambetta.com/client-server-game-architecture.html
- Valve / Yahn Bernier, "Latency Compensating Methods" (2001): https://developer.valvesoftware.com/wiki/Latency_Compensating_Methods_in_Client/Server_In-game_Protocol_Design_and_Optimization
- Valve, "Source Multiplayer Networking": https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking
- GDC 2021, "Breaking the Ankh: Deterministic Propagation Netcode" (Spelunky 2): https://www.gdcvault.com/play/1027358
- Project Zomboid Build 42 netcode rewrite (community documentation, 2025)
- DCSS WebTiles architecture: https://crawl.develz.org
- GGPO documentation: https://www.ggpo.net
- KI Infil, "Rollback Netcode": https://ki.infil.net/w02-netcode.html

---

## Dependency Graph

```
A5.1 (world transfer on join) ──────────────────────► client can join and see world

A1 (input-driven tick + coalesce window)
  └─► A2 (seq + ring-buffer reconciliation)
        └─► A5.3 (ranged lag compensation) ─────────► combat works correctly

A3 (WorldMutationLog hooks)          ← can land in parallel with A1/A2 as own PR
  └─► A4 (delta stream + hash) ───────────────────► efficient + desync-safe sync

A5.2 (activity yield cap) ──────────────────────────► sleep/craft don't lock out client
A5.4 (both_idle verification) ──────────────────────► fast-forward works end-to-end

B1 (monster commands)
  └─► B2 (NPC commands)
        └─► B3 (player action commands)
              └─► B4 (ranged split)
                    └─► B5 (game.cpp decomposition)

B1–B5 improve co-op coverage but do NOT block Track A delivery.
Once B3 is complete, A3 mutation log hooks become redundant (events flow from commands).

MILESTONES:
  A5.1 + A1 + A2             = minimum viable co-op (playable end-to-end)
  + A3 + A4                  = desync-safe co-op
  + A5.2 + A5.3 + A5.4       = complete co-op
  + B1–B5                    = clean architecture
```
