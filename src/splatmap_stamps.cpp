#include "splatmap_stamps.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>
#include <random>

#include "cached_options.h" // tile_iso
#include "field.h"
#include "field_type.h"
#include "game.h"
#include "game_constants.h" // SEEX / SEEY
#include "lighting/render_state.h"
#include "lighting/splatmap_pass.h"
#include "map.h"
#include "sdl_lighting_devui.h" // g_splatmap_enable
#include "sdltiles.h"           // tilecontext

namespace splatmap
{

namespace
{

/// Splatter RNG. Deliberately a FILE-LOCAL mt19937 rather than the game rng():
/// decals are cosmetic, and perturbing the shared game RNG stream from a render
/// path would desync saves and coop. Same rule rain_rng follows in
/// sdl_render_frame.cpp. Seeding it per tile from absolute coordinates is also
/// what makes save/reload regeneration visually stable.
std::mt19937 splat_rng;

auto uniform( float lo, float hi ) -> float
{
    return std::uniform_real_distribution<float>( lo, hi )( splat_rng );
}

/// The eight field types the splatmap draws decals for. The four fd_gibs_*
/// select the `gibs` atlas group, the four fd_blood_* the `blood` group.
auto is_gibs( const field_type_id &type ) -> bool
{
    return type == fd_gibs_flesh || type == fd_gibs_veggy || type == fd_gibs_insect
           || type == fd_gibs_invertebrate;
}

auto is_blood( const field_type_id &type ) -> bool
{
    return type == fd_blood || type == fd_blood_veggy || type == fd_blood_insect
           || type == fd_blood_invertebrate;
}

/// Shared stamp generator for queue_splatter and seed_submap. `key` is the
/// caller's already-resolved submap key (seed_submap has it for the whole 12x12
/// block, so re-deriving it per tile would be pure waste). Returns false when
/// the splatmap is unavailable, so callers can skip the rest of their work.
auto queue_stamps( const tripoint_bub_ms &tile, const field_type_id &type, int intensity,
                   std::uint64_t key ) -> bool
{
    if( !g || !tilecontext ) {
    return false;
}
lighting::splatmap_pass &sp = lighting::get_render_state().splatmap();
if( !sp.ready() ) {
        return false;
    }
    if( !covers_field( type ) ) {
        return true;
    }

    // Pick the atlas group; fall back to the other when the chosen one is empty,
    // and do nothing when both are.
    bool gibs = is_gibs( type );
    if( sp.cell_count( gibs ) == 0 ) {
        gibs = !gibs;
    }
    const int count = sp.cell_count( gibs );
    if( count == 0 ) {
    return true;
}
const int base = sp.cell_base( gibs );

const tripoint_abs_ms abs = get_map().bub_to_abs( tile );

// Local tile within the submap. Bub coords run 0..mapsize*SEEX, so both are
// already non-negative.
const int lx = tile.x() % SEEX;
const int ly = tile.y() % SEEY;

// Deterministic per-tile seed: the same field data regenerates the same
// pattern after a save/reload.
const std::size_t seed = std::hash<long long> {}(
                             ( static_cast<long long>( abs.x() ) * 73'856'093LL )
                                 ^ ( static_cast<long long>( abs.y() ) * 19'349'663LL )
                                 ^ ( static_cast<long long>( abs.z() ) * 83'492'791LL )
                                 ^ ( static_cast<long long>( type.to_i() ) * 2'654'435'761LL ) );
    splat_rng.seed( static_cast<std::mt19937::result_type>( seed ) );

    constexpr float px_per_tile = static_cast<float>( lighting::SPLAT_PX_PER_TILE );
    const int n = std::clamp( intensity, 1, 3 );
    for( int i = 0; i < n; ++i ) {
    lighting::splat_stamp s{};
    s.px = static_cast<float>( lx ) * px_per_tile + px_per_tile * 0.5f
               + uniform( -0.4f * px_per_tile, 0.4f * px_per_tile );
        s.py = static_cast<float>( ly ) * px_per_tile + px_per_tile * 0.5f
               + uniform( -0.4f * px_per_tile, 0.4f * px_per_tile );
        s.size = px_per_tile * uniform( 0.7f, 1.3f );
        s.rotation = uniform( 0.f, 2.f * std::numbers::pi_v<float> );
        // Later stamps in a burst fade, so overlap reads as spray rather than a
        // uniform blob.
        s.alpha = uniform( 0.6f, 1.0f ) * ( 1.0f - 0.25f * static_cast<float>( i ) );
        s.mask_r = 1.f;
        s.mask_g = 0.f;
        s.mask_b = 0.f;
        s.cell = base + static_cast<int>( splat_rng() % static_cast<unsigned>( count ) );
        sp.add_stamp( key, s );
    }
    return true;
}

} // namespace

auto key_of( const tripoint_abs_sm &sm ) -> std::uint64_t
{
    return ( static_cast<std::uint64_t>( sm.x() + 0x80'0000 ) << 40 )
    | ( static_cast<std::uint64_t>( sm.y() + 0x80'0000 ) << 16 )
    | static_cast<std::uint64_t>( sm.z() + 10 );
}

auto active() -> bool
{
    return g_splatmap_enable && !tile_iso && tilecontext != nullptr
           && lighting::get_render_state().splatmap().ready();
}

auto covers_field( const field_type_id &type ) -> bool
{
    return is_blood( type ) || is_gibs( type );
}

auto queue_splatter( const tripoint_bub_ms &where, const field_type_id &type,
                     int intensity ) -> void
{
    if( !g || !tilecontext ) {
    return;
}
const tripoint_abs_sm sm = project_to<coords::sm>( get_map().bub_to_abs( where ) );
    queue_stamps( where, type, intensity, key_of( sm ) );
}

auto seed_submap( const tripoint_bub_ms &origin, std::uint64_t key ) -> void
{
    if( !g || !tilecontext ) {
    return;
}
if( !lighting::get_render_state().splatmap().ready() ) {
        return;
    }
    // Pointers, not values: these globals are assigned by
    // field_types::set_field_type_ids() during load, so a by-value static would
    // snapshot empty ids at static-init time.
    static const std::array<const field_type_id *, 8> covered = {
        &fd_blood, &fd_blood_veggy, &fd_blood_insect, &fd_blood_invertebrate,
        &fd_gibs_flesh, &fd_gibs_veggy, &fd_gibs_insect, &fd_gibs_invertebrate,
    };

    map &here = get_map();
    for( int dy = 0; dy < SEEY; ++dy ) {
    for( int dx = 0; dx < SEEX; ++dx ) {
            const tripoint_bub_ms tile( origin.x() + dx, origin.y() + dy, origin.z() );
            if( !here.inbounds( tile ) ) {
                continue;
            }
            const field &f = here.field_at( tile );
            if( f.field_count() == 0 ) {
                continue;
            }
            for( const field_type_id *type : covered ) {
                if( const field_entry *fe = f.find_field( *type ) ) {
                    queue_stamps( tile, *type, fe->get_field_intensity(), key );
                }
            }
        }
    }
}

} // namespace splatmap
