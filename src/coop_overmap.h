#pragma once
#ifdef COOP_ENABLED

#include "coordinates.h"

#include <string>
#include <vector>

/// Build an overmap_sync packet containing newly-revealed tile positions.
/// Called by game::update_overmap_seen() when tiles are newly marked seen.
auto build_overmap_sync_packet( const std::vector<tripoint_abs_omt> &tiles ) -> std::string;

/// Apply an overmap_sync packet: mark all contained tiles as seen in the
/// given dimension's overmapbuffer.  Called by coop_client::coop_world_tick()
/// (or coop_server receiver_loop) on receiving overmap_sync.
auto apply_overmap_sync_packet( const std::string &json_buf,
                                const std::string &dim_id ) -> void;

#endif // COOP_ENABLED
