#include "throw_radial.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>

#include <RmlUi/Core.h>

#include "avatar.h"
#include "cursesdef.h"
#include "input.h"
#include "itype.h"
#include "options.h"
#include "output.h"
#include "panels.h"   // sidebar_hud_top_rows/_bottom_rows, sidebar_terrain_cols_*
#include "rml_length.h"
#include "rml_screen.h"
#include "rml_util.h"
#include "sdl_input.h"
#include "sdl_window_dims.h"
#include "translations.h"
#include "ui_manager.h"
#include "lighting/rmlui_layer.h"

// throw_radial.rml declares one .tr-slot per quick-slot by hand (RCSS cannot
// iterate, and a data-for would fight the per-element positioning below). Fail
// the build rather than silently drop slots off the wheel if this grows.
static_assert( avatar::MAX_THROW_SLOTS == 6,
               "throw_radial.rml/.rcss declare exactly one .tr-slot per throw slot" );

namespace
{

constexpr int SLOTS = avatar::MAX_THROW_SLOTS;

// Everything below is in dp, matching the negative-margin centring the cards do
// in throw_radial.rcss. Two different divisors get you there and mixing them is
// invisible at scale 1.0 and half a screen out on HiDPI (cf. context_menu.cpp):
//
//   raw SDL mouse point  -> dp : / ui_scale()
//     (RmlUi's own input path already scaled it by density_ratio(), which then
//      cancels against the density_ratio()*ui_scale() dp ratio)
//   Context::GetDimensions() -> dp : / (density_ratio() * ui_scale())
//     (the context is sized in PHYSICAL pixels — rmlui_layer.cpp:1177)

// Below this dp distance the pointer has no meaningful direction, so the
// highlight holds instead of flickering between neighbours across the centre.
constexpr float DEAD_ZONE_DP = 40.0f;

// A tap is normally released before the first 16 ms poll. Honour releases only
// after this long, so a tap leaves a sticky ring instead of flashing one open
// and shut.
constexpr uint64_t MIN_HOLD_MS = 160;

struct radial_session {
    std::array<Rml::String, SLOTS> key_rml;
    std::array<Rml::String, SLOTS> name_rml;
    std::array<Rml::String, SLOTS> count_rml;
    Rml::String hub_title;
    Rml::String hub_sel;
    Rml::DataModelHandle handle;
};

struct ring_geom {
    float cx = 0.0f;      // dp
    float cy = 0.0f;      // dp
    float radius = 0.0f;  // dp
    bool valid = false;
};

auto ui_scale_safe() -> float
{
    const float s = rmlui_layer::ui_scale();
    return s > 0.0f ? s : 1.0f;
}

// Recomputed every frame so a mid-flick resize cannot strand the wheel.
auto compute_ring() -> ring_geom
{
    ring_geom g;
    Rml::Context *ctx = rmlui_layer::context();
    if( ctx == nullptr || TERMX <= 0 || TERMY <= 0 ) { return g; }
    const float ratio = rmlui_layer::density_ratio() * ui_scale_safe();
    if( ratio <= 0.0f ) { return g; }
    const Rml::Vector2i dims = ctx->GetDimensions();
    const float w = static_cast<float>( dims.x ) / ratio;
    const float h = static_cast<float>( dims.y ) / ratio;
    if( w <= 1.0f || h <= 1.0f ) { return g; }

    // Centre on the PLAY AREA, not the window: the sidebar dock and the HUD's
    // top/bottom chrome strips are carved out of the terrain viewport, so a
    // window-centred ring sits visibly off to one side of the map the player is
    // actually looking at. Carved from the same sources the HUD uses, so the two
    // always agree.
    const int width_left = sidebar_terrain_cols_left();
    const int width_right = sidebar_terrain_cols_right();
    const float left = w * width_left / TERMX;
    const float right = w * ( TERMX - width_right ) / TERMX;
    const float top = h * sidebar_hud_top_rows() / TERMY;
    const float bottom = h * ( TERMY - sidebar_hud_bottom_rows() ) / TERMY;

    g.cx = ( left + right ) * 0.5f;
    g.cy = ( top + bottom ) * 0.5f;
    const float play_w = std::max( 1.0f, right - left );
    const float play_h = std::max( 1.0f, bottom - top );
    // Wide enough to separate six cards, tight enough that a flick reaches any
    // of them without crossing the whole screen.
    //
    // The floor is geometric, not taste: the four side cards sit at 0.866*R
    // horizontally (cos 30 deg), and must clear the hub. With a 208dp card
    // (half 104) and a 180dp hub (half 90), 0.866*R - 104 >= 90 gives R >= 226.
    // Keep this in step with .tr-slot / #tr-hub in throw_radial.rcss.
    const float want = std::clamp( std::min( play_w, play_h ) * 0.30f, 226.0f, 320.0f );
    // ...but staying ON SCREEN outranks clearing the hub. A cramped viewport
    // shrinks the ring (cards may then crowd the readout) rather than pushing
    // the top/bottom cards off the play area, where they'd be unreachable.
    // Top/bottom cards need R + 48dp of half-height; side cards need
    // 0.866*R + 104dp of half-width. Both halves come from .tr-slot's
    // width/height in throw_radial.rcss — keep them in step.
    const float fit_v = play_h * 0.5f - 52.0f;
    const float fit_h = ( play_w * 0.5f - 108.0f ) / 0.866f;
    g.radius = std::max( 60.0f, std::min( want, std::min( fit_v, fit_h ) ) );
    g.valid = true;
    return g;
}

// Slot 0 at 12 o'clock, then clockwise. Screen +y is down, so up is -pi/2.
auto slot_angle_rad( int i ) -> float
{
    constexpr float pi = std::numbers::pi_v<float>;
    return ( -0.5f + 2.0f * static_cast<float>( i ) / SLOTS ) * pi;
}

// The pointer in dp, from whichever source is live. get_aim_mouse_pos() is the
// one that survives a modal loop: plain SDL_GetMouseState reports (0,0) unless a
// window holds mouse focus.
auto pointer_dp() -> std::pair<float, float>
{
    const point p = get_aim_mouse_pos();
    const float scale = ui_scale_safe();
    return { static_cast<float>( p.x ) / scale, static_cast<float>( p.y ) / scale };
}

auto slot_under_pointer( const ring_geom &g ) -> std::optional<int>
{
    if( !g.valid ) { return std::nullopt; }
const auto [mx, my] = pointer_dp();
const float dx = mx - g.cx;
const float dy = my - g.cy;
if( std::hypot( dx, dy ) < DEAD_ZONE_DP ) { return std::nullopt; }
    constexpr float pi = std::numbers::pi_v<float>;
    // Rotate so slot 0's centre lands on 0, then quantise to the nearest wedge.
    float t = ( std::atan2( dy, dx ) + 0.5f * pi ) / ( 2.0f * pi );
    t -= std::floor( t );
    return static_cast<int>( std::lround( t * SLOTS ) ) % SLOTS;
}

void apply_rect( Rml::ElementDocument *doc, const ring_geom &g )
{
    if( doc == nullptr || !g.valid ) { return; }
    for( int i = 0; i < SLOTS; ++i ) {
        Rml::Element *el = doc->GetElementById( "tr-slot-" + std::to_string( i ) );
        if( el == nullptr ) { continue; }
        const float a = slot_angle_rad( i );
        el->SetProperty( "left", rml::dp( g.cx + g.radius * std::cos( a ) ) );
        el->SetProperty( "top", rml::dp( g.cy + g.radius * std::sin( a ) ) );
    }
    if( Rml::Element *hub = doc->GetElementById( "tr-hub" ) ) {
        hub->SetProperty( "left", rml::dp( g.cx ) );
        hub->SetProperty( "top", rml::dp( g.cy ) );
    }
}

// Push slot contents + highlight state. `sel` is the pointed-at slot.
void sync( radial_session &s, Rml::ElementDocument *doc, avatar &u, std::optional<int> sel )
{
    const int active = u.get_active_throw_slot();
    for( int i = 0; i < SLOTS; ++i ) {
        const bool empty = u.is_throw_slot_empty( i );
        s.key_rml[i] = rml_escape( string_format( "%d", i + 1 ) );
        if( empty ) {
            s.name_rml[i] = rml_escape( _( "empty" ) );
            s.count_rml[i] = Rml::String();
        } else {
            s.name_rml[i] = rml_escape( u.get_throw_slot( i )->nname( 1 ) );
            s.count_rml[i] = rml_escape( string_format( "x%d", u.count_throwable( i ) ) );
        }
        if( doc != nullptr ) {
            if( Rml::Element *el = doc->GetElementById( "tr-slot-" + std::to_string( i ) ) ) {
                el->SetClass( "empty", empty );
                el->SetClass( "selected", sel.has_value() && *sel == i && !empty );
                el->SetClass( "active", i == active && !empty );
            }
        }
    }
    s.hub_title = rml_escape( _( "THROW" ) );
    if( !sel.has_value() ) {
        s.hub_sel = rml_escape( _( "point to choose" ) );
    } else if( u.is_throw_slot_empty( *sel ) ) {
        s.hub_sel = rml_escape( _( "- empty -" ) );
    } else {
        s.hub_sel = rml_escape( u.get_throw_slot( *sel )->nname( 1 ) );
    }
    s.handle.DirtyAllVariables();
}

} // namespace

auto show_throw_quickslot_radial( avatar &u ) -> throw_radial_result
{
    input_context ctxt( "THROW_RADIAL" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    // Catch-all so digits 1-6 can be read off the raw event, rather than adding
    // six bound actions to a menu whose real input is the pointer.
    ctxt.register_action( "ANY_INPUT" );

    // Declared before the doc so it outlives it — RmlUi holds raw pointers into
    // these members until RemoveDataModel (see rml_screen.h).
    std::unique_ptr<radial_session> data;
    rml_doc rml;

    rml.open( true, "throw_radial", ctxt, [&]( Rml::DataModelConstructor & c ) {
        data = std::make_unique<radial_session>();
        for( int i = 0; i < SLOTS; ++i ) {
            const std::string n = std::to_string( i );
            c.Bind( "s" + n + "_key", &data->key_rml[i] );
            c.Bind( "s" + n + "_name", &data->name_rml[i] );
            c.Bind( "s" + n + "_count", &data->count_rml[i] );
        }
        c.Bind( "hub_title", &data->hub_title );
        c.Bind( "hub_sel", &data->hub_sel );
        data->handle = c.GetModelHandle();
    }, /*passive=*/true );

    // Passive on purpose: the ring paints over the world but must not capture
    // input, because the pointer is SAMPLED every tick rather than clicked. An
    // interactive document would only learn the cursor moved from real mouse
    // events, and hold-mode aims while the player holds a key and never clicks.

    // No curses twin: with no document there is nothing to point at, so tell the
    // caller to fall back rather than spinning a loop over an invisible UI.
    if( !rml ) { return { .shown = false, .slot = std::nullopt }; }

// Hold vs sticky. non_modifier_keys_held() is the event-tracked physical
// count, so this works for any binding (including the CTRL+T default) without
// knowing which key fired the action. NOT const: a tap demotes it below.
bool hold_mode =
    get_option<bool>( "THROW_RADIAL_HOLD" ) && sdl_input::non_modifier_keys_held() > 0;
const uint64_t opened_at = get_sdl_ticks();

ring_geom geom = compute_ring();
const int active_slot = u.get_active_throw_slot();
std::optional<int> sel =
    active_slot >= 0 ? std::optional<int>( active_slot ) : std::nullopt;
sync( *data, rml.document(), u, sel );
apply_rect( rml.document(), geom );

// Only a slot that can actually be thrown commits; an empty wedge cancels.
const auto commit = [&u]( std::optional<int> s ) -> throw_radial_result {
    const bool ok = s.has_value() && *s >= 0 && *s < SLOTS && !u.is_throw_slot_empty( *s );
        return { .shown = true, .slot = ok ? s : std::nullopt };
    };

    while( true ) {
    ui_manager::redraw();
        const std::string action = ctxt.handle_input();

        if( action == "TIMEOUT" ) {
            // The 16 ms frame tick rml_doc::open() installed. This is the point
            // of the loop, not an idle case: it samples the pointer and notices a
            // hold ending. It must never fall through to a cancel — that is
            // exactly the self-dismissing bug query_popup had.
            geom = compute_ring();
            apply_rect( rml.document(), geom );
            const std::optional<int> hovered = slot_under_pointer( geom );
            if( hovered.has_value() && hovered != sel ) {
                sel = hovered;
                sync( *data, rml.document(), u, sel );
            }
            if( hold_mode && sdl_input::non_modifier_keys_held() == 0 ) {
                if( get_sdl_ticks() - opened_at < MIN_HOLD_MS ) {
                    // Released before the ring was usable: that was a TAP, not a
                    // flick. Demote to sticky and stay up so the player can aim,
                    // instead of auto-committing whatever happened to be lit.
                    hold_mode = false;
                } else {
                    return commit( sel );
                }
            }
            continue;
        }
        if( action == "QUIT" ) { return { .shown = true, .slot = std::nullopt }; }
        if( action == "CONFIRM" || action == "SELECT" ) { return commit( sel ); }
        if( action == "HELP_KEYBINDINGS" ) { continue; }

        // Digits pick a wedge outright — the keyboard route to the same choice.
        const int ch = ctxt.get_raw_input().get_first_input();
        if( ch >= '1' && ch < '1' + SLOTS ) { return commit( ch - '1' ); }
    }
}
