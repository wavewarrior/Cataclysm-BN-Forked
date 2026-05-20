#include "render_state.h"

#include "shader_compiler.h"
#include "debug.h"

#include <atomic>
#include <cstring>

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
    // Drop bridge resources first while the device is still alive.
    if( device_.ready() ) {
        if( bridge_xfer_ ) {
            SDL_ReleaseGPUTransferBuffer( device_.raw(), bridge_xfer_ );
        }
        if( bridge_sampler_ ) {
            SDL_ReleaseGPUSampler( device_.raw(), bridge_sampler_ );
        }
        if( bridge_tex_ ) {
            SDL_ReleaseGPUTexture( device_.raw(), bridge_tex_ );
        }
    }
    bridge_xfer_ = nullptr;
    bridge_sampler_ = nullptr;
    bridge_tex_ = nullptr;
    bridge_xfer_capacity_ = 0;
    bridge_w_ = bridge_h_ = 0;

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

void render_state::queue_ui_rect( float x, float y, float w, float h,
                                  float r, float g, float b, float a )
{
    if( !device_.ready() ) {
        return;
    }
    sprite_instance s{};
    s.dst_x = x;
    s.dst_y = y;
    s.dst_w = w;
    s.dst_h = h;
    s.src_u = 0.5f;
    s.src_v = 0.5f;
    s.src_uw = 0.0f;
    s.src_vh = 0.0f;
    s.tint_r = r;
    s.tint_g = g;
    s.tint_b = b;
    s.tint_a = a;
    ui_rect_queue_.push_back( s );
}

void render_state::flush_ui_rects( sprite_batcher &dst )
{
    if( ui_rect_queue_.empty() ) {
        return;
    }
    dst.draw( ui_rect_queue_.data(), ui_rect_queue_.size() );
    ui_rect_queue_.clear();
}

bool render_state::bridge_ready( int w, int h )
{
    if( !device_.ready() || w <= 0 || h <= 0 ) {
        return false;
    }
    if( bridge_tex_ && bridge_w_ == w && bridge_h_ == h ) {
        return true;
    }
    // Drop and recreate on size change.
    if( bridge_tex_ ) {
        SDL_ReleaseGPUTexture( device_.raw(), bridge_tex_ );
        bridge_tex_ = nullptr;
    }
    SDL_GPUTextureCreateInfo tci{};
    tci.type   = SDL_GPU_TEXTURETYPE_2D;
    // BGRA8 is the byte-order match for SDL_PIXELFORMAT_ARGB8888 on
    // little-endian platforms — the legacy display_buffer's format. Direct
    // memcpy from the readback surface lands the right channels without a
    // per-pixel swizzle.
    tci.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    tci.usage  = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width  = static_cast<Uint32>( w );
    tci.height = static_cast<Uint32>( h );
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    bridge_tex_ = SDL_CreateGPUTexture( device_.raw(), &tci );
    if( !bridge_tex_ ) {
        return false;
    }
    bridge_w_ = w;
    bridge_h_ = h;

    if( !bridge_sampler_ ) {
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter = SDL_GPU_FILTER_NEAREST;
        si.mag_filter = SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        bridge_sampler_ = SDL_CreateGPUSampler( device_.raw(), &si );
        if( !bridge_sampler_ ) {
            SDL_ReleaseGPUTexture( device_.raw(), bridge_tex_ );
            bridge_tex_ = nullptr;
            bridge_w_ = 0;
            bridge_h_ = 0;
            return false;
        }
    }
    return true;
}

void render_state::bridge_upload( SDL_GPUCommandBuffer *cb,
                                  const void *pixels, std::uint32_t row_stride,
                                  int w, int h )
{
    if( !bridge_tex_ || !cb || !pixels || w <= 0 || h <= 0 ) {
        return;
    }
    const std::uint32_t bytes_per_row = static_cast<std::uint32_t>( w ) * 4;
    const std::uint32_t needed = bytes_per_row * static_cast<std::uint32_t>( h );
    if( !bridge_xfer_ || bridge_xfer_capacity_ < needed ) {
        if( bridge_xfer_ ) {
            SDL_ReleaseGPUTransferBuffer( device_.raw(), bridge_xfer_ );
            bridge_xfer_ = nullptr;
        }
        // Round up to a multiple of 4 MiB so window resizes don't churn.
        const std::uint32_t round = ( needed + ( 4u << 20 ) - 1 ) & ~( ( 4u << 20 ) - 1 );
        SDL_GPUTransferBufferCreateInfo tci{};
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size  = round;
        bridge_xfer_ = SDL_CreateGPUTransferBuffer( device_.raw(), &tci );
        if( !bridge_xfer_ ) {
            return;
        }
        bridge_xfer_capacity_ = round;
    }
    void *mapped = SDL_MapGPUTransferBuffer( device_.raw(), bridge_xfer_, /*cycle=*/true );
    if( !mapped ) {
        return;
    }
    auto *dstp = static_cast<unsigned char *>( mapped );
    const auto *srcp = static_cast<const unsigned char *>( pixels );
    for( int y = 0; y < h; ++y ) {
        std::memcpy( dstp, srcp, bytes_per_row );
        dstp += bytes_per_row;
        srcp += row_stride;
    }
    SDL_UnmapGPUTransferBuffer( device_.raw(), bridge_xfer_ );

    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
    if( !cp ) {
        return;
    }
    SDL_GPUTextureTransferInfo info{};
    info.transfer_buffer = bridge_xfer_;
    info.offset = 0;

    SDL_GPUTextureRegion region{};
    region.texture = bridge_tex_;
    region.w = static_cast<Uint32>( w );
    region.h = static_cast<Uint32>( h );
    region.d = 1;
    SDL_UploadToGPUTexture( cp, &info, &region, /*cycle=*/false );
    SDL_EndGPUCopyPass( cp );
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
