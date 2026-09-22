#pragma once

#include <algorithm>

namespace lighting {

/// Roof height (tiles); a celestial ray clears a roof once it climbs above this.
inline constexpr float ROOF_H = 1.0f;
/// Occluder height at or above which a sky-dome direction is blocked.
inline constexpr float SKY_WALL_H = 0.60f;

/// Everything the occlusion rules need, as plain data.
struct tile_occlusion_query {
    /// level_cache::transparency_cache value — an ATTENUATION COEFFICIENT, not a
    /// fraction: LIGHT_TRANSPARENCY_SOLID (0.0) is the opaque sentinel and open
    /// air is only LIGHT_TRANSPARENCY_OPEN_AIR (0.0384).
    float transparency = 0.0f;
    /// map::coverage() — the ranged-COVER gameplay stat, 0..100.
    int coverage = 0;
    bool is_tree = false;              ///< TFLAG_TREE
    bool is_vehicle_obstacle = false;  ///< veh_at()->obstacle_at_part()
    bool floor_above = false;          ///< floor_cache(z+1)
    bool outside = false;              ///< outside_cache
    bool terrain_valid = true;         ///< ter(p).is_valid(); false during world load
};

/// The single occlusion verdict every lighting consumer derives from.
struct tile_occlusion {
    bool blocks_light = false;  ///< opaque to light; the ONLY predicate that may seed the SDF
    float height = 0.0f;        ///< occluder height in tiles; 0 when the tile transmits
    bool roofed = false;
    bool open_sky = false;
};

/// Coverage supplies an occluder's HEIGHT; the transparency cache — the same field the
/// game's own LOS trusts, and which already discounts glass, bars and chain-link —
/// decides whether it blocks LIGHT at all. Anything that transmits casts no shadow.
///
/// Rules, transcribed from the previously-scattered sites this header replaces
/// (src/lighting/frame_build.cpp's occ-build loop and src/cata_tiles.cpp's
/// push_occluder_footprint), preserved exactly except where noted:
///
///  - blocks_light: opaque terrain (transparency <= LIGHT_TRANSPARENCY_SOLID, and
///    only once terrain has finished loading) OR a vehicle obstacle part. This is
///    the ONLY predicate permitted to seed the SDF — a tile that transmits light
///    at all (windows, bars, chain-link) must never seed, regardless of how much
///    ranged cover it provides.
///  - height: 0 while terrain is still loading or the tile transmits light;
///    otherwise coverage()/100 clamped to [0,1]; raised to at least 1.0 for a
///    vehicle obstacle; then ZEROED for a full tree (Phase 2.3): tree sun shadows
///    come exclusively from the screen-space silhouette mask (shadow.vert/.frag,
///    sheared sprite copies, Graveyard Keeper style), so a tree must not ALSO
///    shadow the sun march or block the sky dome through OccBuf height — that
///    was the double shadow sprite.frag used to warn about. Trees are opaque
///    (no TRANSPARENT flag) so blocks_light stays true and they still seed the
///    SDF: point-light shadows, AO and GI occlusion keep working off the trunk
///    footprint.
///  - roofed / open_sky are carried through unchanged from the query.
constexpr auto classify_tile_occlusion( const tile_occlusion_query &q ) -> tile_occlusion
{
    tile_occlusion out{};
    const bool transmits = !q.terrain_valid || q.transparency > 0.0f;
    out.blocks_light = ( q.terrain_valid && !transmits ) || q.is_vehicle_obstacle;

    float h = ( !q.terrain_valid || transmits )
                  ? 0.0f
                  : std::clamp( static_cast<float>( q.coverage ) / 100.0f, 0.0f, 1.0f );
    if( q.is_vehicle_obstacle ) {
        h = std::max( h, 1.0f );
    }
    if( q.is_tree ) {
        h = 0.0f;
    }
    out.height = h;

    out.roofed = q.floor_above;
    out.open_sky = q.outside;
    return out;
}

} // namespace lighting
