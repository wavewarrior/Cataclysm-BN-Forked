#include "auto_note.h"

#include <iostream>
#include <memory>

#include "cata_utility.h"
#include "color.h"
#include "cursesdef.h"
#include "filesystem.h"
#include "fstream_utils.h"
#include "game.h"
#include "generic_factory.h"
#include "input.h"
#include "json.h"
#include "map_extras.h"
#include "options.h"
#include "output.h"
#include "point.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"
#include "world.h"

#include <RmlUi/Core.h>

#include "rml_screen.h"
#include "rml_util.h"

namespace
{
// RmlUi render path for the auto notes manager (full UI→RmlUi migration, Tier 1
// screen #5) and the FIRST consumer of the F.3 rml_doc harness — the
// open/close/guard/16ms-tick lifecycle now lives in rml_doc; only the data model
// + this sync stay here. Toggle-list family (cf. distraction): live-synced via
// sync_rml() in on_redraw, so the cursor highlight + per-row status + header all
// track the selection without per-branch DirtyVariable. Status/symbol colour
// rides inside the bound strings via cata_text_to_rml.
struct auto_note_rml_row {
    Rml::String name_rml;
    Rml::String symbol_rml;   // map symbol char, coloured
    Rml::String status_rml;   // yes(green)/no(red)
    bool selected = false;
};
struct auto_note_rml_session {
    Rml::Vector<auto_note_rml_row> rows;
    Rml::String header_rml;   // "Auto notes enabled: True/False"
    bool empty = false;       // no discovered map extras → show the hint instead
    Rml::String empty_rml;
    Rml::DataModelHandle handle;
};

// Context-global type registration, guarded once (see uilist pattern).
bool g_auto_note_types_registered = false;

void register_auto_note_rml_types( Rml::DataModelConstructor &c )
{
    if( g_auto_note_types_registered ) {
        return;
    }
    Rml::StructHandle<auto_note_rml_row> rh = c.RegisterStruct<auto_note_rml_row>();
    rh.RegisterMember( "name_rml", &auto_note_rml_row::name_rml );
    rh.RegisterMember( "symbol_rml", &auto_note_rml_row::symbol_rml );
    rh.RegisterMember( "status_rml", &auto_note_rml_row::status_rml );
    rh.RegisterMember( "selected", &auto_note_rml_row::selected );
    c.RegisterArray<Rml::Vector<auto_note_rml_row>>();
    g_auto_note_types_registered = true;
}
} // namespace

bool &auto_note_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

namespace auto_notes
{
void auto_note_settings::clear()
{
    autoNoteEnabled.clear();
}

bool auto_note_settings::save()
{
    world *world = g->get_active_world();
    if( !world->player_file_exist( ".sav" ) ) {
        return true;
    }

    return world->write_to_player_file( ".ano.json", [&]( std::ostream & fstr ) {
        JsonOut jout{ fstr, true };

        jout.start_object();

        jout.member( "enabled" );

        jout.start_array();
        for( const string_id<map_extra> &entry : autoNoteEnabled ) {
            jout.write( entry.str() );
        }
        jout.end_array();

        jout.member( "discovered" );

        jout.start_array();
        for( const string_id<map_extra> &entry : discovered ) {
            jout.write( entry.str() );
        }
        jout.end_array();

        jout.end_object();

    }, _( "auto notes configuration" ) );
}

void auto_note_settings::load()
{
    clear();

    const auto parseJson = [&]( JsonIn & jin ) {
        jin.start_object();

        while( !jin.end_object() ) {
            const std::string name = jin.get_member_name();

            if( name == "enabled" ) {
                jin.start_array();
                while( !jin.end_array() ) {
                    const std::string entry = jin.get_string();
                    autoNoteEnabled.insert( string_id<map_extra> {entry} );
                }
            } else if( name == "discovered" ) {
                jin.start_array();
                while( !jin.end_array() ) {
                    const std::string entry = jin.get_string();
                    discovered.insert( string_id<map_extra> {entry} );
                }
            } else {
                jin.skip_value();
            }

        }
    };

    if( !g->get_active_world()->read_from_player_file_json( ".ano.json", parseJson,
            true ) ) {
        default_initialize();
        save();
    }
}

void auto_note_settings::default_initialize()
{
    clear();

    for( auto &extra : MapExtras::mapExtraFactory().get_all() ) {
        if( extra.autonote ) {
            autoNoteEnabled.insert( extra.id );
        }
    }
}

void auto_note_settings::set_discovered( const string_id<map_extra> &mapExtId )
{
    discovered.insert( mapExtId );
}

bool auto_note_settings::was_discovered( const string_id<map_extra> &mapExtId ) const
{
    return discovered.contains( mapExtId );
}

void auto_note_settings::show_gui()
{
    auto_note_manager_gui gui{ };
    gui.show();

    if( gui.was_changed() ) {
        save();
    }
}

bool auto_note_settings::has_auto_note_enabled( const string_id<map_extra> &mapExtId ) const
{
    return autoNoteEnabled.contains( mapExtId );
}

void auto_note_settings::set_auto_note_status( const string_id<map_extra> &mapExtId,
        const bool enabled )
{
    if( enabled ) {
        autoNoteEnabled.insert( mapExtId );
    } else if( has_auto_note_enabled( mapExtId ) ) {
        autoNoteEnabled.erase( mapExtId );
    }
}

auto_note_manager_gui::auto_note_manager_gui()
{
    const auto_note_settings &settings = get_auto_notes_settings();

    for( auto &extra : MapExtras::mapExtraFactory().get_all() ) {
        // Ignore all extras that have autonote disabled in the JSON.
        // This filters out lots of extras users shouldn't see (like "normal")
        if( !extra.autonote ) {
            continue;
        }

        bool isAutoNoteEnabled = settings.has_auto_note_enabled( extra.id );

        mapExtraCache.emplace( extra.id, std::make_pair( extra,
                               isAutoNoteEnabled ) );

        if( settings.was_discovered( extra.id ) ) {
            displayCache.push_back( extra.id );
        }
    }
}

bool auto_note_manager_gui::was_changed() const
{
    return wasChanged;
}

void auto_note_manager_gui::show()
{
    const int iHeaderHeight = 3;
    int iContentHeight = 0;
    catacurses::window w_border;
    catacurses::window w_header;
    catacurses::window w;

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        iContentHeight = FULL_SCREEN_HEIGHT - 2 - iHeaderHeight;

        const point iOffset( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                             TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 );

        w_border = catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH,
                                       iOffset );

        w_header = catacurses::newwin( iHeaderHeight, FULL_SCREEN_WIDTH - 2,
                                       iOffset + point_south_east );

        w = catacurses::newwin( iContentHeight, FULL_SCREEN_WIDTH - 2,
                                iOffset + point( 1, iHeaderHeight + 1 ) );

        ui.position_from_window( w_border );
    } );
    ui.mark_resize();

    // ===========================================================================
    // If the display cache contains no entries, the player might not have discovered any of
    // the map extras. In this case, we switch to a special state that alerts the user of this
    // in order to avoid confusion a completely empty GUI might normally create.
    const bool emptyMode = displayCache.empty();
    const int cacheSize = static_cast<int>( displayCache.size() );

    int currentLine = 0;
    int startPosition = 0;
    int endPosition = 0;

    input_context ctx{ "AUTO_NOTES" };
    ctx.register_action( "QUIT" );
    ctx.register_action( "SWITCH_AUTO_NOTE_OPTION" );
    ctx.register_action( "HELP_KEYBINDINGS" );

    if( !emptyMode ) {
        ctx.register_cardinal();
        ctx.register_action( "CONFIRM" );
        ctx.register_action( "QUIT" );
        ctx.register_action( "ENABLE_MAPEXTRA_NOTE" );
        ctx.register_action( "DISABLE_MAPEXTRA_NOTE" );
        ctx.register_action( "CHANGE_MAPEXTRA_CHARACTER" );
    }

    // ---- RmlUi render path (F.3 rml_doc harness) ----------------------------
    // rml_doc owns the open/guard/16ms-tick/close lifecycle; only the model +
    // this live sync stay here. sync_rml() runs in on_redraw, so the cursor
    // highlight, per-row status, and header all track the selection without
    // per-branch DirtyVariable. The data model storage is declared BEFORE rml so
    // it outlives the document (RmlUi holds raw pointers into it until close).
    std::unique_ptr<auto_note_rml_session> data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml ) {
            return;
        }
        const bool an = get_option<bool>( "AUTO_NOTES" );
        data->header_rml = cata_text_to_rml( string_format( _( "Auto notes enabled: %s" ),
                                             colorize( an ? _( "True" ) : _( "False" ), an ? c_light_green : c_light_red ) ) );
        data->empty = emptyMode;
        data->rows.clear();
        if( emptyMode ) {
            data->empty_rml = cata_text_to_rml( colorize(
                                                    _( "Discover more special encounters to populate this list" ), c_light_gray ) );
        } else {
            for( int i = 0; i < cacheSize; ++i ) {
                const string_id<map_extra> &id = displayCache[i];
                const auto &cacheEntry = mapExtraCache[id];
                const bool en = cacheEntry.second;
                auto_note_rml_row r;
                r.name_rml = cata_text_to_rml( cacheEntry.first.name() );
                r.symbol_rml = cata_text_to_rml( colorize( cacheEntry.first.get_symbol(),
                                                 cacheEntry.first.color ) );
                r.status_rml = cata_text_to_rml( colorize(
                                                     en ? pgettext( "auto notes status value", "yes" )
                                                     : pgettext( "auto notes status value", "no" ),
                                                     en ? c_green : c_red ) );
                r.selected = ( i == currentLine );
                data->rows.push_back( r );
            }
        }
        data->handle.DirtyVariable( "header_rml" );
        data->handle.DirtyVariable( "empty" );
        data->handle.DirtyVariable( "empty_rml" );
        data->handle.DirtyVariable( "rows" );
    };

    rml.open( auto_note_rmlui_enabled(), "auto_note", ctx,
    [&]( Rml::DataModelConstructor & c ) {
        data = std::make_unique<auto_note_rml_session>();
        register_auto_note_rml_types( c );
        c.Bind( "rows", &data->rows );
        c.Bind( "header_rml", &data->header_rml );
        c.Bind( "empty", &data->empty );
        c.Bind( "empty_rml", &data->empty_rml );
        c.BindEventCallback( "on_select",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
            int idx = -1;
            if( !args.empty() ) {
                args[0].GetInto( idx );
            }
            if( idx >= 0 && idx < cacheSize ) {
                currentLine = idx;
            }
        } );
        data->handle = c.GetModelHandle();
    } );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        // RmlUi path owns the screen — sync the model and skip curses drawing.
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    while( true ) {
        ui_manager::redraw();

        const std::string currentAction = ctx.handle_input();

        // Actions that also work with no items to display
        if( currentAction == "SWITCH_AUTO_NOTE_OPTION" ) {
            get_options().get_option( "AUTO_NOTES" ).setNext();

            if( get_option<bool>( "AUTO_NOTES" ) && !get_option<bool>( "AUTO_NOTES_MAP_EXTRAS" ) ) {
                get_options().get_option( "AUTO_NOTES_MAP_EXTRAS" ).setNext();
            }

            get_options().save();
        } else if( currentAction == "QUIT" ) {
            break;
        }

        if( emptyMode ) {
            continue;
        }

        const string_id<map_extra> &currentItem = displayCache[currentLine];
        std::pair<const map_extra, bool> &entry = mapExtraCache[currentItem];

        if( currentAction == "UP" ) {
            if( currentLine > 0 ) {
                --currentLine;
            } else {
                currentLine = cacheSize - 1;
            }
        } else if( currentAction == "DOWN" ) {
            if( currentLine == cacheSize - 1 ) {
                currentLine = 0;
            } else {
                ++currentLine;
            }
        }  else if( currentAction == "ENABLE_MAPEXTRA_NOTE" ) {
            entry.second = true;
            wasChanged = true;
        } else if( currentAction == "DISABLE_MAPEXTRA_NOTE" ) {
            entry.second = false;
            wasChanged = true;
        } else if( currentAction == "CONFIRM" ) {
            entry.second = !entry.second;
            wasChanged = true;
        }
    }

    // Tear down the RmlUi document on every exit path (incl. the early return
    // below) while the bound `data` is still alive. close() is idempotent and a
    // no-op when the curses path ran.
    rml.close();

    if( !was_changed() ) {
        return;
    }

    if( query_yn( _( "Save changes?" ) ) ) {
        auto_notes::auto_note_settings &settings = get_auto_notes_settings();

        for( const auto &entry : mapExtraCache ) {
            settings.set_auto_note_status( entry.second.first.id, entry.second.second );
        }
    }
}
} // namespace auto_notes

auto_notes::auto_note_settings &get_auto_notes_settings()
{
    static auto_notes::auto_note_settings staticSettings;
    return staticSettings;
}
