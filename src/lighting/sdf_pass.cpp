#include "lighting/sdf_pass.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "debug.h"
#include "lighting/gpu_device.h"
#include "sdl_wrappers.h"

#define dbg(x) DebugLogFL((x),DC::SDL)

namespace lighting
{

// ---------------------------------------------------------------------------
// GPU texture management
// ---------------------------------------------------------------------------

sdf_pass::~sdf_pass()
{
    // Caller must call shutdown() with the device before destruction.
    // If transparency_tex_ is still non-null, it means shutdown was skipped —
    // we can't release GPU resources without a device pointer here.
}

void sdf_pass::init( gpu_device &dev, int map_w, int map_h )
{
    if( !dev.ready() || map_w <= 0 || map_h <= 0 ) {
        return;
    }
    map_w_ = map_w;
    map_h_ = map_h;

    SDL_GPUDevice *d = dev.raw();

    // Transparency texture: R8 UNORM, 1 byte per tile.
    {
        SDL_GPUTextureCreateInfo tci{};
        tci.type                 = SDL_GPU_TEXTURETYPE_2D;
        tci.format               = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        tci.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER
                                   | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
        tci.width                = static_cast<Uint32>( map_w );
        tci.height               = static_cast<Uint32>( map_h );
        tci.layer_count_or_depth = 1;
        tci.num_levels           = 1;
        tci.sample_count         = SDL_GPU_SAMPLECOUNT_1;
        transparency_tex_ = SDL_CreateGPUTexture( d, &tci );
        if( !transparency_tex_ ) {
            dbg( DL::Error ) << "sdf_pass::init: failed to create transparency_tex";
        }
    }

    // P3.3: sdf_tex_ (R32F) and sky_vis_tex_ deleted — nothing samples them.
    // SDF reads from sdf_storage_; sky_vis reads from skyvis_storage_.

    // Transfer buffers: one for transparency (R8), one for sky_vis_f (R32F).
    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = static_cast<Uint32>( map_w * map_h ); // 1 byte per tile
        xfer_transparency_ = SDL_CreateGPUTransferBuffer( d, &tbci );
    }
    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = static_cast<Uint32>( map_w * map_h ); // 1 byte per tile
        xfer_sky_vis_ = SDL_CreateGPUTransferBuffer( d, &tbci );
    }
    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = static_cast<Uint32>( map_w * map_h * 4 ); // 4 bytes per tile (float)
        xfer_skyvis_f_ = SDL_CreateGPUTransferBuffer( d, &tbci );
    }
    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        // VisBuf shares the SDF_SUPERSAMPLE grid (sub-tile vision-edge sampling).
        tbci.size  = static_cast<Uint32>( map_w * map_h * SDF_SUPERSAMPLE * SDF_SUPERSAMPLE * 4 );
        xfer_vis_f_ = SDL_CreateGPUTransferBuffer( d, &tbci );
    }
    {
        // Stage 2b coverage occluder: tile-res, 2 floats/tile (height, roof).
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = static_cast<Uint32>( map_w * map_h * 2 * 4 );
        xfer_occ_ = SDL_CreateGPUTransferBuffer( d, &tbci );
    }
    {
        // P3 JFA input: tile-res transparency as floats (0.0=opaque .. 1.0=open).
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = static_cast<Uint32>( map_w * map_h * 4 );
        xfer_trans_f_ = SDL_CreateGPUTransferBuffer( d, &tbci );
    }

    // SDF + sky-vis as fragment-readable storage buffers (sampler-texture
    // Load returns 0 on Metal). Same data as the textures, as float arrays.
    {
        SDL_GPUBufferCreateInfo bci{};
        // GRAPHICS read (sprite.frag SdfBuf) + COMPUTE read (gi_field/gi_bounce
        // sphere-march the same SDF on the GPU compute GI path) + COMPUTE write
        // (P3.3: JFA resolve pass writes directly to sdf_storage_).
        bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
                    SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
                    SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
        // SDF_SUPERSAMPLE² subcells per tile, 4 bytes each.
        bci.size  = static_cast<Uint32>( map_w * map_h * SDF_SUPERSAMPLE * SDF_SUPERSAMPLE * 4 );
        sdf_storage_ = SDL_CreateGPUBuffer( d, &bci );
        if( !sdf_storage_ ) {
            dbg( DL::Error ) << "sdf_pass::init: failed to create sdf_storage";
        }
    }
    {
        // GRAPHICS read (sprite.frag SkyVisBuf) + COMPUTE read (sky_sun.comp
        // portal-tests it to find open-sky directions — Stage 2a).
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
                    SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
        bci.size  = static_cast<Uint32>( map_w * map_h * 4 );
        skyvis_storage_ = SDL_CreateGPUBuffer( d, &bci );
        if( !skyvis_storage_ ) {
            dbg( DL::Error ) << "sdf_pass::init: failed to create skyvis_storage";
        }
    }
    {
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        // VisBuf shares the SDF_SUPERSAMPLE grid (SS² floats per tile).
        bci.size  = static_cast<Uint32>( map_w * map_h * SDF_SUPERSAMPLE * SDF_SUPERSAMPLE * 4 );
        visbuf_storage_ = SDL_CreateGPUBuffer( d, &bci );
        if( !visbuf_storage_ ) {
            dbg( DL::Error ) << "sdf_pass::init: failed to create visbuf_storage";
        }
    }
    {
        // Stage 2b unified coverage occluder field — tile-res, 2 floats/tile
        // (height, roof). COMPUTE read (sky_sun.comp marches it).
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
                    SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
        bci.size  = static_cast<Uint32>( map_w * map_h * 2 * 4 );
        occ_storage_ = SDL_CreateGPUBuffer( d, &bci );
        if( !occ_storage_ ) {
            dbg( DL::Error ) << "sdf_pass::init: failed to create occ_storage";
        }
    }
    {
        // P3 JFA input: tile-res transparency as floats (0.0=opaque .. 1.0=open).
        // COMPUTE read so the seed shader can consume it directly.
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
                    SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
        bci.size  = static_cast<Uint32>( map_w * map_h * 4 );
        trans_storage_ = SDL_CreateGPUBuffer( d, &bci );
        if( !trans_storage_ ) {
            dbg( DL::Error ) << "sdf_pass::init: failed to create trans_storage";
        }
    }
}

void sdf_pass::shutdown( gpu_device &dev )
{
    SDL_GPUDevice *d = dev.raw();
    if( !d ) {
        return;
    }
    if( xfer_transparency_ ) {
        SDL_ReleaseGPUTransferBuffer( d, xfer_transparency_ );
        xfer_transparency_ = nullptr;
    }
    if( xfer_sky_vis_ ) {
        SDL_ReleaseGPUTransferBuffer( d, xfer_sky_vis_ );
        xfer_sky_vis_ = nullptr;
    }
    if( xfer_skyvis_f_ ) {
        SDL_ReleaseGPUTransferBuffer( d, xfer_skyvis_f_ );
        xfer_skyvis_f_ = nullptr;
    }
    if( xfer_vis_f_ ) {
        SDL_ReleaseGPUTransferBuffer( d, xfer_vis_f_ );
        xfer_vis_f_ = nullptr;
    }
    if( sdf_storage_ ) {
        SDL_ReleaseGPUBuffer( d, sdf_storage_ );
        sdf_storage_ = nullptr;
    }
    if( skyvis_storage_ ) {
        SDL_ReleaseGPUBuffer( d, skyvis_storage_ );
        skyvis_storage_ = nullptr;
    }
    if( visbuf_storage_ ) {
        SDL_ReleaseGPUBuffer( d, visbuf_storage_ );
        visbuf_storage_ = nullptr;
    }
    if( xfer_occ_ ) {
        SDL_ReleaseGPUTransferBuffer( d, xfer_occ_ );
        xfer_occ_ = nullptr;
    }
    if( occ_storage_ ) {
        SDL_ReleaseGPUBuffer( d, occ_storage_ );
        occ_storage_ = nullptr;
    }
    if( xfer_trans_f_ ) {
        SDL_ReleaseGPUTransferBuffer( d, xfer_trans_f_ );
        xfer_trans_f_ = nullptr;
    }
    if( trans_storage_ ) {
        SDL_ReleaseGPUBuffer( d, trans_storage_ );
        trans_storage_ = nullptr;
    }
    if( transparency_tex_ ) {
        SDL_ReleaseGPUTexture( d, transparency_tex_ );
        transparency_tex_ = nullptr;
    }
}

void sdf_pass::upload( SDL_GPUCopyPass *cp,
                        SDL_GPUDevice   *dev,
                        int runtime_w, int runtime_h,
                        const std::vector<uint8_t> &transparency,
                        const std::vector<float>   &sdf,
                        const std::vector<uint8_t> &sky_vis,
                        const std::vector<float>   &vis,
                        const std::vector<float>   &occ )
{
    if( !cp || !dev || !transparency_tex_ ) {
        return;
    }
    if( runtime_w <= 0 || runtime_h <= 0 ) {
        return;
    }
    // Refuse runtime sizes that exceed the texture allocation. Should never
    // happen if render_state::init sized for REALITY_BUBBLE_SIZE_MAX, but
    // clamp defensively rather than overrun the GPU texture.
    if( runtime_w > map_w_ || runtime_h > map_h_ ) {
        dbg( DL::Error ) << "sdf_pass::upload: runtime " << runtime_w << "x"
                         << runtime_h << " exceeds tex " << map_w_ << "x" << map_h_;
        return;
    }

    const Uint32 pixel_count = static_cast<Uint32>( runtime_w * runtime_h );
    // populated_ flips only when SDF data actually lands on the GPU
    // (see SDF block below). Main-menu / pre-world frames call upload()
    // with empty vectors → guards skip every channel → populated_ must
    // stay false so begin_lighting_frame keeps sdf_map_w/h=0 and the
    // shader skips its shadow march (which would read s=0 → shadow=0
    // → SDF debug view all red + sun killed).

    // All uploads write a runtime_w × runtime_h sub-rect at (0,0). The
    // texture is sized for REALITY_BUBBLE_SIZE_MAX so it can hold any
    // legal mapsize; the shader clamps with runtime_w/h (via map_w()/h()).
    // pixels_per_row = runtime_w so the source rows match the CPU x-major
    // packing sdf[x * runtime_h + y].

    // Upload transparency (R8, 1 byte/tile).
    if( xfer_transparency_ &&
        static_cast<Uint32>( transparency.size() ) >= pixel_count ) {
        void *mapped = SDL_MapGPUTransferBuffer( dev, xfer_transparency_, true );
        if( mapped ) {
            std::memcpy( mapped, transparency.data(), pixel_count );
            SDL_UnmapGPUTransferBuffer( dev, xfer_transparency_ );

            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = xfer_transparency_;
            src.pixels_per_row  = static_cast<Uint32>( runtime_w );

            SDL_GPUTextureRegion dst{};
            dst.texture = transparency_tex_;
            dst.w       = static_cast<Uint32>( runtime_w );
            dst.h       = static_cast<Uint32>( runtime_h );
            dst.d       = 1;

            // cycle=false: shader binds transparency_tex_ by handle.
            // cycle=true would orphan it after every upload (new texture
            // gets the bytes, sampler keeps the stale empty handle).
            SDL_UploadToGPUTexture( cp, &src, &dst, false );
        }
        // P3.3: populated_ flips when transparency lands — JFA writes SDF
        // directly to sdf_storage_, so we no longer need the CPU SDF upload
        // block below to set this flag. begin_lighting_frame() exposes
        // sdf_map_w/h from this frame onward.
        populated_  = true;
        runtime_w_  = runtime_w;
        runtime_h_  = runtime_h;
    }

    // P3.3: CPU SDF upload removed — JFA writes directly to sdf_storage_ on GPU.
    // The `sdf` parameter is retained in the function signature for backward compat
    // with emitter_collector::submit() but no longer consumed here.

    // Stage 2b: unified coverage occluder field — tile-res, 2 floats/tile
    // (occ[(x*runtime_h+y)*2 + 0] = occluder height, +1 = roof bit). Marched by
    // sky_sun.comp for sun/moon/sky occlusion (single occlusion source).
    {
        const Uint32 occ_floats = pixel_count * 2u;
        if( xfer_occ_ && occ_storage_
            && static_cast<Uint32>( occ.size() ) >= occ_floats ) {
            void *mapped = SDL_MapGPUTransferBuffer( dev, xfer_occ_, true );
            if( mapped ) {
                std::memcpy( mapped, occ.data(), occ_floats * sizeof( float ) );
                SDL_UnmapGPUTransferBuffer( dev, xfer_occ_ );

                SDL_GPUTransferBufferLocation tb_src{};
                tb_src.transfer_buffer = xfer_occ_;
                tb_src.offset          = 0;

                SDL_GPUBufferRegion buf_dst{};
                buf_dst.buffer = occ_storage_;
                buf_dst.offset = 0;
                buf_dst.size   = occ_floats * static_cast<Uint32>( sizeof( float ) );

                SDL_UploadToGPUBuffer( cp, &tb_src, &buf_dst, false );
            }
        }
    }

    // P3 JFA input: tile-res transparency as floats (0.0=opaque .. 1.0=open).
    // Convert uint8 bytes → float so the seed shader can read directly.
    if( trans_storage_ && xfer_trans_f_
        && static_cast<Uint32>( transparency.size() ) >= pixel_count ) {
        void *mapped = SDL_MapGPUTransferBuffer( dev, xfer_trans_f_, true );
        if( mapped ) {
            float *fdst = static_cast<float *>( mapped );
            for( Uint32 i = 0; i < pixel_count; ++i ) {
                fdst[i] = static_cast<float>( transparency[i] ) / 255.0f;
            }
            SDL_UnmapGPUTransferBuffer( dev, xfer_trans_f_ );

            SDL_GPUTransferBufferLocation tb_src{};
            tb_src.transfer_buffer = xfer_trans_f_;
            tb_src.offset          = 0;

            SDL_GPUBufferRegion buf_dst{};
            buf_dst.buffer = trans_storage_;
            buf_dst.offset = 0;
            buf_dst.size   = pixel_count * static_cast<Uint32>( sizeof( float ) );

            SDL_UploadToGPUBuffer( cp, &tb_src, &buf_dst, false );
        }
    }

    // P3.3: sky_vis_tex_ deleted — shader reads from skyvis_storage_ buffer, not a texture.
    // The xfer_sky_vis_ transfer buffer is retained for the float conversion path below.

    // Sky-vis as a fragment storage buffer of floats (1.0=open, 0.0=roofed).
    // The shader reads SkyVisBuf, not the R8 texture, because sampler-texture
    // Load returns 0 on Metal. Convert the uint8 bytes (0/255) → float here.
    if( skyvis_storage_ && xfer_skyvis_f_
        && static_cast<Uint32>( sky_vis.size() ) >= pixel_count ) {
        void *mapped = SDL_MapGPUTransferBuffer( dev, xfer_skyvis_f_, true );
        if( mapped ) {
            float *fdst = static_cast<float *>( mapped );
            for( Uint32 i = 0; i < pixel_count; ++i ) {
                fdst[i] = static_cast<float>( sky_vis[i] ) / 255.0f;
            }
            SDL_UnmapGPUTransferBuffer( dev, xfer_skyvis_f_ );

            SDL_GPUTransferBufferLocation tb_src{};
            tb_src.transfer_buffer = xfer_skyvis_f_;
            tb_src.offset          = 0;

            SDL_GPUBufferRegion buf_dst{};
            buf_dst.buffer = skyvis_storage_;
            buf_dst.offset = 0;
            buf_dst.size   = pixel_count * static_cast<Uint32>( sizeof( float ) );

            SDL_UploadToGPUBuffer( cp, &tb_src, &buf_dst, false );
        }
    }

    // Per-tile visibility on the SDF_SUPERSAMPLE grid (SS² floats/tile, x-major,
    // stride runtime_h*SS). Fragment storage buffer (VisBuf); shader applies the
    // soft vision falloff sampled as finely as the SDF shadows.
    if( visbuf_storage_ && xfer_vis_f_
        && static_cast<Uint32>( vis.size() ) >= sdf_subcells ) {
        void *mapped = SDL_MapGPUTransferBuffer( dev, xfer_vis_f_, true );
        if( mapped ) {
            std::memcpy( mapped, vis.data(),
                         sdf_subcells * static_cast<Uint32>( sizeof( float ) ) );
            SDL_UnmapGPUTransferBuffer( dev, xfer_vis_f_ );

            SDL_GPUTransferBufferLocation tb_src{};
            tb_src.transfer_buffer = xfer_vis_f_;
            tb_src.offset          = 0;

            SDL_GPUBufferRegion buf_dst{};
            buf_dst.buffer = visbuf_storage_;
            buf_dst.offset = 0;
            buf_dst.size   = sdf_subcells * static_cast<Uint32>( sizeof( float ) );

            SDL_UploadToGPUBuffer( cp, &tb_src, &buf_dst, false );
        }
    }
}

} // namespace lighting
