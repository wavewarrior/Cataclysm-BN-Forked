#pragma once

#include "coop_mutation_log.h"
#include "coordinates.h"

#include <cstdint>

/// Compute an FNV-1a hash over the subset of world state that coop synchronises:
///   - Terrain and furniture in a radius around `center`
///   - Items on ground in the same radius
///   - Proxy NPC position (if valid proxy_npc_id in session)
///   - Host avatar position
///
/// Both host and client can call this after a full-sync cycle; if the
/// returned values differ, the worlds have diverged.
///
/// `radius` is in submap coordinates (default 2 → 5×5 grid, matching
/// build_and_send_sync's dx/dy range).
auto coop_world_checksum( int radius = 2 ) -> uint64_t;

