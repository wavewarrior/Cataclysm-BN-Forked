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
#include "character_display.h"
#include "character_effects.h"
#include "character_functions.h"
#include "character_martial_arts.h"
#include "hud_shake.h"
#include "character_oracle.h"
#include "color.h"
#include "cursesdef.h"
#include "debug.h"
#include "widget.h"
#include "widget_icon.h"
#include "sidebar_anim.h"
#include "hud_anim.h"
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
#include "weather_type.h"
#ifdef COOP_ENABLED
#include "coop_session.h"
#include "coop_server.h"
#endif

static const trait_id trait_THRESH_FELINE( "THRESH_FELINE" );
static const trait_id trait_THRESH_BIRD( "THRESH_BIRD" );
static const trait_id trait_THRESH_URSINE( "THRESH_URSINE" );

static const efftype_id effect_got_checked( "got_checked" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_bite( "bite" );
static const efftype_id effect_infected( "infected" );

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

// Colored-text overmap minichunk for the RmlUi HUD "map" panel. Reuses the terrain/
// note/vehicle cell lookup draw_overmap_chunk (now removed; had zero remaining callers
// after the Tier-10 curses rip-out) fed into its w_minimap window. MVP simplification
// (locked design decision): the mission-arrow-past-the-grid-edge and border horde
// markers were graphical bonuses layered on the physical pixel minimap — dropped here,
// not portable to an arbitrary text row count. The mission-target and player-position
// cells were curses "reverse video" (red_background()/mvwputch_hi background swaps),
// not representable through cata_text_to_rml's foreground-only colour tags, so both
// are emitted as explicit inline background spans instead.
auto overmap_ui::overmap_chunk_rows( const avatar &you, const tripoint_abs_omt &global_omt,
                                     const int width, const int height ) -> std::vector<std::string>
{
    auto &player_character = get_avatar();
    const point_abs_omt curs = global_omt.xy();
    const auto custom_targ = player_character.get_custom_mission_target();
    const auto mission_targ = you.get_active_mission_target();
    const auto targ = custom_targ != overmap::invalid_tripoint ? custom_targ : mission_targ;
    const bool has_mission = targ != overmap::invalid_tripoint;

    std::vector<std::string> rows;
    rows.reserve( height );
    for( int j = -( height / 2 ); j <= height - ( height / 2 ) - 1; ++j ) {
        std::string row;
        for( int i = -( width / 2 ); i <= width - ( width / 2 ) - 1; ++i ) {
            const tripoint_abs_omt omp( curs + point( i, j ), g->get_levz() );
            nc_color ter_color;
            std::string ter_sym;
            const bool vehicle_here = ACTIVE_OVERMAP_BUFFER.has_vehicle( omp );
            if( ACTIVE_OVERMAP_BUFFER.has_note( omp ) ) {
                const std::string &note_text = ACTIVE_OVERMAP_BUFFER.note( omp );
                const auto note_info = overmap_ui::get_note_display_info( note_text );
                ter_color = std::get<1>( note_info );
                ter_sym = std::string( 1, std::get<0>( note_info ) );
            } else if( !ACTIVE_OVERMAP_BUFFER.seen( omp ) ) {
                ter_sym = " ";
                ter_color = c_black;
            } else if( vehicle_here ) {
                ter_color = c_cyan;
                ter_sym = "c";
            } else {
                const oter_id &cur_ter = ACTIVE_OVERMAP_BUFFER.ter( omp );
                ter_sym = cur_ter->get_symbol();
                ter_color = ACTIVE_OVERMAP_BUFFER.is_explored( omp ) ? c_dark_gray : cur_ter->get_color();
            }
            if( i == 0 && j == 0 ) {
                row += string_format(
                           R"(<span style="background-color:%s;color:#ffffffff;">%s</span>)",
                           nc_color_to_hex( ter_color ), rml_escape( ter_sym ) );
            } else if( has_mission && targ.xy() == omp.xy() ) {
                row += string_format(
                           R"(<span style="background-color:%s;">%s</span>)",
                           nc_color_to_hex( c_red ), cata_text_to_rml( colorize( ter_sym, ter_color ) ) );
            } else {
                row += cata_text_to_rml( colorize( ter_sym, ter_color ) );
            }
        }
        rows.push_back( std::move( row ) );
    }
    return rows;
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
    } else if( u.movement_mode_is( CMM_STEALTH ) ) {
        return c_cyan;
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
    } else if( u.movement_mode_is( CMM_STEALTH ) ) {
        return pgettext( "movement-type", "S" );
    } else if( u.movement_mode_is( CMM_CROUCH ) ) {
        return pgettext( "movement-type", "C" );
    } else {
        return pgettext( "movement-type", "W" );
    }
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
        // Registered unconditionally so non-coop builds hide the panel outright
        // instead of hitting resolve_widget_show_if's "unknown gate" fallback
        // (which would debugmsg-spam AND always show it).
        {
            "coop_panel", []() -> bool {
#ifdef COOP_ENABLED
                return coop_session::get().is_coop();
#else
                return false;
#endif
            }
        },
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
// Fixed-region Qud layout data model. Each region is a pre-rendered RML string
// filled by sidebar_hud_sync each turn. The RML document binds these directly
// via data-rml attributes — no layout iteration.
struct hud_rml_model {
    Rml::String topbar_rml;
    Rml::String topbar_row2_rml;
    Rml::String vitals_rml;
    Rml::String minimap_rml;
    Rml::String minimap_title;
    Rml::String log_rml;
    Rml::String log_title;
    Rml::String botbar_rml;
    Rml::String hotbar_rml;
    Rml::DataModelHandle handle;
};
std::unique_ptr<hud_rml_model> g_hud_data;
Rml::ElementDocument *g_hud_doc = nullptr;





// Temperature hue ladder for the top bar.
auto temp_color( units::temperature t ) -> nc_color
{
    const double f = units::to_fahrenheit( t );
    if( f < 32 ) {
        return c_light_blue;
    } else if( f < 50 ) {
        return c_cyan;
    } else if( f < 77 ) {
        return c_light_gray;
    } else if( f < 95 ) {
        return c_yellow;
    } else {
        return c_red;
    }
}

// Options struct for vbar_rml.
struct vbar_options {
    int cur = 0;
    int max = 0;
    nc_color fill = c_white;
    bool thin = false;
    bool allow_crit = true;
    bool use_gradient = false; // HP bars use green→yellow→red gradient
    std::string text;
    std::string id; // element id for animation targeting
};

// Chunky Qud-style HP bar RML (raw, not through cata_text_to_rml).
// Ticks at 25/50/75% quarter marks. Crit (<25%) triple-encoded: bright-red fill,
// dark-red trough, text suffix " !!".
auto vbar_rml( const vbar_options &o ) -> std::string
{
    const int pct = o.max > 0 ? std::clamp( o.cur * 100 / o.max, 0, 100 ) : 0;
    const bool crit = o.allow_crit && o.max > 0 && pct < 25;
    const std::string fill_hex = [&]() -> std::string {
        if( crit )
    {
        return "#e04040ff";
    }
    if( !o.use_gradient )
    {
        return nc_color_to_hex( o.fill );
        }
        // 3-stop HP gradient: green (#40c040) above 66%, yellow (#c0c040) at 33-66%,
        // red (#e05050) below 33%. Interpolate between stops for smooth transitions.
        unsigned char r = 0, g = 0, b = 0;
        if( pct >= 66 )
    {
        const float t = static_cast<float>( pct - 66 ) / 34.f;
            r = static_cast<unsigned char>( std::lerp( 0xc0, 0x40, t ) );
            g = static_cast<unsigned char>( std::lerp( 0xc0, 0xc0, t ) );
            b = static_cast<unsigned char>( std::lerp( 0x40, 0x40, t ) );
        } else if( pct >= 33 )
    {
        const float t = static_cast<float>( pct - 33 ) / 33.f;
            r = static_cast<unsigned char>( std::lerp( 0xe0, 0xc0, t ) );
            g = static_cast<unsigned char>( std::lerp( 0x50, 0xc0, t ) );
            b = static_cast<unsigned char>( std::lerp( 0x50, 0x40, t ) );
        } else
        {
            r = 0xe0;
            g = 0x50;
            b = 0x50;
        }
        return std::format( "#{:02x}{:02x}{:02x}ff", r, g, b );
    }();
    const std::string text_suffix = crit ? " !!" : "";
    const std::string thin_class = o.thin ? " thin" : "";
    const std::string crit_class = crit ? " crit" : "";

    return string_format(
               R"(<div id="%s" class="vbar%s%s"><div class="vbar-fill" style="width:%d%%;background-color:%s;"></div>)"
               R"(<div class="vbar-tick" style="left:25%%;"></div>)"
               R"(<div class="vbar-tick" style="left:50%%;"></div>)"
               R"(<div class="vbar-tick" style="left:75%%;"></div>)"
               R"(<div class="vbar-text">%s</div>)"
               R"(</div>)",
               o.id, thin_class, crit_class, pct, fill_hex,
               o.text + text_suffix );
}

// Qud vitals overlay: chunky HP bars per body part + thin STA/MANA bars.
auto hud_vitals( avatar &u ) -> std::string
{
    std::string out;
    for( const bodypart_id &bp : u.get_all_body_parts( true ) ) {
        const auto hp_cur = u.get_part_hp_cur( bp );
        const auto hp_max = u.get_part_hp_max( bp );
        const bool broken = u.is_limb_broken( bp.id() ) && !bp->essential;

        nc_color fill = c_white;
        bool allow_crit = true;
        std::string label;

        if( broken ) {
            // Mend bar: gray/blue, no crit
            const bool splinted = u.worn_with_flag( json_flag_SPLINT, bp ) ||
                                  ( u.mutation_value( "mending_modifier" ) >= 1.0f );
            fill = splinted ? c_blue : c_dark_gray;
            allow_crit = false;
            const int mend_pct = hp_max > 0 ? 100 * hp_cur / hp_max : 0;
            label = string_format( "%s %d%%", body_part_hp_bar_ui_text( bp.id() ), mend_pct );
        } else {
            const auto hp = get_hp_bar( hp_cur, hp_max );
            fill = hp.second;
            label = string_format( "%s %d/%d", body_part_hp_bar_ui_text( bp.id() ), hp_cur, hp_max );
        }

        const std::string label_hex = nc_color_to_hex( u.limb_color( bp.id(), true, true, true ) );
        const std::string bar_id = "vbar_" + bp.id().str();
        out += vbar_rml( { .cur = hp_cur, .max = hp_max, .fill = fill, .thin = false,
                           .allow_crit = allow_crit, .use_gradient = true, .text = label, .id = bar_id } );

        // Feed animation: HP percentage normalized 0-1, critical when <25%
        const double norm = hp_max > 0 ? static_cast<double>( hp_cur ) / hp_max : 0.0;
        hud_anim::feed( { .element_id = bar_id, .spec_icon = "hud_vbar",
                          .value = norm, .is_critical = allow_crit && norm < 0.25 } );

    }

    // Visual divider between body-part bars and resource bars.
    out += "<div class=\"vitals-divider\"></div>";
    // Stamina thin bar
    {
        const auto sta_cur = u.get_stamina();
        const auto sta_max = u.get_stamina_max();
        const auto hp = get_hp_bar( sta_cur, sta_max );
        out += vbar_rml( { .cur = sta_cur, .max = sta_max, .fill = hp.second, .thin = true,
                           .allow_crit = false, .text = string_format( "STA %d/%d", sta_cur, sta_max ),
                           .id = "vbar_sta" } );
        const double sta_norm = sta_max > 0 ? static_cast<double>( sta_cur ) / sta_max : 0.0;
        hud_anim::feed( { .element_id = "vbar_sta", .spec_icon = "hud_vbar",
                          .value = sta_norm, .is_critical = false } );
    }

    // Mana thin bar (only when magic is active)
    if( u.magic->max_mana( u ) > 0 ) {
        const auto mana_cur = u.magic->available_mana();
        const auto mana_max = u.magic->max_mana( u );
        out += vbar_rml( { .cur = mana_cur, .max = mana_max, .fill = mana_stat( u ).first, .thin = true,
                           .allow_crit = false, .text = string_format( "MANA %d/%d", mana_cur, mana_max ),
                           .id = "vbar_mana" } );
        const double mana_norm = static_cast<double>( mana_cur ) / mana_max;
        hud_anim::feed( { .element_id = "vbar_mana", .spec_icon = "hud_vbar",
                          .value = mana_norm, .is_critical = false } );
    }

    return out;
}


/// Rich animated log: consumes recent_messages_rich(100), emits per-message
/// RML rows with symbol + timestamp prefix, color attributes, and stable IDs
/// for the animation system to target. Newest message last (chronological).
auto hud_log( avatar & ) -> std::string
{
    std::string out;
    const auto msgs = Messages::recent_messages_rich( 100 );
    for( const Messages::rich_message &m : msgs ) {
        // Symbol based on message type
        const char *sym = "-";
        switch( m.type ) {
            case m_bad:
                sym = "!";
                break;
            case m_good:
                sym = "+";
                break;
            case m_warning:
                sym = "^";
                break;
            default:
                sym = "-";
                break;
        }

        const std::string hex_color = nc_color_to_hex( m.color );
        const std::string row_id = "log-" + std::to_string( m.seq );

        out += "<div class=\"hud-log-entry\" id=\"" + row_id + "\" style=\"color:" + hex_color + "\">";
        out += "<span class=\"hud-log-symbol\">" + std::string( sym ) + "</span>";
        out += "<span class=\"hud-log-time\">" + rml_escape( m.time ) + "</span>";
        out += "<span class=\"hud-log-text\">" + rml_escape( m.text ) + "</span>";
        out += "</div>";
    }
    return out;
}

} // namespace


namespace
{

// Map chunk: 11×11 colored-text overmap minichunk centred on the avatar.
// Square dimensions keep the avatar dead-centre and give a balanced view.
// Rows are joined with "\n" inside a white-space:pre div.
auto hud_map( avatar &u ) -> std::string
{
    const auto rows = overmap_ui::overmap_chunk_rows( u, u.abs_omt_pos(), 11, 11 );
    return "<div class=\"hud-map\">" + join( rows, "\n" ) + "</div>";
}

// Qud top bar row 1: identity + conditions.
auto hud_topbar( avatar &u ) -> std::string
{
    auto seg_id = colorize( u.get_name(), c_white );
    const units::temperature temp = get_weather().get_temperature( u.abs_pos() );
    seg_id += "  " + colorize( string_format( "T:%s", print_temperature( temp ) ),
                               temp_color( temp ) );

    auto seg_cond = std::string();
    const auto append_cond = [&]( const std::pair<std::string, nc_color> &p ) {
        if( !p.first.empty() ) {
            if( !seg_cond.empty() ) {
                seg_cond += "  ";
            }
            seg_cond += colorize( p.first, p.second );
        }
    };
    append_cond( u.get_hunger_description() );
    append_cond( u.get_thirst_description() );
    append_cond( u.get_fatigue_description() );
    append_cond( u.get_pain_description() );
    if( u.weight_carried() > u.weight_capacity() ) {
        if( !seg_cond.empty() ) {
            seg_cond += "  ";
        }
        seg_cond += colorize( _( "Overburdened" ), c_red );
    }

    std::string result = "<div class=\"hud-segment\"><span class=\"seg-label\">ID</span> "
                         + cata_text_to_rml( seg_id ) + "</div>"
                         + "<div class=\"hud-segment hud-seg-cond\"><span class=\"seg-label\">COND</span> "
                         + cata_text_to_rml( seg_cond ) + "</div>";

#ifdef COOP_ENABLED
    const auto &sess = coop_session::get();
    if( sess.is_coop() ) {
        auto seg_coop = colorize( sess.partner_name, c_cyan );
        // HP color: green > 66%, yellow > 33%, red <= 33%
        const nc_color hp_col = sess.partner_hp_pct > 66 ? c_green
                                : sess.partner_hp_pct > 33 ? c_yellow : c_red;
        seg_coop += " " + colorize( string_format( "HP:%d%%", sess.partner_hp_pct ), hp_col );
        // Stamina
        const nc_color sta_col = sess.partner_stamina_pct > 50 ? c_light_green : c_yellow;
        seg_coop += " " + colorize( string_format( "STA:%d%%", sess.partner_stamina_pct ), sta_col );
        // Activity
        if( !sess.partner_activity_str.empty() ) {
            seg_coop += " " + colorize( sess.partner_activity_str, c_light_blue );
        }
        // Ping
        if( sess.partner_ping_ms > 0 ) {
            const nc_color ping_col = sess.partner_ping_ms < 100 ? c_green
                                      : sess.partner_ping_ms < 300 ? c_yellow : c_red;
            seg_coop += " " + colorize( string_format( "%dms", sess.partner_ping_ms ), ping_col );
        }
        // Direction arrow to partner when offscreen
        const auto partner_delta = sess.partner_abs_pos - u.abs_pos();
        const int dx = partner_delta.x();
        const int dy = partner_delta.y();
        if( std::abs( dx ) > 30 || std::abs( dy ) > 15 ) {
            // Offscreen — show compass arrow
            const char *arrow = "?";
            if( std::abs( dx ) > std::abs( dy ) * 2 ) {
                arrow = dx > 0 ? "→" : "←";
            } else if( std::abs( dy ) > std::abs( dx ) * 2 ) {
                arrow = dy > 0 ? "↓" : "↑";
            } else if( dx > 0 ) {
                arrow = dy > 0 ? "↘" : "↗";
            } else {
                arrow = dy > 0 ? "↙" : "↖";
            }
            seg_coop += " " + colorize( arrow, c_white );
        }
        // Reconnecting indicator
        if( sess.is_host() ) {
            if( g->coop_server_ && g->coop_server_->awaiting_reconnect() ) {
                seg_coop += " " + colorize( _( "[RECONNECTING]" ), c_yellow );
            }
        }
        // Build RML
        result += "<div class=\"hud-segment\"><span class=\"seg-label\">CO-OP</span> "
                  + cata_text_to_rml( seg_coop ) + "</div>";
    }
#endif

    return result;
}

// Qud top bar row 2: stats + time/place.
auto hud_topbar_row2( avatar &u ) -> std::string
{
    auto seg_stats = colorize( "STR:", c_light_gray ) +
                     colorize( std::to_string( u.get_str() ), str_string( u ).first );
    seg_stats += " " + colorize( "DEX:", c_light_gray ) +
                 colorize( std::to_string( u.get_dex() ), dex_string( u ).first );
    seg_stats += " " + colorize( "INT:", c_light_gray ) +
                 colorize( std::to_string( u.get_int() ), int_string( u ).first );
    seg_stats += " " + colorize( "PER:", c_light_gray ) +
                 colorize( std::to_string( u.get_per() ), per_string( u ).first );
    seg_stats += "  " + colorize( "SPD:", c_light_gray ) +
                 colorize( std::to_string( u.get_speed() ), value_color( u.get_speed() ) );
    seg_stats += "  " + colorize( "FOC:", c_light_gray ) +
                 colorize( std::to_string( u.focus_pool ), focus_color( u.focus_pool ) );

    auto seg_time = std::string();
    if( u.has_watch() ) {
        seg_time += colorize( to_string_time_of_day( calendar::turn ), c_light_gray );
    } else if( g->get_levz() >= 0 ) {
        seg_time += colorize( time_approx(), c_light_gray );
    } else {
        seg_time += colorize( "???", c_light_gray );
    }
    seg_time += string_format( ", day %d of %s",
                               day_of_season<int>( calendar::turn ) + 1,
                               calendar::name_season( season_of_year( calendar::turn ) ) );
    seg_time += " :: " +
                colorize( ACTIVE_OVERMAP_BUFFER.ter( u.abs_omt_pos() )->get_name(), c_white );
    if( g->get_levz() < 0 ) {
        seg_time += colorize( string_format( " %dd deep", -g->get_levz() ), c_dark_gray );
    }

    return "<div class=\"hud-segment\"><span class=\"seg-label\">STAT</span> "
           + cata_text_to_rml( seg_stats ) + "</div>"
           + "<div class=\"hud-segment\"><span class=\"seg-label\">TIME</span> "
           + cata_text_to_rml( seg_time ) + "</div>";
}


// Qud bottom bar: EFFECTS (left) + TARGET with inline HP bar + weapon + SAFE/HOSTILES (right).
auto hud_botbar( avatar &u ) -> std::string
{
    // --- Left: EFFECTS list ---
    const auto effects = character_display::effect_name_and_text( u );
    auto left = std::string( _( "EFFECTS:" ) ) + " ";
    auto effects_rml = std::string(); // raw RML appended after cata_text_to_rml(left)
    if( effects.empty() ) {
        left += colorize( _( "none" ), c_dark_gray );
    } else {
        constexpr size_t max_shown = 8;
        // Build raw RML directly — inner <span id="status-*"> wrappers for
        // Phase 7 animation targets would be escaped by colorize/cata_text_to_rml.
        const auto gray_hex = nc_color_to_hex( c_light_gray );
        auto joined = std::string();
        auto first = true;
        for( const auto &effect : effects | std::views::take( max_shown ) ) {
            if( !first ) {
                joined += " :: ";
            }
            first = false;
            std::string spec_key;
            if( effect.first.find( _( "Poison" ) ) != std::string::npos ) {
                spec_key = "poison";
            } else if( effect.first.find( _( "On Fire" ) ) != std::string::npos
                       || effect.first.find( _( "Burning" ) ) != std::string::npos ) {
                spec_key = "fire";
            } else if( effect.first.find( _( "Bleeding" ) ) != std::string::npos ) {
                spec_key = "bleed";
            } else if( effect.first.find( _( "Irradiated" ) ) != std::string::npos
                       || effect.first.find( _( "Radiation" ) ) != std::string::npos ) {
                spec_key = "rad";
            }
            if( !spec_key.empty() ) {
                joined += "<span id=\"status-" + spec_key + "\">"
                          + rml_escape( effect.first ) + "</span>";
            } else {
                joined += rml_escape( effect.first );
            }
        }
        if( effects.size() > max_shown ) {
            joined += rml_escape(
                          string_format( " (+%d)", static_cast<int>( effects.size() - max_shown ) ) );
        }
        effects_rml = "<span style=\"color:" + gray_hex + ";\">" + joined + "</span>";
    }
    // Feed status effect animations (Phase 7): map effect names to animation specs.
    {
        const auto feed_status_anim = [&]( const std::string & spec ) {
            hud_anim::feed( { .element_id = "status-" + spec, .spec_icon = "status_" + spec,
                              .value = 1.0, .is_critical = false } );
        };
        for( const auto &[nm, desc] : effects ) {
            if( nm.find( _( "Poison" ) ) != std::string::npos ) {
                feed_status_anim( "poison" );
            }
            if( nm.find( _( "On Fire" ) ) != std::string::npos ||
                nm.find( _( "Burning" ) ) != std::string::npos ) {
                feed_status_anim( "fire" );
            }
            if( nm.find( _( "Bleeding" ) ) != std::string::npos ) {
                feed_status_anim( "bleed" );
            }
            if( nm.find( _( "Radiation" ) ) != std::string::npos ||
                nm.find( _( "Irradiated" ) ) != std::string::npos ) {
                feed_status_anim( "rad" );
            }
        }
    }

    // --- Middle: TARGET with inline HP bar ---
    auto middle_text = std::string();
    auto middle_rml = std::string();
    const auto target_ptr = u.last_target.lock();
    if( const Creature *t = target_ptr.get() ) {
        const std::string disp_name = t->disp_name();
        const auto &att = Creature::get_attitude_ui_data( t->attitude_to( u ) );
        const int t_hp = t->get_hp();
        const int t_hp_max = std::max( t->get_hp_max(), 1 );
        const int pct = std::clamp( t_hp * 100 / t_hp_max, 0, 100 );
        const std::string hp_hex = nc_color_to_hex( get_hp_bar( t_hp, t_hp_max ).second );

        middle_text += colorize( "TARGET:", c_light_gray ) + " " +
                       colorize( disp_name, t->basic_symbol_color() ) + " [" +
                       colorize( att.first.translated(), att.second ) + "] ";
        // Inline HP bar: raw RML kept separate from colorize() output
        middle_rml += string_format(
                          R"(<span class="tbar"><span class="tbar-fill" style="width:%d%%;background-color:%s;"></span></span>)",
                          pct, hp_hex );
        middle_rml += " " + std::to_string( t_hp );
    }

    // --- Right: weapon + SAFE + HOSTILES ---
    auto right = std::string();
    right += colorize( character_funcs::fmt_wielded_weapon( u ), c_light_gray ) + "  ";
    right += colorize( move_mode_string( u ), move_mode_color( u ) ) + "  ";
    const auto hostiles = u.get_mon_visible().nearby_hostile_count;
    right += colorize( _( "SAFE" ), safe_color() ) + ": " +
             colorize( std::to_string( hostiles ),
                       hostiles > 0 ? c_red : c_dark_gray );

    // Assemble: left span (via cata_text_to_rml) + middle text (via cata_text_to_rml) +
    // middle raw RML (tbar) + right span (via cata_text_to_rml)
    std::string out = "<span class=\"strip-left\">" + cata_text_to_rml( left ) + effects_rml +
                      "</span>";
    if( !middle_text.empty() ) {
        out += " " + cata_text_to_rml( middle_text ) + " " + middle_rml;
    }
    out += "<span class=\"strip-right\">" + cata_text_to_rml( right ) + "</span>";
    return out;
}

// Qud ability-bar hotbar: real CBN keybinds, one slot per bound action.
auto hud_hotbar( avatar & ) -> std::string
{
    constexpr std::array<action_id, 9> acts = {
        ACTION_FIRE, ACTION_RELOAD_WIELDED, ACTION_TOGGLE_RUN,
        ACTION_TOGGLE_CROUCH, ACTION_WAIT, ACTION_PICKUP,
        ACTION_CRAFT, ACTION_INVENTORY, ACTION_MAP,
    };
    constexpr std::array<const char *, 9> labels = {
        "Fire", "Reload", "Run", "Crouch", "Wait", "Pick Up", "Craft", "Inventory", "Map",
    };

    input_context ctxt = get_default_mode_input_context();
    std::string out;
    for( int i = 0; i < 9; ++i ) {
        const std::string key = ctxt.get_desc( action_ident( acts[i] ), true );
        if( key.empty() ) {
            continue;
        }
        if( !out.empty() ) {
            out += " ";
        }
        out += "[" + colorize( key, c_yellow ) + "] " +
               colorize( std::string( " " ) + _( labels[i] ) + " ", c_light_gray );
    }
    return out;
}

} // namespace

// Sticky autoscroll: true = snap to bottom on new messages.
static bool g_hud_log_sticky = true;
// Previous log window seq range for pruning stale animation keys.
static std::pair<unsigned, unsigned> g_hud_log_prev_seq = { 0, 0 };


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
    // Fixed-region model: bind each string directly.
    g_hud_data = std::make_unique<hud_rml_model>();
    c.Bind( "topbar_rml", &g_hud_data->topbar_rml );
    c.Bind( "topbar_row2_rml", &g_hud_data->topbar_row2_rml );
    c.Bind( "vitals_rml", &g_hud_data->vitals_rml );
    c.Bind( "minimap_rml", &g_hud_data->minimap_rml );
    c.Bind( "minimap_title", &g_hud_data->minimap_title );
    c.Bind( "log_rml", &g_hud_data->log_rml );
    c.Bind( "log_title", &g_hud_data->log_title );
    c.Bind( "botbar_rml", &g_hud_data->botbar_rml );
    c.Bind( "hotbar_rml", &g_hud_data->hotbar_rml );
    g_hud_data->handle = c.GetModelHandle();
    // Static dock headers — set once, no DirtyVariable needed.
    g_hud_data->minimap_title = Rml::String( to_upper_case( _( "Minimap" ) ) );
    g_hud_data->log_title = Rml::String( to_upper_case( _( "Log" ) ) );
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
    // Opening the HUD claims the top/bottom chrome strips carved out of the terrain
    // viewport (sidebar_hud_top_rows/_bottom_rows) — apply the new viewport size.
    g->mark_main_ui_adaptor_resize();
}

// Position all HUD regions absolutely. Called every sync so it tracks resize.
// Strips span the FULL viewport width (Qud's do), no longer map-width only.
static void sidebar_hud_apply_rect()
{
    if( g_hud_doc == nullptr || TERMX <= 0 || TERMY <= 0 ) {
        return;
    }

    // Dock width — needed first so topbar/botbar/hotbar can be narrowed.
    const auto &layout = panel_manager::get_manager().get_current_layout();
    const bool sidebar_right = get_option<std::string>( "SIDEBAR_POSITION" ) == "right";
    float dock_width_pct = 0.0f;
    if( layout.begin() != layout.end() ) {
        dock_width_pct = 100.0f * layout.begin()->get_width() / TERMX;
    }
    const float bar_width_pct = 100.0f - dock_width_pct;
    const std::string bar_left = sidebar_right
                                 ? "0%"
                                 : string_format( "%.4f%%", dock_width_pct );

    const float top_rows_pct = 100.0f * sidebar_hud_top_rows() / TERMY;
    // Top bar: auto height, narrowed to avoid dock.
    if( Rml::Element *el = g_hud_doc->GetElementById( "hud-topbar" ) ) {
        el->SetProperty( "left", bar_left );
        el->SetProperty( "top", "0%" );
        el->SetProperty( "width", string_format( "%.4f%%", bar_width_pct ) );
        el->SetProperty( "height", "auto" );
    }

    // Bottom rows: botbar + hotbar, narrowed to avoid dock.
    const float bottom_rows_pct = 100.0f * sidebar_hud_bottom_rows() / TERMY;
    const float half_bottom_pct = bottom_rows_pct / 2.0f;
    if( Rml::Element *el = g_hud_doc->GetElementById( "hud-botbar" ) ) {
        el->SetProperty( "left", bar_left );
        el->SetProperty( "top", string_format( "%.4f%%", 100.0f - bottom_rows_pct ) );
        el->SetProperty( "width", string_format( "%.4f%%", bar_width_pct ) );
        el->SetProperty( "height", "auto" );
    }
    if( Rml::Element *el = g_hud_doc->GetElementById( "hud-hotbar" ) ) {
        el->SetProperty( "left", bar_left );
        el->SetProperty( "top", string_format( "%.4f%%", 100.0f - half_bottom_pct ) );
        el->SetProperty( "width", string_format( "%.4f%%", bar_width_pct ) );
        el->SetProperty( "height", "auto" );
    }

    // Vitals overlay: top-left of the viewport area.
    const int width_left = panel_manager::get_manager().get_width_left();
    const int width_right = panel_manager::get_manager().get_width_right();
    const float width_left_pct = 100.0f * width_left / TERMX;
    const float width_right_pct = 100.0f * width_right / TERMX;
    if( Rml::Element *el = g_hud_doc->GetElementById( "hud-vitals" ) ) {
        el->SetProperty( "left", string_format( "%.4f%%", width_left_pct ) );
        el->SetProperty( "top", string_format( "%.4f%%", top_rows_pct + 1.0f ) );
    }

    // Dock: full height minus bars, pinned to sidebar edge.
    if( layout.begin() != layout.end() ) {
        const float dock_left_pct = sidebar_right
                                    ? 100.0f - width_right_pct
                                    : 0.0f;
        if( Rml::Element *el = g_hud_doc->GetElementById( "hud-dock" ) ) {
            el->SetProperty( "left", string_format( "%.4f%%", dock_left_pct ) );
            el->SetProperty( "top", string_format( "%.4f%%", top_rows_pct ) );
            el->SetProperty( "width", string_format( "%.4f%%", dock_width_pct ) );
            el->SetProperty( "height", string_format( "%.4f%%", 100.0 - top_rows_pct - bottom_rows_pct ) );
        }
    }
}

void sidebar_hud_sync( avatar &u )
{
    if( g_hud_doc == nullptr || !g_hud_data ) {
        return;
    }
    // Fixed-region Qud layout: fill each region string from its producer,
    // dirty the variable, and reposition rects.
    g_hud_data->topbar_rml = hud_topbar( u );
    g_hud_data->handle.DirtyVariable( "topbar_rml" );
    g_hud_data->topbar_row2_rml = hud_topbar_row2( u );
    g_hud_data->handle.DirtyVariable( "topbar_row2_rml" );

    g_hud_data->vitals_rml = hud_vitals( u );
    g_hud_data->handle.DirtyVariable( "vitals_rml" );

    g_hud_data->minimap_rml = hud_map( u );
    // Detect HP decrease for screen shake (Phase 4).
    {
        int total_hp = 0;
        for( const bodypart_id &bp : u.get_all_body_parts( true ) ) {
            total_hp += u.get_part_hp_cur( bp );
        }
        static int prev_total_hp = -1;
        if( prev_total_hp >= 0 && total_hp < prev_total_hp ) {
            const int dmg = prev_total_hp - total_hp;
            const int max_hp = u.get_hp_max();
            const float intensity = std::clamp( static_cast<float>( dmg ) / max_hp, 0.0f, 1.0f );
            hud_shake::trigger( intensity );
            hud_anim::feed( { .element_id = "hud-vignette", .spec_icon = "hud_vignette",
                              .value = intensity, .is_critical = false } );
        }
        prev_total_hp = total_hp;
    }
    // Environmental HUD tinting (Phase 6): apply CSS classes based on conditions.
    {
        const auto apply_env_classes = [&]( const char *id ) {
            Rml::Element *el = g_hud_doc->GetElementById( id );
            if( el == nullptr ) {
                return;
            }
            // Night: desaturate HUD (21:00 - 06:00)
            const int hour = hour_of_day<int>( calendar::turn );
            const bool is_night = hour >= 21 || hour < 6;
            el->SetClass( "env-night", is_night );

            // Radiation: green tint
            const bool irradiated = u.get_rad() > 0.0f;
            el->SetClass( "env-rad", irradiated );

            // Cold: blue tint (< 0°C)
            const units::temperature temp = get_weather().get_temperature( u.abs_pos() );
            const bool cold = units::to_celsius( temp ) < 0.0f;
            el->SetClass( "env-cold", cold );

            // Fire proximity: warm tint (radius 3 matches warmth radius)
            const bool near_fire = get_map().has_nearby_fire( u.bub_pos(), 3 );
            el->SetClass( "env-fire", near_fire );

            // Storm: shake/vignette modulation
            const auto &wid = get_weather().weather_id;
            const bool is_storm = wid == weather_type_id( "thunder" )
                                  || wid == weather_type_id( "lightning" );
            el->SetClass( "env-storm", is_storm );
        };
        apply_env_classes( "hud-topbar" );
        apply_env_classes( "hud-botbar" );
        apply_env_classes( "hud-dock" );
        apply_env_classes( "hud-vitals" );
    }
    g_hud_data->handle.DirtyVariable( "minimap_rml" );

    // Sticky autoscroll: check if user is at bottom BEFORE rebuilding content.
    {
        Rml::Element* lb = g_hud_doc->GetElementById( "hud-log-body" );
        if( lb != nullptr ) {
            const float st = lb->GetScrollTop();
            const float ch = lb->GetClientHeight();
            const float sh = lb->GetScrollHeight();
            g_hud_log_sticky = ( st + ch >= sh - 4.0f );
        }
    }

    // Feed log row animations and prune stale keys.
    const auto prev_log_seq = g_hud_log_prev_seq;
    {
        const auto msgs = Messages::recent_messages_rich( 100 );
        unsigned cur_min = 0, cur_max = 0;
        if( !msgs.empty() ) {
            cur_min = msgs.front().seq;
            cur_max = msgs.back().seq;
        }
        // Forget keys that dropped out of the window.
        if( g_hud_log_prev_seq.first > 0 ) {
            for( unsigned s = g_hud_log_prev_seq.first; s < g_hud_log_prev_seq.second; ++s ) {
                if( s < cur_min || s > cur_max ) {
                    hud_anim::forget( "log-" + std::to_string( s ) );
                }
            }
        }
        // Feed current rows.
        for( const Messages::rich_message &m : msgs ) {
            hud_anim::feed( { .element_id = "log-" + std::to_string( m.seq ), .spec_icon = "hud_log_entry" } );
        }
        g_hud_log_prev_seq = { cur_min, cur_max };
    }

    if( g_hud_log_prev_seq != prev_log_seq ) {
        g_hud_data->log_rml = hud_log( u );
        g_hud_data->handle.DirtyVariable( "log_rml" );
    }

    // Snap scroll to bottom if sticky.
    if( g_hud_log_sticky && g_hud_doc != nullptr ) {
        if( Rml::Element * lb = g_hud_doc->GetElementById( "hud-log-body" ) ) {
            lb->SetScrollTop( lb->GetScrollHeight() );
        }
    }

    g_hud_data->botbar_rml = hud_botbar( u );
    g_hud_data->handle.DirtyVariable( "botbar_rml" );

    g_hud_data->hotbar_rml = hud_hotbar( u );
    g_hud_data->handle.DirtyVariable( "hotbar_rml" );

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
    hud_anim::clear();
    g_hud_data.reset();
    // Closing the HUD releases the carved top/bottom chrome strips — the terrain
    // viewport must reclaim the full height.
    g->mark_main_ui_adaptor_resize();
}

bool sidebar_hud_active()
{
    // True iff the HUD doc is open → game::draw_panels suppresses the WHOLE curses
    // sidebar (the column owns the entire region). Replaces the per-panel owns_panel gate.
    return g_hud_doc != nullptr;
}
int sidebar_hud_top_rows()
{
    return sidebar_hud_rmlui_enabled() && rmlui_layer::ready() ? 3 : 0;
}

int sidebar_hud_bottom_rows()
{
    return sidebar_hud_rmlui_enabled() && rmlui_layer::ready() ? 4 : 0;
}

auto sidebar_hud_anim_tick() -> void
{
    if( g_hud_doc == nullptr ) {
        return;
    }

    // Decay shake before sampling.
    static std::uint32_t last_ms = 0;
    const std::uint32_t now = sidebar_anim::now_ms();
    if( last_ms > 0 ) {
        const float dt = std::max( 0.0f, static_cast<float>( now - last_ms ) ) / 1000.0f;
        hud_shake::tick( dt );
    }
    last_ms = now;

    hud_anim::tick( g_hud_doc, now );

    // Screen shake (Phase 4): apply margin offsets to HUD containers.
    const auto offset = hud_shake::sample();
    const bool shaking = ( offset.dx != 0.0f || offset.dy != 0.0f );
    const auto apply_shake = [&]( const char *id ) {
        Rml::Element *el = g_hud_doc->GetElementById( id );
        if( el == nullptr ) {
            return;
        }
        if( shaking ) {
            el->SetProperty( "margin-left", std::format( "{:.1f}px", offset.dx ) );
            el->SetProperty( "margin-top", std::format( "{:.1f}px", offset.dy ) );
        } else {
            el->RemoveProperty( "margin-left" );
            el->RemoveProperty( "margin-top" );
        }
    };
    apply_shake( "hud-topbar" );
    apply_shake( "hud-botbar" );
    apply_shake( "hud-dock" );
    apply_shake( "hud-vitals" );
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

// Data-driven value widget: RmlUi hud_produce reads the widget's _var (via
// get_var_value) and colors it exactly as the curses draw_* panels did — label in
// c_light_gray, value bar/number in the value's color. No curses draw (Tier-10
// rip-out); icons are a Phase-4 SVG concern, not this MVP text row.
window_panel make_value_widget_panel( const widget &w, int width )
{
    const widget_id id = w.getId();
    const auto hud_produce = [id]( avatar & u ) -> std::string {
        if( !id.is_valid() )
    {
        return "";
    }
    const widget &wd = *id; // static widget data; u carries the live state
    const auto val = wd.get_var_value( u );
    const auto val_color = value_widget_color( wd, val, u );
    const auto label = wd.label().translated();
    // Right-hand readout: a fill bar + percent for bounded vars (those with a
    // max), else the raw number. Reuses get_hp_bar's 5-cell bar string so it
    // reads like the native HP/stamina panels.
    const auto vmax = value_var_max( wd.var(), u );
    auto rhs = std::string();
    if( vmax && *vmax > 0 )
    {
        // Clamp the percent to match get_hp_bar's clamped bar — val can exceed
        // max (e.g. buffs) and would otherwise print ">100%" beside a full bar.
        const auto pct = std::clamp( 100 * val / *vmax, 0, 100 );
            rhs = get_hp_bar( val, *vmax ).first + string_format( " %d%%", pct );
        } else
        {
            rhs = string_format( "%d", val );
        }
        return colorize( label, c_light_gray ) + "  " + colorize( rhs, val_color );
    };
    const int panel_width = std::max( 1, width > 0 ? width : w.width() );
    const bool default_toggle = !w.has_flag( "W_DISABLED_BY_DEFAULT" );
    window_panel wp( {}, value_widget_name( id ), w.height(), panel_width,
                     default_toggle, resolve_widget_show_if( w ) );
    wp.hud_produce = hud_produce;
    return wp;
}

// Per-body-part color for a body-graph dimension. The renderer-agnostic core:
// each body_graph* var picks a different per-bp value to color by, mirroring
// CDDA's display::get_bodygraph_bp_color but against BN's getters.
static auto bodygraph_bp_color( const avatar &u, const bodypart_id &bp, widget_var dim ) -> nc_color
{
    switch( dim ) {
    case widget_var::body_graph:
        return u.limb_color( bp.id(), true, true, true );
        case widget_var::body_graph_temp: {
            const auto temp_conv = u.get_part_temp_cur( bp );
            if( temp_conv > BODYTEMP_SCORCHING ) {
                return c_red;
            } else if( temp_conv > BODYTEMP_VERY_HOT ) {
                return c_light_red;
            } else if( temp_conv > BODYTEMP_HOT ) {
                return c_yellow;
            } else if( temp_conv > BODYTEMP_COLD ) {
                return c_green;
            } else if( temp_conv > BODYTEMP_VERY_COLD ) {
                return c_light_blue;
            } else if( temp_conv > BODYTEMP_FREEZING ) {
                return c_cyan;
            }
            return c_blue;
        }
        case widget_var::body_graph_encumb: {
            const auto enc = u.encumb( bp.id() );
            return enc < 10 ? c_light_gray : enc < 40 ? c_yellow : c_light_red;
        }
        case widget_var::body_graph_status:
            if( u.has_effect( effect_bleed, bp.id() ) ) {
                return c_red;
            } else if( u.has_effect( effect_bite, bp.id() ) ) {
                return c_yellow;
            } else if( u.has_effect( effect_infected, bp.id() ) ) {
                return c_green;
            } else if( u.worn_with_flag( json_flag_SPLINT, bp ) ) {
                return c_blue;
            }
            return c_dark_gray;
        case widget_var::body_graph_wet:
            return u.get_part( bp ).get_wetness() > 0 ? c_light_blue : c_dark_gray;
        default:
            return c_white;
    }
}

// Body-graph value widget: one row per main body part (the hud_limbs iteration
// pattern), label colored by the widget's body_graph* dimension via
// bodygraph_bp_color(). Color encodes the data; no HP bar, to stay distinct from
// the native Limbs panel.
window_panel make_bodygraph_widget_panel( const widget &w, int width )
{
    const widget_var dim = w.var();
    const auto hud_produce = [dim]( avatar & u ) -> std::string {
        std::string out;
        bool first = true;
        for( const bodypart_id &bp : u.get_all_body_parts( true ) )
        {
            if( !first ) {
                out += "\n";
            }
            first = false;
            const auto name = left_justify( body_part_hp_bar_ui_text( bp ), 5 );
            out += colorize( name, bodygraph_bp_color( u, bp, dim ) );
        }
        return out;
    };
    const int panel_width = std::max( 1, width > 0 ? width : w.width() );
    const bool default_toggle = !w.has_flag( "W_DISABLED_BY_DEFAULT" );
    window_panel wp( {}, value_widget_name( w.getId() ), w.height(), panel_width,
                     default_toggle, resolve_widget_show_if( w ) );
    wp.hud_produce = hud_produce;
    return wp;
}

// Build a window_panel for any widget, dispatching on style. Unknown styles fall
// back to the native bridge.
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
