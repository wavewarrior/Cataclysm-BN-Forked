#include "gpu_sdf_pass.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/sdf_pass.h"
#include "lighting/shader_compiler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

// Subcells per tile side. Single-sourced from sdf_pass.h's SDF_SUPERSAMPLE so the
// GPU buffer sizing here can never drift from the CPU upload sizing. Must also
// match the shader-side SDF_SS in jfa_shared.hlsl.
static constexpr std::uint32_t SDF_SS = static_cast<std::uint32_t>(SDF_SUPERSAMPLE);

gpu_sdf_pass::~gpu_sdf_pass() { shutdown(); }

bool gpu_sdf_pass::init(gpu_device& dev, std::uint32_t max_w, std::uint32_t max_h) {
    shutdown();
    dev_ = &dev;
    if (!dev.ready()) {
        dbg(DL::Error) << "gpu_sdf_pass::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();

    const std::uint32_t max_sw = max_w * SDF_SS;
    const std::uint32_t max_sh = max_h * SDF_SS;

    // Compile all three compute pipelines. `name` is the bare "<x>.comp" label;
    // the source file adds the ".hlsl" extension (matches gi_compute_pass /
    // sky_sun_pass). Loading without ".hlsl" reads an empty file → "missing entry
    // point" → the pipeline silently fails to create.
    auto compile_one = [&](const char* name) -> SDL_GPUComputePipeline* {
        const std::string src = load_lighting_shader_source(std::string(name) + ".hlsl");
        auto pp = compile_compute_pipeline(dev, src, "main", name);
        if (!pp) {
            DebugLogFL(DL::Error, DC::Main) << "gpu_sdf_pass: " << name << " pipeline failed";
            return nullptr;
        }
        return pp.pipeline;
    };

    seed_pipeline_ = compile_one("jfa_seed.comp");
    flood_pipeline_ = compile_one("jfa_flood.comp");
    resolve_pipeline_ = compile_one("jfa_resolve.comp");
    occ_base_pipeline_ = compile_one("occ_base.comp");
    occ_raster_pipeline_ = compile_one("occ_raster.comp");

    // Fail loudly if any pipeline didn't compile — otherwise ready() is false and
    // the JFA pass is silently disabled while init() reports success (which is
    // exactly how the missing-".hlsl" / missing-include breakage stayed hidden).
    if (!seed_pipeline_ || !flood_pipeline_ || !resolve_pipeline_ || !occ_base_pipeline_
        || !occ_raster_pipeline_) {
        dbg(DL::Error) << "gpu_sdf_pass::init: one or more JFA pipelines failed to compile";
        return false;
    }

    // Allocate ping-pong seed buffers (2 floats/subcell).
    const std::uint32_t rw =
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    const std::uint32_t subcells = max_sw * max_sh;
    seed_a_ = create_buffer(subcells * 2u * 4u, rw);
    seed_b_ = create_buffer(subcells * 2u * 4u, rw);
    // Sub-tile coverage field, 1 uint/subcell.
    occ_ss_ = create_buffer(subcells * 4u, rw);
    // Tile-res captured mask, 1 uint/tile.
    max_tiles_ = max_w * max_h;
    captured_buf_ = create_buffer(max_tiles_ * 4u, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);

    if (!seed_a_ || !seed_b_ || !occ_ss_ || !captured_buf_) {
        dbg(DL::Error) << "gpu_sdf_pass: buffer allocation failed";
        return false;
    }
    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = max_tiles_ * 4u;
        xfer_captured_ = SDL_CreateGPUTransferBuffer(dev.raw(), &tbci);
    }
    if (!xfer_captured_) {
        dbg(DL::Error) << "gpu_sdf_pass: captured transfer buffer failed: " << SDL_GetError();
        return false;
    }

    max_sw_ = max_sw;
    max_sh_ = max_sh;

    DebugLogFL(DL::Info, DC::Main)
        << "gpu_sdf_pass init OK: SS=" << max_sw << "x" << max_sh << " tiles=" << max_tiles_;

    return true;
}

SDL_GPUBuffer* gpu_sdf_pass::create_buffer(std::uint32_t bytes, SDL_GPUBufferUsageFlags usage) {
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = usage;
    bci.size = bytes;
    SDL_GPUBuffer* b = SDL_CreateGPUBuffer(dev_->raw(), &bci);
    if (!b) { dbg(DL::Error) << "gpu_sdf_pass: buffer create failed: " << SDL_GetError(); }
    return b;
}

void gpu_sdf_pass::shutdown() noexcept {
    if (dev_ && dev_->ready()) {
        SDL_GPUDevice* d = dev_->raw();
        if (seed_pipeline_) { SDL_ReleaseGPUComputePipeline(d, seed_pipeline_); }
        if (flood_pipeline_) { SDL_ReleaseGPUComputePipeline(d, flood_pipeline_); }
        if (resolve_pipeline_) { SDL_ReleaseGPUComputePipeline(d, resolve_pipeline_); }
        if (occ_base_pipeline_) { SDL_ReleaseGPUComputePipeline(d, occ_base_pipeline_); }
        if (occ_raster_pipeline_) { SDL_ReleaseGPUComputePipeline(d, occ_raster_pipeline_); }
        if (seed_a_) { SDL_ReleaseGPUBuffer(d, seed_a_); }
        if (seed_b_) { SDL_ReleaseGPUBuffer(d, seed_b_); }
        if (occ_ss_) { SDL_ReleaseGPUBuffer(d, occ_ss_); }
        if (captured_buf_) { SDL_ReleaseGPUBuffer(d, captured_buf_); }
        if (quads_buf_) { SDL_ReleaseGPUBuffer(d, quads_buf_); }
        if (xfer_captured_) { SDL_ReleaseGPUTransferBuffer(d, xfer_captured_); }
        if (xfer_quads_) { SDL_ReleaseGPUTransferBuffer(d, xfer_quads_); }
    }
    seed_pipeline_ = nullptr;
    flood_pipeline_ = nullptr;
    resolve_pipeline_ = nullptr;
    occ_base_pipeline_ = nullptr;
    occ_raster_pipeline_ = nullptr;
    seed_a_ = nullptr;
    seed_b_ = nullptr;
    occ_ss_ = nullptr;
    captured_buf_ = nullptr;
    quads_buf_ = nullptr;
    xfer_captured_ = nullptr;
    xfer_quads_ = nullptr;
    quads_capacity_ = 0;
    sort_order_.clear();
    page_runs_.clear();
    max_sw_ = 0;
    max_sh_ = 0;
    max_tiles_ = 0;
}

// Upload the captured mask (always) and the page-sorted quad array (when there is
// one). Returns true when occ_raster has work to do.
bool gpu_sdf_pass::upload_occluders(
    SDL_GPUCommandBuffer* cb, const occluder_capture& occ, std::uint32_t runtime_w,
    std::uint32_t runtime_h) {
    SDL_GPUDevice* d = dev_->raw();
    const std::uint32_t tiles = runtime_w * runtime_h;
    const auto& mask = occ.captured_mask();
    const auto& quads = occ.quads();
    const auto& pages = occ.pages();

    // The capture is sized to the runtime cache dims, the same dims the shaders index
    // with. A mismatch means cata_tiles has not drawn yet this frame (main menu, or a
    // partial UI redraw); seed nothing so occ_base falls back to TransBuf everywhere.
    const bool mask_ok = tiles > 0 && tiles <= max_tiles_
                         && static_cast<std::uint32_t>(occ.width()) == runtime_w
                         && static_cast<std::uint32_t>(occ.height()) == runtime_h
                         && mask.size() >= tiles;

    // occ_base reads CapturedBuf unconditionally, so it must hold defined bytes even
    // when there is nothing captured — otherwise stale marks would suppress the
    // tile-square fallback and whole regions would stop occluding.
    if (void* mapped = SDL_MapGPUTransferBuffer(d, xfer_captured_, true)) {
        std::uint32_t* dst = static_cast<std::uint32_t*>(mapped);
        if (mask_ok) {
            for (std::uint32_t i = 0; i < tiles; ++i) { dst[i] = mask[i] ? 1u : 0u; }
        } else {
            std::fill_n(dst, tiles, 0u);
        }
        SDL_UnmapGPUTransferBuffer(d, xfer_captured_);
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = xfer_captured_;
        src.offset = 0;
        SDL_GPUBufferRegion dstr{};
        dstr.buffer = captured_buf_;
        dstr.offset = 0;
        dstr.size = tiles * 4u;
        if (SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb)) {
            SDL_UploadToGPUBuffer(cp, &src, &dstr, false);
            SDL_EndGPUCopyPass(cp);
        }
    }

    page_runs_.clear();
    if (!mask_ok || quads.empty() || quads.size() != pages.size()) { return false; }

    // Sort quad indices by atlas page: one compute dispatch reads one texture, so the
    // quads must be contiguous per page. std::stable_sort keeps draw order within a
    // page, which keeps the upload deterministic frame to frame.
    const std::uint32_t n = static_cast<std::uint32_t>(quads.size());
    sort_order_.resize(n);
    std::iota(sort_order_.begin(), sort_order_.end(), 0u);
    std::ranges::stable_sort(sort_order_, [&pages](std::uint32_t a, std::uint32_t b) {
        return pages[a] < pages[b];
    });

    // Grow the quad buffer geometrically so a busy frame does not reallocate yearly.
    if (n > quads_capacity_) {
        std::uint32_t cap = quads_capacity_ ? quads_capacity_ : 1024u;
        while (cap < n) { cap *= 2u; }
        if (quads_buf_) { SDL_ReleaseGPUBuffer(d, quads_buf_); quads_buf_ = nullptr; }
        if (xfer_quads_) { SDL_ReleaseGPUTransferBuffer(d, xfer_quads_); xfer_quads_ = nullptr; }
        const std::uint32_t bytes = cap * static_cast<std::uint32_t>(sizeof(occluder_quad));
        quads_buf_ = create_buffer(bytes, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ);
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = bytes;
        xfer_quads_ = SDL_CreateGPUTransferBuffer(d, &tbci);
        quads_capacity_ = (quads_buf_ && xfer_quads_) ? cap : 0u;
    }
    if (!quads_buf_ || !xfer_quads_) { return false; }

    void* mapped = SDL_MapGPUTransferBuffer(d, xfer_quads_, true);
    if (!mapped) { return false; }
    occluder_quad* dst = static_cast<occluder_quad*>(mapped);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t s = sort_order_[i];
        dst[i] = quads[s];
        const occluder_page& pg = pages[s];
        if (page_runs_.empty() || page_runs_.back().tex != pg.tex) {
            page_runs_.push_back({.tex = pg.tex,
                                  .atlas_w = static_cast<std::uint32_t>(pg.atlas_w),
                                  .atlas_h = static_cast<std::uint32_t>(pg.atlas_h),
                                  .first = i,
                                  .count = 0u});
        }
        ++page_runs_.back().count;
    }
    SDL_UnmapGPUTransferBuffer(d, xfer_quads_);

    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = xfer_quads_;
    src.offset = 0;
    SDL_GPUBufferRegion dstr{};
    dstr.buffer = quads_buf_;
    dstr.offset = 0;
    dstr.size = n * static_cast<std::uint32_t>(sizeof(occluder_quad));
    if (SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb)) {
        SDL_UploadToGPUBuffer(cp, &src, &dstr, false);
        SDL_EndGPUCopyPass(cp);
    }
    return !page_runs_.empty();
}

void gpu_sdf_pass::record(
    SDL_GPUCommandBuffer* cb, SDL_GPUBuffer* trans_buf, SDL_GPUBuffer* target_sdf,
    std::uint32_t runtime_w, std::uint32_t runtime_h, const occluder_capture& occ,
    float occ_soft_gain) {
    if (!ready() || !cb || !trans_buf || !target_sdf || runtime_w == 0 || runtime_h == 0) {
        return;
    }

    const std::uint32_t ss_w = runtime_w * SDF_SS;
    const std::uint32_t ss_h = runtime_h * SDF_SS;

    // Grid dimensions for dispatch (numthreads(8,8,1)).
    const std::uint32_t gx = (ss_w + 7u) / 8u;
    const std::uint32_t gy = (ss_h + 7u) / 8u;

    const bool have_quads = upload_occluders(cb, occ, runtime_w, runtime_h);

    occ_params op{};
    op.map_w = runtime_w;
    op.map_h = runtime_h;
    op.occ_soft_gain = occ_soft_gain;

    // --- OCC BASE pass --- tile-square fallback for tiles nothing was captured for.
    {
        SDL_PushGPUComputeUniformData(cb, /*slot=*/0, &op, sizeof(op));
        SDL_GPUStorageBufferReadWriteBinding rw{};
        rw.buffer = occ_ss_;
        rw.cycle = false;
        SDL_GPUComputePass* p = SDL_BeginGPUComputePass(cb, nullptr, 0, &rw, 1);
        if (!p) { return; }
        SDL_BindGPUComputePipeline(p, occ_base_pipeline_);
        SDL_GPUBuffer* ro[2] = {trans_buf, captured_buf_};
        SDL_BindGPUComputeStorageBuffers(p, /*first_slot=*/0, ro, 2);
        SDL_DispatchGPUCompute(p, gx, gy, 1);
        SDL_EndGPUComputePass(p);
    }

    // --- OCC RASTER pass --- one dispatch per atlas page, one group per quad, one
    // thread per subcell of that quad's tile. Separate passes let SDL_GPU insert the
    // read-after-write barriers; InterlockedMax makes the order irrelevant anyway.
    if (have_quads) {
        for (const page_run& run : page_runs_) {
            if (!run.tex || run.count == 0u) { continue; }
            op.quad_base = run.first;
            op.atlas_w = run.atlas_w;
            op.atlas_h = run.atlas_h;
            SDL_PushGPUComputeUniformData(cb, /*slot=*/0, &op, sizeof(op));
            SDL_GPUStorageBufferReadWriteBinding rw{};
            rw.buffer = occ_ss_;
            rw.cycle = false;
            SDL_GPUComputePass* p = SDL_BeginGPUComputePass(cb, nullptr, 0, &rw, 1);
            if (!p) { break; }
            SDL_BindGPUComputePipeline(p, occ_raster_pipeline_);
            SDL_GPUBuffer* ro[1] = {quads_buf_};
            SDL_BindGPUComputeStorageBuffers(p, /*first_slot=*/0, ro, 1);
            SDL_GPUTexture* rt[1] = {run.tex};
            SDL_BindGPUComputeStorageTextures(p, /*first_slot=*/0, rt, 1);
            SDL_DispatchGPUCompute(p, run.count, 1, 1);
            SDL_EndGPUComputePass(p);
        }
    }

    // --- SEED pass --- coverage -> occupancy via the world-locked Bayer threshold.
    {
        jfa_params params{};
        params.map_w = runtime_w;
        params.map_h = runtime_h;
        params.step = 0.0f;

        SDL_PushGPUComputeUniformData(cb, /*slot=*/0, &params, sizeof(params));
        SDL_GPUStorageBufferReadWriteBinding rw{};
        rw.buffer = seed_a_;
        rw.cycle = false;
        SDL_GPUComputePass* p = SDL_BeginGPUComputePass(cb, nullptr, 0, &rw, 1);
        if (!p) { return; }

        SDL_BindGPUComputePipeline(p, seed_pipeline_);

        SDL_GPUBuffer* ro[1] = {occ_ss_}; // t0 (sub-tile coverage)
        SDL_BindGPUComputeStorageBuffers(p, /*first_slot=*/0, ro, 1);

        SDL_DispatchGPUCompute(p, gx, gy, 1);
        SDL_EndGPUComputePass(p);
    }

    // --- FLOOD pass (ping-pong) ---
    SDL_GPUBuffer* flood_result = seed_a_; // tracks which buffer holds final seed data
    {
        jfa_params params{};
        params.map_w = runtime_w;
        params.map_h = runtime_h;

        // Step schedule: standard JFA — start at the smallest power-of-two that
        // is >= the largest SS-grid dimension, then halve down to 1. This floods
        // the ENTIRE grid (every subcell finds its nearest seed), matching the
        // full-map reach of the deleted CPU Euclidean DT. A fixed start step
        // (SDF_FLOOD_STEP) only reached ~step*2 subcells, so any receiver beyond
        // that radius got the jfa_resolve "no seed" clamp (a constant ~4 tiles)
        // instead of its true distance — which flattened the trace_shadow
        // penumbra into a hard lit/shadow ring at the reach boundary.
        std::uint32_t start_step = 1u;
        while (start_step < std::max(ss_w, ss_h)) { start_step <<= 1u; }
        const int flood_passes = static_cast<int>(std::log2(static_cast<double>(start_step))) + 1;

        SDL_GPUBuffer* seed_read = seed_a_;
        SDL_GPUBuffer* seed_write = seed_b_;

        for (int pass = 0; pass < flood_passes; ++pass) {
            params.step = static_cast<float>(start_step >> static_cast<std::uint32_t>(pass));

            SDL_PushGPUComputeUniformData(cb, /*slot=*/0, &params, sizeof(params));
            SDL_GPUStorageBufferReadWriteBinding rw{};
            rw.buffer = seed_write;
            rw.cycle = false;
            SDL_GPUComputePass* p = SDL_BeginGPUComputePass(cb, nullptr, 0, &rw, 1);
            if (!p) { break; }

            SDL_BindGPUComputePipeline(p, flood_pipeline_);

            SDL_GPUBuffer* ro[1] = {seed_read}; // t0 (input seed buffer)
            SDL_BindGPUComputeStorageBuffers(p, /*first_slot=*/0, ro, 1);

            SDL_DispatchGPUCompute(p, gx, gy, 1);
            SDL_EndGPUComputePass(p);

            // Swap ping-pong buffers.
            std::swap(seed_read, seed_write);
        }

        flood_result = seed_read;
    }

    // --- RESOLVE pass --- writes directly to target_sdf (sdf_storage_)
    {
        jfa_params params{};
        params.map_w = runtime_w;
        params.map_h = runtime_h;
        params.step = 0.0f;

        SDL_PushGPUComputeUniformData(cb, /*slot=*/0, &params, sizeof(params));
        SDL_GPUStorageBufferReadWriteBinding rw{};
        rw.buffer = target_sdf;
        rw.cycle = false;
        SDL_GPUComputePass* p = SDL_BeginGPUComputePass(cb, nullptr, 0, &rw, 1);
        if (!p) { return; }

        SDL_BindGPUComputePipeline(p, resolve_pipeline_);

        // flood_result holds the final seed data after ping-pong swaps.
        SDL_GPUBuffer* ro[1] = {flood_result}; // t0 (seed buffer)
        SDL_BindGPUComputeStorageBuffers(p, /*first_slot=*/0, ro, 1);

        SDL_DispatchGPUCompute(p, gx, gy, 1);
        SDL_EndGPUComputePass(p);
    }
}

} // namespace lighting
