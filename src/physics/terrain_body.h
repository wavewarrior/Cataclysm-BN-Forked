#pragma once
#ifdef BOX2D_ENABLED
#include <box2d/box2d.h>
#include "coordinates.h"
#include <cstdint>
#include <vector>

class map;

namespace physics {

/// Classification of a terrain tile for Box2D body creation.
enum class tile_body_class : uint8_t {
    passable = 0, ///< Open ground — no body needed.
    bashable,     ///< Destructible obstacle — per-tile body; may be destroyed on bash.
    solid,        ///< Indestructible obstacle — per-tile static body.
};

/// Returns the physics class for the tile at `bub` in map `m`.
auto classify_tile( const map &m, tripoint_bub_ms bub ) -> tile_body_class;

/// Create one static Box2D body per solid/bashable tile in the submap whose in-bubble
/// origin (lower-left corner) is `bub_origin`.  Bashable-tile bodies have their user-data
/// set to the encoded tile bub_ms position via `encode_tile_pos` so callers can recover
/// the tile identity for Phase 5 bash callbacks.
///
/// @param world     The persistent Box2D world.
/// @param m         Live map reference used for tile classification queries.
/// @param bub_origin Bub_ms position of the submap's (0,0) local tile.
/// @return Flat list of created b2BodyId values (solid then bashable order is not guaranteed).
auto build_submap_terrain_bodies( b2WorldId         world,
                                   const map        &m,
                                   tripoint_bub_ms   bub_origin ) -> std::vector<b2BodyId>;

/// Pack a `tripoint_bub_ms` into a pointer-width integer for `b2Body_SetUserData`.
/// Only the lower 48 bits are used; each coordinate occupies 16 bits (signed).
constexpr auto encode_tile_pos( tripoint_bub_ms p ) -> std::uintptr_t
{
    return ( static_cast<std::uintptr_t>( static_cast<uint16_t>( p.z() ) ) << 32 )
         | ( static_cast<std::uintptr_t>( static_cast<uint16_t>( p.y() ) ) << 16 )
         |   static_cast<std::uintptr_t>( static_cast<uint16_t>( p.x() ) );
}

/// Inverse of `encode_tile_pos`.
constexpr auto decode_tile_pos( std::uintptr_t enc ) -> tripoint_bub_ms
{
    const auto x = static_cast<int>( static_cast<int16_t>(   enc         & 0xFFFF ) );
    const auto y = static_cast<int>( static_cast<int16_t>( ( enc >> 16 ) & 0xFFFF ) );
    const auto z = static_cast<int>( static_cast<int16_t>( ( enc >> 32 ) & 0xFFFF ) );
    return tripoint_bub_ms{ x, y, z };
}

} // namespace physics
#endif // BOX2D_ENABLED
