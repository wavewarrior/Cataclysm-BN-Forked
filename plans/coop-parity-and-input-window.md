# Co-op feature parity + dynamic input buffer window

## Context

Two deliverables in `C:/WORK/GIT_REPOS/Cataclysm-BN-Forked`:

1. **Parity** — bring the co-op client up to single-player feature level. The co-op client
   today runs *only* `g->u.process_turn()` per synced turn (`src/coop_client.cpp:951-954`),
   while single-player runs the whole of `game::post_action_world_step()`
   (`src/game_action.cpp:1828-2145`). As a direct consequence the client has: frozen weather,
   no day/night light-level updates, no body temperature / wetness simulation, no
   hunger-thirst-fatigue-healing tick, missions that never progress, crafting/reading
   activities that never advance, immunity to fields (fire/acid/smoke), no monster-spotted
   safe-mode alerts, no sound markers or sound-wave visualisation, no combat/vehicle SFX,
   and dead per-turn Lua mod hooks. End state: the client runs the avatar-local half of the
   turn step, so every one of those single-player features works for the client too, while
   map/creature/overmap authority stays with the host.

2. **Dynamic input buffer window** — the co-op main loop
   (`src/main.cpp:712-798`) pushes every resolved action into an *unbounded*
   `std::queue<std::string> pending_action_queue_` (`src/game.h:242`) and drains it at
   roughly one action per world tick (`while( g->u.moves > 0 )`, `src/main.cpp:773-784`;
   the host drains client actions strictly one-per-tick at `src/coop_server.cpp:649`).
   Mashing a key therefore banks N actions that keep firing long after the key is released.
   The coalescing window that is supposed to absorb this is a hardcoded
   `COALESCE_WINDOW_MS = 16.0` (`src/main.cpp:717`) — far shorter than the wall-clock cost of
   the tick that resolves an action. End state: the window is sized each frame from the
   measured tick cost of the *slower* of host and client, the action buffer is timestamped
   and depth-capped, and entries older than one window are discarded — with the invariant
   that **the most recent input is never dropped**.

The two phases are independent and may be implemented in either order.

## Approach

Step 0: copy this file to `plans/coop-parity-and-input-window.md` in the repo before
starting — `plans/` is the permanent record that survives session resets (AGENTS.md).

### Phase 1 — Dynamic input buffer window

Phase 1 steps are ordered; 1.1 has no dependencies, 1.2 depends on 1.1's constants,
1.3 depends on both.

#### Step 1.1 — New pure module `src/coop_input_window.{h,cpp}`

No existing equivalent was found: `src/coop_proto.h` holds only constants, and there is no
timestamped-queue or EWMA helper anywhere in `src/`. Create both files and add
`coop_input_window.cpp` nowhere explicitly — `src/CMakeLists.txt` globs `.cpp` with
`CONFIGURE_DEPENDS`, so it is picked up automatically.

`src/coop_input_window.h` — exact contents:

```cpp
#pragma once

#include <cstddef>
#include <deque>
#include <string>

/// One buffered player action awaiting execution in the co-op main loop.
struct buffered_action {
    std::string action;        ///< resolved action string, e.g. "move_n"
    double enqueued_ms = 0.0;  ///< steady-clock ms, same clock as coop_admit_action's now_ms
    /// False for actions that cannot change world state (menus, info screens, toggles,
    /// save/quit).  Set by the caller from can_action_change_worldstate(); such entries are
    /// never evicted and never expire, so a menu key pressed during a burst always lands.
    bool evictable = true;
};

/// Exponentially-weighted mean of recent world-tick wall-clock costs.
/// Pure state holder — no game access, so [coop][inputwindow] tests drive it directly.
struct coop_tick_cost_tracker {
    double ewma_ms = 0.0;
    auto sample( double tick_ms ) -> void;
    auto value() const -> double { return ewma_ms; }
};

/// The input coalescing/staleness window: how long the slower side needs to resolve one
/// committed action.  clamp( max( local, remote ), MIN, MAX ).
auto coop_input_window_ms( double local_ewma_ms, double remote_ewma_ms ) -> double;

/// Append `act` at `now_ms`.  While the queue exceeds COOP_MAX_QUEUED_ACTIONS, erase the
/// oldest evictable entry; if none is evictable, erase the oldest entry regardless so the
/// bound is hard.  Returns the number of entries evicted.
auto coop_admit_action( std::deque<buffered_action> &q, buffered_action act )
-> std::size_t; // *NOPAD*

/// Erase every evictable entry older than `window_ms`, except the single most recent entry
/// in the queue, which is never dropped.  Returns the number erased.
auto coop_expire_stale_actions( std::deque<buffered_action> &q, double now_ms,
                                double window_ms ) -> std::size_t; // *NOPAD*
```

`src/coop_input_window.cpp` — behaviour, exactly:

- `sample()`: first sample (`ewma_ms == 0.0`) assigns `tick_ms` directly; afterwards
  `ewma_ms = COOP_INPUT_EWMA_ALPHA * tick_ms + ( 1.0 - COOP_INPUT_EWMA_ALPHA ) * ewma_ms`.
  Negative or non-finite `tick_ms` is ignored (leave `ewma_ms` unchanged) — guards against a
  clock jump.
- `coop_input_window_ms()`: `std::clamp( std::max( local, remote ), COOP_INPUT_WINDOW_MIN_MS,
  COOP_INPUT_WINDOW_MAX_MS )`. Non-finite inputs are treated as `0.0`.
- `coop_admit_action()`: as documented above. Note the eviction loop runs *after* the push,
  so admitting into a full queue drops the oldest, not the new entry — the newest intent
  always survives.
- `coop_expire_stale_actions()`: iterate front-to-back over all but the last element; erase
  entries where `entry.evictable && ( now_ms - entry.enqueued_ms ) > window_ms`.
  A queue of size ≤ 1 always returns 0.

Add these constants to `src/coop_proto.h`, immediately after
`COOP_FAST_FORWARD_ACCUM_MS` (line 69), keeping that file the single source of truth for
shared co-op timing:

```cpp
/// Input buffer window bounds (ms of wall clock).  The window is sized from the measured
/// tick cost of the slower of host and client; see coop_input_window.h.
/// MIN preserves the previous fixed 16 ms coalescing behaviour on a fast machine.
constexpr double COOP_INPUT_WINDOW_MIN_MS = 16.0;
/// MAX bounds how long stale input is retained; above this the game feels unresponsive.
constexpr double COOP_INPUT_WINDOW_MAX_MS = 250.0;
/// EWMA smoothing for the tick-cost estimate: reacts within ~4 ticks, ignores one-off spikes.
constexpr double COOP_INPUT_EWMA_ALPHA = 0.25;
/// Hard depth cap on the co-op action buffer: the action in flight plus one look-ahead.
/// This is the anti-"train of actions" bound — a burst can never bank more than this.
constexpr std::size_t COOP_MAX_QUEUED_ACTIONS = 2;
```

`coop_proto.h` currently includes only `<cstdint>`; add `#include <cstddef>` for `std::size_t`.

#### Step 1.2 — Measure tick cost and exchange it between host and client

`coop_session` (`src/coop_session.h`) is the existing home for partner telemetry
(`partner_ping_ms` is already an `std::atomic<int>` written by the IO thread). Add two
members next to it, following the same comment style:

```cpp
    /// EWMA of the wall-clock cost of this side's own world tick, in ms.
    /// Main-thread only; written by the co-op main loop in main.cpp.
    coop_tick_cost_tracker local_tick_cost;
    /// Partner-reported tick cost in ms.  Written by the host's receiver thread from
    /// client_status, and by the client's main thread from sync — atomic like partner_ping_ms.
    std::atomic<int> partner_tick_ms{0};
```

Add `#include "coop_input_window.h"` to `coop_session.h`.

Wire fields (both additive; a missing field parses as `0`, so an old peer degrades to
local-only sizing — no version bump). All four sites are verified:

- **client → host**, packet `client_status` (13). Build side: `src/coop_client.cpp`, inside
  the `status_jout` object (lines 296-354), immediately after
  `status_jout.member( "ping_echo", std::to_string( last_ping_ts_ ) );` at line 318, add
  `status_jout.member( "tick_ms", static_cast<int>( coop_session::get().local_tick_cost.value() ) );`
  Parse side: `src/coop_server.cpp`, in the `else if( t == coop_pkt::client_status )` branch
  that opens at line 302, immediately after
  `client_hp_pct_.store( d.get_int( "hp_pct", 100 ) );` at line 306, add
  `coop_session::get().partner_tick_ms.store( d.get_int( "tick_ms", 0 ) );`
  This branch runs on the receiver thread, which is why `partner_tick_ms` is atomic.
- **host → client**, packet `sync` (20). Build side: `src/coop_server.cpp`, in
  `build_and_send_sync()`, immediately after
  `jout.member( "ping_ts", static_cast<int64_t>( SDL_GetTicks() ) );` at line 1546, add
  `jout.member( "tick_ms", static_cast<int>( coop_session::get().local_tick_cost.value() ) );`
  Parse side: `src/coop_client.cpp`, in `apply_sync()` beside the existing `ping_ts` read that
  assigns `last_ping_ts_` (grep `ping_ts` in that function), add
  `coop_session::get().partner_tick_ms.store( <the parsed int> );`

#### Step 1.3 — Rewire the co-op main loop

Change `std::queue<std::string> pending_action_queue_` at `src/game.h:242` to
`std::deque<buffered_action> pending_action_queue_;` and add
`#include "coop_input_window.h"` to `game.h`. Line 242 is the only `std::queue` use in that
header, so also delete `#include <queue>` at `src/game.h:43`.
`pending_action_queue_` appears at exactly `src/game.h:242` and
`src/main.cpp:737, 769, 774, 776` — those are all the call sites.

In `src/main.cpp`, rewrite the co-op loop body (lines 712-798) as follows. Everything not
mentioned stays byte-identical.

- Delete `constexpr double COALESCE_WINDOW_MS = 16.0;` (line 717).
- Add, next to `last_tick`:
  ```cpp
  const auto loop_start = clk::now();
  const auto now_ms = [&]() -> double {
      return std::chrono::duration<double, std::milli>( clk::now() - loop_start ).count();
  };
  ```
- Compute the window once per frame, before the coalesce block:
  ```cpp
  auto &sess = coop_session::get();
  const double window_ms = coop_input_window_ms(
      sess.local_tick_cost.value(),
      static_cast<double>( sess.partner_tick_ms.load() ) );
  ```
- Replace the enqueue at line 737 (`g->pending_action_queue_.push( action_str );`) with:
  ```cpp
  const bool evictable = can_action_change_worldstate( look_up_action( action_str ) );
  coop_admit_action( g->pending_action_queue_,
                     { .action = action_str, .enqueued_ms = now_ms(), .evictable = evictable } );
  host_acted = true;
  ```
  `src/main.cpp` already includes `game.h`, `avatar.h`, `coop_session.h`, `input.h` and
  `<chrono>` transitively (the loop already uses `std::chrono::steady_clock` at line 714).
  Add two includes to the block at `src/main.cpp:25-53`: `#include "action.h"` (for
  `look_up_action` / `can_action_change_worldstate`, declared at `src/action.h:412` and
  `:441`) and `#include <chrono>` to make the existing implicit dependency explicit.
- Replace `coalesce_start_ms >= COALESCE_WINDOW_MS` (line 752) with
  `coalesce_start_ms >= window_ms`.
- Immediately before the `if( fire_tick )` block, expire stale input:
  ```cpp
  coop_expire_stale_actions( g->pending_action_queue_, now_ms(), window_ms );
  ```
  This is what kills the trailing train: once the player stops pressing, everything older
  than one window is discarded on the next frame, leaving at most the newest entry.
- Wrap the tick body in a wall-clock measurement and feed the tracker. Inside
  `if( fire_tick ) { … }`, take `const auto tick_t0 = clk::now();` as the first statement and,
  after the `while( g->u.moves > 0 )` drain loop closes (before the `is_game_over()` check),
  add:
  ```cpp
  sess.local_tick_cost.sample(
      std::chrono::duration<double, std::milli>( clk::now() - tick_t0 ).count() );
  ```
- Update the drain loop (lines 773-784) for the deque:
  ```cpp
  while( g->u.moves > 0 ) {
      if( !g->pending_action_queue_.empty() ) {
          const auto act = g->pending_action_queue_.front().action;
          g->pending_action_queue_.pop_front();
          DebugLog( DL::Info, DC::Main ) << "[coop][action] " << act;
          g->handle_action_from( act );
          if( g->modal_fiber_ && !g->modal_fiber_->done() ) { break; }
      } else {
          g->u.moves = 0;
          break;
      }
  }
  ```
- Extend the existing tick DebugLog (line 766-769) with ` << " window=" << window_ms` so the
  adaptive window is visible in `config/debug.log` for the verification step.

Edge cases, all covered by the above: empty queue → `coop_expire_stale_actions` returns 0 and
the drain loop zeroes `u.moves` as before; single entry → never expired, so a lone keypress
during a 5-second tick still executes; a menu/info action (`evictable == false`) is never
evicted and never expires, so it survives an arbitrary burst; partner never reports
(`partner_tick_ms == 0`) → window falls back to the local estimate, floor 16 ms, which is the
current behaviour.

Explicitly not touched: the single-player loop (`src/main.cpp:801`, `while( !g->do_turn() );`)
and its existing OS-input drain at `src/game.cpp:1105-1107`. "Host and client" makes this
window a co-op concept; single-player already has `inp_mngr.pump_events()`.

### Phase 2 — Client world-step parity

Phase 2 steps are ordered: 2.1 must land before 2.2 (weather needs the host seed), 2.2
before 2.3. 2.4 and 2.5 are independent of each other and of 2.1-2.3.

#### Step 2.1 — Ship the host's world seed to the client

`weather_manager::update_weather()` derives everything from
`weather_gen.get_weather( g->u.abs_pos(), calendar::turn, g->get_seed() )`
(`src/weather.cpp:1150`) — deterministic in (position, turn, seed). `game::seed`
(`src/game.h:1286`) is per-save, assigned `rng_bits()` at new game
(`src/game_setup.cpp:668`) and persisted (`src/savegame.cpp:1299`), so the client's own world
has a *different* seed. Without adopting the host's seed the client would simulate a
different sky from the same turn. The existing `rng_seed` field in `world_seed` is
`g_main_rng_seed` (the global RNG engine seed, `src/coop_server.cpp:1662`) — a different
value; do not reuse it.

- `src/coop_packets.h`: add `unsigned int world_seed = 0;` to `struct world_seed_data`
  (lines 17-24), beside the existing `unsigned int rng_seed = 0;` at line 22.
- `src/coop_packets.cpp`: in `build_world_seed_packet` (line 9) add
  `jout.member( "world_seed", std::to_string( d.world_seed ) );` next to the `rng_seed`
  member (line 23) — string-encoded for the same reason `rng_seed` is: `unsigned int` values
  above `INT_MAX` do not survive `get_int`. In `parse_world_seed_packet` (line 46) add
  `result.world_seed = static_cast<unsigned int>( std::stoul( d.get_string( "world_seed", "0" ) ) );`
  next to line 65.
- `src/coop_server.cpp`: set `.world_seed = g->get_seed()` in **both** `world_seed_data`
  literals — the synchronous one at line 211 and the async-join one at line 1706. The async
  one reads only `pending_seed_*` members captured on the main thread at lines 1657-1662, so
  also add `unsigned int pending_seed_world_seed_ = 0;` to `src/coop_server.h` beside
  `unsigned int pending_seed_rng_ = 0;` at line 331, assign it `g->get_seed()` alongside
  line 1662, and read it at line 1706.
- `src/game.h`: add a setter beside `get_seed()` (line 988):
  `auto set_seed( unsigned int s ) -> void { seed = s; }`
- `src/coop_client.cpp`: store `world_seed_` from the parsed packet in `receive_world_seed()`
  (beside `world_seed_turn_` at line 168), and apply it in `apply_world_seed_to_avatar()`
  as the **first** statement, before the `calendar::turn` assignment at line 184:
  `if( world_seed_ != 0 ) { g->set_seed( world_seed_ ); }`

#### Step 2.2 — `game::coop_client_turn_step()` and `game::coop_client_frame_step()`

Add both to `src/game_action.cpp` immediately after `post_action_world_step()`
(which ends at line 2145), and declare them in `src/game.h` beside
`post_action_world_step`'s declaration. Two functions, not one, because a single sync can
advance up to `COOP_ACTIVITY_YIELD_INTERVAL` (10) turns: per-turn avatar simulation must run
once per turn, but cache rebuilds and audio are per-frame. This mirrors what single-player
already does during activity fast-forward, which rebuilds the map cache once per 10-turn
window (`src/game_activity.cpp:342-343`).

Both functions live in `game_action.cpp` alongside `post_action_world_step()`, so every
name in the tables below — `cleanup_arenas`, `get_weather`, `timed_events`, `mission`,
`sfx::*`, `sounds::*`, `character_funcs::*`, `explosion_handler`, `DynamicDataLoader` — is
already in scope in that translation unit. **No new `#include` is needed for anything
copied**; the only new symbol is `sounds::clear_recent_sounds()` below.

**Selection rule** (apply it to decide anything not listed): a statement from
`post_action_world_step()` belongs on the client iff it touches only the client's own
avatar, its own save-local bookkeeping, or local caches/UI/audio. Anything that mutates
map terrain, items on the ground, creatures, vehicles, fields, power grids or the overmap is
host-authoritative and stays out — the client receives those through `apply_sync`.

`coop_client_turn_step()` — called once per advanced turn. Copy each statement from the
`post_action_world_step()` line given, preserving relative order:

| from | statement |
|---|---|
| 1837 | `cleanup_arenas();` |
| 1885-1888 | `weather_manager &weather = get_weather(); weather.clear_temp_cache();` |
| 1895 | `timed_events.process();` |
| 1898 | `mission::process_all();` |
| 1901-1906 | the `u.in_vehicle && u.controlling_vehicle` theft check, verbatim |
| 1908-1910 | `if( u.is_mounted() ) { u.check_mount_is_spooked(); }` |
| 1929 | `u.update_body();` |
| 1933-1937 | the AUTOSAVE block, verbatim |
| 1940-1941 | `weather.update_weather(); reset_light_level();` |
| 1946-1947 | `process_voluntary_act_interrupt(); process_activity();` |
| 1952 | `sounds::reset_markers();` |
| 1961 | `sounds::process_sound_markers( &u );` |
| 1963-1965 | `if( u.is_deaf() ) { sfx::do_hearing_loss(); }` |
| — | `sounds::clear_recent_sounds();` — **new**, see below |
| 1977-1983 | the scent block (`bio_scent_mask`/`DEBUG_NOSCENT` guard, `scent.set`, `overmapbuffer.set_scent`, `scent.update`), verbatim |
| 2001 | `m.creature_in_field( u );` |
| 2049 | `u.process_turn();` |
| 2054 | `cata::run_on_every_x_hooks( *DynamicDataLoader::get_instance().lua );` |
| 2058 | `explosion_handler::get_explosion_queue().execute();` |
| 2061 | `cleanup_dead();` |
| 2069-2071 | `if( get_levz() >= 0 && !u.is_underwater() ) { handle_weather_effects( weather.weather_id ); }` |
| 2076-2078 | `u.update_bodytemp( m, weather ); character_funcs::update_body_wetness( u, get_weather().get_precise() ); u.apply_wetness_morale( weather.temperature );` |
| 2092 | `u.volume = 0;` |

`coop_client_frame_step()` — called once per `apply_sync`, after the turn loop:

| from | statement |
|---|---|
| 1882 | `m.invalidate_visibility_caches();` |
| 1948 | `update_performance_bubble();` |
| 1967-1969 | the `driving_view_offset` / `calc_driving_offset` block, verbatim |
| 1988 | `m.build_floor_caches();` |
| 2028 | `m.build_map_cache( get_levz(), true );` |
| 2046 | `mon_info_update();` |
| 2073 | `handle_wait_activity_redraw();` |
| 2081-2083 | `if( !u.is_deaf() ) { sfx::remove_hearing_loss(); }` |
| 2085-2089 | the four `sfx::do_*` calls, verbatim |

**Deliberately excluded, do not add** — each mutates host-authoritative state or duplicates
host work: `try_activity_fixed_window_skip()` (1838, host drives fast-forward via
`COOP_ACTIVITY_YIELD_INTERVAL`); `gamemode->per_turn()` and `calendar::turn += 1_turns`
(1871-1872, turn arrives in the sync packet); `load_npcs()` (1891); horde / mongroup /
`m.spawn_monsters` (1911-1924); `perhaps_add_random_npc()` (1945); the NPC sound-marker loop
(1956-1960); `m.process_falling()` / `autopilot_vehicles()` / `m.vehmove()` (1991-1994);
`m.process_items()` (1998); grid trackers (2004-2008, 2112-2120); portal / pocket-dimension /
vehicle-portal ticks (2011-2013); `fluid_grid::update` (2016); `sounds::process_sounds()`
(2021 — it signals hordes and drives monster AI; replaced by `clear_recent_sounds()`);
`monmove()` (2033); `npcmove()` / `sleep_skip_npc_process()` (2036-2040);
`overmap_npc_move()` (2042); `update_stair_monsters()` (2045); `world_tick()` (2097);
`submap_loader` update block (2106-2110 — background streaming would run mapgen the host
never ran).

`mission::process_all()`, `timed_events.process()` and the explosion queue can each mutate
local map state in rare paths. That is accepted: the client's map is overwritten by the
delta stream and the 30-second full sync, so any divergence is transient — and without them
the client's quest system, timed events and its own thrown explosives are dead features.

**New helper** — `sounds::clear_recent_sounds()`. `recent_sounds` (`src/sounds.cpp:168`) is
cleared *only* by `process_sounds()` (line 515) and `reset_sounds()` (line 775). The client
must not call `process_sounds()` (horde signalling + monster AI) and must not call
`reset_sounds()` (it also clears `sound_markers` at line 777, wiping the sound overlay), so
without a narrow clear `recent_sounds` grows without bound for the whole session. Add to
`src/sounds.h` inside `namespace sounds`, directly after the `void reset_markers();`
declaration (line 70):

```cpp
/* Clear the pending monster-AI sound list without touching player-visible markers.
 * Co-op clients need this: they never run process_sounds() (host owns monster AI), so
 * recent_sounds would otherwise grow for the whole session. */
void clear_recent_sounds();
```

and in `src/sounds.cpp` beside `reset_sounds()` (line 773):
`void sounds::clear_recent_sounds() { recent_sounds.clear(); }`

#### Step 2.3 — Call the new steps from the client

In `src/coop_client.cpp`, replace the tail of `apply_sync()` at lines 951-954:

```cpp
    const int turns_advanced =
        std::max( 0, to_turn<int>( calendar::turn ) - to_turn<int>( turn_before ) );
    const int catch_up = std::min( turns_advanced, COOP_ACTIVITY_YIELD_INTERVAL );
    for( int i = 0; i < catch_up; ++i ) { g->u.process_turn(); }
```

with

```cpp
    const int turns_advanced =
        std::max( 0, to_turn<int>( calendar::turn ) - to_turn<int>( turn_before ) );
    const int catch_up = std::min( turns_advanced, COOP_ACTIVITY_YIELD_INTERVAL );
    // Per-turn avatar simulation: the client's half of post_action_world_step().
    // u.process_turn() is called inside coop_client_turn_step() — do NOT call it here too.
    for( int i = 0; i < catch_up; ++i ) { g->coop_client_turn_step(); }
    // Caches, monster info and audio are per-frame, not per-turn (matches single-player's
    // activity fast-forward, game_activity.cpp:342-343).
    g->coop_client_frame_step();
```

`coop_client_frame_step()` runs unconditionally, even when `catch_up == 0`, so a sync that
carries only tile deltas still refreshes the vision cache and monster info.

Keep the existing `g->u.process_turn();` at `src/coop_client.cpp:221`
(`apply_world_seed_to_avatar`) — that is the one-shot spawn initialisation, not a turn step.

#### Step 2.4 — Host lays a scent trail for the client's proxy

Single-player writes the player's scent every turn (`src/game_action.cpp:1977-1983`), which
is how monsters track a player by smell. The host writes it only for `g->u`, so the client's
avatar is currently odourless in the authoritative world and no monster can ever track it.
In `coop_server::coop_world_tick()`, inside the existing `if( proxy )` block (opens at
`src/coop_server.cpp:549`) and immediately before the `try_pop_action()` call at line 649,
add:

```cpp
        // Parity with post_action_world_step(): a player leaves a scent trail, so the
        // client's proxy must too, or monsters can never track the client by smell.
        if( !proxy->has_active_bionic( bionic_id( "bio_scent_mask" ) ) ) {
            g->scent.set( proxy->bub_pos(), proxy->scent, proxy->get_type_of_scent() );
        }
```

`bionic_id` is already in scope in `coop_server.cpp` (constructed at line 609) — no new
include. `g->scent` is the public `scent_map &scent;` at `src/game.h:1152`; `Character::scent`
is the public `int scent = 0;` at `src/character.h:1732` and
`Character::get_type_of_scent()` is declared at `src/character.h:1953`, so the proxy `npc *`
supplies both directly.

#### Step 2.5 — Keep the two Lua mapgen/spawn guards as they are

`src/catalua.cpp:932` and `:943` skip `on_mapgen_postprocess` / mapgen item placement for
clients, and `src/game.cpp:2329-2332` skips `on_creature_spawn` / `on_monster_spawn`. Those
hooks place items and creatures — host-authoritative — so re-enabling them would make the
client generate content the host never generated. Leave all three gates untouched. The
per-turn mod hook (`run_on_every_x_hooks`) added in Step 2.2 is the mod-parity fix; these
three are not.

## Critical files & anchors

- `src/main.cpp:712-798` — the entire co-op accumulator/fiber main loop. `COALESCE_WINDOW_MS`
  (717), enqueue (737), coalesce arithmetic (742-763), tick + drain (765-790). Everything in
  Phase 1.3 happens here.
- `src/game_action.cpp:1828-2145` — `post_action_world_step()`. The source of every statement
  copied in Step 2.2; the line numbers in that table are hints, re-read before copying.
- `src/coop_client.cpp:547-955` — `apply_sync()`. Turn accounting at 552-557, the
  `process_turn()` tail at 951-954 replaced in Step 2.3, and the `last_ping_ts_` parse that
  anchors the `tick_ms` addition in Step 1.2.
- `src/coop_server.cpp:464-778` — `coop_world_tick()`. The `if( proxy )` block (549-655) holds
  the one-action-per-tick drain (649) and receives the proxy scent write in Step 2.4.
- `src/coop_proto.h:57-69` — shared timing constants; the new input-window constants join
  this block and must stay consistent with `main.cpp` as the existing comment demands.

## Verification

Working directory is the repo root. Build first — never with a short timeout, never killed
mid-run (see AGENTS.md build rules); run it as a background job and poll:

```powershell
cmake --preset windows-tiles-sounds-x64-msvc
cmake --build --preset windows-msvc-release --target cataclysm-bn-tiles cata_test-tiles
```

### 1. New unit tests for the input window (proves Phase 1's logic)

Add `tests/coop_input_window_test.cpp`, tagged `[coop][inputwindow]`, following the plain
pure-function style of `tests/coop_reconcile_test.cpp`. `tests/CMakeLists.txt:5-6` globs
`tests/*.cpp` with `CONFIGURE_DEPENDS`, so no build-file edit is needed. It must assert, at
minimum:

- `coop_input_window_ms( 5, 5 ) == COOP_INPUT_WINDOW_MIN_MS` (floor holds).
- `coop_input_window_ms( 900, 40 ) == COOP_INPUT_WINDOW_MAX_MS` (ceiling holds).
- `coop_input_window_ms( 40, 120 ) == 120.0` — the **remote** side wins when it is slower;
  this is the "longest taking action of host and client" requirement.
- `coop_tick_cost_tracker`: first `sample(100)` → `value() == 100`; a following `sample(0)`
  → `75.0` with `alpha = 0.25`; `sample(-1)` and `sample(NaN)` leave the value unchanged.
- Admitting 10 evictable actions leaves exactly `COOP_MAX_QUEUED_ACTIONS`, and the survivors
  are the **last two admitted** (newest intent wins).
- Admitting 10 actions where one is `evictable = false` keeps that entry plus the newest,
  and the non-evictable entry is still present after `coop_expire_stale_actions` with a
  window of `0.0`.
- `coop_expire_stale_actions` with three entries at `t = 0, 0, 100` and `now = 200,
  window = 50` erases the two old evictable entries and keeps the newest — asserting the
  never-drop-the-newest invariant directly.
- A queue of size 1 whose single entry is 10 s old survives `coop_expire_stale_actions`.

Run:

```powershell
.\out\build\windows-tiles-sounds-x64-msvc\Release\cata_test-tiles.exe "[coop][inputwindow]"
```

### 2. Existing co-op suite must stay green (proves nothing regressed)

```powershell
.\out\build\windows-tiles-sounds-x64-msvc\Release\cata_test-tiles.exe "[coop]"
```

`tests/coop_inproc_test.cpp` drives a real host+client pair through `coop_sim_transport`
(harness at `tests/coop_inproc_test.cpp:53-143`) and already covers join, movement relay,
status sync, resync and checksum convergence. Phase 2 changes what runs on the client per
turn, so the checksum-convergence cases there are the regression net for map divergence.

### 3. In-process parity test (proves Phase 2's new behaviour)

Extend `tests/coop_inproc_test.cpp` with a `[coop][inproc][parity]` case that, after
`h.setup()`:

- flips `coop_session::get().mode` to `client` with the existing `coop_mode_guard`,
- records `g->u.get_hunger()` (or `get_stored_kcal()`), `get_weather().weather_id`,
  and `to_turn<int>( calendar::turn )`,
- runs ~60 `h.tick()` iterations,
- asserts the turn advanced, **and** that the client-side hunger/kcal value changed —
  which is false today, because `update_body()` never runs on the client, and true after
  Step 2.2. This is the concrete input → observable output check for the parity work.

### 4. Live two-instance smoke test (the end-to-end proof)

Launch two instances of the installed build on one machine, hosting from the first and
joining `127.0.0.1` from the second (co-op entry points live in `src/coop_menu.cpp`).
Launch directly into a world to skip menu driving:
`cataclysm-bn-tiles.exe --dont-debugmsg --world "<WorldName>"`.

On the **client** instance, confirm each previously-dead feature:

- **Input window** — hold a movement key for ~3 s, then release. Before the change the
  avatar keeps walking for several tiles after release; after it, movement stops within one
  window. Confirm in `config/debug.log` that `[coop][tick=…]` lines now print
  `window=<n>` with `n > 16` while the tick is expensive, and that `pending=` never exceeds
  2. Then, with the host asleep or crafting (so its ticks are slow), press a single
  direction key once — it must still execute, proving the never-drop-the-newest invariant.
- **Weather** — the client's sidebar weather string and any rain/snow overlay must match
  the host's and must change over time instead of staying frozen.
- **Light level** — advance the clock past dusk (host uses debug time skip); the client's
  screen must darken.
- **Body needs** — client hunger/thirst/fatigue must tick up; sleeping must restore fatigue.
- **Activity** — start a craft on the client; the progress bar must advance to completion.
- **Fields** — walk the client into fire; it must take damage and catch fire.
- **Safe mode** — a monster entering the client's view must raise the safe-mode prompt.
- **Sound** — firing a gun near the client must produce sound markers and audible SFX.
- **Missions** — accept a mission on the client and confirm it appears and progresses in the
  missions screen.

Read `config/debug.log` after the session for `[coop]` errors and any `debugmsg`.

## Assumptions & contingencies

- **Weather is reproduced, not synced.** Step 2.1 relies on
  `weather_generator::get_weather` being a pure function of (position, turn, seed)
  (`src/weather.cpp:1150`). If the live smoke test shows host and client weather diverging
  anyway, do not chase determinism: add a `"weather"` object (`weather_id` string,
  `temperature`, `windspeed`, `winddirection`) to the `sync` packet next to `tick_ms`, and
  in `coop_client_turn_step()` replace `weather.update_weather()` with applying those fields
  and calling `sfx::do_ambient()`.
- **`COOP_MAX_QUEUED_ACTIONS = 2`** is chosen so a burst can bank at most one action ahead of
  the one in flight. If playtesting finds held-key movement stutters on a slow host, raise it
  to `3` in `coop_proto.h` — it is a single constant and the tests assert against the
  constant, not a literal.
- **`COOP_INPUT_WINDOW_MAX_MS = 250.0`** bounds staleness. If a slow host makes the client
  feel laggy because the window keeps input alive for a quarter second, lower it to `120.0`;
  do not remove the ceiling, or a multi-second host tick would retain a whole burst.
- **`can_action_change_worldstate()`** (`src/action.cpp:403`) classifies `ACTION_INVENTORY`
  and other modal openers as world-changing, so they are evictable. That is safe because
  eviction and expiry never touch the newest entry — the exemption list only needs to cover
  keys a player might press *before* a burst. If a specific modal key still gets swallowed in
  playtesting, add that `action_id` to the `return false` list in
  `can_action_change_worldstate()` rather than special-casing it in the co-op loop.
- **Proxy pickup is not a gap.** `src/coop_server.cpp:818-822` logs "NPC pickup deferred",
  but the client's real pickup relay is the `"PICKUP"` manifest
  (`src/activity_actor.cpp:1159-1223` → `coop_server::apply_pickup_manifest`,
  `src/coop_server.cpp:881`), which does remove the items from the host's map. Only the proxy
  NPC's own inventory is unpopulated, and it is never read (death drops are relayed by
  `coop_client::send_death_drop`). Do not spend time on it.
- **Client death already matches single-player.** `src/main.cpp:786-789` calls
  `g->cleanup_at_end()` for client and host alike, so a dead client returns to the main menu
  exactly as in single-player. The host-side "full respawn / session-end logic is deferred"
  note at `src/coop_server.cpp:660` is about rejoining, which has no single-player
  equivalent — out of scope here.
