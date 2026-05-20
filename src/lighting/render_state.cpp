#include "render_state.h"

#include "shader_compiler.h"
#include "debug.h"

#include <atomic>

#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace lighting
{

namespace
{
// The hidden window the GPU device claims during phase 2i-A. Owned here so
// the WinDestroy path doesn't have to reach into render_state's internals
// to clean it up; the destructor below tears it down after the device has
// released its claim.
struct hidden_window_holder {
    SDL_Window *win = nullptr;
    ~hidden_window_holder() {
        if( win ) {
            SDL_DestroyWindow( win );
            win = nullptr;
        }
    }
};
hidden_window_holder secondary;
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

bool try_init_render_state()
{
    bool expected = false;
    if( !initialised.compare_exchange_strong( expected, true ) ) {
        return true; // already initialised
    }

    // Hidden secondary window — SDL_GPU exclusive-claims a window, so we
    // can't share the one SDL_CreateRenderer already owns. 1x1 is the
    // smallest SDL3 allows; the actual size doesn't matter because we
    // never present to it during phase 2i-A.
    secondary.win = SDL_CreateWindow( "cataclysm_gpu_warmup", 1, 1,
                                      SDL_WINDOW_HIDDEN );
    if( !secondary.win ) {
        dbg( DL::Warn ) << "lighting: hidden window create failed: " << SDL_GetError()
                        << " — phase 2i-A warmup skipped.";
        initialised.store( false );
        return false;
    }

    try {
        get_render_state().init( secondary.win );
        dbg( DL::Info ) << "lighting: render_state up (phase 2i-A warmup).";
        return true;
    } catch( const std::exception &e ) {
        dbg( DL::Warn ) << "lighting: render_state init failed: " << e.what()
                        << " — phase 2i-A warmup skipped. Legacy SDL_Renderer "
                        << "path unaffected; lighting rework cutover (phase 2i-B) "
                        << "will need this to succeed first.";
        SDL_DestroyWindow( secondary.win );
        secondary.win = nullptr;
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
    if( secondary.win ) {
        SDL_DestroyWindow( secondary.win );
        secondary.win = nullptr;
    }
}

} // namespace lighting
