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
    while( true ) {
        std::vector<gpu_emitter> work_emitters;
        std::vector<uint8_t>    work_transparency;
        std::vector<float>      work_sdf;
        std::vector<uint8_t>    work_sky_vis;
        {
            std::unique_lock<std::mutex> lk( mu_ );
            cv_.wait( lk, [this] { return have_pending_ || stop_; } );
            if( stop_ ) {
                break;
            }
            work_emitters     = std::move( pending_ );
            work_transparency = std::move( pending_transparency_ );
            work_sdf          = std::move( pending_sdf_ );
            work_sky_vis      = std::move( pending_sky_vis_ );
            have_pending_ = false;
        }
        upload_to_gpu( work_emitters, work_transparency, work_sdf, work_sky_vis );
    }
}

void emitter_collector::upload_to_gpu( const std::vector<gpu_emitter> &data,
                                         const std::vector<uint8_t>    &transparency,
                                         const std::vector<float>      &sdf,
                                         const std::vector<uint8_t>    &sky_vis )
{
    if( !rs_.device().raw() ) {
        return;
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
    SDL_UnmapGPUTransferBuffer( rs_.device().raw(), xfer_[write_slot_] );

    // Acquire command buffer, do copy pass.
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer( rs_.device().raw() );
    if( !cb ) {
        dbg( DL::Error ) << "emitter_collector: SDL_AcquireGPUCommandBuffer failed";
        return;
    }

    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
    if( !cp ) {
        SDL_CancelGPUCommandBuffer( cb );
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

        // cycle=false: shader binds emitter_tex_[read_slot_] by pointer.
        // cycle=true would orphan the original GPU handle each upload, so the
        // sampler would read the stale (empty) texture while our writes land
        // in an invisible new one. Ring-slot swap below provides the
        // frames-in-flight safety this would otherwise be needed for.
        SDL_UploadToGPUTexture( cp, &tex_src, &tex_dst, /*cycle=*/false );
    }

    // Phase 4/8: upload transparency + SDF + sky_vis textures in the same copy pass.
    if( rs_.sdf().ready() && !transparency.empty() && !sdf.empty() ) {
        rs_.sdf().upload( cp, rs_.device().raw(), transparency, sdf, sky_vis );
    }

    SDL_EndGPUCopyPass( cp );
    SDL_SubmitGPUCommandBuffer( cb );

    // Swap slots atomically.  After this the new data is visible via read_buffer().
    last_count_.store( count, std::memory_order_relaxed );
    read_slot_.store( write_slot_, std::memory_order_release );
    write_slot_ = 1 - write_slot_;
}

} // namespace lighting
