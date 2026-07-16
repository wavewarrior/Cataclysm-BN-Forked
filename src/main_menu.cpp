#include "main_menu.h"
#ifdef COOP_ENABLED
#include "coop_menu.h"
#endif

#include "auto_pickup.h"
#include "avatar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "catalua.h"
#include "character_id.h"
#include "color.h"
#include "cursesport.h"
#include "debug.h"
#include "init.h"
#include "distraction_manager.h"
#include "enums.h"
#include "filesystem.h"
#include "fstream_utils.h"
#include "game.h"
#include "game_info.h"
#include "gamemode.h"
#include "get_version.h"
#include "help.h"
#include "loading_ui.h"
#include "mapbuffer.h"
#include "mapsharing.h"
#include "messages.h"
#include "lighting/menu_plexus.h"
#include "newcharacter.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "pldata.h"
#include "popup.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "safemode_ui.h"
#include "scenario.h"
#include "sdltiles.h"
#include "sdlsound.h"
#include "sounds.h"
#include "string_formatter.h"
#include "text_snippets.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "wcwidth.h"
#include "worldfactory.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <functional>
#include <istream>
#include <memory>
#include <optional>

enum class main_menu_opts : int {
    MOTD = 0,
    NEWCHAR = 1,
    LOADCHAR = 2,
    WORLD = 3,
    SETTINGS = 4,
    HELP = 5,
    CREDITS = 6,
    QUIT = 7,
#ifdef COOP_ENABLED
    COOP = 8,
#endif
};
#ifdef COOP_ENABLED
static constexpr int max_menu_opts = 8;
#else
static constexpr int max_menu_opts = 7;
#endif

static int getopt( main_menu_opts o ) { return static_cast<int>( o ); }

namespace
{
point prev_submenu_top_left;
point prev_submenu_size;

// --- RmlUi render path (Tier 4 screen #3: the title screen) -----------------
struct mm_item {
    Rml::String text_rml;
    Rml::String icon_dec;   // RCSS decorator for bindrune nav icon
    bool selected = false;
};
struct mm_session {
    Rml::String title_main_rml;       // "CATACLYSM"
    Rml::String title_sub_rml;        // "Bright Nights"
    Rml::String version_rml;
    Rml::String tips_rml;
    Rml::String keybinds_rml;         // bottom keybind hints
    Rml::String panel_header_rml;     // left panel section header
    Rml::String context_header_rml;   // right panel header
    Rml::String context_body_rml;     // right panel body (tips/preview)
    Rml::Vector<mm_item> items;       // top nav bar
    Rml::Vector<mm_item> submenu;     // left panel action list
    Rml::Vector<mm_item> motd_lines;  // right panel scroll (MOTD/Credits)
    bool show_motd = false;
    bool show_submenu = false;
    Rml::DataModelHandle handle;
};

bool g_mm_types_registered = false;

void register_mm_rml_types( Rml::DataModelConstructor& c )
{
    if( g_mm_types_registered ) { return; }
    Rml::StructHandle<mm_item> ih = c.RegisterStruct<mm_item>();
    ih.RegisterMember( "text_rml", &mm_item::text_rml );
    ih.RegisterMember( "icon_dec", &mm_item::icon_dec );
    ih.RegisterMember( "selected", &mm_item::selected );
    c.RegisterArray<Rml::Vector<mm_item>>();

    g_mm_types_registered = true;
}

// Load / character-select (one world): each row is a saved character + its own
// bindrune sigil decorator (a ?proc:bindrune procedural texture seeded by name).
struct lc_row {
    Rml::String text_rml;
    Rml::String sigil_dec;
    bool selected = false;
};
struct lc_session {
    Rml::Vector<lc_row> rows;
    Rml::String tooltip_rml;
    Rml::DataModelHandle handle;
};

bool g_lc_types_registered = false;

void register_lc_rml_types( Rml::DataModelConstructor& c )
{
    if( g_lc_types_registered ) { return; }
    Rml::StructHandle<lc_row> rh = c.RegisterStruct<lc_row>();
    rh.RegisterMember( "text_rml", &lc_row::text_rml );
    rh.RegisterMember( "sigil_dec", &lc_row::sigil_dec );
    rh.RegisterMember( "selected", &lc_row::selected );
    c.RegisterArray<Rml::Vector<lc_row>>();

    g_lc_types_registered = true;
}

// FNV-1a of the character name → the bindrune seed (the generator's own fnv1a is
// translation-unit-private; one short local copy avoids widening its header).
unsigned lc_name_seed( const std::string& name )
{
    unsigned h = 2166136261u;
    for( const char ch : name ) {
        h ^= static_cast<unsigned char>( ch );
        h *= 16777619u;
    }
    return h;
}
} // namespace

bool &main_menu_rmlui_enabled()
{
    static bool enabled = true;
    return enabled;
}

bool &loadchar_rmlui_enabled()
{
    static bool enabled = true;
    return enabled;
}

void main_menu::on_move() const { sfx::play_variant_sound( "menu_move", "default", 100 ); }

void main_menu::on_error() { sfx::play_variant_sound( "menu_error", "default", 100 ); }

class sound_on_move_uilist_callback: public uilist_callback
{
    private:
        main_menu *mmenu;
        bool first = true;

    public:
        sound_on_move_uilist_callback( main_menu* mmenu ): mmenu( mmenu ) {}

        void select( uilist* ) override {
            if( first ) {
            // Don't emit sound when menu is opened
            first = false;
            return;
        }
        mmenu->on_move();
    }
};

std::vector<std::string> main_menu::load_file(
    const std::string& path, const std::string& alt_text ) const
{
    std::vector<std::string> result;
    read_from_file(
        path,
    [&result]( std::istream & fin ) {
        std::string line;
        while( std::getline( fin, line ) ) {
            if( !line.empty() && line[0] == '#' ) { continue; }
            result.push_back( line );
        }
    },
    true );
    if( result.empty() ) { result.push_back( alt_text ); }
    return result;
}

holiday main_menu::get_holiday_from_time() { return ::get_holiday_from_time( 0, true ); }

void main_menu::init_windows()
{
    if( LAST_TERM == point( TERMX, TERMY ) ) { return; }

    // main window should also expand to use available display space.
    // expanding to evenly use up half of extra space, for now.
    extra_w = ( ( TERMX - FULL_SCREEN_WIDTH ) / 2 ) - 1;
    int extra_h = ( ( TERMY - FULL_SCREEN_HEIGHT ) / 2 ) - 1;
    extra_w = ( extra_w > 0 ? extra_w : 0 );
    extra_h = ( extra_h > 0 ? extra_h : 0 );
    const int total_w = FULL_SCREEN_WIDTH + extra_w;
    const int total_h = FULL_SCREEN_HEIGHT + extra_h;

    // position of window within main display
    const point p0( ( TERMX - total_w ) / 2, ( TERMY - total_h ) / 2 );

    w_open = catacurses::newwin( total_h, total_w, p0 );
    // When the RML render path + plexus is active, the plexus IS the background
    // — no need for the old lit-world bleed-through.  Only enable the transparent
    // backdrop for the legacy curses path.
    cata_cursesport::set_window_transparent_backdrop( w_open, !main_menu_rmlui_enabled() );

    menu_offset.y = total_h - 3;
    // note: if iMenuOffset is changed,
    // please update MOTD and credits to indicate how long they can be.

    LAST_TERM = point( TERMX, TERMY );
}

void main_menu::init_strings()
{
    // ASCII Art
    mmenu_title = load_file( PATH_INFO::title( current_holiday ), _( "Cataclysm: Bright Nights" ) );
    // MOTD
    auto motd = load_file( PATH_INFO::motd(), _( "No message today." ) );

    mmenu_motd.clear();
    for( const std::string& line : motd ) { mmenu_motd += ( line.empty() ? " " : line ) + "\n"; }
    mmenu_motd = colorize( mmenu_motd, c_light_red );
    mmenu_motd_len = foldstring( mmenu_motd, FULL_SCREEN_WIDTH - 2 ).size();

    // Credits
    mmenu_credits.clear();
    read_from_file(
        PATH_INFO::credits(),
    [&]( std::istream & stream ) {
        std::string line;
        while( std::getline( stream, line ) ) {
            if( line[0] != '#' ) { mmenu_credits += ( line.empty() ? " " : line ) + "\n"; }
        }
    },
    true );

    if( mmenu_credits.empty() ) { mmenu_credits = _( "No credits information found." ); }
    mmenu_credits_len = foldstring( mmenu_credits, FULL_SCREEN_WIDTH - 2 ).size();

    // fill menu with translated menu items
    vMenuItems.clear();
    vMenuItems.emplace_back( pgettext( "Main Menu", "<M|m>OTD" ) );
    vMenuItems.emplace_back( pgettext( "Main Menu", "<N|n>ew Game" ) );
    vMenuItems.emplace_back( pgettext( "Main Menu", "Lo<a|A>d" ) );
    vMenuItems.emplace_back( pgettext( "Main Menu", "<W|w>orld" ) );
    vMenuItems.emplace_back( pgettext( "Main Menu", "Se<t|T>tings" ) );
    vMenuItems.emplace_back( pgettext( "Main Menu", "H<e|E|?>lp" ) );
    vMenuItems.emplace_back( pgettext( "Main Menu", "<C|c>redits" ) );
    vMenuItems.emplace_back( pgettext( "Main Menu", "<Q|q>uit" ) );
#ifdef COOP_ENABLED
    vMenuItems.emplace_back( pgettext( "Main Menu", "C<o|O>-op" ) );
#endif

    // new game menu items
    vNewGameSubItems.clear();
    vNewGameSubItems.emplace_back( pgettext( "Main Menu|New Game", "C<u|U>stom Character" ) );
    vNewGameSubItems.emplace_back( pgettext( "Main Menu|New Game", "<P|p>reset Character" ) );
    vNewGameSubItems.emplace_back( pgettext( "Main Menu|New Game", "<R|r>andom Character" ) );
    if( !MAP_SHARING::isSharing() ) {
        // "Play Now" function doesn't play well together with shared maps
        vNewGameSubItems.emplace_back( pgettext(
                                           "Main Menu|New Game",
                                           "Play Now!  (<D|d>efault "
                                           "Scenario)" ) );
        vNewGameSubItems.emplace_back( pgettext( "Main Menu|New Game", "Play N<o|O>w!" ) );

        // Special games don't play well together with shared maps
        vNewGameSubItems.emplace_back( pgettext( "Main Menu|New Game", "<T|t>utorial" ) );
        vNewGameSubItems.emplace_back( pgettext( "Main Menu|New Game", "<D|d>efence mode" ) );
    }
    vNewGameHints.clear();
    vNewGameHints.emplace_back( _(
                                    "Allows you to fully customize points pool, scenario, and "
                                    "character's profession, stats, traits, skills and other "
                                    "parameters." ) );
    vNewGameHints.emplace_back( _( "Select from one of previously created character templates." ) );
    vNewGameHints.emplace_back( _(
                                    "Creates random character, but lets you preview the generated "
                                    "character and the scenario and change character and/or scenario "
                                    "if needed." ) );
    vNewGameHints.emplace_back( _(
                                    "Puts you right in the game, randomly choosing character's "
                                    "traits, profession, skills and other parameters.  Scenario is "
                                    "fixed to Evacuee." ) );
    vNewGameHints.emplace_back( _(
                                    "Puts you right in the game, randomly choosing scenario and "
                                    "character's traits, profession, skills and other parameters." ) );
    vNewGameHints.emplace_back( _(
                                    "Learn controls and basic game mechanics while exploring a small "
                                    "underground complex." ) );
    vNewGameHints.emplace_back( _(
                                    "Defend against waves of incoming enemies.  This game mode hasn't "
                                    "been updated in a while and may contain bugs." ) );
    vNewGameHotkeys.clear();
    vNewGameHotkeys.reserve( vNewGameSubItems.size() );
    for( const std::string& item : vNewGameSubItems ) {
        vNewGameHotkeys.push_back( get_hotkeys( item ) );
    }

    // determine hotkeys from translated menu item text
    vMenuHotkeys.clear();
    for( const std::string& item : vMenuItems ) { vMenuHotkeys.push_back( get_hotkeys( item ) ); }

    vWorldSubItems.clear();
    vWorldSubItems.emplace_back( pgettext( "Main Menu|World", "Show World Mods" ) );
    vWorldSubItems.emplace_back( pgettext( "Main Menu|World", "Edit World Mods" ) );
    vWorldSubItems.emplace_back( pgettext( "Main Menu|World", "Copy World Settings" ) );
    vWorldSubItems.emplace_back( pgettext( "Main Menu|World", "Character to Template" ) );
    vWorldSubItems.emplace_back( pgettext( "Main Menu|World", "Reset World" ) );
    vWorldSubItems.emplace_back( pgettext( "Main Menu|World", "Delete World" ) );
    vWorldSubItems.emplace_back( pgettext( "Main Menu|World", "Convert to V2 Save Format" ) );
    vWorldSubItems.emplace_back( pgettext( "Main Menu|World", "<= Return" ) );

    vWorldHotkeys = {'m', 'e', 's', 't', 'r', 'd', 'f', 'q'};

    vSettingsSubItems.clear();
    vSettingsSubItems.emplace_back( pgettext( "Main Menu|Settings", "<O|o>ptions" ) );
    vSettingsSubItems.emplace_back( pgettext( "Main Menu|Settings", "Ke<y|Y>bindings" ) );
    vSettingsSubItems.emplace_back( pgettext( "Main Menu|Settings", "A<u|U>topickup" ) );
    vSettingsSubItems.emplace_back( pgettext( "Main Menu|Settings", "Sa<f|F>emode" ) );
    vSettingsSubItems.emplace_back( pgettext( "Main Menu|Settings", "<D|d>istractions" ) );
    vSettingsSubItems.emplace_back( pgettext( "Main Menu|Settings", "Colo<r|R>s" ) );

    vSettingsHotkeys.clear();
    for( const std::string& item : vSettingsSubItems ) {
        vSettingsHotkeys.push_back( get_hotkeys( item ) );
    }

    vdaytip = get_random_tip_of_the_day();
}

void main_menu::load_char_templates()
{
    templates.clear();

    for( std::string path :
         get_files_from_path( ".template", PATH_INFO::templatedir(), false, true ) ) {
        path.erase( path.find( ".template" ), std::string::npos );
        path.erase( 0, path.find_last_of( "\\/" ) + 1 );
        templates.push_back( path );
    }
    std::sort( templates.begin(), templates.end(), localized_compare );
    std::reverse( templates.begin(), templates.end() );
}

bool main_menu::opening_screen()
{
    // set holiday based on local system time
    current_holiday = get_holiday_from_time();

    // Play title music, whoo!
    play_music( "title" );

    world_generator->set_active_world( nullptr );
    world_generator->init();

    get_help().load();
    init_strings();

    if( !assure_dir_exist( PATH_INFO::config_dir() ) ) {
        popup( _( "Unable to make config directory.  Check permissions." ) );
        return false;
    }

    if( !assure_dir_exist( PATH_INFO::user_moddir() ) ) {
        popup( _( "Unable to make user mods directory.  Check permissions." ) );
        return false;
    }

    if( !assure_dir_exist( PATH_INFO::savedir() ) ) {
        popup( _( "Unable to make save directory.  Check permissions." ) );
        return false;
    }

    if( !assure_dir_exist( PATH_INFO::templatedir() ) ) {
        popup( _( "Unable to make templates directory.  Check permissions." ) );
        return false;
    }

    if( !assure_dir_exist( PATH_INFO::user_fontdir() ) ) {
        popup( _( "Unable to make fonts directory.  Check permissions." ) );
        return false;
    }

    if( !assure_dir_exist( PATH_INFO::user_sound() ) ) {
        popup( _( "Unable to make sound directory.  Check permissions." ) );
        return false;
    }

    if( !assure_dir_exist( PATH_INFO::user_gfx() ) ) {
        popup( _( "Unable to make graphics directory.  Check permissions." ) );
        return false;
    }

    std::optional<int> os_bitness = get_os_bitness();
    std::optional<int> game_bitness = game_info::bitness();
    if( os_bitness && *os_bitness == 64 && game_bitness && *game_bitness == 32 ) {
        popup( _(
                   "You are running x32 build of the game on a x64 operating system.  "
                   "This means the game will NOT be able to access all memory, "
                   "and you may experience random out-of-memory crashes.  "
                   "Consider obtaining a x64 build of the game to avoid that, "
                   "but if you *really* want to be running x32 build of the game "
                   "for some reason (or don't have a choice), you may want to lower "
                   "your memory usage by disabling tileset, soundpack and mods "
                   "and increasing autosave frequency." ) );
    }

    load_char_templates();

    ctxt.register_cardinal();
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "PAGE_UP" );
    ctxt.register_action( "PAGE_DOWN" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );

    // for the menu shortcuts
    ctxt.register_action( "ANY_INPUT" );
    bool start = false;

    avatar& player_character = get_avatar();
    player_character = avatar();

    int sel_line = 0;

    // Make [Load Game] the default cursor position if there's game save available
    if( !world_generator->all_worldnames().empty() ) {
        sel1 = getopt( main_menu_opts::LOADCHAR );
        sel2 = world_generator->get_world_index( world_generator->last_world_name );
    }

    background_pane background;

    // RmlUi render path (Tier 4 #3: the title screen). Render-only — the loop owns the
    // 2-level keyboard nav (sel1/sel2/sel_line); the New/Load/World actions delegate.
    std::unique_ptr<mm_session> data;
    bool rml_scroll_pending = false;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml ) { return; }
        // Typography title (replaces ASCII logo).
        data->title_main_rml = cata_text_to_rml( colorize( "CATACLYSM", c_yellow ) );
        data->title_sub_rml = cata_text_to_rml( colorize( "Bright Nights", c_light_gray ) );
        data->version_rml = cata_text_to_rml(
                                colorize( string_format( _( "Version: %s" ), getVersionString() ), c_light_blue ) );
        data->keybinds_rml = cata_text_to_rml( colorize(
                _( "[LEFT/RIGHT] Navigate   [UP/DOWN] Select   [ENTER] Confirm   [ESC] Quit" ),
                c_dark_gray ) );

        // Nav icons: each menu item gets a unique bindrune sigil seeded by its
        // index.  The procedural texture generator (?proc:bindrune) produces a
        // deterministic rune for each seed, matching the frame's runic aesthetic.
        // Deterministic seeds: 0x4D4F (MOTD), 0x4E47 (NEWCHAR), etc.
        static const unsigned mm_rune_seeds[] = {
            0x4D4F, 0x4E47, 0x4C43, 0x574F, 0x5345, 0x4845, 0x4352, 0x5155,
#ifdef COOP_ENABLED
            0x434F,
#endif
        };
        static constexpr size_t mm_rune_count = sizeof( mm_rune_seeds ) / sizeof( mm_rune_seeds[0] );

        // Top nav items.
        data->items.clear();
        for( size_t i = 0; i < vMenuItems.size(); ++i ) {
            const bool s = static_cast<int>( i ) == sel1;
            mm_item m;
            m.text_rml = cata_text_to_rml(
                             shortcut_text( s ? hilite( c_yellow ) : c_yellow, vMenuItems[i] ) );
            // Boxed band-glyph icon — same style as the runic frame decorations.
            if( i < mm_rune_count ) {
                const char *ink = s ? "c4a832" : "a1885f";
                m.icon_dec = string_format(
                                 "image( ?proc:runic-icon:48:%u:%s none contain ) border-box",
                                 mm_rune_seeds[i], ink );
            }
            m.selected = s;
            data->items.push_back( m );
        }

        // Submenu / MOTD-Credits text.
        data->submenu.clear();
        data->motd_lines.clear();
        data->show_submenu = false;
        data->show_motd = false;
        const main_menu_opts sel_o = static_cast<main_menu_opts>( sel1 );
        const auto push_sub = [&]( const std::string & s, bool sel ) {
            mm_item m;
            m.text_rml = cata_text_to_rml( s );
            m.selected = sel;
            data->submenu.push_back( m );
        };

        // Default context for the right panel.
        data->panel_header_rml = Rml::String();
        data->context_header_rml = Rml::String();
        data->context_body_rml = Rml::String();

        switch( sel_o ) {
            case main_menu_opts::CREDITS:
            case main_menu_opts::MOTD: {
                data->show_motd = true;
                data->context_header_rml = cata_text_to_rml( colorize(
                                               sel_o == main_menu_opts::CREDITS ? _( "CREDITS" ) : _( "MESSAGE OF THE DAY" ),
                                               c_yellow ) );
                const std::string &text =
                    sel_o == main_menu_opts::CREDITS ? mmenu_credits : mmenu_motd;
                for( const std::string &ln : foldstring( text, FULL_SCREEN_WIDTH - 2 ) ) {
                    mm_item m;
                    m.text_rml = cata_text_to_rml( ln );
                    data->motd_lines.push_back( m );
                }
                break;
            }
            case main_menu_opts::SETTINGS:
                data->show_submenu = true;
                data->panel_header_rml = cata_text_to_rml( colorize( _( "SETTINGS" ), c_yellow ) );
                data->context_header_rml = cata_text_to_rml( colorize( _( "TIP" ), c_yellow ) );
                data->context_body_rml = cata_text_to_rml(
                                             colorize( string_format( _( "Tip of the day: %s" ), vdaytip ), c_white ) );
                for( size_t i = 0; i < vSettingsSubItems.size(); ++i ) {
                    push_sub(
                        shortcut_text( static_cast<int>( i ) == sel2 ? hilite( c_yellow ) : c_yellow,
                                       vSettingsSubItems[i] ),
                        static_cast<int>( i ) == sel2 );
                }
                break;
            case main_menu_opts::NEWCHAR:
                data->show_submenu = true;
                data->panel_header_rml = cata_text_to_rml( colorize( _( "NEW GAME" ), c_yellow ) );
                data->context_header_rml = cata_text_to_rml( colorize( _( "INFO" ), c_yellow ) );
                if( sel2 >= 0 && sel2 < static_cast<int>( vNewGameHints.size() ) ) {
                    data->context_body_rml = cata_text_to_rml(
                                                 colorize( vNewGameHints[sel2], c_yellow ) );
                }
                for( size_t i = 0; i < vNewGameSubItems.size(); ++i ) {
                    push_sub(
                        shortcut_text( static_cast<int>( i ) == sel2 ? hilite( c_yellow ) : c_yellow,
                                       vNewGameSubItems[i] ),
                        static_cast<int>( i ) == sel2 );
                }
                break;
            case main_menu_opts::LOADCHAR:
            case main_menu_opts::WORLD: {
                data->show_submenu = true;
                const bool is_world = sel1 == getopt( main_menu_opts::WORLD );
                data->panel_header_rml = cata_text_to_rml( colorize(
                                             is_world ? _( "WORLDS" ) : _( "LOAD GAME" ), c_yellow ) );
                data->context_header_rml = cata_text_to_rml( colorize( _( "WORLD INFO" ), c_yellow ) );
                const bool extra_opt = is_world;
                if( extra_opt ) {
                    push_sub( colorize( _( "Create World" ), sel2 == 0 ? hilite( c_yellow ) : c_yellow ),
                              sel2 == 0 );
                }
                const std::vector<std::string> all_worldnames = world_generator->all_worldnames();
                for( size_t i = 0; i < all_worldnames.size(); ++i ) {
                    WORLDINFO *world = world_generator->get_world( all_worldnames[i] );
                    const int savegames_count = world->world_saves.size();
                    const nc_color clr =
                        ( all_worldnames[i] == "TUTORIAL" || all_worldnames[i] == "DEFENSE" )
                        ? c_light_cyan
                        : c_white;
                    const bool sel = sel2 == static_cast<int>( i ) + ( extra_opt ? 1 : 0 );
                    push_sub( colorize( string_format( "%s (%d)", all_worldnames[i], savegames_count ),
                                        sel ? hilite( clr ) : clr ),
                              sel );
                }
                // Context body: show selected world info.
                {
                    const int world_idx = sel2 - ( extra_opt ? 1 : 0 );
                    if( world_idx >= 0 && world_idx < static_cast<int>( all_worldnames.size() ) ) {
                        WORLDINFO *world = world_generator->get_world( all_worldnames[world_idx] );
                        data->context_body_rml = cata_text_to_rml( colorize( string_format(
                                                     _( "World: %s\nSaves: %d" ), all_worldnames[world_idx],
                                                     static_cast<int>( world->world_saves.size() ) ), c_white ) );
                    }
                }
                break;
            }
            default:
                // HELP, QUIT, COOP — no submenu, show tip of the day.
                data->context_header_rml = cata_text_to_rml( colorize( _( "TIP" ), c_yellow ) );
                data->context_body_rml = cata_text_to_rml(
                                             colorize( string_format( _( "Tip of the day: %s" ), vdaytip ), c_white ) );
                break;
        }

        // Bottom tips line.
        if( sel_o == main_menu_opts::NEWCHAR && sel2 >= 0 &&
            sel2 < static_cast<int>( vNewGameHints.size() ) ) {
            data->tips_rml = cata_text_to_rml( colorize( vNewGameHints[sel2], c_yellow ) );
        } else {
            std::string tips = _( "Bugs?  Suggestions?  Use links in MOTD to report them." );
            data->tips_rml = cata_text_to_rml( colorize( tips, c_white ) );
        }

        // Dirty all bound variables.
        data->handle.DirtyVariable( "title_main_rml" );
        data->handle.DirtyVariable( "title_sub_rml" );
        data->handle.DirtyVariable( "version_rml" );
        data->handle.DirtyVariable( "keybinds_rml" );
        data->handle.DirtyVariable( "panel_header_rml" );
        data->handle.DirtyVariable( "context_header_rml" );
        data->handle.DirtyVariable( "context_body_rml" );
        data->handle.DirtyVariable( "items" );
        if( data->show_submenu ) { data->handle.DirtyVariable( "submenu" ); }
        if( data->show_motd ) { data->handle.DirtyVariable( "motd_lines" ); }
        data->handle.DirtyVariable( "tips_rml" );
        data->handle.DirtyVariable( "show_motd" );
        data->handle.DirtyVariable( "show_submenu" );

        // MOTD/Credits keyboard scroll-follow.
        if( rml_scroll_pending && data->show_motd ) {
            rml_scroll_pending = false;
            if( Rml::Element *list = rml.document()->GetElementById( "mm-motd" ) ) {
                if( sel_line >= 0 && sel_line < list->GetNumChildren() ) {
                    list->GetChild( sel_line )->ScrollIntoView(
                        Rml::ScrollIntoViewOptions( Rml::ScrollAlignment::Start ) );
                }
            }
        }
    };
    rml.open( main_menu_rmlui_enabled(), "mainmenu", ctxt, [&]( Rml::DataModelConstructor & c ) {
        data = std::make_unique<mm_session>();
        register_mm_rml_types( c );
        c.Bind( "title_main_rml", &data->title_main_rml );
        c.Bind( "title_sub_rml", &data->title_sub_rml );
        c.Bind( "version_rml", &data->version_rml );
        c.Bind( "tips_rml", &data->tips_rml );
        c.Bind( "keybinds_rml", &data->keybinds_rml );
        c.Bind( "panel_header_rml", &data->panel_header_rml );
        c.Bind( "context_header_rml", &data->context_header_rml );
        c.Bind( "context_body_rml", &data->context_body_rml );
        c.Bind( "items", &data->items );
        c.Bind( "submenu", &data->submenu );
        c.Bind( "motd_lines", &data->motd_lines );
        c.Bind( "show_motd", &data->show_motd );
        c.Bind( "show_submenu", &data->show_submenu );
        c.BindEventCallback(
        "on_item", [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) { args[0].GetInto( idx ); }
            if( idx >= 0 && idx < static_cast<int>( vMenuItems.size() ) ) {
                sel1 = idx;
                sel2 = 0;
                sel_line = 0;
            }
        } );
        c.BindEventCallback(
        "on_sub", [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) { args[0].GetInto( idx ); }
            if( idx >= 0 ) { sel2 = idx; }
        } );
        data->handle = c.GetModelHandle();
    } );

    ui_adaptor ui;
    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( rml ) {
            sync_rml();
            return;
        }
    } );
    ui.on_screen_resize( [this]( ui_adaptor & ui ) {
        init_windows();
        ui.position_from_window( w_open );
        int lw = 0, lh = 0;
        SDL_GetWindowSize( get_sdl_window().get(), &lw, &lh );
        lighting::plexus_resize( lw, lh );
    } );
    ui.mark_resize();

    // Plexus background — init and make visible.
    lighting::plexus_init();
    {
        int lw = 0, lh = 0;
        SDL_GetWindowSize( get_sdl_window().get(), &lw, &lh );
        lighting::plexus_resize( lw, lh );
    }
    lighting::g_plexus_visible = true;
    on_out_of_scope plexus_cleanup( []() {
        lighting::g_plexus_visible = false;
        lighting::plexus_finish();
    } );

    bool start_new = false;
    // Set a short input timeout so the loop polls regularly for plexus animation
    // instead of blocking forever waiting for user input.
    ctxt.set_timeout( 33 ); // ~30fps loop rate
    on_out_of_scope reset_timeout( [&]() { ctxt.reset_timeout(); } );
    while( !start ) {
        ui_manager::redraw();
        // Refresh in case player created new world or deleted old world
        // Since this is an index for a mutable array, it should always be regenerated instead of
        // modified.
        const size_t last_world_pos = world_generator->get_world_index(
                                          world_generator->last_world_name );
        std::string action = ctxt.handle_input();
        input_event sInput = ctxt.get_raw_input();

        // check automatic menu shortcuts
        for( int i = 0; static_cast<size_t>( i ) < vMenuHotkeys.size(); ++i ) {
            for( const std::string& hotkey : vMenuHotkeys[i] ) {
                if( sInput.text == hotkey && sel1 != i ) {
                    sel1 = i;
                    sel2 = i == getopt( main_menu_opts::LOADCHAR ) ? last_world_pos : 0;
                    sel_line = 0;
                    if( i == getopt( main_menu_opts::HELP ) ) {
                        action = "CONFIRM";
                    } else if( i == getopt( main_menu_opts::QUIT ) ) {
                        action = "QUIT";
                    }
                }
            }
        }
        if( sel1 == getopt( main_menu_opts::SETTINGS ) ) {
            for( int i = 0; static_cast<size_t>( i ) < vSettingsSubItems.size(); ++i ) {
                for( const std::string& hotkey : vSettingsHotkeys[i] ) {
                    if( sInput.text == hotkey ) {
                        sel2 = i;
                        action = "CONFIRM";
                    }
                }
            }
        }
        if( sel1 == getopt( main_menu_opts::NEWCHAR ) ) {
            for( int i = 0; static_cast<size_t>( i ) < vNewGameSubItems.size(); ++i ) {
                for( const std::string& hotkey : vNewGameHotkeys[i] ) {
                    if( sInput.text == hotkey ) {
                        sel2 = i;
                        action = "CONFIRM";
                    }
                }
            }
        }

        // also check special keys
        if( action == "QUIT" ) {
            ui_manager::redraw();
            if( query_yn( _( "Really quit?" ) ) ) { return false; }
        } else if( action == "LEFT" || action == "PREV_TAB" ) {
            sel_line = 0;
            if( sel1 > 0 ) {
                sel1--;
            } else {
                sel1 = max_menu_opts;
            }
            sel2 = sel1 == getopt( main_menu_opts::LOADCHAR ) ? last_world_pos : 0;
            on_move();
        } else if( action == "RIGHT" || action == "NEXT_TAB" ) {
            sel_line = 0;
            if( sel1 < max_menu_opts ) {
                sel1++;
            } else {
                sel1 = 0;
            }
            sel2 = sel1 == getopt( main_menu_opts::LOADCHAR ) ? last_world_pos : 0;
            on_move();
        } else if( action == "UP" || action == "DOWN" || action == "PAGE_UP"
                   || action == "PAGE_DOWN" || action == "SCROLL_UP" || action == "SCROLL_DOWN" ) {
            int max_item_count = 0;
            int min_item_val = 0;
            main_menu_opts opt = static_cast<main_menu_opts>( sel1 );
            switch( opt ) {
                case main_menu_opts::MOTD:
                case main_menu_opts::CREDITS:
                    rml_scroll_pending = true;
                    if( action == "UP" || action == "PAGE_UP" || action == "SCROLL_UP" ) {
                        if( sel_line > 0 ) { sel_line--; }
                    } else if( action == "DOWN" || action == "PAGE_DOWN"
                               || action == "SCROLL_DOWN" ) {
                        int effective_height = sel_line + FULL_SCREEN_HEIGHT - 2;
                        if( ( opt == main_menu_opts::CREDITS && effective_height < mmenu_credits_len )
                            || ( opt == main_menu_opts::MOTD && effective_height < mmenu_motd_len ) ) {
                            sel_line++;
                        }
                    }
                    break;
                case main_menu_opts::LOADCHAR:
                    max_item_count = world_generator->all_worldnames().size();
                    break;
                case main_menu_opts::WORLD:
                    // extra 1 = "Create New World"
                    max_item_count = world_generator->all_worldnames().size() + 1;
                    break;
                case main_menu_opts::NEWCHAR:
                    max_item_count = vNewGameSubItems.size();
                    break;
                case main_menu_opts::SETTINGS:
                    max_item_count = vSettingsSubItems.size();
                    break;
                case main_menu_opts::HELP:
                case main_menu_opts::QUIT:
                default:
                    break;
            }
            if( max_item_count > 0 ) {
                if( action == "UP" || action == "PAGE_UP" || action == "SCROLL_UP" ) {
                    sel2--;
                    if( sel2 < min_item_val ) { sel2 = max_item_count - 1; }
                } else if( action == "DOWN" || action == "PAGE_DOWN" || action == "SCROLL_DOWN" ) {
                    sel2++;
                    if( sel2 >= max_item_count ) { sel2 = min_item_val; }
                }
                on_move();
            }
        } else if( action == "CONFIRM" ) {
            switch( static_cast<main_menu_opts>( sel1 ) ) {
                case main_menu_opts::HELP:
                    get_help().display_help();
                    break;
                case main_menu_opts::QUIT:
                    return false;
                case main_menu_opts::SETTINGS:
                    if( sel2 == 0 ) { /// Options
                        get_options().show( false );
                        // The language may have changed- gracefully handle this.
                        init_strings();
                    } else if( sel2 == 1 ) { /// Keybindings
                        input_context ctxt_default = get_default_mode_input_context();
                        ctxt_default.display_menu();
                    } else if( sel2 == 2 ) { /// Autopickup
                        get_auto_pickup().show();
                    } else if( sel2 == 3 ) { /// Safemode
                        get_safemode().show();
                    } else if( sel2 == 4 ) {
                        get_distraction_manager().show();
                    } else if( sel2 == 5 ) {
                        all_colors.show_gui();
                    }
                    break;
                case main_menu_opts::WORLD:
                    world_tab( sel2 > 0 ? world_generator->all_worldnames().at( sel2 - 1 ) : "" );
                    break;
                case main_menu_opts::LOADCHAR:
                    if( static_cast<std::size_t>( sel2 ) < world_generator->all_worldnames().size() ) {
                        start = load_character_tab( world_generator->all_worldnames().at( sel2 ) );
                    } else {
                        on_error();
                        popup( _( "No world to load." ) );
                    }
                    break;
                case main_menu_opts::NEWCHAR:
                    start = new_character_tab();
                    if( start ) { start_new = true; }
                    break;
                case main_menu_opts::MOTD:
                case main_menu_opts::CREDITS:
                default:
                    break;
#ifdef COOP_ENABLED
                case main_menu_opts::COOP:
                    start = coop_menu::run();
                    break;
#endif
            }
        }
    }
    if( start_new && get_scenario() ) {
        add_msg( get_scenario()->description( player_character.male ) );
    }
    return true;
}

bool main_menu::new_character_tab()
{
    std::string selected_template;

    avatar& pc = get_avatar();
    // Preset character templates
    if( sel2 == 1 ) {
        if( templates.empty() ) {
            on_error();
            popup( _( "No templates found!" ) );
            return false;
        }
        while( true ) {
            uilist mmenu( _( "Choose a preset character template" ), {} );
            mmenu.border_color = c_light_gray;
            mmenu.hotkey_color = c_yellow;
            sound_on_move_uilist_callback cb( this );
            mmenu.callback = &cb;
            mmenu.menu_style = "save"; // RmlUi: compact centred dialog

            int opt_val = 0;
            for( const std::string& tmpl : templates ) {
                mmenu.entries.emplace_back( opt_val++, true, MENU_AUTOASSIGN, tmpl );
            }
            mmenu.entries.emplace_back( opt_val++, true, 'q', "<= Return" );
            mmenu.query();
            opt_val = mmenu.ret;
            if( opt_val < 0 || static_cast<size_t>( opt_val ) >= templates.size() ) { return false; }

            std::string res =
                query_popup()
                .context( "LOAD_DELETE_CANCEL" )
                .default_color( c_light_gray )
                .message( _( "What to do with template \"%s\"?" ), templates[opt_val] )
                .option( "LOAD" )
                .option( "DELETE" )
                .option( "CANCEL" )
                .cursor( 0 )
                .query()
                .action;
            if( res == "DELETE"
                && query_yn( _( "Are you sure you want to delete %s?" ), templates[opt_val] ) ) {
                const auto path = PATH_INFO::templatedir() + templates[opt_val] + ".template";
                if( !remove_file( path ) ) {
                    popup( _( "Sorry, something went wrong." ) );
                } else {
                    templates.erase( templates.begin() + opt_val );
                }
            } else if( res == "LOAD" ) {
                selected_template = templates[opt_val];
                break;
            }

            if( templates.empty() ) { return false; }
        }
    }

    on_out_of_scope cleanup( [&pc]() {
        g->gamemode.reset();
        pc = avatar();
        world_generator->set_active_world( nullptr );
    } );
    g->gamemode.reset();

    WORLDINFO* world;
    if( sel2 == 5 ) {
        g->gamemode = get_special_game( special_game_type::TUTORIAL );
        world = world_generator->make_new_world( special_game_type::TUTORIAL );
    } else if( sel2 == 6 ) {
        g->gamemode = get_special_game( special_game_type::DEFENSE );
        world = world_generator->make_new_world( special_game_type::DEFENSE );
    } else {
        // Pick a world, suppressing prompts if it's "play now" mode.
        bool empty_only = sel2 == 3 || sel2 == 4;
        bool show_prompt = !empty_only;
        world = world_generator->pick_world( show_prompt, empty_only );
    }

    if( world == nullptr ) { return false; }
    world_generator->set_active_world( world );
    // Join pre-warm thread before any DDL work (load_world_modfiles)
    init::join_prewarm();
    try {
        g->setup();
    } catch( const std::exception& err ) {
        debugmsg( "Error: %s", err.what() );
        return false;
    }

    if( g->gamemode ) {
        bool success = g->gamemode->init();
        if( success ) { cleanup.cancel(); }
        return success;
    }

    character_type play_type = character_type::CUSTOM;
    switch( sel2 ) {
        case 0:
            play_type = character_type::CUSTOM;
            break;
        case 1:
            play_type = character_type::TEMPLATE;
            break;
        case 2:
            play_type = character_type::RANDOM;
            break;
        case 3:
            play_type = character_type::NOW;
            break;
        case 4:
            play_type = character_type::FULL_RANDOM;
            break;
    }
    if( !pc.create( play_type, selected_template ) ) {
        load_char_templates();
        MAPBUFFER.clear();
        get_primary_overmapbuffer().clear();
        return false;
    }

    if( !g->start_game() ) { return false; }
    cleanup.cancel();
    return true;
}

bool main_menu::load_character_tab( const std::string& worldname )
{
    WORLDINFO* world = world_generator->get_world( worldname );
    savegames = world->world_saves;
    if( MAP_SHARING::isSharing() ) {
        auto new_end = std::remove_if( savegames.begin(), savegames.end(), []( const save_t &str ) {
            return str.decoded_name() != MAP_SHARING::getUsername();
        } );
        savegames.erase( new_end, savegames.end() );
    }

    if( savegames.empty() ) {
        on_error();
        //~ %s = world name
        popup( _( "%s has no characters to load!" ), worldname );
        return false;
    }

    int opt_val = -1;

    // RmlUi path: a sigil-decorated character list (mirrors pick_world). Gated on
    // rml.open() SUCCESS, not just the toggle, so a not-ready RmlUi falls through
    // to the uilist instead of showing a blank screen.
    input_context ctxt( "LOAD_CHAR_SELECT" );
    ctxt.register_updown();
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "CONFIRM" );

    std::unique_ptr<lc_session> data;
    size_t sel = 0;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml ) { return; }
        data->rows.clear();
        // Sigil ink = the list's text colour ("#rrggbbaa" → the rrggbb digits).
        const std::string ink = nc_color_to_hex( c_white ).substr( 1, 6 );
        for( size_t i = 0; i < savegames.size(); ++i ) {
            const std::string name = savegames[i].decoded_name();
            lc_row r;
            r.text_rml = cata_text_to_rml( colorize( ( i == sel ? ">> " : "   " ) + name, c_white ) );
            r.sigil_dec = string_format(
                              "image( ?proc:bindrune:96:%u:%s none contain ) border-box", lc_name_seed( name ),
                              ink.c_str() );
            r.selected = i == sel;
            data->rows.push_back( r );
        }
        data->tooltip_rml = cata_text_to_rml(
                                colorize( string_format( _( "Load character from \"%s\"" ), worldname ), c_white ) );
        data->handle.DirtyVariable( "rows" );
        data->handle.DirtyVariable( "tooltip_rml" );
    };

    if( rml.open( loadchar_rmlui_enabled(), "loadchar", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
    data = std::make_unique<lc_session>();
        register_lc_rml_types( c );
        c.Bind( "rows", &data->rows );
        c.Bind( "tooltip_rml", &data->tooltip_rml );
        c.BindEventCallback(
        "on_select", [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) { args[0].GetInto( idx ); }
            if( idx >= 0 && idx < static_cast<int>( savegames.size() ) ) {
                sel = static_cast<size_t>( idx );
            }
        } );
        data->handle = c.GetModelHandle();
    } ) ) {
        ui_adaptor ui;
        ui.on_redraw( [&]( const ui_adaptor & ) { sync_rml(); } );
        while( true ) {
            ui_manager::redraw();
            const std::string action = ctxt.handle_input();
            if( action == "QUIT" ) {
                opt_val = -1;
                break;
            } else if( action == "DOWN" ) {
                sel = ( sel + 1 ) % savegames.size();
            } else if( action == "UP" ) {
                sel = ( sel == 0 ) ? savegames.size() - 1 : sel - 1;
            } else if( action == "CONFIRM" ) {
                opt_val = static_cast<int>( sel );
                break;
            }
        }
        rml.close();
    } else {
        uilist mmenu( string_format( _( "Load character from \"%s\"" ), worldname ), {} );
        mmenu.border_color = c_light_gray;
        mmenu.hotkey_color = c_yellow;
        sound_on_move_uilist_callback cb( this );
        mmenu.callback = &cb;
        mmenu.menu_style = "save"; // RmlUi: compact centred dialog
        int ov = 0;
        for( const save_t &s : savegames ) {
            mmenu.entries
            .emplace_back( ov++, true, MENU_AUTOASSIGN, colorize( s.decoded_name(), c_white ) );
        }
        mmenu.entries.emplace_back( ov++, true, 'q', "<= Return" );
        mmenu.query();
        opt_val = mmenu.ret;
    }
    if( opt_val < 0 || static_cast<size_t>( opt_val ) >= savegames.size() ) { return false; }

    avatar& pc = get_avatar();
    on_out_of_scope cleanup( [&pc]() {
        pc = avatar();
        world_generator->set_active_world( nullptr );
    } );

    g->gamemode = nullptr;
    world_generator->last_world_name = world->world_name;
    world_generator->last_character_name = savegames[opt_val].decoded_name();
    world_generator->save_last_world_info();
    world_generator->set_active_world( world );
    // Join pre-warm thread before any DDL work (load_world_modfiles)
    init::join_prewarm();
    drain_worker_thread_debugmsgs();

    // Check if pre-warm loaded this world's data (reuse path)
    const auto* prewarm = init::get_prewarm_result();
    const bool reuse_prewarm = prewarm != nullptr
                               && prewarm->world_name == worldname
                               && prewarm->error.empty();

    try {
        g->setup( !reuse_prewarm );
        if( reuse_prewarm ) {
            g->complete_prewarm_reuse( prewarm->mod_ids );
        }
    } catch( const std::exception &err ) {
        debugmsg( "Error: %s", err.what() );
        return false;
    }

    if( g->load( savegames[opt_val] ) ) {
        cleanup.cancel();
        return true;
    }

    return false;
}

void main_menu::world_tab( const std::string& worldname )
{
    // Create world
    if( sel2 == 0 ) {
        world_generator->make_new_world();
        return;
    }

    auto* world = world_generator->get_world( worldname );
    const auto is_v2_world = world->world_save_format == save_format::V2_COMPRESSED_SQLITE3;

    uilist mmenu( string_format( _( "Manage world \"%s\"" ), worldname ), {} );
    mmenu.border_color = c_light_gray;
    mmenu.hotkey_color = c_yellow;
    sound_on_move_uilist_callback cb( this );
    mmenu.callback = &cb;
    mmenu.menu_style = "save"; // RmlUi: compact centred dialog
    for( size_t i = 0; i < vWorldSubItems.size(); i++ ) {
        const auto enabled = !( i == 6 && is_v2_world );
        mmenu.entries
        .emplace_back( static_cast<int>( i ), enabled, vWorldHotkeys[i], vWorldSubItems[i] );
    }
    mmenu.query();
    int opt_val = mmenu.ret;
    if( opt_val < 0 || static_cast<size_t>( opt_val ) >= vWorldSubItems.size() ) { return; }

    auto clear_world = [this, &worldname]( bool do_delete ) {
        world_generator->delete_world( worldname, do_delete );
        savegames.clear();
        MAPBUFFER.clear();
        get_primary_overmapbuffer().clear();
        if( do_delete ) {
            sel2 = 0; // reset to create world selection
        }
    };

    auto convert_v2 = [this, &worldname]() {
        world_generator->set_active_world( nullptr );
        savegames.clear();
        MAPBUFFER.clear();
        get_primary_overmapbuffer().clear();
        world_generator->convert_to_v2( worldname );
    };

    switch( opt_val ) {
        case 6: // Convert to V2 Save Format
            if( query_yn( _( "Convert to V2 Save Format? A backup will be created. Conversion may "
                             "take several minutes." ) ) ) {
                convert_v2();
            }
            break;
        case 5: // Delete World
            if( query_yn( _( "Delete the world and all saves within?" ) ) ) { clear_world( true ); }
            break;
        case 4: // Reset World
            if( query_yn( _( "Remove all saves and regenerate world?" ) ) ) { clear_world( false ); }
            break;
        case 0: // Active World Mods
            world_generator->show_active_world_mods(
                world_generator->get_world( worldname )->active_mod_order );
            break;
        case 1: // Edit World Mods
            if( query_yn( _(
                              "Editing mod list or mod load order may render the world unstable or "
                              "completely unplayable.  "
                              "It is advised to manually back up world files before proceeding.  "
                              "If you have just started playing, consider creating new world instead.\n"
                              "Proceed?" ) ) ) {
                world_generator->edit_active_world_mods( world );
            }
            break;
        case 2: // Copy World settings
            world_generator->make_new_world( true, worldname );
            break;
        case 3: // Character to Template
            if( load_character_tab( worldname ) ) {
                avatar& pc = get_avatar();
                pc.setID( character_id(), true );
                pc.reset_all_missions();
                pc.character_to_template( pc.name );
                pc = avatar();
                MAPBUFFER.clear();
                get_primary_overmapbuffer().clear();
                load_char_templates();
            }
            break;
        default:
            break;
    }
}

std::string main_menu::halloween_spider()
{
    static const std::string spider =
        "\\ \\ \\/ / / / / / / /\n"
        " \\ \\/\\/ / / / / / /\n"
        "\\ \\/__\\/ / / / / /\n"
        " \\/____\\/ / / / /\n"
        "\\/______\\/ / / /\n"
        "/________\\/ / /\n"
        "__________\\/ /\n"
        "___________\\/\n"
        "        |\n"
        "        |\n"
        "        |\n"
        "        |\n"
        "        |\n"
        "        |\n"
        "        |\n"
        "        |\n"
        "        |\n"
        "        |\n"
        "  , .   |  . ,\n" // NOLINT(cata-text-style)
        "  { | ,--, | }\n" // NOLINT(cata-text-style)
        "   \\\\{~~~~}//\n"
        "  /_/ {<color_c_red>..</color>} \\_\\\n"
        "  { {      } }\n"
        "  , ,      , ."; // NOLINT(cata-text-style)

    return spider;
}

std::string main_menu::halloween_graves()
{
    static const std::string graves =
        "                    _\n"
        "        -q       __(\")_\n"
        "         (\\      \\_  _/\n"
        " .-.   .-''\"'.     |/\n" // NOLINT(cata-text-style)
        "|RIP|  | RIP |   .-.\n"
        "|   |  |     |  |RIP|\n"
        ";   ;  |     | ,'---',"; // NOLINT(cata-text-style)

    return graves;
}
