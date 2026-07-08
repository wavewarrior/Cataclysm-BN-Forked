#include "gpu_geometry.h"

#include "debug.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

class gpu_geometry_impl {
public:
    gpu_device* dev = nullptr;
    SDL_GPUTexture* white = nullptr;

    void init(gpu_device& d) {
        if (!d.ready()) { throw std::runtime_error("gpu_geometry::init: gpu_device not ready"); }
        dev = &d;

        SDL_GPUTextureCreateInfo tci{};
        tci.type = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        tci.width = 1;
        tci.height = 1;
        tci.layer_count_or_depth = 1;
        tci.num_levels = 1;
        tci.sample_count = SDL_GPU_SAMPLECOUNT_1;

        white = SDL_CreateGPUTexture(d.raw(), &tci);
        if (!white) {
            throw std::runtime_error(
                std::string("gpu_geometry: create white tex: ") + SDL_GetError());
        }

        // Upload a single 0xFFFFFFFF pixel via a one-shot transfer.
        SDL_GPUTransferBufferCreateInfo tbi{};
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbi.size = 4;
        SDL_GPUTransferBuffer* xfer = SDL_CreateGPUTransferBuffer(d.raw(), &tbi);
        if (!xfer) {
            throw std::runtime_error(std::string("gpu_geometry: xfer alloc: ") + SDL_GetError());
        }
        void* mapped = SDL_MapGPUTransferBuffer(d.raw(), xfer, false);
        if (!mapped) {
            SDL_ReleaseGPUTransferBuffer(d.raw(), xfer);
            throw std::runtime_error("gpu_geometry: map xfer");
        }
        const std::uint32_t white_pixel = 0xFFFFFFFFu;
        std::memcpy(mapped, &white_pixel, 4);
        SDL_UnmapGPUTransferBuffer(d.raw(), xfer);

        // Need a one-off command buffer to dispatch the upload.
        SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(d.raw());
        if (!cb) {
            SDL_ReleaseGPUTransferBuffer(d.raw(), xfer);
            throw std::runtime_error("gpu_geometry: acquire cb");
        }
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
        SDL_GPUTextureTransferInfo ti{};
        ti.transfer_buffer = xfer;
        SDL_GPUTextureRegion region{};
        region.texture = white;
        region.w = 1;
        region.h = 1;
        region.d = 1;
        SDL_UploadToGPUTexture(cp, &ti, &region, false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(cb);

        SDL_ReleaseGPUTransferBuffer(d.raw(), xfer);

        dbg(DL::Info) << "gpu_geometry initialised (1x1 white tex).";
    }

    void shutdown() noexcept {
        if (!dev) { return; }
        if (white) {
            SDL_ReleaseGPUTexture(dev->raw(), white);
            white = nullptr;
        }
        dev = nullptr;
    }
};

// ---- trampolines -----------------------------------------------------

gpu_geometry::gpu_geometry(): p(std::make_unique<gpu_geometry_impl>()) {}
gpu_geometry::gpu_geometry(gpu_geometry&&) noexcept = default;
gpu_geometry& gpu_geometry::operator=(gpu_geometry&&) noexcept = default;
gpu_geometry::~gpu_geometry() {
    if (p) { p->shutdown(); }
}

void gpu_geometry::init(gpu_device& d) { p->init(d); }
void gpu_geometry::shutdown() noexcept {
    if (p) { p->shutdown(); }
}
SDL_GPUTexture* gpu_geometry::white_texture() const noexcept { return p ? p->white : nullptr; }

void gpu_geometry::rect(
    sprite_batcher& dst, float x, float y, float w, float h, const float rgba[4]) const {
    sprite_instance s{};
    s.dst_x = x;
    s.dst_y = y;
    s.dst_w = w;
    s.dst_h = h;
    // Sample the centre of the 1×1 white tex — flat colour everywhere.
    s.src_u = 0.5f;
    s.src_v = 0.5f;
    s.src_uw = 0.0f;
    s.src_vh = 0.0f;
    s.tint_r = rgba[0];
    s.tint_g = rgba[1];
    s.tint_b = rgba[2];
    s.tint_a = rgba[3];
    dst.draw(s);
}

void gpu_geometry::horizontal_line(
    sprite_batcher& dst, float x, float y, float x2, float thickness, const float rgba[4]) const {
    if (x2 < x) { std::swap(x, x2); }
    rect(dst, x, y, x2 - x, std::max(1.0f, thickness), rgba);
}

void gpu_geometry::vertical_line(
    sprite_batcher& dst, float x, float y, float y2, float thickness, const float rgba[4]) const {
    if (y2 < y) { std::swap(y, y2); }
    rect(dst, x, y, std::max(1.0f, thickness), y2 - y, rgba);
}

} // namespace lighting
