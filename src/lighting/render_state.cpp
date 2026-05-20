#include "render_state.h"

#include "shader_compiler.h"
#include "debug.h"

#include <atomic>

#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace lighting
{

namespace
{
// Atomic gate so try_init / shutdown are safe to call more than once.
// Phase 2i-A's hidden-window holder is gone — the GPU device now claims
// the *visible* window that the caller hands us.
std::atomic<bool> initialised{ false };

} // namespace

void render_state::init( SDL_Window *host_window )
{
    device_.init( host_window, /*debug=*/false, /*vsync=*/false );

    init_shader_compiler();

    const SDL_GPUTextureFormat fmt = device_.swapchain_format();

    pipeline_desc tile_desc{};
    tile_desc.color_target_format = fmt;
    tile_batcher_.init( device_, tile_desc, "tile_batcher" );

    pipeline_desc ui_desc{};
    ui_desc.color_target_format = fmt;
    ui_batcher_.init( device_, ui_desc, "ui_batcher" );

    fonts_.init( device_, fmt );
    atlas_.init( device_ );
    geometry_.init( device_ );
}

void render_state::shutdown() noexcept
{
    // Tear-down order is the reverse of init so each component still sees
    // a live device when it releases its own GPU resources.
    geometry_.shutdown();
    atlas_.shutdown();
    fonts_.shutdown();
    ui_batcher_.shutdown();
    tile_batcher_.shutdown();
    shutdown_shader_compiler();
    device_.shutdown();
}

render_state &get_render_state()
{
    static render_state instance;
    return instance;
}

bool init_render_state_on( SDL_Window *visible_window )
{
    bool expected = false;
    if( !initialised.compare_exchange_strong( expected, true ) ) {
        return true; // already initialised
    }
    if( !visible_window ) {
        initialised.store( false );
        dbg( DL::Warn ) << "lighting: init_render_state_on(nullptr)";
        return false;
    }

    try {
        get_render_state().init( visible_window );
        dbg( DL::Info ) << "lighting: render_state up on visible window.";
        return true;
    } catch( const std::exception &e ) {
        dbg( DL::Warn ) << "lighting: render_state init failed: " << e.what();
        initialised.store( false );
        return false;
    }
}

void shutdown_render_state() noexcept
{
    bool expected = true;
    if( !initialised.compare_exchange_strong( expected, false ) ) {
        return;
    }
    get_render_state().shutdown();
}

} // namespace lighting
