#pragma once
#ifdef COOP_ENABLED

#include <cstdint>
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

    auto is_host() const -> bool { return mode == coop_mode::host; }
    auto is_client() const -> bool { return mode == coop_mode::client; }
    auto is_coop() const -> bool { return mode != coop_mode::none; }

    /// Global singleton — constructed on first use, never destroyed.
    static auto get() -> coop_session&; // *NOPAD*
};

#endif // COOP_ENABLED
