#include "lighting/emitter_collector.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/render_state.h"
#include "lighting/sdf_pass.h"
#include "sdl_wrappers.h"

#include <cstring>

#define dbg(x) DebugLogFL((x), DC::SDL) // SDL3 headers

namespace lighting {

// MAX_EMITTERS comes from gpu_emitter.h (8192). The storage buffer is
// sized for that many entries up-front so the handle stays stable.
static constexpr Uint32 EMITTER_BUF_BYTES =
    static_cast<Uint32>(MAX_EMITTERS) * static_cast<Uint32>(sizeof(gpu_emitter));

emitter_collector::emitter_collector(render_state& rs): rs_(rs) {
    SDL_GPUDevice* dev = rs_.device().raw();

    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = EMITTER_BUF_BYTES;
        xfer_ = SDL_CreateGPUTransferBuffer(dev, &tbci);
        if (!xfer_) { dbg(DL::Error) << "emitter_collector: failed to create transfer buffer"; }
    }

    {
        // GRAPHICS_STORAGE_READ — fragment-stage StructuredBuffer access.
        // Bound via SDL_BindGPUFragmentStorageBuffers (slot 0); declared in
        // HLSL as StructuredBuffer<GpuEmitter> at register(t1, space2) — after
        // the only sampled texture Atlas(t0). SdfBuf (slot 1 → t2) and
        // SkyVisBuf (slot 2 → t3) are the other fragment storage buffers.
        SDL_GPUBufferCreateInfo bci{};
        // GRAPHICS read (sprite.frag Emitters) + COMPUTE read (gi_field.comp
        // gathers the same emitter data on the GPU compute GI path).
        bci.usage =
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
        bci.size = EMITTER_BUF_BYTES;
        emitter_buf_ = SDL_CreateGPUBuffer(dev, &bci);
        if (!emitter_buf_) {
            dbg(DL::Error) << "emitter_collector: failed to create emitter buffer";
        }
    }
}

emitter_collector::~emitter_collector() {
    SDL_GPUDevice* dev = rs_.device().raw();
    if (xfer_) {
        SDL_ReleaseGPUTransferBuffer(dev, xfer_);
        xfer_ = nullptr;
    }
    if (emitter_buf_) {
        SDL_ReleaseGPUBuffer(dev, emitter_buf_);
        emitter_buf_ = nullptr;
    }
}

void emitter_collector::submit(
    std::vector<gpu_emitter> snapshot, std::vector<uint8_t> transparency, std::vector<float> sdf,
    std::vector<uint8_t> sky_vis, int runtime_w, int runtime_h, std::vector<float> occ,
    std::vector<float> albedo) {
    pending_ = std::move(snapshot);
    pending_transparency_ = std::move(transparency);
    pending_sdf_ = std::move(sdf);
    pending_occ_ = std::move(occ);
    pending_albedo_ = std::move(albedo);
    pending_sky_vis_ = std::move(sky_vis);
    pending_runtime_w_ = runtime_w;
    pending_runtime_h_ = runtime_h;
    have_pending_ = true;
}

void emitter_collector::flush_to_render_cb(SDL_GPUCommandBuffer* cb) {
    if (!cb || !rs_.device().raw()) { return; }

    if (!have_pending_) { return; }
    std::vector<gpu_emitter> data = std::move(pending_);
    std::vector<uint8_t> transparency = std::move(pending_transparency_);
    std::vector<float> occ = std::move(pending_occ_);
    std::vector<float> albedo = std::move(pending_albedo_);
    std::vector<float> sdf = std::move(pending_sdf_);
    std::vector<uint8_t> sky_vis = std::move(pending_sky_vis_);
    const int runtime_w = pending_runtime_w_;
    const int runtime_h = pending_runtime_h_;
    pending_runtime_w_ = 0;
    pending_runtime_h_ = 0;
    have_pending_ = false;

    if (!xfer_ || !emitter_buf_) { return; }

    const int count = std::min(static_cast<int>(data.size()), static_cast<int>(MAX_EMITTERS));
    const Uint32 byte_size = static_cast<Uint32>(count) * static_cast<Uint32>(sizeof(gpu_emitter));

    // cycle=true on map: SDL_GPU rotates internal staging if last
    // frame's upload is still in flight.
    void* mapped = SDL_MapGPUTransferBuffer(rs_.device().raw(), xfer_, /*cycle=*/true);
    if (!mapped) {
        dbg(DL::Error) << "emitter_collector: SDL_MapGPUTransferBuffer failed";
        return;
    }
    if (!data.empty()) {
        const gpu_emitter& e0 = data[0];
        dbg(DL::Debug) << "emitter[0]: pos=(" << e0.pos_x << "," << e0.pos_y << "," << e0.pos_z
                       << ") r=" << e0.radius << " rgb=(" << e0.r << "," << e0.g << "," << e0.b
                       << ")";
    }
    if (byte_size > 0) { std::memcpy(mapped, data.data(), byte_size); }
    SDL_UnmapGPUTransferBuffer(rs_.device().raw(), xfer_);

    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    if (!cp) { return; }

    if (count > 0) {
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = xfer_;
        src.offset = 0;

        SDL_GPUBufferRegion dst{};
        dst.buffer = emitter_buf_;
        dst.offset = 0;
        dst.size = byte_size;

        // cycle=true: SDL_GPU orphans the previous physical resource if
        // the GPU is still consuming last frame's data, allocates a new
        // one, and ensures subsequent same-CB bindings see the new
        // resource. Same-CB ordering means the upload→sample barrier is
        // emitted automatically before the render pass.
        SDL_UploadToGPUBuffer(cp, &src, &dst, /*cycle=*/true);
    }

    // Phase 4/8: upload transparency + sky_vis + occ in the same copy pass.
    // Runtime W/H come from the submitter (sdltiles.cpp) so the upload region
    // matches the live mapsize, even if the GPU buffer is over-allocated.
    // P3.3: do NOT gate on `!sdf.empty()` — the CPU SDF vector is now always {}
    // (JFA writes sdf_storage_ on the GPU). transparency feeds the JFA seed via
    // trans_storage_, so transparency presence is the real upload trigger.
    if (rs_.sdf().ready() && !transparency.empty() && runtime_w > 0 && runtime_h > 0) {
        rs_.sdf().upload(
            cp, rs_.device().raw(), runtime_w, runtime_h, transparency, sdf, sky_vis, occ,
            albedo);
    }

    SDL_EndGPUCopyPass(cp);

    last_count_.store(count, std::memory_order_relaxed);
}

} // namespace lighting
