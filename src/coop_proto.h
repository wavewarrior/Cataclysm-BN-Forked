#pragma once
#ifdef COOP_ENABLED

#include <cstdint>

/// Unique character_id value reserved for the client's proxy NPC on the host.
/// Chosen to be far from any legitimate auto-assigned ID.
static constexpr int COOP_PROXY_CHAR_ID = 0x7FFF'FFFE;

/// Wire packet types.  Value stored as a single uint8_t in the JSON envelope
/// under key "t".  Sparse numbering leaves room for future additions.
enum class coop_pkt : uint8_t {
    handshake = 1,  ///< version + mod hash (bidirectional at connect time)
    world_seed = 2, ///< world id, turn, spawn pos, rng seed (host → client)

    action = 11,        ///< keypress + context blob (client → host, async)
    join_info = 12,     ///< client starting position after loading save (client → host, on join)
    client_status = 13, ///< activity, stamina, mood (client → host, per tick)

    sync = 20,         ///< tile + monster + entity bulk update (host → client)
    vehicle_sync = 21, ///< vehicle state delta (host → client)
    overmap_sync = 22, ///< overmap chunk (host → client)

    resync_request = 25, ///< client detected hash mismatch; host responds with forced full sync
    ///< (client → host)
    chat = 30,           ///< free-form text (bidirectional, any time)

    vehicle_state  = 42, ///< driven vehicle position/heading/velocity (client → host, per tick while driving)
    trade_offer    = 43, ///< one player offers an item to the other (bidirectional; "from" field disambiguates)
    trade_accept   = 44, ///< recipient confirms trade; transfer executes
    trade_reject   = 45, ///< recipient declines trade
    tap_shoulder   = 46, ///< interrupt partner's current long activity (bidirectional)
    overmap_mark   = 47, ///< place or clear a shared overmap marker (bidirectional)
    emote          = 48, ///< player emote — "high_five" in this version (bidirectional)
    stabilize      = 49, ///< host stabilizes downed client (host → client only)

    disconnect = 99, ///< graceful close notification
};

/// World mutation event types used by coop_mutation_log (A3).
/// Stored as uint8_t in the per-tick event buffer and serialised in A4 delta packets.
enum class coop_event_type : uint8_t {
    terrain_changed = 1,   ///< submap::set_ter()
    furniture_changed = 2, ///< submap::set_furn()
    creature_moved = 3,    ///< Creature::setpos()
    creature_died = 4,     ///< monster/npc/Character::die()
    creature_spawned = 5,  ///< future: creature placement
    creature_hp = 6,       ///< future: hp change
    item_spawned = 7,      ///< map::add_item()
    item_removed = 8,      ///< future: item removal
    field_created = 9,     ///< sub_add_field()
    field_changed = 10,    ///< set_field_intensity() / set_field_age()
    field_expired = 11,    ///< field removed in process_fields()
    turn_advanced = 12,    ///< calendar::turn increment
};

/// Fast-forward timing constants — single source of truth for server, client, and tests.
///
/// COOP_IDLE_TICK_MS must match IDLE_TICK_INTERVAL_MS in main.cpp (1000.0).
/// main.cpp is not included here; add a static_assert there if they ever drift.
constexpr double COOP_IDLE_TICK_MS = 1000.0; ///< must equal IDLE_TICK_INTERVAL_MS in main.cpp
constexpr int COOP_MAX_CATCH_UP = 3;         ///< max idle ticks per outer-loop iteration
/// How often the host sends a sync during a long activity (sleep, craft, read).
/// Also the max process_turn() calls the client runs per sync — both sides share one constant
/// so they stay in sync.  See game::execute_activity_fixed_window_skip (A5.2).
constexpr int COOP_ACTIVITY_YIELD_INTERVAL = 10;
/// Accumulator value that triggers COOP_MAX_CATCH_UP game ticks in the main loop.
/// Derived — changing COOP_IDLE_TICK_MS or COOP_MAX_CATCH_UP automatically updates this.
constexpr double COOP_FAST_FORWARD_ACCUM_MS = COOP_MAX_CATCH_UP * COOP_IDLE_TICK_MS;

#endif // COOP_ENABLED
