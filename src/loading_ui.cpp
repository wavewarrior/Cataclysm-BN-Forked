#include "loading_ui.h"

#include <RmlUi/Core.h>

#include <memory>
#include <string>

#include "cached_options.h"
#include "color.h"
#include "input.h"
#include "output.h"
#include "path_info.h"
#include "rml_length.h"
#include "rml_util.h"
#include "sdltiles.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "lighting/rmlui_layer.h"

// ── Bespoke RmlUi loading screen (full UI→RmlUi migration) ───────────────────
// Non-modal like the sidebar HUD (no input_context / loop): lazy open, sync each
// loading_ui::show(), close on the owner's destruction. The uilist `menu` stays the
// state holder; loading_doc_sync reads its entries + selected to drive the doc, so
// the curses path is a clean toggle fallback (and the only path until RmlUi is ready).

bool &loading_rmlui_enabled()
{
    static bool enabled = true;
    return enabled;
}

namespace
{
// One step row: colour-tagged text (cata_text_to_rml) + a state the RCSS styles
// (done = ✓/green, current = ▶/glow, pending = •/dim).
struct loading_row_model {
    Rml::String rml;
    Rml::String state;
};
struct loading_rml_model {
    Rml::String title_rml;
    Rml::Vector<loading_row_model> rows;
    int done = 0;
    int total = 0;
    Rml::DataModelHandle handle;
};
std::unique_ptr<loading_rml_model> g_loading_data;
Rml::ElementDocument *g_loading_doc = nullptr;

// Lazily create the "loading" data model + open gui/loading.rml. No-op when the
// toggle is off, RmlUi is not ready (early data load, before the first
// refresh_display inits the context), or already open. Mirrors sidebar_hud_open's
// rollback so a failed open leaves no dangling model. passive=true: render-only,
// Context-global type registration, guarded once (see uilist pattern).
bool g_loading_types_registered = false;

// never captures input.
void loading_doc_open()
{
    if( g_loading_doc != nullptr ) {
        return;  // already open (idempotent)
    }
    if( !loading_rmlui_enabled() || !rmlui_layer::ready() ) {
        return;
    }
    Rml::Context *ctx = rmlui_layer::context();
    if( ctx == nullptr ) {
        return;
    }
    Rml::DataModelConstructor c = ctx->CreateDataModel( "loading" );
    if( !c ) {
        return;
    }
    if( !g_loading_types_registered ) {
        Rml::StructHandle<loading_row_model> rh = c.RegisterStruct<loading_row_model>();
        rh.RegisterMember( "rml", &loading_row_model::rml );
        rh.RegisterMember( "state", &loading_row_model::state );
        c.RegisterArray<Rml::Vector<loading_row_model>>();
        g_loading_types_registered = true;
    }
    g_loading_data = std::make_unique<loading_rml_model>();
    c.Bind( "title_rml", &g_loading_data->title_rml );
    c.Bind( "rows", &g_loading_data->rows );
    c.Bind( "done", &g_loading_data->done );
    c.Bind( "total", &g_loading_data->total );
    g_loading_data->handle = c.GetModelHandle();
    Rml::ElementDocument *doc =
        rmlui_layer::open_document( PATH_INFO::datadir() + "gui/loading.rml", true );
    if( doc == nullptr ) {
        ctx->RemoveDataModel( "loading" );
        g_loading_data.reset();
        return;
    }
    g_loading_doc = doc;
}

// Rebuild the model from the uilist state: title, one row per entry (done when
// proceed() coloured it green, current at the selected index, else pending) and the
// done/total counts. The bar fill width is set imperatively (RCSS has no expression).
void loading_doc_sync( const uilist &menu )
{
    if( g_loading_doc == nullptr || !g_loading_data ) {
        return;
    }
    g_loading_data->title_rml = cata_text_to_rml( menu.text );
    g_loading_data->rows.clear();
    int done = 0;
    for( int i = 0; i < static_cast<int>( menu.entries.size() ); i++ ) {
        const uilist_entry &e = menu.entries[i];
        loading_row_model row;
        row.rml = cata_text_to_rml( e.txt );
        if( e.text_color == c_green ) {
            row.state = "done";
            done++;
        } else if( i == menu.selected ) {
            row.state = "current";
        } else {
            row.state = "pending";
        }
        g_loading_data->rows.push_back( std::move( row ) );
    }
    g_loading_data->done = done;
    g_loading_data->total = static_cast<int>( menu.entries.size() );
    g_loading_data->handle.DirtyVariable( "title_rml" );
    g_loading_data->handle.DirtyVariable( "rows" );
    g_loading_data->handle.DirtyVariable( "done" );
    g_loading_data->handle.DirtyVariable( "total" );

    if( Rml::Element *fill = g_loading_doc->GetElementById( "loading-bar-fill" ) ) {
        const float pct = g_loading_data->total > 0
                          ? 100.0f * done / g_loading_data->total : 0.0f;
        fill->SetProperty( "width", rml::pct( pct ) );
    }
}

void loading_doc_close()
{
    if( g_loading_doc == nullptr ) {
        return;
    }
    rmlui_layer::close_document( g_loading_doc );
    if( Rml::Context *ctx = rmlui_layer::context() ) {
        ctx->RemoveDataModel( "loading" );
    }
    g_loading_doc = nullptr;
    g_loading_data.reset();
}
} // namespace

loading_ui::loading_ui( bool display )
{
    if( display && !test_mode ) {
        menu = std::make_unique<uilist>();
        menu->settext( _( "Loading" ) );
    }
}

loading_ui::~loading_ui()
{
    // Tear down the bespoke RmlUi loading doc (no-op when the curses path ran).
    loading_doc_close();
}

void loading_ui::add_entry( const std::string &description )
{
    if( menu != nullptr ) {
        menu->addentry( menu->entries.size(), true, 0, description );
    }
}

void loading_ui::new_context( const std::string &desc )
{
    if( menu != nullptr ) {
        menu->reset();
        menu->settext( desc );
        ui = nullptr;
        // A new phase should appear immediately, however recently the previous one
        // drew: reset the rate limit so the next show() presents.
        last_draw_ = {};
    }
}

void loading_ui::init()
{
    if( menu != nullptr && ui == nullptr ) {
        ui = std::make_unique<ui_adaptor>();
        ui->on_screen_resize( [this]( ui_adaptor & ui ) { menu->reposition( ui ); } );
        menu->reposition( *ui );
        ui->on_redraw( [this]( ui_adaptor & ui ) {
            // Prefer the bespoke RmlUi loading doc; fall back to the curses uilist when
            // the toggle is off or RmlUi is not ready yet (early data load).
            loading_doc_open();
            if( g_loading_doc != nullptr ) {
                loading_doc_sync( *menu );
            } else {
                menu->show( ui );
            }
        } );
    }
}

void loading_ui::proceed()
{
    init();

    if( menu != nullptr && !menu->entries.empty() ) {
        if( menu->selected >= 0 && menu->selected < static_cast<int>( menu->entries.size() ) ) {
            // TODO: Color it red if it errored hard, yellow on warnings
            menu->entries[menu->selected].text_color = c_green;
        }

        if( menu->selected + 1 < static_cast<int>( menu->entries.size() ) ) {
            menu->scrollby( 1 );
        }
    }

    show();
}

void loading_ui::show()
{
    draw( false );
}

void loading_ui::draw( const bool force )
{
    init();

    if( menu == nullptr ) {
        return;
    }
    // Presenting costs 20-35ms per frame here (whole lighting + composite pipeline,
    // plus an RmlUi relayout of this document), and the loaders call show()/proceed()
    // thousands of times per world load: a measured 77% of a 228s world load was
    // spent inside refresh_display, against ~3.5s of actual JSON scanning, with only
    // ~1ms of loading work between consecutive frames. Rate-limit the PRESENTATION
    // to ~10fps; menu state is still updated on every call, so the progress list and
    // the RmlUi model stay correct — they are just shown a few ms later.
    constexpr auto min_interval = std::chrono::milliseconds( 100 );
    const auto now = std::chrono::steady_clock::now();
    if( !force && now - last_draw_ < min_interval ) {
        return;
    }
    last_draw_ = now;

    ui_manager::redraw();
    refresh_display();
    inp_mngr.pump_events();
}
