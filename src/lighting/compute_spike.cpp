#include "compute_spike.h"

#include <cstdint>
#include <cstring>

#include <SDL3/SDL_gpu.h>

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"

#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace lighting
{

namespace
{

// Minimal compute shader: dynamic [loop] (count from a uniform) over a readonly
// StructuredBuffer<float>, one scalar SB read per iter, one RWStructuredBuffer
// write. The same shape the fragment stage chokes on for D3D12 DXIL. SDL_GPU
// compute HLSL register spaces: readonly storage = (tN, space0), read-write
// storage = (uN, space1), uniforms = (bN, space2).
constexpr const char *SPIKE_COMP_HLSL = R"hlsl(
StructuredBuffer<float>   InBuf  : register(t0, space0);
RWStructuredBuffer<float> OutBuf : register(u0, space1);

cbuffer SpikeParams : register(b0, space2) {
    uint count;
    uint pad0;
    uint pad1;
    uint pad2;
};

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    float acc = 0.0;
    [loop] for (uint i = 0u; i < count; ++i) {
        acc += InBuf[i];   // dynamic-index scalar SB read inside a dynamic loop
    }
    if (tid.x == 0u) {
        OutBuf[0] = acc;
    }
}
)hlsl";

constexpr std::uint32_t SPIKE_N = 256u; // input element count / loop iterations

} // namespace

bool run_compute_spike( gpu_device &dev )
{
    if( !dev.ready() ) {
        dbg( DL::Error ) << "compute_spike: gpu_device not ready";
        return false;
    }
    SDL_GPUDevice *d = dev.raw();

    init_shader_compiler();
    auto cp = compile_compute_pipeline( dev, SPIKE_COMP_HLSL, "main", "compute_spike" );

    // The headline go/no-go line. Logged whether or not creation succeeded so a
    // single grep of debug.log answers the gate.
    DebugLogFL( DL::Info, DC::Main )
            << "[A0][compute-spike] pipeline create "
            << ( cp ? "OK" : "FAILED" )
            << " — reflect: ro_sb=" << cp.resources.num_readonly_storage_buffers
            << " rw_sb=" << cp.resources.num_readwrite_storage_buffers
            << " uniforms=" << cp.resources.num_uniform_buffers
            << " threads=(" << cp.resources.threadcount_x << ","
            << cp.resources.threadcount_y << "," << cp.resources.threadcount_z << ")";

    if( !cp ) {
        DebugLogFL( DL::Error, DC::Main )
                << "[A0][compute-spike] NO-GO: compute pipeline creation failed: "
                << SDL_GetError() << " — fall back to CPU-GI contingency.";
        return false;
    }

    bool ok = false;

    // ── Input storage buffer: SPIKE_N floats, all 1.0 → expected sum == N. ──
    const std::uint32_t in_bytes = SPIKE_N * static_cast<std::uint32_t>( sizeof( float ) );
    SDL_GPUBufferCreateInfo in_bci{};
    in_bci.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    in_bci.size  = in_bytes;
    SDL_GPUBuffer *in_buf = SDL_CreateGPUBuffer( d, &in_bci );

    // ── Output storage buffer: 1 float. ──
    SDL_GPUBufferCreateInfo out_bci{};
    out_bci.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    out_bci.size  = static_cast<std::uint32_t>( sizeof( float ) );
    SDL_GPUBuffer *out_buf = SDL_CreateGPUBuffer( d, &out_bci );

    if( !in_buf || !out_buf ) {
        DebugLogFL( DL::Error, DC::Main )
                << "[A0][compute-spike] buffer create failed: " << SDL_GetError()
                << " (pipeline created OK — the primary D3D12 gate passed; "
                "dispatch couldn't be set up, result unverified)";
        ok = false;
    } else {
        // Stage the input via an upload transfer buffer.
        SDL_GPUTransferBufferCreateInfo up_tbci{};
        up_tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        up_tbci.size  = in_bytes;
        SDL_GPUTransferBuffer *up_tb = SDL_CreateGPUTransferBuffer( d, &up_tbci );
        if( up_tb ) {
            void *map = SDL_MapGPUTransferBuffer( d, up_tb, false );
            if( map ) {
                float *f = static_cast<float *>( map );
                for( std::uint32_t i = 0; i < SPIKE_N; ++i ) {
                    f[i] = 1.0f;
                }
                SDL_UnmapGPUTransferBuffer( d, up_tb );
            }

            SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer( d );
            if( cb ) {
                // Upload input.
                SDL_GPUCopyPass *cp_up = SDL_BeginGPUCopyPass( cb );
                SDL_GPUTransferBufferLocation src{};
                src.transfer_buffer = up_tb;
                src.offset = 0;
                SDL_GPUBufferRegion dst{};
                dst.buffer = in_buf;
                dst.offset = 0;
                dst.size   = in_bytes;
                SDL_UploadToGPUBuffer( cp_up, &src, &dst, /*cycle=*/false );
                SDL_EndGPUCopyPass( cp_up );

                // Dispatch: count from a pushed uniform (dynamic loop bound).
                struct {
                    std::uint32_t count;
                    std::uint32_t pad0;
                    std::uint32_t pad1;
                    std::uint32_t pad2;
                } params{ SPIKE_N, 0u, 0u, 0u };
                SDL_PushGPUComputeUniformData( cb, /*slot=*/0, &params, sizeof( params ) );

                SDL_GPUStorageBufferReadWriteBinding rw{};
                rw.buffer = out_buf;
                rw.cycle  = false;
                SDL_GPUComputePass *pass =
                    SDL_BeginGPUComputePass( cb, nullptr, 0, &rw, 1 );
                if( pass ) {
                    SDL_BindGPUComputePipeline( pass, cp.pipeline );
                    SDL_BindGPUComputeStorageBuffers( pass, /*first_slot=*/0, &in_buf, 1 );
                    SDL_DispatchGPUCompute( pass, 1, 1, 1 );
                    SDL_EndGPUComputePass( pass );
                } else {
                    DebugLogFL( DL::Error, DC::Main )
                            << "[A0][compute-spike] BeginGPUComputePass failed: "
                            << SDL_GetError();
                }

                // Read back OutBuf[0].
                SDL_GPUTransferBufferCreateInfo dn_tbci{};
                dn_tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
                dn_tbci.size  = static_cast<std::uint32_t>( sizeof( float ) );
                SDL_GPUTransferBuffer *dn_tb = SDL_CreateGPUTransferBuffer( d, &dn_tbci );
                if( dn_tb ) {
                    SDL_GPUCopyPass *cp_dn = SDL_BeginGPUCopyPass( cb );
                    SDL_GPUBufferRegion rd{};
                    rd.buffer = out_buf;
                    rd.offset = 0;
                    rd.size   = static_cast<std::uint32_t>( sizeof( float ) );
                    SDL_GPUTransferBufferLocation rdst{};
                    rdst.transfer_buffer = dn_tb;
                    rdst.offset = 0;
                    SDL_DownloadFromGPUBuffer( cp_dn, &rd, &rdst );
                    SDL_EndGPUCopyPass( cp_dn );
                    SDL_SubmitGPUCommandBuffer( cb );
                    SDL_WaitForGPUIdle( d ); // synchronous — spike only, runs once

                    const float *res = static_cast<const float *>(
                                           SDL_MapGPUTransferBuffer( d, dn_tb, false ) );
                    if( res ) {
                        const float got = *res;
                        const float expect = static_cast<float>( SPIKE_N );
                        ok = ( got > expect - 0.5f && got < expect + 0.5f );
                        DebugLogFL( DL::Info, DC::Main )
                                << "[A0][compute-spike] dispatch result OutBuf[0]=" << got
                                << " expected=" << expect << " → "
                                << ( ok ? "GO (compute runs on this backend)"
                                     : "MISMATCH (created+ran but wrong value)" );
                        SDL_UnmapGPUTransferBuffer( d, dn_tb );
                    }
                    SDL_ReleaseGPUTransferBuffer( d, dn_tb );
                } else {
                    SDL_SubmitGPUCommandBuffer( cb );
                }
            }
            SDL_ReleaseGPUTransferBuffer( d, up_tb );
        }
    }

    if( in_buf ) {
        SDL_ReleaseGPUBuffer( d, in_buf );
    }
    if( out_buf ) {
        SDL_ReleaseGPUBuffer( d, out_buf );
    }
    SDL_ReleaseGPUComputePipeline( d, cp.pipeline );
    return ok;
}

} // namespace lighting
