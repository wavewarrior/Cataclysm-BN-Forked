#include "panels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iosfwd>
#include <iterator>
#include <list>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>

#include <RmlUi/Core.h>

#include "action.h"
#include "avatar.h"
#include "behavior.h"
#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "catalua_impl.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_effects.h"
#include "character_functions.h"
#include "character_martial_arts.h"
#include "character_oracle.h"
#include "color.h"
#include "cursesdef.h"
#include "debug.h"
#include "widget.h"
#include "widget_icon.h"
#include "sidebar_anim.h"
#include "effect.h"
#include "fstream_utils.h"
#include "game.h"
#include "game_constants.h"
#include "game_ui.h"
#include "input.h"
#include "item.h"
#include "json.h"
#include "lua_sidebar_widgets.h"
#include "magic.h"
#include "map.h"
#include "messages.h"
#include "omdata.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmap_ui.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "panels_utility.h"
#include "player.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "lighting/rmlui_layer.h"
#include "pldata.h"
#include "point.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_utils.h"
#include "tileray.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "units.h"
#include "units_utility.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "weather.h"

static const trait_id trait_THRESH_FELINE( "THRESH_FELINE" );
static const trait_id trait_THRESH_BIRD( "THRESH_BIRD" );
static const trait_id trait_THRESH_URSINE( "THRESH_URSINE" );

static const efftype_id effect_got_checked( "got_checked" );

static const flag_id json_flag_THERMOMETER( "THERMOMETER" );
static const flag_id json_flag_SPLINT( "SPLINT" );

namespace
{
struct panel_layout_entry {
    std::string name;
    std::optional<std::string> lua_id;
    bool toggle = true;
};

auto saved_panel_layouts() -> std::map<std::string, std::vector<panel_layout_entry>> & // *NOPAD*
{
    static auto layouts = std::map<std::string, std::vector<panel_layout_entry>> {};
    return layouts;
}

auto resolve_layout_entry_name( const panel_layout_entry &entry,
                                const std::map<std::string, std::string> &lua_name_by_id ) -> std::optional<std::string>
{
    if( entry.lua_id ) {
        const auto it = lua_name_by_id.find( *entry.lua_id );
        if( it == lua_name_by_id.end() ) {
            return std::nullopt;
        }
        return it->second;
    }
    if( entry.name.empty() ) {
        return std::nullopt;
    }
    return entry.name;
}

auto apply_saved_layout_entries( std::vector<window_panel> &layout,
                                 const std::vector<panel_layout_entry> &entries,
                                 const std::map<std::string, std::string> &lua_name_by_id ) -> void
{
    auto it = layout.begin();
    std::ranges::for_each( entries, [&]( const panel_layout_entry & entry ) {
        const auto resolved_name = resolve_layout_entry_name( entry, lua_name_by_id );
        if( !resolved_name ) {
            return;
        }
        auto search_range = std::ranges::subrange( it, layout.end() );
        auto match = std::ranges::find( search_range, *resolved_name, &window_panel::get_name );
        if( match == search_range.end() ) {
            return;
        }
        if( it != match ) {
            auto panel = *match;
            layout.erase( match );
            it = layout.insert( it, std::move( panel ) );
        }
        it->toggle = entry.toggle;
        ++it;
    } );
}

struct lua_widget_line {
    std::string text;
    nc_color color = c_light_gray;
};

auto split_widget_lines( const std::string &text,
                         const nc_color color ) -> std::vector<lua_widget_line>
{
    auto parts = text | std::views::split( '\n' )
    | std::views::transform( [color]( const auto & part ) {
        return lua_widget_line{
            .text = std::ranges::to<std::string>( part ),
            .color = color,
        };
    } );
    return std::ranges::to<std::vector<lua_widget_line>>( parts );
}

auto append_widget_lines( std::vector<lua_widget_line> &out,
                          std::vector<lua_widget_line> &&more ) -> void
{
    std::ranges::move( more, std::back_inserter( out ) );
}

auto lua_panel_name( const cata::lua_sidebar_widgets::widget_entry &widget ) -> std::string
{
    return widget.name.empty() ? widget.id : widget.name;
}

auto resolve_widget_color( const sol::object &obj ) -> std::optional<nc_color>
{
    if( !obj.valid() || obj == sol::lua_nil ) {
        return std::nullopt;
    }
    if( obj.is<color_id>() ) {
        return get_all_colors().get( obj.as<color_id>() );
    }
    if( obj.is<std::string>() ) {
        const auto id = get_all_colors().name_to_id( obj.as<std::string>(),
                        report_color_error::no );
        return get_all_colors().get( id );
    }
    return std::nullopt;
}

auto to_widget_lines( const sol::object &value ) -> std::vector<lua_widget_line>
{
    if( !value.valid() || value == sol::lua_nil ) {
        return {};
    }
    if( value.is<std::string>() ) {
        return split_widget_lines( value.as<std::string>(), c_light_gray );
    }
    if( !value.is<sol::table>() ) {
        return {};
    }

    auto lines = std::vector<lua_widget_line> {};
    auto table = value.as<sol::table>();
    const auto count = static_cast<size_t>( table.size() );
    auto indices = std::views::iota( size_t{ 1 }, count + 1 );
    std::ranges::for_each( indices, [&]( const size_t idx ) {
        auto entry = table.get<sol::object>( idx );
        if( !entry.valid() || entry == sol::lua_nil ) {
            return;
        }
        if( entry.is<std::string>() ) {
            append_widget_lines( lines,
                                 split_widget_lines( entry.as<std::string>(), c_light_gray ) );
            return;
        }
        if( !entry.is<sol::table>() ) {
            return;
        }
        auto entry_tbl = entry.as<sol::table>();
        auto text = entry_tbl.get_or<std::string>( "text", "" );
        if( text.empty() ) {
            return;
        }
        auto color_obj = entry_tbl.get<sol::object>( "color" );
        auto color = resolve_widget_color( color_obj ).value_or( c_light_gray );
        append_widget_lines( lines, split_widget_lines( text, color ) );
    } );
    return lines;
}

auto get_lua_widget_lines( const cata::lua_sidebar_widgets::widget_entry &widget,
                           const int width, const int height ) -> std::vector<lua_widget_line>
{
    try {
        auto res = widget.draw( width, height );
        check_func_result( res );
        const auto return_count = res.return_count();
        if( return_count == 0 ) {
            return {};
        }
        auto lines = std::vector<lua_widget_line> {};
        auto indices = std::views::iota( 0, return_count );
        std::ranges::for_each( indices, [&]( const int idx ) {
            auto value = res.get<sol::object>( idx );
            append_widget_lines( lines, to_widget_lines( value ) );
        } );
        return lines;
    } catch( const std::runtime_error &err ) {
        debugmsg( "Failed to draw Lua sidebar widget '%s': %s", widget.id, err.what() );
    }
    return {};
}

auto should_render_lua_widget( const cata::lua_sidebar_widgets::widget_entry &widget ) -> bool
{
    if( widget.panel_visible_fn ) {
        try {
            auto res = ( *widget.panel_visible_fn )();
            check_func_result( res );
            if( res.return_count() == 0 ) {
                return true;
            }
            auto obj = res.get<sol::object>();
            if( !obj.valid() || obj == sol::lua_nil ) {
                return true;
            }
            if( obj.is<bool>() ) {
                return obj.as<bool>();
            }
        } catch( const std::runtime_error &err ) {
            debugmsg( "Failed to get Lua sidebar widget '%s' visibility: %s", widget.id, err.what() );
        }
        return true;
    }
    if( widget.panel_visible_value.has_value() && !*widget.panel_visible_value ) {
        return false;
    }
    if( !widget.render ) {
        return true;
    }
    try {
        auto res = ( *widget.render )();
        check_func_result( res );
        if( res.return_count() == 0 ) {
            return true;
        }
        if( res.return_count() == 0 ) {
            return true;
        }
        auto obj = res.get<sol::object>();
        if( !obj.valid() || obj == sol::lua_nil ) {
            return true;
        }
        if( obj.is<bool>() ) {
            return obj.as<bool>();
        }
        return true;
    } catch( const std::runtime_error &err ) {
        debugmsg( "Failed to render-check Lua sidebar widget '%s': %s", widget.id, err.what() );
    }
    return true;
}

auto draw_lua_widget_panel( const cata::lua_sidebar_widgets::widget_entry &widget,
                            const catacurses::window &w ) -> void
{
    werase( w );
    const auto window_height = getmaxy( w );
    const auto window_width = getmaxx( w );
    const auto lines = get_lua_widget_lines( widget, window_width, window_height );
    const auto layout_id = panel_manager::get_manager().get_current_layout_id();
    const auto add_leading_space = layout_id == "labels" || layout_id == "labels-narrow";
    const auto max_lines = static_cast<size_t>( window_height );
    const auto count = std::min( lines.size(), max_lines );
    auto indices = std::views::iota( size_t{ 0 }, count );
    std::ranges::for_each( indices, [&]( const size_t idx ) {
        const auto &line = lines[idx];
        auto cur_color = line.color;
        const auto display_text = add_leading_space ? " " + line.text : line.text;
        print_colored_text( w, point( 0, static_cast<int>( idx ) ), cur_color, line.color,
                            display_text, report_color_error::no );
    } );
    wnoutrefresh( w );
}

auto make_lua_widget_panel( const cata::lua_sidebar_widgets::widget_entry &widget,
                            const int width ) -> window_panel
{
    const auto panel_name = lua_panel_name( widget );
    const auto widget_id = widget.id;
    auto draw_func = [widget_id]( avatar &, const catacurses::window & w ) {
        const auto *entry = cata::lua_sidebar_widgets::find_widget( widget_id );
        if( entry == nullptr ) {
            werase( w );
            wnoutrefresh( w );
            return;
        }
        draw_lua_widget_panel( *entry, w );
    };
    auto render_func = [widget_id]() -> bool {
        const auto *entry = cata::lua_sidebar_widgets::find_widget( widget_id );
        if( entry == nullptr )
        {
            return false;
        }
        return should_render_lua_widget( *entry );
    };
    window_panel wp( draw_func, panel_name, widget.height, width, widget.default_toggle,
                     render_func, widget.redraw_every_frame );
    // Dynamic content height: size the panel to its actual rendered line count, collapsing
    // unused rows. The declared height acts as the ceiling (mods declare a larger height to
    // allow more lines). Stage 5's widget engine will unify this via get_wgt_height.
    wp.dynamic_height = [widget_id, width]() -> int {
        const auto *entry = cata::lua_sidebar_widgets::find_widget( widget_id );
        if( entry == nullptr )
        {
            return 0;
        }
        const int ceiling = std::max( entry->height, 1 );
        const auto lines = get_lua_widget_lines( *entry, width, ceiling );
        return std::clamp( static_cast<int>( lines.size() ), 1, ceiling );
    };
    return wp;
}
} // namespace

// constructor
window_panel::window_panel( std::function<void( avatar &, const catacurses::window & )>
                            draw_func, const std::string &nm, int ht, int wd, bool default_toggle_,
                            std::function<bool()> render_func,  bool force_draw )
{
    draw = std::move( draw_func );
    name = nm;
    height = ht;
    width = wd;
    toggle = default_toggle_;
    default_toggle = default_toggle_;
    always_draw = force_draw;
    render = std::move( render_func );
}

// ====================================
// panels prettify and helper functions
// ====================================

static std::pair<nc_color, std::string> str_string( const avatar &p )
{
    const nc_color clr = color_compare_base( p.get_str_base(), p.get_str() );
    return std::make_pair( clr, _( "Str " ) + value_trimmed( p.get_str() ) );
}

static std::pair<nc_color, std::string> dex_string( const avatar &p )
{
    const nc_color clr = color_compare_base( p.get_dex_base(), p.get_dex() );
    return std::make_pair( clr, _( "Dex " ) + value_trimmed( p.get_dex() ) );
}

static std::pair<nc_color, std::string> int_string( const avatar &p )
{
    const nc_color clr = color_compare_base( p.get_int_base(), p.get_int() );
    return std::make_pair( clr, _( "Int " ) + value_trimmed( p.get_int() ) );
}

static std::pair<nc_color, std::string> per_string( const avatar &p )
{
    const nc_color clr = color_compare_base( p.get_per_base(), p.get_per() );
    return std::make_pair( clr, _( "Per " ) + value_trimmed( p.get_per() ) );
}

int window_panel::get_height() const
{
    if( dynamic_height ) {
        return dynamic_height();
    }
    if( height != -1 ) {
        return height;
    } else if( pixel_minimap_option ) {
        const int minimap_height = get_option<int>( "PIXEL_MINIMAP_HEIGHT" );
        return minimap_height > 0 ? minimap_height : width / 2;
    } else {
        return 0;
    }
}

int window_panel::get_width() const
{
    return width;
}

std::string window_panel::get_name() const
{
    return name;
}

void overmap_ui::draw_overmap_chunk( const catacurses::window &w_minimap, const avatar &you,
                                     const tripoint_abs_omt &global_omt, point start_input,
                                     const int width, const int height )
{
    auto &player_character = get_avatar();
    const point_abs_omt curs = global_omt.xy();
    const auto custom_targ = player_character.get_custom_mission_target();
    const auto mission_targ = you.get_active_mission_target();
    const auto targ = custom_targ != overmap::invalid_tripoint ? custom_targ : mission_targ;
    auto drew_mission = targ == overmap::invalid_tripoint;
    const int start_y = start_input.y + ( height / 2 ) - 2;
    const int start_x = start_input.x + ( width / 2 ) - 2;

    for( int i = -( width / 2 ); i <= width - ( width / 2 ) - 1; i++ ) {
        for( int j = -( height / 2 ); j <= height - ( height / 2 ) - 1; j++ ) {
            const tripoint_abs_omt omp( curs + point( i, j ), g->get_levz() );
            nc_color ter_color;
            std::string ter_sym;
            const bool seen = ACTIVE_OVERMAP_BUFFER.seen( omp );
            const bool vehicle_here = ACTIVE_OVERMAP_BUFFER.has_vehicle( omp );
            if( ACTIVE_OVERMAP_BUFFER.has_note( omp ) ) {

                const std::string &note_text = ACTIVE_OVERMAP_BUFFER.note( omp );

                const auto note_info = overmap_ui::get_note_display_info( note_text );
                ter_color = std::get<1>( note_info );
                ter_sym = std::string( 1, std::get<0>( note_info ) );
            } else if( !seen ) {
                ter_sym = " ";
                ter_color = c_black;
            } else if( vehicle_here ) {
                ter_color = c_cyan;
                ter_sym = "c";
            } else {
                const oter_id &cur_ter = ACTIVE_OVERMAP_BUFFER.ter( omp );
                ter_sym = cur_ter->get_symbol();
                if( ACTIVE_OVERMAP_BUFFER.is_explored( omp ) ) {
                    ter_color = c_dark_gray;
                } else {
                    ter_color = cur_ter->get_color();
                }
            }
            if( !drew_mission && targ.xy() == omp.xy() ) {
                // If there is a mission target, and it's not on the same
                // overmap terrain as the player character, mark it.
                // TODO: Inform player if the mission is above or below
                drew_mission = true;
                if( i != 0 || j != 0 ) {
                    ter_color = red_background( ter_color );
                }
            }
            if( i == 0 && j == 0 ) {
                mvwputch_hi( w_minimap, point( 3 + start_x, 3 + start_y ), ter_color, ter_sym );
            } else {
                mvwputch( w_minimap, point( 3 + i + start_x, 3 + j + start_y ), ter_color, ter_sym );
            }
        }
    }

    // Print arrow to mission if we have one!
    if( !drew_mission ) {
        double slope = curs.x() != targ.x() ?
                       static_cast<double>( targ.y() - curs.y() ) / ( targ.x() - curs.x() ) : 4;

        if( curs.x() == targ.x() || std::fabs( slope ) > 3.5 ) {  // Vertical slope
            const int arrowy = targ.y() > curs.y() ? 6 : 0;

            mvwputch( w_minimap, point( 3 + start_x, arrowy + start_y ), c_red, '*' );
        } else {
            int arrowx = -1;
            int arrowy = -1;
            if( std::fabs( slope ) >= 1.0 ) {  // y diff is bigger!
                arrowy = ( targ.y() > curs.y() ? 6 : 0 );
                arrowx = static_cast<int>( 3 + 3 * ( targ.y() > curs.y() ? slope : ( 0 - slope ) ) );
                arrowx = clamp( arrowx, 0, 6 );
            } else {
                arrowx = ( targ.x() > curs.x() ? 6 : 0 );
                arrowy = static_cast<int>( 3 + 3 * ( targ.x() > curs.x() ? slope : ( 0 - slope ) ) );
                arrowy = clamp( arrowy, 0, 6 );
            }
            char glyph = '*';
            if( targ.z() > you.bub_pos().z() ) {
                glyph = '^';
            } else if( targ.z() < you.bub_pos().z() ) {
                glyph = 'v';
            }

            mvwputch( w_minimap, point( arrowx + start_x, arrowy + start_y ), c_red, glyph );
        }
    }
    const int sight_points = player_character.overmap_sight_range( g->light_level(
                                 player_character.bub_pos().z() ) );
    for( int i = -3; i <= 3; i++ ) {
        for( int j = -3; j <= 3; j++ ) {
            if( i > -3 && i < 3 && j > -3 && j < 3 ) {
                continue; // only do hordes on the border, skip inner map
            }
            const tripoint_abs_omt omp( curs + point( i, j ), g->get_levz() );
            int horde_size = ACTIVE_OVERMAP_BUFFER.get_horde_size( omp );
            if( horde_size >= HORDE_VISIBILITY_SIZE ) {
                if( ACTIVE_OVERMAP_BUFFER.seen( omp )
                    && player_character.overmap_los( omp, sight_points ) ) {
                    mvwputch( w_minimap, point( i + 3, j + 3 ), c_green,
                              horde_size > HORDE_VISIBILITY_SIZE * 2 ? 'Z' : 'z' );
                }
            }
        }
    }
}

static void decorate_panel( const std::string &name, const catacurses::window &w )
{
    werase( w );
    draw_border( w );

    static const char *title_prefix = " ";
    const std::string &title = name;
    static const char *title_suffix = " ";
    static const std::string full_title = string_format( "%s%s%s",
                                          title_prefix, title, title_suffix );
    const int start_pos = center_text_pos( full_title, 0, getmaxx( w ) - 1 );
    mvwprintz( w, point( start_pos, 0 ), c_white, title_prefix );
    wprintz( w, c_light_red, title );
    wprintz( w, c_white, title_suffix );
}

static std::string time_approx()
{
    const int iHour = hour_of_day<int>( calendar::turn );
    if( iHour >= 23 || iHour <= 1 ) {
        return _( "Around midnight" );
    } else if( iHour <= 4 ) {
        return _( "Dead of night" );
    } else if( iHour <= 6 ) {
        return _( "Around dawn" );
    } else if( iHour <= 8 ) {
        return _( "Early morning" );
    } else if( iHour <= 10 ) {
        return _( "Morning" );
    } else if( iHour <= 13 ) {
        return _( "Around noon" );
    } else if( iHour <= 16 ) {
        return _( "Afternoon" );
    } else if( iHour <= 18 ) {
        return _( "Early evening" );
    } else if( iHour <= 20 ) {
        return _( "Around dusk" );
    }
    return _( "Night" );
}

static nc_color value_color( int stat )
{
    nc_color valuecolor = c_light_gray;

    if( stat >= 75 ) {
        valuecolor = c_green;
    } else if( stat >= 50 ) {
        valuecolor = c_yellow;
    } else if( stat >= 25 ) {
        valuecolor = c_red;
    } else {
        valuecolor = c_magenta;
    }
    return valuecolor;
}

static std::pair<nc_color, int> morale_stat( const avatar &u )
{
    const int morale_int = u.get_morale_level();
    nc_color morale_color = c_white;
    if( morale_int >= 10 ) {
        morale_color = c_green;
    } else if( morale_int <= -10 ) {
        morale_color = c_red;
    }
    return std::make_pair( morale_color, morale_int );
}

struct temp_delta_extremes {
    temp_delta_extremes( bodypart_str_id extreme_cur_bp,
                         int extreme_cur_temp,
                         bodypart_str_id extreme_conv_bp,
                         int extreme_conv_temp ) :
        extreme_cur_bp( extreme_cur_bp ),
        extreme_cur_temp( extreme_cur_temp ),
        extreme_conv_bp( extreme_conv_bp ),
        extreme_conv_temp( extreme_conv_temp )
    {}
    bodypart_str_id extreme_cur_bp;
    int extreme_cur_temp;
    bodypart_str_id extreme_conv_bp;
    int extreme_conv_temp;
};

static temp_delta_extremes temp_delta( const avatar &u )
{
    bodypart_str_id extreme_cur_bp;
    int current_bp_extreme = BODYTEMP_NORM;
    bodypart_str_id extreme_conv_bp;
    int conv_bp_extreme = BODYTEMP_NORM;
    for( const auto &pr : u.get_body() ) {
        int temp_cur = pr.second.get_temp_cur();
        if( std::abs( temp_cur - BODYTEMP_NORM ) >
            std::abs( current_bp_extreme - BODYTEMP_NORM ) ) {
            extreme_cur_bp = pr.first;
            current_bp_extreme = temp_cur;
        }

        int temp_conv = pr.second.get_temp_conv();
        if( std::abs( temp_conv - BODYTEMP_NORM ) >
            std::abs( conv_bp_extreme - BODYTEMP_NORM ) ) {
            extreme_conv_bp = pr.first;
            conv_bp_extreme = temp_conv;
        }
    }
    return temp_delta_extremes( extreme_cur_bp, current_bp_extreme, extreme_conv_bp, conv_bp_extreme );
}

static int define_temp_level( const int lvl )
{
    if( lvl > BODYTEMP_SCORCHING ) {
        return 7;
    } else if( lvl > BODYTEMP_VERY_HOT ) {
        return 6;
    } else if( lvl > BODYTEMP_HOT ) {
        return 5;
    } else if( lvl > BODYTEMP_COLD ) {
        return 4;
    } else if( lvl > BODYTEMP_VERY_COLD ) {
        return 3;
    } else if( lvl > BODYTEMP_FREEZING ) {
        return 2;
    }
    return 1;
}

static std::pair<nc_color, std::string> temp_delta_arrows( const avatar &u )
{
    std::string temp_message;
    nc_color temp_color = c_white;
    temp_delta_extremes temp_struct = temp_delta( u );
    // Assign zones for comparisons
    const int cur_zone = define_temp_level( temp_struct.extreme_cur_temp );
    const int conv_zone = define_temp_level( temp_struct.extreme_conv_temp );

    // delta will be positive if temp_cur is rising
    const int delta = conv_zone - cur_zone;
    // Decide if temp_cur is rising or falling
    if( delta > 2 ) {
        temp_message = " ↑↑↑";
        temp_color = c_red;
    } else if( delta == 2 ) {
        temp_message = " ↑↑";
        temp_color = c_light_red;
    } else if( delta == 1 ) {
        temp_message = " ↑";
        temp_color = c_yellow;
    } else if( delta == 0 ) {
        temp_message = "-";
        temp_color = c_green;
    } else if( delta == -1 ) {
        temp_message = " ↓";
        temp_color = c_light_blue;
    } else if( delta == -2 ) {
        temp_message = " ↓↓";
        temp_color = c_cyan;
    } else {
        temp_message = " ↓↓↓";
        temp_color = c_blue;
    }
    return std::make_pair( temp_color, temp_message );
}

static std::pair<nc_color, std::string> temp_stat( const avatar &u )
{
    /// Find hottest/coldest bodypart
    // Calculate the most extreme body temperatures
    temp_delta_extremes temp_struct = temp_delta( u );
    int extreme_cur_temp = temp_struct.extreme_cur_temp;

    // printCur the hottest/coldest bodypart
    std::string temp_string;
    nc_color temp_color = c_yellow;
    if( extreme_cur_temp > BODYTEMP_SCORCHING ) {
        temp_color = c_red;
        temp_string = _( "Scorching!" );
    } else if( extreme_cur_temp > BODYTEMP_VERY_HOT ) {
        temp_color = c_light_red;
        temp_string = _( "Very hot!" );
    } else if( extreme_cur_temp > BODYTEMP_HOT ) {
        temp_color = c_yellow;
        temp_string = _( "Warm" );
    } else if( extreme_cur_temp > BODYTEMP_COLD ) {
        temp_color = c_green;
        temp_string = _( "Comfortable" );
    } else if( extreme_cur_temp > BODYTEMP_VERY_COLD ) {
        temp_color = c_light_blue;
        temp_string = _( "Chilly" );
    } else if( extreme_cur_temp > BODYTEMP_FREEZING ) {
        temp_color = c_cyan;
        temp_string = _( "Very cold!" );
    } else if( extreme_cur_temp <= BODYTEMP_FREEZING ) {
        temp_color = c_blue;
        temp_string = _( "Freezing!" );
    }
    return std::make_pair( temp_color, temp_string );
}

static std::string get_armor( const avatar &u, bodypart_id bp, unsigned int truncate = 0 )
{
    for( auto it = u.worn.rbegin(); it != u.worn.rend(); ) {
        if( ( *it )->covers( bp ) ) {
            return ( *it )->tname( 1, true, truncate );
        }

        it++;
    }
    return "-";
}

static face_type get_face_type( const avatar &u )
{
    face_type fc = face_human;
    if( u.has_trait( trait_THRESH_FELINE ) ) {
        fc = face_cat;
    } else if( u.has_trait( trait_THRESH_URSINE ) ) {
        fc = face_bear;
    } else if( u.has_trait( trait_THRESH_BIRD ) ) {
        fc = face_bird;
    }
    return fc;
}

static std::string morale_emotion( const int morale_cur, const face_type face,
                                   const bool horizontal_style )
{
    if( horizontal_style ) {
        if( face == face_bear || face == face_cat ) {
            if( morale_cur >= 200 ) {
                return "@W@";
            } else if( morale_cur >= 100 ) {
                return "OWO";
            } else if( morale_cur >= 50 ) {
                return "owo";
            } else if( morale_cur >= 10 ) {
                return "^w^";
            } else if( morale_cur >= -10 ) {
                return "-w-";
            } else if( morale_cur >= -50 ) {
                return "-m-";
            } else if( morale_cur >= -100 ) {
                return "TmT";
            } else if( morale_cur >= -200 ) {
                return "XmX";
            } else {
                return "@m@";
            }
        } else if( face == face_bird ) {
            if( morale_cur >= 200 ) {
                return "@v@";
            } else if( morale_cur >= 100 ) {
                return "OvO";
            } else if( morale_cur >= 50 ) {
                return "ovo";
            } else if( morale_cur >= 10 ) {
                return "^v^";
            } else if( morale_cur >= -10 ) {
                return "-v-";
            } else if( morale_cur >= -50 ) {
                return ".v.";
            } else if( morale_cur >= -100 ) {
                return "TvT";
            } else if( morale_cur >= -200 ) {
                return "XvX";
            } else {
                return "@v@";
            }
        } else if( morale_cur >= 200 ) {
            return "@U@";
        } else if( morale_cur >= 100 ) {
            return "OuO";
        } else if( morale_cur >= 50 ) {
            return "^u^";
        } else if( morale_cur >= 10 ) {
            return "n_n";
        } else if( morale_cur >= -10 ) {
            return "-_-";
        } else if( morale_cur >= -50 ) {
            return "-n-";
        } else if( morale_cur >= -100 ) {
            return "TnT";
        } else if( morale_cur >= -200 ) {
            return "XnX";
        } else {
            return "@n@";
        }
    } else if( morale_cur >= 100 ) {
        return "8D";
    } else if( morale_cur >= 50 ) {
        return ":D";
    } else if( face == face_cat && morale_cur >= 10 ) {
        return ":3";
    } else if( face != face_cat && morale_cur >= 10 ) {
        return ":)";
    } else if( morale_cur >= -10 ) {
        return ":|";
    } else if( morale_cur >= -50 ) {
        return "):";
    } else if( morale_cur >= -100 ) {
        return "D:";
    } else {
        return "D8";
    }
}

static std::pair<nc_color, std::string> power_stat( const avatar &u )
{
    nc_color c_pwr = c_red;
    std::string s_pwr;
    if( !u.has_max_power() ) {
        s_pwr = "--";
        c_pwr = c_light_gray;
    } else {
        if( u.get_power_level() >= u.get_max_power_level() / 2 ) {
            c_pwr = c_light_blue;
        } else if( u.get_power_level() >= u.get_max_power_level() / 3 ) {
            c_pwr = c_yellow;
        } else if( u.get_power_level() >= u.get_max_power_level() / 4 ) {
            c_pwr = c_red;
        }

        if( u.get_power_level() < 1_kJ ) {
            s_pwr = std::to_string( units::to_joule( u.get_power_level() ) ) +
                    pgettext( "energy unit: joule", "J" );
        } else {
            s_pwr = std::to_string( units::to_kilojoule( u.get_power_level() ) ) +
                    pgettext( "energy unit: kilojoule", "kJ" );
        }
    }
    return std::make_pair( c_pwr, s_pwr );
}

static std::pair<nc_color, std::string> mana_stat( const player &u )
{
    nc_color c_mana = c_red;
    std::string s_mana;
    if( u.magic->max_mana( u ) <= 0 ) {
        s_mana = "--";
        c_mana = c_light_gray;
    } else {
        if( u.magic->available_mana() >= u.magic->max_mana( u ) / 2 ) {
            c_mana = c_light_blue;
        } else if( u.magic->available_mana() >= u.magic->max_mana( u ) / 3 ) {
            c_mana = c_yellow;
        }
        s_mana = std::to_string( u.magic->available_mana() );
    }
    return std::make_pair( c_mana, s_mana );
}

static nc_color safe_color()
{
    nc_color s_color = g->safe_mode ? c_green : c_red;
    if( g->safe_mode == SAFE_MODE_OFF && get_option<bool>( "AUTOSAFEMODE" ) ) {
        int s_return = get_option<int>( "AUTOSAFEMODETURNS" );
        int iPercent = g->turnssincelastmon * 100 / s_return;
        if( iPercent >= 100 ) {
            s_color = c_green;
        } else if( iPercent >= 75 ) {
            s_color = c_yellow;
        } else if( iPercent >= 50 ) {
            s_color = c_light_red;
        } else if( iPercent >= 25 ) {
            s_color = c_red;
        }
    }
    return s_color;
}

// ===============================
// panels code
// ===============================

static nc_color move_mode_color( avatar &u )
{
    if( u.movement_mode_is( CMM_RUN ) ) {
        return c_red;
    } else if( u.movement_mode_is( CMM_CROUCH ) ) {
        return c_light_blue;
    } else {
        return c_light_gray;
    }
}

static std::string move_mode_string( avatar &u )
{
    if( u.movement_mode_is( CMM_RUN ) ) {
        return pgettext( "movement-type", "R" );
    } else if( u.movement_mode_is( CMM_CROUCH ) ) {
        return pgettext( "movement-type", "C" );
    } else {
        return pgettext( "movement-type", "W" );
    }
}

static std::string carry_weight_string( const avatar &u )
{
    double weight_carried = round_up( convert_weight( u.weight_carried() ), 1 ); // In kg/lbs
    double weight_capacity = round_up( convert_weight( u.weight_capacity() ), 1 );
    return string_format( "%.1f/%.1f", weight_carried, weight_capacity );
}

static std::string carry_volume_string( const avatar &u )
{
    double volume_carried = round_up( convert_volume( to_milliliter( u.volume_carried() ) ),
                                      2 );
    double volume_capacity = round_up( convert_volume( to_milliliter( u.volume_capacity() ) ),
                                       2 ); // In liters/cups/wolf paws or whatever burger units
    return string_format( "%.2f/%.2f", volume_carried, volume_capacity );
}

// Phase-accurate moon icon name for the current time. Maps the 8 moon_phase
// enumerators to the per-phase SVGs in gfx/widgets/ (waxing lights from the
// right, waning from the left). Any out-of-range phase falls back to the
// generic moon disc.

// Draw the phase-accurate moon disc with animation: a pop on phase change (specs
// under the "moon" icon id in icons.json). Shared by the narrow + wide panels.

// Pre-rotated directional wind-arrow icon for `dirangle`. Bins to the same 8
// 45-degree sectors as get_wind_arrow() (weather.cpp), keyed by the origin
// cardinal; the SVG points where the wind blows TOWARD (origin N -> arrow down).
// Out-of-range angle (calm / no direction) -> the generic gust icon.

// ============
// INITIALIZERS
// ============

static bool spell_panel()
{
    // If a mod says to always show it, then return early
    if( get_option<bool>( "ALWAYS_SHOW_MANA" ) ) {
        return true;
    }
    // Also return early if we're below our maximum capacity
    if( get_avatar().magic->available_mana() < get_avatar().magic->max_mana( get_avatar() ) ) {
        return true;
    }
    // Determine if any of the spells the player has take mana to cast
    std::vector<spell_id> spells = get_avatar().magic->spells();
    bool has_manacasting = false;
    for( spell_id sp : spells ) {
        spell temp_spell = get_avatar().magic->get_spell( sp );
        if( temp_spell.energy_source() == mana_energy ) {
            has_manacasting = true;
        }
    }
    return has_manacasting;
}

static bool veh_panel()
{
    return get_avatar().in_vehicle;
}
bool default_render()
{
    return true;
}

// Optional show/hide predicates a widget can name via "show_if" — the data-driven
// equivalent of the render_func gate the hardcoded panels pass (spell_panel,
// veh_panel). std::function so the TU-static predicates bind directly.
static const std::map<std::string, std::function<bool()>> &render_predicate_registry()
{
    static const std::map<std::string, std::function<bool()>> reg = {
        { "spell_panel", spell_panel },
        { "veh_panel", veh_panel },
    };
    return reg;
}

// ── Sidebar HUD → RmlUi (Tier 7, render-only, continuous) ────────────────────
// Persistent HUD document (see panels.h). Lives in this TU so it can reuse the
// file-static stat colour helpers (str_string/etc.). NOT the modal rml_doc harness:
// the HUD has no blocking input loop, so there is NO 16ms input tick — the main game
// loop ticks the RmlUi context every frame, and open order keeps modal screens
// stacked above the HUD. Lifecycle is driven from game::draw_panels + cleanup_at_end.
namespace
{
// Bound model (slice 3 structural pivot): the sidebar is ONE flex column of rows, one
// row per present panel in layout order. Each row is a pre-rendered RML string (a
// migrated producer's output, or a "[name]" placeholder) plus a flex flag (set for the
// sentinel-height log/minimap panels so they grow to fill the column). data-for in
// sidebar_hud.rml iterates `rows`; C++ rebuilds the vector each sync.
struct hud_row_model {
    Rml::String rml;
    bool flex = false;
};
struct hud_rml_model {
    Rml::Vector<hud_row_model> rows;
    Rml::DataModelHandle handle;
};
std::unique_ptr<hud_rml_model> g_hud_data;
Rml::ElementDocument *g_hud_doc = nullptr;

// One row mirroring draw_stats: "STR n  DEX n  INT n  PER n"; each value is coloured
// by the same str_string/etc. helper draw_stats uses (label stays default HUD colour).
// The cata colour tags become RML spans via cata_text_to_rml in sidebar_hud_sync.
std::string hud_stats_text( avatar &u )
{
    const auto seg = [&]( const std::string & label, const nc_color clr, int val ) {
        const std::string num = val < 100 ? std::to_string( val ) : "99+";
        return label + " " + colorize( num, clr );
    };
    return seg( _( "STR" ), str_string( u ).first, u.get_str() ) + "  " +
           seg( _( "DEX" ), dex_string( u ).first, u.get_dex() ) + "  " +
           seg( _( "INT" ), int_string( u ).first, u.get_int() ) + "  " +
           seg( _( "PER" ), per_string( u ).first, u.get_per() );
}

// Mirrors draw_stat_wide / draw_stat_narrow (the labels/wide + narrow "Stats" variant):
// STR/DEX/INT/PER plus a Power readout and the Safe-mode flag, two rows. draw_stats
// (classic) omits Power+Safe, so this is a distinct content variant — the layout's panel
// name selects which producer runs (see g_hud_owned).
std::string hud_stats_wide( avatar &u )
{
    const auto seg = [&]( const std::string & label, const nc_color clr, const std::string & val ) {
        return label + " " + colorize( val, clr );
    };
    const std::pair<nc_color, std::string> pwr = power_stat( u );
    std::string out =
        seg( _( "Str" ), str_string( u ).first, std::to_string( u.get_str() ) ) + "  " +
        seg( _( "Dex" ), dex_string( u ).first, std::to_string( u.get_dex() ) ) + "  " +
        seg( _( "Power" ), pwr.first, pwr.second ) + "\n";
    out +=
        seg( _( "Int" ), int_string( u ).first, std::to_string( u.get_int() ) ) + "  " +
        seg( _( "Per" ), per_string( u ).first, std::to_string( u.get_per() ) ) + "  " +
        seg( _( "Safe" ), safe_color(), g->safe_mode ? _( "On" ) : _( "Off" ) );
    return out;
}

// Mirrors draw_stealth (the "Sound" panel): Speed value + move-mode counter + sound
// level (or DEAF). Reproduced in reading order with simple spacing — not the curses
// cell-exact columns — per the slice-1 precedent (eyeball judges parity).
std::string hud_sound_text( avatar &u )
{
    std::string r = std::string( _( "Speed" ) ) + " " +
                    colorize( std::to_string( u.get_speed() ), value_color( u.get_speed() ) );
    const std::string move_string = std::to_string( u.movecounter ) + move_mode_string( u );
    r += "  " + colorize( move_string, move_mode_color( u ) );
    if( u.is_deaf() ) {
        r += "  " + colorize( _( "DEAF" ), c_red );
    } else {
        r += "  " + std::string( _( "Sound:" ) ) + " " +
             colorize( std::to_string( u.volume ), u.volume != 0 ? c_yellow : c_light_gray );
    }
    return r;
}

// Mirrors draw_needs_compact (the "Needs" panel), 3 rows. Curses lays it out as two
// columns (hunger/fatigue/pain | thirst/temp/focus); reproduced row-by-row in reading
// order, left field then right field, per the slice-1 precedent.
std::string hud_needs_text( avatar &u )
{
    const auto desc = []( const std::pair<std::string, nc_color> &p ) {
        return colorize( p.first, p.second );
    };
    const std::pair<std::string, nc_color> hunger = u.get_hunger_description();
    const std::pair<std::string, nc_color> thirst = u.get_thirst_description();
    const std::pair<std::string, nc_color> fatigue = u.get_fatigue_description();
    const std::pair<std::string, nc_color> pain = u.get_pain_description();
    const std::pair<nc_color, std::string> temp = temp_stat( u );
    const std::pair<nc_color, std::string> arrow = temp_delta_arrows( u );
    std::string out = desc( hunger ) + "   " + desc( thirst ) + "\n";
    out += desc( fatigue ) + "   " + colorize( temp.second, temp.first ) +
           colorize( arrow.second, arrow.first ) + "\n";
    out += desc( pain ) + "   " + std::string( _( "Focus" ) ) + " " +
           colorize( std::to_string( u.focus_pool ), focus_color( u.focus_pool ) );
    return out;
}

// Mirrors draw_needs_labels / draw_needs_narrow (labels/wide + narrow "Needs" variant):
// Pain/Thirst, Rest/Hunger, Heat — NO fatigue-arrow or Focus (those belong to the
// compact variant above). Distinct content → its own producer.
std::string hud_needs_labels( avatar &u )
{
    const auto desc = []( const std::pair<std::string, nc_color> &p ) {
        return colorize( p.first, p.second );
    };
    const std::pair<nc_color, std::string> temp = temp_stat( u );
    std::string out = std::string( _( "Pain" ) ) + " " + desc( u.get_pain_description() ) + "   " +
                      std::string( _( "Thirst" ) ) + " " + desc( u.get_thirst_description() ) + "\n";
    out += std::string( _( "Rest" ) ) + " " + desc( u.get_fatigue_description() ) + "   " +
           std::string( _( "Hunger" ) ) + " " + desc( u.get_hunger_description() ) + "\n";
    out += std::string( _( "Heat" ) ) + " " + colorize( temp.second, temp.first );
    return out;
}

// Mirrors draw_sound_labels / draw_sound_narrow (labels/wide + narrow "Sound" variant):
// just the sound level (or Deaf!). draw_stealth (compact) also shows Speed + move, so
// that stays a separate producer (hud_sound_text).
std::string hud_sound_labels( avatar &u )
{
    if( u.is_deaf() ) {
        return std::string( _( "Sound:" ) ) + " " + colorize( _( "Deaf!" ), c_red );
    }
    return std::string( _( "Sound:" ) ) + " " + colorize( std::to_string( u.volume ), c_yellow );
}

// Mirrors draw_weightvolume_* (Wgt + Volume). All variants render the same content
// (only column layout differs), so one producer serves every variant. Threshold colours
// match draw_weightvolume_labels.
std::string hud_wgtvol( avatar &u )
{
    const nc_color wclr = u.weight_carried() > u.weight_capacity() ? c_red :
                          u.weight_carried() > u.weight_capacity() * 0.75 ? c_yellow : c_light_gray;
    const nc_color vclr = u.volume_carried() > u.volume_capacity() * 0.85 ? c_red :
                          u.volume_carried() > u.volume_capacity() * 0.65 ? c_yellow : c_light_gray;
    return std::string( _( "Wgt" ) ) + " " + colorize( carry_weight_string( u ), wclr ) + "   " +
           std::string( _( "Volume" ) ) + " " + colorize( carry_volume_string( u ), vclr );
}

// Mirrors print_mana (the native "Mana" panel, all variants — content is identical
// across them, only spacing differs).
std::string hud_mana( avatar &u )
{
    const std::pair<nc_color, std::string> m = mana_stat( u );
    return std::string( _( "Mana" ) ) + " " + colorize( m.second, m.first ) + "   " +
           std::string( _( "Max" ) ) + " " +
           colorize( std::to_string( u.magic->max_mana( u ) ), c_light_blue );
}

// Mirrors draw_hint: the panel-options keybind prompt. Single variant.
std::string hud_hint( avatar & )
{
    const std::string press = press_x( ACTION_TOGGLE_PANEL_ADM );
    return colorize( press, c_light_green ) + " " +
           colorize( _( "to open sidebar options" ), c_white );
}

// Mirrors draw_char_wide (the "Movement" panel): Sound/Mood/Focus, then Stam/Speed/Move.
// Smiley + stamina are text (emote string / hp-bar string), so this is pure text.
std::string hud_movement( avatar &u )
{
    const std::pair<nc_color, int> morale = morale_stat( u );
    const bool m_style = get_option<std::string>( "MORALE_STYLE" ) == "horizontal";
    const std::string smiley = morale_emotion( morale.second, get_face_type( u ), m_style );
    const nc_color move_color = move_mode_color( u );
    const std::string movecost = std::to_string( u.movecounter ) + "(" + move_mode_string( u ) + ")";
    const nc_color stam_clr = get_hp_bar( u.get_stamina(), u.get_stamina_max() ).second;
    const std::string stam = get_option<std::string>( "HEALTH_STYLE" ) == "number"
                             ? std::to_string( u.get_stamina() )
                             : get_hp_bar( u.get_stamina(), u.get_stamina_max() ).first;
    std::string out =
        std::string( _( "Sound" ) ) + " " + colorize( std::to_string( u.volume ), c_light_gray ) + "  " +
        std::string( _( "Mood" ) ) + " " + colorize( smiley, morale.first ) + "  " +
        std::string( _( "Focus" ) ) + " " +
        colorize( std::to_string( u.focus_pool ), focus_color( u.focus_pool ) ) + "\n";
    out +=
        std::string( _( "Stam" ) ) + " " + colorize( stam, stam_clr ) + "  " +
        std::string( _( "Speed" ) ) + " " +
        colorize( std::to_string( u.get_speed() ), focus_color( u.get_speed() ) ) + "  " +
        std::string( _( "Move" ) ) + " " + colorize( movecost, move_color );
    return out;
}

// Mirrors draw_weapon_labels: wielded weapon + martial style. fmt_wielded_weapon carries
// its own colour tags; the gray wrap only tints any untagged remainder.
std::string hud_weapon( avatar &u )
{
    return std::string( _( "Wield" ) ) + " " +
           colorize( character_funcs::fmt_wielded_weapon( u ), c_light_gray ) + "\n" +
           std::string( _( "Style" ) ) + " " +
           colorize( u.martial_arts_data->selected_style_name( u ), c_light_gray );
}

// Mirrors draw_armor / draw_armor_padding: per-body-part outermost armor. get_armor()
// returns already-coloured text. Single content variant.
std::string hud_armor( avatar &u )
{
    const unsigned int maxlen = 24;
    const auto row = [&]( const std::string & label, const char *bp ) {
        return colorize( label, c_light_gray ) + " " + get_armor( u, bodypart_id( bp ), maxlen );
    };
    return row( _( "Head" ), "head" ) + "\n" +
           row( _( "Torso" ), "torso" ) + "\n" +
           row( _( "Arms" ), "arm_r" ) + "\n" +
           row( _( "Legs" ), "leg_r" ) + "\n" +
           row( _( "Feet" ), "foot_r" );
}

// One body-part's HP as a coloured string — reproduces draw_limb_health's TEXT (broken
// limb #/= mend bar / number / 5-cell hp bar + trailing dots), no curses window. Used by
// hud_limbs.
std::string hud_limb_health( avatar &u, const bodypart_id &bp, bool num_style )
{
    const int hp_cur = u.get_part_hp_cur( bp );
    const int hp_max = u.get_part_hp_max( bp );
    const bool checked = u.has_effect( effect_got_checked );
    std::optional<nc_color> color_override;
    if( u.is_limb_broken( bp.id() ) && !bp->essential ) {
        const int mend_perc = hp_max > 0 ? 100 * hp_cur / hp_max : 0;
        const bool splinted = u.worn_with_flag( json_flag_SPLINT, bp ) ||
                              ( u.mutation_value( "mending_modifier" ) >= 1.0f );
        const nc_color color = splinted ? c_blue : c_dark_gray;
        if( num_style || checked ) {
            color_override = color;
        } else {
            const int num = mend_perc / 20;
            return colorize( std::string( num, '#' ) + std::string( 5 - num, '=' ), color );
        }
    }
    std::pair<std::string, nc_color> hp = get_hp_bar( hp_cur, hp_max );
    if( color_override ) {
        hp.second = *color_override;
    }
    if( num_style || checked ) {
        return colorize( string_format( "%3d", hp_cur ), hp.second );
    }
    std::string bar = colorize( hp.first, hp.second );
    const int dots = 5 - utf8_width( hp.first );
    if( dots > 0 ) {
        bar += colorize( std::string( dots, '.' ), c_white );
    }
    return bar;
}

// Mirrors draw_limb_wide / draw_limb2 / draw_limb_narrow (all limb variants — same data,
// only layout differs, so one producer serves all): "<name>: <hp bar>" per body part in
// reading order, one row each. Full colour fidelity (limb_color name + hp-bar colour).
std::string hud_limbs( avatar &u )
{
    const bool num_style = get_option<std::string>( "HEALTH_STYLE" ) == "number";
    std::string out;
    bool first = true;
    for( const bodypart_id &bp : u.get_all_body_parts( true ) ) {
        if( !first ) {
            out += "\n";
        }
        first = false;
        const std::string name = left_justify( body_part_hp_bar_ui_text( bp.id() ), 5 );
        out += colorize( name, u.limb_color( bp.id(), true, true, true ) ) + " " +
               hud_limb_health( u, bp, num_style );
    }
    return out;
}

// Mirrors draw_messages (the "Log" panel): the recent message buffer in chronological
// order, one line each. MVP CHEAPEST SOURCE — Messages::recent_messages drops the
// per-type colour + age fade the curses display applies (FIDELITY GAP, flag for phase 2:
// add a coloured accessor). The flex row grows; content top-aligns + clips if it overruns.
std::string hud_log( avatar & )
{
    std::string out;
    bool first = true;
    for( const std::pair<std::string, std::string> &m : Messages::recent_messages( 20 ) ) {
        if( !first ) {
            out += "\n";
        }
        first = false;
        out += m.second;
    }
    return out;
}

// Mirrors draw_loc_labels (the Location panel, all loc_* variants): Place / X,Y,Z / Sky
// (weather) / Light / Date / Time, one row each, full colour. The optional inline overmap
// minichunk (draw_loc_wide_map's `minimap` flag → draw_overmap_chunk) is GRAPHICAL and
// DROPPED for MVP (phase 2, like the pixel minimap RTT). draw_location_classic is a
// different compact layout and is NOT served here.
std::string hud_location( avatar &u )
{
    const oter_id &cur_ter = ACTIVE_OVERMAP_BUFFER.ter( u.abs_omt_pos() );
    const tripoint_abs_omt coord = u.abs_omt_pos();
    std::string out = std::string( _( "Place: " ) ) +
                      colorize( cur_ter->get_name(), c_white ) + "\n";
    out += std::string( _( "X,Y,Z: " ) );
    if( get_option<std::string>( "OVERMAP_COORDINATE_FORMAT" ) == "subdivided" ) {
        point_abs_om abs_coord;
        tripoint_om_omt rel_coord;
        std::tie( abs_coord, rel_coord ) = project_remain<coords::om>( coord );
        out += colorize( string_format( "%d'%d, %d'%d, %d", abs_coord.x(), rel_coord.x(),
                                        abs_coord.y(), rel_coord.y(), coord.z() ), c_white );
    } else {
        out += colorize( string_format( "%d, %d, %d", coord.x(), coord.y(), coord.z() ), c_white );
    }
    out += "\n";
    if( g->get_levz() < 0 ) {
        out += std::string( _( "Sky  : Underground" ) );
    } else {
        out += std::string( _( "Sky  :" ) ) + " " +
               colorize( get_weather().weather_id->name.translated(),
                         get_weather().weather_id->color );
    }
    out += "\n";
    const std::pair<std::string, nc_color> ll = get_light_level(
                character_funcs::fine_detail_vision_mod( get_avatar() ) );
    out += std::string( _( "Light:" ) ) + " " + colorize( ll.first, ll.second ) + "\n";
    out += string_format( _( "Date : %s, day %d" ),
                          calendar::name_season( season_of_year( calendar::turn ) ),
                          day_of_season<int>( calendar::turn ) + 1 ) + "\n";
    if( u.has_watch() ) {
        out += string_format( _( "Time : %s" ), to_string_time_of_day( calendar::turn ) );
    } else if( g->get_levz() >= 0 ) {
        out += string_format( _( "Time : %s" ), time_approx() );
    } else {
        // NOLINTNEXTLINE(cata-text-style): the question mark does not end a sentence
        out += std::string( _( "Time : ???" ) );
    }
    return out;
}

// Compass — MVP DESIGN SIMPLIFICATION. The native full compass (draw_compass_padding ->
// g->mon_info) is a GRAPHICAL 3x3 directional symbol grid + creature list → phase 2. For
// MVP this renders the essential can't-play-without datum the simple-compass uses: enemy
// COUNTS per direction (cached visible_count_by_dir, octants N/NE/E/SE/S/SW/W/NW + local).
// A design call for the user: counts vs the symbol grid (flag at eyeball).
std::string hud_compass( avatar &u )
{
    static constexpr std::array<const char *, 9> dir_labels = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW", "--"
    };
    const auto &counts = u.get_mon_visible().visible_count_by_dir;
    std::string out;
    for( int i = 0; i < 9; ++i ) {
        if( counts[i] > 0 ) {
            out += string_format( "%s(%d) ", dir_labels[i], counts[i] );
        }
    }
    if( out.empty() ) {
        return colorize( _( "No enemies in sight" ), c_dark_gray );
    }
    return colorize( out, c_white );
}

// VARIANT-AWARE producer table: maps a window_panel name to the producer that
// reproduces THAT variant's content. A logical panel (e.g. Stats) appears in any one
// layout under exactly one name, so several rows point at different producers — the
// runtime name selects the right content (classic draw_stats vs labels draw_stat_wide
// genuinely render different fields). Names matched case-insensitively (hud_producer).
//
// Two naming schemes coexist (make_native_widget_panel names a panel by its widget id;
// the built-in classic/narrow/labels layouts name it by the translate_marker label):
//   - built-in label:      "Stats" / "Sound" / "Needs" / "Wgt/Vol"
//   - widget id (variant): "stats"(wide) / "stats_compact"(classic) / "stats_narrow", etc.
// Unlisted names have NO producer → sidebar_hud_sync emits a "[name]" placeholder (no
// curses fallback after whole-sidebar suppression). "Mana" (the label) is deliberately
// NOT listed: it collides with the val_mana value-widget (a bare number), so only the
// native mana* ids map to the full readout.
struct hud_producer_entry {
    const char *panel_name;                  // widget id OR built-in label (CI match)
    std::string ( *produce )( avatar & );    // variant-specific content producer
};
const std::array<hud_producer_entry, 46> g_hud_producers = {{
        // Stats — classic (draw_stats) vs labels/wide + narrow (draw_stat_wide/_narrow)
        { "Stats",         hud_stats_text },
        { "stats_compact", hud_stats_text },
        { "stats",         hud_stats_wide },
        { "stats_narrow",  hud_stats_wide },
        // Sound — compact (draw_stealth: speed+move+sound) vs labels/narrow (sound only)
        { "Sound",         hud_sound_text },
        { "sound_compact", hud_sound_text },
        { "sound",         hud_sound_labels },
        { "sound_narrow",  hud_sound_labels },
        // Needs — compact (arrows+Focus) vs labels/narrow (pain/thirst/rest/hunger/heat)
        { "Needs",         hud_needs_text },
        { "needs_compact", hud_needs_text },
        { "needs",         hud_needs_labels },
        { "needs_narrow",  hud_needs_labels },
        // Wgt/Vol — content identical across variants (only columns differ)
        { "Wgt/Vol",              hud_wgtvol },
        { "weightvolume",         hud_wgtvol },
        { "weightvolume_compact", hud_wgtvol },
        { "weightvolume_narrow",  hud_wgtvol },
        // Mana — native mana panel only (NOT the "Mana" value-widget label)
        { "mana",         hud_mana },
        { "mana_compact", hud_mana },
        { "mana_narrow",  hud_mana },
        { "mana_wide",    hud_mana },
        // Pure-text panels (no widget icons / embedded graphics)
        { "hint",          hud_hint },
        { "Hint",          hud_hint },
        { "movement",      hud_movement },
        { "weapon",        hud_weapon },
        { "armor",         hud_armor },
        { "armor_classic", hud_armor },
        { "Armor",         hud_armor },
        // Limbs — HP per body part (all variants same data, layout differs)
        { "limbs",         hud_limbs },
        { "limbs_compact", hud_limbs },
        { "limbs_narrow",  hud_limbs },
        { "Limbs",         hud_limbs },
        // Log — recent message buffer
        { "log",           hud_log },
        { "log_classic",   hud_log },
        { "Log",           hud_log },
        // Location — loc_labels family (text rows; inline overmap chunk dropped, phase 2)
        { "location",      hud_location },
        { "location_alt",  hud_location },
        { "location_narrow", hud_location },
        { "Location",      hud_location },
        // Compass — MVP directional enemy COUNTS (full mon_info symbol grid is phase 2)
        { "compass",            hud_compass },
        { "compass_comp",       hud_compass },
        { "compass_simple",     hud_compass },
        { "compass_compact",    hud_compass },
        { "compass_comp_compact", hud_compass },
        { "Compass",            hud_compass },
        { "Compact Compass",    hud_compass },
        { "Simple Compass",     hud_compass },
    }
};

std::string ( *hud_producer( const std::string &name ) )( avatar & )
{
    const std::string lname = to_lower_case( name );
    for( const hud_producer_entry &p : g_hud_producers ) {
        if( lname == to_lower_case( p.panel_name ) ) {
            return p.produce;
        }
    }
    return nullptr;
}
} // namespace

bool &sidebar_hud_rmlui_enabled()
{
    // Default ON (Tier 7 Phase-1 MVP flip, 2026-06-20): the flex-column HUD covers every
    // text panel; remaining slots (minimap/bodygraph/full-compass/val_*) show visible
    // [name] placeholders until phase 2. Toggle via the F4 panel for an A/B vs curses.
    static bool enabled = true;
    return enabled;
}

void sidebar_hud_open()
{
    if( g_hud_doc != nullptr ) {
        return;  // already open (idempotent)
    }
    if( !sidebar_hud_rmlui_enabled() || !rmlui_layer::ready() ) {
        return;
    }
    Rml::Context *ctx = rmlui_layer::context();
    if( ctx == nullptr ) {
        return;
    }
    Rml::DataModelConstructor c = ctx->CreateDataModel( "sidebar_hud" );
    if( !c ) {
        return;
    }
    // Register the row struct + array once per data-model construction. RegisterStruct
    // is context-global and persists past RemoveDataModel; re-registering on reopen is
    // safe (uilist precedent, plan "Type-register reuse across reopen").
    Rml::StructHandle<hud_row_model> rh = c.RegisterStruct<hud_row_model>();
    rh.RegisterMember( "rml", &hud_row_model::rml );
    rh.RegisterMember( "flex", &hud_row_model::flex );
    c.RegisterArray<Rml::Vector<hud_row_model>>();
    g_hud_data = std::make_unique<hud_rml_model>();
    c.Bind( "rows", &g_hud_data->rows );
    g_hud_data->handle = c.GetModelHandle();
    // passive=true: render-only HUD — it must not capture in-game world mouse
    // (look/examine). See rmlui_layer::any_interactive_open / process_event.
    Rml::ElementDocument *doc =
        rmlui_layer::open_document( PATH_INFO::datadir() + "gui/sidebar_hud.rml", true );
    if( doc == nullptr ) {
        // Roll back so a failed open leaves no dangling model (cf. rml_doc::open).
        ctx->RemoveDataModel( "sidebar_hud" );
        g_hud_data.reset();
        return;
    }
    g_hud_doc = doc;
}

// Position the single column container (#hud-sidebar) at the sidebar rect. The curses
// terminal is TERMX×TERMY cells over the full window, so the sidebar (W cells wide, full
// height) is (W/TERMX*100)% wide, full height, anchored at the left or right edge per
// SIDEBAR_POSITION. Called every sync so it tracks resize for free. Width = the layout's
// first-panel width (matches the legacy overflow-marker assumption).
static void sidebar_hud_apply_rect()
{
    if( g_hud_doc == nullptr || TERMX <= 0 ) {
        return;
    }
    const auto &layout = panel_manager::get_manager().get_current_layout();
    if( layout.begin() == layout.end() ) {
        return;
    }
    const int wd = layout.begin()->get_width();
    const bool sidebar_right = get_option<std::string>( "SIDEBAR_POSITION" ) == "right";
    const float width_pct = 100.0f * wd / TERMX;
    const float left_pct = sidebar_right ? 100.0f - width_pct : 0.0f;
    Rml::Element *el = g_hud_doc->GetElementById( "hud-sidebar" );
    if( el == nullptr ) {
        return;
    }
    el->SetProperty( "left", string_format( "%.4f%%", left_pct ) );
    el->SetProperty( "top", "0%" );
    el->SetProperty( "width", string_format( "%.4f%%", width_pct ) );
    el->SetProperty( "height", "100%" );
}

void sidebar_hud_sync( avatar &u )
{
    if( g_hud_doc == nullptr || !g_hud_data ) {
        return;
    }
    // One-shot coverage dump the first sync after the HUD opens — surfaces which layout
    // panels still render a [name] placeholder (the Tier-10 rip-out gate audit).
    static bool coverage_logged = false;
    if( !coverage_logged ) {
        coverage_logged = true;
        DebugLog( DL::Info, DC::Main ) << sidebar_hud_coverage_report();
    }
    // Whole-sidebar ownership: rebuild the row list from EVERY present panel (toggled on +
    // render predicate true) in layout order. A migrated panel emits its producer's RML; an
    // unmigrated panel emits a visible "[name]" placeholder (never a silent blank — after
    // whole-sidebar suppression there is no curses fallback). Sentinel-height panels (log
    // -2 / minimap -1) get flex=true so they grow to fill the column.
    g_hud_data->rows.clear();
    for( const window_panel &panel : panel_manager::get_manager().get_current_layout() ) {
        if( !panel.toggle || !panel.render() ) {
            continue;
        }
        hud_row_model row;
        std::string( *produce )( avatar & ) = hud_producer( panel.get_name() );
        if( produce != nullptr ) {
            row.rml = cata_text_to_rml( produce( u ) );
        } else {
            row.rml = cata_text_to_rml( colorize( "[" + panel.get_name() + "]", c_dark_gray ) );
        }
        row.flex = panel.get_height() < 0;
        g_hud_data->rows.push_back( std::move( row ) );
    }
    g_hud_data->handle.DirtyVariable( "rows" );
    sidebar_hud_apply_rect();
}

void sidebar_hud_close()
{
    if( g_hud_doc == nullptr ) {
        return;
    }
    rmlui_layer::close_document( g_hud_doc );
    if( Rml::Context *ctx = rmlui_layer::context() ) {
        ctx->RemoveDataModel( "sidebar_hud" );
    }
    g_hud_doc = nullptr;
    g_hud_data.reset();
}

bool sidebar_hud_active()
{
    // True iff the HUD doc is open → game::draw_panels suppresses the WHOLE curses
    // sidebar (the column owns the entire region). Replaces the per-panel owns_panel gate.
    return g_hud_doc != nullptr;
}

bool sidebar_hud_has_producer( const std::string &name )
{
    // A panel is "covered" iff it has an RmlUi producer (else sidebar_hud_sync emits a
    // visible [name] placeholder). Mechanical input to the Tier-10 rip-out coverage gate.
    return hud_producer( name ) != nullptr;
}

std::string sidebar_hud_coverage_report()
{
    // Walk the active sidebar layout and classify each present panel covered/uncovered.
    // "Is every panel in my one UI built?" — the rip-out flip gate, as a mechanical check.
    int total = 0;
    int covered = 0;
    std::string uncovered;
    for( const window_panel &panel : panel_manager::get_manager().get_current_layout() ) {
        if( !panel.toggle || !panel.render() ) {
            continue;
        }
        total++;
        if( sidebar_hud_has_producer( panel.get_name() ) ) {
            covered++;
        } else {
            uncovered += ( uncovered.empty() ? "" : ", " ) + panel.get_name();
        }
    }
    std::string out = string_format( _( "sidebar HUD coverage: %d/%d panels" ), covered, total );
    if( !uncovered.empty() ) {
        out += " — uncovered: " + uncovered;
    }
    return out;
}

/* Floating HUD panels implementation. */
#include "hud_manager.h"

void floating_hud_open()
{
    hud_manager::instance().open_all();
}

void floating_hud_sync( avatar &u )
{
    hud_manager::instance().update( u );
}

void floating_hud_close()
{
    hud_manager::instance().close_all();
}

bool floating_hud_active()
{
    return hud_manager::instance().is_active();
}
// Resolve a widget's "show_if" to a window_panel render predicate (the data-driven
// equivalent of the hardcoded panels' render_func). Empty / unknown → always show.
static std::function<bool()> resolve_widget_show_if( const widget &w )
{
    const std::string &gate = w.show_if();
    if( gate.empty() ) {
        return default_render;
    }
    const auto &preg = render_predicate_registry();
    const auto pit = preg.find( gate );
    if( pit != preg.end() ) {
        return pit->second;
    }
    debugmsg( "widget '%s' references unknown show_if '%s'",
              w.getId().c_str(), gate.c_str() );
    return default_render;
}

window_panel make_native_widget_panel( const widget &w, int width )
{
    // Tier-10 curses rip-out: native widgets no longer resolve a curses draw_*.
    // The RmlUi HUD renders each panel by NAME via hud_producer(); build the
    // window_panel name-only (no curses draw). The widget id is the stable
    // save/match key and the hud_producer lookup key.
    const int panel_width = std::max( 1, width > 0 ? width : w.width() );
    const bool default_toggle = !w.has_flag( "W_DISABLED_BY_DEFAULT" );
    const bool force_draw = w.has_flag( "W_ALWAYS_DRAW" );
    return window_panel( {}, w.getId().str(), w.height(), panel_width,
                         default_toggle, resolve_widget_show_if( w ), force_draw );
}

// Color for a value widget's number, mirroring how the native draw_* panels
// color the same stat so the data-driven rows read like the legacy sidebar.
// Reuses the TU-static color helpers above; falls back to c_white.
static nc_color value_widget_color( const widget &w, int val, const avatar &u )
{
    switch( w.var() ) {
        case widget_var::stat_str:
            return str_string( u ).first;
        case widget_var::stat_dex:
            return dex_string( u ).first;
        case widget_var::stat_int:
            return int_string( u ).first;
        case widget_var::stat_per:
            return per_string( u ).first;
        case widget_var::speed:
            return value_color( u.get_speed() );
        case widget_var::stamina:
            return get_hp_bar( u.get_stamina(), u.get_stamina_max() ).second;
        case widget_var::thirst:
            return u.get_thirst_description().second;
        case widget_var::fatigue:
            return u.get_fatigue_description().second;
        case widget_var::morale:
            return morale_stat( u ).first;
        case widget_var::mana:
            return mana_stat( u ).first;
        case widget_var::max_mana:
            return c_light_blue;
        case widget_var::pain:
            // BN has no native pain-color helper; redden as perceived pain rises.
            return val <= 0 ? c_light_gray : val < 20 ? c_yellow : val < 40 ? c_light_red : c_red;
        // Body-graph dimensions never flow through the value renderer.
        case widget_var::body_graph:
        case widget_var::body_graph_temp:
        case widget_var::body_graph_encumb:
        case widget_var::body_graph_status:
        case widget_var::body_graph_wet:
        case widget_var::last:
            break;
    }
    return c_white;
}

// Max for a bounded value var — the divisor for a fill bar. nullopt → unbounded,
// so the widget shows a right-aligned number with no bar (pain/speed/morale/etc.
// have no clean ceiling). Only vars with a real max getter qualify.
static std::optional<int> value_var_max( widget_var var, const avatar &u )
{
    switch( var ) {
        case widget_var::stamina:
            return u.get_stamina_max();
        case widget_var::mana:
            return u.magic->max_mana( u );
        default:
            return std::nullopt;
    }
}

// Clean English gutter/save-key name for a value widget: strip the "val_" id
// prefix and capitalize ("val_pain" -> "Pain"). These widgets are new and
// opt-in — no save has ever persisted the raw id — so this becomes the stable
// save-key, re-localized via _() at display (show_adm). The translated _label
// stays the in-row display string.
static std::string value_widget_name( const widget_id &id )
{
    std::string name = id.str();
    if( name.starts_with( "val_" ) ) {
        name.erase( 0, 4 );
    }
    // Title-case each underscore-separated segment, joining with spaces:
    // "val_pain" -> "Pain", "bodygraph_temp" -> "Bodygraph Temp".
    bool at_word_start = true;
    for( char &c : name ) {
        if( c == '_' ) {
            c = ' ';
            at_word_start = true;
        } else if( at_word_start ) {
            c = toupper( static_cast<unsigned char>( c ) );
            at_word_start = false;
        }
    }
    return name;
}

// Data-driven value widget: draws "[icon] label value" itself from the widget's
// _var (via get_var_value), with an optional leading two-tone SVG icon tinted to
// match the value color. The parity bridge (native) hands its whole window to a
// draw_* fn; this renderer owns the layout, so it is where icons have a clean home.
window_panel make_value_widget_panel( const widget &w, int width )
{
    const widget_id id = w.getId();
    const std::string label = w.label().translated();
    const std::string &icon = w.icon();
    auto draw_func = [id, label, icon]( avatar & u, const catacurses::window & win ) {
        werase( win );
        if( !id.is_valid() ) {
            wnoutrefresh( win );
            return;
        }
        const widget &wd = *id; // static widget data; u carries the live state
        const int val = wd.get_var_value( u );
        const nc_color val_color = value_widget_color( wd, val, u );
        // Animation: feed the live value to the registry once, then drive both the
        // icon transform and the row-change highlight. State is keyed by widget id
        // and lives in the registry, not this closure (closures rebuild on reload).
        const std::uint32_t now = sidebar_anim::now_ms();
        sidebar_anim::registry &reg = sidebar_anim::get();
        // Critical band reuses the value colour: the stat's own colouring already
        // turns red in its danger zone (high pain, low stamina, ...).
        const bool crit = val_color == c_red || val_color == c_light_red;
        // Row-change flash: a fading highlight bar behind the whole row, driven by
        // specs under the reserved "_row" id (the color_blend channel rests at 0,
        // so it is invisible until a change fires it).
        reg.update( id.str() + "#row", "_row", static_cast<double>( val ), crit, now );
        const sidebar_anim::icon_transform row_tr = reg.sample( id.str() + "#row", now );
        if( row_tr.blend > 0.001f ) {
            draw_widget_row_highlight( win, 0, getmaxx( win ), row_tr.blend_color, row_tr.blend );
        }
        int col = 0;
        if( !icon.empty() ) {
            // Tint the icon to the value color so a reddening stat reddens its glyph;
            // a value change pops the icon (scale ease-back).
            reg.update( id.str(), icon, static_cast<double>( val ), crit, now );
            const sidebar_anim::icon_transform tr = reg.sample( id.str(), now );
            draw_widget_icon( win, point( 0, 0 ), icon, val_color, tr );
            // A square icon is ~2 cells wide at the usual ~2:1 font ratio; start
            // the label at col 3 to leave a one-cell gap after it.
            col = 3;
        }
        mvwprintz( win, point( col, 0 ), c_light_gray, "%s", label );

        // Right-hand readout: a fill bar + percent for bounded vars (those with a
        // max), else the raw number. Reuses get_hp_bar's 5-cell bar string so it
        // reads like the native HP/stamina panels.
        const std::optional<int> vmax = value_var_max( wd.var(), u );
        std::string rhs;
        if( vmax && *vmax > 0 ) {
            // Clamp the percent to match get_hp_bar's clamped bar — val can exceed
            // max (e.g. buffs) and would otherwise print ">100%" beside a full bar.
            const int pct = std::clamp( 100 * val / *vmax, 0, 100 );
            rhs = get_hp_bar( val, *vmax ).first + string_format( " %d%%", pct );
        } else {
            rhs = string_format( "%d", val );
        }
        // Right-align to the panel edge, but never overlap the label.
        const int label_end = col + utf8_width( label ) + 1;
        const int rhs_x = std::max( label_end, getmaxx( win ) - utf8_width( rhs ) );
        mvwprintz( win, point( rhs_x, 0 ), val_color, "%s", rhs );
        wnoutrefresh( win );
    };
    const int panel_width = std::max( 1, width > 0 ? width : w.width() );
    const bool default_toggle = !w.has_flag( "W_DISABLED_BY_DEFAULT" );
    return window_panel( draw_func, value_widget_name( id ), w.height(), panel_width,
                         default_toggle, resolve_widget_show_if( w ) );
}

// Build a window_panel for any widget, dispatching on style. Unknown styles fall
// back to the native bridge.
// Per-body-part color for a body-graph dimension. The renderer-agnostic core:
// each body_graph* var picks a different per-bp value to color by, mirroring
// CDDA's display::get_bodygraph_bp_color but against BN's getters.

// Body-graph value widget: lays the main body parts in a 2-column grid (the
// draw_limb2 layout) and colors each limb label by the widget's body_graph*
// dimension. Color encodes the data; no HP bar, to stay distinct from the
// native Limbs panel.
window_panel make_bodygraph_widget_panel( const widget &w, int width )
{
    // Tier-10 curses rip-out: name-only. The RmlUi HUD shows a placeholder for the
    // body graph until the dedicated HUD plan builds it (no curses draw).
    const int panel_width = std::max( 1, width > 0 ? width : w.width() );
    const bool default_toggle = !w.has_flag( "W_DISABLED_BY_DEFAULT" );
    return window_panel( {}, value_widget_name( w.getId() ), w.height(), panel_width,
                         default_toggle, resolve_widget_show_if( w ) );
}

static window_panel make_widget_panel( const widget &w, int width )
{
    if( w.style() == "number" || w.style() == "value" ) {
        return make_value_widget_panel( w, width );
    }
    if( w.style() == "body_graph" ) {
        return make_bodygraph_widget_panel( w, width );
    }
    return make_native_widget_panel( w, width );
}

// Build selectable sidebar layouts from every "sidebar"-style widget and merge
// them into `layouts`, keyed by the widget id (e.g. "custom"). Called post-load
// so widget::get_all() is populated. Custom layouts are opt-in — they never
// replace a built-in or auto-switch a player.
static void inject_widget_layouts( std::map<std::string, std::vector<window_panel>> &layouts )
{
    for( const widget &sb : widget::get_all() ) {
        if( sb.style() != "sidebar" ) {
            continue;
        }
        std::vector<window_panel> panels;
        for( const widget_id &child : sb._widgets ) {
            if( child.is_valid() ) {
                panels.push_back( make_widget_panel( *child, sb.width() ) );
            }
        }
        if( !panels.empty() ) {
            layouts[sb.getId().str()] = std::move( panels );
        }
    }
}

panel_manager::panel_manager()
{
    // Tier-10 curses rip-out: no built-in curses layouts. The widget layout(s)
    // (data/json/ui/sidebar.json, e.g. "custom") are injected after world load via
    // reload_widget_layouts(); current_layout_id is overwritten from panel_options.json
    // in load() when a saved selection exists.
    current_layout_id = "custom";
}

std::vector<window_panel> &panel_manager::get_current_layout()
{
    auto kv = layouts.find( current_layout_id );
    if( kv != layouts.end() ) {
        return kv->second;
    }
    // The selected id may name a layout not built yet — the widget layouts are
    // injected only after world load (reload_widget_layouts). Built-in layouts were
    // removed (Tier-10 curses rip-out), so fall back to any existing layout WITHOUT
    // discarding current_layout_id; if none exist yet, return a static empty layout
    // so early callers (pre-world-load) never deref end().
    if( !layouts.empty() ) {
        return layouts.begin()->second;
    }
    static std::vector<window_panel> empty_layout;
    return empty_layout;
}

std::string panel_manager::get_current_layout_id() const
{
    return current_layout_id;
}

bool panel_manager::has_layout( const std::string &id ) const
{
    return layouts.contains( id );
}

int panel_manager::get_width_right()
{
    if( get_option<std::string>( "SIDEBAR_POSITION" ) == "left" ) {
        return width_left;
    }
    return width_right;
}

int panel_manager::get_width_left()
{
    if( get_option<std::string>( "SIDEBAR_POSITION" ) == "left" ) {
        return width_right;
    }
    return width_left;
}

void panel_manager::init()
{
    // NOTE: this runs in game::load_static_data, BEFORE world modfiles (widget
    // JSON) load — so widget-driven layouts are built later via
    // reload_widget_layouts(), called from game::setup after load_world_modfiles.
    load();
    // Layouts are empty until reload_widget_layouts() runs (after world load); guard the
    // deref. The real sidebar width is applied there. The sidebar isn't drawn before
    // then, so a 0 placeholder here is harmless.
    auto &layout = get_current_layout();
    update_offsets( layout.empty() ? 0 : layout.begin()->get_width() );
}

void panel_manager::reload_widget_layouts()
{
    inject_widget_layouts( layouts );
    // These layouts are built after panel_options.json was read, so re-apply any
    // saved toggle/order state for them now that they exist (e.g. the "custom"
    // sidebar). apply_saved_layout_entries is idempotent for the built-ins that
    // already had it applied during deserialize.
    auto &saved_layouts = saved_panel_layouts();
    for( auto &kv : layouts ) {
        const auto saved = saved_layouts.find( kv.first );
        if( saved != saved_layouts.end() ) {
            apply_saved_layout_entries( kv.second, saved->second,
                                        std::map<std::string, std::string> {} );
        }
    }
    // Built-in layouts were removed (Tier-10 curses rip-out), so update_offsets is no
    // longer called with a built-in width at init(). Apply the active widget layout's
    // sidebar width now that the layout exists.
    auto &layout = get_current_layout();
    if( !layout.empty() ) {
        update_offsets( layout.begin()->get_width() );
    }
}

auto panel_manager::sync_lua_panels() -> void
{
    const auto &widgets = cata::lua_sidebar_widgets::get_widgets();
    auto lua_name_by_id = std::map<std::string, std::string> {};
    auto next_names = std::set<std::string> {};
    std::ranges::for_each( widgets, [&]( const cata::lua_sidebar_widgets::widget_entry & widget ) {
        const auto panel_name = lua_panel_name( widget );
        lua_name_by_id.insert_or_assign( widget.id, panel_name );
        next_names.insert( panel_name );
    } );

    const auto previous_names = lua_panel_names;
    lua_panel_names = next_names;
    auto &saved_layouts = saved_panel_layouts();

    auto find_saved_entry = [&]( const std::vector<panel_layout_entry> &entries,
    const std::string & widget_id ) {
        return std::ranges::find_if( entries, [&]( const panel_layout_entry & entry ) {
            return entry.lua_id && *entry.lua_id == widget_id;
        } );
    };

    auto compute_insert_index = [&]( const std::vector<panel_layout_entry> &entries,
                                     const std::string & widget_id,
    const std::vector<window_panel> &layout ) -> std::optional<int> {
        const auto entry_it = find_saved_entry( entries, widget_id );
        if( entry_it == entries.end() )
        {
            return std::nullopt;
        }
        const auto entry_index = static_cast<size_t>(
            std::ranges::distance( entries.begin(), entry_it ) );
        auto before_view = entries | std::views::take( entry_index );
        const auto insert_index = std::ranges::count_if( before_view, [&]( const panel_layout_entry & entry )
        {
            const auto resolved_name = resolve_layout_entry_name( entry, lua_name_by_id );
            if( !resolved_name ) {
                return false;
            }
            const auto match = std::ranges::find( layout, *resolved_name, &window_panel::get_name );
            return match != layout.end();
        } );
        return static_cast<int>( insert_index );
    };

    auto sync_layout = [&]( const std::string & layout_id, std::vector<window_panel> &layout ) {
        std::erase_if( layout, [&]( const window_panel & panel ) {
            const auto name = panel.get_name();
            return previous_names.contains( name ) && !next_names.contains( name );
        } );

        const auto layout_width = layout.empty() ? 0 : layout.front().get_width();
        const auto saved_iter = saved_layouts.find( layout_id );
        const auto *saved_entries = saved_iter == saved_layouts.end() ? nullptr : &saved_iter->second;
        std::ranges::for_each( widgets, [&]( const cata::lua_sidebar_widgets::widget_entry & widget ) {
            const auto panel_name = lua_panel_name( widget );
            auto existing = std::ranges::find( layout, panel_name, &window_panel::get_name );
            if( existing == layout.end() ) {
                auto new_panel = make_lua_widget_panel( widget, layout_width );
                auto saved_index = std::optional<int> {};
                if( saved_entries != nullptr ) {
                    const auto entry_it = find_saved_entry( *saved_entries, widget.id );
                    if( entry_it != saved_entries->end() ) {
                        new_panel.toggle = entry_it->toggle;
                        saved_index = compute_insert_index( *saved_entries, widget.id, layout );
                    }
                }
                if( saved_index ) {
                    const auto max_index = static_cast<int>( layout.size() );
                    const auto insert_at = std::clamp( *saved_index, 0, max_index );
                    auto it = layout.begin();
                    std::ranges::advance( it, insert_at );
                    layout.insert( it, std::move( new_panel ) );
                } else if( widget.order ) {
                    const auto max_index = static_cast<int>( layout.size() );
                    const auto desired_index = std::max( 0, *widget.order - 1 );
                    const auto insert_at = std::clamp( desired_index, 0, max_index );
                    auto it = layout.begin();
                    std::ranges::advance( it, insert_at );
                    layout.insert( it, std::move( new_panel ) );
                } else {
                    layout.emplace_back( std::move( new_panel ) );
                }
                return;
            }
            const auto was_toggle = existing->toggle;
            auto updated = make_lua_widget_panel( widget, layout_width );
            updated.toggle = was_toggle;
            *existing = std::move( updated );
        } );
    };

    std::ranges::for_each( layouts, [&]( auto & entry ) {
        sync_layout( entry.first, entry.second );
    } );
}

void panel_manager::update_offsets( int x )
{
    width_right = x;
    width_left = 0;
}

bool panel_manager::save()
{
    return write_to_file( PATH_INFO::panel_options(), [&]( std::ostream & fout ) {
        JsonOut jout( fout, true );
        serialize( jout );
    }, _( "panel options" ) );
}

bool panel_manager::load()
{
    return read_from_file_json( PATH_INFO::panel_options(), [&]( JsonIn & jsin ) {
        deserialize( jsin );
    }, true );
}

void panel_manager::serialize( JsonOut &json )
{
    json.start_array();
    json.start_object();

    json.member( "current_layout_id", current_layout_id );
    json.member( "layouts" );

    json.start_array();
    const auto &widgets = cata::lua_sidebar_widgets::get_widgets();
    auto lua_id_by_name = std::map<std::string, std::string> {};
    std::ranges::for_each( widgets, [&]( const cata::lua_sidebar_widgets::widget_entry & widget ) {
        const auto panel_name = lua_panel_name( widget );
        lua_id_by_name.insert_or_assign( panel_name, widget.id );
    } );

    std::ranges::for_each( layouts, [&]( const auto & kv ) {
        json.start_object();

        json.member( "layout_id", kv.first );
        json.member( "panels" );

        json.start_array();

        std::ranges::for_each( kv.second, [&]( const window_panel & panel ) {
            json.start_object();

            json.member( "name", panel.get_name() );
            json.member( "toggle", panel.toggle );
            if( lua_panel_names.contains( panel.get_name() ) ) {
                const auto it = lua_id_by_name.find( panel.get_name() );
                if( it != lua_id_by_name.end() ) {
                    json.member( "lua_id", it->second );
                }
            }

            json.end_object();
        } );

        json.end_array();
        json.end_object();
    } );

    json.end_array();

    json.end_object();
    json.end_array();
}

void panel_manager::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    JsonObject joLayouts( jsin.get_object() );

    current_layout_id = joLayouts.get_string( "current_layout_id" );
    auto &saved_layouts = saved_panel_layouts();
    saved_layouts.clear();
    const auto layouts_array = joLayouts.get_array( "layouts" );
    const auto layouts_count = layouts_array.size();
    auto layout_indices = std::views::iota( size_t{ 0 }, layouts_count );
    std::ranges::for_each( layout_indices, [&]( const size_t layout_index ) {
        const auto joLayout = layouts_array.get_object( layout_index );
        const auto layout_id = joLayout.get_string( "layout_id" );
        // Always read "panels" — even for a layout not (yet) registered. The
        // runtime "custom" widget layout is built later (reload_widget_layouts,
        // after world load), so at this point it is absent from `layouts`. We
        // still consume + preserve its entries: skipping the field here left it
        // unvisited and tripped JsonObject::report_unvisited at load.
        auto entries = std::vector<panel_layout_entry> {};
        const auto panels_array = joLayout.get_array( "panels" );
        const auto panels_count = panels_array.size();
        auto panel_indices = std::views::iota( size_t{ 0 }, panels_count );
        std::ranges::for_each( panel_indices, [&]( const size_t panel_index ) {
            const auto joPanel = panels_array.get_object( panel_index );
            auto name = joPanel.get_string( "name" );
            const auto toggle = joPanel.get_bool( "toggle", true );
            auto lua_id = std::optional<std::string> {};
            if( joPanel.has_member( "lua_id" ) ) {
                lua_id = joPanel.get_string( "lua_id" );
            }
            entries.emplace_back( panel_layout_entry{
                .name = std::move( name ),
                .lua_id = std::move( lua_id ),
                .toggle = toggle,
            } );
        } );
        // Apply to the live layout if it exists now; preserve the saved entries
        // regardless, so a later-built layout (custom) can restore its toggles.
        auto layout_iter = layouts.find( layout_id );
        if( layout_iter != layouts.end() ) {
            apply_saved_layout_entries( layout_iter->second, entries,
                                        std::map<std::string, std::string> {} );
        }
        saved_layouts[layout_id] = std::move( entries );
    } );
    jsin.end_array();
}

// ---- show_adm RmlUi render path (P3 track-A) -------------------------------
// The `}` SIDEBAR OPTIONS menu. Render-only modal doc: the keyboard owns all
// nav / toggle / move / layout-switch; the model is synced each frame. Three
// columns (panel list / help / layouts) with a 2D cursor; the swap-drag reorder
// is reproduced by emitting the panel column in display order (the source row
// jumps to the cursor, yellow).
namespace
{
struct adm_rml_panel {
    Rml::String name_rml;
    bool selected = false;
    bool is_source = false;
};
struct adm_rml_layout {
    Rml::String name_rml;
    bool selected = false;
};
struct adm_rml_data {
    Rml::String title_rml;
    Rml::Vector<adm_rml_panel> panels;
    Rml::String col1_rml;
    Rml::Vector<adm_rml_layout> layouts;
    Rml::DataModelHandle handle;
};

bool g_panel_adm_types_registered = false;

void register_panel_adm_rml_types( Rml::DataModelConstructor &c )
{
    // RegisterStruct/Array are context-global and persist past RemoveDataModel —
    // guard so a reopen doesn't double-register (uilist-proven pattern).
    if( g_panel_adm_types_registered ) {
        return;
    }
    Rml::StructHandle<adm_rml_panel> ph = c.RegisterStruct<adm_rml_panel>();
    ph.RegisterMember( "name_rml", &adm_rml_panel::name_rml );
    ph.RegisterMember( "selected", &adm_rml_panel::selected );
    ph.RegisterMember( "is_source", &adm_rml_panel::is_source );
    c.RegisterArray<Rml::Vector<adm_rml_panel>>();
    Rml::StructHandle<adm_rml_layout> lh = c.RegisterStruct<adm_rml_layout>();
    lh.RegisterMember( "name_rml", &adm_rml_layout::name_rml );
    lh.RegisterMember( "selected", &adm_rml_layout::selected );
    c.RegisterArray<Rml::Vector<adm_rml_layout>>();
    g_panel_adm_types_registered = true;
}
} // namespace

bool &panel_adm_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

void panel_manager::show_adm()
{
    input_context ctxt( "PANEL_MGMT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "UP" );
    ctxt.register_action( "DOWN" );
    ctxt.register_action( "LEFT" );
    ctxt.register_action( "RIGHT" );
    ctxt.register_action( "MOVE_PANEL" );
    ctxt.register_action( "TOGGLE_PANEL" );

    const std::vector<int> column_widths = { 17, 37, 17 };

    size_t current_col = 0;
    size_t current_row = 0;
    bool swapping = false;
    size_t source_row = 0;
    size_t source_index = 0;

    bool recalc = true;
    bool exit = false;
    // map of row the panel is on vs index
    // panels not renderable due to game configuration will not be in this map
    std::map<size_t, size_t> row_indices;

    g->show_panel_adm = true;
    g->invalidate_main_ui_adaptor();

    catacurses::window w;

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        const auto panel_rows = static_cast<int>( layouts[current_layout_id].size() );
        const auto layout_rows = static_cast<int>( layouts.size() );
        const auto desired_rows = std::max( panel_rows, layout_rows );
        const auto window_height = std::clamp( desired_rows + 1, 21, TERMY - 2 );
        w = catacurses::newwin( window_height, 75,
                                point( ( TERMX / 2 ) - 38, ( TERMY / 2 ) - ( window_height / 2 ) ) );

        ui.position_from_window( w );
    } );
    ui.mark_resize();

    // ---- RmlUi render path (F.3 rml_doc harness) ----------------------------
    // `rml_data` before `rml` so the doc tears down while the model is alive. The
    // doc is rebuilt each frame from the live cursor + layout state; the keyboard
    // owns all editing.
    std::unique_ptr<adm_rml_data> rml_data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml || !rml_data ) {
            return;
        }
        adm_rml_data &d = *rml_data;
        auto &panels = layouts[current_layout_id];

        d.title_rml = cata_text_to_rml( colorize( _( "SIDEBAR OPTIONS" ), c_white ) );

        // Col 0: renderable panels in display order. During a swap-drag the source
        // panel jumps to the cursor row (yellow) and the others shift ±1 to open
        // the insertion gap — exactly the curses offset logic, applied to the
        // emit order instead of a y-coordinate.
        struct disp_row {
            int row = 0;
            size_t index = 0;
            bool is_source = false;
        };
        std::vector<disp_row> ordered;
        for( const std::pair<const size_t, size_t> &ri : row_indices ) {
            const size_t r = ri.first;
            const size_t idx = ri.second;
            if( swapping && idx == source_index ) {
                ordered.push_back( disp_row{ static_cast<int>( current_row ), idx, true } );
                continue;
            }
            int offset = 0;
            if( swapping ) {
                if( current_row > source_row && r > source_row && r <= current_row ) {
                    offset = -1;
                } else if( current_row < source_row && r < source_row && r >= current_row ) {
                    offset = 1;
                }
            }
            ordered.push_back( disp_row{ static_cast<int>( r ) + offset, idx, false } );
        }
        std::sort( ordered.begin(), ordered.end(),
        []( const disp_row & a, const disp_row & b ) {
            return a.row < b.row;
        } );
        d.panels.clear();
        for( const disp_row &dr : ordered ) {
            adm_rml_panel row;
            const nc_color col = dr.is_source ? c_yellow
                                 : ( panels[dr.index].toggle ? c_white : c_dark_gray );
            row.name_rml = cata_text_to_rml( colorize( _( panels[dr.index].get_name() ), col ) );
            row.selected = current_col == 0 && dr.row == static_cast<int>( current_row );
            row.is_source = dr.is_source;
            d.panels.push_back( std::move( row ) );
        }

        // Col 1: the static help/keys column (live keybind descriptions).
        const int col_width = column_widths[1] - 4;
        std::string h;
        h += colorize( trunc_ellipse( ctxt.get_desc( "TOGGLE_PANEL" ), col_width ) + ":",
                       c_light_green ) + "\n";
        h += colorize( _( "Toggle panels on/off" ), c_white ) + "\n";
        h += colorize( trunc_ellipse( ctxt.get_desc( "MOVE_PANEL" ), col_width ) + ":",
                       c_light_green ) + "\n";
        h += colorize( _( "Change display order" ), c_white ) + "\n";
        h += colorize( trunc_ellipse( ctxt.get_desc( "QUIT" ), col_width ) + ":",
                       c_light_green ) + "\n";
        h += colorize( _( "Exit" ), c_white );
        d.col1_rml = cata_text_to_rml( h );

        // Col 2: the layout list (current layout in light_blue).
        d.layouts.clear();
        size_t li = 0;
        for( const auto &layout : layouts ) {
            adm_rml_layout row;
            const nc_color c = current_layout_id == layout.first ? c_light_blue : c_white;
            row.name_rml = cata_text_to_rml( colorize( _( layout.first ), c ) );
            row.selected = current_col == 2 && current_row == li;
            d.layouts.push_back( std::move( row ) );
            ++li;
        }

        d.handle.DirtyVariable( "title_rml" );
        d.handle.DirtyVariable( "panels" );
        d.handle.DirtyVariable( "col1_rml" );
        d.handle.DirtyVariable( "layouts" );
    };
    rml.open( panel_adm_rmlui_enabled(), "panel_adm", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        rml_data = std::make_unique<adm_rml_data>();
        register_panel_adm_rml_types( c );
        c.Bind( "title_rml", &rml_data->title_rml );
        c.Bind( "panels", &rml_data->panels );
        c.Bind( "col1_rml", &rml_data->col1_rml );
        c.Bind( "layouts", &rml_data->layouts );
        rml_data->handle = c.GetModelHandle();
    } );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        // RmlUi path owns the modal — sync the model and skip the curses draw.
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    while( !exit ) {
        auto &panels = layouts[current_layout_id];

        if( recalc ) {
            recalc = false;

            row_indices.clear();
            for( size_t i = 0, row = 0; i < panels.size(); i++ ) {
                if( panels[i].render() ) {
                    row_indices.emplace( row, i );
                    row++;
                }
            }
        }

        const size_t num_rows = current_col == 0 ? row_indices.size() : layouts.size();
        current_row = clamp<size_t>( current_row, 0, num_rows - 1 );

        ui_manager::redraw();

        const std::string action = ctxt.handle_input();
        if( action == "UP" ) {
            if( current_row > 0 ) {
                current_row -= 1;
            } else {
                current_row = num_rows - 1;
            }
        } else if( action == "DOWN" ) {
            if( current_row + 1 < num_rows ) {
                current_row += 1;
            } else {
                current_row = 0;
            }
        } else if( action == "MOVE_PANEL" && current_col == 0 ) {
            swapping = !swapping;
            if( swapping ) {
                // source window from the swap
                // saving win1 index
                source_row = current_row;
                source_index = row_indices[current_row];
            } else {
                // dest window for the swap
                // saving win2 index
                const size_t target_index = row_indices[current_row];

                int distance = target_index - source_index;
                size_t step_dir = distance > 0 ? 1 : -1;
                for( size_t i = source_index; i != target_index; i += step_dir ) {
                    std::swap( panels[i], panels[i + step_dir] );
                }
                g->invalidate_main_ui_adaptor();
                recalc = true;
            }
        } else if( !swapping && action == "MOVE_PANEL" && current_col == 2 ) {
            auto iter = std::next( layouts.begin(), current_row );
            current_layout_id = iter->first;
            int width = panel_manager::get_manager().get_current_layout().begin()->get_width();
            update_offsets( width );
            int h; // to_map_font_dimension needs a second input
            to_map_font_dimension( width, h );
            // tell the game that the main screen might have a different size now.
            g->mark_main_ui_adaptor_resize();
            recalc = true;
        } else if( !swapping && ( action == "RIGHT" || action == "LEFT" ) ) {
            // there are only two columns
            if( current_col == 0 ) {
                current_col = 2;
            } else {
                current_col = 0;
            }
        } else if( !swapping && action == "TOGGLE_PANEL" && current_col == 0 ) {
            panels[row_indices[current_row]].toggle = !panels[row_indices[current_row]].toggle;
            g->invalidate_main_ui_adaptor();
        } else if( action == "QUIT" ) {
            exit = true;
            save();
        }
    }

    g->show_panel_adm = false;
    g->invalidate_main_ui_adaptor();
}
