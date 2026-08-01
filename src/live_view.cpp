#include "live_view.h"

#include <algorithm> // min & max
#include <memory>

#include <RmlUi/Core.h>

#include "color.h"
#include "cursesdef.h"
#include "cursesport.h"
#include "game.h"
#include "map.h"
#include "options.h"
#include "output.h"
#include "panels.h"
#include "path_info.h"
#include "rml_length.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "translations.h"
#include "ui_manager.h"
#include "lighting/rmlui_layer.h"

namespace
{

constexpr int START_LINE = 1;
constexpr int MIN_BOX_HEIGHT = 3;

// ── RmlUi render path (P3 track-A) ───────────────────────────────────────────
// Non-modal, passive overlay box: opened lazily when the hover box appears, fed
// each redraw by game::print_all_tile_info_text() (the migrated look_around info
// producer), positioned at the sidebar edge, closed when the box hides. Uses the
// rmlui_layer doc lifecycle directly (not the modal rml_doc harness).
struct lv_rml_model {
    Rml::String title_rml;
    Rml::String info_rml;
    Rml::DataModelHandle handle;
};
std::unique_ptr<lv_rml_model> g_lv_data;
Rml::ElementDocument *g_lv_doc = nullptr;

/// Place the box at the sidebar edge. Called from BOTH `lv_rml_open` and
/// `lv_rml_sync`, and that is the whole point: `open` sets `title_rml` and hands the
/// document to the layer, so the overlay can be rendered before any sync runs. With
/// positioning only in `sync`, that first frame drew the box at its default — and
/// `.lv-box` is `position: absolute` with `left`/`top`/`width` all unset, so `auto`
/// resolved to the static position at the TOP-LEFT of the containing block at
/// natural width. What that put on screen was the `< Mouse View >` title and the
/// inner `.lv-info` scroll pane's unstyled grey scrollbar furniture, in the corner
/// where the sidebar HUD draws its identity row, looking for all the world like a
/// HUD defect. Position on open and the unpositioned frame cannot happen.
void lv_rml_apply_rect()
{
    if( g_lv_doc == nullptr || TERMX <= 0 ) {
        return;
    }
    auto &mgr = panel_manager::get_manager();
    const bool sidebar_right = get_option<std::string>( "SIDEBAR_POSITION" ) == "right";
    // Floor the column width: `update_offsets` zeroes the column opposite the
    // configured sidebar, and the widget layout the other one comes from can be
    // empty, so this can reach 0 — and a 0% width is a box the scroll furniture
    // spills straight out of.
    const int raw_wd = sidebar_right ? mgr.get_width_right() : mgr.get_width_left();
    const int wd = raw_wd > 0 ? raw_wd : std::max( TERMX / 5, 1 );
    const float width_pct = 100.0f * wd / TERMX;
    const float left_pct = sidebar_right ? 100.0f - width_pct : 0.0f;
    if( Rml::Element *el = g_lv_doc->GetElementById( "lv-box" ) ) {
        el->SetProperty( "left", rml::pct( left_pct ) );
        el->SetProperty( "top", "0%" );
        el->SetProperty( "width", rml::pct( width_pct ) );
    }
}

void lv_rml_open()
{
    if( g_lv_doc != nullptr ) {
        return;  // already open (idempotent)
    }
    if( !live_view_rmlui_enabled() || !rmlui_layer::ready() ) {
        return;
    }
    Rml::Context *ctx = rmlui_layer::context();
    if( ctx == nullptr ) {
        return;
    }
    Rml::DataModelConstructor c = ctx->CreateDataModel( "live_view" );
    if( !c ) {
        return;
    }
    g_lv_data = std::make_unique<lv_rml_model>();
    c.Bind( "title_rml", &g_lv_data->title_rml );
    c.Bind( "info_rml", &g_lv_data->info_rml );
    g_lv_data->title_rml = cata_text_to_rml( _( "< <color_green>Mouse View</color> >" ) );
    g_lv_data->handle = c.GetModelHandle();
    // passive=true: render-only overlay — it must not capture in-game world mouse.
    Rml::ElementDocument *doc =
        rmlui_layer::open_document( PATH_INFO::datadir() + "gui/live_view.rml", true );
    if( doc == nullptr ) {
        // Roll back so a failed open leaves no dangling model (cf. rml_doc::open).
        ctx->RemoveDataModel( "live_view" );
        g_lv_data.reset();
        return;
    }
    g_lv_doc = doc;
    // Position BEFORE the layer can render this document, so the box is never
    // painted at its unpositioned default. See lv_rml_apply_rect.
    lv_rml_apply_rect();
}

void lv_rml_close()
{
    if( g_lv_doc == nullptr ) {
        return;
    }
    rmlui_layer::close_document( g_lv_doc );
    if( Rml::Context *ctx = rmlui_layer::context() ) {
        ctx->RemoveDataModel( "live_view" );
    }
    g_lv_doc = nullptr;
    g_lv_data.reset();
}

void lv_rml_sync( const tripoint_bub_ms &mouse_position )
{
    if( g_lv_doc == nullptr || !g_lv_data ) {
        return;
    }
    const visibility_variables &cache = get_map().get_visibility_variables_cache();
    g_lv_data->info_rml = cata_text_to_rml(
                              g->print_all_tile_info_text( mouse_position, std::string(), cache ) );
    g_lv_data->handle.DirtyVariable( "info_rml" );

    lv_rml_apply_rect();
}

} //namespace

live_view::live_view() = default;
live_view::~live_view() = default;

bool &live_view_rmlui_enabled()
{
    // Default OFF, as this comment has always said and as the value did not: it read
    // `true`, so the RmlUi hover tooltip was live for everyone rather than being the
    // opt-in experiment it is documented as. That mattered once the sidebar HUD moved
    // into the same corner — the overlay renders its `< Mouse View >` title and its
    // scroll furniture over the HUD's identity row whenever the pointer is inside the
    // window, and it is anchored to a `panel_manager` sidebar column that the phosphor
    // HUD no longer defines the same way. Turning it off restores the curses tooltip
    // path in `live_view::show`, which is unaffected, and leaves the RmlUi version
    // reachable from the F4 panel for whoever finishes it. Its placement bug is
    // recorded in plans/phosphor-hud.md.
    static bool enabled = false;
    return enabled;
}

void live_view::init()
{
    hide();
}

void live_view::hide()
{
    lv_rml_close();
    ui = nullptr;
}

void live_view::show( const tripoint_bub_ms &p )
{
    mouse_position = p;
    if( !ui ) {
        ui = std::make_unique<ui_adaptor>();
        ui->on_screen_resize( [this]( ui_adaptor & ui ) {
            auto &mgr = panel_manager::get_manager();
            const bool sidebar_right = get_option<std::string>( "SIDEBAR_POSITION" ) == "right";
            const int width = sidebar_right ? mgr.get_width_right() : mgr.get_width_left();

            const int max_height = TERMY / 2;
            const int line_limit = max_height - 2;
            const visibility_variables &cache = get_map().get_visibility_variables_cache();
            int line_out = START_LINE;
            // HACK: using dummy window to get the window height without refreshing.
            win = catacurses::newwin( 1, width, point_zero );
            g->pre_print_all_tile_info( mouse_position, win, line_out, line_limit, cache );
            const int live_view_box_height = std::min( max_height, std::max( line_out + 2, MIN_BOX_HEIGHT ) );

            win = catacurses::newwin( live_view_box_height, width,
                                      point( sidebar_right ? TERMX - width : 0, 0 ) );
            ui.position_from_window( win );
        } );
        ui->on_redraw( [this]( const ui_adaptor & ) {
            // RmlUi overlay owns the box (curses fallback removed in the tiles-only rip-out).
            if( g_lv_doc != nullptr ) {
                lv_rml_sync( mouse_position );
            }
        } );
        lv_rml_open();
    }
    // Always mark ui for resize as the required box height may have changed.
    ui->mark_resize();
}

bool live_view::is_enabled()
{
    return ui != nullptr;
}
