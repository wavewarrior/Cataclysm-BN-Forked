#include "rmlui_layer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include <RmlUi/Core.h>

#include "debug.h"
#include "gpu_device.h"
#include "path_info.h"
#include "rmlui_render_interface.h"
#include "rmlui_system_interface.h"

// Lighting/ files must define dbg themselves (not globally available).
#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace rmlui_layer
{

namespace
{

bool g_ready = false;
// init() tried once (success or failure); don't re-attempt every frame.
bool g_attempted = false;
// Window kept for per-frame context sizing + the render-pass projection.
SDL_Window *g_window = nullptr;

// Interfaces are held by raw pointer inside RmlUi, so they must outlive
// Rml::Shutdown(). Owned here; destroyed AFTER Rml::Shutdown() in shutdown().
std::unique_ptr<lighting::rmlui_render_interface> g_render;
std::unique_ptr<lighting::rmlui_system_interface> g_system;

// Owned by RmlUi core; destroyed by Rml::Shutdown(). Not deleted manually.
Rml::Context *g_context = nullptr;

// Last physical/logical pixel ratio applied to the context (HiDPI density).
float g_density_ratio = 1.0f;

// User UI-scale multiplier (F4 dev slider). Multiplies the HiDPI ratio applied to
// the context's dp lengths, scaling font + all dp spacing across every RmlUi panel.
// Does NOT touch g_density_ratio (input mapping keeps the true physical/logical ratio).
float g_ui_scale = 1.0f;

// CRT post-effect knobs (F4 sliders). See crt() / apply_crt().
crt_params g_crt;

// Documents currently open (shown) via open_document(), in open order. The
// layer is "active" while this is non-empty. Documents are owned by g_context;
// this only tracks which are live so active()/the frame gates can see them.
std::vector<Rml::ElementDocument *> g_open_docs;

bool any_open()
{
    return !g_open_docs.empty();
}

}  // namespace

bool init( lighting::gpu_device &dev )
{
    if( g_attempted ) {
        return g_ready;
    }
    if( !dev.ready() ) {
        dbg( DL::Info ) << "rmlui_layer: init skipped (device not ready)";
        return false;
    }
    g_attempted = true;
    g_window = dev.window_ptr();

    // Fail-safe: any failure degrades to "no RmlUi", never crashes the game.
    g_render = std::make_unique<lighting::rmlui_render_interface>();
    g_system = std::make_unique<lighting::rmlui_system_interface>();

    // Build the render interface's GPU resources (pipeline, white texture,
    // sampler) before RmlUi can call into it, on the live render_state device.
    if( !g_render->init( dev ) ) {
        dbg( DL::Error ) << "rmlui_layer: render interface init failed";
        g_render.reset();
        g_system.reset();
        return false;
    }

    Rml::SetRenderInterface( g_render.get() );
    Rml::SetSystemInterface( g_system.get() );

    if( !Rml::Initialise() ) {
        dbg( DL::Error ) << "rmlui_layer: Rml::Initialise failed";
        g_render->shutdown();
        g_render.reset();
        g_system.reset();
        return false;
    }

    int win_w = 0;
    int win_h = 0;
    SDL_GetWindowSizeInPixels( g_window, &win_w, &win_h );
    g_context = Rml::CreateContext( "main", Rml::Vector2i( win_w, win_h ) );
    if( g_context == nullptr ) {
        dbg( DL::Error ) << "rmlui_layer: CreateContext failed";
        Rml::Shutdown();
        g_render->shutdown();
        g_render.reset();
        g_system.reset();
        return false;
    }

    // Registered as a fallback (second arg) so glyphs still resolve even if a
    // document's font-family doesn't match. Family name embedded in the TTF is
    // "Terminus (TTF)" — the RCSS must use that exact string.
    const std::string font = PATH_INFO::fontdir() + "Terminus.ttf";
    if( !Rml::LoadFontFace( font, true ) ) {
        // Non-fatal — boxes/colours still render; only text would be missing.
        dbg( DL::Warn ) << "rmlui_layer: LoadFontFace failed for " << font;
    }

    g_ready = true;
    dbg( DL::Info ) << "rmlui_layer: init ok (" << win_w << "x" << win_h << ")";
    return true;
}

void shutdown()
{
    if( !g_attempted ) {
        return;
    }
    // Tear RmlUi down before the interfaces it points at, then release the
    // render interface's GPU resources before the device is destroyed.
    if( g_ready ) {
        Rml::Shutdown();
    }
    g_open_docs.clear();
    g_context = nullptr;
    if( g_render ) {
        g_render->shutdown();
    }
    g_render.reset();
    g_system.reset();
    g_window = nullptr;
    g_ready = false;
    g_attempted = false;
}

bool ready()
{
    return g_ready;
}

bool active()
{
    return g_ready && any_open();
}

Rml::Context *context()
{
    return g_ready ? g_context : nullptr;
}

float density_ratio()
{
    return g_density_ratio;
}

float &ui_scale()
{
    return g_ui_scale;
}

crt_params &crt()
{
    return g_crt;
}

Rml::ElementDocument *open_document( const std::string &rml_path )
{
    if( !g_ready || g_context == nullptr ) {
        return nullptr;
    }
    Rml::ElementDocument *doc = g_context->LoadDocument( rml_path );
    if( doc == nullptr ) {
        dbg( DL::Warn ) << "rmlui_layer: LoadDocument failed for " << rml_path;
        return nullptr;
    }
    doc->Show();
    g_open_docs.push_back( doc );
    dbg( DL::Info ) << "rmlui_layer: opened document " << rml_path
                    << " (" << g_open_docs.size() << " open)";
    return doc;
}

void close_document( Rml::ElementDocument *doc )
{
    if( doc == nullptr || g_context == nullptr ) {
        return;
    }
    const auto it = std::find( g_open_docs.begin(), g_open_docs.end(), doc );
    if( it == g_open_docs.end() ) {
        return;  // already closed / not ours
    }
    g_open_docs.erase( it );
    doc->Hide();
    g_context->UnloadDocument( doc );
}

namespace
{
int mod_state()
{
    const SDL_Keymod m = SDL_GetModState();
    int s = 0;
    if( m & SDL_KMOD_CTRL ) {
        s |= Rml::Input::KM_CTRL;
    }
    if( m & SDL_KMOD_SHIFT ) {
        s |= Rml::Input::KM_SHIFT;
    }
    if( m & SDL_KMOD_ALT ) {
        s |= Rml::Input::KM_ALT;
    }
    if( m & SDL_KMOD_GUI ) {
        s |= Rml::Input::KM_META;
    }
    return s;
}
}  // namespace

bool process_event( const SDL_Event &ev )
{
    if( !g_ready || !any_open() || g_context == nullptr ) {
        return false;
    }
    // Mouse only: keyboard belongs to the game's input_context, which drives
    // menu navigation. We forward mouse so :hover and row click events work.
    // Context is sized in physical pixels; SDL mouse coords are in window points.
    float sx = 1.f;
    float sy = 1.f;
    if( g_window != nullptr ) {
        int pw = 0;
        int ph = 0;
        int ww = 0;
        int wh = 0;
        SDL_GetWindowSizeInPixels( g_window, &pw, &ph );
        SDL_GetWindowSize( g_window, &ww, &wh );
        if( ww > 0 ) {
            sx = static_cast<float>( pw ) / ww;
        }
        if( wh > 0 ) {
            sy = static_cast<float>( ph ) / wh;
        }
    }
    const int mods = mod_state();
    switch( ev.type ) {
        case SDL_EVENT_MOUSE_MOTION:
            g_context->ProcessMouseMove( static_cast<int>( ev.motion.x * sx ),
                                         static_cast<int>( ev.motion.y * sy ), mods );
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const int btn = ev.button.button == SDL_BUTTON_LEFT ? 0
                            : ev.button.button == SDL_BUTTON_RIGHT ? 1
                            : ev.button.button == SDL_BUTTON_MIDDLE ? 2 : 3;
            if( ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ) {
                g_context->ProcessMouseButtonDown( btn, mods );
            } else {
                g_context->ProcessMouseButtonUp( btn, mods );
            }
            return true;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            // SDL: +y scrolls up; RmlUi: +delta scrolls down.
            g_context->ProcessMouseWheel( -ev.wheel.y, mods );
            return true;
        default:
            // Keyboard / text / everything else: let the game handle it.
            return false;
    }
}

namespace
{
// Authentic non-uniform CRT flicker — the aleclownes.com/2017 reference opacity
// table (20 steps, random-ish). Stepped (not interpolated) for the stutter; the
// `flicker` slider lerps from 1.0 (off) toward these values (mean ~0.5).
constexpr float CRT_FLICKER[20] = {
    0.27861f, 0.34769f, 0.23604f, 0.90626f, 0.18128f,
    0.83891f, 0.65583f, 0.67807f, 0.26559f, 0.84693f,
    0.96019f, 0.08594f, 0.20313f, 0.71988f, 0.53455f,
    0.37288f, 0.71428f, 0.70419f, 0.70030f, 0.24387f,
};

// 0..1 -> 0..255 alpha byte for an #rrggbbaa colour.
unsigned crt_a255( float a01 )
{
    return static_cast<unsigned>( std::lround( std::clamp( a01, 0.0f, 1.0f ) * 255.0f ) );
}

// Apply the F4 CRT knobs to every open document as inline RCSS gradient decorators,
// before Update() so they lay out this frame. Scanlines = repeating-linear-gradient
// on #crt-overlay (drawn on top, sized to the window in explicit px since an abspos
// width/height:100% resolves to zero here); the scroll is baked into the stop
// offsets (phase). Vignette = radial-gradient on every .panel.
void apply_crt()
{
    const double t = g_system ? g_system->GetElapsedTime() : 0.0;

    char vignette[160];
    if( g_crt.enabled ) {
        ( void )std::snprintf( vignette, sizeof( vignette ),
                               "radial-gradient( farthest-corner, #00000000, #000000%02x )",
                               crt_a255( g_crt.vignette_alpha ) );
    } else {
        ( void )std::snprintf( vignette, sizeof( vignette ), "none" );
    }

    for( Rml::ElementDocument *doc : g_open_docs ) {
        if( doc == nullptr ) {
            continue;
        }
        Rml::ElementList panels;
        doc->GetElementsByClassName( panels, "panel" );
        for( Rml::Element *pe : panels ) {
            pe->SetProperty( "decorator", vignette );
        }
        Rml::Element *overlay = doc->GetElementById( "crt-overlay" );
        if( overlay == nullptr ) {
            continue;
        }
        // Mask the scanlines to the active panel (the "device screen"): size the
        // overlay to the panel's border-box rect, not the whole window. Geometry is
        // from the previous frame's layout (apply runs before Update), but panels
        // don't move so the 1-frame lag is invisible; a zero box (first frame /
        // unlaid-out) just hides the overlay until it settles.
        Rml::Element *panel = panels.empty() ? nullptr : panels.front();
        Rml::Vector2f poff;
        Rml::Vector2f psz;
        if( panel != nullptr ) {
            poff = panel->GetAbsoluteOffset( Rml::BoxArea::Border );
            psz = panel->GetBox().GetSize( Rml::BoxArea::Border );
        }
        if( !g_crt.enabled || panel == nullptr || psz.x <= 0.f || psz.y <= 0.f ) {
            overlay->SetProperty( "display", "none" );
            continue;
        }
        const float pitch = std::max( 2.0f, g_crt.scanline_pitch );
        const float thick = std::clamp( g_crt.scanline_thickness, 0.5f, pitch - 0.5f );
        const float gap = pitch - thick;
        const float phase = static_cast<float>( std::fmod( t * g_crt.roll_speed, pitch ) );
        const unsigned sa = crt_a255( g_crt.scanline_alpha );
        // Flicker = a uniform black layer (under the scanlines) whose alpha pulses
        // per the random table. Modulating a thin scanline's opacity is invisible;
        // pulsing a full-panel tint reads as whole-screen CRT flicker. 0 -> no pulse.
        const int idx = static_cast<int>( std::fmod( t * 24.0, 20.0 ) );
        const float ftab = CRT_FLICKER[std::clamp( idx, 0, 19 )];
        const unsigned fa = crt_a255( g_crt.flicker * ( 1.0f - ftab ) );
        char dec[320];
        ( void )std::snprintf( dec, sizeof( dec ),
                               "linear-gradient( 0deg, #000000%02x, #000000%02x ), "
                               "repeating-linear-gradient( 0deg, "
                               "#00000000 %.2fpx, #00000000 %.2fpx, "
                               "#000000%02x %.2fpx, #000000%02x %.2fpx )",
                               fa, fa, phase, phase + gap, sa, phase + gap, sa, phase + pitch );
        overlay->SetProperty( "display", "block" );
        overlay->SetProperty( "position", "absolute" );
        overlay->SetProperty( "left", std::to_string( poff.x ) + "px" );
        overlay->SetProperty( "top", std::to_string( poff.y ) + "px" );
        overlay->SetProperty( "width", std::to_string( psz.x ) + "px" );
        overlay->SetProperty( "height", std::to_string( psz.y ) + "px" );
        overlay->SetProperty( "decorator", dec );
        overlay->SetProperty( "opacity", "1.0" );
    }
}
}  // namespace

void new_frame()
{
    if( !g_ready ) {
        return;
    }
    // Drain deferred GPU-resource frees every frame, even with nothing open.
    g_render->begin_frame();
    if( any_open() && g_context != nullptr && g_window != nullptr ) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels( g_window, &w, &h );
        if( w > 0 && h > 0 ) {
            g_context->SetDimensions( Rml::Vector2i( w, h ) );
            // Context is sized in PHYSICAL pixels so the GPU projection matches
            // the framebuffer. Set the density ratio = physical / logical so RCSS
            // `dp` units scale with HiDPI — without this, px-sized fonts render at
            // half the intended size on a 2x retina display (the "tiny font"). The
            // mouse sx/sy scaling in process_event handles the inverse for input.
            int lw = 0;
            int lh = 0;
            SDL_GetWindowSize( g_window, &lw, &lh );
            if( lw > 0 ) {
                g_density_ratio = static_cast<float>( w ) / static_cast<float>( lw );
                g_context->SetDensityIndependentPixelRatio( g_density_ratio * g_ui_scale );
            }
        }
        apply_crt();
        g_context->Update();
    }
}

void prepare( SDL_GPUCommandBuffer *cb )
{
    if( g_ready && any_open() && g_context != nullptr ) {
        // Pre-render OUTSIDE the render pass so geometry compiles immediately
        // (not deferred by begin_render_pass). Then upload_pending uploads the
        // compiled data to GPU buffers. The real render pass's ctx->Render()
        // reuses the cached handles and successfully renders on this same frame.
        g_context->Render();
        g_render->upload_pending( cb );
    }
}

void render_in_pass( SDL_GPURenderPass *rp, SDL_GPUCommandBuffer *cb )
{
    if( !g_ready || !any_open() || g_window == nullptr || g_context == nullptr ) {
        return;
    }
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels( g_window, &w, &h );
    const auto uw = static_cast<std::uint32_t>( w > 0 ? w : 1 );
    const auto uh = static_cast<std::uint32_t>( h > 0 ? h : 1 );
    // RmlUi context is sized in physical pixels, so projection == target.
    g_render->begin_render_pass( rp, cb, uw, uh, uw, uh );
    g_context->Render();  // renders all shown documents, z-ordered by open order
    g_render->end_render_pass();

    // D3D12 in-pass-upload watch: a non-zero textures count means a glyph atlas
    // uploaded mid-pass (the documented hazard). Geometry is deferred so a
    // non-zero compiles count is safe. Logs only on change.
    static std::uint32_t last_c = 0;
    static std::uint32_t last_t = 0;
    const std::uint32_t c = g_render->compiles_in_pass();
    const std::uint32_t t = g_render->textures_in_pass();
    if( c != last_c || t != last_t ) {
        last_c = c;
        last_t = t;
        DebugLogFL( DL::Info, DC::Main )
                << "rmlui in-pass uploads: compiles=" << c << " textures=" << t
                << " (0 = safe; geometry deferred, textures are the D3D12 watch)";
    }
}

}  // namespace rmlui_layer
