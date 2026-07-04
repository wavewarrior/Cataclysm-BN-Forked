#pragma once
#ifdef COOP_ENABLED

#include "coordinates.h"

#include <cstdint>
#include <optional>
#include <string>

/// Plain-data structs and pure serialisation/deserialisation functions for
/// co-op wire packets.  No game state, no sockets.

struct world_seed_data {
    int turn = 0;
    tripoint_abs_ms spawn_pos;
    std::string player_name;
    std::string world_name;
};

struct action_packet_data {
    uint32_t seq = 0;
    std::string key;
    std::string ctx_json; ///< wire key is "ctx"
};

struct sync_header_data {
    int turn = -1;
    int last_seq = -1;
    tripoint_abs_ms proxy_pos;
    tripoint_abs_ms host_pos;
    bool has_proxy_pos = false;
    bool has_host_pos = false;
};

auto build_world_seed_packet(const world_seed_data&) -> std::string;
auto build_action_packet(const action_packet_data&) -> std::string;
auto parse_world_seed_packet(const std::string&) -> std::optional<world_seed_data>;
auto parse_action_packet(const std::string&) -> std::optional<action_packet_data>;
auto parse_sync_header(const std::string&) -> std::optional<sync_header_data>;

#endif // COOP_ENABLED
