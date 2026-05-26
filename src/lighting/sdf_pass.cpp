#include "lighting/sdf_pass.h"

#include <algorithm>
#include <cstring>
#include <queue>
#include <utility>

#include "debug.h"
#include "lighting/gpu_device.h"
#include "sdl_wrappers.h"

#define dbg(x) DebugLogFL((x),DC::SDL)

namespace lighting
{

// ---------------------------------------------------------------------------
// CPU distance transform
// ---------------------------------------------------------------------------

std::vector<float> compute_sdf_cpu( const float *trans, int w, int h )
{
    const int total = w * h;
    std::vector<float> dist( total, 1e9f );

    struct cell {
        int x, y;
    };
    std::queue<cell> q;

    // Seed: every opaque tile (transparency == 0) starts at distance 0.
    for( int x = 0; x < w; ++x ) {
        for( int y = 0; y < h; ++y ) {
            if( trans[x * h + y] == 0.0f ) {
                dist[x * h + y] = 0.0f;
                q.push( { x, y } );
            }
        }
    }

    // Chebyshev BFS (8-connected, step cost = 1.0 for all neighbours).
    static constexpr int DX[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    static constexpr int DY[8] = { -1,  0,  1,-1, 1,-1, 0, 1 };

    while( !q.empty() ) {
        const auto [cx, cy] = q.front();
        q.pop();
        const float cd = dist[cx * h + cy];
        for( int i = 0; i < 8; ++i ) {
            const int nx = cx + DX[i];
            const int ny = cy + DY[i];
            if( nx < 0 || ny < 0 || nx >= w || ny >= h ) {
                continue;
            }
            const float nd = cd + 1.0f;
            if( nd < dist[nx * h + ny] ) {
                dist[nx * h + ny] = nd;
                q.push( { nx, ny } );
            }
        }
    }

    return dist;
}

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

    // SDF texture: R32 FLOAT, 4 bytes per tile.
    // Plan calls for R16F; using R32F in Phase 4 for simpler CPU→GPU copy
    // (no half-float conversion needed). Phase 6 JFA can switch to R16F.
    {
        SDL_GPUTextureCreateInfo tci{};
        tci.type                 = SDL_GPU_TEXTURETYPE_2D;
        tci.format               = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        tci.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER
                                   | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
        tci.width                = static_cast<Uint32>( map_w );
        tci.height               = static_cast<Uint32>( map_h );
        tci.layer_count_or_depth = 1;
        tci.num_levels           = 1;
        tci.sample_count         = SDL_GPU_SAMPLECOUNT_1;
        sdf_tex_ = SDL_CreateGPUTexture( d, &tci );
        if( !sdf_tex_ ) {
            dbg( DL::Error ) << "sdf_pass::init: failed to create sdf_tex";
        }
    }

    // Sky visibility texture: R8_UNORM, 1 byte per tile (255=open sky, 0=indoor).
    {
        SDL_GPUTextureCreateInfo tci{};
        tci.type                 = SDL_GPU_TEXTURETYPE_2D;
        tci.format               = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        tci.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        tci.width                = static_cast<Uint32>( map_w );
        tci.height               = static_cast<Uint32>( map_h );
        tci.layer_count_or_depth = 1;
        tci.num_levels           = 1;
        tci.sample_count         = SDL_GPU_SAMPLECOUNT_1;
        sky_vis_tex_ = SDL_CreateGPUTexture( d, &tci );
        if( !sky_vis_tex_ ) {
            dbg( DL::Warn ) << "sdf_pass::init: failed to create sky_vis_tex";
        }
    }

    // Transfer buffers: one for transparency (R8), one for SDF (R32F), one for sky_vis (R8).
    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = static_cast<Uint32>( map_w * map_h ); // 1 byte per tile
        xfer_transparency_ = SDL_CreateGPUTransferBuffer( d, &tbci );
    }
    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = static_cast<Uint32>( map_w * map_h * 4 ); // 4 bytes per tile
        xfer_sdf_ = SDL_CreateGPUTransferBuffer( d, &tbci );
    }
    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = static_cast<Uint32>( map_w * map_h ); // 1 byte per tile
        xfer_sky_vis_ = SDL_CreateGPUTransferBuffer( d, &tbci );
    }

    // Phase 6b: SDF as vertex-readable storage buffer (same data as sdf_tex).
    {
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        bci.size  = static_cast<Uint32>( map_w * map_h * 4 );
        sdf_storage_ = SDL_CreateGPUBuffer( d, &bci );
        if( !sdf_storage_ ) {
            dbg( DL::Error ) << "sdf_pass::init: failed to create sdf_storage";
        }
    }
}

void sdf_pass::shutdown( gpu_device &dev )
{
    SDL_GPUDevice *d = dev.raw();
    if( !d ) {
        return;
    }
    if( xfer_sdf_ ) {
        SDL_ReleaseGPUTransferBuffer( d, xfer_sdf_ );
        xfer_sdf_ = nullptr;
    }
    if( xfer_transparency_ ) {
        SDL_ReleaseGPUTransferBuffer( d, xfer_transparency_ );
        xfer_transparency_ = nullptr;
    }
    if( xfer_sky_vis_ ) {
        SDL_ReleaseGPUTransferBuffer( d, xfer_sky_vis_ );
        xfer_sky_vis_ = nullptr;
    }
    if( sdf_storage_ ) {
        SDL_ReleaseGPUBuffer( d, sdf_storage_ );
        sdf_storage_ = nullptr;
    }
    if( sdf_tex_ ) {
        SDL_ReleaseGPUTexture( d, sdf_tex_ );
        sdf_tex_ = nullptr;
    }
    if( transparency_tex_ ) {
        SDL_ReleaseGPUTexture( d, transparency_tex_ );
        transparency_tex_ = nullptr;
    }
    if( sky_vis_tex_ ) {
        SDL_ReleaseGPUTexture( d, sky_vis_tex_ );
        sky_vis_tex_ = nullptr;
    }
}

void sdf_pass::upload( SDL_GPUCopyPass *cp,
                        SDL_GPUDevice   *dev,
                        const std::vector<uint8_t> &transparency,
                        const std::vector<float>   &sdf,
                        const std::vector<uint8_t> &sky_vis )
{
    if( !cp || !dev || !transparency_tex_ || !sdf_tex_ ) {
        return;
    }

    const Uint32 pixel_count = static_cast<Uint32>( map_w_ * map_h_ );

    // Upload transparency (R8, 1 byte/tile).
    if( xfer_transparency_ &&
        static_cast<Uint32>( transparency.size() ) >= pixel_count ) {
        void *mapped = SDL_MapGPUTransferBuffer( dev, xfer_transparency_, true );
        if( mapped ) {
            std::memcpy( mapped, transparency.data(), pixel_count );
            SDL_UnmapGPUTransferBuffer( dev, xfer_transparency_ );

            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = xfer_transparency_;
            src.pixels_per_row  = static_cast<Uint32>( map_w_ );

            SDL_GPUTextureRegion dst{};
            dst.texture = transparency_tex_;
            dst.w       = static_cast<Uint32>( map_w_ );
            dst.h       = static_cast<Uint32>( map_h_ );
            dst.d       = 1;

            // cycle=false: shader binds transparency_tex_ by handle.
            SDL_UploadToGPUTexture( cp, &src, &dst, true );
        }
    }

    // Upload SDF (R32F, 4 bytes/tile).
    if( xfer_sdf_ &&
        static_cast<Uint32>( sdf.size() ) >= pixel_count ) {
        void *mapped = SDL_MapGPUTransferBuffer( dev, xfer_sdf_, true );
        if( mapped ) {
            std::memcpy( mapped, sdf.data(), pixel_count * sizeof( float ) );
            SDL_UnmapGPUTransferBuffer( dev, xfer_sdf_ );

            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = xfer_sdf_;
            src.pixels_per_row  = static_cast<Uint32>( map_w_ );

            SDL_GPUTextureRegion dst{};
            dst.texture = sdf_tex_;
            dst.w       = static_cast<Uint32>( map_w_ );
            dst.h       = static_cast<Uint32>( map_h_ );
            dst.d       = 1;

            // cycle=false: same rationale — shader keeps the original handle.
            SDL_UploadToGPUTexture( cp, &src, &dst, true );

            // Also upload SDF to the vertex-shader storage buffer.
            if( sdf_storage_ ) {
                SDL_GPUTransferBufferLocation tb_src{};
                tb_src.transfer_buffer = xfer_sdf_;
                tb_src.offset          = 0;

                SDL_GPUBufferRegion buf_dst{};
                buf_dst.buffer = sdf_storage_;
                buf_dst.offset = 0;
                buf_dst.size   = pixel_count * static_cast<Uint32>( sizeof( float ) );

                SDL_UploadToGPUBuffer( cp, &tb_src, &buf_dst, true );
            }
        }
    }

    // Phase 8: upload sky_vis (R8_UNORM, 1 byte/tile, 255=open sky).
    if( sky_vis_tex_ && xfer_sky_vis_
        && static_cast<int>( sky_vis.size() ) >= map_w_ * map_h_ ) {
        void *mapped = SDL_MapGPUTransferBuffer( dev, xfer_sky_vis_, true );
        if( mapped ) {
            std::memcpy( mapped, sky_vis.data(),
                         static_cast<size_t>( map_w_ * map_h_ ) );
            SDL_UnmapGPUTransferBuffer( dev, xfer_sky_vis_ );

            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = xfer_sky_vis_;
            src.offset          = 0;

            SDL_GPUTextureRegion dst{};
            dst.texture   = sky_vis_tex_;
            dst.x         = 0; dst.y = 0; dst.z = 0;
            dst.w         = static_cast<Uint32>( map_w_ );
            dst.h         = static_cast<Uint32>( map_h_ );
            dst.d         = 1;
            dst.layer     = 0;
            dst.mip_level = 0;

            // cycle=false: sky_vis_tex_ is one handle the shader binds by
            // pointer every frame. cycle=true would orphan it after each
            // upload (write goes to a new texture, sampler reads the stale
            // one) — observed as terrain-shaped sky_vis garbage in the
            // shader debug view.
            SDL_UploadToGPUTexture( cp, &src, &dst, true );
        }
    }
}

} // namespace lighting
