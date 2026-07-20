#pragma once
#ifndef CATA_SRC_PHYSICS_FILTER_BITS_H
#define CATA_SRC_PHYSICS_FILTER_BITS_H

#include <algorithm>
#include <cstdint>

namespace physics {

/// Box2D filter bit layout for z-level isolation.
///
/// All body types at the same z share a single category bit (bits 0–20 of uint32_t).
/// Body-type collision exclusion uses b2Filter::groupIndex:
///   - terrain  (groupIndex  0): uses category/mask rules — static-static never contacts
///   - vehicle  (groupIndex -1): same-group vehicles never collide with each other
///   - creature (groupIndex -2): same-group creatures never collide; shapes are sensors
///
/// Raycasts (b2QueryFilter) use maskBits = z_category_bit(z) to hit all body types at z.
///
/// This replaces the prior dual-range layout (terrain bits 0–20, vehicle bits 20–40)
/// which overflowed uint32_t at vehicle z ≥ 2.
inline auto z_category_bit( int z ) -> uint32_t
{
    return 1u << static_cast<uint32_t>( std::clamp( z + 10, 0, 20 ) );
}

constexpr int32_t terrain_group  =  0;
constexpr int32_t vehicle_group  = -1;
constexpr int32_t creature_group = -2;

} // namespace physics

#endif // CATA_SRC_PHYSICS_FILTER_BITS_H
