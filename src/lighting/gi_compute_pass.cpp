#include "gi_compute_pass.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"

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

gi_compute_pass::~gi_compute_pass() { shutdown(); }

bool gi_compute_pass::init(gpu_device& dev, std::uint32_t max_w, std::uint32_t max_h) {
    shutdown();
    dev_ = &dev;
    if (!dev.ready()) {
        dbg(DL::Error) << "gi_compute_pass::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();

    const std::string field_src = load_lighting_shader_source("gi_field.comp.hlsl");
    const std::string bounce_src = load_lighting_shader_source("gi_bounce.comp.hlsl");
    auto fp = compile_compute_pipeline(dev, field_src, "main", "gi_field.comp");
    auto bp = compile_compute_pipeline(dev, bounce_src, "main", "gi_bounce.comp");

    // Structural gate (DC::Main — DC::SDL is filtered). Field: 2 readonly storage
    // buffers (emitters, sdf, sky) + 1 readwrite (field). Bounce: 2 readonly
    // (field, sdf) + 1 readwrite (gi). No samplers (compute dodges the fragment
    // sampler-order root-sig that killed rc.frag on D3D12).
    DebugLogFL(DL::Info, DC::Main)
        << "gi_field.comp reflection: ro_sb=" << fp.resources.num_readonly_storage_buffers
        << " rw_sb=" << fp.resources.num_readwrite_storage_buffers
        << " uniforms=" << fp.resources.num_uniform_buffers << " threads=("
        << fp.resources.threadcount_x << "," << fp.resources.threadcount_y << ","
        << fp.resources.threadcount_z << ") (expects ro_sb=3 rw_sb=1)";
    DebugLogFL(DL::Info, DC::Main)
        << "gi_bounce.comp reflection: ro_sb=" << bp.resources.num_readonly_storage_buffers
        << " rw_sb=" << bp.resources.num_readwrite_storage_buffers
        << " uniforms=" << bp.resources.num_uniform_buffers << " threads=("
        << bp.resources.threadcount_x << "," << bp.resources.threadcount_y << ","
        << bp.resources.threadcount_z << ") (expects ro_sb=2 rw_sb=1)";

    // Allocate the buffers FIRST, before checking the pipelines. The sprite's
    // GiBuf bind reads gi_buffer() unconditionally (all-or-none storage-buffer
    // bind), so a valid zeroed buffer must exist even if a pipeline failed on
    // this backend; ready() gates record(), so a failed pipeline just leaves GI
    // reading as zero.
    const std::uint32_t floats = max_w * max_h * FLOATS_PER_TILE;
    field_buf_ = create_buffer(
        floats,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE);
    gi_buf_ = create_buffer(
        floats,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
    if (!field_buf_ || !gi_buf_) { return false; }
    max_w_ = max_w;
    max_h_ = max_h;
    zero_buffer(gi_buf_, floats);

    if (!fp) {
        DebugLogFL(DL::Error, DC::Main)
            << "gi_compute_pass FIELD pipeline create failed: " << SDL_GetError()
            << " — GI disabled, gi_buf bound as zero.";
        return false;
    }
    if (!bp) {
        DebugLogFL(DL::Error, DC::Main)
            << "gi_compute_pass BOUNCE pipeline create failed: " << SDL_GetError()
            << " — GI disabled, gi_buf bound as zero.";
        SDL_ReleaseGPUComputePipeline(dev_->raw(), fp.pipeline);
        return false;
    }
    field_pipeline_ = fp.pipeline;
    bounce_pipeline_ = bp.pipeline;
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

void gi_compute_pass::zero_buffer(SDL_GPUBuffer* buf, std::uint32_t floats) {
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

bool gi_compute_pass::resize(std::uint32_t max_w, std::uint32_t max_h) {
    if (field_buf_ && gi_buf_ && max_w == max_w_ && max_h == max_h_) { return true; }
    if (dev_ && dev_->ready()) {
        if (field_buf_) {
            SDL_ReleaseGPUBuffer(dev_->raw(), field_buf_);
            field_buf_ = nullptr;
        }
        if (gi_buf_) {
            SDL_ReleaseGPUBuffer(dev_->raw(), gi_buf_);
            gi_buf_ = nullptr;
        }
    }
    const std::uint32_t floats = max_w * max_h * FLOATS_PER_TILE;
    field_buf_ = create_buffer(
        floats,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE);
    gi_buf_ = create_buffer(
        floats,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
    if (!field_buf_ || !gi_buf_) { return false; }
    max_w_ = max_w;
    max_h_ = max_h;
    zero_buffer(gi_buf_, floats);
    return true;
}

void gi_compute_pass::shutdown() noexcept {
    if (dev_ && dev_->ready()) {
        if (field_pipeline_) { SDL_ReleaseGPUComputePipeline(dev_->raw(), field_pipeline_); }
        if (bounce_pipeline_) { SDL_ReleaseGPUComputePipeline(dev_->raw(), bounce_pipeline_); }
        if (field_buf_) { SDL_ReleaseGPUBuffer(dev_->raw(), field_buf_); }
        if (gi_buf_) { SDL_ReleaseGPUBuffer(dev_->raw(), gi_buf_); }
    }
    field_pipeline_ = bounce_pipeline_ = nullptr;
    field_buf_ = gi_buf_ = nullptr;
    max_w_ = max_h_ = 0;
}

void gi_compute_pass::record(
    SDL_GPUCommandBuffer* cb, SDL_GPUBuffer* emitter_buf, SDL_GPUBuffer* sdf_buf,
    SDL_GPUBuffer* sky_buf, std::uint32_t runtime_w, std::uint32_t runtime_h,
    const gi_params& params) {
    if (!ready() || !cb || !emitter_buf || !sdf_buf || !sky_buf || runtime_w == 0
        || runtime_h == 0) {
        return;
    }
    const std::uint32_t gx = (runtime_w + 7u) / 8u; // ceil(W/8) — numthreads(8,8,1)
    const std::uint32_t gy = (runtime_h + 7u) / 8u;

    // ── Pass 1: FIELD — per-tile direct radiance (occluded emitter gather). ──
    SDL_PushGPUComputeUniformData(cb, /*slot=*/0, &params, sizeof(params));
    {
        SDL_GPUStorageBufferReadWriteBinding rw{};
        rw.buffer = field_buf_;
        rw.cycle = false; // retained intermediate; fully rewritten each gather
        SDL_GPUComputePass* p = SDL_BeginGPUComputePass(cb, nullptr, 0, &rw, 1);
        if (!p) {
            dbg(DL::Error) << "gi field pass: BeginGPUComputePass failed: " << SDL_GetError();
            return;
        }
        SDL_BindGPUComputePipeline(p, field_pipeline_);
        SDL_GPUBuffer* ro[3] = {emitter_buf, sdf_buf, sky_buf}; // t0, t1, t2 (SkyBuf)
        SDL_BindGPUComputeStorageBuffers(p, /*first_slot=*/0, ro, 3);
        SDL_DispatchGPUCompute(p, gx, gy, 1);
        SDL_EndGPUComputePass(p);
    }

    // ── Pass 2: BOUNCE — march rays through the field → colored bounce. ──
    SDL_PushGPUComputeUniformData(cb, /*slot=*/0, &params, sizeof(params));
    {
        SDL_GPUStorageBufferReadWriteBinding rw{};
        rw.buffer = gi_buf_;
        rw.cycle = false; // retained on skip frames (sprite reads it every frame)
        SDL_GPUComputePass* p = SDL_BeginGPUComputePass(cb, nullptr, 0, &rw, 1);
        if (!p) {
            dbg(DL::Error) << "gi bounce pass: BeginGPUComputePass failed: " << SDL_GetError();
            return;
        }
        SDL_BindGPUComputePipeline(p, bounce_pipeline_);
        SDL_GPUBuffer* ro[2] = {field_buf_, sdf_buf}; // t0 (field), t1 (sdf)
        SDL_BindGPUComputeStorageBuffers(p, /*first_slot=*/0, ro, 2);
        SDL_DispatchGPUCompute(p, gx, gy, 1);
        SDL_EndGPUComputePass(p);
    }
}

void gi_compute_pass::debug_log_stats(std::uint32_t runtime_w, std::uint32_t runtime_h) {
    if (!gi_buf_ || !dev_ || !dev_->ready() || runtime_w == 0 || runtime_h == 0) { return; }
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
    rd.buffer = gi_buf_;
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
