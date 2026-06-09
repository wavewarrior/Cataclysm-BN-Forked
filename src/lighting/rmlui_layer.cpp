#include "rmlui_layer.h"

#include <cstdint>
#include <memory>
#include <string>

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
// Phase 4: the spike menu is a player UI — hidden by default, toggled with F10.
bool g_visible = false;
// init() tried once (success or failure); don't re-attempt every frame.
bool g_attempted = false;
// Phase 2 self-test quad path. Off now that a real document is loaded; flip true
// to isolate the pipeline from RmlUi layout/font if a document misbehaves.
bool g_self_test = false;
// Window kept for per-frame context sizing + the render-pass projection.
SDL_Window *g_window = nullptr;
// Loaded spike document (owned by the RmlUi context; not deleted manually).
Rml::ElementDocument *g_document = nullptr;

// Interfaces are held by raw pointer inside RmlUi, so they must outlive
// Rml::Shutdown(). Owned here; destroyed AFTER Rml::Shutdown() in shutdown().
std::unique_ptr<lighting::rmlui_render_interface> g_render;
std::unique_ptr<lighting::rmlui_system_interface> g_system;

// Owned by RmlUi core; destroyed by Rml::Shutdown(). Not deleted manually.
Rml::Context *g_context = nullptr;

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

    const std::string font = PATH_INFO::fontdir() + "Terminus.ttf";
    if( !Rml::LoadFontFace( font ) ) {
        // Non-fatal — boxes/colours still render; only text would be missing.
        dbg( DL::Warn ) << "rmlui_layer: LoadFontFace failed for " << font;
    }

    // Phase 4: load + show the styled spike menu. Non-fatal if missing (the
    // self-test path remains available by flipping g_self_test).
    const std::string doc = PATH_INFO::datadir() + "gui/spike_menu.rml";
    g_document = g_context->LoadDocument( doc );
    if( g_document != nullptr ) {
        g_document->Show();
        if( Rml::Element *first = g_document->GetElementById( "first" ) ) {
            first->Focus();  // so arrow-key (nav:auto) navigation has a start point
        }
        dbg( DL::Info ) << "rmlui_layer: loaded document " << doc;
    } else {
        dbg( DL::Warn ) << "rmlui_layer: LoadDocument failed for " << doc;
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
    g_document = nullptr;
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

bool &visible()
{
    return g_visible;
}

bool active()
{
    return g_ready && g_visible;
}

namespace
{
Rml::Input::KeyIdentifier map_key( SDL_Keycode k )
{
    switch( k ) {
        case SDLK_UP:
            return Rml::Input::KI_UP;
        case SDLK_DOWN:
            return Rml::Input::KI_DOWN;
        case SDLK_LEFT:
            return Rml::Input::KI_LEFT;
        case SDLK_RIGHT:
            return Rml::Input::KI_RIGHT;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return Rml::Input::KI_RETURN;
        case SDLK_ESCAPE:
            return Rml::Input::KI_ESCAPE;
        case SDLK_TAB:
            return Rml::Input::KI_TAB;
        case SDLK_BACKSPACE:
            return Rml::Input::KI_BACK;
        case SDLK_SPACE:
            return Rml::Input::KI_SPACE;
        default:
            return Rml::Input::KI_UNKNOWN;
    }
}

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
    if( !g_ready || !g_visible || g_context == nullptr ) {
        return false;
    }
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
        case SDL_EVENT_KEY_DOWN: {
            const Rml::Input::KeyIdentifier k = map_key( ev.key.key );
            if( k != Rml::Input::KI_UNKNOWN ) {
                g_context->ProcessKeyDown( k, mods );
            }
            return true;
        }
        case SDL_EVENT_KEY_UP: {
            const Rml::Input::KeyIdentifier k = map_key( ev.key.key );
            if( k != Rml::Input::KI_UNKNOWN ) {
                g_context->ProcessKeyUp( k, mods );
            }
            return true;
        }
        case SDL_EVENT_TEXT_INPUT:
            g_context->ProcessTextInput( Rml::String( ev.text.text ) );
            return true;
        default:
            return false;
    }
}

void new_frame()
{
    if( !g_ready ) {
        return;
    }
    // Drain deferred GPU-resource frees every frame, even while hidden.
    g_render->begin_frame();
    if( g_visible && g_context != nullptr && g_window != nullptr ) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels( g_window, &w, &h );
        if( w > 0 && h > 0 ) {
            g_context->SetDimensions( Rml::Vector2i( w, h ) );
        }
        g_context->Update();
    }
}

void prepare( SDL_GPUCommandBuffer *cb )
{
    if( g_ready && g_visible ) {
        // Upload geometry compiled this frame OUTSIDE the render pass (D3D12).
        g_render->upload_pending( cb );
    }
}

void render_in_pass( SDL_GPURenderPass *rp, SDL_GPUCommandBuffer *cb )
{
    if( !g_ready || !g_visible || g_window == nullptr ) {
        return;
    }
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels( g_window, &w, &h );
    const auto uw = static_cast<std::uint32_t>( w > 0 ? w : 1 );
    const auto uh = static_cast<std::uint32_t>( h > 0 ? h : 1 );
    // RmlUi context is sized in physical pixels, so projection == target.
    g_render->begin_render_pass( rp, cb, uw, uh, uw, uh );
    if( g_self_test ) {
        g_render->draw_self_test();  // Phase 2: prove the pipeline without a document
    } else if( g_context != nullptr ) {
        g_context->Render();
    }
    g_render->end_render_pass();
}

}  // namespace rmlui_layer
