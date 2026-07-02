#include "help.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <list>
#include <numeric>
#include <optional>
#include <vector>

#include "action.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "color.h"
#include "cursesdef.h"
#include "debug.h"
#include "fstream_utils.h"
#include "input.h"
#include "json.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "string_formatter.h"
#include "string_utils.h"
#include "text_snippets.h"
#include "translations.h"
#include "ui_manager.h"
#include "path_display.h"

#include <RmlUi/Core.h>
#include <memory>

#include "lighting/rmlui_layer.h"
#include "rml_screen.h"
#include "rml_util.h"

help &get_help()
{
    static help single_instance;
    return single_instance;
}

void help::load()
{
    read_from_file_json( PATH_INFO::help(), [&]( JsonIn & jsin ) {
        deserialize( jsin );
    }, true );
}

void help::deserialize( JsonIn &jsin )
{
    hotkeys.clear();

    std::string note_colors = get_note_colors();
    std::string dir_grid = get_dir_grid();

    jsin.start_array();
    while( !jsin.end_array() ) {
        JsonObject jo = jsin.get_object();

        std::vector<std::string> messages;
        jo.read( "messages", messages );

        for( auto &line : messages ) {
            if( line == "<DRAW_NOTE_COLORS>" ) {
                line = replace_all( line, "<DRAW_NOTE_COLORS>", note_colors );
                continue;
            } else if( line == "<HELP_DRAW_DIRECTIONS>" ) {
                line = replace_all( line, "<HELP_DRAW_DIRECTIONS>", dir_grid );
                continue;
            } else if( line == "<GAME_DIRECTORIES>" ) {
                line = resolved_game_paths();
            }
        }

        jo.get_string( "type" ); // Mark as visited
        std::string name = jo.get_string( "name" );
        help_texts[jo.get_int( "order" )] = std::make_pair( name, messages );
        hotkeys.push_back( get_hotkeys( name ) );
    }
}

std::string help::get_dir_grid()
{
    static const std::array<action_id, 9> movearray = {{
            ACTION_MOVE_FORTH_LEFT, ACTION_MOVE_FORTH, ACTION_MOVE_FORTH_RIGHT,
            ACTION_MOVE_LEFT,  ACTION_PAUSE,  ACTION_MOVE_RIGHT,
            ACTION_MOVE_BACK_LEFT, ACTION_MOVE_BACK, ACTION_MOVE_BACK_RIGHT
        }
    };

    std::string movement = "<LEFTUP_0>  <UP_0>  <RIGHTUP_0>   <LEFTUP_1>  <UP_1>  <RIGHTUP_1>\n"
                           " \\ | /     \\ | /\n"
                           "  \\|/       \\|/\n"
                           "<LEFT_0>--<pause_0>--<RIGHT_0>   <LEFT_1>--<pause_1>--<RIGHT_1>\n"
                           "  /|\\       /|\\\n"
                           " / | \\     / | \\\n"
                           "<LEFTDOWN_0>  <DOWN_0>  <RIGHTDOWN_0>   <LEFTDOWN_1>  <DOWN_1>  <RIGHTDOWN_1>";

    for( auto dir : movearray ) {
        std::vector<char> keys = keys_bound_to( dir );
        for( size_t i = 0; i < 2; i++ ) {
            std::string what = "<" + action_ident( dir ) + string_format( "_%d>", i );
            std::string with = i < keys.size()
                               ? string_format( "<color_light_blue>%s</color>", keys[i] )
                               : "<color_red>?</color>";
            movement = replace_all( movement, what, with );
        }
    }

    return movement;
}

std::string help::get_note_colors()
{
    std::string text = _( "Note colors: " );
    for( const auto &color_pair : get_note_color_names() ) {
        // The color index is not translatable, but the name is.
        text += string_format( "%s:%s, ", colorize( color_pair.first, get_note_color( color_pair.first ) ),
                               _( color_pair.second ) );
    }

    return text;
}

namespace
{
// RmlUi render path for the help screen (full UI→RmlUi migration, Tier 1 screen
// #3). One document, two regions toggled by `showing_article` (data-if),
// mirroring the legacy two-phase flow: a clickable topic menu, then a blocking
// scrolling article. Keyboard stays on input_context — hotkeys pick a topic,
// UP/DOWN/PAGE_* scroll the #help-article-body element via SetScrollTop (RmlUi
// handles the mouse wheel). The mouse on_topic click sets pending_topic, acted
// on by the menu loop (no nested input loop from inside the data callback).
struct help_rml_topic {
    Rml::String name_rml;   // shortcut_text()+cata_text_to_rml() — hotkey span
};
struct help_rml_session {
    Rml::Vector<help_rml_topic> topics;
    Rml::String intro_rml;
    Rml::String article_body;   // colour-tagged article; RmlUi wraps (data-rml)
    bool showing_article = false;
    Rml::DataModelHandle handle;
    Rml::ElementDocument *doc = nullptr;
    Rml::Element *scroll = nullptr;  // #help-article-body, scrolled from C++
};

bool g_help_types_registered = false;
bool g_help_model_active = false;

void register_help_rml_types( Rml::DataModelConstructor &c )
{
    if( g_help_types_registered ) {
        return;
    }
    Rml::StructHandle<help_rml_topic> th = c.RegisterStruct<help_rml_topic>();
    th.RegisterMember( "name_rml", &help_rml_topic::name_rml );
    c.RegisterArray<Rml::Vector<help_rml_topic>>();
    g_help_types_registered = true;
}
} // namespace

bool &help_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

void help::display_help()
{
    catacurses::window w_help_border;
    catacurses::window w_help;

    ui_adaptor ui;
    const auto init_windows = [&]( ui_adaptor & ui ) {
        w_help_border = catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH,
                                            point( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                                                TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 ) );
        w_help = catacurses::newwin( FULL_SCREEN_HEIGHT - 2, FULL_SCREEN_WIDTH - 2,
                                     point( 1 + ( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0 ),
                                            1 + ( TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 ) ) );
        ui.position_from_window( w_help_border );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    ctxt.register_cardinal();
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "PAGE_UP" );
    ctxt.register_action( "PAGE_DOWN" );
    // for the menu shortcuts
    ctxt.register_action( "ANY_INPUT" );

    std::string action;

    // ---- RmlUi render path --------------------------------------------------
    // When active the curses on_redraw is skipped; the topic menu is synced once
    // below and the article is shown/scrolled inline in the input loop. A mouse
    // click on a topic sets pending_topic, acted on by the loop (no nested input
    // loop from inside the data callback).
    std::unique_ptr<help_rml_session> rml;
    int pending_topic = -1;
    const auto scroll_rml = [&]( float frac ) {
        if( !rml || rml->scroll == nullptr ) {
            return;
        }
        Rml::Element *e = rml->scroll;
        const float ch = e->GetClientHeight();
        const float max_top = std::max( 0.0f, e->GetScrollHeight() - ch );
        const float t = std::clamp( e->GetScrollTop() + frac * ch, 0.0f, max_top );
        e->SetScrollTop( t );
    };

    if( help_rmlui_enabled() && rmlui_layer::ready() && !g_help_model_active ) {
        if( Rml::Context *ctx = rmlui_layer::context() ) {
            Rml::DataModelConstructor c = ctx->CreateDataModel( "help" );
            if( c ) {
                rml = std::make_unique<help_rml_session>();
                register_help_rml_types( c );
                c.Bind( "topics", &rml->topics );
                c.Bind( "intro_rml", &rml->intro_rml );
                c.Bind( "article_body", &rml->article_body );
                c.Bind( "showing_article", &rml->showing_article );
                c.BindEventCallback( "on_topic",
                [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
                    int idx = -1;
                    if( !args.empty() ) {
                        args[0].GetInto( idx );
                    }
                    if( idx >= 0 && idx < static_cast<int>( help_texts.size() ) ) {
                        pending_topic = idx;
                    }
                } );
                rml->handle = c.GetModelHandle();
                rml->doc = rmlui_layer::open_document( PATH_INFO::datadir() + "gui/help.rml" );
                if( rml->doc == nullptr ) {
                    ctx->RemoveDataModel( "help" );
                    rml.reset();
                } else {
                    rml->scroll = rml->doc->GetElementById( "help-article-body" );
                    // The topic menu is static — build it once here. Each name
                    // gets its hotkey-highlight span the same way draw_menu does
                    // (shortcut_text), then converted to RML spans.
                    rml->intro_rml = cata_text_to_rml(
                                         _( "Please press one of the following for help on that topic:\n"
                                            "Press ESC to return to the game." ) );
                    rml->topics.clear();
                    for( size_t i = 0; i < help_texts.size(); i++ ) {
                        help_rml_topic t;
                        t.name_rml = cata_text_to_rml(
                                         shortcut_text( c_light_blue, _( help_texts[i].first ) ) );
                        rml->topics.push_back( t );
                    }
                    rml->handle.DirtyVariable( "intro_rml" );
                    rml->handle.DirtyVariable( "topics" );
                    g_help_model_active = true;
                    // Tick at 16ms so RmlUi hover/mouse-wheel stay live between keys.
                    ctxt.set_timeout( 16 );
                }
            }
        }
    }

    ui.on_redraw( [&]( const ui_adaptor & ) {
        // Tier-10 rip-out: the RmlUi document renders the menu + article; curses draw is gone.
    } );

    do {
        ui_manager::redraw();

        action = ctxt.handle_input();

        // Pick a topic: a mouse click (pending_topic) or a hotkey match.
        int selected = -1;
        if( pending_topic >= 0 ) {
            selected = pending_topic;
            pending_topic = -1;
        } else {
            const std::string sInput = ctxt.get_raw_input().text;
            for( size_t i = 0; i < hotkeys.size() && selected < 0; ++i ) {
                for( const std::string &hotkey : hotkeys[i] ) {
                    if( sInput == hotkey ) {
                        selected = static_cast<int>( i );
                        break;
                    }
                }
            }
        }

        if( selected >= 0 ) {
            const size_t i = static_cast<size_t>( selected );
            std::vector<std::string> i18n_help_texts;
            i18n_help_texts.reserve( help_texts[i].second.size() );
            std::transform( help_texts[i].second.begin(), help_texts[i].second.end(),
            std::back_inserter( i18n_help_texts ), [&]( std::string & line ) {
                std::string line_proc = _( line );
                size_t pos = line_proc.find( "<press_", 0, 7 );
                while( pos != std::string::npos ) {
                    size_t pos2 = line_proc.find( ">", pos, 1 );

                    std::string action = line_proc.substr( pos + 7, pos2 - pos - 7 );
                    auto replace = "<color_light_blue>" + press_x( look_up_action( action ), "", "" ) + "</color>";

                    if( replace.empty() ) {
                        debugmsg( "Help json: Unknown action: %s", action );
                    } else {
                        line_proc = replace_all( line_proc, "<press_" + action + ">", replace );
                    }

                    pos = line_proc.find( "<press_", pos2, 7 );
                }
                return line_proc;
            } );

            if( !i18n_help_texts.empty() ) {
                const std::string joined =
                    std::accumulate( i18n_help_texts.begin() + 1, i18n_help_texts.end(),
                                     i18n_help_texts.front(),
                []( const std::string & lhs, const std::string & rhs ) {
                    return lhs + "\n\n" + rhs;
                } );

                if( rml ) {
                    // Show the article pane and run an inner scroll loop until
                    // the user backs out (ESC/confirm), then return to the menu.
                    rml->article_body = cata_text_to_rml( joined );
                    rml->showing_article = true;
                    rml->handle.DirtyVariable( "article_body" );
                    rml->handle.DirtyVariable( "showing_article" );
                    if( rml->scroll != nullptr ) {
                        rml->scroll->SetScrollTop( 0.0f );
                    }
                    while( true ) {
                        ui_manager::redraw();
                        const std::string a = ctxt.handle_input();
                        if( a == "DOWN" ) {
                            scroll_rml( 0.1f );
                        } else if( a == "UP" ) {
                            scroll_rml( -0.1f );
                        } else if( a == "PAGE_DOWN" ) {
                            scroll_rml( 0.9f );
                        } else if( a == "PAGE_UP" ) {
                            scroll_rml( -0.9f );
                        } else if( a == "QUIT" || a == "CONFIRM" ) {
                            break;
                        }
                    }
                    rml->showing_article = false;
                    rml->handle.DirtyVariable( "showing_article" );
                } else {
                    ui.on_screen_resize( nullptr );

                    const auto get_w_help_border = [&]() {
                        init_windows( ui );
                        return w_help_border;
                    };

                    scrollable_text( get_w_help_border, _( " HELP " ), joined );

                    ui.on_screen_resize( init_windows );
                }
            }
            action = "CONFIRM";
        }
    } while( action != "QUIT" );

    // Tear down the RmlUi document + data model (no-op if the curses path ran).
    if( rml ) {
        if( rml->doc != nullptr ) {
            rmlui_layer::close_document( rml->doc );
        }
        if( Rml::Context *ctx = rmlui_layer::context() ) {
            ctx->RemoveDataModel( "help" );
        }
        g_help_model_active = false;
        rml.reset();
    }
}

std::string get_hint()
{
    return SNIPPET.random_from_category( "hint" ).value_or( translation() ).translated();
}
