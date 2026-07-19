#include "bionics_ui.h"

#include <algorithm> //std::min
#include <array>
#include <cstddef>
#include <memory>

#include "bionics.h"
#include "catacharset.h"
#include "cata_utility.h"
#include "character.h"
#include "color.h"
#include "enum_conversions.h"
#include "flat_set.h"
#include "game.h"
#include "input.h"
#include "inventory.h"
#include "make_static.h"
#include "options.h"
#include "output.h"
#include "string_formatter.h"
#include "string_id.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "uistate.h"
#include "units.h"

#include <RmlUi/Core.h>

#include "rml_screen.h"
#include "rml_util.h"

static const std::string flag_SAFE_FUEL_OFF( "SAFE_FUEL_OFF" );
static const flag_id flag_MULTIINSTALL( "MULTIINSTALL" );

// '!', '-' and '=' are uses as default bindings in the menu
const invlet_wrapper
bionic_chars( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\"#&()*+./:;@[\\]^_{|}" );

namespace
{
enum bionic_tab_mode {
    TAB_ACTIVE,
    TAB_PASSIVE
};
enum bionic_menu_mode {
    ACTIVATING,
    EXAMINING,
    REASSIGNING
};

std::string sort_mode_str( bionic_ui_sort_mode mode )
{
    switch( mode ) {
        case bionic_ui_sort_mode::nsort:
        case bionic_ui_sort_mode::NONE:
            return _( "None" );
        case bionic_ui_sort_mode::POWER:
            return _( "Power usage" );
        case bionic_ui_sort_mode::NAME:
            return _( "Name" );
        case bionic_ui_sort_mode::INVLET:
            return _( "Manual (shortcut)" );
    }
    return "error";
}

bool is_power_cbm( const bionic_data &data )
{
    return !data.fuel_opts.empty() || data.is_remote_fueled;
}

units::energy bionic_sort_power( const bionic_data &lbd )
{
    return is_power_cbm( lbd ) ? units::energy( -1_kJ * lbd.fuel_efficiency ) :
           lbd.power_activate + lbd.power_over_time;
}

using sorted_bionics = cata::flat_set<bionic *, bionic_sort_less>;

sorted_bionics filtered_bionics( bionic_collection &all_bionics,
                                 bionic_tab_mode mode )
{
    const auto less = bionic_sort_less{uistate.bionic_sort_mode};
    sorted_bionics filtered_entries( less );
    std::set<bionic_id> displayed_bionics;
    for( auto &elem : all_bionics ) {
        if( ( mode == TAB_ACTIVE ) == elem.id->activated && !displayed_bionics.contains( elem.id ) ) {
            filtered_entries.insert( &elem );
            displayed_bionics.insert( elem.id );
        }
    }
    return filtered_entries;
}

bionic_ui_sort_mode pick_sort_mode()
{
    uilist tmenu;
    tmenu.text = _( "Sort bionics by:" );
    tmenu.addentry( 1, true, 'p', sort_mode_str( bionic_ui_sort_mode::POWER ) );
    tmenu.addentry( 2, true, 'n', sort_mode_str( bionic_ui_sort_mode::NAME ) );
    tmenu.addentry( 3, true, 'i', sort_mode_str( bionic_ui_sort_mode::INVLET ) );
    tmenu.addentry( 4, true, 'o', sort_mode_str( bionic_ui_sort_mode::NONE ) );

    tmenu.query();
    switch( tmenu.ret ) {
        case 1:
            return bionic_ui_sort_mode::POWER;
        case 2:
            return bionic_ui_sort_mode::NAME;
        case 3:
            return bionic_ui_sort_mode::INVLET;
        case 4:
            return bionic_ui_sort_mode::NONE;
    }

    return bionic_ui_sort_mode::NONE;
}

} // namespace

bool bionic_sort_less::operator()( const bionic &lhs, const bionic &rhs ) const
{
    const bionic_data &lbd = lhs.info();
    const bionic_data &rbd = rhs.info();

    switch( mode ) {
        case bionic_ui_sort_mode::nsort:
        case bionic_ui_sort_mode::NONE:
            //use installation order
            return true;
        case bionic_ui_sort_mode::INVLET:
            return lhs.invlet < rhs.invlet;
        case bionic_ui_sort_mode::POWER: {
            units::energy lbd_sort_power = bionic_sort_power( lbd );
            units::energy rbd_sort_power = bionic_sort_power( rbd );
            if( lbd_sort_power != rbd_sort_power ) {
                return lbd_sort_power < rbd_sort_power;
            }
        }
        /* fallthrough */
        case bionic_ui_sort_mode::NAME:
            return localized_compare( lbd.name.translated(), rbd.name.translated() );
    }
    return false;
}

namespace io
{
template<>
std::string enum_to_string<bionic_ui_sort_mode>( bionic_ui_sort_mode mode )
{
    switch( mode ) {
        case bionic_ui_sort_mode::nsort:
        case bionic_ui_sort_mode::NONE:
            return "none";
        case bionic_ui_sort_mode::POWER:
            return "power";
        case bionic_ui_sort_mode::NAME:
            return "name";
        case bionic_ui_sort_mode::INVLET:
            return "invlet";
    }

    return "error";
}
} // namespace io

static bionic *bionic_by_invlet( bionic_collection &bionics, const int ch )
{
    // space is a special case for unassigned
    if( ch == ' ' ) {
        return nullptr;
    }

    for( auto &elem : bionics ) {
        if( elem.invlet == ch ) {
            return &elem;
        }
    }
    return nullptr;
}

char get_free_invlet( bionic_collection &bionics )
{
    for( auto &inv_char : bionic_chars ) {
        if( bionic_by_invlet( bionics, inv_char ) == nullptr ) {
            return inv_char;
        }
    }
    return ' ';
}

// ── Titlebar text builders ────────────────────────────────────────────────
// Pure text, extracted so the RmlUi render path and the curses titlebar share
// ONE source of truth (no drift). The curses draw_bionics_titlebar keeps its own
// border-glyph drawing + positioning and just sources its text from these.

static std::string bionics_fuel_text( Character *who )
{
    static const flag_id json_flag_PERPETUAL( "PERPETUAL" );
    std::string fuel_string = _( "Available Fuel: " );
    bool found_fuel = false;
    for( const bionic &bio : *who->my_bionics ) {
        for( const itype_id &fuel : who->get_fuel_available( bio.id ) ) {
            found_fuel = true;
            //TODO!: figure out tname so we don't need this, it's an infinite one
            const item &temp_fuel = *item::spawn_temporary( fuel );
            if( temp_fuel.has_flag( json_flag_PERPETUAL ) ) {
                if( fuel == itype_id( "sunlight" ) && !g->is_in_sunlight( who->bub_pos() ) ) {
                    continue;
                }
                fuel_string += colorize( temp_fuel.tname(), c_green ) + " ";
                continue;
            }
            fuel_string += temp_fuel.tname() + ": " + colorize( who->get_value( fuel.str() ),
                           c_green ) + "/" + std::to_string( who->get_total_fuel_capacity( fuel ) ) + " ";
        }
        if( bio.info().is_remote_fueled && who->has_active_bionic( bio.id ) ) {
            const itype_id rem_fuel = who->find_remote_fuel( true );
            if( !rem_fuel.is_empty() ) {
                const item &tmp_rem_fuel = *item::spawn_temporary( rem_fuel );
                if( tmp_rem_fuel.has_flag( json_flag_PERPETUAL ) ) {
                    fuel_string += colorize( tmp_rem_fuel.tname(), c_green ) + " ";
                } else {
                    fuel_string += tmp_rem_fuel.tname() + ": " + colorize( who->get_value( "rem_" + rem_fuel.str() ),
                                   c_green ) + " ";
                }
                found_fuel = true;
            }
        }
    }
    return found_fuel ? fuel_string : std::string();
}

static std::string bionics_power_markup( Character *who )
{
    std::string power_string;
    const int curr_power = units::to_joule( who->get_power_level() );
    const int kilo = curr_power / units::to_joule( 1_kJ );
    const int joule = ( curr_power % units::to_joule( 1_kJ ) ) / units::to_joule( 1_J );
    if( kilo > 0 ) {
        power_string = std::to_string( kilo );
        if( joule > 0 ) {
            power_string += pgettext( "decimal separator", "." ) + std::to_string( joule );
        }
        power_string += pgettext( "energy unit: kilojoule", "kJ" );
    } else {
        power_string = std::to_string( joule );
        power_string += pgettext( "energy unit: joule", "J" );
    }
    return string_format(
               _( "Bionic Power: <color_light_blue>%s</color>/<color_light_blue>%ikJ</color>" ),
               power_string, units::to_kilojoule( who->get_max_power_level() ) );
}

static std::string bionics_hints_text( bionic_menu_mode mode, const input_context &ctxt )
{
    std::string desc_append = string_format(
                                  _( "[<color_yellow>%s</color>] Reassign, [<color_yellow>%s</color>] Switch tabs, "
                                     "[<color_yellow>%s</color>] Toggle fuel saving mode, "
                                     "[<color_yellow>%s</color>] Toggle sprite visibility, "
                                     "[<color_yellow>%s</color>] Toggle auto start mode." ),
                                  ctxt.get_desc( "REASSIGN" ), ctxt.get_desc( "NEXT_TAB" ), ctxt.get_desc( "TOGGLE_SAFE_FUEL" ),
                                  ctxt.get_desc( "TOGGLE_SPRITE" ),
                                  ctxt.get_desc( "TOGGLE_AUTO_START" ) );
    desc_append += string_format( _( " [<color_yellow>%s</color>] Sort: %s" ), ctxt.get_desc( "SORT" ),
                                  sort_mode_str( uistate.bionic_sort_mode ) );
    if( mode == REASSIGNING ) {
        return _( "Reassigning.  Select a bionic to reassign or press [<color_yellow>SPACE</color>] to cancel." );
    } else if( mode == ACTIVATING ) {
        return string_format( _( "<color_green>Activating</color>  "
                                 "[<color_yellow>%s</color>] Examine, %s" ),
                              ctxt.get_desc( "TOGGLE_EXAMINE" ), desc_append );
    } else if( mode == EXAMINING ) {
        return string_format( _( "<color_light_blue>Examining</color>  "
                                 "[<color_yellow>%s</color>] Activate, %s" ),
                              ctxt.get_desc( "TOGGLE_EXAMINE" ), desc_append );
    }
    return std::string();
}

//builds the power usage string of a given bionic
static std::string build_bionic_poweronly_string( const bionic &bio )
{
    const bionic_data &bio_data = bio.id.obj();
    std::vector<std::string> properties;

    if( bio_data.power_activate > 0_kJ ) {
        properties.push_back( string_format( _( "%s act" ),
                                             units::display( bio_data.power_activate ) ) );
    }
    if( bio_data.power_deactivate > 0_kJ ) {
        properties.push_back( string_format( _( "%s deact" ),
                                             units::display( bio_data.power_deactivate ) ) );
    }
    if( bio_data.power_trigger > 0_kJ ) {
        properties.push_back( string_format( _( "%s trigger" ),
                                             units::display( bio_data.power_trigger ) ) );
    }
    if( bio_data.kcal_trigger > 0 ) {
        properties.push_back( string_format( _( "%i kcal trigger" ),
                                             bio_data.kcal_trigger ) );
    }
    if( bio_data.charge_time > 0 && bio_data.power_over_time > 0_kJ ) {
        properties.push_back( bio_data.charge_time == 1
                              ? string_format( _( "%s/turn" ), units::display( bio_data.power_over_time ) )
                              : string_format( _( "%s/%d turns" ), units::display( bio_data.power_over_time ),
                                               bio_data.charge_time ) );
    }
    if( bio_data.has_flag( STATIC( flag_id( "BIONIC_TOGGLED" ) ) ) ) {
        properties.emplace_back( bio.powered ? _( "ON" ) : _( "OFF" ) );
    }
    if( bio.incapacitated_time > 0_turns ) {
        properties.emplace_back( _( "(incapacitated)" ) );
    }
    if( !bio.show_sprite ) {
        properties.emplace_back( _( "(hidden)" ) );
    }
    if( !bio.has_flag( flag_SAFE_FUEL_OFF ) && ( !bio.info().fuel_opts.empty() ||
            bio.info().is_remote_fueled ) ) {
        properties.emplace_back( _( "(fuel saving ON)" ) );
    }
    if( bio.is_auto_start_on() && ( !bio.info().fuel_opts.empty() || bio.info().is_remote_fueled ) ) {
        const std::string label = string_format( _( "(auto start < %d %%)" ),
                                  static_cast<int>( bio.get_auto_start_thresh() * 100 ) );
        properties.push_back( label );
    }

    return enumerate_as_string( properties, enumeration_conjunction::none );
}

//generates the string that show how much power a bionic uses
static std::string build_bionic_powerdesc_string( const bionic &bio )
{
    std::string power_desc;
    const std::string power_string = build_bionic_poweronly_string( bio );
    power_desc += bio.id->name.translated();
    if( !power_string.empty() ) {
        power_desc += ", " + power_string;
    }
    return power_desc;
}

//get a text color depending on the power/powering state of the bionic
nc_color get_bionic_text_color( const bionic &bio, const bool isHighlightedBionic )
{
    nc_color type = c_white;
    bool is_power_source = bio.id->has_flag( STATIC( flag_id( "BIONIC_POWER_SOURCE" ) ) );
    if( bio.id->activated ) {
        if( isHighlightedBionic ) {
            if( bio.powered && !is_power_source ) {
                type = h_red;
            } else if( is_power_source && !bio.powered ) {
                type = h_light_cyan;
            } else if( is_power_source && bio.powered ) {
                type = h_light_green;
            } else {
                type = h_light_red;
            }
        } else {
            if( bio.powered && !is_power_source ) {
                type = c_red;
            } else if( is_power_source && !bio.powered ) {
                type = c_light_cyan;
            } else if( is_power_source && bio.powered ) {
                type = c_light_green;
            } else {
                type = c_light_red;
            }
        }
    } else {
        if( isHighlightedBionic ) {
            if( is_power_source ) {
                type = h_light_cyan;
            } else {
                type = h_cyan;
            }
        } else {
            if( is_power_source ) {
                type = c_light_cyan;
            } else {
                type = c_cyan;
            }
        }
    }
    return type;
}

// ── RmlUi render path (full UI→RmlUi migration, Tier 2 screen #2) ─────────────
// Tabs (ACTIVE/PASSIVE) + a single list of the current tab's bionics + an examine
// pane, via the F.3 rml_doc harness. 4th rml_doc consumer; first user of the
// shared theme .tabs/.tab component. Row colour + "> "/"• " marker baked into the
// bound text; the titlebar (power + fuel + hints) reuses the bionics_*_text
// builders. Modes/toggles + activation stay on input_context (keyboard).
namespace
{
struct bionic_rml_row {
    Rml::String text_rml;
    bool selected = false;
};
struct bionic_rml_session {
    Rml::Vector<bionic_rml_row> rows;     // current tab's list
    bool active_tab = false;              // tab_mode == TAB_ACTIVE
    Rml::String active_tab_label_rml;     // "ACTIVE (n)"
    Rml::String passive_tab_label_rml;    // "PASSIVE (n)"
    bool empty = false;
    Rml::String empty_rml;
    Rml::String title_rml;                // power + fuel + mode hints
    bool examining = false;
    Rml::String examine_rml;              // bionic description (examine mode)
    Rml::DataModelHandle handle;
};

bool g_bionics_types_registered = false;

void register_bionics_rml_types( Rml::DataModelConstructor &c )
{
    if( g_bionics_types_registered ) {
        return;
    }
    Rml::StructHandle<bionic_rml_row> rh = c.RegisterStruct<bionic_rml_row>();
    rh.RegisterMember( "text_rml", &bionic_rml_row::text_rml );
    rh.RegisterMember( "selected", &bionic_rml_row::selected );
    c.RegisterArray<Rml::Vector<bionic_rml_row>>();
    g_bionics_types_registered = true;
}
} // namespace

bool &bionics_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

void show_bionics_ui( Character &who )
{
    bionic_collection &bionics = *who.my_bionics;
    sorted_bionics passive = filtered_bionics( bionics, TAB_PASSIVE );
    sorted_bionics active = filtered_bionics( bionics, TAB_ACTIVE );
    bionic *bio_last = nullptr;
    bionic_tab_mode tab_mode = TAB_ACTIVE;

    //added title_tab_height for the tabbed bionic display
    const int TITLE_HEIGHT = 4;
    const int TITLE_TAB_HEIGHT = 3;
    int HEIGHT = 0;
    int WIDTH = 0;
    int LIST_HEIGHT = 0;
    int list_start_y = 0;
    int half_list_view_location = 0;
    catacurses::window wBio;
    catacurses::window w_description;
    catacurses::window w_title;
    catacurses::window w_tabs;

    bool hide = false;
    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        if( hide ) {
            ui.position( point_zero, point_zero );
            return;
        }
        // Main window
        /** Total required height is:
         * top frame line:                                         + 1
         * height of title window:                                 + TITLE_HEIGHT
         * height of tabs:                                         + TITLE_TAB_HEIGHT
         * height of the biggest list of active/passive bionics:   + bionic_count
         * bottom frame line:                                      + 1
         * TOTAL: TITLE_HEIGHT + TITLE_TAB_HEIGHT + bionic_count + 2
         */
        HEIGHT = std::min( TERMY,
                           std::max( FULL_SCREEN_HEIGHT,
                                     TITLE_HEIGHT + TITLE_TAB_HEIGHT +
                                     static_cast<int>( bionics.size() ) + 2 ) );
        WIDTH = FULL_SCREEN_WIDTH + ( TERMX - FULL_SCREEN_WIDTH ) / 2;
        const point START( ( TERMX - WIDTH ) / 2, ( TERMY - HEIGHT ) / 2 );
        //wBio is the entire bionic window
        wBio = catacurses::newwin( HEIGHT, WIDTH, START );

        LIST_HEIGHT = HEIGHT - TITLE_HEIGHT - TITLE_TAB_HEIGHT - 2;

        const int DESCRIPTION_WIDTH = WIDTH - 2 - 40;
        const int DESCRIPTION_START_Y = START.y + TITLE_HEIGHT + TITLE_TAB_HEIGHT + 1;
        const int DESCRIPTION_START_X = START.x + 1 + 40;
        //w_description is the description panel that is controlled with ! key
        w_description = catacurses::newwin( LIST_HEIGHT, DESCRIPTION_WIDTH,
                                            point( DESCRIPTION_START_X, DESCRIPTION_START_Y ) );

        // Title window
        const int TITLE_START_Y = START.y + 1;
        const int HEADER_LINE_Y = TITLE_HEIGHT + TITLE_TAB_HEIGHT;
        w_title = catacurses::newwin( TITLE_HEIGHT, WIDTH - 2, START + point_east );

        const int TAB_START_Y = TITLE_START_Y + 3;
        //w_tabs is the tab bar for passive and active bionic groups
        w_tabs = catacurses::newwin( TITLE_TAB_HEIGHT, WIDTH,
                                     point( START.x, TAB_START_Y ) );

        // offset for display: bionic with index i is drawn at y=list_start_y+i
        // drawing the bionics starts with bionic[scroll_position]
        // scroll_position;
        list_start_y = HEADER_LINE_Y;
        half_list_view_location = LIST_HEIGHT / 2;

        ui.position_from_window( wBio );
    } );
    ui.mark_resize();

    int scroll_position = 0;
    int cursor = 0;

    //generate the tab title string and a count of the bionics owned
    bionic_menu_mode menu_mode = ACTIVATING;
    int max_scroll_position = 0;

    input_context ctxt( "BIONICS" );
    ctxt.register_updown();
    ctxt.register_action( "ANY_INPUT" );
    ctxt.register_action( "TOGGLE_EXAMINE" );
    ctxt.register_action( "REASSIGN" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "TOGGLE_SAFE_FUEL" );
    ctxt.register_action( "TOGGLE_SPRITE" );
    ctxt.register_action( "TOGGLE_AUTO_START" );
    ctxt.register_action( "SORT" );

    // ---- RmlUi render path (F.3 rml_doc harness) ----------------------------
    // rml_doc owns the open/guard/16ms-tick/close lifecycle; only the model + this
    // live sync stay here. sync_rml() runs in on_redraw, so the tabs, the current
    // tab's list, the titlebar, and the examine pane track the loop state. Model
    // storage declared BEFORE rml so it outlives the document. RmlUi renders all
    // rows + scrolls natively, so the legacy scroll_position windowing stays a
    // curses-only concern.
    std::unique_ptr<bionic_rml_session> data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml ) {
            return;
        }
        sorted_bionics *cur = ( tab_mode == TAB_ACTIVE ? &active : &passive );

        data->active_tab = ( tab_mode == TAB_ACTIVE );
        data->active_tab_label_rml = cata_text_to_rml( string_format( _( "ACTIVE (%i)" ),
                                     active.size() ) );
        data->passive_tab_label_rml = cata_text_to_rml( string_format( _( "PASSIVE (%i)" ),
                                      passive.size() ) );

        // Titlebar: power + mode hints + fuel (fuel hidden while reassigning).
        std::string fuel = ( menu_mode == REASSIGNING ) ? std::string() : bionics_fuel_text( &who );
        std::string title = bionics_power_markup( &who ) + "\n" + bionics_hints_text( menu_mode, ctxt );
        if( !fuel.empty() ) {
            title += "\n" + fuel;
        }
        data->title_rml = cata_text_to_rml( title );

        data->empty = cur->empty();
        if( cur->empty() ) {
            data->empty_rml = cata_text_to_rml( colorize(
                                                    tab_mode == TAB_ACTIVE ? _( "No activatable bionics installed." )
                                                    : _( "No passive bionics installed." ), c_light_gray ) );
        }

        data->rows.clear();
        for( int i = 0; i < static_cast<int>( cur->size() ); ++i ) {
            const bionic &bio = *( *cur )[i];
            const nc_color col = get_bionic_text_color( bio, false );
            const std::string marker = ( i == cursor ) ? "> " : "• ";
            const std::string desc = string_format( "%c %s", ( *cur )[i]->invlet,
                                                    build_bionic_powerdesc_string( bio ) );
            bionic_rml_row r;
            r.text_rml = cata_text_to_rml( marker + colorize( desc, col ) );
            r.selected = ( i == cursor );
            data->rows.push_back( r );
        }

        data->examining = ( menu_mode == EXAMINING );
        if( data->examining && cursor >= 0 && cursor < static_cast<int>( cur->size() ) ) {
            const bionic &bio = *( *cur )[cursor];
            std::string ex = colorize( string_format( "%s", bio.id->name ), c_white ) + "\n";
            const std::string poweronly = build_bionic_poweronly_string( bio );
            if( !poweronly.empty() ) {
                ex += colorize( string_format( _( "Power usage: %s" ), poweronly ), c_light_gray ) + "\n";
            }
            ex += colorize( string_format( "%s", bio.id->description ), c_light_blue );
            if( bio.info().has_flag( flag_MULTIINSTALL ) ) {
                const int count = who.count_bionic_of_type( bio.id );
                if( count != 1 ) {
                    ex += "\n" + colorize( string_format(
                                               "You have %s instances of this bionic installed.", count ), c_magenta );
                }
            }
            data->examine_rml = cata_text_to_rml( ex );
        } else {
            data->examine_rml.clear();
        }

        data->handle.DirtyVariable( "rows" );
        data->handle.DirtyVariable( "active_tab" );
        data->handle.DirtyVariable( "active_tab_label_rml" );
        data->handle.DirtyVariable( "passive_tab_label_rml" );
        data->handle.DirtyVariable( "empty" );
        data->handle.DirtyVariable( "empty_rml" );
        data->handle.DirtyVariable( "title_rml" );
        data->handle.DirtyVariable( "examining" );
        data->handle.DirtyVariable( "examine_rml" );
    };

    rml.open( bionics_rmlui_enabled(), "bionics", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        data = std::make_unique<bionic_rml_session>();
        register_bionics_rml_types( c );
        c.Bind( "rows", &data->rows );
        c.Bind( "active_tab", &data->active_tab );
        c.Bind( "active_tab_label_rml", &data->active_tab_label_rml );
        c.Bind( "passive_tab_label_rml", &data->passive_tab_label_rml );
        c.Bind( "empty", &data->empty );
        c.Bind( "empty_rml", &data->empty_rml );
        c.Bind( "title_rml", &data->title_rml );
        c.Bind( "examining", &data->examining );
        c.Bind( "examine_rml", &data->examine_rml );
        // Clicking a tab switches it (resets cursor/scroll, like NEXT_TAB);
        // clicking/hovering a row moves the cursor. Keyboard nav unchanged.
        c.BindEventCallback( "on_tab",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            const bionic_tab_mode want = ( idx == 0 ) ? TAB_ACTIVE : TAB_PASSIVE;
            if( want != tab_mode ) {
                tab_mode = want;
                cursor = 0;
                scroll_position = 0;
            }
        } );
        c.BindEventCallback( "on_select",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            sorted_bionics *curl = ( tab_mode == TAB_ACTIVE ? &active : &passive );
            if( idx >= 0 && idx < static_cast<int>( curl->size() ) ) {
                cursor = idx;
            }
        } );
        data->handle = c.GetModelHandle();
    } );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            // Hide the doc during transient activation/targeting (matches the
            // curses `hide` early-return) instead of leaving a stale overlay.
            rml.document()->SetProperty( "visibility", hide ? "hidden" : "visible" );
            if( !hide ) {
                sync_rml();
            }
            return;
        }
    } );

    for( ;; ) {
        ui_manager::redraw();

        //track which list we are looking at
        ::sorted_bionics *current_bionic_list = ( tab_mode == TAB_ACTIVE ? &active : &passive );
        max_scroll_position = std::max( 0, static_cast<int>( current_bionic_list->size() ) - LIST_HEIGHT );
        scroll_position = clamp( scroll_position, 0, max_scroll_position );
        cursor = clamp<int>( cursor, 0, current_bionic_list->size() );

        const std::string action = ctxt.handle_input();
        if( coop_fiber::active() ) {
            active  = filtered_bionics( bionics, TAB_ACTIVE );
            passive = filtered_bionics( bionics, TAB_PASSIVE );
            current_bionic_list = tab_mode == TAB_ACTIVE ? &active : &passive;
            if( !current_bionic_list->empty() ) {
                cursor = std::min( cursor, static_cast<int>( current_bionic_list->size() ) - 1 );
            }
        }
        const int ch = ctxt.get_raw_input().get_first_input();
        bionic *tmp = nullptr;

        if( action == "DOWN" ) {
            if( static_cast<size_t>( cursor ) < current_bionic_list->size() - 1 ) {
                cursor++;
            } else {
                cursor = 0;
            }
            if( scroll_position < max_scroll_position &&
                cursor - scroll_position > LIST_HEIGHT - half_list_view_location ) {
                scroll_position++;
            }
            if( scroll_position > 0 && cursor - scroll_position < half_list_view_location ) {
                scroll_position = std::max( cursor - half_list_view_location, 0 );
            }
        } else if( action == "UP" ) {
            if( cursor > 0 ) {
                cursor--;
            } else {
                cursor = current_bionic_list->size() - 1;
            }
            if( scroll_position > 0 && cursor - scroll_position < half_list_view_location ) {
                scroll_position--;
            }
            if( scroll_position < max_scroll_position &&
                cursor - scroll_position > LIST_HEIGHT - half_list_view_location ) {
                scroll_position =
                    std::max( std::min<int>( current_bionic_list->size() - LIST_HEIGHT,
                                             cursor - half_list_view_location ), 0 );
            }
        } else if( menu_mode == REASSIGNING ) {
            menu_mode = ACTIVATING;

            if( action == "CONFIRM" && !current_bionic_list->empty() ) {
                auto &bio_list = tab_mode == TAB_ACTIVE ? active : passive;
                tmp = bio_list[cursor];
            } else {
                tmp = bionic_by_invlet( bionics, ch );
            }

            if( tmp == nullptr ) {
                // Selected an non-existing bionic (or Escape, or ...)
                continue;
            }
            const int newch = popup_getkey( _( "%s; enter new letter.  Space to clear.  Esc to cancel." ),
                                            tmp->id->name );
            if( newch == ch || newch == KEY_ESCAPE ) {
                continue;
            }
            if( newch == ' ' ) {
                tmp->invlet = ' ';
                continue;
            }
            if( !bionic_chars.valid( newch ) ) {
                popup( _( "Invalid bionic letter.  Only those characters are valid:\n\n%s" ),
                       bionic_chars.get_allowed_chars() );
                continue;
            }
            bionic *otmp = bionic_by_invlet( bionics, newch );
            if( otmp != nullptr ) {
                std::swap( tmp->invlet, otmp->invlet );
            } else {
                tmp->invlet = newch;
            }
            // TODO: show a message like when reassigning a key to an item?
        } else if( action == "NEXT_TAB" ) {
            scroll_position = 0;
            cursor = 0;
            if( tab_mode == TAB_ACTIVE ) {
                tab_mode = TAB_PASSIVE;
            } else {
                tab_mode = TAB_ACTIVE;
            }
        } else if( action == "PREV_TAB" ) {
            scroll_position = 0;
            cursor = 0;
            if( tab_mode == TAB_PASSIVE ) {
                tab_mode = TAB_ACTIVE;
            } else {
                tab_mode = TAB_PASSIVE;
            }
        } else if( action == "REASSIGN" ) {
            menu_mode = REASSIGNING;
        } else if( action == "TOGGLE_EXAMINE" ) {
            // switches between activation and examination
            menu_mode = menu_mode == ACTIVATING ? EXAMINING : ACTIVATING;
        } else if( action == "TOGGLE_SAFE_FUEL" ) {
            auto &bio_list = tab_mode == TAB_ACTIVE ? active : passive;
            if( !current_bionic_list->empty() ) {
                tmp = bio_list[cursor];
                if( !tmp->info().fuel_opts.empty() || tmp->info().is_remote_fueled ) {
                    tmp->toggle_safe_fuel_mod();
                    g->invalidate_main_ui_adaptor();
                } else {
                    popup( _( "You can't toggle fuel saving mode on a non-fueled CBM." ) );
                }
            }
        } else if( action == "TOGGLE_AUTO_START" ) {
            auto &bio_list = tab_mode == TAB_ACTIVE ? active : passive;
            if( !current_bionic_list->empty() ) {
                tmp = bio_list[cursor];
                if( !tmp->info().fuel_opts.empty() || tmp->info().is_remote_fueled ) {
                    tmp->toggle_auto_start_mod();
                    g->invalidate_main_ui_adaptor();
                } else {
                    popup( _( "You can't toggle auto start mode on a non-fueled CBM." ) );
                }
            }

        } else if( action == "TOGGLE_SPRITE" ) {
            auto &bio_list = tab_mode == TAB_ACTIVE ? active : passive;
            if( !current_bionic_list->empty() ) {
                tmp = bio_list[cursor];
                tmp->show_sprite = !tmp->show_sprite;
            }
        } else if( action == "SORT" ) {
            uistate.bionic_sort_mode = pick_sort_mode();
            // FIXME: is there a better way to resort?
            active = filtered_bionics( bionics, TAB_ACTIVE );
            passive = filtered_bionics( bionics, TAB_PASSIVE );
        } else if( action == "CONFIRM" || action == "ANY_INPUT" ) {
            auto &bio_list = tab_mode == TAB_ACTIVE ? active : passive;
            if( action == "CONFIRM" && !current_bionic_list->empty() ) {
                tmp = bio_list[cursor];
            } else {
                tmp = bionic_by_invlet( bionics, ch );
                if( tmp && tmp != bio_last ) {
                    // new bionic selected, update cursor and scroll position
                    int temp_cursor = 0;
                    for( temp_cursor = 0; temp_cursor < static_cast<int>( bio_list.size() ); temp_cursor++ ) {
                        if( bio_list[temp_cursor] == tmp ) {
                            break;
                        }
                    }
                    // if bionic is not found in current list, ignore the attempt to view/activate
                    if( temp_cursor >= static_cast<int>( bio_list.size() ) ) {
                        continue;
                    }
                    //relocate cursor to the bionic that was found
                    cursor = temp_cursor;
                    scroll_position = 0;
                    while( scroll_position < max_scroll_position &&
                           cursor - scroll_position > LIST_HEIGHT - half_list_view_location ) {
                        scroll_position++;
                    }
                }
            }
            if( !tmp ) {
                // entered a key that is not mapped to any bionic
                continue;
            }
            bio_last = tmp;
            const bionic_id &bio_id = tmp->id;
            const bionic_data &bio_data = bio_id.obj();
            if( menu_mode == ACTIVATING ) {
                if( bio_data.activated ) {
                    hide = true;
                    ui.mark_resize();
                    if( tmp->powered ) {
                        who.deactivate_bionic( *tmp );
                    } else {
                        bool close_ui = false;
                        who.activate_bionic( *tmp, false, &close_ui );
                        // Clear the menu if we are firing a bionic gun
                        if( close_ui || tmp->ammo_count > 0 ) {
                            break;
                        }
                    }
                    hide = false;
                    ui.mark_resize();
                    g->invalidate_main_ui_adaptor();
                    if( who.moves < 0 ) {
                        return;
                    }
                    continue;
                } else {
                    popup( _( "You can not activate %s!\n"
                              "To read a description of %s, press '%s'" ), bio_data.name,
                           bio_data.name, ctxt.get_desc( "TOGGLE_EXAMINE" ) );
                }
            } else if( menu_mode == EXAMINING ) {
                // Describing bionics, allow user to jump to description key
                if( action != "CONFIRM" ) {
                    for( size_t i = 0; i < active.size(); i++ ) {
                        if( active[i] == tmp ) {
                            tab_mode = TAB_ACTIVE;
                            cursor = static_cast<int>( i );
                            int max_scroll_check = std::max( 0, static_cast<int>( active.size() ) - LIST_HEIGHT );
                            if( static_cast<int>( i ) > max_scroll_check ) {
                                scroll_position = max_scroll_check;
                            } else {
                                scroll_position = i;
                            }
                            break;
                        }
                    }
                    for( size_t i = 0; i < passive.size(); i++ ) {
                        if( passive[i] == tmp ) {
                            tab_mode = TAB_PASSIVE;
                            cursor = static_cast<int>( i );
                            int max_scroll_check = std::max( 0, static_cast<int>( passive.size() ) - LIST_HEIGHT );
                            if( static_cast<int>( i ) > max_scroll_check ) {
                                scroll_position = max_scroll_check;
                            } else {
                                scroll_position = i;
                            }
                            break;
                        }
                    }
                }
            }
        } else if( action == "QUIT" ) {
            break;
        }
    }

    // Tear down the RmlUi document while the bound `data` is still alive. close()
    // is idempotent and a no-op when the curses path ran; the early `return` on a
    // bionic that spends moves is covered by the rml_doc destructor (rml is
    // declared after data, so it tears down first, while data is still alive).
    rml.close();
}
