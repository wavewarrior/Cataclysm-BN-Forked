#pragma once

// Game-side glue for the world-locked GPU splatmap (lighting/splatmap_pass.h).
//
// Kept as a small pure-function header rather than edits to map.h (>10 usages,
// per the repo's "don't churn big headers" rule). Everything here is a no-op
// without a renderer (tests, --check-mods) or when the splatmap failed to load
// its stamp atlas, so field gameplay is never affected.

#include <cstdint>

#include "coordinates.h"
#include "type_id.h"

namespace splatmap
{

/// Packed cache key for a submap: 24 bits x, 24 bits y, 8 bits z (biased).
auto key_of( const tripoint_abs_sm &sm ) -> std::uint64_t;

/// Single source of truth for "the splatmap is drawing decals this frame".
///
/// MUST gate BOTH the composite (sdl_render_frame) and the grid-sprite
/// suppression (cata_tiles_draw_layers). If the two ever disagree, blood goes
/// invisible: the tile sprite is hidden while nothing composites in its place.
///
/// False under an ISOMETRIC tileset: player_to_screen's iso branch maps a submap
/// to a sheared DIAMOND, but the composite quad is axis-aligned, so the decal
/// layer cannot line up with the tiles. Iso falls back to the old grid sprite.
auto active() -> bool;

/// True when the splatmap renders decals for this field type, so its
/// grid-locked tile sprite must be suppressed.
auto covers_field( const field_type_id &type ) -> bool;

/// Queue decal stamps for a splatter of `type` at `where` (map-bub coords).
/// No-op when the renderer is absent (tests) or the type is not covered.
auto queue_splatter( const tripoint_bub_ms &where, const field_type_id &type,
                     int intensity ) -> void;

/// Seed a freshly created submap entry from current field data. `origin` is the
/// submap's top-left tile in map-bub coords.
auto seed_submap( const tripoint_bub_ms &origin, std::uint64_t key ) -> void;

} // namespace splatmap
