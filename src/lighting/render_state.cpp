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

    // Phase 4: SDF / transparency / sky_vis textures.
    //
    // Size the GPU textures for the WORST-CASE bubble (REALITY_BUBBLE_SIZE_MAX
    // × SEEX). The CPU vectors and shader sampling use the *runtime* mapsize
    // passed into sdf_pass::upload() each frame — texture is just storage.
    //
    // Why not g_mapsize at init time:
    //   WinCreate runs BEFORE options are fully resolved into g_mapsize for
    //   some configurations (and before world load can override it). Init-time
    //   g_mapsize=11 + world load to g_mapsize=15 produced a 132x132 texture
    //   fed 180x180 of CPU data — the upload's first 17424 of 32400 floats
    //   landed in the texture with completely garbled row mapping → shader
    //   sampled wrong tiles → SDF view uniformly red.
    //
    // Max-size cost: 192² × (4 sdf + 1 trans + 1 sky_vis + 4 storage) ≈ 370 KB
    // GPU. Per-frame upload only touches runtime_w × runtime_h tiles.
    const int rt_tiles = REALITY_BUBBLE_SIZE_MAX * SEEX;
    sdf_.init( device_, rt_tiles, rt_tiles );

    // Radiance-cascade GI pass (Step-3 Phase 2). Its output cascade texture is
    // sized to the same physical map extent as the SDF (square), stored
    // transposed as a drop-in for the sprite's IndirectTex.
    rc_.init( device_, static_cast<std::uint32_t>( rt_tiles ),
              static_cast<std::uint32_t>( rt_tiles ) );

    // UI compositor target. Sized to the PHYSICAL (drawable) swapchain pixels
    // so the composite blit is 1:1; the resize hook in sdltiles keeps it in
    // sync with window size changes. A failed alloc leaves ui_target() != null
    // with a null texture(); callers must guard on texture() before use.
    {
        int pw = 0;
        int ph = 0;
        SDL_GetWindowSizeInPixels( host_window, &pw, &ph );
        if( pw <= 0 || ph <= 0 ) {
            pw = 1;
            ph = 1;
        }
        ui_target_ = std::make_unique<ui_composite_target>();
        ui_target_->init( device_, pw, ph );

        // World accumulation layer. HDR (RGBA16F) so the lit result keeps
        // values >1 for the tonemap pass instead of clipping at the 8-bit
        // store. Guard on format support (universal on the min-spec, but a
        // silent miss = black screen) and fall back to swapchain 8-bit, which
        // is the pre-1b behaviour (identity tonemap → unchanged output).
        // init() leaves it dirty → the world pass treats that as "needs full
        // clear" on the first frame.
        SDL_GPUTextureFormat world_fmt = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        if( !SDL_GPUTextureSupportsFormat( device_.raw(), world_fmt,
                SDL_GPU_TEXTURETYPE_2D,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER ) ) {
            dbg( DL::Error ) << "render_state: RGBA16F unsupported as color+sampler "
                             "target; HDR world target falls back to swapchain 8-bit "
                             "(bright lighting will clip).";
            world_fmt = device_.swapchain_format();
        } else {
            dbg( DL::Info ) << "render_state: HDR world target = RGBA16F.";
        }
        world_target_ = std::make_unique<ui_composite_target>();
        world_target_->init( device_, pw, ph, world_fmt );

        // Tonemapped LDR resolve target (swapchain format) + the fullscreen
        // tonemap pass that fills it from world_target_. In step 1a the world
        // target is still 8-bit and the tonemap shader is identity, so this is
        // a pixel-identical passthrough; the RGBA16F flip (1b) and AgX (1c)
        // build on this seam. (LIGHTING_REWORK_PLAN.md step 1.)
        world_ldr_target_ = std::make_unique<ui_composite_target>();
        world_ldr_target_->init( device_, pw, ph );
        tonemap_.init( device_, device_.swapchain_format() );
    }
}

void render_state::shutdown() noexcept
{
    // Phase 3: stop collector thread before releasing GPU resources.
    collector_.reset();

    // Release the compositor textures + tonemap pass while the device is live.
    ui_target_.reset();
    world_target_.reset();
    world_ldr_target_.reset();
    tonemap_.shutdown();
    rc_.shutdown();

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
    // Route into the current adaptor's retained slice if we're inside a
    // redraw_cb. Otherwise fall through to the composited output (e.g.
    // background fills queued outside the ui_manager redraw loop).
    if( current_slices_ ) {
        current_slices_->ui_rects.push_back( s );
    } else if( transient_routing_ ) {
        ui_rect_transient_.push_back( s );
    } else {
        ui_rect_queue_.push_back( s );
    }
}

void render_state::set_tile_scissor( const SDL_Rect *rect )
{
    tile_batcher_.set_scissor( rect );
}

void render_state::clear_tile_scissor()
{
    tile_batcher_.set_scissor( nullptr );
}

void render_state::begin_lighting_frame( const frame_light_inputs &in )
{
    // Resolve everything render_state owns internally: emitter storage
    // buffer + count from the collector, SDF + sky_vis textures + dims
    // from the sdf_pass, sampler from this object. Caller only provides
    // what it alone knows (camera, time, tile geometry, ambient).
    SDL_GPUBuffer  *ebuf = collector_ ? collector_->emitter_buffer() : nullptr;
    // SDF buffer / sky_vis texture only get exposed to the shader once at
    // least one upload has populated them. Until then, the bytes are
    // undefined → the shader's shadow march would read s≈0 → shadow=0 and
    // zero out all emitter contribution past ~1 tile. Surface as null +
    // sdf_map_w/h=0 to take the no-SDF code path in the shader.
    // SDF + sky_vis are fragment storage buffers now (Metal mis-binds
    // sampler-texture Load → returned 0 for every fragment).
    const bool      sdf_ready = sdf_.populated();
    SDL_GPUBuffer  *sbuf = sdf_ready ? sdf_.sdf_buffer()      : nullptr;
    SDL_GPUBuffer  *kvis = sdf_ready ? sdf_.sky_vis_buffer()  : nullptr;
    SDL_GPUBuffer  *vbuf = sdf_ready ? sdf_.vis_buffer()      : nullptr;
    // GI source for the sprite's IndirectTex: the GPU radiance-cascade gather
    // (Phase 2) when enabled + ready, else the CPU 1-bounce indirect texture.
    // Both share the transposed drop-in layout, so this is a pure bind swap —
    // the A/B oracle that lets RC be validated against the known-good CPU path
    // (the CPU path is retired in Phase 4). cascade_tex_ is cleared at init, so
    // it is safe to bind before the first gather (reads as no-GI).
    SDL_GPUTexture *itex = nullptr;
    if( sdf_ready ) {
        itex = ( gi_use_rc_ && rc_.ready() ) ? rc_.cascade_texture()
                                             : sdf_.indirect_texture();
    }
    const Uint32 ne = collector_
                      ? static_cast<Uint32>( collector_->last_count() )
                      : 0u;
    const Uint32 sw = sdf_ready ? static_cast<Uint32>( sdf_.map_w() ) : 0u;
    const Uint32 sh = sdf_ready ? static_cast<Uint32>( sdf_.map_h() ) : 0u;

    tile_batcher_.set_lighting_resources(
        in.tile_pixel_size, in.z_level, ne, in.ambient,
        in.camera_off_x, in.camera_off_y, sw, sh,
        ebuf, sbuf, gpu_sampler_, kvis, itex, vbuf, &in.sun, &in.debug );
}

void render_state::flush_ui_rects( sprite_batcher &dst )
{
    // Composited queue: drain WITHOUT clearing — clear_ui_queues() at the
    // top of each ui_manager redraw cycle resets it. Lets refresh_display
    // re-flush the same draws on no-input frames instead of going black.
    if( !ui_rect_queue_.empty() ) {
        dst.draw( ui_rect_queue_.data(), ui_rect_queue_.size() );
    }
    // Transient overlay queue: drain AND clear every frame. refresh_display
    // re-populates this each frame (e.g. LIGHT-DBG widget); without the
    // per-frame clear the same overlay pushes would pile up indefinitely.
    if( !ui_rect_transient_.empty() ) {
        dst.draw( ui_rect_transient_.data(), ui_rect_transient_.size() );
        ui_rect_transient_.clear();
    }
}

void render_state::clear_ui_queues() noexcept
{
    ui_rect_queue_.clear();
    font_glyph_queue_.clear();
    // Defensive: transient queues are normally drained by flush each
    // frame; clearing here too prevents leftovers leaking across a
    // ui_manager redraw cycle if a flush was skipped (no swapchain etc).
    ui_rect_transient_.clear();
    font_glyph_transient_.clear();
    // The composited UI is about to be rebuilt (ui_manager re-appends every
    // adaptor's slice after this). Mark the compositor dirty so Pass A
    // re-renders it this frame. On frames where this is NOT called (a bare
    // refresh_display re-fire with no ui_manager redraw cycle), the compositor
    // stays clean → Pass A is skipped → the persistent texture is reused,
    // which is the partial-redraw flicker fix.
    if( ui_target_ ) {
        ui_target_->invalidate();
    }
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
                                     float r, float g, float b, float a,
                                     bool lit, float rotation )
{
    // Full-texture sample — for callers with one texture per glyph
    // (CachedTTFFont).
    queue_font_glyph( glyph_tex, dst_x, dst_y, dst_w, dst_h,
                      0.0f, 0.0f, 1.0f, 1.0f, r, g, b, a, lit, rotation );
}

void render_state::queue_font_glyph( SDL_GPUTexture *glyph_tex,
                                     float dst_x, float dst_y, float dst_w, float dst_h,
                                     float src_u, float src_v, float src_uw, float src_vh,
                                     float r, float g, float b, float a,
                                     bool lit, float rotation )
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
    d.inst.rotation = rotation; // radians, clockwise about the quad centre
    d.lit         = lit;
    if( current_slices_ ) {
        current_slices_->font_glyphs.push_back( d );
    } else if( transient_routing_ ) {
        font_glyph_transient_.push_back( d );
    } else {
        font_glyph_queue_.push_back( d );
    }
}

void render_state::append_ui_rects( const std::vector<sprite_instance> &src )
{
    ui_rect_queue_.insert( ui_rect_queue_.end(), src.begin(), src.end() );
}

void render_state::append_font_glyphs( const std::vector<font_glyph_draw> &src )
{
    font_glyph_queue_.insert( font_glyph_queue_.end(), src.begin(), src.end() );
}

void render_state::flush_font_glyphs( sprite_batcher &dst, SDL_GPUSampler *sampler )
{
    if( font_glyph_queue_.empty() && font_glyph_transient_.empty() ) {
        return;
    }
    if( !sampler ) {
        font_glyph_transient_.clear();
        return;
    }
    // One set_texture + one draw per glyph. set_texture() flushes the
    // previous segment, so each iteration is one SDL_DrawGPUPrimitives.
    // Composited queue first (z-order: under transient overlays).
    // Composited: drain WITHOUT clearing (ui_manager owns the reset).
    // Transient: drain AND clear every frame (re-populated each frame).
    for( const font_glyph_draw &g : font_glyph_queue_ ) {
        // Pass per-glyph `lit` flag so HUD glyphs (default false) skip the
        // lighting fragment-shader path, while future world-space text
        // (queued with lit=true) goes through the full lit segment.
        dst.set_texture( g.texture, sampler, g.lit );
        dst.draw( g.inst );
    }
    for( const font_glyph_draw &g : font_glyph_transient_ ) {
        dst.set_texture( g.texture, sampler, g.lit );
        dst.draw( g.inst );
    }
    font_glyph_transient_.clear();
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
