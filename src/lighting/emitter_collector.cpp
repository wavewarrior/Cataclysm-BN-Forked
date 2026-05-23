#include "lighting/emitter_collector.h"

#include <cstring>

#include "debug.h"
#include "lighting/render_state.h"
#include "lighting/gpu_device.h"
#include "lighting/sdf_pass.h"
#include "sdl_wrappers.h" // SDL3 headers

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
            DebugLog( DL::Error ) << "emitter_collector: failed to create SSBO slot " << i;
        }

        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = SSBO_SIZE;
        xfer_[i] = SDL_CreateGPUTransferBuffer( dev, &tbci );
        if( !xfer_[i] ) {
            DebugLog( DL::Error ) << "emitter_collector: failed to create transfer buffer slot " << i;
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
    }
}

void emitter_collector::submit( std::vector<gpu_emitter> snapshot,
                                 std::vector<uint8_t>    transparency,
                                 std::vector<float>      sdf )
{
    {
        std::lock_guard<std::mutex> lk( mu_ );
        pending_              = std::move( snapshot );
        pending_transparency_ = std::move( transparency );
        pending_sdf_          = std::move( sdf );
        have_pending_         = true;
    }
    cv_.notify_one();
}

SDL_GPUBuffer *emitter_collector::read_buffer() const noexcept
{
    return ssbo_[read_slot_.load( std::memory_order_acquire )];
}

void emitter_collector::thread_main()
{
    while( true ) {
        std::vector<gpu_emitter> work_emitters;
        std::vector<uint8_t>    work_transparency;
        std::vector<float>      work_sdf;
        {
            std::unique_lock<std::mutex> lk( mu_ );
            cv_.wait( lk, [this] { return have_pending_ || stop_; } );
            if( stop_ ) {
                break;
            }
            work_emitters     = std::move( pending_ );
            work_transparency = std::move( pending_transparency_ );
            work_sdf          = std::move( pending_sdf_ );
            have_pending_ = false;
        }
        upload_to_gpu( work_emitters, work_transparency, work_sdf );
    }
}

void emitter_collector::upload_to_gpu( const std::vector<gpu_emitter> &data,
                                         const std::vector<uint8_t>    &transparency,
                                         const std::vector<float>      &sdf )
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
        DebugLog( DL::Error ) << "emitter_collector: SDL_MapGPUTransferBuffer failed";
        return;
    }
    std::memcpy( mapped, data.data(), byte_size );
    SDL_UnmapGPUTransferBuffer( rs_.device().raw(), xfer_[write_slot_] );

    // Acquire command buffer, do copy pass.
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer( rs_.device().raw() );
    if( !cb ) {
        DebugLog( DL::Error ) << "emitter_collector: SDL_AcquireGPUCommandBuffer failed";
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

    // Phase 4: upload transparency + SDF textures in the same copy pass.
    if( rs_.sdf().ready() && !transparency.empty() && !sdf.empty() ) {
        rs_.sdf().upload( cp, rs_.device().raw(), transparency, sdf );
    }

    SDL_EndGPUCopyPass( cp );
    SDL_SubmitGPUCommandBuffer( cb );

    // Swap slots atomically.  After this the new data is visible via read_buffer().
    last_count_.store( count, std::memory_order_relaxed );
    read_slot_.store( write_slot_, std::memory_order_release );
    write_slot_ = 1 - write_slot_;
}

} // namespace lighting
