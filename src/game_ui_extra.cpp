// game_ui_extra.cpp — extracted from game.cpp (B5-4 UI/look/list cluster)
// Functions: look_debug, draw_look_around_cursor, print_all_tile_info_text,
//            check_zone*, is_zones_manager*, zones_manager, pre_print_all_tile_info,
//            look_around (both overloads), find_nearby_items, draw_trail_to_square,
//            zoom_*, get_zoom, get_moves_since_last_save, get_user_action_counter,
//            take_screenshot (both), list_items_monsters, list_items, list_monsters

#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <RmlUi/Core.h>
#include "action.h"
#include "avatar.h"
#include "avatar_action.h"
#include "avatar_functions.h"
#include "calendar.h"
#include "cached_options.h"
#include "character_functions.h"
#include "character_turn.h"
#include "clzones.h"
#include "construction.h"
#include "construction_group.h"
#include "construction_partial.h"
#include "coordinates.h"
#include "creature.h"
#include "creature_tracker.h"
#include "debug.h"
#include "editmap.h"
#include "effect.h"
#include "event.h"
#include "event_bus.h"
#include "field.h"
#include "field_type.h"
#include "flag.h"
#include "game_constants.h"
#include "game_ui.h"
#include "input.h"
#include "item.h"
#include "itype.h"
#include "live_view.h"
#include "map.h"
#include "mapdata.h"
#include "map_item_stack.h"
#include "messages.h"
#include "monster.h"
#include "mongroup.h"
#include "mtype.h"
#include "npc.h"
#include "omdata.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "player_activity.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "rng.h"
#include "rot.h"
#include "sdltiles.h"
#include "cata_tiles.h"
#include "sounds.h"
#include "units_utility.h"
#include "string_formatter.h"
#include "translations.h"
#include "trap.h"
#include "vpart_range.h"
#include "vehicle_part.h"
#include "type_id.h"
#include "mapsharing.h"
#include "safemode_ui.h"
#include "ui.h"
#include "ui_manager.h"
#include "uistate.h"
#include "units.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "world.h"
#include "overmap_ui.h"
#include "panels.h"
#include "popup.h"
#include "string_input_popup.h"
#include "profile.h"
#include "game_inventory.h"
#include "item_category.h"

// File-local string-ID statics referenced by extracted functions
static const efftype_id effect_blind( "blind" );
static const trap_str_id tr_unfinished_construction( "tr_unfinished_construction" );
static const trait_id trait_ILLITERATE( "ILLITERATE" );
static const trait_id trait_INATTENTIVE( "INATTENTIVE" );
static const skill_id skill_survival( "survival" );


// Callback helpers relocated from game.cpp (no remaining callers there after B5-4 extraction).
// draw_trail implementation lives at the bottom of this TU.
static void draw_trail( const tripoint_bub_ms &start, const tripoint_bub_ms &end, bool bDrawX );

struct zone_callback_options {
    std::optional<tripoint_abs_ms> &zone_start;
    std::optional<tripoint_abs_ms> &zone_end;
    bool &zone_blink;
    bool &zone_cursor;
    std::function<std::vector<tripoint_bub_ms>( const tripoint_abs_ms &, const tripoint_abs_ms & )>
    point_generator;
    bool is_moving_zone = false;
};

static auto create_zone_callback( const zone_callback_options &options ) ->
shared_ptr_fast<game::draw_callback_t>
{
    auto &zone_start = options.zone_start;
    auto &zone_end = options.zone_end;
    auto &zone_cursor = options.zone_cursor;
    auto point_generator = options.point_generator;
    const auto is_moving_zone = options.is_moving_zone;
    ( void ) options.zone_blink;
    return make_shared_fast<game::draw_callback_t>(
    [ &, point_generator = std::move( point_generator ), is_moving_zone]() {
        if( zone_cursor ) {
            if( is_moving_zone ) {
                g->draw_cursor( abs_to_bub( tripoint_abs_ms( ( zone_start.value().raw() + zone_end.value().raw() ) /
                                            2 ) ) );
            } else {
                if( zone_end ) {
                    g->draw_cursor( abs_to_bub( zone_end.value() ) );
                } else if( zone_start ) {
                    g->draw_cursor( abs_to_bub( zone_start.value() ) );
                }
            }
        }
        if( zone_start && zone_end ) {
            const tripoint_rel_ms offset = tripoint_rel_ms::zero(); //TILES

            const tripoint_abs_ms start( std::min( zone_start->x(), zone_end->x() ),
                                         std::min( zone_start->y(), zone_end->y() ),
                                         zone_end->z() );
            const tripoint_abs_ms end( std::max( zone_start->x(), zone_end->x() ),
                                       std::max( zone_start->y(), zone_end->y() ),
                                       zone_end->z() );
            auto points = std::vector<tripoint_bub_ms>();
            if( point_generator ) {
                points = point_generator( start, end );
            }
            auto zone_options = zone_draw_options{
                .start = abs_to_bub( start ),
                .end = abs_to_bub( end ),
                .offset = offset,
                .points = std::move( points )
            };
            g->draw_zones( zone_options );
        }
    } );
}

static shared_ptr_fast<game::draw_callback_t> create_trail_callback(
    const std::optional<tripoint_bub_ms> &trail_start,
    const std::optional<tripoint_bub_ms> &trail_end,
    const bool &trail_end_x
)
{
    return make_shared_fast<game::draw_callback_t>(
    [&]() {
        if( trail_start && trail_end ) {
            draw_trail( trail_start.value(), trail_end.value(), trail_end_x );
        }
    } );
}

// get_fire_fuel_string is also static in game.cpp (used by game::examine there);
// this is an independent file-local copy for print_all_tile_info_text.
static std::string get_fire_fuel_string( const tripoint_bub_ms &examp )
{
    map &here = get_map();
    if( here.has_flag( TFLAG_FIRE_CONTAINER, examp ) ) {
        field_entry *fire = here.get_field( examp, fd_fire );
        if( fire ) {
            std::string ss;
            ss += _( "There is a fire here." );
            ss += " ";
            if( fire->get_field_intensity() > 1 ) {
                ss += _( "It's too big and unpredictable to evaluate how long it will last." );
                return ss;
            }
            time_duration fire_age = fire->get_field_age();
            int mod = 5 - g->u.get_skill_level( skill_survival );
            mod = std::max( mod, 0 );
            if( fire_age >= 0_turns ) {
                if( mod >= 4 ) {
                    ss += _( "It's going to go out soon without extra fuel." );
                    return ss;
                } else {
                    fire_age = 30_minutes - fire_age;
                    if( to_string_approx( fire_age - fire_age * mod / 5 ) == to_string_approx(
                            fire_age + fire_age * mod / 5 ) ) {
                        ss += string_format(
                                  _( "Without extra fuel it might burn yet for maybe %s, but might also go out sooner." ),
                                  to_string_approx( fire_age - fire_age * mod / 5 ) );
                    } else {
                        ss += string_format(
                                  _( "Without extra fuel it might burn yet for between %s to %s, but might also go out sooner." ),
                                  to_string_approx( fire_age - fire_age * mod / 5 ),
                                  to_string_approx( fire_age + fire_age * mod / 5 ) );
                    }
                    return ss;
                }
            } else {
                fire_age = fire_age * -1 + 30_minutes;
                if( mod >= 4 ) {
                    if( fire_age <= 1_hours ) {
                        ss += _( "It's quite decent and looks like it'll burn for a bit without extra fuel." );
                        return ss;
                    } else if( fire_age <= 3_hours ) {
                        ss += _( "It looks solid, and will burn for a few hours without extra fuel." );
                        return ss;
                    } else {
                        ss += _( "It's very well supplied and even without extra fuel might burn for at least a part of a day." );
                        return ss;
                    }
                } else {
                    if( to_string_approx( fire_age - fire_age * mod / 5 ) == to_string_approx(
                            fire_age + fire_age * mod / 5 ) ) {
                        ss += string_format( _( "Without extra fuel it will burn for about %s." ),
                                             to_string_approx( fire_age - fire_age * mod / 5 ) );
                    } else {
                        ss += string_format( _( "Without extra fuel it will burn for between %s to %s." ),
                                             to_string_approx( fire_age - fire_age * mod / 5 ),
                                             to_string_approx( fire_age + fire_age * mod / 5 ) );
                    }
                    return ss;
                }
            }
        }
    }
    return {};
}

std::optional<tripoint_bub_ms> game::look_debug()
{
    editmap edit;
    return edit.edit();
}
////////////////////////////////////////////////////////////////////////////////////////////

void game::draw_look_around_cursor( const tripoint_bub_ms &lp,
                                    const visibility_variables &/* cache */ )
{
    if( !liveview.is_enabled() ) {
        draw_cursor( lp );
    }
}

std::string game::print_all_tile_info_text( const tripoint_bub_ms &lp,
        const std::string &area_name, const visibility_variables &cache )
{
    // Parallel to print_all_tile_info, producing the same content as one
    // colour-tagged string for the look_around RmlUi info pane. The curses
    // print_* helpers above are left untouched (A/B toggle). Curses column
    // alignment / window scroll windowing are dropped (semantic rewrite).
    std::vector<std::string> out;

    visibility_type visibility = VIS_HIDDEN;
    const bool inbounds = m.inbounds( lp );
    if( inbounds ) {
        visibility = m.get_visibility( m.apparent_light_at( lp, cache ), cache );
    }
    const Creature *creature = critter_at( lp, true );

    if( visibility == VIS_CLEAR ) {
        const optional_vpart_position vp = m.veh_at( lp );

        // --- terrain (cf. print_terrain_info) ---
        const ter_t &terrain = m.ter( lp ).obj();
        const oter_id &cur_ter_m = get_overmapbuffer( current_dimension_id_ ).ter(
                                       tripoint_abs_omt( project_to<coords::omt>( m.bub_to_abs( lp ) ) ) );
        const nc_color location_color = cur_ter_m->get_color( uistate.overmap_show_land_use_codes );
        const int move_cost = m.move_cost( lp );
        const bool mc0 = move_cost == 0;
        const std::string move_cost_str = mc0 ? _( "Impassable" )
                                          : string_format( _( "Move cost: %d" ), move_cost * 50 );
        const std::pair<std::string, nc_color> ll = get_light_level( std::max( 1.0,
            LIGHT_AMBIENT_LIT - m.ambient_light_at( lp ) + 1.0 ) );
        out.emplace_back( colorize( area_name, location_color ) + "  " +
                          colorize( move_cost_str, mc0 ? c_light_red : c_light_gray ) );
        out.emplace_back( colorize( m.tername( lp ), terrain.color() ) + "  " +
                          colorize( ll.first, ll.second ) );
        const std::string terrain_desc = terrain.description.translated();
        if( !terrain_desc.empty() ) {
            out.emplace_back( colorize( terrain_desc, c_light_gray ) );
        }
        if( m.has_furn( lp ) ) {
            const furn_t &furniture = m.furn( lp ).obj();
            out.emplace_back( colorize( m.furnname( lp ), furniture.color() ) );
            const std::string fd = furniture.description.translated();
            if( !fd.empty() ) {
                out.emplace_back( colorize( fd, c_light_gray ) );
            }
        }
        const int coverage = m.coverage( lp );
        if( coverage > 0 ) {
            out.emplace_back( colorize( string_format( _( "Cover: %d%%" ), coverage ), c_dark_gray ) );
        }
        const int block_chance = m.obstacle_coverage( u.bub_pos(), lp );
        if( block_chance > 0 ) {
            out.emplace_back( colorize( string_format( _( "Block: %d%%" ), block_chance ), c_dark_gray ) );
        }
        const std::string feats = m.features( lp );
        if( !feats.empty() ) {
            out.emplace_back( colorize( feats, c_dark_gray ) );
        }
        const std::string signage = m.get_signage( lp );
        if( !signage.empty() ) {
            const std::string sign_string = u.has_trait( trait_ILLITERATE ) ? "???" : signage;
            out.emplace_back( colorize( string_format( _( "Sign: %s" ), sign_string ), c_light_gray ) );
        }
        if( m.has_zlevels() && lp.z() > -OVERMAP_DEPTH && !m.has_floor( lp ) ) {
            tripoint_bub_ms below( lp.xy(), lp.z() - 1 );
            std::string tile_below = m.tername( below );
            if( m.has_furn( below ) ) {
                tile_below += ", " + m.furnname( below );
            }
            out.emplace_back( colorize( string_format(
                                            m.has_floor_or_support( lp ) ? _( "Below: %s; Walkable" ) :
                                            _( "Below: %s; No support" ), tile_below ), c_dark_gray ) );
        }

        // --- fields (cf. print_fields_info) ---
        const field &tmpfield = m.field_at( lp );
        for( const auto &fld : tmpfield ) {
            const field_entry &cur = fld.second;
            if( fld.first.obj().has_fire && ( m.has_flag( TFLAG_FIRE_CONTAINER, lp ) ||
                                              m.ter( lp ) == t_pit_shallow || m.ter( lp ) == t_pit ) ) {
                out.emplace_back( colorize( get_fire_fuel_string( lp ), cur.color() ) );
            } else {
                out.emplace_back( colorize( cur.name(), cur.color() ) );
            }
        }

        // --- trap (cf. print_trap_info) ---
        const trap &tr = m.tr_at( lp );
        if( tr.can_see( lp, u ) ) {
            partial_con *pc = m.partial_con_at( lp );
            std::string tr_name;
            if( pc && tr.loadid == tr_unfinished_construction ) {
                const construction &built = pc->id.obj();
                tr_name = string_format( _( "Unfinished task: %s, %d%% complete" ),
                                         built.group->name(), pc->counter / 100000 );
            } else {
                tr_name = tr.name();
            }
            out.emplace_back( colorize( tr_name, tr.color ) );
        }

        // --- creature (the shared producer; cf. print_creature_info) ---
        if( creature != nullptr && ( u.sees( *creature ) || creature == &u ) ) {
            const std::string ci = creature->print_info_text();
            if( !ci.empty() ) {
                out.emplace_back( ci );
            }
        }

        // --- vehicle (reuses part_list_text; cf. print_vehicle_info) ---
        if( const vehicle *veh = veh_pointer_or_null( vp ) ) {
            out.emplace_back( colorize( veh->name, c_white ) );
            const std::string pl = veh->part_list_text( vp ? vp->part_index() : -1 );
            if( !pl.empty() ) {
                out.emplace_back( pl );
            }
        }

        // --- items (cf. print_items_info) ---
        if( m.sees_some_items( lp, u ) ) {
            if( m.has_flag( "CONTAINER", lp ) && !m.could_see_items( lp, u ) ) {
                out.emplace_back( _( "You cannot see what is inside of it." ) );
            } else if( ( u.has_effect( effect_blind ) || u.worn_with_flag( flag_BLIND ) ) &&
                       u.clairvoyance() < 1 ) {
                out.emplace_back( colorize(
                                      _( "There's something there, but you can't see what it is." ), c_yellow ) );
            } else {
                std::map<std::string, std::pair<int, nc_color>> item_names;
                for( auto &it : m.i_at( lp ) ) {
                    ++item_names[it->tname()].first;
                    item_names[it->tname()].second = it->color_in_inventory();
                }
                for( const auto &in : item_names ) {
                    const std::string label = in.second.first > 1
                                              ? string_format( "%s [%d]", in.first, in.second.first )
                                              : in.first;
                    out.emplace_back( colorize( label, in.second.second ) );
                }
            }
        }

        // --- graffiti (cf. print_graffiti_info) ---
        if( m.has_graffiti_at( lp ) ) {
            out.emplace_back( colorize( string_format(
                                            m.ter( lp ) == t_grave_new ? _( "Graffiti: %s" ) : _( "Inscription: %s" ),
                                            m.graffiti_at( lp ) ), c_light_gray ) );
        }
    } else {
        // --- reduced visibility (cf. print_visibility_info + infrared/specials) ---
        const char *visibility_message = nullptr;
        switch( visibility ) {
            case VIS_BOOMER:
                visibility_message = _( "A bright pink blur." );
                break;
            case VIS_BOOMER_DARK:
                visibility_message = _( "A pink blur." );
                break;
            case VIS_DARK:
                visibility_message = _( "Darkness." );
                break;
            case VIS_LIT:
                visibility_message = _( "Bright light." );
                break;
            case VIS_HIDDEN:
            default:
                visibility_message = _( "Unseen." );
                break;
        }
        out.emplace_back( colorize( visibility_message, c_light_gray ) );

        if( creature != nullptr ) {
            std::vector<std::string> buf;
            if( u.sees_with_infrared( *creature ) ) {
                creature->describe_infrared( buf );
            } else if( u.sees_with_specials( *creature ) ) {
                creature->describe_specials( buf );
            }
            for( const std::string &s : buf ) {
                out.emplace_back( s );
            }
        }
    }

    if( inbounds ) {
        const auto this_sound = sounds::sound_at( lp );
        if( !this_sound.empty() ) {
            out.emplace_back( string_format( _( "You heard %s from here." ), this_sound ) );
        } else {
            auto tmp = lp;
            for( tmp.z() = -OVERMAP_DEPTH; tmp.z() <= OVERMAP_HEIGHT; tmp.z()++ ) {
                if( tmp.z() == lp.z() ) {
                    continue;
                }
                const auto zlev_sound = sounds::sound_at( tmp );
                if( !zlev_sound.empty() ) {
                    out.emplace_back( string_format( tmp.z() > lp.z() ?
                                                     _( "You heard %s from above." ) :
                                                     _( "You heard %s from below." ), zlev_sound ) );
                }
            }
        }
    }

    std::string res;
    for( size_t i = 0; i < out.size(); i++ ) {
        if( i > 0 ) {
            res += '\n';
        }
        res += out[i];
    }
    return res;
}

bool game::check_zone( const zone_type_id &type, const tripoint_bub_ms &where ) const
{
    return zone_manager::get_manager().has( type, m.bub_to_abs( where ) );
}

bool game::check_near_zone( const zone_type_id &type, const tripoint_bub_ms &where ) const
{
    return zone_manager::get_manager().has_near( type, m.bub_to_abs( where ) );
}

bool game::is_zones_manager_open() const
{
    return zones_manager_open;
}

bool game::is_zone_submap_grid_overlay_enabled() const
{
    return zone_submap_grid_overlay;
}

// ---- zones_manager RmlUi render path (P3 track-A) --------------------------
// The zones manager (`Y` screen). Render-only doc, sibling of list_items: the
// keyboard owns add/remove/enable/disable/move/edit and the overlay toggles; the
// model is synced each frame. The map cursor + zone overlay stay on the map path.
// Three sections: the scrolling zone list, the active zone's options block, and a
// shortcut footer. Hidden during the nested point-selection look_around (the
// curses `show` gate).
namespace
{
struct zm_rml_row {
    Rml::String name_rml;
    Rml::String type_rml;
    Rml::String dist_rml;
    Rml::String veh_rml;
    bool selected = false;
};
struct zm_rml_opt {
    Rml::String key_rml;
    Rml::String val_rml;
};
struct zm_rml_data {
    Rml::String header_rml;
    Rml::Vector<zm_rml_row> rows;
    bool empty = false;
    Rml::String empty_rml;
    bool has_options = false;
    Rml::String options_title_rml;
    Rml::Vector<zm_rml_opt> options;
    Rml::String footer_rml;
    Rml::DataModelHandle handle;
};

bool g_zones_manager_types_registered = false;

void register_zones_manager_rml_types( Rml::DataModelConstructor &c )
{
    // RegisterStruct/Array are context-global and persist past RemoveDataModel —
    // guard so a reopen doesn't double-register (uilist-proven pattern).
    if( g_zones_manager_types_registered ) {
        return;
    }
    Rml::StructHandle<zm_rml_row> rh = c.RegisterStruct<zm_rml_row>();
    rh.RegisterMember( "name_rml", &zm_rml_row::name_rml );
    rh.RegisterMember( "type_rml", &zm_rml_row::type_rml );
    rh.RegisterMember( "dist_rml", &zm_rml_row::dist_rml );
    rh.RegisterMember( "veh_rml", &zm_rml_row::veh_rml );
    rh.RegisterMember( "selected", &zm_rml_row::selected );
    c.RegisterArray<Rml::Vector<zm_rml_row>>();
    Rml::StructHandle<zm_rml_opt> oh = c.RegisterStruct<zm_rml_opt>();
    oh.RegisterMember( "key_rml", &zm_rml_opt::key_rml );
    oh.RegisterMember( "val_rml", &zm_rml_opt::val_rml );
    c.RegisterArray<Rml::Vector<zm_rml_opt>>();
    g_zones_manager_types_registered = true;
}
} // namespace

bool &zones_manager_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

void game::zones_manager()
{
    const auto stored_view_offset = u.view_offset;

    u.view_offset = tripoint_rel_ms::zero();

    const int zone_ui_height = 12;
    const int zone_options_height = 7;

    const int width = 45;

    int offsetX = 0;
    int max_rows = 0;

    catacurses::window w_zones;
    catacurses::window w_zones_border;
    catacurses::window w_zones_info;
    catacurses::window w_zones_info_border;
    catacurses::window w_zones_options;

    bool show = true;

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        if( !show ) {
            ui.position( point_zero, point_zero );
            return;
        }
        offsetX = get_option<std::string>( "SIDEBAR_POSITION" ) != "left" ?
                  TERMX - width : 0;
        const int w_zone_height = TERMY - zone_ui_height;
        max_rows = w_zone_height - 2;
        w_zones = catacurses::newwin( w_zone_height - 2, width - 2,
                                      point( offsetX + 1, 1 ) );
        w_zones_border = catacurses::newwin( w_zone_height, width,
                                             point( offsetX, 0 ) );
        w_zones_info = catacurses::newwin( zone_ui_height - zone_options_height - 1,
                                           width - 2, point( offsetX + 1, w_zone_height ) );
        w_zones_info_border = catacurses::newwin( zone_ui_height, width,
                              point( offsetX, w_zone_height ) );
        w_zones_options = catacurses::newwin( zone_options_height - 1, width - 2,
                                              point( offsetX + 1, TERMY - zone_options_height ) );

        ui.position( point( offsetX, 0 ), point( width, TERMY ) );
    } );
    ui.mark_resize();

    std::string action;
    input_context ctxt( "ZONES_MANAGER" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "ADD_ZONE" );
    ctxt.register_action( "REMOVE_ZONE" );
    ctxt.register_action( "MOVE_ZONE_UP" );
    ctxt.register_action( "MOVE_ZONE_DOWN" );
    ctxt.register_action( "SHOW_ZONE_ON_MAP" );
    ctxt.register_action( "ENABLE_ZONE" );
    ctxt.register_action( "DISABLE_ZONE" );
    ctxt.register_action( "SHOW_ALL_ZONES" );
    ctxt.register_action( "TOGGLE_ZONE_OVERLAY" );
    ctxt.register_action( "debug_submap_grid" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    auto &mgr = zone_manager::get_manager();
    int start_index = 0;
    int active_index = 0;
    bool stuff_changed = false;
    bool show_all_zones = false;
    int zone_cnt = 0;

    // get zones on the same z-level, with distance between player and
    // zone center point <= 50 or all zones, if show_all_zones is true
    auto get_zones = [&]() {
        std::vector<zone_manager::ref_zone_data> zones;
        if( show_all_zones ) {
            zones = mgr.get_zones();
        } else {
            const auto &u_abs_pos = m.bub_to_abs( u.bub_pos() );
            for( zone_manager::ref_zone_data &ref : mgr.get_zones() ) {
                const auto &zone_abs_pos = ref.get().get_center_point();
                if( u_abs_pos.z() == zone_abs_pos.z() && rl_dist( u_abs_pos, zone_abs_pos ) <= 50 ) {
                    zones.emplace_back( ref );
                }
            }
        }
        zone_cnt = static_cast<int>( zones.size() );
        return zones;
    };

    auto zones = get_zones();

    std::optional<tripoint_abs_ms> zone_start;
    std::optional<tripoint_abs_ms> zone_end;
    auto zone_blink = false;
    auto zone_cursor = false;
    auto current_zone_type = zone_type_id();
    shared_ptr_fast<const blueprint_options> current_bp_options;
    static const auto zone_construction_blueprint = zone_type_id( "CONSTRUCTION_BLUEPRINT" );
    auto zone_point_generator =
    [&]( const tripoint_abs_ms & start, const tripoint_abs_ms & end ) -> std::vector<tripoint_bub_ms> {
        if( current_zone_type == zone_construction_blueprint )
        {
            if( current_bp_options ) {
                const std::vector<tripoint_abs_ms> covered_points = current_bp_options->get_covered_points( start,
                    end );
                auto points = covered_points
                | std::views::transform( []( const tripoint_abs_ms & p ) {
                    return abs_to_bub( p );
                } )
                | std::ranges::to<std::vector>();
                return points;
            }
        }
        return std::vector<tripoint_bub_ms>();
    };
    shared_ptr_fast<draw_callback_t> zone_cb = create_zone_callback( zone_callback_options{
        .zone_start = zone_start,
        .zone_end = zone_end,
        .zone_blink = zone_blink,
        .zone_cursor = zone_cursor,
        .point_generator = zone_point_generator,
    } );
    add_draw_callback( zone_cb );

    auto query_position =
    [&]() -> std::optional<std::pair<tripoint_abs_ms, tripoint_abs_ms>> {
        on_out_of_scope invalidate_current_ui( [&]()
        {
            ui.mark_resize();
        } );
        restore_on_out_of_scope<bool> show_prev( show );
        restore_on_out_of_scope<std::optional<tripoint_abs_ms>> zone_start_prev( zone_start );
        restore_on_out_of_scope<std::optional<tripoint_abs_ms>> zone_end_prev( zone_end );
        restore_on_out_of_scope<bool> zone_cursor_prev( zone_cursor );
        show = false;
        zone_start = std::nullopt;
        zone_end = std::nullopt;
        zone_cursor = true;
        ui.mark_resize();

        static_popup popup;
        popup.on_top( true );
        popup.message( "%s", _( "Select first point." ) );

        auto center = u.bub_pos() + u.view_offset;

        const look_around_result first = look_around( /*show_window=*/false, center, center, false, true,
            false );
        if( first.position )
    {
        popup.message( "%s", _( "Select second point." ) );

            const look_around_result second = look_around( /*show_window=*/false, center, *first.position,
                true, true, false );
            if( second.position ) {
                auto first_abs = m.bub_to_abs( tripoint_bub_ms( std::min( first.position->x(),
                                               second.position->x() ),
                                               std::min( first.position->y(), second.position->y() ),
                                               std::min( first.position->z(),
                                                   second.position->z() ) ) );
                auto second_abs = m.bub_to_abs( tripoint_bub_ms( std::max( first.position->x(),
                                                second.position->x() ),
                                                std::max( first.position->y(), second.position->y() ),
                                                std::max( first.position->z(),
                                                    second.position->z() ) ) );
                return std::pair<tripoint_abs_ms, tripoint_abs_ms>( first_abs, second_abs );
            }
        }

        return std::nullopt;
    };

    // ---- RmlUi render path (F.3 rml_doc harness, sibling of list_items) ------
    // `rml_data` before `rml` so the doc tears down while the model is alive. The
    // doc is rebuilt each frame from the live zone list + cursor; the keyboard
    // owns all editing. Native scroll replaces the curses calcStartPos windowing.
    // During the nested point-selection look_around (show == false) the doc is
    // hidden, matching the curses path zeroing the windows.
    std::unique_ptr<zm_rml_data> rml_data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml || !rml_data ) {
            return;
        }
        zm_rml_data &d = *rml_data;

        d.header_rml = cata_text_to_rml( colorize( _( "Zones manager" ), c_white ) );
        d.empty = zone_cnt == 0;
        d.empty_rml = rml_escape( _( "No Zones defined." ) );

        // Zone rows: name / type / distance-direction / vehicle marker. The active
        // row is recoloured (light_green/green) exactly as the curses body; the
        // shared .selected highlight adds the accent background.
        d.rows.clear();
        const auto player_absolute_pos = m.bub_to_abs( u.bub_pos() );
        for( int i = 0; i < zone_cnt; ++i ) {
            const auto &zone = zones[i].get();
            const bool selected = i == active_index;
            nc_color colorLine = zone.get_enabled() ? c_white : c_light_gray;
            if( selected ) {
                colorLine = zone.get_enabled() ? c_light_green : c_green;
            }
            zm_rml_row row;
            row.selected = selected;
            row.name_rml = cata_text_to_rml( colorize( trim_by_length( zone.get_name(), 15 ), colorLine ) );
            row.type_rml = cata_text_to_rml( colorize( mgr.get_name_from_type( zone.get_type() ),
                                             colorLine ) );
            const auto center = zone.get_center_point();
            row.dist_rml = cata_text_to_rml( colorize(
                                                 string_format( "%d %s",
                                                     static_cast<int>( trig_dist( player_absolute_pos, center ) ),
                                                     direction_name_short( direction_from( player_absolute_pos, center ) ) ),
                                                 colorLine ) );
            row.veh_rml = cata_text_to_rml( colorize( zone.get_is_vehicle() ? "*" : "", colorLine ) );
            d.rows.push_back( std::move( row ) );
        }

        // Active zone's options block (key→value descriptions).
        d.options.clear();
        d.has_options = false;
        d.options_title_rml = Rml::String();
        if( zone_cnt > 0 ) {
            const auto &zone = zones[active_index].get();
            if( zone.has_options() ) {
                const auto &descriptions = zone.get_options().get_descriptions();
                if( !descriptions.empty() ) {
                    d.has_options = true;
                    d.options_title_rml = cata_text_to_rml( colorize( _( "Options" ), c_white ) );
                    for( const auto &desc : descriptions ) {
                        zm_rml_opt o;
                        o.key_rml = cata_text_to_rml( colorize( desc.first, c_white ) );
                        o.val_rml = cata_text_to_rml( colorize( desc.second, c_white ) );
                        d.options.push_back( std::move( o ) );
                    }
                }
            }
        }

        // Shortcut footer. <O>/<G> tokens are live-coloured by overlay / submap-grid state.
        const nc_color o_col = g->show_zone_overlay ? c_light_green : c_white;
        const nc_color g_col = ( g->debug_submap_grid_overlay || zone_submap_grid_overlay )
                               ? c_light_green : c_white;
        std::string f;
        f += colorize( _( "<A>dd" ), c_light_green ) + "  "
             + colorize( _( "<R>emove" ), c_light_green ) + "  "
             + colorize( _( "<E>nable" ), c_light_green ) + "  "
             + colorize( _( "<D>isable" ), c_light_green ) + "\n";
        f += colorize( _( "<+-> Move up/down" ), c_light_green ) + "  "
             + colorize( _( "<Enter>-Edit" ), c_light_green ) + "\n";
        f += colorize( _( "<S>how all / hide distant" ), c_light_green ) + "  "
             + colorize( _( "<M>ap" ), c_light_green ) + "\n";
        f += colorize( _( "<O> - Toggle Overlays" ), o_col ) + "  "
             + colorize( _( "<G> - Submap grid" ), g_col );
        d.footer_rml = cata_text_to_rml( f );

        d.handle.DirtyVariable( "header_rml" );
        d.handle.DirtyVariable( "rows" );
        d.handle.DirtyVariable( "empty" );
        d.handle.DirtyVariable( "empty_rml" );
        d.handle.DirtyVariable( "has_options" );
        d.handle.DirtyVariable( "options_title_rml" );
        d.handle.DirtyVariable( "options" );
        d.handle.DirtyVariable( "footer_rml" );
    };
    rml.open( zones_manager_rmlui_enabled(), "zones_manager", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        rml_data = std::make_unique<zm_rml_data>();
        register_zones_manager_rml_types( c );
        c.Bind( "header_rml", &rml_data->header_rml );
        c.Bind( "rows", &rml_data->rows );
        c.Bind( "empty", &rml_data->empty );
        c.Bind( "empty_rml", &rml_data->empty_rml );
        c.Bind( "has_options", &rml_data->has_options );
        c.Bind( "options_title_rml", &rml_data->options_title_rml );
        c.Bind( "options", &rml_data->options );
        c.Bind( "footer_rml", &rml_data->footer_rml );
        rml_data->handle = c.GetModelHandle();
    } );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        // RmlUi path owns the panel — hide during the nested look_around, else
        // sync the model and skip the curses draw.
        if( rml ) {
            rml.document()->SetProperty( "visibility", show ? "visible" : "hidden" );
            if( show ) {
                sync_rml();
            }
            return;
        }
    } );

    zones_manager_open = true;
    do {
        if( action == "ADD_ZONE" ) {
            do { // not a loop, just for quick bailing out if canceled
                const auto maybe_id = mgr.query_type();
                if( !maybe_id.has_value() ) {
                    break;
                }

                const zone_type_id &id = maybe_id.value();
                auto options = zone_options::create( id );

                if( !options->query_at_creation() ) {
                    break;
                }

                auto default_name = options->get_zone_name_suggestion();
                if( default_name.empty() ) {
                    default_name = mgr.get_name_from_type( id );
                }
                const auto maybe_name = mgr.query_name( default_name );
                if( !maybe_name.has_value() ) {
                    break;
                }
                const std::string &name = maybe_name.value();

                current_zone_type = id;
                current_bp_options = std::dynamic_pointer_cast<const blueprint_options>( options );
                std::optional<std::pair<tripoint_abs_ms, tripoint_abs_ms>> position;
                position = query_position();
                if( !position ) {
                    break;
                }

                mgr.add( name, id, g->u.get_faction()->id(), false, true, position->first,
                         position->second, options );

                zones = get_zones();
                active_index = zone_cnt - 1;

                stuff_changed = true;
            } while( false );
        } else if( action == "SHOW_ALL_ZONES" ) {
            show_all_zones = !show_all_zones;
            zones = get_zones();
            active_index = 0;
        } else if( action == "TOGGLE_ZONE_OVERLAY" ) {
            g->show_zone_overlay = !g->show_zone_overlay;
        } else if( action == "debug_submap_grid" ) {
            zone_submap_grid_overlay = !zone_submap_grid_overlay;
        } else if( zone_cnt > 0 ) {
            if( action == "UP" ) {
                active_index--;
                if( active_index < 0 ) {
                    active_index = zone_cnt - 1;
                }
            } else if( action == "DOWN" ) {
                active_index++;
                if( active_index >= zone_cnt ) {
                    active_index = 0;
                }
            } else if( action == "REMOVE_ZONE" ) {
                if( active_index < zone_cnt ) {
                    mgr.remove( zones[active_index] );
                    zones = get_zones();
                    active_index--;

                    active_index = std::max( active_index, 0 );
                }
                stuff_changed = true;

            } else if( action == "CONFIRM" ) {
                auto &zone = zones[active_index].get();

                uilist as_m;
                as_m.text = _( "What do you want to change:" );
                as_m.entries.emplace_back( 1, true, '1', _( "Edit name" ) );
                as_m.entries.emplace_back( 2, true, '2', _( "Edit type" ) );
                as_m.entries.emplace_back( 3, zone.get_options().has_options(), '3',
                                           zone.get_type() == zone_type_id( "LOOT_CUSTOM" ) ? _( "Edit filter" ) : _( "Edit options" ) );
                as_m.entries.emplace_back( 4, !zone.get_is_vehicle(), '4', _( "Edit position" ) );
                // TODO: Enable moving vzone after vehicle zone can be bigger than 1*1
                as_m.entries.emplace_back( 5, !zone.get_is_vehicle(), '5', _( "Move position" ) );
                as_m.query();

                switch( as_m.ret ) {
                    case 1:
                        if( zone.set_name() ) {
                            stuff_changed = true;
                        }
                        break;
                    case 2:
                        if( zone.set_type() ) {
                            stuff_changed = true;
                        }
                        break;
                    case 3:
                        if( zone.get_options().query() ) {
                            stuff_changed = true;
                        }
                        break;
                    case 4: {
                        const auto pos = query_position();
                        if( pos && ( pos->first != zone.get_start_point() ||
                                     pos->second != zone.get_end_point() ) ) {
                            zone.set_position( *pos );
                            stuff_changed = true;
                        }
                        break;
                    }
                    case 5: {
                        on_out_of_scope invalidate_current_ui( [&]() {
                            ui.mark_resize();
                        } );
                        restore_on_out_of_scope<bool> show_prev( show );
                        restore_on_out_of_scope<std::optional<tripoint_abs_ms>> zone_start_prev( zone_start );
                        restore_on_out_of_scope<std::optional<tripoint_abs_ms>> zone_end_prev( zone_end );
                        restore_on_out_of_scope<bool> zone_cursor_prev( zone_cursor );
                        show = false;
                        zone_start = std::nullopt;
                        zone_end = std::nullopt;
                        zone_cursor = true;
                        ui.mark_resize();
                        static_popup message_pop;
                        message_pop.on_top( true );
                        message_pop.message( "%s", _( "Moving zone." ) );
                        const auto zone_local_start_point = m.abs_to_bub( zone.get_start_point() );
                        const auto zone_local_end_point = m.abs_to_bub( zone.get_end_point() );
                        // local position of the zone center, used to calculate the u.view_offset,
                        // could center the screen to the position it represents
                        auto view_center = m.abs_to_bub( zone.get_center_point() );
                        const look_around_result result_local = look_around( false, view_center,
                                                                zone_local_start_point, false, false,
                                                                false, true, zone_local_end_point );
                        if( result_local.position ) {
                            const auto new_start_point = m.bub_to_abs( *result_local.position );
                            if( new_start_point == zone.get_start_point() ) {
                                break; // Nothing changed, don't save
                            }

                            const auto new_end_point = zone.get_end_point() - zone.get_start_point() + new_start_point;
                            zone.set_position( std::pair<tripoint_abs_ms, tripoint_abs_ms>( new_start_point, new_end_point ) );
                            stuff_changed = true;
                        }
                    }
                    break;
                    default:
                        break;
                }

            } else if( action == "MOVE_ZONE_UP" && zone_cnt > 1 ) {
                if( active_index < zone_cnt - 1 ) {
                    mgr.swap( zones[active_index], zones[active_index + 1] );
                    zones = get_zones();
                    active_index++;
                }
                stuff_changed = true;

            } else if( action == "MOVE_ZONE_DOWN" && zone_cnt > 1 ) {
                if( active_index > 0 ) {
                    mgr.swap( zones[active_index], zones[active_index - 1] );
                    zones = get_zones();
                    active_index--;
                }
                stuff_changed = true;

            } else if( action == "SHOW_ZONE_ON_MAP" ) {
                //show zone position on overmap;
                tripoint_abs_omt player_overmap_position = u.abs_omt_pos();
                // TODO: fix point types
                tripoint_abs_omt zone_overmap( project_to<coords::omt>
                                               ( zones[active_index].get().get_center_point() ) );

                ui::omap::display_zones( player_overmap_position, zone_overmap, active_index );
            } else if( action == "ENABLE_ZONE" ) {
                zones[active_index].get().set_enabled( true );

                stuff_changed = true;

            } else if( action == "DISABLE_ZONE" ) {
                zones[active_index].get().set_enabled( false );

                stuff_changed = true;
            }
        }

        if( zone_cnt > 0 ) {
            const auto &zone = zones[active_index].get();
            zone_start = zone.get_start_point();
            zone_end = zone.get_end_point();
            current_zone_type = zone.get_type();
            current_bp_options = std::dynamic_pointer_cast<const blueprint_options>(
                                     zone.get_options_ptr() );
        } else {
            zone_start = zone_end = std::nullopt;
            current_zone_type = zone_type_id();
            current_bp_options = nullptr;
        }

        // Actually accessed from the terrain overlay callback `zone_cb` in the
        // call to `ui_manager::redraw`.
        //NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
        zone_blink = zone_cnt > 0;
        g->invalidate_main_ui_adaptor();

        ui_manager::redraw();

        //Wait for input
        action = ctxt.handle_input();
    } while( action != "QUIT" );
    zones_manager_open = false;
    ctxt.reset_timeout();
    zone_cb = nullptr;

    if( stuff_changed ) {
        auto &zones = zone_manager::get_manager();
        if( query_yn( _( "Save changes?" ) ) ) {
            zones.save_zones();
        } else {
            zones.load_zones();
        }

        zones.cache_data();
    }

    u.view_offset = stored_view_offset;
}

void game::pre_print_all_tile_info( const tripoint_bub_ms &lp, const catacurses::window &w_info,
                                    int &first_line, const int /*last_line*/,
                                    const visibility_variables &cache )
{
    // get global area info according to look_around caret position
    // TODO: fix point types
    const oter_id &cur_ter_m = get_overmapbuffer( current_dimension_id_ ).ter( tripoint_abs_omt(
                                   project_to<coords::omt>( m.bub_to_abs( lp ) ) ) );
    const std::string area_name = cur_ter_m->get_name();
    // The curses print_* helpers were deleted in the tiles-only rip-out; this is
    // now purely a box-sizing helper for live_view — advance `first_line` by the
    // wrapped height of the same colour-tagged text the RmlUi pane renders.
    const std::string info = print_all_tile_info_text( lp, area_name, cache );
    first_line += static_cast<int>( foldstring( info, getmaxx( w_info ) ).size() );
}

std::optional<tripoint_bub_ms> game::look_around( look_around_mode mode,
        const std::optional<tripoint_bub_ms> &start_point )
{
    auto center = u.bub_pos() + u.view_offset;
    look_around_result result = look_around( /*show_window=*/true, center,
                                start_point.value_or( center ),
                                false, false, false, false, tripoint_bub_ms::zero(), mode );
    return result.position;
}

look_around_result game::look_around( bool show_window, tripoint_bub_ms &center,
                                      const tripoint_bub_ms &start_point, bool has_first_point, bool select_zone, bool peeking,
                                      bool is_moving_zone, const tripoint_bub_ms &end_point, look_around_mode mode )
{
    bVMonsterLookFire = false;

    auto zlSwitch = [&]<typename T>( T normal, T m2d, T m3d ) {
        switch( mode ) {
            default:
            case LA_MODE_DEFAULT:
                return normal;
            case LA_MODE_2D:
                return m2d;
            case LA_MODE_3D:
                return m3d;
        };
    };

    // TODO: Make this `true`
    const bool allow_zlev_move = zlSwitch(
                                     m.has_zlevels() && get_option<bool>( "FOV_3D" ),
                                     false,
                                     true
                                 );

    temp_exit_fullscreen();

    auto lp = is_moving_zone ? tripoint_bub_ms( ( start_point.raw() + end_point.raw() ) / 2 ) :
              start_point; // cursor
    int &lx = lp.x();
    int &ly = lp.y();
    int &lz = lp.z();

    int soffset = get_option<int>( "FAST_SCROLL_OFFSET" );
    bool fast_scroll = false;

    std::unique_ptr<ui_adaptor> ui;
    catacurses::window w_info;
    if( show_window ) {
        ui = std::make_unique<ui_adaptor>();
        ui->on_screen_resize( [&]( ui_adaptor & ui ) {
            int panel_width = panel_manager::get_manager().get_current_layout().begin()->get_width();

            const int minimap_height_opt = get_option<int>( "PIXEL_MINIMAP_HEIGHT" );
            const int minimap_height = minimap_height_opt > 0 ? minimap_height_opt : panel_width / 2;
            int height = pixel_minimap_option
                         ? TERMY - minimap_height
                         : TERMY;

            // If particularly small, base height on panel width irrespective of other elements.
            // Value here is attempting to get a square-ish result assuming 1x2 proportioned font.
            if( height < panel_width / 2 ) {
                height = panel_width / 2;
            }

            int la_y = 0;
            int la_x = TERMX - panel_width;
            std::string position = get_option<std::string>( "LOOKAROUND_POSITION" );
            if( position == "left" ) {
                if( get_option<std::string>( "SIDEBAR_POSITION" ) == "right" ) {
                    la_x = panel_manager::get_manager().get_width_left();
                } else {
                    la_x = panel_manager::get_manager().get_width_left() - panel_width;
                }
            }
            int la_h = height;
            int la_w = panel_width;
            w_info = catacurses::newwin( la_h, la_w, point( la_x, la_y ) );

            ui.position_from_window( w_info );
        } );
        ui->mark_resize();
    }

    std::string action;
    input_context ctxt( "LOOK" );
    ctxt.set_iso( true );
    ctxt.register_directions();
    ctxt.register_action( "COORDINATE" );
    ctxt.register_action( "LEVEL_UP" );
    ctxt.register_action( "LEVEL_DOWN" );
    ctxt.register_action( "TOGGLE_FAST_SCROLL" );
    ctxt.register_action( "EXTENDED_DESCRIPTION" );
    ctxt.register_action( "SELECT" );
    if( peeking ) {
        ctxt.register_action( "throw_blind" );
    }
    if( !select_zone ) {
        ctxt.register_action( "TRAVEL_TO" );
        ctxt.register_action( "LIST_ITEMS" );
    }
    ctxt.register_action( "MOUSE_MOVE" );
    ctxt.register_action( "CENTER" );

    ctxt.register_action( "debug_scent" );
    ctxt.register_action( "debug_scent_type" );
    ctxt.register_action( "debug_temp" );
    ctxt.register_action( "debug_visibility" );
    ctxt.register_action( "debug_lighting" );
    ctxt.register_action( "debug_radiation" );
    ctxt.register_action( "debug_outside" );
    ctxt.register_action( "debug_sound_absorption" );
    ctxt.register_action( "debug_sound_walls" );
    ctxt.register_action( "debug_submap_grid" );
    ctxt.register_action( "debug_hour_timer" );
    ctxt.register_action( "debug_fps" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "zoom_out" );
    ctxt.register_action( "zoom_in" );
    ctxt.register_action( "debug_tileset" );
    ctxt.register_action( "toggle_pixel_minimap" );
    ctxt.register_action( "toggle_zone_overlay" );

    const int old_levz = get_levz();
    const int min_levz = zlSwitch( std::max( old_levz - fov_3d_z_range, -OVERMAP_DEPTH ),
                                   old_levz,        -OVERMAP_DEPTH );
    const int max_levz = zlSwitch( std::min( old_levz + fov_3d_z_range, OVERMAP_HEIGHT ), old_levz,
                                   OVERMAP_HEIGHT );

    m.update_visibility_cache( old_levz );
    const visibility_variables &cache = m.get_visibility_variables_cache();

    look_around_result result;

    // ---- look_around RmlUi render path (§8.1 track-A, F.3 rml_doc harness) ----
    // Render-only info pane fed by print_all_tile_info_text() (the parallel
    // tile-readout producer; creature section = the shared Creature::print_info_text()).
    // 4 scalar string binds (title / cursor coords / tile info / footer hints) — no
    // struct/array registration. The map cursor (ter_indicator_cb) + zone overlay stay
    // on the map path. Function-scope so the rml_doc dtor tears down on every exit.
    struct la_rml_data {
        Rml::String header_rml;
        Rml::String cursor_rml;
        Rml::String info_rml;
        Rml::String footer_rml;
        Rml::DataModelHandle handle;
    };
    std::unique_ptr<la_rml_data> rml_data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml || !rml_data ) {
            return;
        }
        la_rml_data &d = *rml_data;
        d.header_rml = cata_text_to_rml( colorize( _( "Look Around" ), c_green ) );
        d.cursor_rml = rml_escape( string_format( _( "Cursor At: (%d,%d,%d)" ), lx, ly, lz ) );
        const oter_id &cur_ter_m = get_overmapbuffer( current_dimension_id_ ).ter(
                                       tripoint_abs_omt( project_to<coords::omt>( m.bub_to_abs( lp ) ) ) );
        d.info_rml = cata_text_to_rml( print_all_tile_info_text( lp, cur_ter_m->get_name(), cache ) );
        const std::string ed = string_format( _( "%s - %s" ), ctxt.get_desc( "EXTENDED_DESCRIPTION" ),
                                              ctxt.get_action_name( "EXTENDED_DESCRIPTION" ) );
        const std::string fs = string_format( _( "%s - %s" ), ctxt.get_desc( "TOGGLE_FAST_SCROLL" ),
                                              ctxt.get_action_name( "TOGGLE_FAST_SCROLL" ) );
        const std::string pm = string_format( _( "%s - %s" ), ctxt.get_desc( "toggle_pixel_minimap" ),
                                              ctxt.get_action_name( "toggle_pixel_minimap" ) );
        d.footer_rml = cata_text_to_rml( colorize( ed, c_light_gray ) + "\n" +
                                         colorize( fs, fast_scroll ? c_light_green : c_green ) + "   " +
                                         colorize( pm, pixel_minimap_option ? c_light_green : c_green ) );
        d.handle.DirtyVariable( "header_rml" );
        d.handle.DirtyVariable( "cursor_rml" );
        d.handle.DirtyVariable( "info_rml" );
        d.handle.DirtyVariable( "footer_rml" );
    };
    rml.open( look_around_rmlui_enabled() && show_window, "look_around", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        rml_data = std::make_unique<la_rml_data>();
        c.Bind( "header_rml", &rml_data->header_rml );
        c.Bind( "cursor_rml", &rml_data->cursor_rml );
        c.Bind( "info_rml", &rml_data->info_rml );
        c.Bind( "footer_rml", &rml_data->footer_rml );
        rml_data->handle = c.GetModelHandle();
    } );

    shared_ptr_fast<draw_callback_t> ter_indicator_cb;

    if( show_window && ui ) {
        ui->on_redraw( [&]( const ui_adaptor & ) {
            // RmlUi owns the info pane (curses fallback removed in the tiles-only rip-out).
            sync_rml();
        } );
        ter_indicator_cb = make_shared_fast<draw_callback_t>( [&]() {
            draw_look_around_cursor( lp, cache );
        } );
        add_draw_callback( ter_indicator_cb );
    }

    std::optional<tripoint_abs_ms> zone_start;
    std::optional<tripoint_abs_ms> zone_end;
    bool zone_blink = false;
    bool zone_cursor = true;
    auto noop_zone_points = []( const tripoint_abs_ms &, const tripoint_abs_ms & ) {
        return std::vector<tripoint_bub_ms>();
    };
    shared_ptr_fast<draw_callback_t> zone_cb = create_zone_callback( zone_callback_options{
        .zone_start = zone_start,
        .zone_end = zone_end,
        .zone_blink = zone_blink,
        .zone_cursor = zone_cursor,
        .point_generator = noop_zone_points,
        .is_moving_zone = is_moving_zone,
    } );
    add_draw_callback( zone_cb );

    is_looking = true;
    const auto prev_offset = u.view_offset;
    const float prev_tileset_zoom = tileset_zoom;
    while( is_moving_zone && square_dist( start_point, end_point ) > 256 / get_zoom() &&
           get_zoom() != 4 ) {
        zoom_out();
    }
    mark_main_ui_adaptor_resize();
    do {
        u.view_offset = center - u.bub_pos();
        if( select_zone ) {
            if( has_first_point ) {
                zone_start = bub_to_abs( start_point );
                zone_end = bub_to_abs( lp );
            } else {
                zone_start = bub_to_abs( lp );
                zone_end = std::nullopt;
            }
            zone_blink = zone_start.has_value();
        }

        if( is_moving_zone ) {
            zone_start = bub_to_abs( lp ) - ( start_point.raw() + end_point.raw() ) / 2 + start_point.raw();
            zone_end = bub_to_abs( lp ) - ( start_point.raw() + end_point.raw() ) / 2 + end_point.raw();
            zone_blink = true;
        }
        g->invalidate_main_ui_adaptor();
        ui_manager::redraw();

        if( pixel_minimap_option ) {
            ctxt.set_timeout( 125 );
        }

        //Wait for input
        // only specify a timeout here if "EDGE_SCROLL" is enabled
        // otherwise use the previously set timeout
        const auto edge_scroll = mouse_edge_scrolling_terrain( ctxt );
        const int scroll_timeout = get_option<int>( "EDGE_SCROLL" );
        const bool edge_scrolling = edge_scroll != tripoint_rel_ms::zero() && scroll_timeout >= 0;
        if( edge_scrolling ) {
            action = ctxt.handle_input( scroll_timeout );
        } else {
            action = ctxt.handle_input();
        }
        ( void ) zone_blink; // kept for callback signature
        if( action == "LIST_ITEMS" ) {
            list_items_monsters();
        } else if( action == "TOGGLE_FAST_SCROLL" ) {
            fast_scroll = !fast_scroll;
        } else if( action == "toggle_pixel_minimap" ) {
            toggle_pixel_minimap();

            if( show_window && ui ) {
                ui->mark_resize();
            }
        } else if( action == "toggle_zone_overlay" ) {
            g->show_zone_overlay = !g->show_zone_overlay;
        } else if( action == "LEVEL_UP" || action == "LEVEL_DOWN" ) {
            if( !allow_zlev_move ) {
                continue;
            }

            const int dz = ( action == "LEVEL_UP" ? 1 : -1 );
            lz = clamp( lz + dz, min_levz, max_levz );
            center.z() = clamp( center.z() + dz, min_levz, max_levz );

            add_msg( m_debug, "levx: %d, levy: %d, levz: %d", get_levx(), get_levy(), center.z() );
            u.view_offset.z() = center.z() - u.bub_pos().z();
            m.invalidate_map_cache( center.z() );
        } else if( action == "TRAVEL_TO" ) {
            if( !u.sees( lp ) ) {
                add_msg( _( "You can't see that destination." ) );
                continue;
            }

            auto route = m.route( u.bub_pos(), lp, u.get_legacy_pathfinding_settings(),
                                  u.get_legacy_path_avoid() );
            if( route.size() > 1 ) {
                route.pop_back();
                u.set_destination( route );
            } else {
                add_msg( m_info, _( "You can't travel there." ) );
                continue;
            }
        } else if( action == "debug_scent" || action == "debug_scent_type" ) {
            if( !MAP_SHARING::isCompetitive() || MAP_SHARING::isDebugger() ) {
                display_scent();
            }
        } else if( action == "debug_temp" ) {
            if( !MAP_SHARING::isCompetitive() || MAP_SHARING::isDebugger() ) {
                display_temperature();
            }
        } else if( action == "debug_lighting" ) {
            if( !MAP_SHARING::isCompetitive() || MAP_SHARING::isDebugger() ) {
                display_lighting();
            }
        } else if( action == "debug_transparency" ) {
            if( !MAP_SHARING::isCompetitive() || MAP_SHARING::isDebugger() ) {
                display_transparency();
            }
        } else if( action == "debug_outside" ) {
            if( !MAP_SHARING::isCompetitive() || MAP_SHARING::isDebugger() ) {
                display_outside();
            }
        } else if( action == "debug_sound_absorption" ) {
            if( !MAP_SHARING::isCompetitive() || MAP_SHARING::isDebugger() ) {
                display_sound_absorption();
            }
        } else if( action == "debug_sound_walls" ) {
            if( !MAP_SHARING::isCompetitive() || MAP_SHARING::isDebugger() ) {
                display_sound_walls();
            }
        } else if( action == "debug_radiation" ) {
            if( !MAP_SHARING::isCompetitive() || MAP_SHARING::isDebugger() ) {
                display_radiation();
            }
        } else if( action == "debug_tileset" ) {
            if( !MAP_SHARING::isCompetitive() || MAP_SHARING::isDebugger() ) {
                display_tiles_no_vfx();
            }
        } else if( action == "debug_submap_grid" ) {
            g->debug_submap_grid_overlay = !g->debug_submap_grid_overlay;
        } else if( action == "debug_hour_timer" ) {
            toggle_debug_hour_timer();
        } else if( action == "debug_fps" ) {
            toggle_debug_fps();
        } else if( action == "EXTENDED_DESCRIPTION" ) {
            extended_description( lp );
        } else if( action == "CENTER" ) {
            center = u.bub_pos();
            lp = u.bub_pos();
            u.view_offset.z() = 0;
        } else if( action == "MOUSE_MOVE" || action == "TIMEOUT" ) {
            // This block is structured this way so that edge scroll can work
            // whether the mouse is moving at the edge or simply stationary
            // at the edge. But even if edge scroll isn't in play, there's
            // other things for us to do here.

            if( edge_scrolling ) {
                center += action == "MOUSE_MOVE" ? edge_scroll * 2 : edge_scroll;
            } else if( action == "MOUSE_MOVE" ) {
                const std::optional<tripoint_bub_ms> mouse_pos = ctxt.get_coordinates( w_terrain );
                if( mouse_pos ) {
                    lx = mouse_pos->x();
                    ly = mouse_pos->y();
                }
            }
        } else if( std::optional<tripoint_rel_ms> vec = ctxt.get_direction( action ) ) {
            if( fast_scroll ) {
                vec->x() *= soffset;
                vec->y() *= soffset;
            }

            lx = lx + vec->x();
            ly = ly + vec->y();
            center.x() = center.x() + vec->x();
            center.y() = center.y() + vec->y();
        } else if( action == "throw_blind" ) {
            result.peek_action = PA_BLIND_THROW;
        } else if( action == "zoom_in" ) {
            center.x() = lp.x();
            center.y() = lp.y();
            zoom_in();
            mark_main_ui_adaptor_resize();
        } else if( action == "zoom_out" ) {
            center.x() = lp.x();
            center.y() = lp.y();
            zoom_out();
            mark_main_ui_adaptor_resize();
        }
    } while( action != "QUIT" && action != "CONFIRM" && action != "SELECT" && action != "TRAVEL_TO" &&
             action != "throw_blind" );

    if( m.has_zlevels() && center.z() != old_levz ) {
        m.invalidate_map_cache( old_levz );
        m.build_map_cache( old_levz );
        u.view_offset.z() = 0;
    }

    ctxt.reset_timeout();
    u.view_offset = prev_offset;
    zone_cb = nullptr;
    is_looking = false;

    reenter_fullscreen();
    bVMonsterLookFire = true;

    if( action == "CONFIRM" || action == "SELECT" ) {
        result.position = is_moving_zone ? abs_to_bub( zone_start.value_or( tripoint_abs_ms::zero() ) ) :
                          lp;
    }

    if( is_moving_zone && get_zoom() != prev_tileset_zoom ) {
        // Reset the tileset zoom to the previous value
        set_zoom( prev_tileset_zoom );
        mark_main_ui_adaptor_resize();
    }

    return result;
}

std::vector<map_item_stack> game::find_nearby_items( int iRadius )
{
    std::map<std::string, map_item_stack> temp_items;
    std::vector<map_item_stack> ret;
    std::vector<std::string> item_order;

    if( u.is_blind() && u.clairvoyance() < 1 ) {
        return ret;
    }

    int range = fov_3d ? ( fov_3d_z_range * 2 ) + 1 : 1;
    int center_z = u.bub_pos().z();

    for( int i = 1; i <= range; i++ ) {
        int z = i % 2 ? center_z - i / 2 : center_z + i / 2;
        for( auto &points_p_it : closest_points_first<tripoint_bub_ms>( {u.bub_pos().xy(), z}, iRadius ) ) {
            if( points_p_it.y() >= u.bub_pos().y() - iRadius && points_p_it.y() <= u.bub_pos().y() + iRadius &&
                u.sees( points_p_it ) &&
                m.sees_some_items( points_p_it, u ) ) {

                for( auto &elem : m.i_at( points_p_it ) ) {
                    const std::string name = elem->tname();
                    const tripoint_rel_ms relative_pos = points_p_it - u.bub_pos();

                    if( std::find( item_order.begin(), item_order.end(), name ) == item_order.end() ) {
                        item_order.push_back( name );
                        temp_items[name] = map_item_stack( elem, relative_pos );
                    } else {
                        temp_items[name].add_at_pos( elem, relative_pos );
                    }
                }
            }
        }
    }

    for( auto &elem : item_order ) {
        ret.push_back( temp_items[elem] );
    }

    return ret;
}

void draw_trail( const tripoint_bub_ms &start, const tripoint_bub_ms &end,
                 const bool /* bDrawX */ )
{
    std::vector<tripoint_bub_ms> pts;
    auto center = g->u.bub_pos() + g->u.view_offset;
    if( start != end ) {
        //Draw trail
        pts = line_to( start, end, 0, 0 );
    } else {
        //Draw point
        pts.emplace_back( start );
    }

    // The tiles trajectory is drawn by game::draw_line (tilecontext->init_draw_line).
    // TODO(tiles-rip-out): re-add the target/z-direction end marker (X/^/v) via the
    // tiles overlay — the old curses w_terrain mvwputch marker was dead and is removed.
    g->draw_line( end, center, pts );
}

void game::draw_trail_to_square( const tripoint_rel_ms &t, bool bDrawX )
{
    ::draw_trail( u.bub_pos(), u.bub_pos() + t, bDrawX );
}

static void centerlistview( const tripoint_rel_ms &active_item_position, int ui_width )
{
    avatar &u = get_avatar();
    if( get_option<std::string>( "SHIFT_LIST_ITEM_VIEW" ) != "false" ) {
        u.view_offset.z() = active_item_position.z();
        if( get_option<std::string>( "SHIFT_LIST_ITEM_VIEW" ) == "centered" ) {
            u.view_offset.x() = active_item_position.x();
            u.view_offset.y() = active_item_position.y();
        } else {
            auto pos( active_item_position.xy() + point( POSX, POSY ) );

            // item/monster list UI is on the right, so get the difference between its
            // width and the width the terrain viewport already gives up on that edge
            // (0 under the floating RmlUi HUD, a real column under the curses sidebar)
            int sidebar_right_adjusted = ui_width - sidebar_terrain_cols_right();
            // if and only if that difference is greater than zero, use that as offset
            int right_offset = sidebar_right_adjusted > 0 ? sidebar_right_adjusted : 0;

            // Convert offset to tile counts, calculate adjusted terrain window width
            // This lets us account for possible differences in terrain width between
            // the normal sidebar and the list-all-whatever display.
            to_map_font_dim_width( right_offset );
            int terrain_width = TERRAIN_WINDOW_WIDTH - right_offset;

            if( pos.x() < 0 ) {
                u.view_offset.x() = pos.x();
            } else if( pos.x() >= terrain_width ) {
                u.view_offset.x() = pos.x() - ( terrain_width - 1 );
            } else {
                u.view_offset.x() = 0;
            }

            if( pos.y() < 0 ) {
                u.view_offset.y() = pos.y();
            } else if( pos.y() >= TERRAIN_WINDOW_HEIGHT ) {
                u.view_offset.y() = pos.y() - ( TERRAIN_WINDOW_HEIGHT - 1 );
            } else {
                u.view_offset.y() = 0;
            }
        }
    }

}

static constexpr int MAXIMUM_ZOOM_LEVEL = 4;
static constexpr int MINIMUM_ZOOM_LEVEL = 64;

static float calc_next_zoom( float cur_zoom, int direction )
{
    const int step_count = get_option<int>( "ZOOM_STEP_COUNT" );
    const double nth_root_2 = std::pow( 2, 1. / step_count );
    // What is our current zoom index:
    // nth_root_2 ** step = cur_zoom
    // log( nth_root_2 ** step ) = log( cur_zoom )
    // step = log(cur_zoom) / log( nth_root_2 )
    const double expected_cur_ndx = log( cur_zoom ) / log( nth_root_2 );

    // Round to closest integer
    const size_t zoom_level = std::round( expected_cur_ndx ) + direction;

    // calculate next zoom value, and wrap if needed
    double next_zoom = std::pow( nth_root_2, zoom_level );
    if( next_zoom < MAXIMUM_ZOOM_LEVEL - 0.0001f ) {
        next_zoom = MINIMUM_ZOOM_LEVEL;
    } else if( next_zoom > MINIMUM_ZOOM_LEVEL + 0.0001f ) {
        next_zoom = MAXIMUM_ZOOM_LEVEL;
    }

    return next_zoom;
}

void game::zoom_out()
{
    tileset_zoom = calc_next_zoom( tileset_zoom, -1 );
    rescale_tileset( tileset_zoom );
}

void game::zoom_out_overmap()
{
    if( overmap_tileset_zoom > MAXIMUM_ZOOM_LEVEL ) {
        overmap_tileset_zoom /= 2;
    } else {
        overmap_tileset_zoom = 64;
    }
    overmap_tilecontext->set_draw_scale( overmap_tileset_zoom );
}

void game::zoom_in()
{
    tileset_zoom = calc_next_zoom( tileset_zoom, 1 );
    rescale_tileset( tileset_zoom );
}

void game::zoom_in_overmap()
{
    if( overmap_tileset_zoom == 64 ) {
        overmap_tileset_zoom = MAXIMUM_ZOOM_LEVEL;
    } else {
        overmap_tileset_zoom *= 2;
    }
    overmap_tilecontext->set_draw_scale( overmap_tileset_zoom );
}

void game::reset_zoom()
{
    tileset_zoom = DEFAULT_TILESET_ZOOM;
    rescale_tileset( tileset_zoom );
}

void game::set_zoom( const float level )
{
    if( tileset_zoom != level ) {
        tileset_zoom = level;
        rescale_tileset( tileset_zoom );
    }
}

float game::get_zoom() const
{
    return tileset_zoom;
}

int game::get_moves_since_last_save() const
{
    return moves_since_last_save;
}

int game::get_user_action_counter() const
{
    return user_action_counter;
}

bool game::take_screenshot( const std::string &path ) const
{
    return save_screenshot( path );
}

bool game::take_screenshot() const
{
    // check that the current '<world>/screenshots' directory exists
    std::string map_directory = get_active_world()->info->folder_path() + "/screenshots/";
    assure_dir_exist( map_directory );

    // build file name: <map_dir>/screenshots/[<character_name>]_<date>.png
    // Date format is a somewhat ISO-8601 compliant GMT time date (except for some characters that wouldn't pass on most file systems like ':').
    std::time_t time = std::time( nullptr );
    std::stringstream date_buffer;
    date_buffer << std::put_time( std::gmtime( &time ), "%F_%H-%M-%S_%z" );
    const std::string tmp_file_name = string_format( "[%s]_%s.png", get_player_character().get_name(),
                                      date_buffer.str() );
    const std::string file_name = ensure_valid_file_name( tmp_file_name );
    const std::string current_file_path = map_directory + file_name;

    // Take a screenshot of the viewport.
    if( take_screenshot( current_file_path ) ) {
        popup( _( "Successfully saved your screenshot to: %s" ), map_directory );
        return true;
    } else {
        popup( _( "An error occurred while trying to save the screenshot." ) );
        return false;
    }
}

struct nearby_vehicle_entry {
    vehicle *veh = nullptr;
    tripoint_bub_ms pos = tripoint_bub_ms::zero();
    int dist = 0;
};

using vehicle_list_t = std::vector<nearby_vehicle_entry>;

enum class vehicle_menu_ret : int {
    CHANGE_TAB,
    QUIT,
};

static int vmenu_tab_delta = 1;

static auto find_visible_vehicles( avatar &viewer, map &here, int radius ) -> vehicle_list_t
{
    vehicle_list_t vehicles;
    if( viewer.is_blind() && viewer.clairvoyance() < 1 ) {
        return vehicles;
    }

    for( wrapped_vehicle &wrapped : here.get_vehicles() ) {
        vehicle *veh = wrapped.v;
        if( veh == nullptr ) {
            continue;
        }

        int best_dist = INT_MAX;
        std::optional<tripoint_bub_ms> best_pos;
        for( const vpart_reference &vpr : veh->get_all_parts() ) {
            if( vpr.part().removed ) {
                continue;
            }
            const tripoint_bub_ms part_pos = veh->bub_part_location( vpr.part() );
            const int dist = rl_dist( viewer.bub_pos(), part_pos );
            if( dist > radius || !viewer.sees( part_pos ) ) {
                continue;
            }
            if( dist < best_dist ) {
                best_dist = dist;
                best_pos = part_pos;
                if( dist == 0 ) {
                    break;
                }
            }
        }

        if( best_pos ) {
            vehicles.push_back( { veh, *best_pos, best_dist } );
        }
    }

    std::sort( vehicles.begin(), vehicles.end(),
    []( const nearby_vehicle_entry & lhs, const nearby_vehicle_entry & rhs ) {
        if( lhs.dist != rhs.dist ) {
            return lhs.dist < rhs.dist;
        }
        return lhs.veh->name < rhs.veh->name;
    } );

    return vehicles;
}

static auto vehicle_damage_summary( const vehicle &veh ) -> std::pair<std::string, nc_color>
{
    const vehicle_part_range vpr = veh.get_all_parts();
    const int total_damage = std::accumulate( vpr.begin(), vpr.end(), 0,
    []( int lhs, const vpart_reference & rhs ) {
        return lhs + std::max( rhs.part().damage(), 0 );
    } );
    const int total_max = std::accumulate( vpr.begin(), vpr.end(), 0,
    []( int lhs, const vpart_reference & rhs ) {
        return lhs + rhs.part().max_damage();
    } );
    const int pct = total_max ? 100 * total_damage / total_max : 0;

    if( pct < 5 ) {
        return { _( "like new" ), c_light_green };
    } else if( pct < 33 ) {
        return { _( "dented" ), c_yellow };
    } else if( pct < 66 ) {
        return { _( "battered" ), c_magenta };
    } else if( pct < 100 ) {
        return { _( "wrecked" ), c_red };
    }
    return { _( "destroyed" ), c_dark_gray };
}

// ---- list_vehicles RmlUi render path ---------------------------------------
// The nearby-vehicle list (V screen, vehicles tab). Render-only doc: keyboard
// owns cursor nav + tab switching; model synced each frame from vehicle_list /
// iActive. Native scroll replaces calcStartPos windowing.
namespace
{
struct lv_rml_row {
    Rml::String name_rml;
    Rml::String dist_rml;
    bool selected = false;
};
struct lv_rml_data {
    Rml::String  header_rml;
    Rml::Vector<lv_rml_row> rows;
    bool         empty = false;
    Rml::String  empty_rml;
    Rml::String  info_rml;
    Rml::String  footer_rml;
    Rml::DataModelHandle handle;
};

bool g_list_vehicles_types_registered = false;

void register_list_vehicles_rml_types( Rml::DataModelConstructor &c )
{
    if( g_list_vehicles_types_registered ) {
        return;
    }
    auto rh = c.RegisterStruct<lv_rml_row>();
    rh.RegisterMember( "name_rml", &lv_rml_row::name_rml );
    rh.RegisterMember( "dist_rml", &lv_rml_row::dist_rml );
    rh.RegisterMember( "selected",  &lv_rml_row::selected );
    c.RegisterArray<Rml::Vector<lv_rml_row>>();
    g_list_vehicles_types_registered = true;
}
} // namespace

bool &list_vehicles_rmlui_enabled()
{
    static bool enabled = true;
    return enabled;
}

static auto list_vehicles( const vehicle_list_t &vehicle_list ) -> vehicle_menu_ret
{
    avatar &viewer = get_avatar();
    int iInfoHeight = 0;
    const int width = 45;
    int offsetX = 0;
    int iMaxRows = 0;

    catacurses::window w_vehicles;
    catacurses::window w_vehicles_border;
    catacurses::window w_vehicle_info;
    catacurses::window w_vehicle_info_border;

    vehicle *cur_vehicle = nullptr;
    tripoint_rel_ms active_pos = tripoint_rel_ms::zero();
    bool hide_ui = false;

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        if( hide_ui ) {
            ui.position( point_zero, point_zero );
        } else {
            constexpr int info_lines = 9;
            const int desired_info_height = info_lines + 2;
            offsetX = TERMX - width;
            iInfoHeight = std::min( desired_info_height, TERMY - 3 );
            iMaxRows = TERMY - iInfoHeight - 1;

            w_vehicles = catacurses::newwin( iMaxRows, width - 2, point( offsetX + 1, 1 ) );
            w_vehicles_border = catacurses::newwin( iMaxRows + 1, width, point( offsetX, 0 ) );
            w_vehicle_info = catacurses::newwin( iInfoHeight - 2, width - 2,
                                                 point( offsetX + 1, TERMY - iInfoHeight + 1 ) );
            w_vehicle_info_border = catacurses::newwin( iInfoHeight, width, point( offsetX,
                                    TERMY - iInfoHeight ) );

            if( cur_vehicle ) {
                centerlistview( active_pos, width );
            }

            ui.position( point( offsetX, 0 ), point( width, TERMY ) );
        }
    } );
    ui.mark_resize();

    const auto stored_view_offset = viewer.view_offset;
    viewer.view_offset = tripoint_rel_ms::zero();

    int iActive = 0;
    std::string action;
    input_context ctxt( "LIST_VEHICLES" );
    ctxt.register_action( "UP", to_translation( "Move cursor up" ) );
    ctxt.register_action( "DOWN", to_translation( "Move cursor down" ) );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    std::unique_ptr<lv_rml_data> rml_data;
    rml_doc rml;
    const itype_id fuel_type_battery( "battery" );

    const auto sync_rml = [&]() {
        if( !rml || !rml_data ) {
            return;
        }
        lv_rml_data &d = *rml_data;

        d.empty = vehicle_list.empty();
        d.empty_rml = rml_escape( _( "You don't see any vehicles around you!" ) );

        const int iNumVehicles = static_cast<int>( vehicle_list.size() );
        d.header_rml = cata_text_to_rml( string_format( "%s   %s",
                                         colorize( _( "Vehicles" ), c_white ),
                                         colorize( string_format( "%d / %d",
                                             vehicle_list.empty() ? 0 : iActive + 1, iNumVehicles ),
                                             c_light_green ) ) );

        d.rows.clear();
        for( int idx = 0; idx < iNumVehicles; ++idx ) {
            const nearby_vehicle_entry &entry = vehicle_list[idx];
            const bool selected = idx == iActive;
            lv_rml_row row;
            row.selected = selected;
            nc_color name_color = selected ? hilite( c_light_gray ) : c_light_gray;
            row.name_rml = cata_text_to_rml( colorize( entry.veh->name, name_color ) );
            const int dist = entry.dist;
            row.dist_rml = cata_text_to_rml( colorize(
                                                 string_format( "%d %s", dist,
                                                     direction_name_short( direction_from( viewer.bub_pos(), entry.pos ) ) ),
                                                 selected ? c_light_green : c_light_gray ) );
            d.rows.push_back( std::move( row ) );
        }

        // Info pane: the selected vehicle's status lines.
        if( cur_vehicle ) {
            const int speed = static_cast<int>( convert_velocity( cur_vehicle->velocity, VU_VEHICLE ) );
            const std::string speed_text = string_format( _( "%d %s" ), speed,
                                           velocity_units( VU_VEHICLE ) );
            const bool wheels_ok = cur_vehicle->sufficient_wheel_config();
            const auto [status_text, status_color] = vehicle_damage_summary( *cur_vehicle );
            const bool is_boat = !cur_vehicle->floating.empty();
            units::volume total_cargo = 0_ml;
            units::volume free_cargo  = 0_ml;
            for( const vpart_reference &vp : cur_vehicle->get_any_parts( "CARGO" ) ) {
                const size_t p = vp.part_index();
                total_cargo += cur_vehicle->max_volume( p );
                free_cargo  += cur_vehicle->free_volume( p );
            }
            bool leaking_fuel = false;
            for( const vpart_reference &vpr : cur_vehicle->get_all_parts() ) {
                const vehicle_part &part = vpr.part();
                if( !part.is_leaking() || part.ammo_remaining() <= 0 ) {
                    continue;
                }
                if( part.ammo_current() == fuel_type_battery ) {
                    continue;
                }
                leaking_fuel = true;
                break;
            }
            const bool can_float = cur_vehicle->can_float();
            std::string info;
            info += cata_text_to_rml( colorize( cur_vehicle->name, c_light_gray ) );
            info += "<br/>";
            info += cata_text_to_rml( colorize( string_format( "[%s]", cur_vehicle->type.str() ),
                                                c_light_blue ) );
            info += "<br/>";
            info += cata_text_to_rml( colorize( _( "Speed: " ), c_light_gray ) );
            info += cata_text_to_rml( colorize( speed_text, c_light_green ) );
            info += "<br/>";
            info += cata_text_to_rml( colorize( _( "Engine: " ), c_light_gray ) );
            info += cata_text_to_rml( colorize(
                                          cur_vehicle->engine_on ? _( "on" ) : _( "off" ),
                                          cur_vehicle->engine_on ? c_light_green : c_light_red ) );
            info += "<br/>";
            info += cata_text_to_rml( colorize( wheels_ok
                                                ? _( "This vehicle has enough wheels." )
                                                : _( "This vehicle does not have enough wheels." ),
                                                wheels_ok ? c_light_green : c_light_red ) );
            info += "<br/>";
            info += cata_text_to_rml( colorize( _( "Status: " ), c_light_gray ) );
            info += cata_text_to_rml( colorize( status_text, status_color ) );
            info += "<br/>";
            info += cata_text_to_rml( colorize( _( "Cargo: " ), c_light_gray ) );
            info += cata_text_to_rml( colorize( string_format( _( "%s / %s %s" ),
                                                format_volume( total_cargo - free_cargo ),
                                                format_volume( total_cargo ), volume_units_abbr() ), c_yellow ) );
            if( leaking_fuel ) {
                info += "<br/>";
                info += cata_text_to_rml( colorize( _( "This vehicle is leaking fuel." ), c_light_red ) );
            }
            if( is_boat ) {
                info += "<br/>";
                info += cata_text_to_rml( colorize(
                                              can_float ? _( "This vehicle can float." ) : _( "This vehicle can't float." ),
                                              can_float ? c_light_green : c_light_red ) );
            }
            d.info_rml = info;
        } else {
            d.info_rml = Rml::String();
        }

        d.footer_rml = rml_escape( string_format( _( "[%s] Vehicles" ),
                                   ctxt.get_desc( "NEXT_TAB", 1 ) ) );

        d.handle.DirtyVariable( "header_rml" );
        d.handle.DirtyVariable( "rows" );
        d.handle.DirtyVariable( "empty" );
        d.handle.DirtyVariable( "empty_rml" );
        d.handle.DirtyVariable( "info_rml" );
        d.handle.DirtyVariable( "footer_rml" );
    };

    rml.open( list_vehicles_rmlui_enabled(), "list_vehicles", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        rml_data = std::make_unique<lv_rml_data>();
        register_list_vehicles_rml_types( c );
        c.Bind( "header_rml", &rml_data->header_rml );
        c.Bind( "rows",       &rml_data->rows );
        c.Bind( "empty",      &rml_data->empty );
        c.Bind( "empty_rml",  &rml_data->empty_rml );
        c.Bind( "info_rml",   &rml_data->info_rml );
        c.Bind( "footer_rml", &rml_data->footer_rml );
        rml_data->handle = c.GetModelHandle();
    } );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( hide_ui ) {
            return;
        }
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    std::optional<tripoint_bub_ms> trail_start;
    std::optional<tripoint_bub_ms> trail_end;
    bool trail_end_x = false;
    shared_ptr_fast<game::draw_callback_t> trail_cb = create_trail_callback( trail_start, trail_end,
        trail_end_x );
    g->add_draw_callback( trail_cb );

    do {
        if( action == "UP" ) {
            iActive--;
            if( iActive < 0 ) {
                iActive = vehicle_list.empty() ? 0 : static_cast<int>( vehicle_list.size() ) - 1;
            }
        } else if( action == "DOWN" ) {
            iActive++;
            if( iActive >= static_cast<int>( vehicle_list.size() ) ) {
                iActive = 0;
            }
        } else if( action == "NEXT_TAB" || action == "PREV_TAB" ) {
            vmenu_tab_delta = action == "NEXT_TAB" ? 1 : -1;
            viewer.view_offset = stored_view_offset;
            return vehicle_menu_ret::CHANGE_TAB;
        }

        if( iActive >= 0 && static_cast<size_t>( iActive ) < vehicle_list.size() ) {
            cur_vehicle = vehicle_list[iActive].veh;
            active_pos = vehicle_list[iActive].pos - viewer.bub_pos();
            centerlistview( active_pos, width );
            trail_start = viewer.bub_pos();
            trail_end = vehicle_list[iActive].pos;
            // Actually accessed from the terrain overlay callback `trail_cb` in the
            // call to `ui_manager::redraw`.
            //NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
            trail_end_x = false;
        } else {
            cur_vehicle = nullptr;
            active_pos = tripoint_rel_ms::zero();
            viewer.view_offset = stored_view_offset;
            trail_start = trail_end = std::nullopt;
        }
        g->invalidate_main_ui_adaptor();

        ui_manager::redraw();
        action = ctxt.handle_input();
    } while( action != "QUIT" );

    viewer.view_offset = stored_view_offset;
    return vehicle_menu_ret::QUIT;
}

void game::list_items_monsters()
{
    static int vmenu_tab = [] {
        return uistate.vmenu_show_items ? 0 : 1;
    }();

    avatar &viewer = get_avatar();
    map &here = get_map();

    std::vector<Creature *> mons = u.get_visible_creatures( current_daylight_level( calendar::turn ) );
    // whole reality bubble
    const std::vector<map_item_stack> items = find_nearby_items( g_max_view_distance );
    const vehicle_list_t vehicles = find_visible_vehicles( viewer, here, g_max_view_distance );

    if( mons.empty() && items.empty() && vehicles.empty() ) {
        add_msg( m_info, _( "You don't see any items, monsters, or vehicles around you!" ) );
        return;
    }

    std::sort( mons.begin(), mons.end(), [&]( const Creature * lhs, const Creature * rhs ) {
        if( !u.has_trait( trait_INATTENTIVE ) ) {
            const auto att_lhs = lhs->attitude_to( u );
            const auto att_rhs = rhs->attitude_to( u );

            return att_lhs < att_rhs || ( att_lhs == att_rhs
                                          && rl_dist( u.bub_pos(), lhs->bub_pos() ) < rl_dist( u.bub_pos(), rhs->bub_pos() ) );
        } else { // Sort just by distance if player has inattentive trait
            return ( rl_dist( u.bub_pos(), lhs->bub_pos() ) < rl_dist( u.bub_pos(), rhs->bub_pos() ) );
        }
    } );

    const auto tab_empty = [&]( int tab ) {
        if( tab == 0 ) {
            return items.empty();
        } else if( tab == 1 ) {
            return mons.empty();
        }
        return vehicles.empty();
    };

    if( vmenu_tab < 0 || vmenu_tab > 2 ) {
        vmenu_tab = 0;
    }
    if( tab_empty( vmenu_tab ) ) {
        for( int tab = 0; tab < 3; ++tab ) {
            if( !tab_empty( tab ) ) {
                vmenu_tab = tab;
                break;
            }
        }
    }

    temp_exit_fullscreen();
    game::vmenu_ret ret;
    while( true ) {
        if( vmenu_tab == 0 ) {
            ret = list_items( items );
        } else if( vmenu_tab == 1 ) {
            mons = u.get_visible_creatures( current_daylight_level( calendar::turn ) );
            std::sort( mons.begin(), mons.end(), [&]( const Creature * lhs, const Creature * rhs ) {
                if( !u.has_trait( trait_INATTENTIVE ) ) {
                    const auto al = lhs->attitude_to( u );
                    const auto ar = rhs->attitude_to( u );
                    return al < ar || ( al == ar &&
                                        rl_dist( u.bub_pos(), lhs->bub_pos() ) < rl_dist( u.bub_pos(), rhs->bub_pos() ) );
                }
                return rl_dist( u.bub_pos(), lhs->bub_pos() ) < rl_dist( u.bub_pos(), rhs->bub_pos() );
            } );
            ret = list_monsters( mons );
        } else {
            ret = list_vehicles( vehicles ) == vehicle_menu_ret::CHANGE_TAB ?
                  game::vmenu_ret::CHANGE_TAB : game::vmenu_ret::QUIT;
        }
        if( ret == game::vmenu_ret::CHANGE_TAB ) {
            int next_tab = vmenu_tab;
            for( int i = 0; i < 3; ++i ) {
                next_tab = ( next_tab + vmenu_tab_delta + 3 ) % 3;
                if( !tab_empty( next_tab ) ) {
                    vmenu_tab = next_tab;
                    break;
                }
            }
            if( vmenu_tab == 0 ) {
                uistate.vmenu_show_items = true;
            } else if( vmenu_tab == 1 ) {
                uistate.vmenu_show_items = false;
            }
        } else {
            break;
        }
    }

    if( ret == game::vmenu_ret::FIRE ) {
        avatar_action::fire_wielded_weapon( u );
    }
    reenter_fullscreen();
}

// ---- list_items RmlUi render path (P3 track-A) -----------------------------
// The nearby-items list (`V` screen). Render-only doc, twin of list_monsters:
// the keyboard owns cursor nav / filter / priority / examine / compare / travel;
// the model is synced each frame. The info pane is fed by
// rml_util::item_info_rml_lines() (the shared item-info producer). Native scroll
// replaces the curses calcStartPos windowing. Sort-by-category headers are
// interspersed as full-width magenta rows.
namespace
{
struct li_rml_row {
    bool is_cat = false;
    Rml::String cat_rml;
    Rml::String name_rml;
    bool has_new = false;
    Rml::String new_rml;
    Rml::String dist_rml;
    bool selected = false;
};
struct li_rml_info_line {
    Rml::String text_rml;
};
struct li_rml_data {
    Rml::String header_rml;
    Rml::Vector<li_rml_row> rows;
    bool empty = false;
    Rml::String empty_rml;
    Rml::String info_title_rml;
    Rml::Vector<li_rml_info_line> info;
    Rml::String footer_rml;
    Rml::DataModelHandle handle;
};

bool g_list_items_types_registered = false;

void register_list_items_rml_types( Rml::DataModelConstructor &c )
{
    // RegisterStruct/Array are context-global and persist past RemoveDataModel —
    // guard so a reopen doesn't double-register (uilist-proven pattern).
    if( g_list_items_types_registered ) {
        return;
    }
    Rml::StructHandle<li_rml_row> rh = c.RegisterStruct<li_rml_row>();
    rh.RegisterMember( "is_cat", &li_rml_row::is_cat );
    rh.RegisterMember( "cat_rml", &li_rml_row::cat_rml );
    rh.RegisterMember( "name_rml", &li_rml_row::name_rml );
    rh.RegisterMember( "has_new", &li_rml_row::has_new );
    rh.RegisterMember( "new_rml", &li_rml_row::new_rml );
    rh.RegisterMember( "dist_rml", &li_rml_row::dist_rml );
    rh.RegisterMember( "selected", &li_rml_row::selected );
    c.RegisterArray<Rml::Vector<li_rml_row>>();
    Rml::StructHandle<li_rml_info_line> ih = c.RegisterStruct<li_rml_info_line>();
    ih.RegisterMember( "text_rml", &li_rml_info_line::text_rml );
    c.RegisterArray<Rml::Vector<li_rml_info_line>>();
    g_list_items_types_registered = true;
}
} // namespace

bool &list_items_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

game::vmenu_ret game::list_items( const std::vector<map_item_stack> &item_list )
{
    std::vector<map_item_stack> ground_items = item_list;
    int iInfoHeight = 0;
    int iMaxRows = 0;
    int width = 0;
    int width_nob = 0;
    int max_name_width = 0;

    const bool highlight_unread_items = get_option<bool>( "HIGHLIGHT_UNREAD_ITEMS" );
    const nc_color item_new_col = c_light_green;
    const std::string item_new_str = pgettext( "list items", "NEW!" );
    const int item_new_str_width = utf8_width( item_new_str );
    const int left_padding = 1;
    const int right_padding = 2 + 1 + 2 + 1;
    const int padding = left_padding + right_padding;

    //find max length of item name and resize window width
    for( const map_item_stack &cur_item : ground_items ) {
        max_name_width = std::max( max_name_width,
                                   utf8_width( remove_color_tags( cur_item.example->display_name() ) ) );
    }
    max_name_width = max_name_width + padding + 6 +
                     ( highlight_unread_items ? item_new_str_width : 0 );

    tripoint_rel_ms active_pos;
    map_item_stack *activeItem = nullptr;

    catacurses::window w_items;
    catacurses::window w_items_border;
    catacurses::window w_item_info;

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        iInfoHeight = std::min( 25, TERMY / 2 );
        iMaxRows = TERMY - iInfoHeight - 2;

        width = clamp( max_name_width, 45, TERMX / 3 );
        width_nob = width - 2;

        const int offsetX = TERMX - width;

        w_items = catacurses::newwin( TERMY - 2 - iInfoHeight,
                                      width_nob, point( offsetX + 1, 1 ) );
        w_items_border = catacurses::newwin( TERMY - iInfoHeight,
                                             width, point( offsetX, 0 ) );
        w_item_info = catacurses::newwin( iInfoHeight, width,
                                          point( offsetX, TERMY - iInfoHeight ) );

        if( activeItem ) {
            centerlistview( active_pos, width );
        }

        ui.position( point( offsetX, 0 ), point( width, TERMY ) );
    } );
    ui.mark_resize();

    // use previously selected sorting method
    bool sort_radius = uistate.list_item_sort != 2;
    bool addcategory = !sort_radius;

    // reload filter/priority settings on the first invocation, if they were active
    if( !uistate.list_item_init ) {
        if( uistate.list_item_filter_active ) {
            sFilter = uistate.list_item_filter;
        }
        if( uistate.list_item_downvote_active ) {
            list_item_downvote = uistate.list_item_downvote;
        }
        if( uistate.list_item_priority_active ) {
            list_item_upvote = uistate.list_item_priority;
        }
        uistate.list_item_init = true;
    }

    //this stores only those items that match our filter
    std::vector<map_item_stack> filtered_items =
        !sFilter.empty() ? filter_item_stacks( ground_items, sFilter ) : ground_items;
    int highPEnd = list_filter_high_priority( filtered_items, list_item_upvote );
    int lowPStart = list_filter_low_priority( filtered_items, highPEnd, list_item_downvote );
    int iItemNum = ground_items.size();

    const auto stored_view_offset = u.view_offset;

    u.view_offset = tripoint_rel_ms::zero();

    int iActive = 0; // Item index that we're looking at
    bool refilter = true;
    int page_num = 0;
    int iCatSortNum = 0;
    int iScrollPos = 0;
    std::map<int, std::string> mSortCategory;

    std::string action;
    input_context ctxt( "LIST_ITEMS" );
    ctxt.register_action( "UP", to_translation( "Move cursor up" ) );
    ctxt.register_action( "DOWN", to_translation( "Move cursor down" ) );
    ctxt.register_action( "LEFT", to_translation( "Previous item" ) );
    ctxt.register_action( "RIGHT", to_translation( "Next item" ) );
    ctxt.register_action( "PAGE_DOWN" );
    ctxt.register_action( "PAGE_UP" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "FILTER" );
    ctxt.register_action( "RESET_FILTER" );
    ctxt.register_action( "EXAMINE" );
    ctxt.register_action( "COMPARE" );
    ctxt.register_action( "PRIORITY_INCREASE" );
    ctxt.register_action( "PRIORITY_DECREASE" );
    ctxt.register_action( "SORT" );
    ctxt.register_action( "TRAVEL_TO" );

    std::optional<item_filter_type> filter_type;

    // ---- RmlUi render path (F.3 rml_doc harness, twin of list_monsters) ------
    // `rml_data` before `rml` so the doc tears down while the model is alive. The
    // doc is rebuilt each frame from the live cursor state; the keyboard owns all
    // nav / filter / priority / examine / compare / travel. The info pane is
    // item_info_rml_lines() (the shared item-info producer). Native scroll
    // replaces the curses calcStartPos windowing.
    std::unique_ptr<li_rml_data> rml_data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml || !rml_data ) {
            return;
        }
        li_rml_data &d = *rml_data;

        d.empty = ground_items.empty();
        d.empty_rml = rml_escape( _( "You don't see any items around you!" ) );

        // Header: "<Tab> Items   active / total" (curses border title + counter).
        int catsBefore = 0;
        for( const auto &kv : mSortCategory ) {
            if( kv.first < iActive && !kv.second.empty() ) {
                ++catsBefore;
            }
        }
        const int activeNum = iItemNum > 0 ? iActive - catsBefore + 1 : 0;
        const int total = iItemNum - iCatSortNum;
        d.header_rml = cata_text_to_rml( string_format( "%s%s   %s",
                                         colorize( "<Tab> ", c_light_green ),
                                         colorize( _( "Items" ), c_white ),
                                         colorize( string_format( "%d / %d", activeNum, total ),
                                             c_light_green ) ) );

        // Rows: replicate the curses combined walk (sort-category headers
        // interspersed with item rows) WITHOUT the calcStartPos window. Priority
        // colour (yellow/red), in-inventory colour, NEW! badge and the
        // distance/direction cluster are all baked per the curses body.
        d.rows.clear();
        int index = 0;
        int iCatSortOffset = 0;
        auto iter = filtered_items.begin();
        for( int pos = 0; pos < iItemNum; ++pos ) {
            const auto catit = mSortCategory.find( pos );
            if( catit != mSortCategory.end() && !catit->second.empty() ) {
                li_rml_row row;
                row.is_cat = true;
                row.cat_rml = cata_text_to_rml( colorize( catit->second, c_magenta ) );
                d.rows.push_back( std::move( row ) );
                ++iCatSortOffset;
                continue;
            }
            if( iter == filtered_items.end() ) {
                break;
            }
            const int iThisPage = pos == iActive ? page_num : 0;
            const int pidx = std::min<int>( iThisPage, static_cast<int>( iter->vIG.size() ) - 1 );

            std::string sText;
            if( iter->vIG.size() > 1 ) {
                sText += string_format( "[%d/%d] (%d) ", pidx + 1,
                                        static_cast<int>( iter->vIG.size() ), iter->totalcount );
            }
            sText += iter->example->tname();
            if( iter->vIG[pidx].count > 1 ) {
                sText += string_format( "[%d]", iter->vIG[pidx].count );
            }

            nc_color col;
            if( highPEnd > 0 && index < highPEnd + iCatSortOffset ) {
                col = c_yellow;
            } else if( index >= lowPStart + iCatSortOffset ) {
                col = c_red;
            } else {
                col = iter->example->color_in_inventory();
            }

            li_rml_row row;
            row.selected = pos == iActive;
            row.name_rml = cata_text_to_rml( colorize( sText, col ) );

            const bool print_new = highlight_unread_items &&
                                   !uistate.read_items.contains( iter->example->typeId() );
            row.has_new = print_new;
            if( print_new ) {
                row.new_rml = cata_text_to_rml( colorize( item_new_str, item_new_col ) );
            }

            const auto p = iter->vIG[pidx].pos.xy();
            row.dist_rml = cata_text_to_rml( colorize(
                                                 string_format( "%2d %s", rl_dist( point_rel_ms::zero(), p ),
                                                     direction_name_short( direction_from( point_rel_ms::zero(), p ) ) ),
                                                 row.selected ? c_light_green : c_light_gray ) );

            d.rows.push_back( std::move( row ) );
            ++iter;
            ++index;
        }

        // Info pane: the selected stack's item_info_rml_lines() (shared producer).
        d.info.clear();
        if( !ground_items.empty() && activeItem ) {
            const item &loc = *activeItem->example;
            const temperature_flag temperature = rot::temperature_flag_for_location( m, loc );
            std::vector<iteminfo> this_item = activeItem->example->info( temperature );
            std::vector<iteminfo> item_info_dummy;
            item_info_data dummy( "", "", this_item, item_info_dummy );
            dummy.without_getch = true;
            dummy.without_border = true;
            for( const std::string &l : item_info_rml_lines( dummy ) ) {
                li_rml_info_line ln;
                ln.text_rml = l;
                d.info.push_back( std::move( ln ) );
            }
            // Info title: "< item display_name >".
            d.info_title_rml = cata_text_to_rml(
                                   colorize( "< ", c_white )
                                   + colorize( activeItem->example->display_name(),
                                               activeItem->example->color_in_inventory() )
                                   + colorize( " >", c_white ) );
        } else {
            d.info_title_rml = Rml::String();
        }

        // Footer: the reset_item_list_state hint tokens, live keybinds.
        std::string footer = string_format( _( "[%s] Sort: %s" ), ctxt.get_desc( "SORT", 1 ),
                                            sort_radius ? _( "dist" ) : _( "cat" ) );
        if( !sFilter.empty() ) {
            footer += string_format( _( "   [%s] Reset" ), ctxt.get_desc( "RESET_FILTER", 1 ) );
        }
        footer += string_format(
                      _( "   [%s] Examine   [%s] Compare   [%s] Filter   [%s/%s] Priority   [%s] Travel" ),
                      ctxt.get_desc( "EXAMINE", 1 ), ctxt.get_desc( "COMPARE", 1 ),
                      ctxt.get_desc( "FILTER", 1 ), ctxt.get_desc( "PRIORITY_INCREASE", 1 ),
                      ctxt.get_desc( "PRIORITY_DECREASE", 1 ), ctxt.get_desc( "TRAVEL_TO", 1 ) );
        d.footer_rml = rml_escape( footer );

        d.handle.DirtyVariable( "header_rml" );
        d.handle.DirtyVariable( "rows" );
        d.handle.DirtyVariable( "empty" );
        d.handle.DirtyVariable( "empty_rml" );
        d.handle.DirtyVariable( "info_title_rml" );
        d.handle.DirtyVariable( "info" );
        d.handle.DirtyVariable( "footer_rml" );
    };
    rml.open( list_items_rmlui_enabled(), "list_items", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        rml_data = std::make_unique<li_rml_data>();
        register_list_items_rml_types( c );
        c.Bind( "header_rml", &rml_data->header_rml );
        c.Bind( "rows", &rml_data->rows );
        c.Bind( "empty", &rml_data->empty );
        c.Bind( "empty_rml", &rml_data->empty_rml );
        c.Bind( "info_title_rml", &rml_data->info_title_rml );
        c.Bind( "info", &rml_data->info );
        c.Bind( "footer_rml", &rml_data->footer_rml );
        rml_data->handle = c.GetModelHandle();
    } );

    ui.on_redraw( [&]( ui_adaptor & ui ) {
        // RmlUi path owns the panel — sync the model and skip the curses draw.
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    std::optional<tripoint_bub_ms> trail_start;
    std::optional<tripoint_bub_ms> trail_end;
    bool trail_end_x = false;
    shared_ptr_fast<draw_callback_t> trail_cb = create_trail_callback( trail_start, trail_end,
        trail_end_x );
    add_draw_callback( trail_cb );

    do {
        bool recalc_unread = false;
        if( action == "COMPARE" && activeItem ) {
            game_menus::inv::compare( u, active_pos );
            recalc_unread = highlight_unread_items;
        } else if( action == "FILTER" ) {
            filter_type = item_filter_type::FILTER;
            ui.invalidate_ui();
            string_input_popup()
            .title( _( "Filter:" ) )
            .width( 55 )
            .description( _( "UP: history, CTRL-U: clear line, ESC: abort, ENTER: save" ) )
            .identifier( "item_filter" )
            .max_length( 256 )
            .edit( sFilter );
            refilter = true;
            addcategory = !sort_radius;
            uistate.list_item_filter_active = !sFilter.empty();
            filter_type = std::nullopt;
        } else if( action == "RESET_FILTER" ) {
            sFilter.clear();
            filtered_items = ground_items;
            refilter = true;
            uistate.list_item_filter_active = false;
            addcategory = !sort_radius;
        } else if( action == "EXAMINE" && !filtered_items.empty() && activeItem ) {
            std::vector<iteminfo> dummy;
            const item *example_item = activeItem->example;
            // TODO: const_item_location
            const item &loc = *example_item;
            temperature_flag temperature = rot::temperature_flag_for_location( m, loc );
            std::vector<iteminfo> this_item = example_item->info( temperature );

            item_info_data info_data( example_item->tname(), example_item->type_name(), this_item, dummy );
            info_data.handle_scrolling = true;

            rml_examine_item( info_data );
            recalc_unread = highlight_unread_items;
        } else if( action == "PRIORITY_INCREASE" ) {
            filter_type = item_filter_type::HIGH_PRIORITY;
            ui.invalidate_ui();
            list_item_upvote = string_input_popup()
                               .title( _( "High Priority:" ) )
                               .width( 55 )
                               .text( list_item_upvote )
                               .description( _( "UP: history, CTRL-U clear line, ESC: abort, ENTER: save" ) )
                               .identifier( "list_item_priority" )
                               .max_length( 256 )
                               .query_string();
            refilter = true;
            addcategory = !sort_radius;
            uistate.list_item_priority_active = !list_item_upvote.empty();
            filter_type = std::nullopt;
        } else if( action == "PRIORITY_DECREASE" ) {
            filter_type = item_filter_type::LOW_PRIORITY;
            ui.invalidate_ui();
            list_item_downvote = string_input_popup()
                                 .title( _( "Low Priority:" ) )
                                 .width( 55 )
                                 .text( list_item_downvote )
                                 .description( _( "UP: history, CTRL-U clear line, ESC: abort, ENTER: save" ) )
                                 .identifier( "list_item_downvote" )
                                 .max_length( 256 )
                                 .query_string();
            refilter = true;
            addcategory = !sort_radius;
            uistate.list_item_downvote_active = !list_item_downvote.empty();
            filter_type = std::nullopt;
        } else if( action == "SORT" ) {
            if( sort_radius ) {
                sort_radius = false;
                addcategory = true;
                uistate.list_item_sort = 2; // list is sorted by category
            } else {
                sort_radius = true;
                uistate.list_item_sort = 1; // list is sorted by distance
            }
            highPEnd = -1;
            lowPStart = -1;
            iCatSortNum = 0;

            mSortCategory.clear();
            refilter = true;
        } else if( action == "TRAVEL_TO" && activeItem ) {
            if( !u.sees( u.bub_pos() + active_pos ) ) {
                add_msg( _( "You can't see that destination." ) );
            }
            auto route = m.route( u.bub_pos(), u.bub_pos() + active_pos, u.get_legacy_pathfinding_settings(),
                                  u.get_legacy_path_avoid() );
            if( route.size() > 1 ) {
                route.pop_back();
                u.set_destination( route );
                recalc_unread = highlight_unread_items;
                break;
            } else {
                add_msg( m_info, _( "You can't travel there." ) );
            }
        }
        if( uistate.list_item_sort == 1 ) {
            ground_items = item_list;
        } else if( uistate.list_item_sort == 2 ) {
            std::sort( ground_items.begin(), ground_items.end(), map_item_stack::map_item_stack_sort );
        }

        if( refilter ) {
            refilter = false;
            filtered_items = filter_item_stacks( ground_items, sFilter );
            highPEnd = list_filter_high_priority( filtered_items, list_item_upvote );
            lowPStart = list_filter_low_priority( filtered_items, highPEnd, list_item_downvote );
            iActive = 0;
            page_num = 0;
            iItemNum = filtered_items.size();
        }

        if( addcategory ) {
            addcategory = false;
            iCatSortNum = 0;
            mSortCategory.clear();
            if( highPEnd > 0 ) {
                mSortCategory[0] = _( "HIGH PRIORITY" );
                iCatSortNum++;
            }
            std::string last_cat_name;
            for( int i = std::max( 0, highPEnd );
                 i < std::min( lowPStart, static_cast<int>( filtered_items.size() ) ); i++ ) {
                const std::string &cat_name = filtered_items[i].example->get_category().name();
                if( cat_name != last_cat_name ) {
                    mSortCategory[i + iCatSortNum++] = cat_name;
                    last_cat_name = cat_name;
                }
            }
            if( lowPStart < static_cast<int>( filtered_items.size() ) ) {
                mSortCategory[lowPStart + iCatSortNum++] = _( "LOW PRIORITY" );
            }
            if( !mSortCategory[0].empty() ) {
                iActive++;
            }
            iItemNum = static_cast<int>( filtered_items.size() ) + iCatSortNum;
        }

        if( action == "UP" ) {
            do {
                iActive--;

            } while( !mSortCategory[iActive].empty() );
            iScrollPos = 0;
            page_num = 0;
            if( iActive < 0 ) {
                iActive = iItemNum - 1;
            }
            recalc_unread = highlight_unread_items;
        } else if( action == "DOWN" ) {
            do {
                iActive++;

            } while( !mSortCategory[iActive].empty() );
            iScrollPos = 0;
            page_num = 0;
            if( iActive >= iItemNum ) {
                iActive = mSortCategory[0].empty() ? 0 : 1;
            }
            recalc_unread = highlight_unread_items;
        } else if( action == "RIGHT" ) {
            if( !filtered_items.empty() && activeItem ) {
                if( ++page_num >= static_cast<int>( activeItem->vIG.size() ) ) {
                    page_num = activeItem->vIG.size() - 1;
                }
            }
            recalc_unread = highlight_unread_items;
        } else if( action == "LEFT" ) {
            page_num = std::max( 0, page_num - 1 );
            recalc_unread = highlight_unread_items;
        } else if( action == "PAGE_UP" ) {
            iScrollPos--;
        } else if( action == "PAGE_DOWN" ) {
            iScrollPos++;
        } else if( action == "NEXT_TAB" || action == "PREV_TAB" ) {
            vmenu_tab_delta = action == "NEXT_TAB" ? 1 : -1;
            u.view_offset = stored_view_offset;
            return game::vmenu_ret::CHANGE_TAB;
        }

        active_pos = tripoint_rel_ms::zero();
        activeItem = nullptr;

        if( mSortCategory[iActive].empty() ) {
            auto iter = filtered_items.begin();
            for( int iNum = 0; iter != filtered_items.end() && iNum < iActive; iNum++ ) {
                if( mSortCategory[iNum].empty() ) {
                    ++iter;
                }
            }
            if( iter != filtered_items.end() ) {
                active_pos = iter->vIG[page_num].pos;
                activeItem = &( *iter );
            }
        }

        if( activeItem ) {
            centerlistview( active_pos, width );
            trail_start = u.bub_pos();
            trail_end = u.bub_pos() + active_pos;
            // Actually accessed from the terrain overlay callback `trail_cb` in the
            // call to `ui_manager::redraw`.
            //NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
            trail_end_x = true;
            if( recalc_unread ) {
                uistate.read_items.insert( activeItem->example->typeId() );
            }
        } else {
            u.view_offset = stored_view_offset;
            trail_start = trail_end = std::nullopt;
        }
        invalidate_main_ui_adaptor();

        ui_manager::redraw();

        action = ctxt.handle_input();
    } while( action != "QUIT" );

    u.view_offset = stored_view_offset;
    return game::vmenu_ret::QUIT;
}

// ---- list_monsters RmlUi render path (§8.1 gate-blocker backlog) -----------
// The nearby-monster list (`m`). Render-only doc: the keyboard owns cursor nav /
// safemode blacklist / look / fire; the model is synced each frame. The info
// pane is fed by Creature::print_info_text() — the shared monster/npc producer
// that is the "creature-info trio" component (curses print_info untouched for the
// A/B toggle). Native scroll replaces the curses calcStartPos windowing.
namespace
{
struct lm_rml_row {
    bool is_cat = false;
    Rml::String cat_rml;
    Rml::String name_rml;
    Rml::String meta_rml;
    bool selected = false;
};
struct lm_rml_data {
    Rml::String header_rml;
    Rml::Vector<lm_rml_row> rows;
    bool empty = false;
    Rml::String empty_rml;
    Rml::String info_title_rml;
    Rml::String info_rml;
    Rml::String footer_rml;
    Rml::DataModelHandle handle;
};

bool g_list_monsters_types_registered = false;

void register_list_monsters_rml_types( Rml::DataModelConstructor &c )
{
    // RegisterStruct/Array are context-global and persist past RemoveDataModel —
    // guard so a reopen doesn't double-register (uilist-proven pattern).
    if( g_list_monsters_types_registered ) {
        return;
    }
    Rml::StructHandle<lm_rml_row> rh = c.RegisterStruct<lm_rml_row>();
    rh.RegisterMember( "is_cat", &lm_rml_row::is_cat );
    rh.RegisterMember( "cat_rml", &lm_rml_row::cat_rml );
    rh.RegisterMember( "name_rml", &lm_rml_row::name_rml );
    rh.RegisterMember( "meta_rml", &lm_rml_row::meta_rml );
    rh.RegisterMember( "selected", &lm_rml_row::selected );
    c.RegisterArray<Rml::Vector<lm_rml_row>>();
    g_list_monsters_types_registered = true;
}
} // namespace

bool &list_monsters_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

bool &look_around_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

game::vmenu_ret game::list_monsters( std::vector<Creature *> monster_list )
{
    const int iInfoHeight = 15;
    const int width = 45;
    int offsetX = 0;
    int iMaxRows = 0;

    catacurses::window w_monsters;
    catacurses::window w_monsters_border;
    catacurses::window w_monster_info;
    catacurses::window w_monster_info_border;

    Creature *cCurMon = nullptr;
    tripoint_rel_ms iActivePos;

    bool hide_ui = false;

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        if( hide_ui ) {
            ui.position( point_zero, point_zero );
        } else {
            offsetX = TERMX - width;
            iMaxRows = TERMY - iInfoHeight - 1;

            w_monsters = catacurses::newwin( iMaxRows, width - 2, point( offsetX + 1,
                                             1 ) );
            w_monsters_border = catacurses::newwin( iMaxRows + 1, width, point( offsetX,
                                                    0 ) );
            w_monster_info = catacurses::newwin( iInfoHeight - 2, width - 2,
                                                 point( offsetX + 1, TERMY - iInfoHeight + 1 ) );
            w_monster_info_border = catacurses::newwin( iInfoHeight, width, point( offsetX,
                                    TERMY - iInfoHeight ) );

            if( cCurMon ) {
                centerlistview( iActivePos, width );
            }

            ui.position( point( offsetX, 0 ), point( width, TERMY ) );
        }
    } );
    ui.mark_resize();

    const int max_gun_range = u.primary_weapon().gun_range( &u );

    const auto stored_view_offset = u.view_offset;
    u.view_offset = tripoint_rel_ms::zero();

    int iActive = 0; // monster index that we're looking at

    std::string action;
    input_context ctxt( "LIST_MONSTERS" );
    ctxt.register_action( "UP", to_translation( "Move cursor up" ) );
    ctxt.register_action( "DOWN", to_translation( "Move cursor down" ) );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "SAFEMODE_BLACKLIST_ADD" );
    ctxt.register_action( "SAFEMODE_BLACKLIST_REMOVE" );
    ctxt.register_action( "QUIT" );
    if( bVMonsterLookFire ) {
        ctxt.register_action( "look" );
        ctxt.register_action( "fire" );
    }
    ctxt.register_action( "HELP_KEYBINDINGS" );

    // first integer is the row the attitude category string is printed in the menu
    std::map<int, Attitude> mSortCategory;

    const bool player_knows = !u.has_trait( trait_INATTENTIVE );
    if( player_knows ) {
        for( int i = 0, last_attitude = -1; i < static_cast<int>( monster_list.size() ); i++ ) {
            const auto attitude = monster_list[i]->attitude_to( u );
            if( static_cast<int>( attitude ) != last_attitude ) {
                mSortCategory[i + mSortCategory.size()] = attitude;
                last_attitude = static_cast<int>( attitude );
            }
        }
    }

    // ---- RmlUi render path (F.3 rml_doc harness) ------------------------
    // `rml_data` before `rml` so the doc tears down while the model is alive.
    // The doc is rebuilt each frame from the live cursor state; the keyboard owns
    // all nav / safemode / look / fire. The info pane is Creature::print_info_text()
    // (the shared monster/npc producer). Native scroll replaces the curses
    // calcStartPos windowing. During the nested look_around() (hide_ui) the doc is
    // hidden, matching the curses path zeroing the windows.
    std::unique_ptr<lm_rml_data> rml_data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml || !rml_data ) {
            return;
        }
        lm_rml_data &d = *rml_data;

        d.empty = monster_list.empty();
        d.empty_rml = rml_escape( _( "You don't see any monsters around you!" ) );

        // Header: "Monsters   active / total" (curses border title + counter).
        d.header_rml = cata_text_to_rml( string_format( "%s   %s",
                                         colorize( _( "Monsters" ), c_white ),
                                         colorize( string_format( "%d / %d",
                                             monster_list.empty() ? 0 : iActive + 1,
                                             static_cast<int>( monster_list.size() ) ), c_light_green ) ) );

        // Rows: replicate the curses combined walk (attitude-category headers
        // interspersed with creature rows) WITHOUT the calcStartPos window.
        d.rows.clear();
        int iCurMon = 0;
        auto CatSortIter = mSortCategory.cbegin();
        const int combined = static_cast<int>( monster_list.size() + mSortCategory.size() );
        for( int pos = 0; pos < combined; ++pos ) {
            if( player_knows && CatSortIter != mSortCategory.cend() && CatSortIter->first == pos ) {
                lm_rml_row row;
                row.is_cat = true;
                row.cat_rml = cata_text_to_rml( colorize(
                                                    Creature::get_attitude_ui_data( CatSortIter->second ).first.translated(),
                                                    c_magenta ) );
                d.rows.push_back( std::move( row ) );
                ++CatSortIter;
                continue;
            }
            if( iCurMon >= static_cast<int>( monster_list.size() ) ) {
                break;
            }
            Creature *critter = monster_list[iCurMon];
            const bool selected = iCurMon == iActive;
            const monster *m = dynamic_cast<monster *>( critter );

            lm_rml_row row;
            row.selected = selected;

            // Name, with a leading "!" when the creature can see the avatar.
            std::string name_str = colorize( m != nullptr ? m->name() : critter->disp_name(),
                                             critter->basic_symbol_color() );
            if( player_knows && critter->sees( u ) ) {
                name_str = colorize( "! ", c_yellow ) + name_str;
            }
            row.name_rml = cata_text_to_rml( name_str );

            // Meta cluster: HP bar (player_knows) + attitude + distance/direction.
            std::string meta;
            if( player_knows ) {
                nc_color hp_color = c_white;
                std::string hp_bar;
                if( m != nullptr ) {
                    m->get_HP_Bar( hp_color, hp_bar );
                } else {
                    std::tie( hp_bar, hp_color ) =
                        ::get_hp_bar( critter->get_hp(), critter->get_hp_max(), false );
                }
                meta += colorize( hp_bar, hp_color );
                for( int i = 0, bw = utf8_width( hp_bar ); i < 5 - bw; ++i ) {
                    meta += colorize( ".", c_white );
                }
                meta += " ";
            }
            std::string att_str;
            nc_color att_color = c_white;
            if( m != nullptr ) {
                const std::pair<std::string, nc_color> att = m->get_attitude();
                att_str = att.first;
                att_color = att.second;
            } else if( const npc *p = dynamic_cast<npc *>( critter ) ) {
                att_str = npc_attitude_name( p->get_attitude() );
                att_color = p->symbol_color();
            }
            meta += colorize( att_str, att_color ) + "  ";
            const int mon_dist = rl_dist( u.bub_pos(), critter->bub_pos() );
            meta += colorize( string_format( "%d %s", mon_dist,
                                             direction_name_short( direction_from( u.bub_pos(), critter->bub_pos() ) ) ),
                              selected ? c_light_green : c_light_gray );
            row.meta_rml = cata_text_to_rml( meta );

            d.rows.push_back( std::move( row ) );
            ++iCurMon;
        }

        // Info pane: the selected creature's print_info_text() (shared producer).
        d.info_rml = cCurMon != nullptr ? cata_text_to_rml( cCurMon->print_info_text() )
                     : Rml::String();

        // Info title: the look/fire border hints (only when invoked from firing).
        std::string title;
        if( bVMonsterLookFire ) {
            title += string_format( _( "[%s] to look around" ), ctxt.get_desc( "look", 1 ) );
            if( cCurMon && rl_dist( u.bub_pos(), cCurMon->bub_pos() ) <= max_gun_range ) {
                title += string_format( "   [%s] to shoot", ctxt.get_desc( "fire", 1 ) );
            }
        }
        d.info_title_rml = rml_escape( title );

        // Footer: tab hint + the selected creature's safemode blacklist toggle.
        std::string footer = string_format( _( "[%s] Monsters" ), ctxt.get_desc( "NEXT_TAB", 1 ) );
        if( cCurMon && !get_safemode().empty() ) {
            const monster *sm = dynamic_cast<monster *>( cCurMon );
            const std::string monName = sm != nullptr ? sm->name() : get_safemode().npc_type_name();
            if( get_safemode().has_rule( monName, Attitude::A_ANY ) ) {
                footer += string_format( _( "   [%s] Remove from safemode blacklist" ),
                                         ctxt.get_desc( "SAFEMODE_BLACKLIST_REMOVE", 1 ) );
            } else {
                footer += string_format( _( "   [%s] Add to safemode blacklist" ),
                                         ctxt.get_desc( "SAFEMODE_BLACKLIST_ADD", 1 ) );
            }
        }
        d.footer_rml = rml_escape( footer );

        d.handle.DirtyVariable( "header_rml" );
        d.handle.DirtyVariable( "rows" );
        d.handle.DirtyVariable( "empty" );
        d.handle.DirtyVariable( "empty_rml" );
        d.handle.DirtyVariable( "info_title_rml" );
        d.handle.DirtyVariable( "info_rml" );
        d.handle.DirtyVariable( "footer_rml" );
    };
    rml.open( list_monsters_rmlui_enabled(), "list_monsters", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        rml_data = std::make_unique<lm_rml_data>();
        register_list_monsters_rml_types( c );
        c.Bind( "header_rml", &rml_data->header_rml );
        c.Bind( "rows", &rml_data->rows );
        c.Bind( "empty", &rml_data->empty );
        c.Bind( "empty_rml", &rml_data->empty_rml );
        c.Bind( "info_title_rml", &rml_data->info_title_rml );
        c.Bind( "info_rml", &rml_data->info_rml );
        c.Bind( "footer_rml", &rml_data->footer_rml );
        rml_data->handle = c.GetModelHandle();
    } );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        // RmlUi path owns the panel — hide during the nested look_around, else
        // sync the model and skip the curses draw.
        if( rml ) {
            rml.document()->SetProperty( "visibility", hide_ui ? "hidden" : "visible" );
            if( !hide_ui ) {
                sync_rml();
            }
            return;
        }
    } );

    std::optional<tripoint_bub_ms> trail_start;
    std::optional<tripoint_bub_ms> trail_end;
    bool trail_end_x = false;
    shared_ptr_fast<draw_callback_t> trail_cb = create_trail_callback( trail_start, trail_end,
        trail_end_x );
    add_draw_callback( trail_cb );

    do {
        if( action == "UP" ) {
            iActive--;
            if( iActive < 0 ) {
                if( monster_list.empty() ) {
                    iActive = 0;
                } else {
                    iActive = static_cast<int>( monster_list.size() ) - 1;
                }
            }
        } else if( action == "DOWN" ) {
            iActive++;
            if( iActive >= static_cast<int>( monster_list.size() ) ) {
                iActive = 0;
            }
        } else if( action == "NEXT_TAB" || action == "PREV_TAB" ) {
            vmenu_tab_delta = action == "NEXT_TAB" ? 1 : -1;
            u.view_offset = stored_view_offset;
            return game::vmenu_ret::CHANGE_TAB;
        } else if( action == "SAFEMODE_BLACKLIST_REMOVE" ) {
            const auto m = dynamic_cast<monster *>( cCurMon );
            const std::string monName = ( m != nullptr ) ? m->name() : "human";

            if( get_safemode().has_rule( monName, Attitude::A_ANY ) ) {
                get_safemode().remove_rule( monName, Attitude::A_ANY );
            }
        } else if( action == "SAFEMODE_BLACKLIST_ADD" ) {
            if( !get_safemode().empty() ) {
                const auto m = dynamic_cast<monster *>( cCurMon );
                const std::string monName = ( m != nullptr ) ? m->name() : "human";

                get_safemode().add_rule( monName, Attitude::A_ANY, get_option<int>( "SAFEMODEPROXIMITY" ),
                                         RULE_BLACKLISTED );
            }
        } else if( action == "look" ) {
            hide_ui = true;
            ui.mark_resize();
            look_around();
            hide_ui = false;
            ui.mark_resize();
        } else if( action == "fire" ) {
            if( cCurMon != nullptr && rl_dist( u.bub_pos(), cCurMon->bub_pos() ) <= max_gun_range ) {
                u.last_target = shared_from( *cCurMon );
                u.recoil = MAX_RECOIL;
                u.view_offset = stored_view_offset;
                return game::vmenu_ret::FIRE;
            }
        }

        if( iActive >= 0 && static_cast<size_t>( iActive ) < monster_list.size() ) {
            cCurMon = monster_list[iActive];
            iActivePos = cCurMon->bub_pos() - u.bub_pos();
            centerlistview( iActivePos, width );
            trail_start = u.bub_pos();
            trail_end = cCurMon->bub_pos();
            // Actually accessed from the terrain overlay callback `trail_cb` in the
            // call to `ui_manager::redraw`.
            //NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
            trail_end_x = false;
        } else {
            cCurMon = nullptr;
            iActivePos = tripoint_rel_ms::zero();
            u.view_offset = stored_view_offset;
            trail_start = trail_end = std::nullopt;
        }
        invalidate_main_ui_adaptor();

        ui_manager::redraw();

        action = ctxt.handle_input();
        // After a fiber yield the world may have ticked, freeing Creature* in
        // monster_list (sync_rml iterates ALL entries on the next redraw).
        // Rebuild the entire list from u.get_visible_creatures() — same data
        // source as list_items_monsters() — so dead pointers are dropped before
        // sync_rml dereferences them.
        if( coop_fiber::active() ) {
            monster_list = u.get_visible_creatures( current_daylight_level( calendar::turn ) );
            std::sort( monster_list.begin(), monster_list.end(),
            [&]( const Creature * lhs, const Creature * rhs ) {
                if( !u.has_trait( trait_INATTENTIVE ) ) {
                    const auto al = lhs->attitude_to( u );
                    const auto ar = rhs->attitude_to( u );
                    return al < ar || ( al == ar &&
                                        rl_dist( u.bub_pos(), lhs->bub_pos() ) <
                                        rl_dist( u.bub_pos(), rhs->bub_pos() ) );
                }
                return rl_dist( u.bub_pos(), lhs->bub_pos() ) <
                       rl_dist( u.bub_pos(), rhs->bub_pos() );
            } );
            iActive = std::clamp( iActive, 0,
                                  std::max( 0, static_cast<int>( monster_list.size() ) - 1 ) );
            cCurMon = monster_list.empty() ? nullptr : monster_list[iActive];
            if( player_knows ) {
                mSortCategory.clear();
                for( int i = 0, last_attitude = -1; i < static_cast<int>( monster_list.size() ); i++ ) {
                    const auto attitude = monster_list[i]->attitude_to( u );
                    if( static_cast<int>( attitude ) != last_attitude ) {
                        mSortCategory[i + mSortCategory.size()] = attitude;
                        last_attitude = static_cast<int>( attitude );
                    }
                }
            }
        }
    } while( action != "QUIT" );

    u.view_offset = stored_view_offset;

    return game::vmenu_ret::QUIT;
}
