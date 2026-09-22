#include "gi_compute_pass.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/sdf_pass.h"
#include "lighting/shader_compiler.h"
#include "rc_params.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

// 4 floats per tile (rgb + pad) for both the intermediate field and the GI
// output. Plain float32 (not the RC's half) — the readback oracle reads it
// directly, and a structured buffer of scalar float is the D3D12-safe layout
// (the compute spike's proven pattern).
static constexpr std::uint32_t FLOATS_PER_TILE = 4u;

// Mirrors the RcParams cbuffer declared identically in rc_build.comp.hlsl,
// rc_merge.comp.hlsl and rc_resolve.comp.hlsl. 112 bytes: 8 scalars (32B) +
// RC_CASCADES uint4 entries (80B) — every scalar here is 4-byte, so natural
// C++ layout already matches HLSL's 16-byte-register cbuffer packing with no
// manual padding. Kept internal (not in the header): callers only see the
// public gi_params.
struct rc_params_gpu {
    std::uint32_t map_w = 0;
    std::uint32_t map_h = 0;
    std::uint32_t sdf_map_w = 0;
    std::uint32_t sdf_map_h = 0;
    std::uint32_t cascade = 0;
    std::uint32_t sdf_ss = 0;
    float c0_interval = 0.0f;
    float rc_pad0 = 0.0f;
    std::array<rc_cascade_geom, RC_CASCADES> geom{};
};
static_assert(
    sizeof( rc_params_gpu ) == 32 + RC_CASCADES * 16,
    "rc_params_gpu wire-stable with the RcParams cbuffer in rc_*.comp.hlsl" );

gi_compute_pass::~gi_compute_pass() { shutdown(); }

bool gi_compute_pass::init(gpu_device& dev, std::uint32_t max_w, std::uint32_t max_h) {
    shutdown();
    dev_ = &dev;
    if (!dev.ready()) {
        dbg(DL::Error) << "gi_compute_pass::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();

    const std::string field_src = load_lighting_shader_source( "gi_field.comp.hlsl" );
    const std::string build_src = load_lighting_shader_source( "rc_build.comp.hlsl" );
    const std::string merge_src = load_lighting_shader_source( "rc_merge.comp.hlsl" );
    const std::string resolve_src = load_lighting_shader_source( "rc_resolve.comp.hlsl" );
    // SDL_GetError() is GLOBAL: capture each pipeline's error at compile time,
    // or a later pipeline's failure overwrites the earlier one's message.
    auto fp = compile_compute_pipeline( dev, field_src, "main", "gi_field.comp" );
    const std::string field_err = fp ? "" : SDL_GetError();
    auto bp = compile_compute_pipeline( dev, build_src, "main", "rc_build.comp" );
    const std::string build_err = bp ? "" : SDL_GetError();
    auto mp = compile_compute_pipeline( dev, merge_src, "main", "rc_merge.comp" );
    const std::string merge_err = mp ? "" : SDL_GetError();
    auto rp = compile_compute_pipeline( dev, resolve_src, "main", "rc_resolve.comp" );
    const std::string resolve_err = rp ? "" : SDL_GetError();

    // Structural gate (DC::Main — DC::SDL is filtered). Field: 5 readonly
    // storage buffers (emitters, sdf, sky, albedo, prev-gi feedback) + 1
    // readwrite (field). RC build: 2 readonly (field, sdf) + 1 readwrite
    // (atlas). RC merge: 0 readonly + 1 readwrite (atlas, read AND written
    // through the same UAV). RC resolve: 1 readonly (atlas) + 1 readwrite
    // (gi out). No samplers (compute dodges the fragment sampler-order
    // root-sig that killed rc.frag on D3D12). A mismatch here means a
    // buffer was stripped or mis-declared — fail loudly at startup rather
    // than ship a silently degraded GI.
    DebugLogFL( DL::Info, DC::Main )
        << "gi_field.comp reflection: ro_sb=" << fp.resources.num_readonly_storage_buffers
        << " rw_sb=" << fp.resources.num_readwrite_storage_buffers
        << " uniforms=" << fp.resources.num_uniform_buffers << " threads=("
        << fp.resources.threadcount_x << "," << fp.resources.threadcount_y << ","
        << fp.resources.threadcount_z << ") (expects ro_sb=5 rw_sb=1)";
    DebugLogFL( DL::Info, DC::Main )
        << "rc_build.comp reflection: ro_sb=" << bp.resources.num_readonly_storage_buffers
        << " rw_sb=" << bp.resources.num_readwrite_storage_buffers
        << " uniforms=" << bp.resources.num_uniform_buffers << " threads=("
        << bp.resources.threadcount_x << "," << bp.resources.threadcount_y << ","
        << bp.resources.threadcount_z << ") (expects ro_sb=2 rw_sb=1)";
    DebugLogFL( DL::Info, DC::Main )
        << "rc_merge.comp reflection: ro_sb=" << mp.resources.num_readonly_storage_buffers
        << " rw_sb=" << mp.resources.num_readwrite_storage_buffers
        << " uniforms=" << mp.resources.num_uniform_buffers << " threads=("
        << mp.resources.threadcount_x << "," << mp.resources.threadcount_y << ","
        << mp.resources.threadcount_z << ") (expects ro_sb=0 rw_sb=1)";
    DebugLogFL( DL::Info, DC::Main )
        << "rc_resolve.comp reflection: ro_sb=" << rp.resources.num_readonly_storage_buffers
        << " rw_sb=" << rp.resources.num_readwrite_storage_buffers
        << " uniforms=" << rp.resources.num_uniform_buffers << " threads=("
        << rp.resources.threadcount_x << "," << rp.resources.threadcount_y << ","
        << rp.resources.threadcount_z << ") (expects ro_sb=1 rw_sb=1)";

    // Allocate the buffers FIRST, before checking the pipelines. The sprite's
    // GiBuf bind reads gi_buffer() unconditionally (all-or-none storage-buffer
    // bind), so a valid zeroed buffer must exist even if a pipeline failed on
    // this backend; ready() gates record(), so a failed pipeline just leaves GI
    // reading as zero.
    const std::uint32_t floats = max_w * max_h * FLOATS_PER_TILE;
    const std::uint32_t rc_floats = rc_total_floats( max_w, max_h );
    field_buf_ = create_buffer(
        floats,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE );
    rc_atlas_ = create_buffer(
        rc_floats,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE );
    gi_out_buf_ = create_buffer(
        floats,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ );
    if( !field_buf_ || !rc_atlas_ || !gi_out_buf_ ) {
        return false;
    }
    max_w_ = max_w;
    max_h_ = max_h;
    rc_atlas_floats_ = rc_floats;
    zero_buffer( gi_out_buf_, floats );
    zero_buffer( rc_atlas_, rc_floats );

    if( !fp ) {
        DebugLogFL( DL::Error, DC::Main )
            << "gi_compute_pass FIELD pipeline create failed: " << field_err
            << " — GI disabled, gi_buf bound as zero.";
        if( bp ) { SDL_ReleaseGPUComputePipeline( dev_->raw(), bp.pipeline ); }
        if( mp ) { SDL_ReleaseGPUComputePipeline( dev_->raw(), mp.pipeline ); }
        if( rp ) { SDL_ReleaseGPUComputePipeline( dev_->raw(), rp.pipeline ); }
        return false;
    }
    if( !bp ) {
        DebugLogFL( DL::Error, DC::Main )
            << "gi_compute_pass RC BUILD pipeline create failed: " << build_err
            << " — GI disabled, gi_buf bound as zero.";
        SDL_ReleaseGPUComputePipeline( dev_->raw(), fp.pipeline );
        if( mp ) { SDL_ReleaseGPUComputePipeline( dev_->raw(), mp.pipeline ); }
        if( rp ) { SDL_ReleaseGPUComputePipeline( dev_->raw(), rp.pipeline ); }
        return false;
    }
    if( !mp ) {
        DebugLogFL( DL::Error, DC::Main )
            << "gi_compute_pass RC MERGE pipeline create failed: " << merge_err
            << " — GI disabled, gi_buf bound as zero.";
        SDL_ReleaseGPUComputePipeline( dev_->raw(), fp.pipeline );
        SDL_ReleaseGPUComputePipeline( dev_->raw(), bp.pipeline );
        if( rp ) { SDL_ReleaseGPUComputePipeline( dev_->raw(), rp.pipeline ); }
        return false;
    }
    if( !rp ) {
        DebugLogFL( DL::Error, DC::Main )
            << "gi_compute_pass RC RESOLVE pipeline create failed: " << resolve_err
            << " — GI disabled, gi_buf bound as zero.";
        SDL_ReleaseGPUComputePipeline( dev_->raw(), fp.pipeline );
        SDL_ReleaseGPUComputePipeline( dev_->raw(), bp.pipeline );
        SDL_ReleaseGPUComputePipeline( dev_->raw(), mp.pipeline );
        return false;
    }
    field_pipeline_ = fp.pipeline;
    rc_build_pipeline_ = bp.pipeline;
    rc_merge_pipeline_ = mp.pipeline;
    rc_resolve_pipeline_ = rp.pipeline;
    return true;
}

SDL_GPUBuffer* gi_compute_pass::create_buffer(std::uint32_t floats, SDL_GPUBufferUsageFlags usage) {
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = usage;
    bci.size = floats * static_cast<std::uint32_t>(sizeof(float));
    SDL_GPUBuffer* b = SDL_CreateGPUBuffer(dev_->raw(), &bci);
    if (!b) {
        DebugLogFL(DL::Error, DC::Main) << "gi_compute_pass: buffer create: " << SDL_GetError();
    }
    return b;
}

void gi_compute_pass::zero_buffer( SDL_GPUBuffer* buf, std::uint32_t floats ) {
    if( !buf || floats == 0 ) { return; }
    const std::uint32_t bytes = floats * static_cast<std::uint32_t>( sizeof( float ) );
    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer( dev_->raw(), &tbci );
    if( !tb ) { return; }
    void* map = SDL_MapGPUTransferBuffer( dev_->raw(), tb, false );
    if( !map ) {
        SDL_ReleaseGPUTransferBuffer( dev_->raw(), tb );
        return;
    }
    std::memset( map, 0, bytes );
    SDL_UnmapGPUTransferBuffer( dev_->raw(), tb );
    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer( dev_->raw() );
    if( !cb ) {
        SDL_ReleaseGPUTransferBuffer( dev_->raw(), tb );
        return;
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass( cb );
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = tb;
    src.offset = 0;
    SDL_GPUBufferRegion dst{};
    dst.buffer = buf;
    dst.offset = 0;
    dst.size = bytes;
    SDL_UploadToGPUBuffer( cp, &src, &dst, /*cycle=*/false );
    SDL_EndGPUCopyPass( cp );
    SDL_SubmitGPUCommandBuffer( cb );
    SDL_ReleaseGPUTransferBuffer( dev_->raw(), tb );
}

bool gi_compute_pass::resize( std::uint32_t max_w, std::uint32_t max_h ) {
    if( field_buf_ && rc_atlas_ && gi_out_buf_ && max_w == max_w_ && max_h == max_h_ ) {
        return true;
    }
    if( !dev_ || !dev_->ready() ) { return false; }
    if( field_buf_ ) { SDL_ReleaseGPUBuffer( dev_->raw(), field_buf_ ); field_buf_ = nullptr; }
    if( rc_atlas_ ) { SDL_ReleaseGPUBuffer( dev_->raw(), rc_atlas_ ); rc_atlas_ = nullptr; }
    if( gi_out_buf_ ) { SDL_ReleaseGPUBuffer( dev_->raw(), gi_out_buf_ ); gi_out_buf_ = nullptr; }
    const std::uint32_t floats = max_w * max_h * FLOATS_PER_TILE;
    const std::uint32_t rc_floats = rc_total_floats( max_w, max_h );
    field_buf_ = create_buffer(
        floats,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE );
    rc_atlas_ = create_buffer(
        rc_floats,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE );
    gi_out_buf_ = create_buffer(
        floats,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ );
    if( !field_buf_ || !rc_atlas_ || !gi_out_buf_ ) { return false; }
    max_w_ = max_w;
    max_h_ = max_h;
    rc_atlas_floats_ = rc_floats;
    zero_buffer( gi_out_buf_, floats );
    zero_buffer( rc_atlas_, rc_floats );
    return true;
}

void gi_compute_pass::shutdown() noexcept {
    if( dev_ && dev_->ready() ) {
        if( field_pipeline_ ) { SDL_ReleaseGPUComputePipeline( dev_->raw(), field_pipeline_ ); }
        if( rc_build_pipeline_ ) { SDL_ReleaseGPUComputePipeline( dev_->raw(), rc_build_pipeline_ ); }
        if( rc_merge_pipeline_ ) { SDL_ReleaseGPUComputePipeline( dev_->raw(), rc_merge_pipeline_ ); }
        if( rc_resolve_pipeline_ ) {
            SDL_ReleaseGPUComputePipeline( dev_->raw(), rc_resolve_pipeline_ );
        }
        if( field_buf_ ) { SDL_ReleaseGPUBuffer( dev_->raw(), field_buf_ ); }
        if( rc_atlas_ ) { SDL_ReleaseGPUBuffer( dev_->raw(), rc_atlas_ ); }
        if( gi_out_buf_ ) { SDL_ReleaseGPUBuffer( dev_->raw(), gi_out_buf_ ); }
    }
    field_pipeline_ = rc_build_pipeline_ = rc_merge_pipeline_ = rc_resolve_pipeline_ = nullptr;
    field_buf_ = rc_atlas_ = gi_out_buf_ = nullptr;
    max_w_ = max_h_ = rc_atlas_floats_ = 0;
}

void gi_compute_pass::record(
    SDL_GPUCommandBuffer* cb, SDL_GPUBuffer* emitter_buf, SDL_GPUBuffer* sdf_buf,
    SDL_GPUBuffer* sky_buf, SDL_GPUBuffer* albedo_buf, std::uint32_t runtime_w,
    std::uint32_t runtime_h, const gi_params& params ) {
    if( !ready() || !cb || !emitter_buf || !sdf_buf || !sky_buf || !albedo_buf
        || runtime_w == 0 || runtime_h == 0 ) {
        return;
    }
    const std::uint32_t gx = ( runtime_w + 7u ) / 8u; // ceil(W/8) — numthreads(8,8,1)
    const std::uint32_t gy = ( runtime_h + 7u ) / 8u;

    // Multi-bounce convergence: GI only re-records on a structure rebuild
    // (terrain change / z / origin / >=4-tile camera drift), which for a
    // stationary player can be ONE event, ever. A single field->RC pass only
    // gives gi_feedback one iteration of the 1/(1-k) series - indistinguishable
    // from "no effect". Looping the whole pipeline a few times within this
    // ONE rebuild lets each iteration's FIELD read the previous iteration's
    // freshly-resolved gi_out_buf_ (same trick as the cross-rebuild feedback,
    // just repeated immediately), so multi-bounce light actually shows up the
    // moment the knob is raised. Off (gi_feedback<=0): exactly 1 pass, byte-
    // identical to pre-feedback behaviour.
    const std::uint32_t iterations = ( params.gi_feedback > 0.001f ) ? 3u : 1u;
    for( std::uint32_t iter = 0; iter < iterations; ++iter ) {

    // ---- Pass 1: FIELD — per-tile direct radiance gather. Unchanged by
    // Stage 7; Radiance Cascades reads this exactly like the old bounce did.
    SDL_PushGPUComputeUniformData( cb, /*slot=*/0, &params, sizeof( params ) );
    {
        SDL_GPUStorageBufferReadWriteBinding rw{};
        rw.buffer = field_buf_;
        rw.cycle = false; // retained intermediate; fully rewritten each gather
        SDL_GPUComputePass* p = SDL_BeginGPUComputePass( cb, nullptr, 0, &rw, 1 );
        if( !p ) {
            dbg( DL::Error ) << "gi field pass: BeginGPUComputePass failed: " << SDL_GetError();
            return;
        }
        SDL_BindGPUComputePipeline( p, field_pipeline_ );
        SDL_GPUBuffer* ro[5] = { emitter_buf, sdf_buf, sky_buf, albedo_buf, gi_out_buf_ }; // t0..t4
        SDL_BindGPUComputeStorageBuffers( p, /*first_slot=*/0, ro, 5 );
        SDL_DispatchGPUCompute( p, gx, gy, 1 );
        SDL_EndGPUComputePass( p );
    }

    // Per-cascade geometry for THIS frame's runtime map size (<= the max_w_/
    // max_h_ the atlas was allocated for).
    const auto geom = rc_compute_geometry( runtime_w, runtime_h );
    const std::uint32_t needed = rc_total_floats( runtime_w, runtime_h );
    if( needed > rc_atlas_floats_ ) {
        DebugLogFL( DL::Error, DC::Main )
                << "gi_compute_pass: rc atlas too small for runtime size (" << needed << " > "
                << rc_atlas_floats_ << ") — skipping RC this frame";
        return;
    }

    rc_params_gpu rp{};
    rp.map_w = runtime_w;
    rp.map_h = runtime_h;
    rp.sdf_map_w = runtime_w;
    rp.sdf_map_h = runtime_h;
    rp.sdf_ss = static_cast<std::uint32_t>( SDF_SUPERSAMPLE );
    rp.c0_interval = RC_C0_INTERVAL;
    for( std::uint32_t i = 0; i < RC_CASCADES; ++i ) {
        rp.geom[i] = geom[i];
    }

    // ---- Pass 2: RC BUILD, one dispatch per cascade — mutually independent
    // at build time, so dispatch order among them does not matter.
    for( std::uint32_t i = 0; i < RC_CASCADES; ++i ) {
        rp.cascade = i;
        SDL_PushGPUComputeUniformData( cb, /*slot=*/0, &rp, sizeof( rp ) );
        SDL_GPUStorageBufferReadWriteBinding rw{};
        rw.buffer = rc_atlas_;
        rw.cycle = false;
        SDL_GPUComputePass* p = SDL_BeginGPUComputePass( cb, nullptr, 0, &rw, 1 );
        if( !p ) {
            dbg( DL::Error ) << "rc build pass: BeginGPUComputePass failed: " << SDL_GetError();
            return;
        }
        SDL_BindGPUComputePipeline( p, rc_build_pipeline_ );
        SDL_GPUBuffer* ro[2] = { field_buf_, sdf_buf }; // t0 (field), t1 (sdf)
        SDL_BindGPUComputeStorageBuffers( p, /*first_slot=*/0, ro, 2 );
        const std::uint32_t cgx = ( geom[i].probes_x + 7u ) / 8u;
        const std::uint32_t cgy = ( geom[i].probes_y + 7u ) / 8u;
        SDL_DispatchGPUCompute( p, cgx, cgy, 1 );
        SDL_EndGPUComputePass( p );
    }

    // ---- Pass 3: RC MERGE, DESCENDING (RC_CASCADES-2 .. 0). Each dispatch
    // merges cascade i against cascade i+1's CURRENT state — the previous
    // dispatch's output for every i+1 except the top cascade, which is never
    // merged (nothing above it) and so still holds its raw BUILD output.
    // Order matters here, unlike BUILD: reversing it would merge against
    // stale (un-merged) data.
    for( std::uint32_t step = 0; step + 1 < RC_CASCADES; ++step ) {
        const std::uint32_t i = RC_CASCADES - 2 - step; // RC_CASCADES-2, ..., 0
        rp.cascade = i;
        SDL_PushGPUComputeUniformData( cb, /*slot=*/0, &rp, sizeof( rp ) );
        SDL_GPUStorageBufferReadWriteBinding rw{};
        rw.buffer = rc_atlas_;
        rw.cycle = false;
        SDL_GPUComputePass* p = SDL_BeginGPUComputePass( cb, nullptr, 0, &rw, 1 );
        if( !p ) {
            dbg( DL::Error ) << "rc merge pass: BeginGPUComputePass failed: " << SDL_GetError();
            return;
        }
        SDL_BindGPUComputePipeline( p, rc_merge_pipeline_ );
        // No readonly storage buffers: rc_merge.comp reads AND writes rc_atlas_
        // through the single RW binding above (ro_sb=0).
        const std::uint32_t cgx = ( geom[i].probes_x + 7u ) / 8u;
        const std::uint32_t cgy = ( geom[i].probes_y + 7u ) / 8u;
        SDL_DispatchGPUCompute( p, cgx, cgy, 1 );
        SDL_EndGPUComputePass( p );
    }

    // ---- Pass 4: RC RESOLVE — cascade 0 (now fully merged) → gi_out_buf_,
    // the sprite's GI input, in the same layout gi_bounce2.comp used to write.
    rp.cascade = 0;
    SDL_PushGPUComputeUniformData( cb, /*slot=*/0, &rp, sizeof( rp ) );
    {
        SDL_GPUStorageBufferReadWriteBinding rw{};
        rw.buffer = gi_out_buf_;
        rw.cycle = false;
        SDL_GPUComputePass* p = SDL_BeginGPUComputePass( cb, nullptr, 0, &rw, 1 );
        if( !p ) {
            dbg( DL::Error ) << "rc resolve pass: BeginGPUComputePass failed: " << SDL_GetError();
            return;
        }
        SDL_BindGPUComputePipeline( p, rc_resolve_pipeline_ );
        SDL_GPUBuffer* ro[1] = { rc_atlas_ }; // t0
        SDL_BindGPUComputeStorageBuffers( p, /*first_slot=*/0, ro, 1 );
        SDL_DispatchGPUCompute( p, gx, gy, 1 );
        SDL_EndGPUComputePass( p );
    }
    } // for iter
}

void gi_compute_pass::debug_log_stats( std::uint32_t runtime_w, std::uint32_t runtime_h ) {
    if( !gi_out_buf_ || !dev_ || !dev_->ready() || runtime_w == 0 || runtime_h == 0 ) {
        return;
    }
    SDL_GPUDevice* d = dev_->raw();
    const std::uint32_t floats = max_w_ * max_h_ * FLOATS_PER_TILE;
    const std::uint32_t bytes = floats * static_cast<std::uint32_t>(sizeof(float));
    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tbci.size = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(d, &tbci);
    if (!tb) { return; }
    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(d);
    if (!cb) {
        SDL_ReleaseGPUTransferBuffer(d, tb);
        return;
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUBufferRegion rd{};
    rd.buffer = gi_out_buf_;
    rd.offset = 0;
    rd.size = bytes;
    SDL_GPUTransferBufferLocation rdst{};
    rdst.transfer_buffer = tb;
    rdst.offset = 0;
    SDL_DownloadFromGPUBuffer(cp, &rd, &rdst);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cb);
    SDL_WaitForGPUIdle(d); // synchronous — dev oracle only

    const float* px = static_cast<const float*>(SDL_MapGPUTransferBuffer(d, tb, false));
    if (!px) {
        SDL_ReleaseGPUTransferBuffer(d, tb);
        return;
    }
    // gi_buf_ is x-major gi[(x*map_h+y)*4 + c]. Stats over the runtime region.
    double sum = 0.0, cx = 0.0, cy = 0.0, wsum = 0.0;
    float mx = 0.0f;
    long nz = 0;
    for (std::uint32_t x = 0; x < runtime_w; ++x) {
        for (std::uint32_t y = 0; y < runtime_h; ++y) {
            const std::uint32_t idx = (x * max_h_ + y) * FLOATS_PER_TILE;
            const float lum = px[idx + 0] + px[idx + 1] + px[idx + 2];
            sum += lum;
            mx = std::max(mx, lum);
            if (lum > 0.0001f) {
                ++nz;
                cx += static_cast<double>(x) * lum;
                cy += static_cast<double>(y) * lum;
                wsum += lum;
            }
        }
    }
    SDL_UnmapGPUTransferBuffer(d, tb);
    SDL_ReleaseGPUTransferBuffer(d, tb);

    const double cxn = wsum > 0.0 ? cx / wsum : -1.0;
    const double cyn = wsum > 0.0 ? cy / wsum : -1.0;
    DebugLogFL(DL::Info, DC::Main)
        << "gi compute readback [" << runtime_w << "x" << runtime_h << "]: sum=" << sum
        << " max=" << mx << " nonzero=" << nz << " centroid_tile=(" << cxn << "," << cyn << ")";
}

} // namespace lighting
