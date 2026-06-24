#include "gpu_sdf_pass.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/sdf_pass.h"
#include "lighting/shader_compiler.h"

#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace lighting
{

// Subcells per tile side. Single-sourced from sdf_pass.h's SDF_SUPERSAMPLE so the
// GPU buffer sizing here can never drift from the CPU upload sizing. Must also
// match the shader-side SDF_SS in jfa_shared.hlsl.
static constexpr std::uint32_t SDF_SS = static_cast<std::uint32_t>( SDF_SUPERSAMPLE );

// Max flood step size in SS units (covers ~4 tiles — the resolve clamp radius).
static constexpr float SDF_FLOOD_STEP = 16.0f;

gpu_sdf_pass::~gpu_sdf_pass()
{
    shutdown();
}

bool gpu_sdf_pass::init( gpu_device &dev, std::uint32_t max_w, std::uint32_t max_h )
{
    shutdown();
    dev_ = &dev;
    if( !dev.ready() ) {
        dbg( DL::Error ) << "gpu_sdf_pass::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();

    const std::uint32_t max_sw = max_w * SDF_SS;
    const std::uint32_t max_sh = max_h * SDF_SS;

    // Compile all three compute pipelines. `name` is the bare "<x>.comp" label;
    // the source file adds the ".hlsl" extension (matches gi_compute_pass /
    // sky_sun_pass). Loading without ".hlsl" reads an empty file → "missing entry
    // point" → the pipeline silently fails to create.
    auto compile_one = [&]( const char *name ) -> SDL_GPUComputePipeline * {
        const std::string src = load_lighting_shader_source( std::string( name ) + ".hlsl" );
        auto pp = compile_compute_pipeline( dev, src, "main", name );
        if( !pp ) {
            DebugLogFL( DL::Error, DC::Main ) << "gpu_sdf_pass: " << name << " pipeline failed";
            return nullptr;
        }
        return pp.pipeline;
    };

    seed_pipeline_   = compile_one( "jfa_seed.comp" );
    flood_pipeline_  = compile_one( "jfa_flood.comp" );
    resolve_pipeline_ = compile_one( "jfa_resolve.comp" );

    // Fail loudly if any pipeline didn't compile — otherwise ready() is false and
    // the JFA pass is silently disabled while init() reports success (which is
    // exactly how the missing-".hlsl" / missing-include breakage stayed hidden).
    if( !seed_pipeline_ || !flood_pipeline_ || !resolve_pipeline_ ) {
        dbg( DL::Error ) << "gpu_sdf_pass::init: one or more JFA pipelines failed to compile";
        return false;
    }

    // Allocate ping-pong seed buffers (2 floats/subcell).
    const std::uint32_t seed_floats = max_sw * max_sh * 2u;
    seed_a_ = create_buffer( seed_floats, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
                                          SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE );
    seed_b_ = create_buffer( seed_floats, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
                                          SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE );

    if( !seed_a_ || !seed_b_ ) {
        dbg( DL::Error ) << "gpu_sdf_pass: buffer allocation failed";
        return false;
    }

    max_sw_ = max_sw;
    max_sh_ = max_sh;

    DebugLogFL( DL::Info, DC::Main )
            << "gpu_sdf_pass init OK: SS=" << max_sw << "x" << max_sh
            << " seed_floats=" << seed_floats;

    return true;
}

SDL_GPUBuffer *gpu_sdf_pass::create_buffer( std::uint32_t floats, SDL_GPUBufferUsageFlags usage )
{
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = usage;
    bci.size  = floats * static_cast<std::uint32_t>( sizeof( float ) );
    SDL_GPUBuffer *b = SDL_CreateGPUBuffer( dev_->raw(), &bci );
    if( !b ) {
        dbg( DL::Error ) << "gpu_sdf_pass: buffer create failed: " << SDL_GetError();
    }
    return b;
}

void gpu_sdf_pass::shutdown() noexcept
{
    if( dev_ && dev_->ready() ) {
        if( seed_pipeline_   ) SDL_ReleaseGPUComputePipeline( dev_->raw(), seed_pipeline_ );
        if( flood_pipeline_  ) SDL_ReleaseGPUComputePipeline( dev_->raw(), flood_pipeline_ );
        if( resolve_pipeline_) SDL_ReleaseGPUComputePipeline( dev_->raw(), resolve_pipeline_ );
        if( seed_a_   ) SDL_ReleaseGPUBuffer( dev_->raw(), seed_a_ );
        if( seed_b_   ) SDL_ReleaseGPUBuffer( dev_->raw(), seed_b_ );
    }
    seed_pipeline_   = nullptr;
    flood_pipeline_  = nullptr;
    resolve_pipeline_ = nullptr;
    seed_a_          = nullptr;
    seed_b_          = nullptr;
    max_sw_          = 0;
    max_sh_          = 0;
}

void gpu_sdf_pass::record( SDL_GPUCommandBuffer *cb, SDL_GPUBuffer *trans_buf,
                           SDL_GPUBuffer *target_sdf, std::uint32_t runtime_w,
                           std::uint32_t runtime_h )
{
    if( !ready() || !cb || !trans_buf || !target_sdf || runtime_w == 0 || runtime_h == 0 ) {
        return;
    }

    const std::uint32_t ss_w = runtime_w * SDF_SS;
    const std::uint32_t ss_h = runtime_h * SDF_SS;

    // Grid dimensions for dispatch (numthreads(8,8,1)).
    const std::uint32_t gx = ( ss_w + 7u ) / 8u;
    const std::uint32_t gy = ( ss_h + 7u ) / 8u;

    // --- SEED pass ---
    {
        jfa_params params{};
        params.map_w = runtime_w;
        params.map_h = runtime_h;
        params.step  = 0.0f;

        SDL_PushGPUComputeUniformData( cb, /*slot=*/0, &params, sizeof( params ) );
        SDL_GPUStorageBufferReadWriteBinding rw{};
        rw.buffer = seed_a_;
        rw.cycle  = false;
        SDL_GPUComputePass *p = SDL_BeginGPUComputePass( cb, nullptr, 0, &rw, 1 );
        if( !p ) { return; }

        SDL_BindGPUComputePipeline( p, seed_pipeline_ );

        SDL_GPUBuffer *ro[1] = { trans_buf }; // t0 (tile-res transparency)
        SDL_BindGPUComputeStorageBuffers( p, /*first_slot=*/0, ro, 1 );

        SDL_DispatchGPUCompute( p, gx, gy, 1 );
        SDL_EndGPUComputePass( p );
    }

    // --- FLOOD pass (ping-pong) ---
    SDL_GPUBuffer *flood_result = seed_a_;  // tracks which buffer holds final seed data
    {
        jfa_params params{};
        params.map_w = runtime_w;
        params.map_h = runtime_h;

        // Step schedule: start at the FULL SDF_FLOOD_STEP reach and halve down to
        // 1 (e.g. 16,8,4,2,1). passes = log2(step)+1 — the +1 is what makes the
        // first jump equal the advertised flood radius; without it the largest
        // step was only SDF_FLOOD_STEP/2, truncating shadow reach by half.
        const int flood_passes = static_cast<int>(
            std::log2( SDF_FLOOD_STEP ) ) + 1;

        SDL_GPUBuffer *seed_read  = seed_a_;
        SDL_GPUBuffer *seed_write = seed_b_;

        for( int pass = 0; pass < flood_passes; ++pass ) {
            params.step = static_cast<float>( std::pow( 2.0, (double)(flood_passes - 1 - pass) ) );

            SDL_PushGPUComputeUniformData( cb, /*slot=*/0, &params, sizeof( params ) );
            SDL_GPUStorageBufferReadWriteBinding rw{};
            rw.buffer = seed_write;
            rw.cycle  = false;
            SDL_GPUComputePass *p = SDL_BeginGPUComputePass( cb, nullptr, 0, &rw, 1 );
            if( !p ) { break; }

            SDL_BindGPUComputePipeline( p, flood_pipeline_ );

            SDL_GPUBuffer *ro[1] = { seed_read }; // t0 (input seed buffer)
            SDL_BindGPUComputeStorageBuffers( p, /*first_slot=*/0, ro, 1 );

            SDL_DispatchGPUCompute( p, gx, gy, 1 );
            SDL_EndGPUComputePass( p );

            // Swap ping-pong buffers.
            std::swap( seed_read, seed_write );
        }

        flood_result = seed_read;
    }

    // --- RESOLVE pass --- writes directly to target_sdf (sdf_storage_)
    {
        jfa_params params{};
        params.map_w = runtime_w;
        params.map_h = runtime_h;
        params.step  = 0.0f;

        SDL_PushGPUComputeUniformData( cb, /*slot=*/0, &params, sizeof( params ) );
        SDL_GPUStorageBufferReadWriteBinding rw{};
        rw.buffer = target_sdf;
        rw.cycle  = false;
        SDL_GPUComputePass *p = SDL_BeginGPUComputePass( cb, nullptr, 0, &rw, 1 );
        if( !p ) { return; }

        SDL_BindGPUComputePipeline( p, resolve_pipeline_ );

        // flood_result holds the final seed data after ping-pong swaps.
        SDL_GPUBuffer *ro[1] = { flood_result }; // t0 (seed buffer)
        SDL_BindGPUComputeStorageBuffers( p, /*first_slot=*/0, ro, 1 );

        SDL_DispatchGPUCompute( p, gx, gy, 1 );
        SDL_EndGPUComputePass( p );
    }
}

} // namespace lighting
