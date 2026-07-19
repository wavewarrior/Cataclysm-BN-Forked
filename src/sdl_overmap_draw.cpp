#include "coop_session.h"
#include "cata_tiles.h"   // cata_tiles member declarations
#include "sdltiles.h"     // tilecontext, overmap_tilecontext, rescale_tileset
#include "sdl_display.h"  // g_display, display_context, fontwidth, fontheight
#include "sdl_font.h"     // Font, Font_Ptr
#include "sdl_fonts.h"    // draw_string

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cached_options.h"
#include "catacharset.h"
#include "color.h"
#include "coordinates.h"
#include "cuboid_rectangle.h"
#include "cursesdef.h"
#include "cursesport.h"
#include "debug.h"
#include "game.h"
#include "game_constants.h"
#include "line.h"
#include "mapbuffer.h"
#include "mission.h"
#include "mongroup.h"
#include "npc.h"
#include "note_label_utils.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmap_location.h"
#include "overmap_label.h"
#include "overmap_label_note.h"
#include "overmap_special.h"
#include "overmap_ui.h"
#include "overmapbuffer.h"
#include "point.h"
#include "regional_settings.h"
#include "rng.h"
#include "sdl_geometry.h"
#include "sdl_wrappers.h"
#include "string_formatter.h"
#include "translations.h"
#include "uistate.h"
#include "units.h"
#include "weather.h"
#include "weather_type.h"
#include "lighting/render_state.h"
#include "lighting/rmlui_layer.h"
#include "rml_screen.h"

// Reference aliases matching sdltiles.cpp names for the extracted code.
// These let the moved functions read g_display members using the same short
// names they had in the god TU.
static auto &font   = g_display.font;
static auto &renderer   = g_display.renderer;
static auto &geometry   = g_display.geometry;

// Static empty string used as default subcategory in tile_search_params.
static const std::string empty_string;

// -----------------------------------------------------------------------
// Helper: find the mission arrow target on the overmap
// -----------------------------------------------------------------------

static std::optional<std::pair<tripoint_abs_omt, std::string>> get_mission_arrow(
    const inclusive_cuboid<tripoint_abs_omt> &overmap_area, const tripoint_abs_omt &center )
{
    const auto *mission = get_avatar().get_active_mission();
    const bool custom_waypoint_valid = get_avatar().get_custom_mission_target() !=
                                       overmap::invalid_tripoint;
    if( mission == nullptr && !custom_waypoint_valid ) {
        return std::nullopt;
    }
    if( ( mission == nullptr || !mission->has_target() ) && !custom_waypoint_valid ) {
        return std::nullopt;
    }
    tripoint_abs_omt mission_target = custom_waypoint_valid
                                      ? get_avatar().get_custom_mission_target()
                                      : get_avatar().get_active_mission_target();

    std::string mission_arrow_variant;
    if( overmap_area.contains( mission_target ) ) {
        mission_arrow_variant = "mission_cursor";
        return std::make_pair( mission_target, mission_arrow_variant );
    }

    inclusive_rectangle<point_abs_omt> area_flat( overmap_area.p_min.xy(), overmap_area.p_max.xy() );
    if( area_flat.contains( mission_target.xy() ) ) {
        int area_z = center.z();
        if( mission_target.z() > area_z ) {
            mission_arrow_variant = "mission_arrow_up";
        } else {
            mission_arrow_variant = "mission_arrow_down";
        }
        return std::make_pair( tripoint_abs_omt( mission_target.xy(), area_z ), mission_arrow_variant );
    }

    const std::vector<tripoint_abs_omt> traj = line_to( center,
        tripoint_abs_omt( mission_target.xy(), center.z() ) );

    if( traj.empty() ) {
        debugmsg( "Failed to gen overmap mission trajectory %s %s",
                  center.to_string(), mission_target.to_string() );
        return std::nullopt;
    }

    tripoint_abs_omt arr_pos = traj[0];
    for( auto it = traj.rbegin(); it != traj.rend(); it++ ) {
        if( overmap_area.contains( *it ) ) {
            arr_pos = *it;
            break;
        }
    }

    const int north_border_y = ( overmap_area.p_max.y() - overmap_area.p_min.y() ) / 3;
    const int south_border_y = north_border_y * 2;
    const int west_border_x = ( overmap_area.p_max.x() - overmap_area.p_min.x() ) / 3;
    const int east_border_x = west_border_x * 2;

    tripoint_abs_omt north_pmax( overmap_area.p_max );
    north_pmax.y() = overmap_area.p_min.y() + north_border_y;
    tripoint_abs_omt south_pmin( overmap_area.p_min );
    south_pmin.y() += south_border_y;
    tripoint_abs_omt west_pmax( overmap_area.p_max );
    west_pmax.x() = overmap_area.p_min.x() + west_border_x;
    tripoint_abs_omt east_pmin( overmap_area.p_min );
    east_pmin.x() += east_border_x;

    const inclusive_cuboid<tripoint_abs_omt> north_sector( overmap_area.p_min, north_pmax );
    const inclusive_cuboid<tripoint_abs_omt> south_sector( south_pmin, overmap_area.p_max );
    const inclusive_cuboid<tripoint_abs_omt> west_sector( overmap_area.p_min, west_pmax );
    const inclusive_cuboid<tripoint_abs_omt> east_sector( east_pmin, overmap_area.p_max );

    mission_arrow_variant = "mission_arrow_";
    if( north_sector.contains( arr_pos ) ) {
        mission_arrow_variant += 'n';
    } else if( south_sector.contains( arr_pos ) ) {
        mission_arrow_variant += 's';
    }
    if( west_sector.contains( arr_pos ) ) {
        mission_arrow_variant += 'w';
    } else if( east_sector.contains( arr_pos ) ) {
        mission_arrow_variant += 'e';
    }

    return std::make_pair( tripoint_abs_omt( arr_pos ), mission_arrow_variant );
}

// -----------------------------------------------------------------------
// cata_tiles::get_omt_id_rotation_and_subtile
// -----------------------------------------------------------------------

std::string cata_tiles::get_omt_id_rotation_and_subtile(
    const tripoint_abs_omt &omp, int &rota, int &subtile )
{
    auto oter_at = []( const tripoint_abs_omt & p ) {
        const oter_id &cur_ter = ACTIVE_OVERMAP_BUFFER.ter( p );

        if( !uistate.overmap_show_forest_trails &&
            is_ot_match( "forest_trail", cur_ter, ot_match_type::type ) ) {
            return oter_id( "forest" );
        }

        return cur_ter;
    };

    oter_id ot_id = oter_at( omp );
    const oter_t &ot = *ot_id;
    oter_type_id ot_type_id = ot.get_type_id();
    const oter_type_t &ot_type = *ot_type_id;

    if( ot_type.has_connections() ) {
        const oter_type_id neighborhood[4] = {
            oter_at( omp + point_south )->get_type_id(),
            oter_at( omp + point_east )->get_type_id(),
            oter_at( omp + point_west )->get_type_id(),
            oter_at( omp + point_north )->get_type_id()
        };

        char val = 0;

        for( int i = 0; i < 4; ++i ) {
            if( ot_type.connects_to( neighborhood[i] ) ) {
                val += 1 << i;
            }
        }

        get_rotation_and_subtile( val, rota, subtile );
    } else {
        ot.get_rotation_and_subtile( rota, subtile );
    }

    return ot_type_id.id().str();
}

// -----------------------------------------------------------------------
// cata_tiles::draw_om
// -----------------------------------------------------------------------

// §8.1 gate step-4 font straggler: the overmap city / note / center-info labels
// draw through the curses SDL Font glyph path (draw_string). When enabled, route
// them through the §7 RmlUi world-text layer instead (the glyph path that survives
// the curses rip-out). draw_om runs in the redraw cycle (before refresh_display's
// prepare), so world_text_begin + adds here land in this frame's geometry — the
// same timing as the SCT feed in sdl_curses_draw. Default OFF; A/B via F4.
bool &overmap_text_rmlui_enabled()
{
    static bool enabled = true;
    return enabled;
}

// Map a curses palette colour index to the 0xRRGGBBAA world_text_add expects.
static unsigned int omt_label_rgba( int color_index )
{
    const SDL_Color c = windowsPalette[color_index];
    return ( static_cast<unsigned>( c.r ) << 24 ) | ( static_cast<unsigned>( c.g ) << 16 ) |
           ( static_cast<unsigned>( c.b ) << 8 ) | 0xFFu;
}

void cata_tiles::draw_om( point dest, const tripoint_abs_omt &center_abs_omt, bool blink )
{
    if( !g ) {
        return;
    }

    // §7 world-text: clear this frame's queue before re-submitting the labels
    // below (mirrors the SCT begin in sdl_curses_draw). Only when routing is on.
    const bool omt_text_rml = overmap_text_rmlui_enabled();
    if( omt_text_rml ) {
        rmlui_layer::world_text_begin();
    }

    // clear_frame_queues() was already called by redraw_invalidated() before
    // this callback runs. Only clear the tile sprite queue so that overmap
    // tiles don't accumulate across ticks; UI queues are owned by the
    // enclosing redraw_invalidated() cycle.
    if( auto *rs = &lighting::get_render_state(); rs->ready() ) {
        rs->clear_tile_queue();
    }

    int width = OVERMAP_WINDOW_TERM_WIDTH * font->width;
    int height = OVERMAP_WINDOW_TERM_HEIGHT * font->height;

    {
        // GPU scissor — clips overmap tile sprites to the overmap viewport.
        SDL_Rect clipRect = { dest.x, dest.y, width, height };
        lighting::get_render_state().set_tile_scissor( &clipRect );

        //fill render area with black to prevent artifacts where no new pixels are drawn
        geometry->rect( renderer, point{ clipRect.x, clipRect.y }, clipRect.w, clipRect.h, SDL_Color() );
    }

    point s;
    get_window_tile_counts( width, height, s.x, s.y );

    op = point( dest.x * fontwidth, dest.y * fontheight );
    // Rounding up to include incomplete tiles at the bottom/right edges
    screentile_width = divide_round_up( width, tile_width );
    screentile_height = divide_round_up( height, tile_height );

    const int min_col = 0;
    const int max_col = s.x;
    const int min_row = 0;
    const int max_row = s.y;
    int height_3d = 0;
    avatar &you = get_avatar();
    const tripoint_abs_omt avatar_pos = you.abs_omt_pos();
    const tripoint_abs_omt corner_NW = center_abs_omt - point( max_col / 2, max_row / 2 );
    const tripoint_abs_omt corner_SE = corner_NW + point( max_col - 1, max_row - 1 );
    const inclusive_cuboid<tripoint_abs_omt> overmap_area( corner_NW, corner_SE );
    // Debug vision allows seeing everything
    const bool has_debug_vision = you.has_trait( trait_id( "DEBUG_NIGHTVISION" ) );
    // sight_points is hoisted for speed reasons.
    const int sight_points = !has_debug_vision ?
                             you.overmap_sight_range( g->light_level( you.bub_pos().z() ) ) :
                             100;
    const bool showhordes = uistate.overmap_show_hordes;
    const bool viewing_weather = ( ( uistate.overmap_debug_weather || uistate.overmap_visible_weather )
                                   && center_abs_omt.z() >= 0 );
    o = corner_NW.xy().reinterpret_as<point_bub_ms>();

    const auto global_omt_to_draw_position = []( const tripoint_abs_omt & omp ) {
        // z position is hardcoded to 0 because the things this will be used to draw should not be skipped
        return tripoint_bub_ms( omp.x(), omp.y(), 0 );
    };
    const auto has_player_label = [&]( const tripoint_abs_omt & pos ) -> bool {
        const auto player_label = overmap_label_note::extract_label( ACTIVE_OVERMAP_BUFFER.note( pos ) );
        return player_label.has_value() && !player_label->empty();
    };
    const auto has_map_label = [&]( const tripoint_abs_omt & pos ) -> bool {
        if( const auto player_label = overmap_label_note::extract_label( ACTIVE_OVERMAP_BUFFER.note( pos ) );
            player_label.has_value() && !player_label->empty() )
        {
            return true;
        }

        const auto &terrain = ACTIVE_OVERMAP_BUFFER.ter( pos );
        if( const auto static_label = overmap_labels::get_label( terrain->get_type_id() );
            static_label.has_value() && !static_label->empty() )
        {
            return true;
        }

        return false;
    };

    // Cache display_oter substitution strings for the active region.
    const regional_settings &active_region_settings = ACTIVE_OVERMAP_BUFFER.get_settings(
            center_abs_omt );
    const bool om_has_display_oter = !active_region_settings.display_oter.is_empty();
    const std::string om_default_oter_str = active_region_settings.default_oter.str();
    const std::string om_display_oter_str = om_has_display_oter
                                            ? active_region_settings.display_oter.str()
                                            : std::string{};

    for( int row = min_row; row < max_row; row++ ) {
        for( int col = min_col; col < max_col; col++ ) {
            const tripoint_abs_omt omp = corner_NW + point( col, row );

            const bool see = has_debug_vision || ACTIVE_OVERMAP_BUFFER.seen( omp );
            const bool los = see && you.overmap_los( omp, sight_points );
            // the full string from the ter_id including _north etc.
            TILE_CATEGORY category = TILE_CATEGORY::C_OVERMAP_TERRAIN;
            std::string id;
            int rotation = 0;
            int subtile = -1;

            if( viewing_weather ) {
                const tripoint_abs_omt omp_sky( omp.xy(), OVERMAP_HEIGHT );
                if( uistate.overmap_debug_weather ||
                    you.overmap_los( omp_sky, sight_points * 2 ) ) {
                    id = overmap_ui::get_weather_at_point( omp_sky.xy() ).c_str();
                    category = TILE_CATEGORY::C_OVERMAP_WEATHER;
                }
            }
            if( id.empty() ) {
                if( see ) {
                    id = get_omt_id_rotation_and_subtile( omp, rotation, subtile );
                    if( om_has_display_oter && id == om_default_oter_str ) {
                        id = om_display_oter_str;
                    }
                } else {
                    id = "unknown_terrain";
                }
            }

            if( overmap_transparency && category != TILE_CATEGORY::C_OVERMAP_WEATHER ) {
                int z_offset = 0;
                while( id == "open_air" ) {
                    z_offset++;
                    const tripoint_abs_omt lower_omp = omp + tripoint( 0, 0, -z_offset );
                    const bool lower_see = has_debug_vision || ACTIVE_OVERMAP_BUFFER.seen( lower_omp );
                    if( !lower_see ) {
                        //actually really strange situation when above overmap is explored, but below one isn't
                        //so let's account for this just in case, drawing highest seen tile
                        z_offset--;
                        break;
                    }
                    id = get_omt_id_rotation_and_subtile( lower_omp, rotation, subtile );
                }
                draw_om_tile_recursively( omp + tripoint( 0, 0, -z_offset ), id, rotation, subtile, z_offset );
            } else {
                const lit_level ll = ACTIVE_OVERMAP_BUFFER.is_explored( omp ) ? lit_level::LOW : lit_level::LIT;

                auto [bgCol, fgCol] = get_overmap_color( ACTIVE_OVERMAP_BUFFER, omp );

                // light level is now used for choosing between grayscale filter and normal lit tiles.
                const tile_search_params tile { id, category, "overmap_terrain", subtile, rotation };
                draw_from_id_string( tile, omp.reinterpret_as<tripoint_bub_ms>(), bgCol, fgCol,
                                     ll, false, 0, false,
                                     height_3d );
            }

            if( blink && uistate.overmap_highlighted_omts.contains( omp ) ) {
                if( tile_iso ) {
                    const tile_search_params tile {"highlight", C_NONE, empty_string, 0, 0};
                    draw_from_id_string( tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt,
                                         lit_level::LIT, false, 0, false );
                } else {
                    SDL_Color c = curses_color_to_SDL( c_pink );
                    c.a = c.a >> 1;
                    auto p = player_to_screen( omp.reinterpret_as<tripoint_bub_ms>().xy() );
                    draw_color_at( c, point_bub_ms( p ), SDL_BLENDMODE_BLEND );
                }
            }

            if( see ) {
                if( blink && uistate.overmap_debug_mongroup ) {
                    const std::vector<mongroup *> mgroups = ACTIVE_OVERMAP_BUFFER.monsters_at( omp );
                    if( !mgroups.empty() ) {
                        const auto horde_it = std::ranges::find_if( mgroups, []( const mongroup * mgp ) {
                            return mgp != nullptr && mgp->horde;
                        } );
                        const mongroup *chosen = horde_it != mgroups.end() ? *horde_it : mgroups.front();
                        if( chosen != nullptr ) {
                            const tile_search_params tile { chosen->type->defaultMonster.str(), C_NONE, empty_string, 0, 0 };
                            draw_from_id_string( tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt,
                                                 lit_level::LIT, false, 0, false );
                        }
                    }
                }
                const auto fallback_horde_id = [&]( const tripoint_abs_omt & pos ) -> std::string {
                    const auto groups = ACTIVE_OVERMAP_BUFFER.monsters_at( pos );
                    const auto horde_it = std::ranges::find_if( groups, []( const mongroup * mgp )
                    {
                        return mgp != nullptr && mgp->horde && mgp->type.is_valid();
                    } );
                    if( horde_it == groups.end() )
                    {
                        return "mon_zombie";
                    }

                    const mongroup *mgp = *horde_it;
                    const MonsterGroup &group = mgp->type.obj();
                    const auto default_id = group.defaultMonster.is_valid()
                        ? group.defaultMonster.str()
                        : std::string( "mon_zombie" );
                    if( group.monsters.empty() )
                    {
                        return default_id;
                    }

                    const auto best_entry = std::ranges::max_element( group.monsters, []( const auto & lhs,
                        const auto & rhs )
                    {
                        return lhs.frequency < rhs.frequency;
                    } );
                    if( best_entry == group.monsters.end() )
                    {
                        return default_id;
                    }
                    return best_entry->name.is_valid() ? best_entry->name.str() : default_id;
                };

                const int horde_size = ACTIVE_OVERMAP_BUFFER.get_horde_size( omp );
                if( showhordes && los && horde_size >= HORDE_VISIBILITY_SIZE ) {
                    // Prefer overmap horde sprites; fall back to a zombie monster sprite if missing.
                    const int clamped_size = std::clamp( horde_size, 1, 90 );
                    const std::string horde_id = string_format( "overmap_horde_%d", clamped_size );
                    if( find_tile_with_season( horde_id ) ) {
                        const tile_search_params tile { horde_id, C_NONE, empty_string, 0, 0 };
                        draw_from_id_string(
                            tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt, lit_level::LIT, false, 0,
                            false );
                    } else {
                        auto fallback_id = fallback_horde_id( omp );
                        if( !find_tile_with_season( fallback_id ) ) {
                            const auto groups = ACTIVE_OVERMAP_BUFFER.monsters_at( omp );
                            const auto horde_it = std::ranges::find_if( groups, []( const mongroup * mgp ) {
                                return mgp != nullptr && mgp->horde && mgp->type.is_valid();
                            } );
                            if( horde_it != groups.end() && ( *horde_it ) != nullptr ) {
                                const MonsterGroup &group = ( *horde_it )->type.obj();
                                if( group.defaultMonster.is_valid() ) {
                                    fallback_id = group.defaultMonster.str();
                                }
                            }
                            if( !find_tile_with_season( fallback_id ) ) {
                                fallback_id = "mon_zombie";
                            }
                        }
                        const tile_search_params tile { fallback_id, C_NONE, empty_string, 0, 0 };
                        draw_from_id_string(
                            tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt, lit_level::LIT, false, 0,
                            false );
                    }
                }
            }

            if( uistate.place_terrain || uistate.place_special ) {
                // Highlight areas that already have been generated
                // TODO: fix point types
                if( ACTIVE_MAPBUFFER.lookup_submap( project_to<coords::sm>( omp ) ) ) {
                    const tile_search_params tile {"highlight", C_NONE, empty_string, 0, 0};
                    draw_from_id_string(
                        tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt,
                        lit_level::LIT, false, 0, false );
                }
            }

            if( blink && ACTIVE_OVERMAP_BUFFER.has_vehicle( omp ) ) {
                const std::string tile_id = find_tile_looks_like( "overmap_remembered_vehicle", C_OVERMAP_NOTE )
                                            ? "overmap_remembered_vehicle"
                                            : "note_c_cyan";
                const tile_search_params tile { tile_id, C_OVERMAP_NOTE, "overmap_note", 0, 0 };
                draw_from_id_string(
                    tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt,
                    lit_level::LIT, false, 0, false );
            }

            if( blink && uistate.overmap_show_map_notes && ACTIVE_OVERMAP_BUFFER.has_note( omp ) &&
                !has_map_label( omp ) ) {

                nc_color ter_color = c_black;
                std::string ter_sym = " ";
                // Display notes in all situations, even when not seen
                std::tie( ter_sym, ter_color, std::ignore ) =
                    overmap_ui::get_note_display_info( ACTIVE_OVERMAP_BUFFER.note( omp ) );

                bool drew_note_sprite = false;
                const std::optional<std::string> note_sprite =
                    overmap_ui::get_note_sprite_id( ACTIVE_OVERMAP_BUFFER.note( omp ) );
                if( note_sprite ) {
                    const tile_search_params sprite_tile { *note_sprite, C_NONE, empty_string, 0, 0 };
                    drew_note_sprite = draw_from_id_string(
                                           sprite_tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt,
                                           lit_level::LIT, false, 0, false );
                }
                if( !drew_note_sprite ) {
                    std::string note_name = "note_" + ter_sym + "_" + string_from_color( ter_color );
                    const tile_search_params tile { note_name, C_OVERMAP_NOTE, "overmap_note", 0, 0 };
                    draw_from_id_string(
                        tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt,
                        lit_level::LIT, false, 0, false );
                }
            }
        }
    }

    if( uistate.place_terrain ) {
        const oter_str_id &terrain_id = uistate.place_terrain->id;
        const oter_t &terrain = *terrain_id;
        std::string id = terrain.get_type_id().str();
        int rotation;
        int subtile;
        terrain.get_rotation_and_subtile( rotation, subtile );
        const tile_search_params tile { id, C_NONE, empty_string, subtile, rotation };
        draw_from_id_string(
            tile, global_omt_to_draw_position( center_abs_omt ), std::nullopt, std::nullopt,
            lit_level::LOW, true, 0, false );
    }
    if( uistate.place_special ) {
        for( const overmap_special_terrain &s_ter : uistate.place_special->preview_terrains() ) {
            if( s_ter.p.z() == 0 ) {
                const point_rel_omt rp( om_direction::rotate( s_ter.p.xy(), uistate.omedit_rotation ) );
                oter_id rotated_id = s_ter.terrain->get_rotated( uistate.omedit_rotation );
                const oter_t &terrain = *rotated_id;
                std::string id = terrain.get_type_id().str();
                int rotation;
                int subtile;
                terrain.get_rotation_and_subtile( rotation, subtile );

                const tile_search_params tile { id, C_OVERMAP_TERRAIN, "overmap_terrain", 0, rotation };
                draw_from_id_string(
                    tile, global_omt_to_draw_position( center_abs_omt + rp ), std::nullopt, std::nullopt,
                    lit_level::LOW, true, 0, false );
            }
        }
    }

    auto npcs_near_player = ACTIVE_OVERMAP_BUFFER.get_npcs_near_player( sight_points );

    // draw nearby seen npcs
    for( const shared_ptr_fast<npc> &guy : npcs_near_player ) {
        const tripoint_abs_omt &guy_loc = guy->abs_omt_pos();
        if( guy_loc.z() == center_abs_omt.z() && ( has_debug_vision ||
                ACTIVE_OVERMAP_BUFFER.seen( guy_loc ) ) ) {
            draw_entity_with_overlays( *guy, global_omt_to_draw_position( guy_loc ), lit_level::LIT,
                                       height_3d );
        }
    }

    if( you.abs_omt_pos().z() == center_abs_omt.z() ) {
        draw_entity_with_overlays( you, global_omt_to_draw_position( avatar_pos ),
                                   lit_level::LIT, height_3d );
    }

    {
        const tile_search_params tile { "cursor", C_NONE, empty_string, 0, 0 };
        draw_from_id_string(
            tile, global_omt_to_draw_position( center_abs_omt ), std::nullopt, std::nullopt,
            lit_level::LIT, false, 0, false );
    }

    {
        const auto& sess = coop_session::get();
        if( sess.shared_mark.has_value() && sess.shared_mark->z() == center_abs_omt.z() ) {
            const tripoint_abs_omt& mark = *sess.shared_mark;
            if( overmap_area.contains( mark ) ) {
                const tile_search_params tile { "note_*_c_light_cyan", C_OVERMAP_NOTE,
                                                "overmap_note", 0, 0 };
                draw_from_id_string(
                    tile, global_omt_to_draw_position( mark ), std::nullopt, std::nullopt,
                    lit_level::LIT, false, 0, false );
                // Draw the label as text to the right of the marker tile, when available.
                if( !sess.shared_mark_label.empty() ) {
                    const auto tile_draw_pos = global_omt_to_draw_position( mark ) - o;
                    const int lx = tile_draw_pos.x() * tile_width  + dest.x + tile_width;
                    const int ly = tile_draw_pos.y() * tile_height + dest.y;
                    const char note_fg = static_cast<char>(
                                             cata_cursesport::colorpairs[c_light_cyan.to_color_pair_index()].FG );
                    if( omt_text_rml ) {
                        rmlui_layer::world_text_add( lx, ly, sess.shared_mark_label,
                                                     omt_label_rgba( note_fg ) );
                    } else {
                        draw_string( *font, renderer, geometry, sess.shared_mark_label,
                                     point( lx, ly ), note_fg );
                    }
                }
            }
        }
    }

    if( blink ) {
        // Draw path for auto-travel
        for( const tripoint_abs_omt &pos : you.omt_path ) {
            const char *id;
            if( pos.z() == center_abs_omt.z() ) {
                id = "overmap_path";
            } else if( pos.z() > center_abs_omt.z() ) {
                id = "overmap_path_above";
            } else {
                id = "overmap_path_below";
            }
            const tile_search_params tile { id, C_NONE, empty_string, 0, 0 };
            draw_from_id_string(
                tile, global_omt_to_draw_position( pos ), std::nullopt, std::nullopt,
                lit_level::LIT, false, 0, false );
        }

        // reduce the area where the map cursor is drawn so it doesn't get cut off
        inclusive_cuboid<tripoint_abs_omt> map_cursor_area = overmap_area;
        map_cursor_area.p_max.y()--;
        const std::optional<std::pair<tripoint_abs_omt, std::string>> mission_arrow =
            get_mission_arrow( map_cursor_area, center_abs_omt );
        if( mission_arrow ) {
            const tile_search_params tile { mission_arrow->second, C_NONE, empty_string, 0, 0 };
            draw_from_id_string(
                tile, global_omt_to_draw_position( mission_arrow->first ), std::nullopt, std::nullopt,
                lit_level::LIT, false, 0, false );
        }
    }

    if( !viewing_weather && uistate.overmap_show_city_labels ) {
        const auto abs_sm_to_draw_label = [&]( const tripoint_abs_sm & city_pos, const int label_length ) {
            const auto tile_draw_pos = global_omt_to_draw_position( project_to<coords::omt>
                                       ( city_pos ) ) - o;
            point draw_point( tile_draw_pos.x() * tile_width + dest.x,
                              tile_draw_pos.y() * tile_height + dest.y );
            // center text on the tile
            draw_point += point( ( tile_width - label_length * fontwidth ) / 2,
                                 ( tile_height - fontheight ) / 2 );
            return draw_point;
        };

        // draws a black rectangle behind a label for visibility and legibility
        const auto label_bg = [&]( const tripoint_abs_sm & pos, const std::string & name ) {
            const int name_length = utf8_width( name );
            const point draw_pos = abs_sm_to_draw_label( pos, name_length );
            const SDL_Rect clipRect = { draw_pos.x, draw_pos.y, name_length * fontwidth, fontheight };

            geometry->rect( renderer, point{ clipRect.x, clipRect.y }, clipRect.w, clipRect.h, SDL_Color() );

            if( omt_text_rml ) {
                rmlui_layer::world_text_add( draw_pos.x, draw_pos.y, name, omt_label_rgba( 11 ) );
            } else {
                draw_string( *font, renderer, geometry, name, draw_pos, 11 );
            }
        };

        const auto abs_omt_to_draw_label = [&]( const tripoint_abs_omt & omt_pos, const int label_length ) {
            const auto tile_draw_pos = global_omt_to_draw_position( omt_pos ) - o;
            auto draw_point = point( tile_draw_pos.x() * tile_width + dest.x,
                                     tile_draw_pos.y() * tile_height + dest.y );
            draw_point += point( ( tile_width - label_length * fontwidth ) / 2,
                                 ( tile_height - fontheight ) / 2 );
            return draw_point;
        };

        const auto label_bg_omt = [&]( const tripoint_abs_omt & pos, const std::string & name ) {
            const auto name_length = utf8_width( name );
            const auto draw_pos = abs_omt_to_draw_label( pos, name_length );
            const auto clip_rect = SDL_Rect{
                .x = draw_pos.x,
                .y = draw_pos.y,
                .w = name_length * fontwidth,
                .h = fontheight
            };

            geometry->rect( renderer, point{ clip_rect.x, clip_rect.y }, clip_rect.w, clip_rect.h,
                            SDL_Color() );

            if( omt_text_rml ) {
                rmlui_layer::world_text_add( draw_pos.x, draw_pos.y, name, omt_label_rgba( 11 ) );
            } else {
                draw_string( *font, renderer, geometry, name, draw_pos, 11 );
            }
        };

        // the tiles on the overmap are overmap tiles, so we need to use
        // coordinate conversions to make sure we're in the right place.
        const int radius = coords::project_to<coords::sm>( tripoint_abs_omt( std::min( max_col, max_row ),
                           0, 0 ) ).x() / 2;

        for( const city_reference &city : ACTIVE_OVERMAP_BUFFER.get_cities_near(
                 coords::project_to<coords::sm>( center_abs_omt ), radius ) ) {
            const tripoint_abs_omt city_center = coords::project_to<coords::omt>( city.abs_sm_pos );
            if( ACTIVE_OVERMAP_BUFFER.seen( city_center ) && overmap_area.contains( city_center ) &&
                !has_player_label( city_center ) ) {
                label_bg( city.abs_sm_pos, city.city->name );
            }
        }

        for( int row = min_row; row < max_row; row++ ) {
            for( int col = min_col; col < max_col; col++ ) {
                const tripoint_abs_omt omt_pos = corner_NW + point( col, row );
                if( !ACTIVE_OVERMAP_BUFFER.seen( omt_pos ) ) {
                    continue;
                }
                auto label_text = std::optional<std::string> {};
                if( const auto player_label =
                        overmap_label_note::extract_label( ACTIVE_OVERMAP_BUFFER.note( omt_pos ) );
                    player_label.has_value() ) {
                    label_text = *player_label;
                } else {
                    const auto &terrain = ACTIVE_OVERMAP_BUFFER.ter( omt_pos );
                    if( const auto static_label = overmap_labels::get_label( terrain->get_type_id() );
                        static_label.has_value() ) {
                        label_text = _( *static_label );
                    }
                }
                if( !label_text.has_value() || label_text->empty() ) {
                    continue;
                }
                if( overmap_area.contains( omt_pos ) ) {
                    label_bg_omt( omt_pos, *label_text );
                }
            }
        }
    }

    std::vector<std::pair<nc_color, std::string>> notes_window_text;

    if( uistate.overmap_show_map_notes ) {
        const std::string &note_text = ACTIVE_OVERMAP_BUFFER.note( center_abs_omt );
        if( !note_text.empty() && !overmap_label_note::is_label_only( note_text ) ) {
            const std::tuple<char, nc_color, size_t> note_info = overmap_ui::get_note_display_info(
                    note_text );
            const size_t pos = std::get<2>( note_info );
            if( pos != std::string::npos ) {
                const auto display_note_text =
                    note_label_utils::strip_label_commands( note_text.substr( pos ) );
                if( !display_note_text.empty() ) {
                    notes_window_text.emplace_back( std::get<1>( note_info ), display_note_text );
                }
            }
            if( ACTIVE_OVERMAP_BUFFER.is_marked_dangerous( center_abs_omt ) ) {
                notes_window_text.emplace_back( c_red, _( "DANGEROUS AREA!" ) );
            }
        }
    }

    if( has_debug_vision || ACTIVE_OVERMAP_BUFFER.seen( center_abs_omt ) ) {
        for( const auto &npc : npcs_near_player ) {
            if( !npc->marked_for_death && npc->abs_omt_pos() == center_abs_omt ) {
                notes_window_text.emplace_back( npc->basic_symbol_color(), npc->name );
            }
        }
    }

    for( auto &v : ACTIVE_OVERMAP_BUFFER.get_vehicle( center_abs_omt ) ) {
        notes_window_text.emplace_back( c_white, v.name );
    }

    if( !notes_window_text.empty() ) {
        constexpr int padding = 2;

        const auto draw_note_text = [&]( point  draw_pos, const std::string & name,
        nc_color & color ) {
            char note_fg_color = color == c_yellow ? 11 :
                                 cata_cursesport::colorpairs[color.to_color_pair_index()].FG;
            if( omt_text_rml ) {
                rmlui_layer::world_text_add( draw_pos.x, draw_pos.y, name, omt_label_rgba( note_fg_color ) );
                return draw_pos;
            }
            return draw_string( *font, renderer, geometry, name, draw_pos, note_fg_color );
        };

        // Find screen coordinates to the right of the center tile
        auto center_sm = coords::project_to<coords::sm>( tripoint_abs_omt( center_abs_omt.x() + 1,
                         center_abs_omt.y(), center_abs_omt.z() ) );
        const auto tile_draw_pos = global_omt_to_draw_position( project_to<coords::omt>
                                   ( center_sm ) ) - o;
        point draw_point( tile_draw_pos.x() * tile_width + dest.x,
                          tile_draw_pos.y() * tile_height + dest.y );
        draw_point += point( padding, padding );

        // Draw notes header. Very simple label at the moment
        nc_color header_color = c_white;
        const std::string header_string = _( "-- Notes: --" );
        SDL_Rect header_background_rect = {
            draw_point.x - padding,
            draw_point.y - padding,
            fontwidth * utf8_width( header_string ) + padding * 2,
            fontheight + padding * 2
        };
        geometry->rect( renderer, point{ header_background_rect.x, header_background_rect.y },
                        header_background_rect.w, header_background_rect.h, SDL_Color{ 0, 0, 0, 175 } );
        draw_note_text( draw_point, header_string, header_color );
        draw_point.y += fontheight + padding * 2;

        const int starting_x = draw_point.x;

        for( auto &line : notes_window_text ) {
            const auto color_segments = split_by_color( line.second );
            std::stack<nc_color> color_stack;
            nc_color default_color = std::get<0>( line );
            color_stack.push( default_color );
            std::vector<std::tuple<nc_color, std::string>> colored_lines;

            draw_point.x = starting_x;

            int line_length = 0;
            for( auto seg : color_segments ) {
                if( seg.empty() ) {
                    continue;
                }

                if( seg[0] == '<' ) {
                    const color_tag_parse_result::tag_type type = update_color_stack(
                            color_stack, seg, report_color_error::no );
                    if( type != color_tag_parse_result::non_color_tag ) {
                        seg = rm_prefix( seg );
                    }
                }

                nc_color &color = color_stack.empty() ? default_color : color_stack.top();
                colored_lines.emplace_back( color, seg );
                line_length += utf8_width( seg );
            }

            // Draw background first for the whole line
            SDL_Rect background_rect = {
                draw_point.x - padding,
                draw_point.y - padding,
                fontwidth *line_length + padding * 2,
                fontheight + padding * 2
            };
            geometry->rect( renderer, point{ background_rect.x, background_rect.y },
                            background_rect.w, background_rect.h, SDL_Color{ 0, 0, 0, 175 } );

            // Draw colored text segments
            for( auto &colored_line : colored_lines ) {
                std::string &text = std::get<1>( colored_line );
                draw_point.x = draw_note_text( draw_point, text, std::get<0>( colored_line ) ).x;
            }

            draw_point.y += fontheight + padding;
        }
    }

    lighting::get_render_state().clear_tile_scissor();
}


