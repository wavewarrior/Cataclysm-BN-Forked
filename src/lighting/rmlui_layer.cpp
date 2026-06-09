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
// Phase 2: default visible so the self-test quad shows without input wiring
// (Phase 3 adds a toggle key). Phase 4 flips the self-test off for documents.
bool g_visible = true;
// init() tried once (success or failure); don't re-attempt every frame.
bool g_attempted = false;
// Phase 2: draw the render-interface self-test quad instead of Context::Render()
// (no document loaded yet). Phase 4 sets this false.
bool g_self_test = true;
// Window kept for per-frame context sizing + the render-pass projection.
SDL_Window *g_window = nullptr;

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
        // Non-fatal for the skeleton, but log it — a missing font means no text
        // will render once documents are loaded in Phase 4.
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

bool process_event( const SDL_Event & /*ev*/ )
{
    // PHASE 3: translate SDL -> Rml input and return whether RmlUi consumed it.
    return false;
}

void new_frame()
{
    if( !g_ready || !g_visible ) {
        return;
    }
    g_render->begin_frame();
    if( g_context != nullptr && g_window != nullptr ) {
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
