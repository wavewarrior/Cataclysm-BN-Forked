#include "cata_tiles.h"
#include "cata_tiles_internal.h"

#include "character.h"
#include "avatar.h"
#include "catacharset.h"
#include "character_state_provider.h"
#include "field.h"
#include "field_type.h"
#include "submap.h"
#include "veh_type.h"
#include "creature.h"
#include "game.h"
#include "flag.h"
#include "item.h"
#include "item_factory.h"
#include "map.h"
#include "mtype.h"
#include "monster.h"
#include "options.h"
#include "player.h"
#include "mod_tileset.h"
#include "splatmap_stamps.h"
#include "trap.h"
#include "type_id.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace cata_tiles_internal;

static const efftype_id effect_ridden( "ridden" );

static const trait_id trait_INATTENTIVE( "INATTENTIVE" );

static const std::string ZOMBIE_REVIVAL_INDICATOR( "zombie_revival_indicator" );

// sprite.vert applies a wind breeze to the canopy (read from sprite_instance.pad1).
// Templated so it works for both ter_t and furn_t (shared has_flag base).
template <typename T> static float foliage_sway_weight( const T& obj )
{
    if( obj.has_flag( TFLAG_TREE ) ) { return 0.5f; }
    if( obj.has_flag( TFLAG_YOUNG ) ) { return 0.8f; }
    if( obj.has_flag( TFLAG_SHRUB ) ) { return 1.0f; }
    if( obj.has_flag( TFLAG_TALL_GRASS ) ) { return 0.6f; }
    return 0.0f;
}

// sprite.frag applies a vertical-face arc scaled by this (sprite_instance::face_amt):
// the surface normal tilts toward the viewer at the sprite's base and away at its top,
// so a vertical surface finally responds to light DIRECTION instead of merely getting
// uniformly brighter. 0 = flat/horizontal, which is every other sprite in the game and
// is why the arc is provably inert outside these two functions.
//
// TFLAG_WALL is the solid vertical face. A wall you can SEE THROUGH is glazing rather
// than masonry (t_wall_glass, t_reinforced_glass), so it takes the reduced window
// amount — the same `wall && transparent` distinction hud_radar.cpp:140-143 draws.
//
// Windows themselves are NOT walls: t_window's flags are TRANSPARENT / CONNECT_TO_WALL /
// WINDOW with no WALL, so a TFLAG_WALL test alone misses every one of them. "WINDOW" has
// no ter_bitflags entry, so testing it costs a std::set<std::string> lookup; gating that
// behind the cached TFLAG_CONNECT_TO_WALL bit rejects all ground, vegetation and floor
// terrain first and loses nothing. Verified against every terrain definition in
// data/json: of the 25 WINDOW-flagged terrains, the 6 that lack CONNECT_TO_WALL are
// exactly the taped ones, which carry WALL and are already caught above.
static auto terrain_face_amount( const ter_t &t ) -> float
{
    if( t.has_flag( TFLAG_WALL ) ) {
        return t.transparent ? 0.6f : 1.0f;
    }
    if( t.has_flag( TFLAG_CONNECT_TO_WALL ) && t.has_flag( "WINDOW" ) ) {
        return 0.6f;
    }
    return 0.0f;
}

// Furniture carries no "is a vertical surface" flag, so its face amount is read off
// `coverage` — the sight-blocking percentage every furniture definition already has,
// documented at mapdata.h:486 as "<30 won't cover from sight". Below that threshold the
// furniture is flat enough to leave alone (rugs, mats, low tables); above it the amount
// ramps to a moderate 0.5, because even a full bookcase presents a less clean vertical
// face than a wall does. Deriving it this way covers new furniture the day it is
// defined, with no id list to maintain.
static auto furniture_face_amount( const furn_t &f ) -> float
{
    constexpr float sight_cover_min = 30.0f;
    constexpr float max_amount = 0.5f;
    const float ramp = ( static_cast<float>( f.coverage ) - sight_cover_min )
                       / ( 100.0f - sight_cover_min );
    return max_amount * std::clamp( ramp, 0.0f, 1.0f );
}

bool cata_tiles::draw_terrain(
    const tripoint_bub_ms& p, const lit_level ll, int &height_3d, const bool ( &invisible )[5],
    int z_drop )
{
    // Step 2 (grid-decoupled lighting): terrain/furniture/vparts are the sprites whose
    // alpha defines the ground occluders, so only these three layers feed the SDF seed.
    const occluder_capture_guard occ_guard( *this );
    // Bash-shake transform for this tile (identity when none/expired; self-resetting per call).
    active_anim_xform_ = tile_hit_xform( p );
    map& here = get_map();
    const auto override = terrain_override.find( p );
    const bool overridden = override != terrain_override.end();
    bool neighborhood_overridden = overridden;
    if( !neighborhood_overridden ) {
        for( point dir : neighborhood ) {
            if( terrain_override.contains( p + dir ) ) {
                neighborhood_overridden = true;
                break;
            }
        }
    }

    const ter_id& t = here.ter( p );
    const auto [bgCol, fgCol] = get_terrain_color( t.obj(), here, p );

    // Foliage sway: tag swayable vegetation so sprite.vert breezes the canopy.
    // Gated by the ANIMATIONS option (idle_animations.enabled()), and marking it
    // present keeps the redraw loop repainting so the breeze actually animates.
    const float ter_sway = ( t && idle_animations.enabled() ) ? foliage_sway_weight( *t ) : 0.0f;
    if( ter_sway > 0.0f ) { idle_animations.mark_present(); }
    // Vertical-face arc: walls and windows are the surfaces that should respond to
    // light DIRECTION rather than just brightness. Unconditional (no option gate) —
    // it is shading, not animation — and 0 for everything that is not a wall.
    const float ter_face = t ? terrain_face_amount( *t ) : 0.0f;

    // first memorize the actual terrain
    if( t && !invisible[0] ) {
        int subtile = 0;
        int rotation = 0;
        int connect_group = 0;
        if( t.obj().connects( connect_group ) ) {
            get_connect_values( p, subtile, rotation, connect_group, {} );
            // re-memorize previously seen terrain in case new connections have been seen
            here.set_memory_seen_cache_dirty( p );
        } else {
            get_terrain_orientation( p, rotation, subtile, {}, invisible );
            // do something to get other terrain orientation values
        }
        const std::string& tname = t.id().str();
        if( here.check_seen_cache( p ) ) {
            if( !t->has_flag( TFLAG_NO_MEMORY ) && !t->has_flag( TFLAG_Z_TRANSPARENT ) ) {
                g->u.memorize_tile( here.bub_to_abs( p ), tname, subtile, rotation );
                g->u.memorize_terrain_tile( here.bub_to_abs( p ), tname, subtile, rotation );
            } else {
                g->u.clear_memorized_tile( here.bub_to_abs( p ) );
            }
        }
        // draw the actual terrain if there's no override
        if( !neighborhood_overridden ) {
            // Open air is used for holes / sky. Drawing a cyan marker here can bleed through
            // semi-transparent sprites (e.g. explosion smoke) and look like stuck artifacts.
            // If a tileset provides an explicit tile for it, use that; otherwise draw nothing.
            if( t == t_open_air ) {
                if( tileset_ptr && tileset_ptr->find_tile_type( tname ) ) {
                    const auto tile = tile_search_params{tname, C_TERRAIN, empty_string, 0, 0};
                    return draw_from_id_string(
                               tile, p, bgCol, fgCol, ll, true, z_drop, false, height_3d );
                }
                return true;
            }

            const auto tile = tile_search_params{
                .id = tname,
                .category = C_TERRAIN,
                .subcategory = empty_string,
                .subtile = subtile,
                .rota = rotation};
            // If this terrain is foliage-swayable AND a dedicated canopy overlay
            // sprite (_canopy suffix) exists in the tileset, draw the base
            // terrain completely static and let the overlay carry the sway.
            // Otherwise fall back to the bg/fg split (draw_sprite_at enforces
            // sway=0 on the bg layer so ground stays static either way).
            std::string canopy_id;
            if( ter_sway > 0.0f && tileset_ptr
                && tileset_ptr->find_tile_type( canopy_id = tname + "_canopy" ) ) {
                draw_from_id_string(
                    tile, p, bgCol, fgCol, ll, true, z_drop, false, height_3d, 0.0f, ter_face );
                const tile_search_params canopy{canopy_id, C_TERRAIN, empty_string, 0, 0};
                draw_from_id_string(
                    canopy, p, bgCol, fgCol, ll, true, z_drop, false, height_3d, ter_sway, 0.0f );
            } else {
                draw_from_id_string(
                    tile, p, bgCol, fgCol, ll, true, z_drop, false, height_3d, ter_sway, ter_face );
            }
            return true;
        }
    }
    if( invisible[0] ? overridden : neighborhood_overridden ) {
        // and then draw the override terrain
        const ter_id& t2 = overridden ? override->second : t;
        if( t2 ) {
            // both the current and neighboring overrides may change the appearance
            // of the tile, so always re-calculate it.
            int subtile = 0;
            int rotation = 0;
            int connect_group = 0;
            if( t2.obj().connects( connect_group ) ) {
                get_connect_values( p, subtile, rotation, connect_group, terrain_override );
            } else {
                get_terrain_orientation( p, rotation, subtile, terrain_override, invisible );
            }
            const std::string& tname = t2.id().str();
            // tile overrides are never memorized
            // tile overrides are always shown with full visibility
            const lit_level lit = overridden ? lit_level::LIT : ll;
            const bool nv = !overridden;
            const tile_search_params tile{tname, C_TERRAIN, empty_string, subtile, rotation};
            return draw_from_id_string( tile, p, bgCol, fgCol, lit, nv, z_drop, false, height_3d,
                                        0.0f, terrain_face_amount( *t2 ) );
        }
    } else if( invisible[0] ) {
        // try drawing memory if invisible and not overridden
        const auto ret = get_ter_memory_at( p );
        if( ret.has_value() ) {
            const auto& [tile_id, subtile, rotation] = ret.value();
            const tile_search_params tile{tile_id, C_TERRAIN, empty_string, subtile, rotation};
            return draw_from_id_string(
                       tile, p, bgCol, fgCol, lit_level::MEMORIZED, true, z_drop, false, height_3d );
        }
    }
    return false;
}

bool cata_tiles::draw_furniture(
    const tripoint_bub_ms& p, const lit_level ll, int &height_3d, const bool ( &invisible )[5],
    int z_drop )
{
    const occluder_capture_guard occ_guard( *this );
    // Bash-shake transform for this tile (identity when none/expired; self-resetting per call).
    active_anim_xform_ = tile_hit_xform( p );
    const auto override = furniture_override.find( p );
    const bool overridden = override != furniture_override.end();
    bool neighborhood_overridden = overridden;
    if( !neighborhood_overridden ) {
        for( point dir : neighborhood ) {
            if( furniture_override.contains( p + dir ) ) {
                neighborhood_overridden = true;
                break;
            }
        }
    }

    const map& here = get_map();
    const furn_id& f = here.furn( p );
    const auto [bgCol, fgCol] = get_furniture_color( f.obj(), here, p );

    // Foliage sway: tag swayable furniture vegetation (gated by ANIMATIONS).
    const float furn_sway = ( f && idle_animations.enabled() ) ? foliage_sway_weight( *f ) : 0.0f;
    if( furn_sway > 0.0f ) { idle_animations.mark_present(); }
    // Vertical-face arc, scaled off sight coverage — see furniture_face_amount.
    const float furn_face = f ? furniture_face_amount( *f ) : 0.0f;

    // first memorize the actual furniture
    if( f && !invisible[0] ) {
        const int neighborhood[4] = {
            static_cast<int>( here.furn( p + point_south ) ),
            static_cast<int>( here.furn( p + point_east ) ),
            static_cast<int>( here.furn( p + point_west ) ),
            static_cast<int>( here.furn( p + point_north ) )
        };
        int subtile = 0;
        int rotation = 0;

        int connect_group = 0;
        if( f.obj().connects( connect_group ) ) {
            get_furn_connect_values( p, subtile, rotation, connect_group, {} );
        } else {
            get_tile_values_with_ter( p, f.to_i(), neighborhood, subtile, rotation );
        }

        const std::string& fname = f.id().str();
        if( here.check_seen_cache( p ) ) {
            g->u.memorize_tile( here.bub_to_abs( p ), fname, subtile, rotation );
        }
        // draw the actual furniture if there's no override
        if( !neighborhood_overridden ) {
            const tile_search_params tile{fname, C_FURNITURE, empty_string, subtile, rotation};
            // Same canopy-overlay pattern as draw_terrain: if a _canopy sprite
            // exists, draw the furniture static and let the overlay carry sway.
            std::string canopy_id_f;
            if( furn_sway > 0.0f && tileset_ptr
                && tileset_ptr->find_tile_type( canopy_id_f = fname + "_canopy" ) ) {
                draw_from_id_string(
                    tile, p, bgCol, fgCol, ll, true, z_drop, false, height_3d, 0.0f, furn_face );
                const tile_search_params canopy{canopy_id_f, C_FURNITURE, empty_string, 0, 0};
                draw_from_id_string(
                    canopy, p, bgCol, fgCol, ll, true, z_drop, false, height_3d, furn_sway, 0.0f );
            } else {
                draw_from_id_string(
                    tile, p, bgCol, fgCol, ll, true, z_drop, false, height_3d, furn_sway, furn_face );
            }
            return true;
        }
    }
    if( invisible[0] ? overridden : neighborhood_overridden ) {
        // and then draw the override furniture
        const furn_id& f2 = overridden ? override->second : f;
        if( f2 ) {
            // both the current and neighboring overrides may change the appearance
            // of the tile, so always re-calculate it.
            const auto furn = [&]( const tripoint_bub_ms & q, const bool invis ) -> furn_id {
                const auto it = furniture_override.find( q );
                return it != furniture_override.end() ? it->second
                                               : ( !overridden || !invis )
                                               ? here.furn( tripoint_bub_ms( q ) )
                                               : f_null;
            };
            const int neighborhood[4] = {
                static_cast<int>( furn( p + point_south, invisible[1] ) ),
                static_cast<int>( furn( p + point_east, invisible[2] ) ),
                static_cast<int>( furn( p + point_west, invisible[3] ) ),
                static_cast<int>( furn( p + point_north, invisible[4] ) )
            };
            int subtile = 0;
            int rotation = 0;

            int connect_group = 0;
            if( f.obj().connects( connect_group ) ) {
                get_furn_connect_values( p, subtile, rotation, connect_group, {} );
            } else {
                get_tile_values_with_ter( p, f.to_i(), neighborhood, subtile, rotation );
            }

            get_tile_values( f2.to_i(), neighborhood, subtile, rotation );
            const std::string& fname = f2.id().str();
            // tile overrides are never memorized
            // tile overrides are always shown with full visibility
            const lit_level lit = overridden ? lit_level::LIT : ll;
            const bool nv = !overridden;
            const tile_search_params tile{fname, C_FURNITURE, empty_string, subtile, rotation};
            return draw_from_id_string( tile, p, bgCol, fgCol, lit, nv, z_drop, false, height_3d,
                                        0.0f, furniture_face_amount( *f2 ) );
        }
    } else if( invisible[0] ) {
        // try drawing memory if invisible and not overridden
        const auto ret = get_furn_memory_at( p );
        if( ret.has_value() ) {
            const auto& [tile_id, subtile, rotation] = ret.value();
            const tile_search_params tile{tile_id, C_FURNITURE, empty_string, subtile, rotation};
            return draw_from_id_string(
                       tile, p, bgCol, fgCol, lit_level::MEMORIZED, true, z_drop, false, height_3d );
        }
    }
    return false;
}
bool cata_tiles::draw_trap(
    const tripoint_bub_ms& p, const lit_level ll, int &height_3d, const bool ( &invisible )[5],
    int z_drop )
{
    const auto override = trap_override.find( p );
    const bool overridden = override != trap_override.end();
    bool neighborhood_overridden = overridden;
    if( !neighborhood_overridden ) {
        for( point dir : neighborhood ) {
            if( trap_override.contains( p + dir ) ) {
                neighborhood_overridden = true;
                break;
            }
        }
    }

    map& here = get_map();
    const trap& tr = here.tr_at( p );
    const trap_id& tr_id = tr.loadid;
    auto [bgCol, fgCol] = get_trap_color( tr, here, p );

    // first memorize the actual trap
    if( tr_id && !invisible[0] && tr_id.obj().can_see( p, g->u ) ) {
        const int neighborhood[4] = {
            static_cast<int>( here.tr_at( p + point_south ).loadid ),
            static_cast<int>( here.tr_at( p + point_east ).loadid ),
            static_cast<int>( here.tr_at( p + point_west ).loadid ),
            static_cast<int>( here.tr_at( p + point_north ).loadid )
        };
        int subtile = 0;
        int rotation = 0;
        get_tile_values( tr_id.to_i(), neighborhood, subtile, rotation );
        const std::string trname = tr_id.id().str();
        if( here.check_seen_cache( p ) && tr_id != tr_ledge ) {
            g->u.memorize_tile( here.bub_to_abs( p ), trname, subtile, rotation );
        }
        // draw the actual trap if there's no override
        if( !neighborhood_overridden ) {
            const tile_search_params tile{trname, C_TRAP, empty_string, subtile, rotation};
            return draw_from_id_string( tile, p, bgCol, fgCol, ll, true, z_drop, false, height_3d );
        }
    }
    if( overridden || ( !invisible[0] && neighborhood_overridden && tr_id.obj().can_see( p, g->u ) ) ) {
        // and then draw the override trap
        const trap_id& tr2 = overridden ? override->second : tr_id;
        if( tr2 ) {
            // both the current and neighboring overrides may change the appearance
            // of the tile, so always re-calculate it.
            const auto tr_at = [&]( const tripoint_bub_ms & q, const bool invis ) -> trap_id {
                const auto it = trap_override.find( q );
                return it != trap_override.end() ? it->second
                                          : ( !overridden || !invis )
                                          ? here.tr_at( tripoint_bub_ms( q ) ).loadid
                                          : tr_null;
            };
            const int neighborhood[4] = {
                static_cast<int>( tr_at( p + point_south, invisible[1] ) ),
                static_cast<int>( tr_at( p + point_east, invisible[2] ) ),
                static_cast<int>( tr_at( p + point_west, invisible[3] ) ),
                static_cast<int>( tr_at( p + point_north, invisible[4] ) )
            };
            int subtile = 0;
            int rotation = 0;
            get_tile_values( tr2.to_i(), neighborhood, subtile, rotation );
            const std::string& trname = tr2.id().str();
            // tile overrides are never memorized
            // tile overrides are always shown with full visibility
            const lit_level lit = overridden ? lit_level::LIT : ll;
            const bool nv = !overridden;
            const tile_search_params tile{trname, C_TRAP, empty_string, subtile, rotation};
            return draw_from_id_string( tile, p, bgCol, fgCol, lit, nv, z_drop, false, height_3d );
        }
    } else if( invisible[0] ) {
        // try drawing memory if invisible and not overridden
        const auto ret = get_trap_memory_at( p );
        if( ret.has_value() ) {
            const auto& [tile_id, subtile, rotation] = ret.value();
            const tile_search_params tile2{tile_id, C_TRAP, empty_string, subtile, rotation};
            return draw_from_id_string(
                       tile2, p, bgCol, fgCol, lit_level::MEMORIZED, true, z_drop, false, height_3d );
        }
    }
    return false;
}

bool cata_tiles::draw_graffiti(
    const tripoint_bub_ms& p, const lit_level ll, int &height_3d, const bool ( &invisible )[5],
    int z_drop )
{
    const auto override = graffiti_override.find( p );
    const bool overridden = override != graffiti_override.end();
    if( overridden ? !override->second : ( invisible[0] || !get_map().has_graffiti_at( p ) ) ) {
        return false;
    }
    const lit_level lit = overridden ? lit_level::LIT : ll;

    const auto [bgCol, fgCol] = get_graffiti_color( get_map(), p );

    const tile_search_params tile{"graffiti", C_NONE, empty_string, 0, 0};
    return draw_from_id_string( tile, p, bgCol, fgCol, lit, false, z_drop, false, height_3d );
}

bool cata_tiles::draw_field_or_item(
    const tripoint_bub_ms& p, const lit_level ll, int &height_3d, const bool ( &invisible )[5],
    int z_drop )
{
    if( ( !fov_3d && z_drop > 0 ) || fov_3d_z_range < z_drop ) { return false; }
    const auto fld_override = field_override.find( p );
    const bool fld_overridden = fld_override != field_override.end();
    map& here = get_map();
    const field_type_id& fld =
        fld_overridden ? fld_override->second : here.field_at( p ).displayed_field_type();

    bool ret_draw_field = false;
    bool ret_draw_items = false;
    // The splatmap draws these field types as sub-tile decals, so their
    // grid-locked 32x32 tile sprite must not also render — otherwise every
    // splatter is double-drawn. splatmap::active() is the SHARED gate with the
    // composite in render_world_pass_w: if these two ever disagree the sprite is
    // hidden with nothing drawn in its place and blood goes invisible. Field
    // gameplay is untouched either way because mod_field_intensity still runs.
    if( ( fld_overridden || !invisible[0] ) && fld.obj().display_field
        && !( splatmap::active() && splatmap::covers_field( fld ) ) ) {
        const lit_level lit = fld_overridden ? lit_level::LIT : ll;
        const bool nv = !fld_overridden;

        auto field_at = [&]( const tripoint_bub_ms & q, const bool invis ) -> field_type_id {
            const auto it = field_override.find( q );
            return it != field_override.end() ? it->second
                                       : ( !fld_overridden || !invis )
                                       ? here.field_at( q ).displayed_field_type()
                                       : fd_null;
        };
        // for rotation information
        const int neighborhood[4] = {
            static_cast<int>( field_at( p + point_south, invisible[1] ) ),
            static_cast<int>( field_at( p + point_east, invisible[2] ) ),
            static_cast<int>( field_at( p + point_west, invisible[3] ) ),
            static_cast<int>( field_at( p + point_north, invisible[4] ) )
        };

        int subtile = 0;
        int rotation = 0;
        get_tile_values( fld.to_i(), neighborhood, subtile, rotation );

        const auto [bgCol, fgCol] = get_field_color( here.field_at( p ), here, p );

        const tile_search_params tile{fld.id().str(), C_FIELD, empty_string, subtile, rotation};
        ret_draw_field = draw_from_id_string( tile, p, bgCol, fgCol, lit, nv, z_drop, false );
    }
    if( fld.obj().display_items ) {
        const auto it_override = item_override.find( p );
        const bool it_overridden = it_override != item_override.end();

        tint_config bgCol;
        tint_config fgCol;

        itype_id it_id;
        mtype_id mon_id;
        bool hilite;
        const itype* it_type;
        if( it_overridden ) {
            it_id = std::get<0>( it_override->second );
            mon_id = std::get<1>( it_override->second );
            hilite = std::get<2>( it_override->second );
            it_type = &*it_id;
        } else if( !invisible[0] && here.sees_some_items( p, g->u ) ) {
            const maptile& tile = here.maptile_at( p );
            const item& itm = tile.get_uppermost_item();
            const mtype* const mon = itm.get_mtype();
            it_id = itm.typeId();
            mon_id = mon ? mon->id : mtype_id::NULL_ID();
            hilite = tile.get_item_count() > 1;
            it_type = itm.type;

            std::tie( bgCol, fgCol ) = get_item_color( itm, here, p );
        } else {
            it_type = nullptr;
            hilite = false;
        }

        if( it_type && !it_id.is_null() ) {
            const std::string disp_id =
                it_id == itype_corpse && mon_id ? "corpse_" + mon_id.str() : it_id.str();
            const std::string it_category = it_type->get_item_type_string();
            const lit_level lit = it_overridden ? lit_level::LIT : ll;
            const bool nv = !it_overridden;

            const tile_search_params tile{disp_id, C_ITEM, it_category, 0, 0};
            ret_draw_items =
                draw_from_id_string( tile, p, bgCol, fgCol, lit, nv, z_drop, false, height_3d );
            if( ret_draw_items && hilite ) { draw_item_highlight( p ); }
        }
    }
    return ret_draw_field && ret_draw_items;
}
bool cata_tiles::draw_vpart(
    const tripoint_bub_ms& p, lit_level ll, int &height_3d, const bool ( &invisible )[5],
    int z_drop )
{
    const occluder_capture_guard occ_guard( *this );
    const auto override = vpart_override.find( p );
    const bool overridden = override != vpart_override.end();
    map& here = get_map();
    // first memorize the actual vpart
    const optional_vpart_position vp = here.veh_at( p );

    auto [bgCol, fgCol] = get_vpart_color( vp, here, p );

    if( vp && !invisible[0] ) {
        const vehicle& veh = vp->vehicle();
        int veh_part = vp->part_index();

        // Gets the visible part, should work fine once tileset vp_ids are updated to work with the
        // vehicle part json ids get the vpart_id
        char part_mod = 0;
        const Creature* critter = g->critter_at( p, true );
        // Use the roof/top-down variant only for non-structural interior parts (floor, seat,
        // cargo etc.). If any part at this mount is an obstacle (board, windshield, door),
        // pass roof=false so part_displayed_at returns that obstacle part instead of the
        // on_roof tile. part_info(veh_part) only reflects the cached part (often the frame),
        // so search all parts at the position via part_with_feature.
        const bool has_obstacle_here = vp.part_with_feature( VPFLAG_OBSTACLE, false ).has_value();
        const bool use_roof_variant = z_drop > 0 && critter == nullptr && !has_obstacle_here;
        if( use_roof_variant ) {
            auto res = get_vpart_color( vp, here, p, true );
            bgCol = res.first;
            fgCol = res.second;
        }
        const vpart_id& vp_id = veh.part_id_string( veh_part, use_roof_variant, part_mod );
        const int subtile = part_mod == 1 ? open_ : part_mod == 2 ? broken : 0;
        const auto rotation = static_cast<int>(
                                  std::round( to_degrees( veh.part_display_direction( veh_part, use_roof_variant ) ) ) );
        const std::string vpname = "vp_" + vp_id.str();
        avatar& you = get_avatar();
        // Always clear while moving — the check_seen_cache gate would miss residual
        // stationary memory once map_memory_seen_cache is set by draw_from_id_string,
        // letting ghost tiles survive until the submap exits the reality bubble.
        // Always memorize while stationary so returning to a previous position
        // after a trip refreshes the tile rather than leaving it blank.
        if( veh.forward_velocity() ) {
            you.clear_memorized_overlay( here.bub_to_abs( p ) );
        } else {
            you.memorize_tile( here.bub_to_abs( p ), vpname, subtile, rotation );
        }
        if( !overridden ) {
            // Vehicle smooth render offset (Box2D sub-tile residual).
            if( !tile_iso ) {
                active_anim_xform_ = sprite_xform{
                    .off_x = veh.render_offset_x * static_cast<float>( tile_width ),
                    .off_y = veh.render_offset_y * static_cast<float>( tile_height ) };
            }
            const std::optional<vpart_reference> cargopart = vp.part_with_feature( "CARGO", true );
            const bool draw_highlight =
                cargopart && !veh.get_items( cargopart->part_index() ).empty();
            const tile_search_params tile =
            {vpname, C_VEHICLE_PART, empty_string, subtile, rotation};
            const bool ret =
                draw_from_id_string( tile, p, bgCol, fgCol, ll, true, z_drop, false, height_3d );
            if( ret && draw_highlight ) { draw_item_highlight( p ); }
            active_anim_xform_ = {};
            return ret;
        }
    }

    if( overridden ) {
        // and then draw the override vpart
        const vpart_id& vp2 = std::get<0>( override->second );
        if( vp2 ) {
            const char part_mod = std::get<1>( override->second );
            const int subtile = part_mod == 1 ? open_ : part_mod == 2 ? broken : 0;
            const units::angle rotation = std::get<2>( override->second );
            const int draw_highlight = std::get<3>( override->second );
            const std::string vpname = "vp_" + vp2.str();
            // tile overrides are never memorized
            // tile overrides are always shown with full visibility
            const tile_search_params tile = {
                vpname, C_VEHICLE_PART, empty_string, subtile,
                static_cast<int>( std::round( to_degrees( rotation ) ) )
            };

            const bool ret = draw_from_id_string(
                                 tile, p, bgCol, fgCol, lit_level::LIT, false, z_drop, false, height_3d );
            if( ret && draw_highlight ) { draw_item_highlight( p ); }
            return ret;
        }
    } else if( invisible[0] ) {
        if( vp ) {
            // Vehicle is here but outside FOV — draw it live with memorised lighting so the
            // entire vehicle remains visible (player hand-wavingly "knows" where their vehicle is).
            const vehicle& veh = vp->vehicle();
            const int veh_part = vp->part_index();
            char part_mod = 0;
            const Creature* critter = g->critter_at( p, true );
            const bool has_obstacle_here = vp.part_with_feature( VPFLAG_OBSTACLE, false ).has_value();
            const bool use_roof_variant = z_drop > 0 && critter == nullptr && !has_obstacle_here;
            if( use_roof_variant ) {
                auto res = get_vpart_color( vp, here, p, true );
                bgCol = res.first;
                fgCol = res.second;
            }
            const vpart_id& vp_id = veh.part_id_string( veh_part, use_roof_variant, part_mod );
            const int subtile = part_mod == 1 ? open_ : part_mod == 2 ? broken : 0;
            const auto rotation = static_cast<int>(
                                      std::round( to_degrees( veh.part_display_direction( veh_part, use_roof_variant ) ) ) );
            const std::string vpname = "vp_" + vp_id.str();
            if( !veh.forward_velocity() ) {
                get_avatar().memorize_tile( here.bub_to_abs( p ), vpname, subtile, rotation );
            }
            const tile_search_params tile{vpname, C_VEHICLE_PART, empty_string, subtile, rotation};
            if( !tile_iso ) {
                active_anim_xform_ = sprite_xform{
                    .off_x = veh.render_offset_x * static_cast<float>( tile_width ),
                    .off_y = veh.render_offset_y * static_cast<float>( tile_height ) };
            }
            const bool ret = draw_from_id_string(
                                 tile, p, bgCol, fgCol, lit_level::MEMORIZED, true, z_drop, false, height_3d );
            active_anim_xform_ = {};
            return ret;
        }
        // No live vehicle at this position — fall back to map memory so previously-seen
        // tiles are shown as a ghost until they scroll out of range (vehicle moved away).
        const auto ret = get_vpart_memory_at( p );
        if( ret.has_value() ) {
            const auto [tile_id, subtile, rotation] = ret.value();
            const tile_search_params tile{tile_id, C_VEHICLE_PART, empty_string, subtile, rotation};
            return draw_from_id_string(
                       tile, p, bgCol, fgCol, lit_level::MEMORIZED, true, z_drop, false, height_3d );
        }
    } else if( here.has_rope_at( p ) ) {
        auto veh_pair = here.get_rope_at( p.xy() );
        vehicle* veh = veh_pair.first;
        int veh_part = veh_pair.second;

        int veh_z = veh->bub_ms_location().z();
        auto part = veh->part( veh_part ).info();
        if( veh_z - p.z() <= 0 ) { return false; }
        if( part.ladder_length() >= veh_z - p.z() ) {
            for( int i = p.z() + 1; i <= veh_z; i++ ) {
                if( here.ter( tripoint_bub_ms( p.x(), p.y(), i ) ).id().str() != "t_open_air" ) {
                    return false;
                }
            }
        }
        // Gets the visible part, should work fine once tileset vp_ids are updated to work with the
        // vehicle part json ids get the vpart_id
        char part_mod = 0;
        const vpart_id& vp_id = veh->part( veh_part ).info().get_id();
        const int subtile = part_mod == 1 ? open_ : part_mod == 2 ? broken : 0;
        const auto rotation = static_cast<int>(
                                  std::round( to_degrees( veh->part_display_direction( veh_part ) ) ) );
        const std::string vpname = "vp_" + vp_id.str();
        avatar& you = get_avatar();
        const auto abs_pos = here.bub_to_abs( p );
        // Projected rope segments are live draws, not persistent vehicle parts.
        if( you.get_memorized_tile( abs_pos ).tile == vpname ) {
            you.clear_memorized_overlay( abs_pos );
        }
        if( !tile_iso ) {
            active_anim_xform_ = sprite_xform{
                .off_x = veh->render_offset_x * static_cast<float>( tile_width ),
                .off_y = veh->render_offset_y * static_cast<float>( tile_height ) };
        }
        const tile_search_params tile = {vpname, C_VEHICLE_PART, empty_string, subtile, rotation};
        const bool ret =
            draw_from_id_string( tile, p, bgCol, fgCol, ll, true, z_drop, false, height_3d );
        active_anim_xform_ = {};
        return ret;
    }
    return false;
}

bool cata_tiles::draw_critter_at(
    const tripoint_bub_ms& p, lit_level ll, int &height_3d, const bool ( &invisible )[5],
    int z_drop )
{
    // Don't let a leftover tile bash-shake bleed onto creature sprites.
    // (When the deferred y-sort pass prefetches the creature + xform, skip the wipe
    //  so the prefetched xform isn't discarded before the monster/player draw.)
    if( !prefetch_valid_ ) { active_anim_xform_ = {}; }
    if( ( !fov_3d && z_drop > 0 ) || fov_3d_z_range < z_drop ) { return false; }
    // Hover-outline: should this creature get a silhouette ring? want_outline_ is
    // a one-shot latch consumed by the first fg sprite enqueue in draw_sprite_at
    // (so only the base sprite is outlined, not worn overlays). Reset on entry to
    // avoid leaking onto terrain if a previous creature drew no fg sprite.
    want_outline_ = false;
    const bool do_outline =
        g_outline_enable && ( outline_all_ || ( hover_tile_ && *hover_tile_ == p ) );
    // Mark where this creature's sprites start so build_outline_ring can splice a
    // composite silhouette behind the whole range (body + worn items).
    const std::size_t outline_start =
        do_outline ? lighting::get_render_state().tile_sprite_count() : 0;
    bool result;
    bool is_player;
    bool sees_player;
    Attitude attitude;
    const auto override = monster_override.find( p );
    if( override != monster_override.end() ) {
        const mtype_id id = std::get<0>( override->second );
        if( !id ) { return false; }
        is_player = false;
        sees_player = false;
        attitude = std::get<3>( override->second );
        const std::string& chosen_id = id.str();
        const std::string& ent_subcategory =
            id.obj().species.empty() ? empty_string : id.obj().species.begin()->str();
        const tile_search_params tile = {chosen_id, C_MONSTER, ent_subcategory, corner, 0};
        if( do_outline ) {
            want_outline_ = true;
            outline_color_ = outline_color_for( attitude, false );
        }
        result = draw_from_id_string(
                     tile, p, std::nullopt, std::nullopt, lit_level::LIT, false, z_drop, false, height_3d );
    } else if( !invisible[0] ) {
        const Creature* pcritter = prefetch_valid_ ? prefetch_critter_ : g->critter_at( p, true );
        if( pcritter == nullptr ) { return false; }
        const Creature& critter = *pcritter;

        if( !g->u.sees( critter ) ) {
            if( g->u.sees_with_infrared( critter ) || g->u.sees_with_specials( critter ) ) {
                const tile_search_params tile{"infrared_creature", C_NONE, empty_string, 0, 0};
                return draw_from_id_string(
                           tile, p, std::nullopt, std::nullopt, lit_level::LIT, false, z_drop, false,
                           height_3d );
            }
            return false;
        }
        result = false;
        sees_player = false;
        is_player = false;
        attitude = Attitude::A_ANY;
        const monster* m = dynamic_cast<const monster *>( &critter );
        if( m != nullptr ) {
            constexpr auto ent_category = C_MONSTER;
            std::string ent_subcategory = empty_string;
            if( !m->type->species.empty() ) { ent_subcategory = m->type->species.begin()->str(); }
            constexpr int subtile = corner;
            // depending on the toggle flip sprite left or right
            int rot_facing = -1;
            if( m->facing == FD_RIGHT ) {
                rot_facing = 0;
            } else if( m->facing == FD_LEFT ) {
                rot_facing = 4;
            }
            if( rot_facing >= 0 ) {
                const auto ent_name = m->type->id;
                std::string chosen_id = ent_name.str();
                if( m->has_effect( effect_ridden ) ) {
                    int pl_under_height = 6;
                    if( m->mounted_player ) {
                        draw_entity_with_overlays( *m->mounted_player, p, ll, pl_under_height );
                    }
                    const std::string prefix = "rid_";
                    std::string copy_id = chosen_id;
                    const std::string ridden_id = copy_id.insert( 0, prefix );
                    const tile_type* tt = tileset_ptr->find_tile_type( ridden_id );
                    if( tt ) { chosen_id = ridden_id; }
                }

                const auto [bgCol, fgCol] = get_monster_color( *m, get_map(), p );

                const tile_search_params
                tile{chosen_id, ent_category, ent_subcategory, subtile, rot_facing};
                // Sprite-animation transform for this monster (and its z-overlay).
                // When deferred y-sort prefetched the xform, use that instead.
                active_anim_xform_ =
                    prefetch_valid_ ? prefetch_xform_ : compute_anim_xform( critter );
                // Propagate vehicle sub-tile offset so passengers slide with the vehicle.
                if( !tile_iso && m->has_effect( effect_ridden ) ) {
                    if( const auto vp_ride = get_map().veh_at( p ) ) {
                        const auto &veh = vp_ride->vehicle();
                        active_anim_xform_.off_x += veh.render_offset_x * static_cast<float>( tile_width );
                        active_anim_xform_.off_y += veh.render_offset_y * static_cast<float>( tile_height );
                    }
                }
                if( do_outline ) {
                    want_outline_ = true;
                    outline_color_ = outline_color_for( m->attitude_to( g->u ), false );
                }
                result =
                    draw_from_id_string( tile, p, bgCol, fgCol, ll, false, z_drop, false, height_3d );
                active_anim_xform_ = {};
                sees_player = m->sees( g->u );
                attitude = m->attitude_to( g->u );
            }
        }
        const player* pl = dynamic_cast<const player *>( &critter );
        if( pl != nullptr ) {
            if( do_outline ) {
                const bool self = pl->is_player();
                want_outline_ = true;
                outline_color_ =
                    outline_color_for( self ? Attitude::A_ANY : pl->attitude_to( g->u ), self );
            }
            draw_entity_with_overlays( *pl, p, ll, height_3d );
            result = true;
            if( pl->is_player() ) {
                is_player = true;
            } else {
                sees_player = pl->sees( g->u );
                attitude = pl->attitude_to( g->u );
            }
        }
    } else {
        // invisible
        const Creature* critter = prefetch_valid_ ? prefetch_critter_ : g->critter_at( p, true );
        if( critter && ( g->u.sees_with_infrared( *critter ) || g->u.sees_with_specials( *critter ) ) ) {
            // try drawing infrared creature if invisible and not overridden
            // return directly without drawing overlay
            const tile_search_params tile{"infrared_creature", C_NONE, empty_string, 0, 0};
            return draw_from_id_string(
                       tile, p, std::nullopt, std::nullopt, lit_level::LIT, false, z_drop, false,
                       height_3d );
        }
        return false;
    }

    // Hover-outline: now that the creature's body + worn-item sprites are all
    // queued (but BEFORE the attitude indicator below), splice the composite ring
    // behind the whole range. One union silhouette, no per-item inner seams.
    if( want_outline_ ) {
        lighting::render_state& rs = lighting::get_render_state();
        const float rad = std::max( 1.0f, tile_width * g_outline_thickness );
        rs.build_outline_ring(
            outline_start, rs.tile_sprite_count(), outline_color_.r / 255.0f,
            outline_color_.g / 255.0f, outline_color_.b / 255.0f, g_outline_alpha, rad,
            g_outline_alpha_cut );
        want_outline_ = false;
    }

    if( result && !is_player ) {
        std::string draw_id = "overlay_" + Creature::attitude_raw_string( attitude );
        if( sees_player && !g->u.has_trait( trait_INATTENTIVE ) ) { draw_id += "_sees_player"; }
        if( tileset_ptr->find_tile_type( draw_id ) ) {
            const tile_search_params tile{draw_id, C_NONE, empty_string, 0, 0};
            draw_from_id_string(
                tile, p, std::nullopt, std::nullopt, lit_level::LIT, false, z_drop, false,
                height_3d );
        }
    }
    // Clear the latch in case no fg sprite consumed it (don't leak onto terrain).
    want_outline_ = false;
    return result;
}
bool cata_tiles::draw_zone_mark(
    const tripoint_bub_ms& p, lit_level ll, int &height_3d, const bool ( &invisible )[5],
    int z_drop )
{
    if( invisible[0] ) { return false; }

    if( !g->is_zones_manager_open() ) { return false; }

    const zone_manager& mgr = zone_manager::get_manager();
    const auto& abs = get_map().bub_to_abs( p );
    const auto zone = mgr.get_bottom_zone( abs );

    if( zone && zone->has_options() ) {
        auto option = dynamic_cast<const mark_option *>( &zone->get_options() );

        if( option && !option->get_mark().empty() ) {
            const tile_search_params tile{option->get_mark(), C_NONE, empty_string, 0, 0};
            return draw_from_id_string(
                       tile, p, std::nullopt, std::nullopt, ll, true, z_drop, false, height_3d );
        }
    }

    return false;
}

bool cata_tiles::draw_zombie_revival_indicators(
    const tripoint_bub_ms& pos, const lit_level /*ll*/, int & /*height_3d*/,
    const bool ( &invisible )[5], int z_drop )
{
    map& here = get_map();
    if( tileset_ptr->find_tile_type( ZOMBIE_REVIVAL_INDICATOR ) && !invisible[0]
        && !item_override.contains( pos ) && here.could_see_items( tripoint_bub_ms( pos ), g->u ) ) {
        for( auto& i : here.i_at( tripoint_bub_ms( pos ) ) ) {
            if( i->is_corpse() ) {
                if( i->can_revive()
                    || ( i->get_mtype()->zombify_into && !i->has_flag( flag_PULPED ) ) ) {
                    const tile_search_params
                    tile{ZOMBIE_REVIVAL_INDICATOR, C_NONE, empty_string, 0, 0};
                    return draw_from_id_string(
                               tile, pos, std::nullopt, std::nullopt, lit_level::LIT, false, z_drop,
                               false );
                }
            }
        }
    }
    return false;
}

// Check if overlay matches any prefix in the list
// overlay_id is like "wielded_katana", "worn_boots", "mutation_HORNS", etc.
static bool matches_overlay_prefix_list(
    const std::string& overlay_id, const std::vector<std::string> &prefixes )
{
    for( const auto& prefix : prefixes ) {
        if( overlay_id.compare( 0, prefix.size(), prefix ) == 0 ) { return true; }
    }
    return false;
}

// Determine if a state modifier group applies to a given overlay
static bool group_applies_to_overlay(
    const state_modifier_group& group, const std::string& overlay_id,
    const std::vector<std::string> &global_whitelist,
    const std::vector<std::string> &global_blacklist )
{
    // Per-group filters override global if either list is non-empty
    const bool group_has_filters = !group.whitelist.empty() || !group.blacklist.empty();
    const auto& whitelist = group_has_filters ? group.whitelist : global_whitelist;
    const auto& blacklist = group_has_filters ? group.blacklist : global_blacklist;

    // Blacklist checked first - always excludes
    if( matches_overlay_prefix_list( overlay_id, blacklist ) ) { return false; }

    // Whitelist only active if non-empty
    if( !whitelist.empty() ) { return matches_overlay_prefix_list( overlay_id, whitelist ); }

    return true; // No whitelist = applies to all (that passed blacklist)
}

std::tuple<SDL_Surface_Ptr, point> cata_tiles::build_composite_uv_modifier(
    const Character& ch, const int width, const int height, const std::vector<bool> &group_filter )
{
#if !defined(DYNAMIC_ATLAS)
    // UV modifier system requires dynamic atlas for sprite surface access
    ( void )ch;
    ( void )width;
    ( void )height;
    ( void )group_filter;
    return std::make_tuple( nullptr, point_zero );
#else
    const auto& state_modifiers = tileset_ptr->get_state_modifiers();
    if( state_modifiers.empty() ) { return std::make_tuple( nullptr, point_zero ); }

    // If filter provided, check if any groups are enabled
    const bool use_filter = !group_filter.empty();
    if( use_filter ) {
        bool any_enabled = false;
        for( size_t i = 0; i < state_modifiers.size() && i < group_filter.size(); ++i ) {
            if( group_filter[i] ) {
                any_enabled = true;
                break;
            }
        }
        if( !any_enabled ) { return std::make_tuple( nullptr, point_zero ); }
    }

    SDL_Surface_Ptr composite = nullptr;
    point composite_offset = point_zero;
    bool use_offset_mode = true;

    // First pass: determine bounds
    int min_x = 0, min_y = 0, max_x = width, max_y = height;

    for( size_t i = 0; i < state_modifiers.size(); ++i ) {
        // Skip groups not in filter
        if( use_filter && ( i >= group_filter.size() || !group_filter[i] ) ) { continue; }

        const auto& group = state_modifiers[i];
        std::optional<std::string> current_state =
            get_character_state_for_group( ch, group.group_id );
        if( !current_state ) { continue; }

        auto it = group.tiles.find( *current_state );
        if( it == group.tiles.end() || !it->second.fg_sprite ) { continue; }

        const state_modifier_tile& tile = it->second;
        auto [found, mod_surf, mod_rect] = tileset_ptr->get_sprite_surface( *tile.fg_sprite );
        if( !found || !mod_surf ) { continue; }

        min_x = std::min( min_x, tile.offset.x );
        min_y = std::min( min_y, tile.offset.y );
        max_x = std::max( max_x, tile.offset.x + mod_rect.w );
        max_y = std::max( max_y, tile.offset.y + mod_rect.h );
    }

    const int comp_width = max_x - min_x;
    const int comp_height = max_y - min_y;
    composite_offset = point( min_x, min_y );

    // Process modifiers in priority order (index 0 = highest)
    for( size_t i = 0; i < state_modifiers.size(); ++i ) {
        // Skip groups not in filter
        if( use_filter && ( i >= group_filter.size() || !group_filter[i] ) ) { continue; }

        const auto& group = state_modifiers[i];
        std::optional<std::string> current_state =
            get_character_state_for_group( ch, group.group_id );
        if( !current_state ) { continue; }

        auto it = group.tiles.find( *current_state );
        if( it == group.tiles.end() ) { continue; }

        const state_modifier_tile& tile = it->second;

        // If fg_sprite is null, this state is an identity (no modification).
        // Skip to next group - override_lower only takes effect when we actually
        // apply a UV modification.
        if( !tile.fg_sprite ) { continue; }

        auto [found, mod_surf, mod_rect] = tileset_ptr->get_sprite_surface( *tile.fg_sprite );
        if( !found || !mod_surf ) {
            // Sprite not found - skip this group, don't let it block others
            continue;
        }

        if( !composite ) {
            use_offset_mode = group.use_offset_mode;
            composite = create_identity_uv_surface( comp_width, comp_height, use_offset_mode );
            if( !composite ) { return std::make_tuple( nullptr, point_zero ); }
        }

        SDL_Surface_Ptr mod_temp = create_surface_32( mod_rect.w, mod_rect.h );
        if( mod_temp ) {
            SDL_SetSurfaceBlendMode( mod_surf, SDL_BLENDMODE_NONE );
            SDL_Rect src_rect = mod_rect;
            SDL_Rect dst_rect = {0, 0, mod_rect.w, mod_rect.h};
            SDL_BlitSurface( mod_surf, &src_rect, mod_temp.get(), &dst_rect );

            const int dst_x = tile.offset.x - composite_offset.x;
            const int dst_y = tile.offset.y - composite_offset.y;
            chain_uv_modifier_at( composite.get(), mod_temp.get(), use_offset_mode, dst_x, dst_y );
        }

        if( group.override_lower ) { break; }
    }

    return std::make_tuple( std::move( composite ), composite_offset );
#endif
}

std::tuple<SDL_Surface_Ptr, point> cata_tiles::build_composite_uv_modifier(
    const Character& ch, const int width, const int height )
{
    // No filter = include all groups
    return build_composite_uv_modifier( ch, width, height, {} );
}

void cata_tiles::draw_entity_with_overlays(
    const Character& ch, const tripoint_bub_ms& p, lit_level ll, int &height_3d,
    const bool as_independent_entity )
{
    std::string ent_name;

    if( ch.is_npc() ) {
        ent_name = ch.male ? "npc_male" : "npc_female";
    } else {
        ent_name = ch.male ? "player_male" : "player_female";
    }

    const auto& state_modifiers = tileset_ptr->get_state_modifiers();
    const auto& global_whitelist = tileset_ptr->get_global_warp_whitelist();
    const auto& global_blacklist = tileset_ptr->get_global_warp_blacklist();
    const bool use_state_modifiers =
        get_option<bool>( "STATE_MODIFIERS" ) && !state_modifiers.empty();

    // Cache for warp hashes by group filter signature
    // Maps filter signature (vector<bool>) to registered warp hash
    std::map<std::vector<bool>, size_t> signature_to_hash;

    // Build composite for the base character sprite (not an overlay).
    // Only include groups without a whitelist, since the base sprite won't match any overlay
    // prefix. Groups with only a blacklist still apply (blacklist filters out overlays, not the
    // base).
    size_t base_warp_hash = TILESET_NO_WARP;
    std::vector<bool> base_signature( state_modifiers.size(), false );
    for( size_t i = 0; i < state_modifiers.size(); ++i ) {
        // Base sprite applies if group has no whitelist (whitelist requires matching an overlay
        // prefix)
        base_signature[i] = state_modifiers[i].whitelist.empty();
    }

    if( use_state_modifiers ) {
#if defined(DYNAMIC_ATLAS)
        tileset_ptr->ensure_readback_loaded();

        // Check if any groups apply to the base sprite
        bool any_base_groups = false;
        for( bool applies : base_signature ) {
            if( applies ) {
                any_base_groups = true;
                break;
            }
        }

        if( any_base_groups ) {
            auto [uv_surface, uv_offset] = build_composite_uv_modifier(
                                               ch, tileset_ptr->get_tile_width(), tileset_ptr->get_tile_height(), base_signature );
            if( uv_surface ) {
                // Determine offset mode from the first matching state modifier group
                bool offset_mode = true;
                for( size_t i = 0; i < state_modifiers.size(); ++i ) {
                    if( base_signature[i] ) {
                        auto state = get_character_state_for_group( ch, state_modifiers[i].group_id );
                        if( state && state_modifiers[i].tiles.count( *state ) ) {
                            offset_mode = state_modifiers[i].use_offset_mode;
                            break;
                        }
                    }
                }
                // Register the warp surface with the tileset and get its content hash
                base_warp_hash = tileset_ptr->register_warp_surface(
                                     std::move( uv_surface ), uv_offset, offset_mode );
            }
        }
        signature_to_hash[base_signature] = base_warp_hash;
#endif
    }
    active_warp_hash = base_warp_hash;
    // Sprite-animation transform for this character + all its overlays (rigid body).
    // When deferred y-sort prefetched the xform, use that instead.
    active_anim_xform_ = prefetch_valid_ ? prefetch_xform_ : compute_anim_xform( ch );
    // Propagate vehicle sub-tile offset so passengers slide with the vehicle.
    if( !tile_iso && ch.in_vehicle ) {
        if( const auto vp_ride = get_map().veh_at( p ) ) {
            const auto &veh = vp_ride->vehicle();
            active_anim_xform_.off_x += veh.render_offset_x * static_cast<float>( tile_width );
            active_anim_xform_.off_y += veh.render_offset_y * static_cast<float>( tile_height );
        }
    }

    // first draw the character itself(i guess this means a tileset that
    // takes this seriously needs a naked sprite)
    int prev_height_3d = height_3d;


    if( ch.facing == FD_RIGHT || ch.facing == FD_LEFT ) {
        // depending on the toggle flip sprite left or right
        const int rota = ch.facing == FD_RIGHT ? 0 : 4;

        const auto [entity_bgCol, entity_fgCol] = get_character_color( ch, get_map(), p );
        const tile_search_params tile{ent_name, C_NONE, "", corner, rota};
        draw_from_id_string(
            tile, p, entity_bgCol, entity_fgCol, ll, false, 0, as_independent_entity, height_3d );
    }

    const auto& m = get_map();
    auto get_overlay_color = [&]<typename T>( T && arg ) {
        using Decayed = std::remove_reference_t<T>;
        using PtrBase = std::remove_const_t<std::remove_pointer_t<Decayed>>;
        if constexpr( std::is_same_v<PtrBase, item> ) {
            return get_item_color( *arg, m, p );
        } else if constexpr( std::is_same_v<PtrBase, effect> ) {
            return get_effect_color( *arg, ch, m, p );
        } else if constexpr( std::is_same_v<PtrBase, bionic> ) {
            return get_bionic_color( *arg, ch, m, p );
        } else if constexpr( std::is_same_v<PtrBase, mutation> ) {
            return get_mutation_color( *arg, ch, m, p );
        } else {
            return color_tint_pair{std::nullopt, std::nullopt};
        }
    };

    auto should_override = [&]<typename T>( T && arg ) {
        auto check = [&]( const mutation & mut ) {
            mutation_branch branch = mut.first.obj();
            for( const std::string& mut_type : branch.types ) {
                auto controller = tileset_ptr->get_tint_controller( mut_type );
                if( !controller.first.empty() ) { return controller.second; }
            }
            for( const trait_flag_str_id& mut_flag : branch.flags ) {
                auto controller = tileset_ptr->get_tint_controller( mut_flag.str() );
                if( !controller.first.empty() ) { return controller.second; }
            }
            return false;
        };
        using Decayed = std::remove_reference_t<T>;
        using PtrBase = std::remove_const_t<std::remove_pointer_t<Decayed>>;
        if constexpr( std::is_same_v<PtrBase, mutation> ) { return check( *arg ); }
        return false;
    };

    auto is_hair_style = [&]<typename T>( T && arg ) {
        auto check = [&]( const mutation & mut ) {
            if( mut.first.obj().types.contains( "hair_style" ) ) { return true; }
            return false;
        };
        using Decayed = std::remove_reference_t<T>;
        using PtrBase = std::remove_const_t<std::remove_pointer_t<Decayed>>;
        if constexpr( std::is_same_v<PtrBase, mutation> ) { return check( *arg ); }
        return false;
    };

    // next up, draw all the overlays
    const auto overlays = ch.get_overlay_ids();
    for( const auto& [overlay_id, entry] : overlays ) {
        tint_config overlay_bg_color = std::nullopt;
        tint_config overlay_fg_color = std::nullopt;

        std::string draw_id = overlay_id;
        bool found = false;

        if( !std::visit( should_override, entry ) ) {
            // Legacy hair color injection: try to find a tile with the hair color in the name
            if( std::visit( is_hair_style, entry ) ) {
                for( const trait_id& other_mut : ch.get_mutations() ) {
                    if( !other_mut.obj().types.contains( "hair_color" ) ) { continue; }
                    const std::string color_id = other_mut.str();
                    if( draw_id.find( color_id ) != std::string::npos ) { break; }
                    const size_t hair_pos = draw_id.find( "hair_" );
                    if( hair_pos == std::string::npos ) { continue; }
                    const std::string prefix = draw_id.substr( 0, hair_pos );
                    std::string suffix = draw_id.substr( hair_pos );
                    suffix = suffix.substr( suffix.find( '_' ) );
                    const std::string new_id = prefix + color_id + suffix;
                    // draw_id is set to the resolved tile ID if found
                    found = find_overlay_looks_like( ch.male, new_id, draw_id );
                    break;
                }
            }
        }

        if( !found ) {
            auto pair = std::visit( get_overlay_color, entry );
            overlay_bg_color = pair.first;
            overlay_fg_color = pair.second;
            found = find_overlay_looks_like( ch.male, overlay_id, draw_id );
        }
        if( found ) {
            // Determine which groups apply to this overlay based on filters
            if( use_state_modifiers ) {
#if defined(DYNAMIC_ATLAS)
                std::vector<bool> overlay_signature( state_modifiers.size(), false );
                for( size_t i = 0; i < state_modifiers.size(); ++i ) {
                    overlay_signature[i] = group_applies_to_overlay(
                                               state_modifiers[i], overlay_id, global_whitelist, global_blacklist );
                }

                // Look up or build composite for this signature
                auto it = signature_to_hash.find( overlay_signature );
                if( it != signature_to_hash.end() ) {
                    active_warp_hash = it->second;
                } else {
                    // Check if any groups apply
                    bool any_apply = false;
                    for( bool applies : overlay_signature ) {
                        if( applies ) {
                            any_apply = true;
                            break;
                        }
                    }

                    if( !any_apply ) {
                        active_warp_hash = TILESET_NO_WARP;
                    } else {
                        auto [surf, off] = build_composite_uv_modifier(
                                               ch, tileset_ptr->get_tile_width(), tileset_ptr->get_tile_height(),
                                               overlay_signature );
                        if( surf ) {
                            // Determine offset mode from first matching group in this signature
                            bool offset_mode = true;
                            for( size_t i = 0; i < state_modifiers.size(); ++i ) {
                                if( overlay_signature[i] ) {
                                    auto state = get_character_state_for_group(
                                                     ch, state_modifiers[i].group_id );
                                    if( state && state_modifiers[i].tiles.count( *state ) ) {
                                        offset_mode = state_modifiers[i].use_offset_mode;
                                        break;
                                    }
                                }
                            }
                            active_warp_hash = tileset_ptr->register_warp_surface(
                                                   std::move( surf ), off, offset_mode );
                        } else {
                            active_warp_hash = TILESET_NO_WARP;
                        }
                    }
                    signature_to_hash[overlay_signature] = active_warp_hash;
                }
#endif
            }

            int overlay_height_3d = prev_height_3d;
            const int rota = ch.facing == FD_RIGHT ? 0 : 4;
            const tile_search_params tile{draw_id, C_NONE, "", corner, rota};
            draw_from_id_string(
                tile, p, overlay_bg_color, overlay_fg_color, ll, false, 0, as_independent_entity,
                overlay_height_3d );
            // the tallest height-having overlay is the one that counts
            height_3d = std::max( height_3d, overlay_height_3d );
        }
    }

    // Clear the warp state after drawing is complete
    active_warp_hash = TILESET_NO_WARP;
    active_anim_xform_ = {};
#if defined(DYNAMIC_ATLAS)
    tileset_ptr->clear_warp_cache();
#endif
}

bool cata_tiles::draw_item_highlight( const tripoint_bub_ms& pos )
{
    const tile_search_params tile{ITEM_HIGHLIGHT, C_NONE, empty_string, 0, 0};
    return draw_from_id_string(
               tile, pos, std::nullopt, std::nullopt, lit_level::LIT, false, 0, false );
}
