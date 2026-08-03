#pragma once

#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_range.h"
#include <box2d/box2d.h>
#include <climits>
#include <cmath>
#include <optional>

/// Physical size of one map tile in metres.
/// Source: vehicles::cmps_per_tile = 178.816f (vehicle.h:61); TILE_M = cmps_per_tile / 100.
static constexpr float TILE_M = 1.78816f;

/// Compute the Box2D polygon for a vehicle body in its LOCAL (mount-space) frame.
///
/// Fixes three geometry bugs that exist in the naive approach:
///   1. Uses vp.mount() (unrotated local frame) — NOT precalc[0] which is already
///      world-rotated.  Setting bdef.rotation on top of precalc extents double-applies
///      the vehicle's heading, producing wrong contact geometry.
///   2. Off-by-one: mount indices are tile centres; a vehicle spanning [min, max]
///      occupies (max - min + 1) tiles.  hw = (max - min + 1) / 2 * TILE_M.
///   3. Centre offset: mount extents are not symmetric about (0, 0).  b2MakeOffsetBox
///      places the shape at the correct local-frame offset so the body origin (vehicle
///      reference point) and shape centre coincide with world reality.
///
/// Returns `std::nullopt` when the vehicle has no occupied mount at all.  Box2D cannot
/// represent an empty polygon: a default-constructed `b2Polygon` carries `count == 0`,
/// and `b2CreatePolygonShape` hands it straight to `b2ComputePolygonAABB`, which
/// asserts `count > 0` (box2d-src/src/geometry.c:400) and dereferences `vertices[0]`
/// regardless — an unconditional process abort on MSVC, where a failed B2_ASSERT
/// executes `__debugbreak()`.  The optional exists so no caller can consume the
/// degenerate result by accident; the previous signature returned `b2Polygon{}` and
/// all three call sites passed it through unchecked.
///
/// The empty-footprint case is real, not hypothetical: vproto "none"
/// (data/json/vehicles/vehicles.json:11) is declared with `"parts": [ ]` and is the
/// supported way to spawn a bare chassis that the caller then populates with
/// `vehicle::install_part()`.  `map::add_vehicle()` (mapgen.cpp:5910) calls
/// `on_vehicle_added()` before any part exists, so the very first vehicle built that
/// way aborted the process.
///
/// A one-tile vehicle is NOT degenerate — hw = hh = 0.5 * TILE_M gives a valid box —
/// and neither is a straight line of parts, so no legitimate footprint is rejected
/// here.  Only a completely empty part list is.
inline auto vehicle_box2d_shape( const vehicle &v ) -> std::optional<b2Polygon>
{
    auto min_mx = INT_MAX;
    auto max_mx = INT_MIN;
    auto min_my = INT_MAX;
    auto max_my = INT_MIN;
    for( const auto &vp : v.get_all_parts() ) {
        if( vp.part().removed ) {
            continue;
        }
        const auto m = vp.mount();
        min_mx = std::min( min_mx, m.x() );
        max_mx = std::max( max_mx, m.x() );
        min_my = std::min( min_my, m.y() );
        max_my = std::max( max_my, m.y() );
    }
    // No non-removed part contributed an extent.  Returning here also keeps the
    // INT_MAX - INT_MIN signed overflow below unreachable.
    if( min_mx > max_mx ) {
        return std::nullopt;
    }
    const auto hw = ( max_mx - min_mx + 1 ) / 2.0f * TILE_M;
    const auto hh = ( max_my - min_my + 1 ) / 2.0f * TILE_M;
    const auto cx = ( min_mx + max_mx ) / 2.0f * TILE_M;  // local-frame centre offset
    const auto cy = ( min_my + max_my ) / 2.0f * TILE_M;
    return b2MakeOffsetBox( hw, hh, { cx, cy }, 0.0f );
}

/// Rotate a part's local mount offset by `angle_rads` to get its world-space offset.
///
/// Used by `vehicle::refresh_precalc(float)` (Phase 7) for continuous-angle part placement.
/// Result is rounded to the nearest integer tile offset (same rounding as discrete precalc).
///
/// @param p          Vehicle part whose mount offset to rotate.
/// @param angle_rads Heading angle in radians (CCW from +x; from `vehicle::physics_angle`).
/// @return Part offset in rotated world frame, as integer tile coordinates.
inline auto part_world_offset( const vehicle_part &p, float angle_rads ) -> point_rel_ms
{
    const float mx = static_cast<float>( p.mount.x() );
    const float my = static_cast<float>( p.mount.y() );
    const float c  = std::cos( angle_rads );
    const float s  = std::sin( angle_rads );
    return point_rel_ms{
        static_cast<int>( std::round( mx * c - my * s ) ),
        static_cast<int>( std::round( mx * s + my * c ) )
    };
}
