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
    unsigned int rng_seed = 0;
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

/// C6: join_info packet — client's starting abs position on join.
struct join_info_data {
    tripoint_abs_ms pos;
    std::string worn_json; ///< JSON array of serialized worn items; empty = none
};

/// C4: parsed landing position from a MOVE_UP/MOVE_DOWN ctx_json field.
struct vertical_move_ctx {
    tripoint_abs_ms landing;
};

auto build_world_seed_packet( const world_seed_data & ) -> std::string;
auto build_action_packet( const action_packet_data & ) -> std::string;
/// C6: build a join_info packet ({"t":12,"d":{"ax":N,"ay":N,"az":N}}).
auto build_join_info_packet( const join_info_data & ) -> std::string;
auto parse_world_seed_packet( const std::string & ) -> std::optional<world_seed_data>;
auto parse_action_packet( const std::string & ) -> std::optional<action_packet_data>;
auto parse_sync_header( const std::string & ) -> std::optional<sync_header_data>;
/// C6: parse a join_info packet; returns nullopt on wrong type or JSON error.
auto parse_join_info_packet( const std::string& ) -> std::optional<join_info_data>; // *NOPAD*
/// C4: parse a MOVE_UP/MOVE_DOWN ctx_json {"ax":N,"ay":N,"az":N}.
/// Returns nullopt when the string is empty or malformed.
auto parse_vertical_move_ctx( const std::string& ) -> std::optional<vertical_move_ctx>; // *NOPAD*

#endif // COOP_ENABLED
