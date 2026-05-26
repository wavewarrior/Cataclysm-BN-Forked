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

static constexpr Uint32 SSBO_SIZE = static_cast<Uint32>( MAX_EMITTERS ) *
                                    static_cast<Uint32>( sizeof( gpu_emitter ) );

emitter_collector::emitter_collector( render_state &rs ) : rs_( rs )
{
    // Allocate the double-buffered GPU storage buffers and transfer buffers.
    SDL_GPUDevice *dev = rs_.device().raw();

    for( int i = 0; i < RING; ++i ) {
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        bci.size  = SSBO_SIZE;
        ssbo_[i] = SDL_CreateGPUBuffer( dev, &bci );
        if( !ssbo_[i] ) {
            dbg( DL::Error ) << "emitter_collector: failed to create SSBO slot " << i;
        }

        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = SSBO_SIZE;
        xfer_[i] = SDL_CreateGPUTransferBuffer( dev, &tbci );
        if( !xfer_[i] ) {
            dbg( DL::Error ) << "emitter_collector: failed to create transfer buffer slot " << i;
        }

        // Phase 7: 4×64 RGBA32F emitter data texture for fragment-stage access.
        // Row = emitter index; width = 4 (4 float4 data slots per emitter).
        SDL_GPUTextureCreateInfo etci{};
        etci.type              = SDL_GPU_TEXTURETYPE_2D;
        etci.format            = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
        etci.usage             = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        etci.width             = 4u;
        etci.height            = 64u;
        etci.layer_count_or_depth = 1;
        etci.num_levels        = 1;
        emitter_tex_[i] = SDL_CreateGPUTexture( dev, &etci );
        if( !emitter_tex_[i] ) {
            dbg( DL::Warn ) << "emitter_collector: failed to create emitter texture slot " << i;
        }
    }

    // Diagnostic download transfer buffer: holds one RGBA32F pixel = 16 bytes
    // so we can read back what the GPU actually has in EmitterTex slot 0.
    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        tbci.size  = 16u;
        download_xfer_ = SDL_CreateGPUTransferBuffer( dev, &tbci );
    }

    thread_ = std::thread( &emitter_collector::thread_main, this );
}

emitter_collector::~emitter_collector()
{
    {
        std::lock_guard<std::mutex> lk( mu_ );
        stop_ = true;
    }
    cv_.notify_one();
    if( thread_.joinable() ) {
        thread_.join();
    }

    SDL_GPUDevice *dev = rs_.device().raw();
    for( int i = 0; i < RING; ++i ) {
        if( xfer_[i] ) {
            SDL_ReleaseGPUTransferBuffer( dev, xfer_[i] );
        }
        if( ssbo_[i] ) {
            SDL_ReleaseGPUBuffer( dev, ssbo_[i] );
        }
        if( emitter_tex_[i] ) {
            SDL_ReleaseGPUTexture( dev, emitter_tex_[i] );
        }
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
    {
        std::lock_guard<std::mutex> lk( mu_ );
        pending_              = std::move( snapshot );
        pending_transparency_ = std::move( transparency );
        pending_sdf_          = std::move( sdf );
        pending_sky_vis_      = std::move( sky_vis );
        have_pending_         = true;
    }
    cv_.notify_one();
}

SDL_GPUBuffer *emitter_collector::read_buffer() const noexcept
{
    return ssbo_[read_slot_.load( std::memory_order_acquire )];
}

SDL_GPUTexture *emitter_collector::emitter_texture() const noexcept
{
    return emitter_tex_[read_slot_.load( std::memory_order_acquire )];
}

void emitter_collector::thread_main()
{
    // Worker thread no longer performs the GPU upload — that path produced a
    // cross-CB race against the render command buffer (sampler read stale
    // texture data). All GPU work is now done from the main thread via
    // flush_to_render_cb() on the render command buffer. The thread is kept
    // alive so the ctor/dtor flow stays unchanged; it just sleeps on the cv.
    while( true ) {
        std::unique_lock<std::mutex> lk( mu_ );
        cv_.wait( lk, [this] { return stop_; } );
        if( stop_ ) {
            break;
        }
    }
}

void emitter_collector::flush_to_render_cb( SDL_GPUCommandBuffer *cb )
{
    if( !cb || !rs_.device().raw() ) {
        return;
    }

    // Diagnostic readback: drain the previous frame's download (if any). The
    // GPU has had >=1 frame to complete; on D3D12 begin_frame's
    // WaitAndAcquireGPUSwapchainTexture also stalls for in-flight frames so
    // the data is safe to map here.
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

    // Atomically take pending data — main thread is the only consumer now,
    // but the worker-thread submit() path still writes pending_* under the
    // mutex.
    std::vector<gpu_emitter> data;
    std::vector<uint8_t>     transparency;
    std::vector<float>       sdf;
    std::vector<uint8_t>     sky_vis;
    {
        std::lock_guard<std::mutex> lk( mu_ );
        if( !have_pending_ ) {
            return;
        }
        data         = std::move( pending_ );
        transparency = std::move( pending_transparency_ );
        sdf          = std::move( pending_sdf_ );
        sky_vis      = std::move( pending_sky_vis_ );
        have_pending_ = false;
    }

    if( !xfer_[write_slot_] || !ssbo_[write_slot_] ) {
        return;
    }

    const int count = std::min( static_cast<int>( data.size() ), MAX_EMITTERS );
    const Uint32 byte_size = static_cast<Uint32>( count ) *
                             static_cast<Uint32>( sizeof( gpu_emitter ) );

    // Map transfer buffer, copy data.
    void *mapped = SDL_MapGPUTransferBuffer( rs_.device().raw(),
                                              xfer_[write_slot_], /*cycle=*/true );
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
    // TEMPORARY DEBUG: overwrite emitter[0] with a known sentinel pattern so
    // the shader debug viz can prove the upload actually reaches the bound
    // texture. pos=(99, 99, 0), radius=5, full red. If magenta appears near
    // world_pos (99,99), the upload-bind-sample chain works end-to-end and
    // the bug lies upstream in snapshot construction. If still no magenta,
    // the upload itself is not reaching the sampler.
    // (sentinel removed — pipeline confirmed working in zoomed-out view)
    SDL_UnmapGPUTransferBuffer( rs_.device().raw(), xfer_[write_slot_] );

    // Issue the copy pass on the GIVEN render command buffer so the GPU
    // executes uploads before the subsequent render pass samples the
    // textures. No separate AcquireCommandBuffer / SubmitCommandBuffer here.
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
    if( !cp ) {
        return;
    }

    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = xfer_[write_slot_];
    src.offset          = 0;

    SDL_GPUBufferRegion dst{};
    dst.buffer = ssbo_[write_slot_];
    dst.offset = 0;
    dst.size   = byte_size;

    SDL_UploadToGPUBuffer( cp, &src, &dst, /*cycle=*/true );

    // Phase 7: upload same emitter data to 4×64 RGBA32F texture for fragment access.
    // Reuses the same transfer buffer (same bytes; gpu_emitter = 4×float4 = one texture row).
    if( emitter_tex_[write_slot_] && count > 0 ) {
        const Uint32 rows = count > 64 ? 64u : static_cast<Uint32>( count );
        SDL_GPUTextureTransferInfo tex_src{};
        tex_src.transfer_buffer = xfer_[write_slot_];
        tex_src.offset          = 0;
        // SDL3's "0 = tight packing" can be unreliable on D3D12 — pass the
        // values explicitly so the row stride matches the gpu_emitter
        // struct's 64-byte layout (4 RGBA32F pixels = 64 bytes = one row).
        tex_src.pixels_per_row  = 4u;
        tex_src.rows_per_layer  = rows;

        SDL_GPUTextureRegion tex_dst{};
        tex_dst.texture   = emitter_tex_[write_slot_];
        tex_dst.x         = 0;
        tex_dst.y         = 0;
        tex_dst.z         = 0;
        tex_dst.w         = 4;
        tex_dst.h         = rows;
        tex_dst.d         = 1;
        tex_dst.layer     = 0;
        tex_dst.mip_level = 0;

        // cycle=true: GPU readback proves the upload lands, but the shader
        // sampler in the SAME CB reads stale (empty) content. SDL_GPU
        // appears to skip the write→read transition between copy pass and
        // render pass when cycle=false on D3D12; cycle=true forces a fresh
        // internal resource and a proper barrier so the sampler sees the
        // new data.
        SDL_UploadToGPUTexture( cp, &tex_src, &tex_dst, /*cycle=*/true );

        // Diagnostic: download the FIRST pixel of the just-written texture
        // back into download_xfer_ so the CPU can verify what the GPU has.
        // Read happens on the NEXT call to flush_to_render_cb (~1 frame
        // later) when the GPU has finished this CB.
        if( download_xfer_ ) {
            SDL_GPUTextureRegion dl_src{};
            dl_src.texture   = emitter_tex_[write_slot_];
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

    // Phase 4/8: upload transparency + SDF + sky_vis textures in the same copy pass.
    if( rs_.sdf().ready() && !transparency.empty() && !sdf.empty() ) {
        rs_.sdf().upload( cp, rs_.device().raw(), transparency, sdf, sky_vis );
    }

    SDL_EndGPUCopyPass( cp );
    // No submit here — caller (refresh_display) submits the render CB after
    // the render pass, so uploads and draws share one CB and execute in order.

    // DIAGNOSTIC: bypass the slot-swap. Always pin to slot 0 so the shader
    // bind pointer (captured by set_tile_lighting) is the same physical
    // texture handle we just wrote to. Removes one source of ambiguity for
    // the "readback ok / shader sees zero" mystery.
    last_count_.store( count, std::memory_order_relaxed );
    read_slot_.store( 0, std::memory_order_release );
    write_slot_ = 0;
}

} // namespace lighting
