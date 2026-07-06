#pragma once
#ifdef COOP_ENABLED

#include "character_id.h"
#include "coordinates.h"

#include <cstdint>
#include <optional>
#include <string>

enum class coop_mode : uint8_t {
    none = 0,
    host = 1,
    client = 2,
};

struct coop_session {
    coop_mode mode = coop_mode::none;
    std::string partner_name;
    int partner_ping_ms = 0; // updated by receiver thread each tick
    character_id proxy_npc_id;

    /// B3 Phase 9: set to the attacked creature's abs position by autoattack() on success;
    /// cleared to nullopt at autoattack() entry so failed attacks relay nothing.
    /// Only written by autoattack() — does NOT alias last_target_pos (shared by fire/throw).
    std::optional<tripoint_abs_ms> last_autoattack_target;

    auto is_host() const -> bool { return mode == coop_mode::host; }
    auto is_client() const -> bool { return mode == coop_mode::client; }
    auto is_coop() const -> bool { return mode != coop_mode::none; }

    /// Global singleton — constructed on first use, never destroyed.
    static auto get() -> coop_session&; // *NOPAD*
};

#endif // COOP_ENABLED
