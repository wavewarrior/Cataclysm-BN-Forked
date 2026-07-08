#include "distraction_manager.h"

#include <functional>
#include <fstream>
#include <string>

#include "cata_utility.h"
#include "color.h"
#include "cursesdef.h"
#include "input.h"
#include "json.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "uistate.h"
#include "fstream_utils.h"

#include <RmlUi/Core.h>
#include <memory>

#include "lighting/rmlui_layer.h"
#include "rml_screen.h"
#include "rml_util.h"

namespace io
{
template<>
std::string enum_to_string<distraction_type>( distraction_type data )
{
    switch( data ) {
            // *INDENT-OFF*
        case distraction_type::alert: return "Alert";
        case distraction_type::noise: return "Noise";
        case distraction_type::pain: return "Pain";
        case distraction_type::attacked: return "Attacked";
        case distraction_type::hostile_spotted_far: return "Hostile Far";
        case distraction_type::hostile_spotted_near: return "Hostile Near";
        case distraction_type::talked_to: return "Talk";
        case distraction_type::asthma: return "Asthma";
        case distraction_type::weather_change: return "Weather Change";
            // *INDENT-ON*
        case distraction_type::num_distraction_type:
            break;
    }
    debugmsg( "Invalid distraction_type" );
    abort();
}
} // namespace io

namespace
{
// RmlUi render path for the distraction manager (full UI→RmlUi migration, Tier 1
// screen #4). The first toggle-list shape: one document, a moving-cursor list
// whose rows mutate in place (CONFIRM flips Enabled↔Disabled) plus a description
// header that re-renders as the cursor moves. Unlike the sync-once screens
// (help), this is LIVE-SYNC: sync_rml() runs in on_redraw, so every
// ui_manager::redraw() (top of the input loop, after each keystroke) rebuilds
// the rows + header from the current selection — no per-branch DirtyVariable.
// Selection highlight rides .item.selected (theme); status colour rides inside
// status_rml via cata_text_to_rml. Keyboard stays on input_context.
struct distraction_rml_row {
    Rml::String name_rml;
    Rml::String status_rml;
    bool selected = false;
};
struct distraction_rml_session {
    Rml::Vector<distraction_rml_row> rows;
    Rml::String desc_rml;   // description of the selected distraction (header pane)
    Rml::DataModelHandle handle;
    Rml::ElementDocument *doc = nullptr;
};

// Type registration is context-global and persists for the context's life, so
// guard it once. Model NAME "distraction" allows one such screen at a time (it
// can't nest itself), matching the uilist single-instance guard pattern.
bool g_distraction_types_registered = false;
bool g_distraction_model_active = false;

void register_distraction_rml_types( Rml::DataModelConstructor &c )
{
    if( g_distraction_types_registered ) {
        return;
    }
    Rml::StructHandle<distraction_rml_row> rh = c.RegisterStruct<distraction_rml_row>();
    rh.RegisterMember( "name_rml", &distraction_rml_row::name_rml );
    rh.RegisterMember( "status_rml", &distraction_rml_row::status_rml );
    rh.RegisterMember( "selected", &distraction_rml_row::selected );
    c.RegisterArray<Rml::Vector<distraction_rml_row>>();
    g_distraction_types_registered = true;
}
} // namespace

bool &distraction_rmlui_enabled()
{
    // Default OFF — opt in via the F4 panel. See rml_screen.h.
    static bool enabled = true;
    return enabled;
}

namespace distraction_manager
{

static const std::map< distraction_type, std::pair< std::string, std::string> >
distraction_desc = {
    {distraction_type::noise,                { translate_marker( "Noise" ),                     translate_marker( "Interrupts you if you hear a noise." ) } },
    {distraction_type::pain,                 { translate_marker( "Pain" ),                      translate_marker( "Interrupts you if you feel pain." ) } },
    {distraction_type::attacked,             { translate_marker( "Attacked" ),                  translate_marker( "Interrupts you if you are hurt." ) } },
    {distraction_type::hostile_spotted_far,  { translate_marker( "Hostile Spotted" ),           translate_marker( "Interrupts you if you see an enemy." ) } },
    {distraction_type::hostile_spotted_near, { translate_marker( "Hostile Dangerously Close" ), translate_marker( "Interrupts you if an enemy comes within 5 squares." ) } },
    {distraction_type::talked_to,            { translate_marker( "Conversation" ),              translate_marker( "Interrupts you if someone starts a conversation." ) } },
    {distraction_type::asthma,               { translate_marker( "Asthma" ),                    translate_marker( "Interrupts you if you have an asthma attack." ) } },
    {distraction_type::weather_change,       { translate_marker( "Weather change" ),            translate_marker( "Interrupts you if the weather becomes dangerous." ) } }
};

void distraction_manager_gui::show()
{
    const int iHeaderHeight = 4;
    int iContentHeight = 0;
    const int num_distractions = distraction_desc.size();
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

    std::vector<distraction_type> distractions_status;
    distractions_status.reserve( distraction_desc.size() );
    for( auto &dist : distraction_desc ) {
        distractions_status.emplace_back( dist.first );
    };

    int currentLine = 0;
    int startPosition = 0;
    distraction_type cur_distraction = distractions_status[currentLine];

    input_context ctx{ "DISTRACTION_MANAGER" };
    ctx.register_cardinal();
    ctx.register_action( "QUIT" );
    ctx.register_action( "HELP_KEYBINDINGS" );
    ctx.register_action( "CONFIRM" );

    // ---- RmlUi render path --------------------------------------------------
    // Live-synced into a "distraction" data model: sync_rml() runs in on_redraw,
    // so the cursor highlight, per-row Enabled/Disabled status, and header
    // description all track the selection without per-branch DirtyVariable. The
    // existing input loop (UP/DOWN/CONFIRM/QUIT) is untouched.
    std::unique_ptr<distraction_rml_session> rml;
    const auto sync_rml = [&]() {
        if( !rml ) {
            return;
        }
        rml->rows.clear();
        for( int i = 0; i < num_distractions; ++i ) {
            const distraction_type d = distractions_status[i];
            const bool ignored = distractions.contains( d ) && distractions.at( d );
            const nc_color status_color = ignored ? c_red : c_light_green;
            const std::string status_string = ignored ? _( "Disabled" ) : _( "Enabled" );
            distraction_rml_row r;
            r.name_rml = cata_text_to_rml( _( distraction_desc.at( d ).first.c_str() ) );
            r.status_rml = cata_text_to_rml( colorize( status_string, status_color ) );
            r.selected = ( i == currentLine );
            rml->rows.push_back( r );
        }
        rml->desc_rml = cata_text_to_rml( _( distraction_desc.at( cur_distraction ).second.c_str() ) );
        rml->handle.DirtyVariable( "rows" );
        rml->handle.DirtyVariable( "desc_rml" );
    };

    ui.on_redraw( [&]( const ui_adaptor & ) {
        // When the RmlUi path owns the screen, sync the model and skip all curses
        // drawing (mirrors the uilist/missions dual-path).
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    if( distraction_rmlui_enabled() && rmlui_layer::ready() && !g_distraction_model_active ) {
        if( Rml::Context *rctx = rmlui_layer::context() ) {
            Rml::DataModelConstructor c = rctx->CreateDataModel( "distraction" );
            if( c ) {
                rml = std::make_unique<distraction_rml_session>();
                register_distraction_rml_types( c );
                c.Bind( "rows", &rml->rows );
                c.Bind( "desc_rml", &rml->desc_rml );
                c.BindEventCallback( "on_select",
                [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & args ) {
                    int idx = -1;
                    if( !args.empty() ) {
                        args[0].GetInto( idx );
                    }
                    if( idx >= 0 && idx < num_distractions ) {
                        currentLine = idx;
                        cur_distraction = distractions_status[currentLine];
                    }
                } );
                rml->handle = c.GetModelHandle();
                rml->doc = rmlui_layer::open_document( PATH_INFO::datadir() + "gui/distraction.rml" );
                if( rml->doc == nullptr ) {
                    rctx->RemoveDataModel( "distraction" );
                    rml.reset();
                } else {
                    g_distraction_model_active = true;
                    // Tick at 16ms so RmlUi hover/mouse stay live between keys.
                    ctx.set_timeout( 16 );
                }
            }
        }
    }

    while( true ) {
        ui_manager::redraw();

        const std::string currentAction = ctx.handle_input();

        if( currentAction == "QUIT" ) {
            save();
            break;
        }

        if( currentAction == "UP" ) {
            currentLine = modulo( currentLine - 1, num_distractions );
            cur_distraction = distractions_status[currentLine];
        } else if( currentAction == "DOWN" ) {
            currentLine = modulo( currentLine + 1, num_distractions );
            cur_distraction = distractions_status[currentLine];
        } else if( currentAction == "CONFIRM" ) {
            // This will change status color and status text
            distractions[cur_distraction] = !distractions[cur_distraction];
        }
    }

    // Tear down the RmlUi document + data model (no-op if the curses path ran).
    if( rml ) {
        if( rml->doc != nullptr ) {
            rmlui_layer::close_document( rml->doc );
        }
        if( Rml::Context *rctx = rmlui_layer::context() ) {
            rctx->RemoveDataModel( "distraction" );
        }
        g_distraction_model_active = false;
        rml.reset();
    }
}

bool distraction_manager_gui::is_ignored( distraction_type &distract )
{
    // If it doesn't exist it'll create one with a null/false value which works fine for us.
    return distractions[distract];
}

bool distraction_manager_gui::save()
{
    auto file = PATH_INFO::distraction();

    return write_to_file( file, [&]( std::ostream & fout ) {
        JsonOut jout( fout, true );
        distraction_manager_gui::serialize( jout );

    }, _( "distraction manager configuration" ) );
}

void distraction_manager_gui::load()
{
    distractions.clear();
    for( int i = 0; i < static_cast<int>( distraction_type::num_distraction_type ); ++i ) {
        distractions.emplace( static_cast<distraction_type>( i ), false );
    }

    std::ifstream distr;
    std::string file = PATH_INFO::distraction();

    distr.open( file.c_str(), std::ifstream::in | std::ifstream::binary );

    if( distr.good() ) {
        try {
            JsonIn jsin( distr );
            deserialize( jsin );
        } catch( const JsonError &e ) {
            debugmsg( "Error while loading distraction manager settings: %s", e.what() );
        }
    }

    distr.close();
}

void distraction_manager_gui::serialize( JsonOut &json ) const
{
    json.start_array();

for( auto &elem : distractions ) {
    json.start_object();

        json.member( "Distraction Type", io::enum_to_string<distraction_type>( elem.first ) );
        json.member( "Bool", elem.second );

        json.end_object();
    }

    json.end_array();
}

void distraction_manager_gui::deserialize( JsonIn &jsin )
{
    jsin.start_array();
    while( !jsin.end_array() ) {
        JsonObject jo = jsin.get_object();

        if( !jo.has_string( "Distraction Type" ) ) {
            continue;
        }

        const distraction_type type_id = jo.get_enum_value<distraction_type>( "Distraction Type" );
        const bool boolean = jo.get_bool( "Bool" );

        distractions[type_id] = boolean;
    }
}

} // namespace distraction_manager

distraction_manager::distraction_manager_gui &get_distraction_manager()
{
    static distraction_manager::distraction_manager_gui staticSettings;
    return staticSettings;
}
