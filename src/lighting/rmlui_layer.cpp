#include "rmlui_layer.h"

#include <memory>
#include <string>

#include <SDL3/SDL.h>

#include <RmlUi/Core.h>

#include "debug.h"
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
bool g_visible = false;
// init() tried once (success or failure); don't re-attempt every frame.
bool g_attempted = false;

// Interfaces are held by raw pointer inside RmlUi, so they must outlive
// Rml::Shutdown(). Owned here; destroyed AFTER Rml::Shutdown() in shutdown().
std::unique_ptr<lighting::rmlui_render_interface> g_render;
std::unique_ptr<lighting::rmlui_system_interface> g_system;

// Owned by RmlUi core; destroyed by Rml::Shutdown(). Not deleted manually.
Rml::Context *g_context = nullptr;

}  // namespace

bool init( SDL_Window *window, SDL_GPUDevice *device )
{
    if( g_attempted ) {
        return g_ready;
    }
    if( window == nullptr || device == nullptr ) {
        dbg( DL::Info ) << "rmlui_layer: init skipped (null window/device)";
        return false;
    }
    g_attempted = true;

    // Fail-safe: any failure degrades to "no RmlUi", never crashes the game.
    g_render = std::make_unique<lighting::rmlui_render_interface>();
    g_system = std::make_unique<lighting::rmlui_system_interface>();
    Rml::SetRenderInterface( g_render.get() );
    Rml::SetSystemInterface( g_system.get() );

    if( !Rml::Initialise() ) {
        dbg( DL::Error ) << "rmlui_layer: Rml::Initialise failed";
        g_render.reset();
        g_system.reset();
        return false;
    }

    int win_w = 0;
    int win_h = 0;
    SDL_GetWindowSizeInPixels( window, &win_w, &win_h );
    g_context = Rml::CreateContext( "main", Rml::Vector2i( win_w, win_h ) );
    if( g_context == nullptr ) {
        dbg( DL::Error ) << "rmlui_layer: CreateContext failed";
        Rml::Shutdown();
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
    // Tear RmlUi down before the interfaces it points at.
    if( g_ready ) {
        Rml::Shutdown();
    }
    g_context = nullptr;
    g_render.reset();
    g_system.reset();
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
    if( g_ready && g_visible && g_context != nullptr ) {
        g_context->Update();
    }
}

void prepare( SDL_GPUCommandBuffer * /*cb*/ )
{
    // PHASE 2: upload this frame's geometry to the ring buffer (outside the pass).
}

void render_in_pass( SDL_GPURenderPass * /*rp*/, SDL_GPUCommandBuffer * /*cb*/ )
{
    // PHASE 2: g_context->Render() into the open swapchain pass.
}

}  // namespace rmlui_layer
