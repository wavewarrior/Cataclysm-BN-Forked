#include "lighting/sdf_pass.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "debug.h"
#include "lighting/gpu_device.h"
#include "profile.h"
#include "sdl_wrappers.h"

#define dbg(x) DebugLogFL((x),DC::SDL)

namespace lighting
{

// ---------------------------------------------------------------------------
// CPU distance transform
// ---------------------------------------------------------------------------

// Exact Euclidean distance transform (Felzenszwalb & Huttenlocher 2012):
// distance in tiles from each cell to the nearest opaque tile. Two O(n) 1D
// lower-envelope passes (down columns, then across rows) on the squared
// distance, sqrt at the end. Replaces the old Chebyshev BFS, whose chessboard
// metric gave square isolines → square/faceted shadow penumbrae; Euclidean
// gives round isolines → smooth round penumbrae (the sprite shader samples this
// SDF unchanged, so shadows improve for free). Layout is x-major (idx = x*h+y).
std::vector<float> compute_sdf_cpu( const float *trans, int w, int h )
{
    ZoneScopedN( "light_sdf_dt" );
    const int total = w * h;
    constexpr float INF = 1e20f;

    // Working buffers reused across calls (this runs only on the render thread,
    // every frame the SDF rebuilds) — avoids a multi-MB malloc+memset per call.
    // resize() is a no-op once the map size is stable; every element below is
    // overwritten, so no re-zeroing is needed.
    static std::vector<float> grid;
    grid.resize( total );
    for( int i = 0; i < total; ++i ) {
        grid[i] = ( trans[i] == 0.0f ) ? 0.0f : INF;
    }

    const int maxdim = std::max( w, h );
    static std::vector<float> f, d, z;
    static std::vector<int>   vtx;
    f.resize( maxdim );
    d.resize( maxdim );
    z.resize( maxdim + 1 );
    vtx.resize( maxdim );

    // 1D squared-distance transform of f[0..n) into d[0..n) (lower envelope of
    // upward parabolas rooted at each sample).
    auto dt1d = [&]( int n ) {
        int k = 0;
        vtx[0] = 0;
        z[0] = -INF;
        z[1] =  INF;
        for( int q = 1; q < n; ++q ) {
            float s = ( ( f[q] + static_cast<float>( q * q ) )
                        - ( f[vtx[k]] + static_cast<float>( vtx[k] * vtx[k] ) ) )
                      / static_cast<float>( 2 * q - 2 * vtx[k] );
            while( s <= z[k] ) {
                --k;
                s = ( ( f[q] + static_cast<float>( q * q ) )
                      - ( f[vtx[k]] + static_cast<float>( vtx[k] * vtx[k] ) ) )
                    / static_cast<float>( 2 * q - 2 * vtx[k] );
            }
            ++k;
            vtx[k]  = q;
            z[k]    = s;
            z[k + 1] = INF;
        }
        k = 0;
        for( int q = 0; q < n; ++q ) {
            while( z[k + 1] < static_cast<float>( q ) ) {
                ++k;
            }
            const int dq = q - vtx[k];
            d[q] = static_cast<float>( dq * dq ) + f[vtx[k]];
        }
    };

    // Pass 1: down each column (fixed x, vary y — contiguous in x-major).
    for( int x = 0; x < w; ++x ) {
        for( int y = 0; y < h; ++y ) {
            f[y] = grid[x * h + y];
        }
        dt1d( h );
        for( int y = 0; y < h; ++y ) {
            grid[x * h + y] = d[y];
        }
    }
    // Pass 2: across each row (fixed y, vary x).
    for( int y = 0; y < h; ++y ) {
        for( int x = 0; x < w; ++x ) {
            f[x] = grid[x * h + y];
        }
        dt1d( w );
        for( int x = 0; x < w; ++x ) {
            grid[x * h + y] = d[x];
        }
    }

    std::vector<float> dist( total );
    for( int i = 0; i < total; ++i ) {
        dist[i] = std::sqrt( grid[i] );
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
        // sdf_tex_ is DEAD (nothing samples it; SDF reads from sdf_storage_ on
        // Metal). Kept tile-sized only because the upload()/shutdown() guards and
        // the (removable) Phase-6 JFA note reference it. The live SS-finer data
        // goes to xfer_sdf_/sdf_storage_ below.
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
        // 4 bytes per SDF subcell (SDF_SUPERSAMPLE² subcells per tile).
        tbci.size  = static_cast<Uint32>( map_w * map_h * SDF_SUPERSAMPLE * SDF_SUPERSAMPLE * 4 );
        xfer_sdf_ = SDL_CreateGPUTransferBuffer( d, &tbci );
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
        // sphere-march the same SDF on the GPU compute GI path).
        bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
                    SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
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
                        int runtime_w, int runtime_h,
                        const std::vector<uint8_t> &transparency,
                        const std::vector<float>   &sdf,
                        const std::vector<uint8_t> &sky_vis,
                        const std::vector<float>   &vis,
                        const std::vector<float>   &occ )
{
    if( !cp || !dev || !transparency_tex_ || !sdf_tex_ ) {
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
    }

    // Upload SDF (R32F). The SDF channel is SDF_SUPERSAMPLE× finer per axis, so
    // it carries SS² subcells per tile — sized/indexed independently of the
    // tile-res transparency/skyvis/vis channels above. The `sdf` vector is
    // x-major over the SS grid: sdf[(x*SS+sx)*(runtime_h*SS) + (y*SS+sy)].
    const int    ss           = SDF_SUPERSAMPLE;
    const Uint32 sdf_subcells  = pixel_count * static_cast<Uint32>( ss * ss );
    if( xfer_sdf_ &&
        static_cast<Uint32>( sdf.size() ) >= sdf_subcells ) {
        void *mapped = SDL_MapGPUTransferBuffer( dev, xfer_sdf_, true );
        if( mapped ) {
            std::memcpy( mapped, sdf.data(), sdf_subcells * sizeof( float ) );
            SDL_UnmapGPUTransferBuffer( dev, xfer_sdf_ );

            // sdf_tex_ (R32F texture) is DEAD — nothing samples it (see CLAUDE.md;
            // SDF is read from sdf_storage_ on Metal). Skip its upload entirely so
            // we don't pay an SS²-sized texture copy per rebuild for unread data.

            // Real SDF bytes landed on the GPU: now safe for the fragment
            // shader to read. begin_lighting_frame() exposes sdf_map_w/h
            // from this frame onward.
            populated_  = true;
            runtime_w_  = runtime_w;
            runtime_h_  = runtime_h;

            // Upload SDF to the fragment-shader storage buffer (the live
            // consumer). Full SS² subcell payload, x-major (stride map_h*SS).
            if( sdf_storage_ ) {
                SDL_GPUTransferBufferLocation tb_src{};
                tb_src.transfer_buffer = xfer_sdf_;
                tb_src.offset          = 0;

                SDL_GPUBufferRegion buf_dst{};
                buf_dst.buffer = sdf_storage_;
                buf_dst.offset = 0;
                buf_dst.size   = sdf_subcells * static_cast<Uint32>( sizeof( float ) );

                SDL_UploadToGPUBuffer( cp, &tb_src, &buf_dst, false );
            }
        }
    }

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

    // Phase 8: upload sky_vis (R8_UNORM, 1 byte/tile, 255=open sky).
    if( sky_vis_tex_ && xfer_sky_vis_
        && static_cast<Uint32>( sky_vis.size() ) >= pixel_count ) {
        void *mapped = SDL_MapGPUTransferBuffer( dev, xfer_sky_vis_, true );
        if( mapped ) {
            std::memcpy( mapped, sky_vis.data(), pixel_count );
            SDL_UnmapGPUTransferBuffer( dev, xfer_sky_vis_ );

            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = xfer_sky_vis_;
            src.offset          = 0;
            src.pixels_per_row  = static_cast<Uint32>( runtime_w );

            SDL_GPUTextureRegion dst{};
            dst.texture   = sky_vis_tex_;
            dst.x         = 0; dst.y = 0; dst.z = 0;
            dst.w         = static_cast<Uint32>( runtime_w );
            dst.h         = static_cast<Uint32>( runtime_h );
            dst.d         = 1;
            dst.layer     = 0;
            dst.mip_level = 0;

            // cycle=false: sky_vis_tex_ is one handle the shader binds by
            // pointer every frame. cycle=true would orphan it after each
            // upload (write goes to a new texture, sampler reads the stale
            // one) — observed as terrain-shaped sky_vis garbage in the
            // shader debug view.
            SDL_UploadToGPUTexture( cp, &src, &dst, false );
        }
    }

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
