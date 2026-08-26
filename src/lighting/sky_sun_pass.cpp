#include "sky_sun_pass.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"

#include <cstdint>
#include <cstring>
#include <string>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

// 4 floats per tile (rgb sky-access + a sun-occ). Plain float32, scalar
// StructuredBuffer — the D3D12-safe layout the compute spike proved.
static constexpr std::uint32_t FLOATS_PER_TILE = 4u;

sky_sun_pass::~sky_sun_pass() { shutdown(); }

bool sky_sun_pass::init(gpu_device& dev, std::uint32_t max_w, std::uint32_t max_h) {
    shutdown();
    dev_ = &dev;
    if (!dev.ready()) {
        dbg(DL::Error) << "sky_sun_pass::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();

    const std::string src = load_lighting_shader_source("sky_sun.comp.hlsl");
    auto pp = compile_compute_pipeline(dev, src, "main", "sky_sun.comp");

    // Structural gate (DC::Main — DC::SDL is filtered). 2 readonly storage
    // buffers (unified coverage occluder + 8x SDF) + 1 readwrite (sky). No
    // samplers (compute dodges the fragment sampler-order root-sig that killed
    // rc.frag on D3D12).
    DebugLogFL(DL::Info, DC::Main)
        << "sky_sun.comp reflection: ro_sb=" << pp.resources.num_readonly_storage_buffers
        << " rw_sb=" << pp.resources.num_readwrite_storage_buffers
        << " uniforms=" << pp.resources.num_uniform_buffers << " threads=("
        << pp.resources.threadcount_x << "," << pp.resources.threadcount_y << ","
        << pp.resources.threadcount_z << ") (expects ro_sb=2 rw_sb=1)";

    // Allocate the buffer FIRST, before checking the pipeline. The sprite's
    // SkyBuf bind reads sky_buffer() unconditionally (all-or-none storage-buffer
    // bind), so a valid zeroed buffer must exist even if the pipeline failed on
    // this backend; ready() gates record(), so a failed pipeline just leaves the
    // sky reading as zero (dark — same as "no data yet").
    // COMPUTE_STORAGE_READ: gi_field.comp reads SkyBuf to inject sun/sky surface
    // radiance into the GI field (P2 — daylight bounce). GRAPHICS_STORAGE_READ:
    // sprite.frag reads it as the direct SkyBuf term.
    const std::uint32_t floats = max_w * max_h * FLOATS_PER_TILE;
    sky_buf_ = create_buffer(
        floats,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ
            | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
    if (!sky_buf_) { return false; }
    max_w_ = max_w;
    max_h_ = max_h;
    zero_buffer(sky_buf_, floats);

    if (!pp) {
        DebugLogFL(DL::Error, DC::Main)
            << "sky_sun_pass pipeline create failed: " << SDL_GetError()
            << " — sky/sun directional pass disabled, sky_buf bound as zero.";
        return false;
    }
    pipeline_ = pp.pipeline;
    return true;
}

SDL_GPUBuffer* sky_sun_pass::create_buffer(std::uint32_t floats, SDL_GPUBufferUsageFlags usage) {
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = usage;
    bci.size = floats * static_cast<std::uint32_t>(sizeof(float));
    SDL_GPUBuffer* b = SDL_CreateGPUBuffer(dev_->raw(), &bci);
    if (!b) {
        DebugLogFL(DL::Error, DC::Main) << "sky_sun_pass: buffer create: " << SDL_GetError();
    }
    return b;
}

void sky_sun_pass::zero_buffer(SDL_GPUBuffer* buf, std::uint32_t floats) {
    if (!buf || floats == 0) { return; }
    const std::uint32_t bytes = floats * static_cast<std::uint32_t>(sizeof(float));
    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size = bytes;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_->raw(), &tbci);
    if (!tb) { return; }
    void* map = SDL_MapGPUTransferBuffer(dev_->raw(), tb, false);
    if (map) {
        std::memset(map, 0, bytes);
        SDL_UnmapGPUTransferBuffer(dev_->raw(), tb);
        SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(dev_->raw());
        if (cb) {
            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
            SDL_GPUTransferBufferLocation src{};
            src.transfer_buffer = tb;
            src.offset = 0;
            SDL_GPUBufferRegion dst{};
            dst.buffer = buf;
            dst.offset = 0;
            dst.size = bytes;
            SDL_UploadToGPUBuffer(cp, &src, &dst, /*cycle=*/false);
            SDL_EndGPUCopyPass(cp);
            SDL_SubmitGPUCommandBuffer(cb);
        }
    }
    SDL_ReleaseGPUTransferBuffer(dev_->raw(), tb);
}

bool sky_sun_pass::resize(std::uint32_t max_w, std::uint32_t max_h) {
    if (sky_buf_ && max_w == max_w_ && max_h == max_h_) { return true; }
    if (dev_ && dev_->ready() && sky_buf_) {
        SDL_ReleaseGPUBuffer(dev_->raw(), sky_buf_);
        sky_buf_ = nullptr;
    }
    const std::uint32_t floats = max_w * max_h * FLOATS_PER_TILE;
    sky_buf_ = create_buffer(
        floats,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ
            | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
    if (!sky_buf_) { return false; }
    max_w_ = max_w;
    max_h_ = max_h;
    zero_buffer(sky_buf_, floats);
    return true;
}

void sky_sun_pass::shutdown() noexcept {
    if (dev_ && dev_->ready()) {
        if (pipeline_) { SDL_ReleaseGPUComputePipeline(dev_->raw(), pipeline_); }
        if (sky_buf_) { SDL_ReleaseGPUBuffer(dev_->raw(), sky_buf_); }
    }
    pipeline_ = nullptr;
    sky_buf_ = nullptr;
    max_w_ = max_h_ = 0;
}

void sky_sun_pass::record(
    SDL_GPUCommandBuffer* cb, SDL_GPUBuffer* occ_buf, SDL_GPUBuffer* sdf_buf,
    std::uint32_t runtime_w, std::uint32_t runtime_h, const sky_sun_params& params ) {
    if( !ready() || !cb || !occ_buf || !sdf_buf || runtime_w == 0 || runtime_h == 0 ) {
        return;
    }
    const std::uint32_t gx = ( runtime_w + 7u ) / 8u; // ceil(W/8) — numthreads(8,8,1)
    const std::uint32_t gy = ( runtime_h + 7u ) / 8u;

    SDL_PushGPUComputeUniformData( cb, /*slot=*/0, &params, sizeof( params ) );
    SDL_GPUStorageBufferReadWriteBinding rw{};
    rw.buffer = sky_buf_;
    rw.cycle = false; // retained on skip frames (sprite reads it every frame)
    SDL_GPUComputePass* p = SDL_BeginGPUComputePass( cb, nullptr, 0, &rw, 1 );
    if( !p ) {
        dbg( DL::Error ) << "sky_sun pass: BeginGPUComputePass failed: " << SDL_GetError();
        return;
    }
    SDL_BindGPUComputePipeline( p, pipeline_ );
    SDL_GPUBuffer* ro[2] = { occ_buf, sdf_buf }; // t0 (coverage occluder), t1 (8x SDF)
    SDL_BindGPUComputeStorageBuffers( p, /*first_slot=*/0, ro, 2 );
    SDL_DispatchGPUCompute( p, gx, gy, 1 );
    SDL_EndGPUComputePass( p );
}

} // namespace lighting
