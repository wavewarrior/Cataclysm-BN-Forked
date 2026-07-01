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
    client_status = 13, ///< activity, stamina, mood (client → host, per tick)

    sync = 20,         ///< tile + monster + entity bulk update (host → client)
    vehicle_sync = 21, ///< vehicle state delta (host → client)
    overmap_sync = 22, ///< overmap chunk (host → client)

    chat = 30, ///< free-form text (bidirectional, any time)

    disconnect = 99, ///< graceful close notification
};

#endif // COOP_ENABLED
