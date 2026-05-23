#include "render_state.h"
#include "emitter_collector.h"
#include "sdf_pass.h"

#include "shader_compiler.h"
#include "debug.h"
#include "game_constants.h"

#include <algorithm>
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

void gpu_texture_deleter::operator()( SDL_GPUTexture *t ) const noexcept
{
    if( !t ) {
        return;
    }
    auto &rs = get_render_state();
    if( rs.ready() ) {
        SDL_ReleaseGPUTexture( rs.device().raw(), t );
    }
    // If render_state is already shut down, the GPU device is too —
    // SDL has released all underlying objects on device teardown. Leak
    // the handle (which now points at freed memory).
}

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

    SDL_GPUSamplerCreateInfo si{};
    si.min_filter    = SDL_GPU_FILTER_NEAREST;
    si.mag_filter    = SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode   = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    gpu_sampler_ = SDL_CreateGPUSampler( device_.raw(), &si );

    // Phase 3: start the emitter collector thread.
    collector_ = std::make_unique<emitter_collector>( *this );

    // Phase 4: initialise SDF + transparency textures.
    // get_map() cannot be used here (g is null at WinCreate time).
    // Use the default bubble size: 11 submaps × SEEX/SEEY = 132×132 tiles.
    // render_state::ensure_sdf_size() re-inits on first game load if actual
    // size differs.
    static constexpr int DEFAULT_MAP_TILES = 11 * 12; // MAPSIZE=11, SEEX=SEEY=12
    sdf_.init( device_, DEFAULT_MAP_TILES, DEFAULT_MAP_TILES );
}

void render_state::shutdown() noexcept
{
    // Phase 3: stop collector thread before releasing GPU resources.
    collector_.reset();

    // Phase 4: release SDF textures.
    sdf_.shutdown( device_ );

    if( device_.ready() && gpu_sampler_ ) {
        SDL_ReleaseGPUSampler( device_.raw(), gpu_sampler_ );
    }
    gpu_sampler_ = nullptr;

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

void render_state::set_tile_scissor( const SDL_Rect *rect )
{
    tile_batcher_.set_scissor( rect );
}

void render_state::clear_tile_scissor()
{
    tile_batcher_.set_scissor( nullptr );
}

void render_state::set_tile_lighting( SDL_GPUBuffer *emitter_ssbo,
                                       float tile_pixel_size,
                                       float z_level,
                                       Uint32 emitter_count,
                                       float ambient )
{
    tile_batcher_.set_lighting_resources( emitter_ssbo, tile_pixel_size,
                                           z_level, emitter_count, ambient );
}

void render_state::flush_ui_rects( sprite_batcher &dst )
{
    if( ui_rect_queue_.empty() ) {
        return;
    }
    // Drain WITHOUT clearing — clear_frame_queues() (called at the top
    // of each redraw cycle) is what resets the queue. This lets
    // refresh_display re-flush the same queue on no-input frames so
    // the swapchain shows the previous draw state instead of going
    // black when curses hasn't run the per-window draws this frame.
    dst.draw( ui_rect_queue_.data(), ui_rect_queue_.size() );
}

void render_state::clear_ui_queues() noexcept
{
    ui_rect_queue_.clear();
    font_glyph_queue_.clear();
}

void render_state::clear_tile_queue() noexcept
{
    tile_sprite_queue_.clear();
}

void render_state::clear_frame_queues() noexcept
{
    clear_ui_queues();
    clear_tile_queue();
}

SDL_GPUTexture *render_state::upload_surface_to_gpu_texture( SDL_Surface *surface )
{
    if( !device_.ready() || !surface || surface->w <= 0 || surface->h <= 0 ) {
        return nullptr;
    }

    // Force the source pixels into RGBA32 so the GPU format can be
    // hard-coded. SDL_ConvertSurface returns a new owning surface; only
    // free it if we actually converted.
    SDL_Surface *src = surface;
    SDL_Surface *converted = nullptr;
    if( surface->format != SDL_PIXELFORMAT_RGBA32 ) {
        converted = SDL_ConvertSurface( surface, SDL_PIXELFORMAT_RGBA32 );
        if( !converted ) {
            dbg( DL::Warn ) << "upload_surface_to_gpu_texture: SDL_ConvertSurface failed: "
                            << SDL_GetError();
            return nullptr;
        }
        src = converted;
    }

    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = static_cast<std::uint32_t>( src->w );
    tci.height = static_cast<std::uint32_t>( src->h );
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUTexture *tex = SDL_CreateGPUTexture( device_.raw(), &tci );
    if( !tex ) {
        dbg( DL::Warn ) << "upload_surface_to_gpu_texture: create tex: " << SDL_GetError();
        if( converted ) {
            SDL_DestroySurface( converted );
        }
        return nullptr;
    }

    const std::uint32_t row_bytes = static_cast<std::uint32_t>( src->w ) * 4;
    const std::uint32_t total_bytes = row_bytes * static_cast<std::uint32_t>( src->h );

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = total_bytes;
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer( device_.raw(), &tbi );
    if( !xfer ) {
        dbg( DL::Warn ) << "upload_surface_to_gpu_texture: xfer alloc: " << SDL_GetError();
        SDL_ReleaseGPUTexture( device_.raw(), tex );
        if( converted ) {
            SDL_DestroySurface( converted );
        }
        return nullptr;
    }
    void *mapped = SDL_MapGPUTransferBuffer( device_.raw(), xfer, false );
    if( !mapped ) {
        SDL_ReleaseGPUTransferBuffer( device_.raw(), xfer );
        SDL_ReleaseGPUTexture( device_.raw(), tex );
        if( converted ) {
            SDL_DestroySurface( converted );
        }
        return nullptr;
    }
    // SDL_Surface pitch may exceed w*4 (alignment padding). Copy row by
    // row to drop any trailing bytes the GPU upload doesn't expect.
    auto *dst = static_cast<std::uint8_t *>( mapped );
    const auto *psrc = static_cast<const std::uint8_t *>( src->pixels );
    for( int y = 0; y < src->h; ++y ) {
        std::memcpy( dst + static_cast<std::size_t>( y ) * row_bytes,
                     psrc + static_cast<std::size_t>( y ) * src->pitch,
                     row_bytes );
    }
    SDL_UnmapGPUTransferBuffer( device_.raw(), xfer );

    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer( device_.raw() );
    if( !cb ) {
        SDL_ReleaseGPUTransferBuffer( device_.raw(), xfer );
        SDL_ReleaseGPUTexture( device_.raw(), tex );
        if( converted ) {
            SDL_DestroySurface( converted );
        }
        return nullptr;
    }
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
    SDL_GPUTextureTransferInfo ti{};
    ti.transfer_buffer = xfer;
    ti.offset = 0;
    ti.pixels_per_row = static_cast<std::uint32_t>( src->w );
    ti.rows_per_layer = static_cast<std::uint32_t>( src->h );
    SDL_GPUTextureRegion region{};
    region.texture = tex;
    region.w = static_cast<std::uint32_t>( src->w );
    region.h = static_cast<std::uint32_t>( src->h );
    region.d = 1;
    SDL_UploadToGPUTexture( cp, &ti, &region, false );
    SDL_EndGPUCopyPass( cp );
    SDL_SubmitGPUCommandBuffer( cb );

    SDL_ReleaseGPUTransferBuffer( device_.raw(), xfer );
    if( converted ) {
        SDL_DestroySurface( converted );
    }
    return tex;
}

SDL_GPUTexture *render_state::create_rgba_gpu_texture( int w, int h )
{
    if( !device_.ready() || w <= 0 || h <= 0 ) {
        return nullptr;
    }
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = static_cast<std::uint32_t>( w );
    tci.height = static_cast<std::uint32_t>( h );
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture( device_.raw(), &tci );
    if( !tex ) {
        dbg( DL::Warn ) << "create_rgba_gpu_texture: " << SDL_GetError();
    }
    return tex;
}

bool render_state::upload_surface_subregion_to_gpu_texture(
    SDL_GPUTexture *dst, int dst_x, int dst_y,
    SDL_Surface *src, const SDL_Rect *src_rect )
{
    if( !device_.ready() || !dst || !src || src->w <= 0 || src->h <= 0 ) {
        return false;
    }
    SDL_Surface *psrc = src;
    SDL_Surface *converted = nullptr;
    if( src->format != SDL_PIXELFORMAT_RGBA32 ) {
        converted = SDL_ConvertSurface( src, SDL_PIXELFORMAT_RGBA32 );
        if( !converted ) {
            dbg( DL::Warn ) << "upload_surface_subregion: convert: " << SDL_GetError();
            return false;
        }
        psrc = converted;
    }

    // Sub-rect of the (potentially converted) source surface to upload.
    int sx = 0;
    int sy = 0;
    int sw = psrc->w;
    int sh = psrc->h;
    if( src_rect ) {
        sx = std::max( 0, src_rect->x );
        sy = std::max( 0, src_rect->y );
        sw = std::clamp( src_rect->w, 0, psrc->w - sx );
        sh = std::clamp( src_rect->h, 0, psrc->h - sy );
        if( sw <= 0 || sh <= 0 ) {
            if( converted ) {
                SDL_DestroySurface( converted );
            }
            return false;
        }
    }

    const std::uint32_t row_bytes = static_cast<std::uint32_t>( sw ) * 4;
    const std::uint32_t total_bytes = row_bytes * static_cast<std::uint32_t>( sh );

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = total_bytes;
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer( device_.raw(), &tbi );
    if( !xfer ) {
        dbg( DL::Warn ) << "upload_surface_subregion: xfer: " << SDL_GetError();
        if( converted ) {
            SDL_DestroySurface( converted );
        }
        return false;
    }
    void *mapped = SDL_MapGPUTransferBuffer( device_.raw(), xfer, false );
    if( !mapped ) {
        SDL_ReleaseGPUTransferBuffer( device_.raw(), xfer );
        if( converted ) {
            SDL_DestroySurface( converted );
        }
        return false;
    }
    auto *out = static_cast<std::uint8_t *>( mapped );
    const auto *in = static_cast<const std::uint8_t *>( psrc->pixels );
    // Copy only the sub-rect rows, packed tightly into the transfer
    // buffer; the GPU upload region below is sized to the sub-rect
    // dimensions so the upload doesn't touch neighbouring atlas cells.
    for( int y = 0; y < sh; ++y ) {
        const std::uint8_t *row_src = in
                                      + static_cast<std::size_t>( sy + y ) * psrc->pitch
                                      + static_cast<std::size_t>( sx ) * 4;
        std::memcpy( out + static_cast<std::size_t>( y ) * row_bytes,
                     row_src, row_bytes );
    }
    SDL_UnmapGPUTransferBuffer( device_.raw(), xfer );

    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer( device_.raw() );
    if( !cb ) {
        SDL_ReleaseGPUTransferBuffer( device_.raw(), xfer );
        if( converted ) {
            SDL_DestroySurface( converted );
        }
        return false;
    }
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
    SDL_GPUTextureTransferInfo ti{};
    ti.transfer_buffer = xfer;
    ti.offset = 0;
    // Tightly packed layout in the transfer buffer — sw pixels per row,
    // sh rows. SDL_GPU's 0-default for these means "use region.w/region.h",
    // but setting explicitly removes any ambiguity across backends.
    ti.pixels_per_row = static_cast<std::uint32_t>( sw );
    ti.rows_per_layer = static_cast<std::uint32_t>( sh );
    SDL_GPUTextureRegion region{};
    region.texture = dst;
    region.x = static_cast<std::uint32_t>( dst_x );
    region.y = static_cast<std::uint32_t>( dst_y );
    region.w = static_cast<std::uint32_t>( sw );
    region.h = static_cast<std::uint32_t>( sh );
    region.d = 1;
    SDL_UploadToGPUTexture( cp, &ti, &region, false );
    SDL_EndGPUCopyPass( cp );
    SDL_SubmitGPUCommandBuffer( cb );

    SDL_ReleaseGPUTransferBuffer( device_.raw(), xfer );
    if( converted ) {
        SDL_DestroySurface( converted );
    }
    return true;
}

void render_state::queue_font_glyph( SDL_GPUTexture *glyph_tex,
                                     float dst_x, float dst_y, float dst_w, float dst_h,
                                     float r, float g, float b, float a )
{
    // Full-texture sample — for callers with one texture per glyph
    // (CachedTTFFont).
    queue_font_glyph( glyph_tex, dst_x, dst_y, dst_w, dst_h,
                      0.0f, 0.0f, 1.0f, 1.0f, r, g, b, a );
}

void render_state::queue_font_glyph( SDL_GPUTexture *glyph_tex,
                                     float dst_x, float dst_y, float dst_w, float dst_h,
                                     float src_u, float src_v, float src_uw, float src_vh,
                                     float r, float g, float b, float a )
{
    if( !device_.ready() || !glyph_tex ) {
        return;
    }
    font_glyph_draw d{};
    d.texture = glyph_tex;
    d.inst.dst_x = dst_x;
    d.inst.dst_y = dst_y;
    d.inst.dst_w = dst_w;
    d.inst.dst_h = dst_h;
    d.inst.src_u = src_u;
    d.inst.src_v = src_v;
    d.inst.src_uw = src_uw;
    d.inst.src_vh = src_vh;
    d.inst.tint_r = r;
    d.inst.tint_g = g;
    d.inst.tint_b = b;
    d.inst.tint_a = a;
    font_glyph_queue_.push_back( d );
}

void render_state::flush_font_glyphs( sprite_batcher &dst, SDL_GPUSampler *sampler )
{
    if( font_glyph_queue_.empty() ) {
        return;
    }
    if( !sampler ) {
        font_glyph_queue_.clear();
        return;
    }
    // One set_texture + one draw per glyph. sprite_batcher's
    // set_texture() flushes the previous segment, so this is
    // equivalent to per-glyph SDL_DrawGPUPrimitives calls. Atlas
    // packing to fold these into one batch is a future opt.
    //
    // Drain WITHOUT clearing — clear_frame_queues() handles reset at
    // the top of each redraw cycle.
    for( const font_glyph_draw &g : font_glyph_queue_ ) {
        dst.set_texture( g.texture, sampler );
        dst.draw( g.inst );
    }
}

void render_state::queue_tile_sprite( SDL_GPUTexture *atlas_tex,
                                      const sprite_instance &inst )
{
    if( !device_.ready() || !atlas_tex ) {
        return;
    }
    tile_sprite_queue_.push_back( { atlas_tex, inst } );
}

void render_state::flush_tile_sprites( sprite_batcher &dst, SDL_GPUSampler *sampler )
{
    if( tile_sprite_queue_.empty() ) {
        return;
    }
    if( !sampler ) {
        tile_sprite_queue_.clear();
        return;
    }
    // Group consecutive same-texture draws under a single set_texture
    // call. set_texture is a no-op when the requested texture is
    // already bound, so atlas-packed runs naturally batch into one
    // segment.
    // Drain WITHOUT clearing — clear_frame_queues() handles reset at
    // the top of each redraw cycle.
    SDL_GPUTexture *bound = nullptr;
    for( const tile_sprite_draw &s : tile_sprite_queue_ ) {
        if( s.texture != bound ) {
            dst.set_texture( s.texture, sampler );
            bound = s.texture;
        }
        dst.draw( s.inst );
    }
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
