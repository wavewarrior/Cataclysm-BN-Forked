#include "overmap_ui.h"

#include "activity_actor_definitions.h"
#include "all_enum_values.h"
#include "avatar.h"
#include "cached_options.h"
#include "calendar.h"
#include "cata_tiles.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "clzones.h"
#include "color.h"
#include "coordinates.h"
#include "cursesdef.h"
#include "cursesport.h"
#include "distribution_grid.h"
#include "enums.h"
#include "game.h"
#include "game_constants.h"
#include "game_ui.h"
#include "hash_utils.h"
#include "ime.h"
#include "input.h"
#include "int_id.h"
#include "lighting/rmlui_layer.h"
#include "line.h"
#include "map.h"
#include "map_iterator.h"
#include "mapbuffer.h"
#include "messages.h"
#include "mission.h"
#include "mongroup.h"
#include "note_label_utils.h"
#include "npc.h"
#include "omdata.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmap_label.h"
#include "overmap_label_note.h"
#include "overmap_special.h"
#include "overmap_types.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "player_activity.h"
#include "regional_settings.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "rng.h"
#include "sdltiles.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "translations.h"
#include "type_id.h"
#include "ui.h"
#include "ui_manager.h"
#include "uistate.h"
#include "units.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_gen.h"
#include "world_type.h"
#ifdef COOP_ENABLED
#include "coop_client.h"
#include "coop_proto.h"
#include "coop_server.h"
#include "coop_session.h"
#include "json.h"
#include <sstream>
#endif

#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

static const activity_id ACT_TRAVELLING( "ACT_TRAVELLING" );

static const mongroup_id GROUP_FOREST( "GROUP_FOREST" );
static const mongroup_id GROUP_NEMESIS( "GROUP_NEMESIS" );

static const trait_id trait_DEBUG_NIGHTVISION( "DEBUG_NIGHTVISION" );

static constexpr int UILIST_MAP_NOTE_DELETED = -2047;
static constexpr int UILIST_MAP_NOTE_EDITED = -2048;
static constexpr int UILIST_CHANGE_SORT = -2049;

static constexpr int max_note_length = 450;
static constexpr int max_note_display_length = 45;

/** Note preview map width without borders. Odd number. */
static const int npm_width = 3;
/** Note preview map height without borders. Odd number. */
static const int npm_height = 3;

// Tier 6: the overmap legend sidebar RmlUi render path (the text panel beside the
// overmap tile grid). Render-only doc; the overmap tile view itself stays on its
// GPU/ASCII map path. Keyboard owns all navigation.
bool &overmap_rmlui_enabled()
{
    static bool enabled = true;
    return enabled;
}

namespace overmap_ui
{
// persistent data for distribution grid debug drawing
struct grids_draw_data {
    public:
        std::optional<char> get_active( const tripoint_abs_omt& omp ) {
            // TODO: fix point types
            uintptr_t id = get_distribution_grid_tracker().debug_grid_id( omp );
            if( id == 0 ) { return std::nullopt; }

            auto it = list_active.find( id );
            if( it != list_active.end() ) { return it->second; }

            auto ch = pick_char( [this]( char c ) -> bool {
            for( const auto& it : list_active ) {
                if( it.second == c ) { return false; }
                }
                return true;
            } );

            char c = ch.has_value() ? *ch : '?';
            list_active.insert( std::make_pair( id, c ) );
            return c;
        }

        std::optional<char> get_inactive( const tripoint_abs_omt& omp ) {
            std::set<tripoint_abs_omt> grid = ACTIVE_OVERMAP_BUFFER.electric_grid_at( omp );
            if( grid.size() <= 1 ) { return std::nullopt; }
            std::vector<tripoint_abs_omt> sorted( grid.begin(), grid.end() );
            std::sort( sorted.begin(), sorted.end() );

            std::size_t id = cata::range_hash{}( sorted );

            auto it = list_inactive.find( id );
            if( it != list_inactive.end() ) { return it->second.second; }

            // There may be a lot of grids visible at the same time.
            // We have no choice but to allow repeating symbols,
            // but also have to make sure neighbouring grids don't receive same ones.
            auto ch = pick_char( [omp, this]( char c ) {
                for( const auto& it : list_inactive ) {
                    if( it.second.second != c ) { continue; }
                    for( const tripoint_abs_omt& p : it.second.first ) {
                        tripoint_rel_omt delta = p - omp;
                        if( abs( delta.x() ) < 5 && abs( delta.y() ) < 5 && abs( delta.z() ) < 5 ) {
                            return false;
                        }
                    }
                }
                return true;
            } );

            char c = ch.has_value() ? *ch : '?';
            list_inactive.insert( std::make_pair( id, std::make_pair( sorted, c ) ) );
            return c;
        }

    private:
        // Fn(char) -> bool
        template <typename Fn> std::optional<char> pick_char( Fn filter_func ) {
            static std::string candidates( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" );
            for( char c : candidates ) {
                if( filter_func( c ) ) { return c; }
            }
            return std::nullopt;
        }

        std::unordered_map<std::uintptr_t, char> list_active;
        std::unordered_map<std::size_t, std::pair<std::vector<tripoint_abs_omt>, char>> list_inactive;
};

auto fmt_omt_coords( const tripoint_abs_omt& coord ) -> std::string
{
    if( get_option<std::string>( "OVERMAP_COORDINATE_FORMAT" ) == "subdivided" ) {
        point_abs_om abs_coord;
        tripoint_om_omt rel_coord;
        std::tie( abs_coord, rel_coord ) = project_remain<coords::om>( coord );

        return string_format(
                   "%d'%d, %d'%d", abs_coord.x(), rel_coord.x(), abs_coord.y(), rel_coord.y() );
    } else {
        return string_format( "%d, %d", coord.x(), coord.y() );
    }
}

static void create_note( const tripoint_abs_omt& curs );

struct note_display_info {
    char symbol = 'N';
    nc_color color = c_yellow;
    size_t text_offset = 0;
    std::optional<std::string> sprite_id;
};

static note_display_info get_note_display_info_full( const std::string& note )
{
    note_display_info result;
    bool set_color = false;
    bool set_symbol = false;
    bool set_sprite = false;

    size_t pos = 0;
    for( int i = 0; i < 4; ++i ) {
        // find the first non-whitespace non-delimiter
        pos = note.find_first_not_of( " :;", pos, 3 );
        if( pos == std::string::npos ) { return result; }

        // find the first following delimiter
        const auto end = note.find_first_of( " :;", pos, 3 );
        if( end == std::string::npos ) { return result; }

        // set color or symbol
        const char delimiter = note[end];
        const std::string token = note.substr( pos, end - pos );
        if( !set_sprite && delimiter == ':' && token == "SPRITE" ) {
            size_t sprite_start = end + 1;
            if( sprite_start >= note.size() ) {
                result.text_offset = sprite_start;
                return result;
            }
            size_t sprite_end = note.find_first_of( " :;", sprite_start, 3 );
            if( sprite_end == std::string::npos ) { sprite_end = note.size(); }
            std::string sprite_id = note.substr( sprite_start, sprite_end - sprite_start );
            if( !sprite_id.empty() ) {
                result.sprite_id = sprite_id;
                set_sprite = true;
            }
            if( sprite_end >= note.size() ) {
                result.text_offset = sprite_end;
                return result;
            }
            pos = sprite_end + 1;
            result.text_offset = pos;
            continue;
        } else if( !set_symbol && delimiter == ':' && token != "SPRITE" ) {
            result.symbol = note[end - 1];
            result.text_offset = end + 1;
            set_symbol = true;
        } else if( !set_color && delimiter == ';' ) {
            result.color = get_note_color( note.substr( pos, end - pos ) );
            result.text_offset = end + 1;
            set_color = true;
        } else if( !set_color && !set_symbol && !set_sprite ) {
            return result;
        } else {
            return result;
        }

        pos = end + 1;
    }

    return result;
}

// {note symbol, note color, offset to text}
std::tuple<char, nc_color, size_t> get_note_display_info( const std::string& note )
{
    const note_display_info result = get_note_display_info_full( note );
    return std::make_tuple( result.symbol, result.color, result.text_offset );
}

std::optional<std::string> get_note_sprite_id( const std::string& note )
{
    return get_note_display_info_full( note ).sprite_id;
}

static std::array<std::pair<nc_color, std::string>, npm_width * npm_height> get_overmap_neighbors(
    const tripoint_abs_omt& current )
{
    const bool has_debug_vision = get_player_character().has_trait( trait_DEBUG_NIGHTVISION );

    std::array<std::pair<nc_color, std::string>, npm_width * npm_height> map_around;
    int index = 0;
    const point shift( npm_width / 2, npm_height / 2 );
    for( const tripoint_abs_omt& dest :
         tripoint_range<tripoint_abs_omt>( current - shift, current + shift ) ) {
        nc_color ter_color = c_black;
        std::string ter_sym = " ";
        const bool see = has_debug_vision || ACTIVE_OVERMAP_BUFFER.seen( dest );
        if( see ) {
            // Only load terrain if we can actually see it
            oter_id cur_ter = ACTIVE_OVERMAP_BUFFER.ter( dest );
            ter_color = cur_ter->get_color();
            ter_sym = cur_ter->get_symbol();
        } else {
            ter_color = c_dark_gray;
            ter_sym = "#";
        }
        map_around[index++] = std::make_pair( ter_color, ter_sym );
    }
    return map_around;
}

static void update_note_preview(
    const std::string& note,
    const std::array<std::pair<nc_color, std::string>, npm_width * npm_height> &map_around,
    const std::tuple<catacurses::window *, catacurses::window *, catacurses::window *> &
    preview_windows )
{
    auto om_symbol = get_note_display_info( note );
    const nc_color note_color = std::get<1>( om_symbol );
    const char symbol = std::get<0>( om_symbol );
    const std::string note_text = note.substr( std::get<2>( om_symbol ), std::string::npos );
    const std::string visible_note_text = note_label_utils::strip_label_commands( note_text );

    auto w_preview = std::get<0>( preview_windows );
    auto w_preview_title = std::get<1>( preview_windows );
    auto w_preview_map = std::get<2>( preview_windows );

    draw_border( *w_preview );
    // NOLINTNEXTLINE(cata-use-named-point-constants)
    mvwprintz( *w_preview, point( 1, 1 ), c_white, _( "Note preview" ) );
    wnoutrefresh( *w_preview );

    werase( *w_preview_title );
    nc_color default_color = note_color;
    print_colored_text(
        *w_preview_title, point_zero, default_color, note_color, visible_note_text,
        report_color_error::no );
    int note_text_width = utf8_width( visible_note_text );
    mvwputch( *w_preview_title, point( note_text_width, 0 ), c_white, LINE_XOXO );
    for( int i = 0; i < note_text_width; i++ ) {
        mvwputch( *w_preview_title, point( i, 1 ), c_white, LINE_OXOX );
    }
    mvwputch( *w_preview_title, point( note_text_width, 1 ), c_white, LINE_XOOX );
    wnoutrefresh( *w_preview_title );

    const int npm_offset_x = 1;
    const int npm_offset_y = 1;
    werase( *w_preview_map );
    draw_border( *w_preview_map, c_yellow );
    for( int i = 0; i < npm_height; i++ ) {
        for( int j = 0; j < npm_width; j++ ) {
            const auto& ter = map_around[i * npm_width + j];
            mvwputch( *w_preview_map, point( j + npm_offset_x, i + npm_offset_y ), ter.first,
                      ter.second );
        }
    }
    mvwputch( *w_preview_map, point( npm_width / 2 + npm_offset_x, npm_height / 2 + npm_offset_y ),
              note_color, symbol );
    wnoutrefresh( *w_preview_map );
}

weather_type_id get_weather_at_point( const point_abs_omt& pos )
{
    // Weather calculation is a bit expensive, so it's cached here.
    static std::map<point_abs_omt, weather_type_id> weather_cache;
    static time_point last_weather_display = calendar::before_time_starts;
    if( last_weather_display != calendar::turn ) {
        last_weather_display = calendar::turn;
        weather_cache.clear();
    }
    auto iter = weather_cache.find( pos );
    if( iter == weather_cache.end() ) {
        // TODO: fix point types
        tripoint_abs_omt pos_z( pos, OVERMAP_HEIGHT );
        const auto& wgen = ACTIVE_OVERMAP_BUFFER.get_settings( pos_z ).weather;
        auto weather = wgen.get_weather_conditions(
                           project_to<coords::ms>( pos_z ), calendar::turn, g->get_seed() );
        iter = weather_cache.insert( std::make_pair( pos, weather ) ).first;
    }
    return iter->second;
}


static bool query_confirm_delete( bool& ask_when_deleting )
{
    if( !ask_when_deleting ) { return true; }

    uilist qry;
    qry.text = _( "Really delete note?" );
    qry.addentry( 1, true, 'Y', _( "Yes." ) );
    qry.addentry( 2, true, 'I', _( "Yes, and don't ask again." ) );
    qry.addentry( 3, true, 'N', _( "No." ) );
    qry.query();
    switch( qry.ret ) {
        case 1:
            return true;
        case 2:
            ask_when_deleting = false;
            return true;
        default:
            return false;
    }
}

struct note_cached {
    tripoint_abs_omt p;
    nc_color col;
    std::string symbol;
    std::string text;
    std::string text_nocolor;
    int dist_from_pl;
};

// Colour-tagged RML twin of update_note_preview's two panes: the note text (in its
// note_color) and the 3x3 neighbour minimap (note symbol at centre). Shared by the
// notes-manager callback (rendered into the uilist's "callback" element) and the
// create_note editor backdrop, so both stay byte-identical.
static std::string note_preview_rml(
    const std::string& note,
    const std::array<std::pair<nc_color, std::string>, npm_width * npm_height> &map_around )
{
    const auto om_symbol_info = get_note_display_info( note );
    const nc_color note_color = std::get<1>( om_symbol_info );
    const char symbol = std::get<0>( om_symbol_info );
    const size_t prefix_len = std::get<2>( om_symbol_info );
    const std::string note_text = note.substr( prefix_len );
    const std::string visible_note_text = note_label_utils::strip_label_commands( note_text );

    std::string rml;
    rml += "<div class=\"cb-text\">";
    rml += cata_text_to_rml( colorize( "Note preview:", c_white ) );
    rml += "<br/>";
    rml += cata_text_to_rml( colorize( visible_note_text, note_color ) );
    rml += "<br/><br/>";
    rml += cata_text_to_rml( colorize( "Overmap:", c_white ) );
    rml += "<table style=\"border-collapse:collapse;margin-top:4px\">";
    for( int i = 0; i < npm_height; i++ ) {
        rml += "<tr>";
        for( int j = 0; j < npm_width; j++ ) {
            const auto& ter = map_around[i * npm_width + j];
            rml += "<td style=\"border:1px solid #5a5a78;padding:2px "
                   "6px;text-align:center;font-family:monospace\">";
            if( i == npm_height / 2 && j == npm_width / 2 ) {
                rml += cata_text_to_rml( colorize( std::string( 1, symbol ), note_color ) );
            } else {
                rml += cata_text_to_rml( colorize( ter.second, ter.first ) );
            }
            rml += "</td>";
        }
        rml += "</tr>";
    }
    rml += "</table></div>";
    return rml;
}

class map_notes_callback: public uilist_callback
{
    private:
        std::vector<note_cached> const *_notes;
        int _selected = 0;

        catacurses::window w_preview;
        catacurses::window w_preview_title;
        catacurses::window w_preview_map;
        std::tuple<catacurses::window *, catacurses::window *, catacurses::window *> preview_windows;
        ui_adaptor ui;

        tripoint_abs_omt note_location() { return ( *_notes )[_selected].p; }

    public:
        bool ask_when_deleting = true;

        map_notes_callback( std::vector<note_cached> const* notes ): _notes( notes ) {
            ui.on_screen_resize( [this]( ui_adaptor & ui ) {
                w_preview = catacurses::newwin(
                                npm_height + 2, max_note_display_length - npm_width - 1, point( npm_width + 2, 2 ) );
                w_preview_title = catacurses::newwin( 2, max_note_display_length + 1, point_zero );
                w_preview_map = catacurses::newwin( npm_height + 2, npm_width + 2, point( 0, 2 ) );
                preview_windows = std::make_tuple( &w_preview, &w_preview_title, &w_preview_map );

                ui.position( point_zero, point( max_note_display_length + 1, npm_height + 4 ) );
            } );
            ui.mark_resize();

            ui.on_redraw( [this]( const ui_adaptor & ) {
                if( _selected >= 0 && static_cast<size_t>( _selected ) < _notes->size() ) {
                    const tripoint_abs_omt note_pos = note_location();
                    const auto map_around = get_overmap_neighbors( note_pos );
                    update_note_preview(
                        ACTIVE_OVERMAP_BUFFER.note( note_pos ), map_around, preview_windows );
                } else {
                    update_note_preview( {}, {}, preview_windows );
                }
            } );
        }

        bool key( const input_context& ctxt, const input_event& event, int, uilist* menu ) override {
            const std::string& action = ctxt.input_to_action( event );
            if( action == "CHANGE_SORT" ) {
                menu->ret = UILIST_CHANGE_SORT;
                return true;
            }
            if( action == "CLEAR_FILTER" ) {
                menu->clear_filter();
                return true;
            }
            _selected = menu->selected;
            if( _selected >= 0 && _selected < static_cast<int>( _notes->size() ) ) {
                if( action == "DELETE_NOTE" ) {
                    if( ACTIVE_OVERMAP_BUFFER.has_note( note_location() )
                        && query_confirm_delete( ask_when_deleting ) ) {
                        ACTIVE_OVERMAP_BUFFER.delete_note( note_location() );
                        menu->ret = UILIST_MAP_NOTE_DELETED;
                    }
                    return true;
                }
                if( action == "EDIT_NOTE" ) {
                    create_note( note_location() );
                    menu->ret = UILIST_MAP_NOTE_EDITED;
                    return true;
                }
                if( action == "MARK_DANGER" ) {
                    // NOLINTNEXTLINE(cata-text-style): No need for two whitespaces
                    if( query_yn( _( "Mark area as dangerous ( to avoid on automove paths? )" ) ) ) {
                        const int max_amount = 20;
                        // NOLINTNEXTLINE(cata-text-style): No need for two whitespaces
                        const std::string popupmsg = _( "Danger radius in overmap squares? ( 0-20 )" );
                        int amount =
                            string_input_popup()
                            .title( popupmsg )
                            .width( 20 )
                            .text( std::to_string( 0 ) )
                            .only_digits( true )
                            .query_int();
                        if( amount > -1 && amount <= max_amount ) {
                            ACTIVE_OVERMAP_BUFFER.mark_note_dangerous( note_location(), amount, true );
                            menu->ret = UILIST_MAP_NOTE_EDITED;
                            return true;
                        }
                    } else if( ACTIVE_OVERMAP_BUFFER.is_marked_dangerous( note_location() )
                               && query_yn( _( "Remove dangerous mark?" ) ) ) {
                        ACTIVE_OVERMAP_BUFFER.mark_note_dangerous( note_location(), 0, false );
                        menu->ret = UILIST_MAP_NOTE_EDITED;
                        return true;
                    }
                }
            }
            return false;
        }

        void select( uilist* menu ) override {
            _selected = menu->selected;
            ui.invalidate_ui();
        }

        void draw_rml( uilist* menu, Rml::ElementDocument* doc ) override {
            Rml::Element* cb = doc->GetElementById( "callback" );
            if( !cb ) { return; }
            _selected = menu->selected;
            if( _selected < 0 || static_cast<size_t>( _selected ) >= _notes->size() ) {
                cb->SetInnerRML( "" );
                return;
            }

            const tripoint_abs_omt note_pos = note_location();
            const auto map_around = get_overmap_neighbors( note_pos );
            const std::string note = ACTIVE_OVERMAP_BUFFER.note( note_pos );
            cb->SetInnerRML( note_preview_rml( note, map_around ) );
        }
};

enum class sort_mode_t : int {
    name,
    distance,
    symbol,
    num,
};

static bool sortfunc_dist( const note_cached& a, const note_cached& b )
{
    if( a.dist_from_pl == b.dist_from_pl ) {
        // Compare points to get stable order
        return a.p < b.p;
    } else {
        return a.dist_from_pl < b.dist_from_pl;
    }
}

static bool sortfunc_name( const note_cached& a, const note_cached& b )
{
    if( a.text_nocolor == b.text_nocolor ) {
        return sortfunc_dist( a, b );
    } else {
        return localized_compare( a.text_nocolor, b.text_nocolor );
    }
}

static bool sortfunc_symbol( const note_cached& a, const note_cached& b )
{
    if( a.symbol == b.symbol ) {
        return sortfunc_name( a, b );
    } else {
        // Not using lexicographic comparator here because it's case-insensitive
        // NOLINTNEXTLINE(cata-use-localized-sorting)
        return a.symbol < b.symbol;
    }
}

static tripoint_abs_omt show_notes_manager( const tripoint_abs_omt& origin )
{
    tripoint_abs_omt result = tripoint_abs_omt( tripoint_min );

    bool ask_when_deleting = true;
    uilist nmenu;
    std::string filter;
    tripoint_abs_omt selected = origin;
    sort_mode_t sort_mode = sort_mode_t::name;

    const tripoint_abs_omt p_player = g->u.abs_omt_pos();

    bool quit = false;
    while( !quit ) {
        nmenu.init();
        nmenu.color_error( false );
        nmenu.desc_enabled = true;
        nmenu.input_category = "OVERMAP_NOTES";
        nmenu.additional_actions.emplace_back( "DELETE_NOTE", translation() );
        nmenu.additional_actions.emplace_back( "EDIT_NOTE", translation() );
        nmenu.additional_actions.emplace_back( "CHANGE_SORT", translation() );
        nmenu.additional_actions.emplace_back( "CLEAR_FILTER", translation() );
        nmenu.additional_actions.emplace_back( "MARK_DANGER", translation() );
        const input_context ctxt( nmenu.input_category );
        nmenu.text = string_format(
                         _( "<%s> - center on note, <%s> - edit note, <%s> - mark as dangerous, <%s> - delete "
                            "note, <%s> - close window" ),
                         colorize( "RETURN", c_yellow ), colorize( ctxt.key_bound_to( "EDIT_NOTE" ), c_yellow ),
                         colorize( ctxt.key_bound_to( "MARK_DANGER" ), c_red ),
                         colorize( ctxt.key_bound_to( "DELETE_NOTE" ), c_yellow ), colorize( "ESCAPE", c_yellow ) );

        std::vector<note_cached> notes;
        for( int zlev = -OVERMAP_DEPTH; zlev <= OVERMAP_HEIGHT; zlev++ ) {
            overmapbuffer::t_notes_vector notes_raw = ACTIVE_OVERMAP_BUFFER.get_all_notes( zlev );
            notes.reserve( notes.size() + notes_raw.size() );
            for( const auto& it : notes_raw ) {
                auto om_symbol = get_note_display_info( it.second );
                note_cached n;
                n.p = tripoint_abs_omt( it.first, zlev );
                n.col = std::get<1>( om_symbol );
                n.symbol = std::string( 1, std::get<0>( om_symbol ) );
                n.text = it.second.substr( std::get<2>( om_symbol ), std::string::npos );
                n.text_nocolor = remove_color_tags( n.text );
                n.dist_from_pl = rl_dist( p_player, n.p );
                notes.push_back( std::move( n ) );
            }
        }

        const char *sort_str;
        switch( sort_mode ) {
            case sort_mode_t::name:
                sort_str = pgettext( "Sorted by:", "name" );
                std::sort( notes.begin(), notes.end(), sortfunc_name );
                break;
            case sort_mode_t::distance:
                sort_str = pgettext( "Sorted by:", "distance" );
                std::sort( notes.begin(), notes.end(), sortfunc_dist );
                break;
            case sort_mode_t::symbol:
                sort_str = pgettext( "Sorted by:", "symbol" );
                std::sort( notes.begin(), notes.end(), sortfunc_symbol );
                break;
            default:
                debugmsg( "Unimplemented" );
                break;
        }
        //~ %1$d is total number of notes, %2$s is hotkey for sorting, %3$s is sort criterion
        nmenu.title = string_format(
                          _( "Map notes (%1$d)     [%2$s] Sorted by: %3$s" ), notes.size(),
                          ctxt.key_bound_to( "CHANGE_SORT" ), sort_str );

        int entry_to_select = -1;
        for( size_t i = 0; i < notes.size(); i++ ) {
            const note_cached& note = notes[i];
            if( note.p == selected ) { entry_to_select = i; }
            const std::string direction_str = direction_name_short(
                                                  direction_from( p_player, note.p ) );
            const std::string location_desc = ACTIVE_OVERMAP_BUFFER.get_description_at(
                                                  project_to<coords::sm>( note.p ) );

            //~ "Dangerous" indicator for overmap note in note manager.
            //~ Must occupy exactly 2 columns, and not resemble a number or a digit.
            //~ English uses D for Danger, but it's acceptable to leave it untranslated
            //~ or use some special symbol instead (e.g. exclamation mark)
            //~ if you're having trouble making it look nice in your language.
            const char *danger_abbr = pgettext( "danger indicator", " D" );
            const bool is_dangerous = ACTIVE_OVERMAP_BUFFER.is_marked_dangerous( note.p );
            std::optional<int> this_dr = ACTIVE_OVERMAP_BUFFER.has_note_with_danger_radius( note.p );
            std::string dr_short;
            if( this_dr ) {
                if( *this_dr == 0 ) {
                    // Dangerous area
                    dr_short = colorize( danger_abbr, c_red );
                } else {
                    // Dangerous area with danger radius
                    dr_short = string_format( "<color_red>%2d</color>", *this_dr );
                }
            } else {
                if( is_dangerous ) {
                    // Not dangerous by itself, but falls under danger radius
                    // of some other note
                    dr_short = colorize( danger_abbr, c_yellow );
                } else {
                    // Safe
                    dr_short = "  ";
                }
            }

            nmenu.addentry_desc(
                string_format( "[%s] %s", colorize( note.symbol, note.col ), note.text ),
                string_format(
                    _( "<color_red>LEVEL %i, %s</color>: %s (Distance: <color_white>%d %s</color>) "
                       "<color_red>%s</color>" ),
                    note.p.z(), fmt_omt_coords( note.p ), location_desc, note.dist_from_pl,
                    trim_whitespaces( direction_str ), is_dangerous ? _( "DANGEROUS AREA!" ) : "" ) );
            nmenu.entries[i].ctxt = string_format(
                                        "%s<color_white>% 4d %s</color>", dr_short, note.dist_from_pl, direction_str );
        }
        nmenu.set_filter( filter );          // Restore filter
        nmenu.set_selected( entry_to_select ); // Restore selection
        map_notes_callback cb( &notes );
        cb.ask_when_deleting = ask_when_deleting;
        nmenu.callback = &cb;
        nmenu.menu_style = "info"; // RmlUi: two-column with note detail panel
        nmenu.query();

        if( nmenu.ret == UILIST_CHANGE_SORT ) {
            sort_mode = static_cast<sort_mode_t>(
                            ( static_cast<int>( sort_mode ) + 1 ) % static_cast<int>( sort_mode_t::num ) );
        }

        if( nmenu.ret == UILIST_MAP_NOTE_DELETED || nmenu.ret == UILIST_MAP_NOTE_EDITED
            || nmenu.ret == UILIST_CHANGE_SORT ) {
            // Save state
            ask_when_deleting = cb.ask_when_deleting;
            filter = nmenu.get_filter();
            if( nmenu.ret == UILIST_MAP_NOTE_EDITED || nmenu.ret == UILIST_CHANGE_SORT ) {
                // Reselect same note
                assert( nmenu.selected >= 0 && nmenu.selected < static_cast<int>( notes.size() ) );
                selected = notes[nmenu.selected].p;
            } else {
                assert( nmenu.ret == UILIST_MAP_NOTE_DELETED );
                // Select next visible note (if one exists) or the last visible
                // note if removed one was the last.
                bool take_next = false;
                selected = tripoint_abs_omt( tripoint_min );
                for( const int i : nmenu.get_filtered() ) {
                    if( nmenu.selected == i ) {
                        take_next = true;
                        continue;
                    }
                    selected = notes[i].p;
                    if( take_next ) { break; }
                }
            }
        } else if( nmenu.ret >= 0 && nmenu.ret < static_cast<int>( notes.size() ) ) {
            result = notes[nmenu.ret].p;
            quit = true;
        } else {
            quit = true;
        }
    }
    return result;
}


// RmlUi model for the overmap legend sidebar (Tier 6 slice 1). Flat strings only
// (no row vectors) → nothing to register; bound directly in display().
namespace
{
struct om_sidebar_session {
    Rml::String info_rml;
    Rml::String hints_rml;
    Rml::String footer_rml;
    Rml::DataModelHandle handle;
};

// Mirror of draw_om_sidebar's content into the RmlUi model (the curses path below
// stays intact for the A/B). Built each redraw with the current cursor/toggles.
void build_om_sidebar_rml(
    om_sidebar_session& s, const tripoint_abs_omt& center, const tripoint_abs_omt& orig,
    bool fast_scroll, input_context* inp_ctxt, const draw_data_t &data )
{
    avatar& player_character = get_avatar();
    const bool has_debug_vision = player_character.has_trait( trait_DEBUG_NIGHTVISION );
    const int sight_points =
        !has_debug_vision
        ? player_character.overmap_sight_range( g->light_level( player_character.bub_pos().z() ) )
        : 100;
    const bool center_seen = has_debug_vision || ACTIVE_OVERMAP_BUFFER.seen( center );
    const tripoint_abs_omt target = player_character.get_active_mission_target();
    const bool has_target = target != overmap::invalid_tripoint;
    const bool viewing_weather = uistate.overmap_debug_weather || uistate.overmap_visible_weather;

    std::vector<mongroup *> mgroups;
    if( uistate.overmap_debug_mongroup ) {
        mgroups = ACTIVE_OVERMAP_BUFFER.monsters_at( center );
        for( const auto& mgp : mgroups ) {
            if( mgp->horde ) { break; }
        }
    }

    // info block (tile description / weather / mission distance)
    std::string info;
    if( center_seen ) {
        if( !mgroups.empty() ) {
            for( const auto& mgroup : mgroups ) {
                info += colorize( string_format( "Species: %s", mgroup->type.c_str() ), c_blue ) + "\n";
                info +=
                    colorize( string_format(
                                  "# monsters: %d", mgroup->population + mgroup->monsters.size() ),
                              c_blue )
                    + "\n";
                if( !mgroup->horde ) { continue; }
                info += colorize( string_format( "Interest: %d", mgroup->interest ), c_blue ) + "\n";
                info += colorize( string_format( "Behaviour: %s", mgroup->horde_behaviour ), c_blue )
                        + "\n";
                info += colorize( string_format( "Target: %s", mgroup->target.to_string() ), c_blue )
                        + "\n";
            }
        } else {
            const oter_id cur_oter_id = ACTIVE_OVERMAP_BUFFER.ter( center );
            const regional_settings& sidebar_region = ACTIVE_OVERMAP_BUFFER.get_settings( center );
            const bool sidebar_has_display = !sidebar_region.display_oter.is_empty();
            const oter_id default_oter_id = sidebar_region.default_oter.id();
            const oter_id render_oter_id =
                ( sidebar_has_display && cur_oter_id == default_oter_id )
                ? sidebar_region.display_oter.id()
                : cur_oter_id;
            const oter_t &ter = render_oter_id.obj();
            const auto sm_pos = project_to<coords::sm>( center );
            const std::string desc =
                sidebar_has_display && cur_oter_id == default_oter_id
                ? ter.get_name()
                : ACTIVE_OVERMAP_BUFFER.get_description_at( sm_pos );
            info +=
                colorize( ter.get_symbol(), ter.get_color() ) + " " + colorize( desc, c_light_gray );
        }
    } else {
        info += colorize( _( "# Unexplored" ), c_dark_gray );
    }
    if( viewing_weather ) {
        const bool weather_is_visible =
            center.z() >= 0
            && ( uistate.overmap_debug_weather
                 || player_character.overmap_los(
                     tripoint_abs_omt( center.xy(), OVERMAP_HEIGHT ), sight_points * 2 ) );
        info += "\n";
        if( weather_is_visible ) {
            info += colorize( get_weather_at_point( center.xy() )->name.translated(),
                              get_weather_at_point( center.xy() )->color );
        } else {
            info += colorize( _( "# Weather unknown" ), c_dark_gray );
        }
    }
    if( data.debug_editor && center_seen ) {
        const oter_t &oter = ACTIVE_OVERMAP_BUFFER.ter( center ).obj();
        info += "\n"
                + colorize( string_format( _( "oter: %s (rot %d)" ), oter.id.str(), oter.get_rotation() ),
                            c_white );
        info +=
            "\n" + colorize( string_format( _( "oter_type: %s" ), oter.get_type_id().str() ), c_white );
        for( cube_direction dir : all_enum_values<cube_direction>() ) {
            if( std::string * join = ACTIVE_OVERMAP_BUFFER.join_used_at( {center, dir} ) ) {
                info += "\n"
                        + colorize( string_format( _( "join %s: %s" ), io::enum_to_string( dir ), *join ),
                                    c_white );
            }
        }
        std::optional<mapgen_arguments> *args = ACTIVE_OVERMAP_BUFFER.mapgen_args( center );
        if( args ) {
            if( *args ) {
                for( const std::pair<const std::string, cata_variant> &arg : ( **args ).map ) {
                    info += "\n"
                            + colorize( string_format( "%s = %s", arg.first, arg.second.get_string() ),
                                        c_white );
                }
            } else {
                info += "\n" + colorize( _( "args not yet set" ), c_white );
            }
        }
    }
    if( has_target ) {
        const int distance = rl_dist( center, target );
        info += "\n" + colorize( _( "Distance to active mission:" ), c_white );
        info += "\n" + colorize( string_format( _( "%d tiles" ), distance ), c_white );
        const int above_below = target.z() - orig.z();
        std::string msg;
        if( above_below > 0 ) {
            msg = _( "Above us" );
        } else if( above_below < 0 ) {
            msg = _( "Below us" );
        }
        if( above_below != 0 ) { info += "\n" + colorize( msg, c_white ); }
    }
    for( auto& mission : player_character.get_active_missions() ) {
        if( mission->get_target() == center ) { info += "\n" + colorize( mission->name(), c_white ); }
    }
    s.info_rml = cata_text_to_rml( info );

    // hints block (pan hints + the keybinding list, coloured pink when toggled on)
    std::string hints = colorize( _( "Use movement keys to pan." ), c_magenta ) + "\n";
    hints += colorize( _( "Press W to preview route." ), c_magenta ) + "\n";
    hints += colorize( _( "Press again to confirm." ), c_magenta );
    if( inp_ctxt != nullptr ) {
        const auto print_hint = [&]( const std::string & action, nc_color color = c_magenta ) {
            hints +=
                "\n"
                + colorize( string_format( _( "%s - %s" ), inp_ctxt->get_desc( action ),
                                           inp_ctxt->get_action_name( action ) ),
                            color );
        };
        if( data.debug_editor ) {
            print_hint( "PLACE_TERRAIN", c_light_blue );
            print_hint( "PLACE_SPECIAL", c_light_blue );
            print_hint( "SET_SPECIAL_ARGS", c_light_blue );
        }
        const bool show_overlays = uistate.overmap_show_overlays || uistate.overmap_blinking;
        const bool is_explored = ACTIVE_OVERMAP_BUFFER.is_explored( center );
        const bool is_path = ACTIVE_OVERMAP_BUFFER.is_path( center );
        print_hint( "LEVEL_UP" );
        print_hint( "LEVEL_DOWN" );
        print_hint( "CENTER" );
        print_hint( "SEARCH" );
        print_hint( "CREATE_NOTE" );
        print_hint( "DELETE_NOTE" );
        print_hint( "LIST_NOTES" );
        print_hint( "MISSIONS" );
        print_hint( "TOGGLE_MAP_NOTES", uistate.overmap_show_map_notes ? c_pink : c_magenta );
        print_hint( "TOGGLE_BLINKING", uistate.overmap_blinking ? c_pink : c_magenta );
        print_hint( "TOGGLE_OVERLAYS", show_overlays ? c_pink : c_magenta );
        print_hint( "TOGGLE_LAND_USE_CODES",
                    uistate.overmap_show_land_use_codes ? c_pink : c_magenta );
        print_hint( "TOGGLE_CITY_LABELS", uistate.overmap_show_city_labels ? c_pink : c_magenta );
        print_hint( "TOGGLE_HORDES", uistate.overmap_show_hordes ? c_pink : c_magenta );
        print_hint( "TOGGLE_EXPLORED", is_explored ? c_pink : c_magenta );
        print_hint( "TOGGLE_MARK_PATH", is_path ? c_pink : c_magenta );
        print_hint( "TOGGLE_FAST_SCROLL", fast_scroll ? c_pink : c_magenta );
        print_hint( "TOGGLE_FOREST_TRAILS", uistate.overmap_show_forest_trails ? c_pink : c_magenta );
        print_hint( "TOGGLE_OVERMAP_WEATHER", uistate.overmap_visible_weather ? c_pink : c_magenta );
        print_hint( "TOGGLE_DEFAULT_0", uistate.overmap_default_0 ? c_pink : c_magenta );
        print_hint( "SET_CUSTOM_WAYPOINT", player_character.custom_waypoint ? c_pink : c_magenta );
        print_hint( "HELP_KEYBINDINGS" );
        print_hint( "QUIT" );
    }
    s.hints_rml = cata_text_to_rml( hints );

    // footer (dimension name + level/coordinates)
    std::string dim_name;
    if( const dimension_info * dim = g->get_current_dimension_info() ) {
        dim_name =
            dim->display_name.empty()
            ? ( dim->world_type.is_valid()
                ? dim->world_type.obj().name.translated()
                : dim->dimension_id )
            : dim->display_name;
    }
    std::string footer = colorize( dim_name, c_cyan );
    footer += "\n"
              + colorize( string_format( _( "LEVEL %i, %s" ), center.z(), fmt_omt_coords( center ) ), c_red );
    s.footer_rml = cata_text_to_rml( footer );

    s.handle.DirtyAllVariables();
}
} // namespace

static void draw_om_sidebar(
    const catacurses::window & /* wbar */, const tripoint_abs_omt& center,
    const tripoint_abs_omt& orig, bool /* blink */, bool fast_scroll, input_context* inp_ctxt,
    const draw_data_t &data, om_sidebar_session* rml = nullptr )
{
    // RmlUi render path: skip the curses legend draw, sync the model instead.
    if( rml && rml->handle ) {
        build_om_sidebar_rml( *rml, center, orig, fast_scroll, inp_ctxt, data );
        return;
    }
}

tiles_redraw_info redraw_info;

static void draw(
    ui_adaptor& ui, const tripoint_abs_omt& center, const tripoint_abs_omt& orig, bool blink,
    bool show_explored, bool fast_scroll, input_context* inp_ctxt, const draw_data_t &data,
    grids_draw_data& grids_data, om_sidebar_session* rml = nullptr )
{
    draw_om_sidebar( g->w_omlegend, center, orig, blink, fast_scroll, inp_ctxt, data, rml );
    // Tiles-only fork: use_tiles / use_tiles_overmap are forced true, so the overmap
    // always renders via the tile path (sdltiles.cpp). The curses draw_ascii branch
    // (+ its draw_city_labels/draw_map_labels helpers) was dead and has been removed.
    redraw_info = tiles_redraw_info{center, blink};
    werase( g->w_overmap );
    // trigger the actual redraw code in sdltiles.cpp
    wnoutrefresh( g->w_overmap );
}

static void create_note( const tripoint_abs_omt& curs )
{
    std::string color_notes = _( "Color codes: " );
    for( const auto& color_pair : get_note_color_names() ) {
        // The color index is not translatable, but the name is.
        color_notes += string_format(
                           "%1$s:<color_%3$s>%2$s</color>, ", color_pair.first.c_str(), _( color_pair.second ),
                           replace_all( color_pair.second, " ", "_" ) );
    }

    const auto helper_text = string_format(
                                 ".\n\n%s\n%s\n%s\n%s\n%s\n\n%s\n",
                                 _( "Type <color_white>COLOR;TEXT</color> to set a custom color." ),
                                 _( "Type <color_white>GLYPH:TEXT</color> to set a custom glyph." ),
                                 _( "Type <color_white>SPRITE:TILE_ID</color> to set a custom sprite." ),
                                 _( "Type <color_white>LABEL:TEXT</color> to set a custom label." ),
                                 _( "Use <color_white>;</color> as a separator to combine elements." ),
                                 // NOLINTNEXTLINE(cata-text-style): literal exclaimation mark
                                 _( "Examples: <color_white>$:Bank</color> | <color_white>R;Red</color> | "
                                    "<color_white>SPRITE:toolbox</color> | <color_white>LABEL:Survivor City</color>" ) );
    color_notes = color_notes.replace( color_notes.end() - 2, color_notes.end(), helper_text );
    std::string title = _( "Note:" );

    const std::string old_note = ACTIVE_OVERMAP_BUFFER.note( curs );
    std::string new_note = old_note;
    auto map_around = get_overmap_neighbors( curs );

    catacurses::window w_preview;
    catacurses::window w_preview_title;
    catacurses::window w_preview_map;
    std::tuple<catacurses::window *, catacurses::window *, catacurses::window *> preview_windows;

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        w_preview = catacurses::newwin(
                        npm_height + 2, max_note_display_length - npm_width - 1, point( npm_width + 2, 2 ) );
        w_preview_title = catacurses::newwin( 2, max_note_display_length + 1, point_zero );
        w_preview_map = catacurses::newwin( npm_height + 2, npm_width + 2, point( 0, 2 ) );
        preview_windows = std::make_tuple( &w_preview, &w_preview_title, &w_preview_map );

        ui.position( point_zero, point( max_note_display_length + 1, npm_height + 4 ) );
    } );
    ui.mark_resize();

    // RmlUi backdrop: the note preview pane as a doc stacked under the string_input
    // "Note:" popup, replacing the curses w_preview/title/map panes. Re-synced each
    // keystroke (the loop below invalidate_ui()s); same producer as the notes-manager.
    Rml::ElementDocument* preview_doc = nullptr;
    if( overmap_rmlui_enabled() && rmlui_layer::ready() ) {
        preview_doc =
            rmlui_layer::open_document( PATH_INFO::datadir() + "gui/overmap_note.rml", true );
    }

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( preview_doc ) {
            if( Rml::Element * el = preview_doc->GetElementById( "preview" ) ) {
                el->SetInnerRML( note_preview_rml( new_note, map_around ) );
            }
            return;
        }
    } );

    // this implies enable_ime() and ensures that ime mode is always restored on return
    ime_sentry sentry;

    bool esc_pressed = false;
    string_input_popup input_popup;
    input_popup.title( title )
               .width( max_note_length )
               .text( new_note )
               .description( color_notes )
               .title_color( c_white )
               .desc_color( c_light_gray )
               .string_color( c_yellow )
               .identifier( "map_note" );

    do {
        new_note = input_popup.query_string( false );
        if( input_popup.canceled() ) {
            new_note = old_note;
            esc_pressed = true;
            break;
        } else if( input_popup.confirmed() ) {
            break;
        }
        ui.invalidate_ui();
    } while( true );

    if( preview_doc ) { rmlui_layer::close_document( preview_doc ); }
    disable_ime();

    if( !esc_pressed && new_note.empty() && !old_note.empty() ) {
        if( query_yn( _( "Really delete note?" ) ) ) { ACTIVE_OVERMAP_BUFFER.delete_note( curs ); }
    } else if( !esc_pressed && old_note != new_note ) {
        ACTIVE_OVERMAP_BUFFER.add_note( curs, new_note );
    }
}

// RmlUi model for the overmap search result box (Tier 6 slice 2; shares the
// overmap_rmlui_enabled() toggle — one overmap family, gated per sub-screen).
namespace
{
struct om_search_session {
    Rml::String body_rml;
    Rml::String hints_rml;
    Rml::DataModelHandle handle;
};
} // namespace

// if false, search yielded no results
static bool search( const ui_adaptor& om_ui, tripoint_abs_omt& curs, const tripoint_abs_omt& orig )
{
    std::string term =
        string_input_popup()
        .title( _( "Search term:" ) )
        .description( _( "Multiple entries separated with comma (,). Excludes starting with "
                     "hyphen (-)." ) )
        .identifier( "overmap" )
        .query_string();
    if( term.empty() ) { return false; }

    std::vector<point_abs_omt> locations;
    std::vector<point_abs_om> overmap_checked;

    const int radius = OMAPX; // arbitrary
    for( const tripoint_abs_omt& p : points_in_radius( curs, radius ) ) {
        overmap_with_local_coords om_loc = ACTIVE_OVERMAP_BUFFER.get_existing_om_global( p );

        if( om_loc ) {
            tripoint_om_omt om_relative = om_loc.local;
            point_abs_om om_cache = project_to<coords::om>( p.xy() );

            if( std::find( overmap_checked.begin(), overmap_checked.end(), om_cache )
                == overmap_checked.end() ) {
                overmap_checked.push_back( om_cache );
                std::vector<point_abs_omt> notes = om_loc.om->find_notes( curs.z(), term );
                locations.insert( locations.end(), notes.begin(), notes.end() );
            }

            if( om_loc.om->seen( om_relative )
                && match_include_exclude( om_loc.om->ter( om_relative )->get_name(), term ) ) {
                locations.push_back( project_combine( om_loc.om->pos(), om_relative.xy() ) );
            }
        }
    }

    if( locations.empty() ) {
        sfx::play_variant_sound( "menu_error", "default", 100 );
        popup( _( "No results found." ) );
        return false;
    }

    std::sort( locations.begin(), locations.end(),
    [&]( const point_abs_omt & lhs, const point_abs_omt & rhs ) {
        return trig_dist( curs, tripoint_abs_omt( lhs, curs.z() ) )
               < trig_dist( curs, tripoint_abs_omt( rhs, curs.z() ) );
    } );

    int i = 0;
    // Navigate through results
    const tripoint_abs_omt prev_curs = curs;

    catacurses::window w_search;

    ui_adaptor ui;
    int search_width = OVERMAP_LEGEND_WIDTH - 1;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        w_search = catacurses::newwin( 13, search_width, point( TERMX - search_width, 3 ) );

        ui.position_from_window( w_search );
    } );
    ui.mark_resize();

    input_context ctxt( "OVERMAP_SEARCH" );
    ctxt.register_action( "NEXT_TAB", to_translation( "Next result" ) );
    ctxt.register_action( "PREV_TAB", to_translation( "Previous result" ) );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "ANY_INPUT" );

    auto rml_data = std::make_unique<om_search_session>();
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml_data->handle ) { return; }
        std::string body = colorize( _( "Search:" ), c_light_blue ) + " " + colorize( term, c_light_red );
        body +=
            "\n" + colorize( locations.size() == 1 ? _( "Result:" ) : _( "Results:" ), c_light_blue )
            + " "
            + colorize( string_format( "%d/%d", i + 1, static_cast<int>( locations.size() ) ),
                        c_light_red );
        body +=
            "\n" + colorize( _( "Direction:" ), c_light_blue ) + " "
            + colorize(
                string_format( "%d %s", trig_dist( orig, tripoint_abs_omt( locations[i], orig.z() ) ),
                               direction_name_short(
                                   direction_from( orig, tripoint_abs_omt( locations[i], orig.z() ) ) ) ),
                c_light_red );
        rml_data->body_rml = cata_text_to_rml( body );
        std::string hints;
        if( locations.size() > 1 ) {
            hints +=
                string_format(
                    _( "Press [<color_yellow>%s</color>] or [<color_yellow>%s</color>] to "
                       "cycle through search results." ),
                    ctxt.get_desc( "NEXT_TAB" ), ctxt.get_desc( "PREV_TAB" ) )
                + "\n";
        }
        hints +=
            string_format(
                _( "Press [<color_yellow>%s</color>] to confirm." ),
                ctxt.get_desc( "CONFIR"
                               "M" ) )
            + "\n";
        hints += string_format(
                     _( "Press [<color_yellow>%s</color>] to quit." ),
                     ctxt.get_desc(
                         "QUI"
                         "T" ) );
        rml_data->hints_rml = cata_text_to_rml( hints );
        rml_data->handle.DirtyAllVariables();
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    rml.open( overmap_rmlui_enabled(), "overmapsearch", ctxt, [&]( Rml::DataModelConstructor & c ) {
        c.Bind( "body_rml", &rml_data->body_rml );
        c.Bind( "hints_rml", &rml_data->hints_rml );
        rml_data->handle = c.GetModelHandle();
    } );

    std::string action;
    do {
        curs.x() = locations[i].x();
        curs.y() = locations[i].y();
        om_ui.invalidate_ui();
        ui_manager::redraw();
        action = ctxt.handle_input( get_option<int>( "BLINK_SPEED" ) );
        if( uistate.overmap_blinking ) {
            uistate.overmap_show_overlays = !uistate.overmap_show_overlays;
        }
        if( action == "NEXT_TAB" ) {
            i = ( i + 1 ) % locations.size();
        } else if( action == "PREV_TAB" ) {
            i = ( i + locations.size() - 1 ) % locations.size();
        } else if( action == "QUIT" ) {
            curs = prev_curs;
            om_ui.invalidate_ui();
        }
    } while( action != "CONFIRM" && action != "QUIT" );
    return true;
}

static void place_ter_or_special(
    const ui_adaptor& om_ui, tripoint_abs_omt& curs, const std::string& om_action )
{
    uilist pmenu;
    // This simplifies overmap_special selection using uilist
    std::vector<const overmap_special *> oslist;
    const bool terrain = om_action == "PLACE_TERRAIN";

    if( terrain ) {
        pmenu.title = _( "Select terrain to place:" );
        for( const oter_t &oter : overmap_terrains::get_all() ) {
            const std::string entry_text = string_format(
                                               _( "sym: [ %s %s ], color: [ %s %s], name: [ %s ], id: [ %s ]" ),
                                               colorize( oter.get_symbol(), oter.get_color() ),
                                               colorize( oter.get_symbol( true ), oter.get_color( true ) ),
                                               colorize( string_from_color( oter.get_color() ), oter.get_color() ),
                                               colorize( string_from_color( oter.get_color( true ) ), oter.get_color( true ) ),
                                               colorize( oter.get_name(), oter.get_color() ), colorize( oter.id.str(), c_white ) );
            pmenu.addentry( oter.id.id().to_i(), true, 0, entry_text );
        }
    } else {
        pmenu.title = _( "Select special to place:" );
        for( const overmap_special& elem : overmap_specials::get_all() ) {
            oslist.push_back( &elem );
            const std::string entry_text = elem.id.str();
            pmenu.addentry( oslist.size() - 1, true, 0, entry_text );
        }
    }
    pmenu.query();

    if( pmenu.ret >= 0 ) {
        catacurses::window w_editor;

        ui_adaptor ui;
        ui.on_screen_resize( [&]( ui_adaptor & ui ) {
            w_editor = catacurses::newwin( 15, 27, point( TERMX - 27, 3 ) );

            ui.position_from_window( w_editor );
        } );
        ui.mark_resize();

        input_context ctxt( "OVERMAP_EDITOR" );
        ctxt.register_directions();
        ctxt.register_action( "CONFIRM" );
        ctxt.register_action( "ROTATE" );
        ctxt.register_action( "QUIT" );
        ctxt.register_action( "HELP_KEYBINDINGS" );
        ctxt.register_action( "ANY_INPUT" );

        if( terrain ) {
            uistate.place_terrain = &oter_id( pmenu.ret ).obj();
        } else {
            uistate.place_special = oslist[pmenu.ret];
        }
        // TODO: Unify these things.
        const bool can_rotate =
            terrain ? uistate.place_terrain->is_rotatable() : uistate.place_special->is_rotatable();

        uistate.omedit_rotation = om_direction::type::none;
        // If user chose an already rotated submap, figure out its direction
        if( terrain && can_rotate ) {
            for( om_direction::type r : om_direction::all ) {
                if( uistate.place_terrain->id.id() == uistate.place_terrain->get_rotated( r ) ) {
                    uistate.omedit_rotation = r;
                    break;
                }
            }
        }

        // RmlUi backdrop: the editor panel as a passive doc over the map, replacing the
        // curses w_editor box. Re-synced each redraw (id/rotation change on ROTATE).
        Rml::ElementDocument* editor_doc = nullptr;
        if( overmap_rmlui_enabled() && rmlui_layer::ready() ) {
            editor_doc =
                rmlui_layer::open_document( PATH_INFO::datadir() + "gui/overmap_editor.rml", true );
        }
        // RAII teardown so the doc is closed on every exit path (CONFIRM break,
        // QUIT, or an exception out of place_special/ter_set) — matches the
        // render-only close-on-destruction pattern used by the loading/HUD docs.
        on_out_of_scope close_editor_doc( [&]() {
            if( editor_doc ) { rmlui_layer::close_document( editor_doc ); }
        } );
        const auto editor_panel_rml = [&]() -> std::string {
            std::string s;
            s += colorize( terrain ? _( "Place overmap terrain:" ) : _( "Place overmap special:" ),
                           c_white )
            + "\n";
            s += colorize(
                terrain ? uistate.place_terrain->id.str() : uistate.place_special->id.str(),
                c_light_blue )
            + "\n";
            const std::string &rotation = om_direction::name( uistate.omedit_rotation );
            s += colorize(
                string_format( _( "Rotation: %s %s" ), rotation, can_rotate ? "" : _( "(fixed)" ) ),
                c_light_gray )
            + "\n\n";
            // Merged from 5 narrow curses lines into one wrapping paragraph (dev wizard tool).
            s += colorize( _( "Areas highlighted in red already have map content generated. "
                              "Their overmap id will change, but not their contents." ),
                           c_red )
            + "\n\n";
            if( can_rotate )
            {
                s += colorize( string_format( _( "[%s] Rotate" ), ctxt.get_desc( "ROTATE" ) ), c_white )
                + "\n";
            }
            s += colorize( string_format( _( "[%s] Apply" ), ctxt.get_desc( "CONFIRM" ) ), c_white ) + "\n";
            s += colorize( _( "[ESCAPE/Q] Cancel" ), c_white );
            return cata_text_to_rml( s );
        };

        ui.on_redraw( [&]( const ui_adaptor & ) {
            if( editor_doc ) {
                if( Rml::Element * el = editor_doc->GetElementById( "editor" ) ) {
                    el->SetInnerRML( editor_panel_rml() );
                }
                return;
            }
        } );

        std::string action;
        do {
            om_ui.invalidate_ui();
            ui_manager::redraw();

            action = ctxt.handle_input( get_option<int>( "BLINK_SPEED" ) );

            if( const std::optional<tripoint_rel_ms> vec = ctxt.get_direction( action ) ) {
                curs += vec->xy().raw();
            } else if( action == "CONFIRM" ) { // Actually modify the overmap
                if( terrain ) {
                    ACTIVE_OVERMAP_BUFFER.ter_set( curs, uistate.place_terrain->id.id() );
                    ACTIVE_OVERMAP_BUFFER.set_seen( curs, true );
                } else {
                    if( std::optional<std::vector<tripoint_abs_omt>> used_points =
                            ACTIVE_OVERMAP_BUFFER.place_special(
                                *uistate.place_special, curs, uistate.omedit_rotation, false,
                                true ) ) {
                        for( const tripoint_abs_omt& pos : *used_points ) {
                            ACTIVE_OVERMAP_BUFFER.set_seen( pos, true );
                        }
                    }
                }
                break;
            } else if( action == "ROTATE" && can_rotate ) {
                uistate.omedit_rotation = om_direction::turn_right( uistate.omedit_rotation );
                if( terrain ) {
                    uistate.place_terrain =
                        &uistate.place_terrain->get_rotated( uistate.omedit_rotation ).obj();
                }
            }
            if( uistate.overmap_blinking ) {
                uistate.overmap_show_overlays = !uistate.overmap_show_overlays;
            }
        } while( action != "QUIT" );

        uistate.place_terrain = nullptr;
        uistate.place_special = nullptr;
    }
}

static void set_special_args( tripoint_abs_omt& curs )
{
    std::optional<mapgen_arguments> *maybe_args = ACTIVE_OVERMAP_BUFFER.mapgen_args( curs );
    if( !maybe_args ) {
        popup( _( "No overmap special args at this location." ) );
        return;
    }
    if( *maybe_args ) {
        popup( _( "Overmap special args at this location have already been set." ) );
        return;
    }
    std::optional<overmap_special_id> s = ACTIVE_OVERMAP_BUFFER.overmap_special_at( curs );
    if( !s ) {
        popup( _( "No overmap special at this location from which to fetch parameters." ) );
        return;
    }
    const overmap_special& special = **s;
    const mapgen_parameters& params = special.get_params();
    mapgen_arguments args;
    for( const std::pair<const std::string, mapgen_parameter> &p : params.map ) {
        const std::string param_name = p.first;
        const mapgen_parameter& param = p.second;
        std::vector<std::string> possible_values = param.all_possible_values( params );
        uilist arg_menu;
        arg_menu.title = string_format( _( "Select value for mapgen argument %s: " ), param_name );
        for( size_t i = 0; i != possible_values.size(); ++i ) {
            const std::string& v = possible_values[i];
            arg_menu.addentry( i, true, 0, v );
        }
        arg_menu.query();

        if( arg_menu.ret < 0 ) { return; }
        args.map[param_name] =
            cata_variant::from_string( param.type(), std::move( possible_values[arg_menu.ret] ) );
    }
    *maybe_args = args;
}

static std::vector<tripoint_abs_omt> get_overmap_path_to(
    const tripoint_abs_omt dest, bool driving )
{
    if( !ACTIVE_OVERMAP_BUFFER.seen( dest ) ) { return {}; }
    const Character& player_character = get_player_character();
    map& here = get_map();
    const tripoint_abs_omt player_omt_pos = player_character.abs_omt_pos();
    overmap_path_params params;
    vehicle* player_veh = nullptr;
    if( driving ) {
        const optional_vpart_position vp = here.veh_at( player_character.bub_pos() );
        if( !vp.has_value() ) {
            debugmsg( "Failed to find driven vehicle" );
            return {};
        }
        player_veh = &vp->vehicle();
        // for now we can only handle flyers if already in the air
        const bool can_fly = player_veh->is_aircraft() && player_veh->is_flying_in_air();
        const bool can_float = player_veh->can_float();
        const bool can_drive = player_veh->valid_wheel_config();
        // TODO: check engines/fuel
        if( can_fly ) {
            params = overmap_path_params::for_aircraft();
        } else if( can_float && !can_drive ) {
            params = overmap_path_params::for_watercraft();
        } else if( can_drive ) {
            const float offroad_coeff = player_veh->k_traction(
                                            player_veh->wheel_area() * player_veh->average_or_rating() );
            const bool tiny = player_veh->get_points().size() <= 3;
            params = overmap_path_params::for_land_vehicle( offroad_coeff, tiny, can_float );
        } else {
            return {};
        }
    } else {
        params = overmap_path_params::for_player();
        const oter_id dest_ter = ACTIVE_OVERMAP_BUFFER.ter_existing( dest );
        // already in water or going to a water tile
        if( here.has_flag( "SWIMMABLE", player_character.bub_pos() ) || is_river_or_lake( dest_ter ) ) {
            params.water_cost = 100;
        }
    }
    // literal "edge" case: the vehicle may be in a different OMT than the player
    const tripoint_abs_omt start_omt_pos =
        driving ? project_to<coords::omt>( player_veh->abs_sm_pos ) : player_omt_pos;
    if( dest == player_omt_pos || dest == start_omt_pos ) {
        return {};
    } else {
        return ACTIVE_OVERMAP_BUFFER.get_travel_path( start_omt_pos, dest, params );
    }
}

static float overmap_zoom_level = DEFAULT_TILESET_ZOOM;

static tripoint_abs_omt display(
    const tripoint_abs_omt& orig, const draw_data_t &data = draw_data_t() )
{
    const float previous_zoom = g->get_zoom();
    g->set_zoom( overmap_zoom_level );
    on_out_of_scope reset_zoom( [&]() {
        overmap_zoom_level = g->get_zoom();
        g->set_zoom( previous_zoom );
        g->mark_main_ui_adaptor_resize();
    } );

    background_pane bg_pane;

    // Graphical overmap: tiles render into the lit world layer (world_target),
    // but bg_pane's fullscreen black erase of stdscr would composite OVER that
    // layer in Pass B and hide the tiles (pitch-black map). Flag stdscr's
    // backdrop transparent so suppress_cell_bg() skips its pure-black cells and
    // the overmap tiles show through. The legend (w_omlegend) is a separate,
    // opaque window so it is unaffected, and disable_uis_below still hides the
    // game UI. Restored on exit via RAII so the main-menu / loading background
    // panes keep their solid black.
    cata_cursesport::set_window_transparent_backdrop( catacurses::stdscr, true );
    on_out_of_scope restore_stdscr_backdrop( []() {
        cata_cursesport::set_window_transparent_backdrop( catacurses::stdscr, false );
    } );

    ui_adaptor ui;
    ui.on_screen_resize( []( ui_adaptor & ui ) {
        /**
         * Handle possibly different overmap font size
         */
        OVERMAP_LEGEND_WIDTH = clamp( TERMX / 5, 28, 55 );
        OVERMAP_WINDOW_HEIGHT = TERMY;
        OVERMAP_WINDOW_WIDTH = TERMX - OVERMAP_LEGEND_WIDTH;
        OVERMAP_WINDOW_TERM_WIDTH = OVERMAP_WINDOW_WIDTH;
        OVERMAP_WINDOW_TERM_HEIGHT = OVERMAP_WINDOW_HEIGHT;

        to_overmap_font_dimension( OVERMAP_WINDOW_WIDTH, OVERMAP_WINDOW_HEIGHT );

        g->w_omlegend = catacurses::newwin(
                            OVERMAP_WINDOW_TERM_HEIGHT, OVERMAP_LEGEND_WIDTH, point( OVERMAP_WINDOW_TERM_WIDTH, 0 ) );
        g->w_overmap = catacurses::newwin( OVERMAP_WINDOW_HEIGHT, OVERMAP_WINDOW_WIDTH, point_zero );

        ui.position_from_window( catacurses::stdscr );
    } );
    ui.mark_resize();

    tripoint_abs_omt ret = overmap::invalid_tripoint;
    tripoint_abs_omt curs( orig );

    if( data.select != tripoint_abs_omt( -1, -1, -1 ) ) { curs = data.select; }
    // Configure input context for navigating the map.
    input_context ictxt( "OVERMAP" );
    ictxt.register_action( "ANY_INPUT" );
    ictxt.register_directions();
    ictxt.register_action( "CONFIRM" );
    ictxt.register_action( "LEVEL_UP" );
    ictxt.register_action( "LEVEL_DOWN" );
    ictxt.register_action( "ZOOM_OUT" );
    ictxt.register_action( "ZOOM_IN" );
    ictxt.register_action( "HELP_KEYBINDINGS" );
    ictxt.register_action( "MOUSE_MOVE" );
    ictxt.register_action( "SELECT" );
    ictxt.register_action( "CHOOSE_DESTINATION" );

    // Actions whose keys we want to display.
    ictxt.register_action( "CENTER" );
    ictxt.register_action( "CREATE_NOTE" );
    ictxt.register_action( "DELETE_NOTE" );
    ictxt.register_action( "SEARCH" );
    ictxt.register_action( "LIST_NOTES" );
    ictxt.register_action( "TOGGLE_MAP_NOTES" );
    ictxt.register_action( "TOGGLE_BLINKING" );
    ictxt.register_action( "TOGGLE_OVERLAYS" );
    ictxt.register_action( "TOGGLE_HORDES" );
    ictxt.register_action( "TOGGLE_LAND_USE_CODES" );
    ictxt.register_action( "TOGGLE_CITY_LABELS" );
    ictxt.register_action( "TOGGLE_EXPLORED" );
    ictxt.register_action( "TOGGLE_MARK_PATH" );
    ictxt.register_action( "TOGGLE_FAST_SCROLL" );
    ictxt.register_action( "TOGGLE_OVERMAP_WEATHER" );
    ictxt.register_action( "TOGGLE_FOREST_TRAILS" );
    ictxt.register_action( "MISSIONS" );
    ictxt.register_action( "TOGGLE_DEFAULT_0" );
    ictxt.register_action( "SET_CUSTOM_WAYPOINT" );

    if( data.debug_editor ) {
        ictxt.register_action( "PLACE_TERRAIN" );
        ictxt.register_action( "PLACE_SPECIAL" );
        ictxt.register_action( "SET_SPECIAL_ARGS" );
    }
    ictxt.register_action( "QUIT" );
#ifdef COOP_ENABLED
    ictxt.register_action( "CO_OP_MARK_OVERMAP" );
#endif
    std::string action;
    bool show_explored = true;
    bool fast_scroll = false; /* fast scroll state should reset every time overmap UI is opened */
    int fast_scroll_offset = get_option<int>( "FAST_SCROLL_OFFSET" );
    std::optional<tripoint_bub_ms> mouse_pos;
    std::chrono::time_point<std::chrono::steady_clock> last_blink =
        std::chrono::steady_clock::now();
    grids_draw_data grids_data;
    if( uistate.overmap_default_0 ) { curs.z() = 0; }

    auto sidebar = std::make_unique<om_sidebar_session>();
    rml_doc rml;
    ui.on_redraw( [&]( ui_adaptor & ui ) {
        draw( ui, curs, orig, uistate.overmap_show_overlays, show_explored, fast_scroll, &ictxt,
              data, grids_data, rml ? sidebar.get() : nullptr );
    } );

    rml.open( overmap_rmlui_enabled(), "overmap", ictxt, [&]( Rml::DataModelConstructor & c ) {
        c.Bind( "info_rml", &sidebar->info_rml );
        c.Bind( "hints_rml", &sidebar->hints_rml );
        c.Bind( "footer_rml", &sidebar->footer_rml );
        sidebar->handle = c.GetModelHandle();
    } );

    do {

        ui_manager::redraw();
        int scroll_timeout = get_option<int>( "EDGE_SCROLL" );
        // If EDGE_SCROLL is disabled, it will have a value of -1.
        // blinking won't work if handle_input() is passed a negative integer.
        if( scroll_timeout < 0 ) { scroll_timeout = get_option<int>( "BLINK_SPEED" ); }
        action = ictxt.handle_input( scroll_timeout );
        if( const std::optional<tripoint_rel_ms> vec = ictxt.get_direction( action ) ) {
            int scroll_d = fast_scroll ? fast_scroll_offset : 1;
            curs += vec->xy().raw() * scroll_d;
        } else if( action == "MOUSE_MOVE" || action == "TIMEOUT" ) {
            auto edge_scroll = g->mouse_edge_scrolling_overmap( ictxt );
            if( edge_scroll != tripoint_rel_omt::zero() ) {
                if( action == "MOUSE_MOVE" ) { edge_scroll += edge_scroll; }
                curs += edge_scroll;
            }
        } else if( action == "SELECT" && ( mouse_pos = ictxt.get_coordinates( g->w_overmap ) ) ) {
            curs += mouse_pos->xy().raw();
        } else if( action == "CENTER" ) {
            curs = orig;
        } else if( action == "LEVEL_DOWN" && curs.z() > -OVERMAP_DEPTH ) {
            curs.z() -= 1;
        } else if( action == "LEVEL_UP" && curs.z() < OVERMAP_HEIGHT ) {
            curs.z() += 1;
        } else if( action == "ZOOM_OUT" ) {
            g->zoom_out_overmap();
            ui.mark_resize();
        } else if( action == "ZOOM_IN" ) {
            g->zoom_in_overmap();
            ui.mark_resize();
        } else if( action == "CONFIRM" ) {
            ret = curs;
        } else if( action == "QUIT" ) {
            ret = overmap::invalid_tripoint;
        } else if( action == "CREATE_NOTE" ) {
            create_note( curs );
        } else if( action == "DELETE_NOTE" ) {
            if( ACTIVE_OVERMAP_BUFFER.has_note( curs ) && query_yn( _( "Really delete note?" ) ) ) {
                ACTIVE_OVERMAP_BUFFER.delete_note( curs );
            }
        } else if( action == "LIST_NOTES" ) {
            const tripoint_abs_omt p = show_notes_manager( curs );
            if( p != tripoint_abs_omt( tripoint_min ) ) { curs = p; }
        } else if( action == "CHOOSE_DESTINATION" ) {
            avatar& player_character = get_avatar();
            const bool driving =
                player_character.in_vehicle && player_character.controlling_vehicle;
            std::vector<tripoint_abs_omt> path = get_overmap_path_to( curs, driving );
            bool same_path_selected = false;
            if( path == player_character.omt_path ) {
                same_path_selected = true;
            } else {
                player_character.omt_path.swap( path );
            }
            if( same_path_selected && !player_character.omt_path.empty() ) {
                std::string confirm_msg;
                if( !driving
                    && player_character.weight_carried() > player_character.weight_capacity() ) {
                    confirm_msg = _(
                                      "You are overburdened, are you sure you want to travel (it may "
                                      "be painful)?" );
                } else if( !driving && player_character.in_vehicle ) {
                    confirm_msg = _(
                                      "You are in a vehicle but not driving.  Are you sure you want "
                                      "to walk?" );
                } else if( driving ) {
                    confirm_msg = _( "Drive to this point?" );
                } else {
                    confirm_msg = _( "Travel to this point?" );
                }
                if( query_yn( confirm_msg ) ) {
                    if( driving ) {
                        player_character.assign_activity( std::make_unique<player_activity>(
                                                              std::make_unique<autodrive_activity_actor>() ) );
                    } else {
                        player_character.reset_move_mode();
                        player_character.assign_activity( std::make_unique<player_activity>(
                                                              std::make_unique<travelling_activity_actor>() ) );
                    }
                    action = "QUIT";
                }
            }
        } else if( action == "TOGGLE_BLINKING" ) {
            uistate.overmap_blinking = !uistate.overmap_blinking;
            // if we turn off overmap blinking, show overlays and explored status
            if( !uistate.overmap_blinking ) {
                uistate.overmap_show_overlays = true;
            } else {
                show_explored = true;
            }
        } else if( action == "TOGGLE_OVERLAYS" ) {
            // if we are currently blinking, turn blinking off.
            if( uistate.overmap_blinking ) {
                uistate.overmap_blinking = false;
                uistate.overmap_show_overlays = false;
                show_explored = false;
            } else {
                uistate.overmap_show_overlays = !uistate.overmap_show_overlays;
                show_explored = !show_explored;
            }
        } else if( action == "TOGGLE_LAND_USE_CODES" ) {
            uistate.overmap_show_land_use_codes = !uistate.overmap_show_land_use_codes;
        } else if( action == "TOGGLE_MAP_NOTES" ) {
            uistate.overmap_show_map_notes = !uistate.overmap_show_map_notes;
        } else if( action == "TOGGLE_HORDES" ) {
            uistate.overmap_show_hordes = !uistate.overmap_show_hordes;
        } else if( action == "TOGGLE_CITY_LABELS" ) {
            uistate.overmap_show_city_labels = !uistate.overmap_show_city_labels;
        } else if( action == "TOGGLE_EXPLORED" ) {
            ACTIVE_OVERMAP_BUFFER.toggle_explored( curs );
            uistate.overmap_highlighted_omts.erase( curs );
        } else if( action == "TOGGLE_MARK_PATH" ) {
            ACTIVE_OVERMAP_BUFFER.toggle_path( curs );
        } else if( action == "TOGGLE_OVERMAP_WEATHER" ) {
            uistate.overmap_visible_weather = !uistate.overmap_visible_weather;
        } else if( action == "TOGGLE_FAST_SCROLL" ) {
            fast_scroll = !fast_scroll;
        } else if( action == "TOGGLE_FOREST_TRAILS" ) {
            uistate.overmap_show_forest_trails = !uistate.overmap_show_forest_trails;
        } else if( action == "TOGGLE_DEFAULT_0" ) {
            if( uistate.overmap_default_0 ) {
                curs.z() = orig.z();
            } else {
                curs.z() = 0;
            }
            uistate.overmap_default_0 = !uistate.overmap_default_0;
        } else if( action == "SET_CUSTOM_WAYPOINT" ) {
            avatar& player_character = get_avatar();
            if( player_character.custom_waypoint != nullptr ) {
                player_character.custom_waypoint = nullptr;
            } else {
                player_character.custom_waypoint = std::make_unique<tripoint_abs_omt>( curs );
            }
        } else if( action == "SEARCH" ) {
            if( !search( ui, curs, orig ) ) { continue; }
        } else if( action == "PLACE_TERRAIN" || action == "PLACE_SPECIAL" ) {
            place_ter_or_special( ui, curs, action );
        } else if( action == "SET_SPECIAL_ARGS" ) {
            set_special_args( curs );
        } else if( action == "MISSIONS" ) {
            g->list_missions();
        }
#ifdef COOP_ENABLED
        else if( action == "CO_OP_MARK_OVERMAP" && coop_session::get().is_coop() ) {
            std::ostringstream pkt;
            JsonOut jp( pkt );
            jp.start_object();
            jp.member( "t", static_cast<int>( coop_pkt::overmap_mark ) );
            jp.member( "d" );
            jp.start_object();
            jp.member( "omx", curs.x() );
            jp.member( "omy", curs.y() );
            jp.member( "omz", curs.z() );
            jp.member( "label", std::string( "Meet here" ) );
            jp.member( "clear", false );
            jp.end_object();
            jp.end_object();
            const std::string pkt_str = pkt.str();
            auto& sess = coop_session::get();
            if( sess.is_client() && g->coop_client_ ) {
                g->coop_client_->send_raw( pkt_str );
            } else if( sess.is_host() && g->coop_server_ ) {
                g->coop_server_->send_raw( pkt_str );
            }
            // Update local session immediately so the marker appears without round-trip delay.
            sess.shared_mark = curs;
            sess.shared_mark_label = "Meet here";
        }
#endif // COOP_ENABLED

        std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
        if( now > last_blink + std::chrono::milliseconds( get_option<int>( "BLINK_SPEED" ) ) ) {
            if( uistate.overmap_blinking ) {
                uistate.overmap_show_overlays = !uistate.overmap_show_overlays;
            }
            last_blink = now;
        }
    } while( action != "QUIT" && action != "CONFIRM" );
    return ret;
}

} // namespace overmap_ui

void ui::omap::display()
{
    overmap_ui::display( get_player_character().abs_omt_pos(), overmap_ui::draw_data_t() );
}

void ui::omap::display_hordes()
{
    overmap_ui::draw_data_t data;
    uistate.overmap_debug_mongroup = true;
    overmap_ui::display( get_player_character().abs_omt_pos(), data );
    uistate.overmap_debug_mongroup = false;
}

void ui::omap::display_weather()
{
    overmap_ui::draw_data_t data;
    uistate.overmap_debug_weather = true;
    tripoint_abs_omt pos = get_player_character().abs_omt_pos();
    pos.z() = 10;
    overmap_ui::display( pos, data );
    uistate.overmap_debug_weather = false;
}

void ui::omap::display_visible_weather()
{
    overmap_ui::draw_data_t data;
    uistate.overmap_visible_weather = true;
    tripoint_abs_omt pos = get_player_character().abs_omt_pos();
    pos.z() = 10;
    overmap_ui::display( pos, data );
    uistate.overmap_visible_weather = false;
}

void ui::omap::display_scents()
{
    overmap_ui::draw_data_t data;
    data.debug_scent = true;
    overmap_ui::display( get_player_character().abs_omt_pos(), data );
}

void ui::omap::display_distribution_grids()
{
    overmap_ui::draw_data_t data;
    data.debug_grids = true;
    overmap_ui::display( g->u.abs_omt_pos(), data );
}

void ui::omap::display_editor()
{
    overmap_ui::draw_data_t data;
    data.debug_editor = true;
    overmap_ui::display( get_player_character().abs_omt_pos(), data );
}

void ui::omap::display_zones(
    const tripoint_abs_omt& center, const tripoint_abs_omt& select, const int iZoneIndex )
{
    overmap_ui::draw_data_t data;
    data.select = select;
    data.iZoneIndex = iZoneIndex;
    overmap_ui::display( center, data );
}

tripoint_abs_omt ui::omap::choose_point()
{
    return overmap_ui::display( get_player_character().abs_omt_pos() );
}

tripoint_abs_omt ui::omap::choose_point( const tripoint_abs_omt& origin )
{
    return overmap_ui::display( origin );
}

tripoint_abs_omt ui::omap::choose_point( int z )
{
    tripoint_abs_omt loc = get_player_character().abs_omt_pos();
    loc.z() = z;
    return overmap_ui::display( loc );
}
