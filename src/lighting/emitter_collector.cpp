#include "lighting/emitter_collector.h"

#include <cstring>

#include "debug.h"
#include "lighting/render_state.h"
#include "lighting/gpu_device.h"
#include "lighting/sdf_pass.h"
#include "sdl_wrappers.h"

#define dbg(x) DebugLogFL((x),DC::SDL) // SDL3 headers

// SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ — shader reads the SSBO.
// SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD        — CPU → GPU staging path.

namespace lighting
{

// Q9 (Path B): single texture + single staging buffer. SDL_GPU's
// cycle=true on map/upload handles frame-to-frame data dependencies
// internally — the previous 2-slot manual ring is gone.
//
// Q7 (earlier): the 512 KB SSBO ring was deleted. Transfer buffer is now
// sized to exactly one texture upload: 256 rows × sizeof(gpu_emitter) =
// 16 KB.
static constexpr int    EMITTER_TEX_ROWS  = 256;
static constexpr Uint32 EMITTER_TEX_BYTES = static_cast<Uint32>( EMITTER_TEX_ROWS ) *
                                            static_cast<Uint32>( sizeof( gpu_emitter ) );

emitter_collector::emitter_collector( render_state &rs ) : rs_( rs )
{
    SDL_GPUDevice *dev = rs_.device().raw();

    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = EMITTER_TEX_BYTES;
        xfer_ = SDL_CreateGPUTransferBuffer( dev, &tbci );
        if( !xfer_ ) {
            dbg( DL::Error ) << "emitter_collector: failed to create transfer buffer";
        }
    }

    {
        // Phase 7 / Q4 fix: 4×256 RGBA32F emitter data texture for
        // fragment-stage access. Row = emitter index; width = 4 (4 float4
        // data slots per emitter).
        SDL_GPUTextureCreateInfo etci{};
        etci.type              = SDL_GPU_TEXTURETYPE_2D;
        etci.format            = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
        etci.usage             = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        etci.width             = 4u;
        etci.height            = 256u;
        etci.layer_count_or_depth = 1;
        etci.num_levels        = 1;
        emitter_tex_ = SDL_CreateGPUTexture( dev, &etci );
        if( !emitter_tex_ ) {
            dbg( DL::Warn ) << "emitter_collector: failed to create emitter texture";
        }
    }

    // Diagnostic download transfer buffer: one RGBA32F pixel = 16 bytes
    // so we can read back what the GPU actually has in EmitterTex pixel 0.
    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        tbci.size  = 16u;
        download_xfer_ = SDL_CreateGPUTransferBuffer( dev, &tbci );
    }
}

emitter_collector::~emitter_collector()
{
    SDL_GPUDevice *dev = rs_.device().raw();
    if( xfer_ ) {
        SDL_ReleaseGPUTransferBuffer( dev, xfer_ );
        xfer_ = nullptr;
    }
    if( emitter_tex_ ) {
        SDL_ReleaseGPUTexture( dev, emitter_tex_ );
        emitter_tex_ = nullptr;
    }
    if( download_xfer_ ) {
        SDL_ReleaseGPUTransferBuffer( dev, download_xfer_ );
        download_xfer_ = nullptr;
    }
}

void emitter_collector::submit( std::vector<gpu_emitter> snapshot,
                                 std::vector<uint8_t>    transparency,
                                 std::vector<float>      sdf,
                                 std::vector<uint8_t>    sky_vis )
{
    // Single-threaded post-Q8: no mutex, no cv. Refresh_display calls
    // submit() then flush_to_render_cb() in sequence on the main thread.
    pending_              = std::move( snapshot );
    pending_transparency_ = std::move( transparency );
    pending_sdf_          = std::move( sdf );
    pending_sky_vis_      = std::move( sky_vis );
    have_pending_         = true;
}

void emitter_collector::flush_to_render_cb( SDL_GPUCommandBuffer *cb )
{
    // Record the copy pass ON THE CALLER'S COMMAND BUFFER. Same-CB
    // ordering means SDL_GPU emits the write→sample barrier between this
    // copy pass and the upcoming render pass automatically; no fence
    // needed. With cycle=true the underlying physical resource is
    // orphaned and replaced per upload, so the GPU may still be sampling
    // last frame's data via the previous physical resource — SDL_GPU
    // keeps that alive until its consumer retires.
    if( !cb || !rs_.device().raw() ) {
        return;
    }

    // Diagnostic readback: drain the previous frame's download (if any).
    if( download_pending_ && download_xfer_ ) {
        void *mapped = SDL_MapGPUTransferBuffer( rs_.device().raw(),
                                                  download_xfer_, false );
        if( mapped ) {
            const float *p = static_cast<const float *>( mapped );
            debug_d0_x_.store( p[0], std::memory_order_relaxed );
            debug_d0_y_.store( p[1], std::memory_order_relaxed );
            debug_d0_z_.store( p[2], std::memory_order_relaxed );
            debug_d0_w_.store( p[3], std::memory_order_relaxed );
            SDL_UnmapGPUTransferBuffer( rs_.device().raw(), download_xfer_ );
        }
        download_pending_ = false;
    }

    if( !have_pending_ ) {
        return;
    }
    std::vector<gpu_emitter> data         = std::move( pending_ );
    std::vector<uint8_t>     transparency = std::move( pending_transparency_ );
    std::vector<float>       sdf          = std::move( pending_sdf_ );
    std::vector<uint8_t>     sky_vis      = std::move( pending_sky_vis_ );
    have_pending_ = false;

    if( !xfer_ || !emitter_tex_ ) {
        return;
    }

    // Cap to texture row count (the only consumer). MAX_EMITTERS at the
    // snapshot/CPU side is an upper safety bound; the GPU never sees
    // more than EMITTER_TEX_ROWS regardless.
    const int count = std::min( static_cast<int>( data.size() ), EMITTER_TEX_ROWS );
    const Uint32 byte_size = static_cast<Uint32>( count ) *
                             static_cast<Uint32>( sizeof( gpu_emitter ) );

    // Map staging with cycle=true — SDL_GPU rotates its internal
    // physical staging if last frame's upload is still in flight.
    void *mapped = SDL_MapGPUTransferBuffer( rs_.device().raw(),
                                              xfer_, /*cycle=*/true );
    if( !mapped ) {
        dbg( DL::Error ) << "emitter_collector: SDL_MapGPUTransferBuffer failed";
        return;
    }
    if( !data.empty() ) {
        const gpu_emitter &e0 = data[0];
        dbg( DL::Debug ) << "emitter[0]: pos=(" << e0.pos_x << "," << e0.pos_y
                         << "," << e0.pos_z << ") r=" << e0.radius
                         << " rgb=(" << e0.r << "," << e0.g << "," << e0.b << ")";
    }
    std::memcpy( mapped, data.data(), byte_size );
    SDL_UnmapGPUTransferBuffer( rs_.device().raw(), xfer_ );

    // Copy pass on the CALLER'S command buffer.
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
    if( !cp ) {
        return;
    }

    // Phase 7: upload emitter data to 4×256 RGBA32F texture.
    if( count > 0 ) {
        const Uint32 rows = static_cast<Uint32>( count );
        SDL_GPUTextureTransferInfo tex_src{};
        tex_src.transfer_buffer = xfer_;
        tex_src.offset          = 0;
        // SDL3 "0 = tight packing" can be unreliable on D3D12 — pass the
        // values explicitly so the row stride matches gpu_emitter's
        // 64-byte layout (4 RGBA32F pixels = 64 bytes = one row).
        tex_src.pixels_per_row  = 4u;
        tex_src.rows_per_layer  = rows;

        SDL_GPUTextureRegion tex_dst{};
        tex_dst.texture   = emitter_tex_;
        tex_dst.x         = 0;
        tex_dst.y         = 0;
        tex_dst.z         = 0;
        tex_dst.w         = 4;
        tex_dst.h         = rows;
        tex_dst.d         = 1;
        tex_dst.layer     = 0;
        tex_dst.mip_level = 0;

        // cycle=true: SDL_GPU orphans the previous physical texture if
        // the GPU is still using it, allocates a new one, and updates the
        // SRV bound this frame to reference the new physical resource —
        // all within the same CB so the upcoming render pass samples the
        // freshly written data. The Phase 7 "stale sampler" symptom that
        // motivated cycle=false was a cross-CB artefact (resolved by Q2's
        // single-CB rewrite); within one CB this is the canonical
        // SDL_GPU pattern.
        SDL_UploadToGPUTexture( cp, &tex_src, &tex_dst, /*cycle=*/true );

        // Diagnostic: download the FIRST pixel back into download_xfer_.
        // Read happens on the next flush_to_render_cb (~1 frame later).
        if( download_xfer_ ) {
            SDL_GPUTextureRegion dl_src{};
            dl_src.texture   = emitter_tex_;
            dl_src.x         = 0; dl_src.y = 0; dl_src.z = 0;
            dl_src.w         = 1; dl_src.h = 1; dl_src.d = 1;
            dl_src.layer     = 0;
            dl_src.mip_level = 0;

            SDL_GPUTextureTransferInfo dl_dst{};
            dl_dst.transfer_buffer = download_xfer_;
            dl_dst.offset          = 0;
            dl_dst.pixels_per_row  = 1u;
            dl_dst.rows_per_layer  = 1u;

            SDL_DownloadFromGPUTexture( cp, &dl_src, &dl_dst );
            download_pending_ = true;
        }
    }

    // Phase 4/8: upload transparency + SDF + sky_vis in the same copy pass.
    if( rs_.sdf().ready() && !transparency.empty() && !sdf.empty() ) {
        rs_.sdf().upload( cp, rs_.device().raw(), transparency, sdf, sky_vis );
    }

    SDL_EndGPUCopyPass( cp );
    // No submit — caller submits `cb` after the render pass.

    last_count_.store( count, std::memory_order_relaxed );
}

} // namespace lighting
