#include "hud_radar.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "avatar.h"
#include "cached_options.h"
#include "character.h"
#include "creature.h"
#include "enums.h"
#include "game.h"
#include "map.h"
#include "mapdata.h"
#include "monster.h"
#include "npc.h"
#include "options.h"
#include "point.h"
#include "sdl_lighting_devui.h"
#include "sdl_window_dims.h"
#include "vehicle.h"

#include "lighting/render_state.h"
#include "lighting/rmlui_layer.h"

/// The radar's world layer stays inside the HUD's single-hue luminance ladder, so
/// the whole map reads correctly in greyscale. Creature blips are the ONE place a
/// hue is spent, and each also carries a redundant shape cue (square / horizontal
/// bar / vertical bar / cross), so nothing at all is encoded by hue alone.

namespace
{

/// Set at the end of every `hud_radar::draw`; read by `requires_animation`.
bool g_wants_anim = false;

/// What a radar tile is. `num` is the table bound, never a category.
enum class cat : int {
    none = 0,
    soil,
    paved,
    floor_in,
    water,
    vegetation,
    furniture,
    stairs,
    wall,
    opening,
    vehicle,
    num
};

/// Dot geometry inside one `pitch x pitch` grid cell. `full` fills the cell, so
/// walls butt together into continuous building outlines; `dot` leaves a gutter;
/// `half` is bottom-aligned and reads as a surface rather than an object.
enum class dot_shape : int { none = 0, full, dot, half };

struct dot_style {
    hud_phosphor::ink rung = hud_phosphor::ink::dead;
    dot_shape form = dot_shape::none;
};

/// (rung, dot_shape) per category. Every pair is unique in BOTH channels, so the
/// world layer survives being desaturated AND survives being flattened to one
/// luminance — that is what lets it carry ten distinctions without a second hue.
///
/// Read down the `dot` column and the ladder is a ramp from natural to built:
/// soil, paving, indoor floor, furniture, stairs. Splitting bare soil from paving
/// is what makes a road legible as a road: a street now reads one rung brighter
/// than the verge beside it, so the lattice shows the street grid instead of an
/// undifferentiated field of dim dots — and a continuous, recognisable road gives
/// the eye the scale reference that tells it how far across the viewport is.
constexpr std::array<dot_style, static_cast<std::size_t>( cat::num )> world_table = { {
        { hud_phosphor::ink::dead,  dot_shape::none },  // none — nothing drawn
        { hud_phosphor::ink::dead,  dot_shape::dot  },  // soil (grass, dirt, sand)
        { hud_phosphor::ink::rule,  dot_shape::dot  },  // paved (road, walk, concrete)
        { hud_phosphor::ink::label, dot_shape::dot  },  // floor_in (roofed)
        { hud_phosphor::ink::dead,  dot_shape::half },  // water
        { hud_phosphor::ink::rule,  dot_shape::half },  // vegetation
        { hud_phosphor::ink::datum, dot_shape::dot  },  // furniture
        { hud_phosphor::ink::peak,  dot_shape::dot  },  // stairs
        { hud_phosphor::ink::datum, dot_shape::full },  // wall
        { hud_phosphor::ink::peak,  dot_shape::full },  // opening (door/window)
        { hud_phosphor::ink::label, dot_shape::full },  // vehicle
    }
};

/// Largest odd number `<= n`, floored at 1. An odd tile count is what puts the
/// player's own tile exactly in the middle cell rather than on a seam.
auto odd_at_most( int n ) -> int
{
    return std::max( n % 2 == 0 ? n - 1 : n, 1 );
}

/// Pixels of empty margin around a `dot`, from `PIXEL_MINIMAP_MODE`.
auto gutter_for_mode() -> int
{
    const auto mode = get_option<std::string>( "PIXEL_MINIMAP_MODE" );
    if( mode == "solid" ) {
        return 0;
    }
    if( mode == "squares" ) {
        return 1;
    }
    return 2;
}

/// Everything about a tile's category that depends ONLY on its terrain type.
enum class tclass : int { empty = 0, stairs, opening, wall, water, vegetation, air, soil, paved };

/// First match wins. Ordered by how much each fact matters to a player reading the
/// map at a glance: level changes, then openings, then structure, then surface.
auto terrain_class_of( const ter_t &t ) -> tclass
{
    if( t.is_null() ) {
        return tclass::empty;
    }
    const auto wet = t.has_flag( TFLAG_SWIMMABLE ) || t.has_flag( TFLAG_LIQUID ) ||
                     t.has_flag( TFLAG_DEEP_WATER );
    // Water is excluded from the level-change test rather than tested after it,
    // because deep water carries GOES_UP/GOES_DOWN so a swimmer can surface and
    // dive: 19 terrains do, `t_lake_bed`, `t_ocean_bed` and `t_water_dp` among
    // them. Taking the flags at face value painted every lake as a staircase at
    // `ink::peak` — the brightest rung on the ladder, for the least structure on
    // the map. An underwater staircase consequently reads as water, which is the
    // right trade: one rare tile loses its marker, every lake stops blazing.
    if( !wet && ( t.has_flag( TFLAG_GOES_UP ) || t.has_flag( TFLAG_GOES_DOWN ) ||
                  t.has_flag( TFLAG_RAMP ) ) ) {
        return tclass::stairs;
    }
    // No flag marks a door or a window; a door is exactly a terrain that has an
    // open or close transform, which is also what makes gates and hatches read
    // correctly here.
    if( !t.open.is_null() || !t.close.is_null() ) {
        return tclass::opening;
    }
    if( t.has_flag( TFLAG_WALL ) ) {
        // A wall you can see through IS a window. Checked before `wet` so a
        // submerged wall still reads as the structure it is.
        return t.transparent ? tclass::opening : tclass::wall;
    }
    if( wet ) {
        return tclass::water;
    }
    if( t.has_flag( TFLAG_TREE ) || t.has_flag( TFLAG_SHRUB ) ) {
        return tclass::vegetation;
    }
    if( t.has_flag( TFLAG_NO_FLOOR ) ) {
        return tclass::air;
    }
    // `ROAD` is the flag every hard man-made surface carries — pavement, sidewalk,
    // concrete, metal and board floors — and that no soil, grass or sand carries.
    // It has no cached `ter_bitflags` entry, so it costs a `std::set<std::string>`
    // lookup, which is the other half of why this function is memoised.
    return t.has_flag( "ROAD" ) ? tclass::paved : tclass::soil;
}

/// `terrain_class_of`, memoised per terrain type.
///
/// Terrain definitions are immutable once a world is loaded, so the whole flag
/// battery above runs once per `ter_id` instead of once per tile — roughly 4200
/// times per redraw at the default viewport. The cache is keyed on the catalogue
/// size, which changes when a differently-modded world is loaded.
auto terrain_class( const ter_id &id ) -> tclass
{
    static std::vector<tclass> memo;
    const auto n = ter_t::count();
    if( memo.size() != n ) {
        memo.resize( n );
        for( std::size_t k = 0; k < n; ++k ) {
            memo[k] = terrain_class_of( ter_id( static_cast<int>( k ) ).obj() );
        }
    }
    const auto i = static_cast<std::size_t>( id.to_i() );
    return i < memo.size() ? memo[i] : tclass::empty;
}

/// Resolve one tile: its terrain class, plus the two facts that are per-tile
/// rather than per-type. Furniture outranks the surface it stands on but never
/// structure, and a roof turns any walkable surface into an interior floor.
auto classify( const map &m, const tripoint_bub_ms &p, bool indoors ) -> cat
{
    const auto tc = terrain_class( m.ter( p ) );
    switch( tc ) {
        case tclass::empty:   return cat::none;
        case tclass::stairs:  return cat::stairs;
        case tclass::opening: return cat::opening;
        case tclass::wall:    return cat::wall;
        default:              break;
    }
    if( m.has_furn( p ) ) {
        return cat::furniture;
    }
    switch( tc ) {
        case tclass::water:      return cat::water;
        case tclass::vegetation: return cat::vegetation;
        case tclass::air:        return cat::none;
        case tclass::paved:      return indoors ? cat::floor_in : cat::paved;
        default:                 return indoors ? cat::floor_in : cat::soil;
    }
}

/// Scale RGB by the brightness option, leaving alpha alone.
auto shade( const std::array<float, 4> &c, float bright ) -> std::array<float, 4>
{
    return { std::clamp( c[0] * bright, 0.0f, 1.0f ),
             std::clamp( c[1] * bright, 0.0f, 1.0f ),
             std::clamp( c[2] * bright, 0.0f, 1.0f ),
             c[3] };
}

/// Same, for the four `float[4]` outline-colour globals the blips share with the
/// in-world creature outlines.
auto shade( const float *c, float bright ) -> std::array<float, 4>
{
    return { std::clamp( c[0] * bright, 0.0f, 1.0f ),
             std::clamp( c[1] * bright, 0.0f, 1.0f ),
             std::clamp( c[2] * bright, 0.0f, 1.0f ),
             1.0f };
}

/// One grid cell of the world layer, filled by the terrain sweep and then
/// possibly overwritten by the vehicle stamp.
struct grid_cell {
    cat c = cat::none;
    float alpha = 0.0f;
};

} // namespace

auto hud_radar::draw( const avatar &u, const hud_phosphor::layout &l ) -> void
{
    g_wants_anim = false;
    if( !pixel_minimap_option || l.radar.rows <= 0 || l.radar.cols <= 1 || g == nullptr ) {
        return;
    }
    auto &rs = lighting::get_render_state();
    if( !rs.ready() ) {
        return;
    }

    // dp -> LOGICAL window pixels. `hud_dp_ratio` multiplies in the HiDPI density
    // as well because it feeds RmlUi, which works in physical pixels; the UI quad
    // layer projects at the logical window size, so only the user scale applies.
    auto scale = rmlui_layer::ui_scale();
    if( !( scale > 0.0f ) ) {
        scale = 1.0f;
    }

    const auto r = hud_phosphor::to_dp( l.m, l.radar );
    // The region's play-area-facing edge carries the producer's box-drawing
    // vertical; the dot field is everything on the other side of it. When the
    // sidebar is mirrored to the left the vertical moves to the far cell, so the
    // interior starts at the region's own origin instead.
    const auto border_on_left = l.radar.col > 0;
    const auto ix = ( r.x + ( border_on_left ? l.m.cell_w : 0.0f ) ) * scale;
    const auto iy = r.y * scale;
    const auto iw = ( r.w - l.m.cell_w ) * scale;
    const auto ih = r.h * scale;
    if( iw < 8.0f || ih < 8.0f ) {
        return;
    }

    // 65 tiles is the design span; the pitch falls out of it and the tile counts
    // fall out of the pitch, so the field always fills its region exactly.
    constexpr auto design_span = 65.0f;
    const auto pitch = std::max( 2,
                                 static_cast<int>( std::floor( std::min( iw, ih ) / design_span ) ) );
    const auto pitch_f = static_cast<float>( pitch );
    const auto tiles_x = std::clamp( odd_at_most( static_cast<int>( std::floor( iw / pitch_f ) ) ), 1,
                                     129 );
    const auto tiles_y = std::clamp( odd_at_most( static_cast<int>( std::floor( ih / pitch_f ) ) ), 1,
                                     129 );
    const auto ox = ix + ( iw - static_cast<float>( tiles_x ) * pitch_f ) * 0.5f;
    const auto oy = iy + ( ih - static_cast<float>( tiles_y ) * pitch_f ) * 0.5f;

    // Queue order is paint order on this layer, so the ground goes first: a
    // luminance-only encoding needs a background whose luminance it controls, and
    // the RmlUi document above deliberately leaves this region unveiled.
    const auto ground = hud_phosphor::rgba( hud_phosphor::ink::ground );
    rs.queue_ui_rect( ix, iy, iw, ih, ground[0], ground[1], ground[2], 0.92f );

    const auto bright = std::clamp(
                            static_cast<float>( get_option<int>( "PIXEL_MINIMAP_BRIGHTNESS" ) ) / 100.0f, 0.0f, 3.0f );
    auto world_col = std::array<std::array<float, 4>, static_cast<std::size_t>( cat::num )> {};
    for( std::size_t k = 0; k < world_col.size(); ++k ) {
        world_col[k] = shade( hud_phosphor::rgba( world_table[k].rung ), bright );
    }
    const auto gutter = gutter_for_mode();

    const auto emit = [&]( int gx, int gy, dot_shape form, const std::array<float, 4> &col,
    float alpha ) {
        const auto cx = ox + static_cast<float>( gx ) * pitch_f;
        const auto cy = oy + static_cast<float>( gy ) * pitch_f;
        switch( form ) {
            case dot_shape::full:
                rs.queue_ui_rect( cx, cy, pitch_f, pitch_f, col[0], col[1], col[2], alpha );
                break;
            case dot_shape::dot: {
                // The `max(1, gutter)` floor is deliberate: in `solid` mode a dot
                // would otherwise equal a full and the shape channel would
                // collapse, making wall and furniture indistinguishable.
                const auto d = static_cast<float>( std::max( 1, pitch - std::max( 1, gutter ) ) );
                const auto off = ( pitch_f - d ) * 0.5f;
                rs.queue_ui_rect( cx + off, cy + off, d, d, col[0], col[1], col[2], alpha );
                break;
            }
            case dot_shape::half: {
                const auto h = static_cast<float>( std::max( 1, pitch / 2 ) );
                rs.queue_ui_rect( cx, cy + pitch_f - h, pitch_f, h, col[0], col[1], col[2], alpha );
                break;
            }
            case dot_shape::none:
            default:
                break;
        }
    };

    map &m = get_map();
    const auto centre = u.bub_pos();
    const auto z = centre.z();
    const level_cache &cache = m.get_cache_ref( z );
    const auto &vis_vars = m.get_visibility_variables_cache();
    const auto show_memory = u.should_show_map_memory();
    const auto half_x = tiles_x / 2;
    const auto half_y = tiles_y / 2;

    // Reused across frames: the field is redrawn wholesale every redraw, and a
    // 129x129 buffer is not worth reallocating each time.
    static std::vector<grid_cell> grid;
    grid.assign( static_cast<std::size_t>( tiles_x ) * static_cast<std::size_t>( tiles_y ),
                 grid_cell{} );

    for( auto gy = 0; gy < tiles_y; ++gy ) {
        const auto wy = centre.y() - half_y + gy;
        for( auto gx = 0; gx < tiles_x; ++gx ) {
            const auto wx = centre.x() - half_x + gx;
            if( !cache.inbounds( point_bub_ms( wx, wy ) ) ) {
                continue;
            }
            const auto flat = cache.idx( wx, wy );
            const auto vis = m.get_visibility( cache.visibility_cache[flat], vis_vars );
            const auto seen_now = vis == VIS_CLEAR || vis == VIS_LIT || vis == VIS_BOOMER;
            // NOTE the different stride: outside_cache/visibility_cache are
            // `x * cache_y + y`, map_memory_seen_cache is `x + y * cache_x`.
            // Mixing them silently transposes the fog of war.
            const auto remembered = show_memory &&
                                    cache.map_memory_seen_cache[static_cast<std::size_t>( wx + wy * cache.cache_x )];
            if( !seen_now && !remembered ) {
                continue;
            }
            const auto indoors = cache.outside_cache[flat] == 0;
            grid[static_cast<std::size_t>( gy ) * tiles_x + gx] = {
                .c = classify( m, tripoint_bub_ms( wx, wy, z ), indoors ),
                // Two tiers only, and the dim is applied to alpha rather than to
                // the rung, so the ladder's ordering survives inside each tier.
                .alpha = seen_now ? 1.0f : 0.45f,
            };
        }
    }

    // Vehicles are stamped from the level cache's vehicle list — O(parts), not
    // O(map) — and only over cells that already passed the visibility gate. That
    // gate is `alpha > 0`, NOT `c != none`: `classify` legitimately returns `none`
    // for a visible tile that is open air, and a gantry over a shaft is exactly
    // the case where the vehicle is the only thing worth drawing there.
    for( vehicle *v : cache.vehicle_list ) {
        if( v == nullptr ) {
            continue;
        }
        for( const tripoint_abs_ms &ap : v->get_points() ) {
            if( ap.z() != z ) {
                continue;
            }
            const auto bp = m.abs_to_bub( ap );
            const auto gx = bp.x() - centre.x() + half_x;
            const auto gy = bp.y() - centre.y() + half_y;
            if( gx < 0 || gx >= tiles_x || gy < 0 || gy >= tiles_y ) {
                continue;
            }
            auto &cell = grid[static_cast<std::size_t>( gy ) * tiles_x + gx];
            if( cell.alpha > 0.0f ) {
                cell.c = cat::vehicle;
            }
        }
    }

    for( auto gy = 0; gy < tiles_y; ++gy ) {
        for( auto gx = 0; gx < tiles_x; ++gx ) {
            const auto &cell = grid[static_cast<std::size_t>( gy ) * tiles_x + gx];
            const auto k = static_cast<std::size_t>( cell.c );
            emit( gx, gy, world_table[k].form, world_col[k], cell.alpha );
        }
    }

    // ── Creature blips ──────────────────────────────────────────────────────
    // The only hue in the register, reusing the in-world creature-outline colours
    // so a blip matches the outline on the same creature and stays F4-tunable.
    const auto beacon = std::clamp( get_option<int>( "PIXEL_MINIMAP_BEACON_SIZE" ), 1, 4 ) * pitch;
    const auto thin = std::max( 1, beacon / 2 );
    const auto blink = get_option<int>( "PIXEL_MINIMAP_BLINK" );
    const auto blink_on = blink <= 0 ||
                          ( get_sdl_ticks() / ( static_cast<std::uint64_t>( blink ) * 200u ) ) % 2 == 0;

    const auto quad = [&]( int gx, int gy, int w, int h, const std::array<float, 4> &col ) {
        const auto cx = ox + ( static_cast<float>( gx ) + 0.5f ) * pitch_f;
        const auto cy = oy + ( static_cast<float>( gy ) + 0.5f ) * pitch_f;
        rs.queue_ui_rect( cx - static_cast<float>( w ) * 0.5f, cy - static_cast<float>( h ) * 0.5f,
                          static_cast<float>( w ), static_cast<float>( h ), col[0], col[1], col[2], col[3] );
    };

    auto hostile_in_range = false;
    const auto blip = [&]( const Creature & critter ) {
        const auto p = critter.bub_pos();
        if( p.z() != z || !u.sees( critter ) ) {
            return;
        }
        const auto gx = p.x() - centre.x() + half_x;
        const auto gy = p.y() - centre.y() + half_y;
        if( gx < 0 || gx >= tiles_x || gy < 0 || gy >= tiles_y ) {
            return;
        }
        // The renderer's convention, matching outline_color_for's callers.
        switch( critter.attitude_to( u ) ) {
            case Attitude::A_HOSTILE:
                hostile_in_range = true;
                if( blink_on ) {
                    quad( gx, gy, beacon, beacon, shade( g_outline_col_hostile, bright ) );
                }
                break;
            case Attitude::A_FRIENDLY:
                quad( gx, gy, thin, beacon, shade( g_outline_col_friendly, bright ) );
                break;
            case Attitude::A_NEUTRAL:
            default:
                quad( gx, gy, beacon, thin, shade( g_outline_col_neutral, bright ) );
                break;
        }
    };

    for( const monster &mon : g->all_monsters() ) {
        blip( mon );
    }
    for( const npc &guy : g->all_npcs() ) {
        blip( guy );
    }

    // The player last, so nothing can bury it: a cross, which no creature blip uses.
    const auto self = shade( g_outline_col_self, bright );
    quad( half_x, half_y, beacon + 2, thin, self );
    quad( half_x, half_y, thin, beacon + 2, self );

    g_wants_anim = blink > 0 && hostile_in_range;
}

auto hud_radar::requires_animation() -> bool
{
    return g_wants_anim;
}
