#include "context_menu.h"

#include <algorithm>
#include <memory>
#include <string>

#include <RmlUi/Core.h>

#include "cursesdef.h"
#include "input.h"
#include "point.h"
#include "rml_callback.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "string_formatter.h"
#include "ui_manager.h"
#include "lighting/rmlui_layer.h"

namespace
{

/// RmlUi-facing mirror of context_action; kept separate so context_menu.h
/// (the small, per-caller-facing API) stays RmlUi-agnostic.
struct ctx_menu_rml_row {
    Rml::String label_rml;
    Rml::String hotkey_rml;
    bool enabled = true;
    bool highlighted = false;
};

struct ctx_menu_session {
    Rml::Vector<ctx_menu_rml_row> rows;
    Rml::String left_style;
    Rml::String top_style;
    Rml::DataModelHandle handle;
};

// Context-global type registration, guarded once (see the uilist / auto_note /
// bionics_ui rml_doc consumers — RmlUi errors if a struct type is registered
// twice on the same context).
bool g_ctx_menu_types_registered = false;

void register_ctx_menu_rml_types( Rml::DataModelConstructor &c )
{
    if( g_ctx_menu_types_registered ) {
        return;
    }
    g_ctx_menu_types_registered = true;
    Rml::StructHandle<ctx_menu_rml_row> rh = c.RegisterStruct<ctx_menu_rml_row>();
    rh.RegisterMember( "label_rml", &ctx_menu_rml_row::label_rml );
    rh.RegisterMember( "hotkey_rml", &ctx_menu_rml_row::hotkey_rml );
    rh.RegisterMember( "enabled", &ctx_menu_rml_row::enabled );
    rh.RegisterMember( "highlighted", &ctx_menu_rml_row::highlighted );
    c.RegisterArray<Rml::Vector<ctx_menu_rml_row>>();
}

// Panel geometry mirrored from context_menu.rcss (.ctx-panel / .ctx-row), used
// only to clamp the panel on-screen BEFORE RmlUi has laid it out for real.
// Approximate on purpose — actual row height depends on font metrics the C++
// side doesn't have access to, and a few dp of slack either way has no
// visible effect. Keep loosely in sync with the rcss if that layout changes.
constexpr float menu_width_dp = 240.0f;
constexpr float row_height_dp = 34.0f;

} // namespace

auto show_context_menu( const point &screen_pos,
                        const std::vector<context_action> &actions ) -> std::optional<action_id>
{
    if( actions.empty() ) {
    return std::nullopt;
}

// This screen has no curses content at all (it's RmlUi-only, like the
// Character::conduct_blood_analysis blood-test popup), but ui_adaptor still
// needs a positioned window to anchor its invalidation bookkeeping — a
// throwaway 1x1 window is never actually drawn into.
catacurses::window w;
ui_adaptor ui;
ui.on_screen_resize( [&]( ui_adaptor & ui ) {
    w = catacurses::newwin( 1, 1, point_zero );
        ui.position_from_window( w );
    } );
    ui.mark_resize();

    input_context ctxt( "CONTEXT_MENU" );
    ctxt.register_action( "UP" );
    ctxt.register_action( "DOWN" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    const int n = static_cast<int>( actions.size() );
    const auto find_next_enabled = [&]( int from, int dir ) {
        int idx = from;
        for( int step = 0; step < n; ++step ) {
            idx = ( idx + dir + n ) % n;
            if( actions[idx].enabled ) {
                return idx;
            }
        }
        return from; // nothing enabled at all — stay put
    };

    int highlighted = actions[0].enabled ? 0 : find_next_enabled( 0, 1 );
    std::optional<action_id> result;
    bool dismissed = false;

    // Data model storage declared BEFORE rml so it outlives the document (see
    // rml_doc's own header comment: RmlUi holds raw pointers into bound
    // members until RemoveDataModel).
    std::unique_ptr<ctx_menu_session> data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml ) {
            return;
        }
        data->rows.clear();
        data->rows.reserve( actions.size() );
        for( int i = 0; i < n; ++i ) {
            ctx_menu_rml_row row;
            row.label_rml = rml_escape( actions[i].label );
            row.hotkey_rml = rml_escape( actions[i].hotkey_hint );
            row.enabled = actions[i].enabled;
            row.highlighted = ( i == highlighted );
            data->rows.push_back( std::move( row ) );
        }
        data->handle.DirtyVariable( "rows" );
    };

    rml.open( true, "context_menu", ctxt, [&]( Rml::DataModelConstructor & c ) {
        data = std::make_unique<ctx_menu_session>();
        register_ctx_menu_rml_types( c );
        c.Bind( "rows", &data->rows );
        c.Bind( "left_style", &data->left_style );
        c.Bind( "top_style", &data->top_style );
        // Mouse click: pick the row (ignored for disabled rows — this is what
        // gives a "disabled" row no click handler in effect, since data-for
        // rows can't conditionally omit an attribute per-item).
        c.BindEventCallback( "on_pick", rml_idx_callback( [&]( int idx ) {
            if( idx < n && actions[idx].enabled ) {
                result = actions[idx].act;
            }
        } ) );
        // Mouse hover: move the shared keyboard/mouse highlight cursor.
        c.BindEventCallback( "on_hover", rml_idx_callback( [&]( int idx ) {
            if( idx < n ) {
                highlighted = idx;
            }
        } ) );
        // Full-viewport backdrop click (i.e. a click that missed the panel,
        // since the panel is a later, on-top sibling) — treat like Escape.
        c.BindEventCallback( "on_dismiss",
        [&]( Rml::DataModelHandle, Rml::Event &, const Rml::VariantList & ) {
            dismissed = true;
        } );
        data->handle = c.GetModelHandle();
    } );

    if( !rml ) {
    return std::nullopt; // RmlUi not ready — no fallback UI for this component
}

// Position: SDL mouse/window coordinates are logical points; RmlUi's own
// ProcessMouseMove path (rmlui_layer.cpp) scales them up into its context
// space by density_ratio() BEFORE SetDensityIndependentPixelRatio applies
// density_ratio()*ui_scale() on top of that — the density_ratio() factors
// cancel, so a raw screen coordinate converts to dp by dividing by
// ui_scale() alone. Context::GetDimensions(), in contrast, already reports
// physical-pixel context space directly, so clamping IT needs the full
// density_ratio()*ui_scale() divisor.
const float scale = rmlui_layer::ui_scale() > 0.0f ? rmlui_layer::ui_scale() : 1.0f;
float left_dp = screen_pos.x / scale;
float top_dp = screen_pos.y / scale;
if( Rml::Context * ctx = rmlui_layer::context() ) {
    const float ratio = rmlui_layer::density_ratio() * scale;
        if( ratio > 0.0f ) {
            const Rml::Vector2i dims = ctx->GetDimensions();
            const float menu_height_dp = static_cast<float>( n ) * row_height_dp;
            const float max_left = std::max( 0.0f, static_cast<float>( dims.x ) / ratio - menu_width_dp );
            const float max_top = std::max( 0.0f, static_cast<float>( dims.y ) / ratio - menu_height_dp );
            left_dp = std::clamp( left_dp, 0.0f, max_left );
            top_dp = std::clamp( top_dp, 0.0f, max_top );
        }
    }
    data->left_style = string_format( "%.2fdp", left_dp );
    data->top_style = string_format( "%.2fdp", top_dp );
    data->handle.DirtyVariable( "left_style" );
    data->handle.DirtyVariable( "top_style" );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        sync_rml();
    } );

    while( !result && !dismissed ) {
    ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "QUIT" ) {
            dismissed = true;
        } else if( action == "UP" ) {
            highlighted = find_next_enabled( highlighted, -1 );
        } else if( action == "DOWN" ) {
            highlighted = find_next_enabled( highlighted, 1 );
        } else if( action == "CONFIRM" ) {
            if( actions[highlighted].enabled ) {
                result = actions[highlighted].act;
            }
        }
    }

    rml.close();
    return result;
}
