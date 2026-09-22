#pragma once

// Radiance Cascades parameterisation — Stage 7 of
// plans/gpu-daylight-black-scene-bisect-plan.md. Flatland formulation
// (Sannikov, "Radiance Cascades", https://github.com/Raikiri/RadianceCascadesPaper).
// Replaces the fixed 16-ray gather + EMA temporal filter in gi_bounce.comp /
// gi_bounce2.comp with a cascade hierarchy: constant memory per cascade,
// noise-free, no temporal ghosting across a structure rebuild.
//
// Geometry, in this flatland form:
//   - Cascade i has probes spaced 2^i tiles apart (cascade 0 = 1 probe/tile,
//     matching the pre-Stage-7 GI resolution) and RC_C0_DIRS * RC_BRANCH^i
//     angular directions per probe.
//   - probes_i = ceil(map / 2^i) shrinks by ~4x per cascade while dirs_i
//     grows by RC_BRANCH per cascade, so probes_i * dirs_i — the per-cascade
//     texel count — is CONSTANT across cascades (up to ceil() rounding).
//   - Cascade i marches the SDF over world-tile interval
//     [d0*(4^i-1)/3, d0*(4^(i+1)-1)/3], d0 = RC_C0_INTERVAL.
//
// Kept in HLSL lockstep by hand in rc_build.comp.hlsl / rc_merge.comp.hlsl /
// rc_resolve.comp.hlsl (a shader cannot include a C++ header).

#include <algorithm>
#include <array>
#include <cstdint>

namespace lighting {

/// Number of cascades. Reach = RC_C0_PROBE_SPACING * (4^RC_CASCADES - 1) / 3
/// tiles; 5 reaches 341 tiles, comfortably beyond the ~180-tile reality
/// bubble. Drop to 4 (reach 85 tiles, still > the 46x26 viewport) if the
/// frame-cost verification (Stage 7 item 12) misses budget — trade reach,
/// never RC_C0_DIRS or RC_C0_PROBE_SPACING (those govern near-field contact
/// shadow quality, the property RC is adopted for).
inline constexpr std::uint32_t RC_CASCADES = 5u;
/// Cascade-0 probe spacing, in tiles. One probe per tile — matches the
/// pre-Stage-7 GI resolution exactly.
inline constexpr float RC_C0_PROBE_SPACING = 1.0f;
/// Cascade-0 angular direction count. The minimum the x4 branching builds on.
inline constexpr std::uint32_t RC_C0_DIRS = 4u;
/// Angular texels x4 and probe spacing x2 per axis per cascade — constant
/// memory per cascade, and the merge averages 4 angular sub-directions
/// (the paper sanctions averaging 2 or 4 texels).
inline constexpr std::uint32_t RC_BRANCH = 4u;
/// Cascade-0 march interval length, in tiles. Cascade i marches
/// [d0*(4^i-1)/3, d0*(4^(i+1)-1)/3].
inline constexpr float RC_C0_INTERVAL = 1.0f;

/// One cascade's flat-buffer geometry.
struct rc_cascade_geom {
    std::uint32_t probes_x = 0;
    std::uint32_t probes_y = 0;
    std::uint32_t dirs = 0;
    std::uint32_t offset_floats = 0; // element (not byte) offset into the atlas
};

/// Per-cascade geometry for a `map_w` x `map_h` (cascade-0, i.e. tile-res)
/// grid, laid out back-to-back in one flat atlas buffer.
inline auto rc_compute_geometry( std::uint32_t map_w, std::uint32_t map_h )
    -> std::array<rc_cascade_geom, RC_CASCADES>
{
    std::array<rc_cascade_geom, RC_CASCADES> out{};
    std::uint32_t offset = 0;
    for( std::uint32_t i = 0; i < RC_CASCADES; ++i ) {
        const std::uint32_t div = 1u << i;
        const std::uint32_t px = std::max( 1u, ( map_w + div - 1u ) / div );
        const std::uint32_t py = std::max( 1u, ( map_h + div - 1u ) / div );
        std::uint32_t dirs = RC_C0_DIRS;
        for( std::uint32_t b = 0; b < i; ++b ) {
            dirs *= RC_BRANCH;
        }
        out[i] = rc_cascade_geom{ .probes_x = px, .probes_y = py, .dirs = dirs,
                                   .offset_floats = offset };
        offset += px * py * dirs * 4u; // 4 floats/texel: rgb + beta
    }
    return out;
}

/// Total atlas size, in floats, for a `map_w` x `map_h` grid.
inline auto rc_total_floats( std::uint32_t map_w, std::uint32_t map_h ) -> std::uint32_t
{
    const auto g = rc_compute_geometry( map_w, map_h );
    const auto &last = g[RC_CASCADES - 1];
    return last.offset_floats + last.probes_x * last.probes_y * last.dirs * 4u;
}

/// Cascade-i world-tile march interval [near, far).
inline auto rc_cascade_interval( std::uint32_t cascade ) -> std::pair<float, float>
{
    auto pow4 = []( std::uint32_t e ) {
        float v = 1.0f;
        for( std::uint32_t k = 0; k < e; ++k ) {
            v *= 4.0f;
        }
        return v;
    };
    const float near_t = RC_C0_INTERVAL * ( pow4( cascade ) - 1.0f ) / 3.0f;
    const float far_t = RC_C0_INTERVAL * ( pow4( cascade + 1u ) - 1.0f ) / 3.0f;
    return { near_t, far_t };
}

} // namespace lighting
