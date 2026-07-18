#pragma once
#ifdef COOP_ENABLED

#include "coordinates.h"

#include <string>
#include <vector>

/// Build an overmap_sync packet containing newly-revealed tile positions.
auto build_overmap_sync_packet( const std::vector<tripoint_abs_omt> &tiles ) -> std::string;

/// Parse an overmap_sync packet into a list of tile positions.
/// Thread-safe — no game state accessed.  Call from any thread.
auto parse_overmap_sync_tiles( const std::string &json_buf )
-> std::vector<tripoint_abs_omt>;

/// Apply parsed overmap tiles: mark each as seen in the given dimension's overmapbuffer.
/// MUST be called on the main thread only (overmapbuffer is not thread-safe).
auto apply_overmap_sync_tiles( const std::vector<tripoint_abs_omt> &tiles,
                               const std::string &dim_id ) -> void;

#endif // COOP_ENABLED
