#include "gpu_atlas.h"

#include "debug.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

namespace {

// Same stripe heuristic dynamic_atlas uses. Rounds heights to `min_size`
// granularity, keeping rows of equal-height sprites packed dense.
template <typename T> T round_up(T n, T m) { return (m == 0) ? n : ((n + m - 1) / m) * m; }

struct stripe {
    std::uint32_t height;
    std::uint32_t y_offset;
    std::uint32_t x_remainder;
};

struct stripe_packer {
    int w = 0;
    int h = 0;
    std::uint32_t min_size = 0;
    std::uint32_t y_remainder = 0;
    std::vector<stripe> stripes;

    stripe_packer() = default;
    stripe_packer(int W, int H, std::uint32_t min): w(W), h(H), min_size(min), y_remainder(H) {}

    std::optional<SDL_Rect> pack(std::uint32_t pw, std::uint32_t ph) {
        if (static_cast<int>(pw) > w || static_cast<int>(ph) > h) { return std::nullopt; }
        const std::uint32_t rh = round_up(ph, min_size);

        auto it = std::find_if(stripes.begin(), stripes.end(), [&](const stripe& s) {
            return s.height == rh && s.x_remainder >= pw;
        });

        if (it == stripes.end()) {
            if (rh > y_remainder || y_remainder < min_size) { return std::nullopt; }
            it = stripes.insert(
                stripes.end(),
                stripe{rh, static_cast<std::uint32_t>(h) - y_remainder,
                       static_cast<std::uint32_t>(w)});
            y_remainder -= rh;
        }

        const std::uint32_t x_off = static_cast<std::uint32_t>(w) - it->x_remainder;
        SDL_Rect r{static_cast<int>(x_off), static_cast<int>(it->y_offset), static_cast<int>(pw),
                   static_cast<int>(ph)};
        it->x_remainder -= pw;
        if (it->x_remainder < min_size) { it->x_remainder = 0; }
        return r;
    }
};

} // namespace

// ---- PIMPL ------------------------------------------------------------

class gpu_atlas_impl {
public:
    gpu_device* dev = nullptr;
    int page_w = 0;
    int page_h = 0;
    int min_w = 0;
    int min_h = 0;

    struct page {
        SDL_GPUTexture* tex = nullptr;
        stripe_packer packer;
    };
    std::vector<page> pages;

    std::unordered_map<std::size_t, gpu_atlas_slot> cache;

    // Persistent transfer buffer reused across uploads — sized to one
    // page worth so even a full-atlas refresh fits a single map+copy
    // round trip.
    SDL_GPUTransferBuffer* xfer = nullptr;
    std::uint32_t xfer_capacity = 0;

    gpu_atlas_impl(int pw, int ph, int mw, int mh): page_w(pw), page_h(ph), min_w(mw), min_h(mh) {}

    ~gpu_atlas_impl() { shutdown(); }

    void init(gpu_device& d) { dev = &d; }

    void shutdown() noexcept {
        if (!dev) { return; }
        SDL_GPUDevice* r = dev->raw();
        for (page& p : pages) {
            if (p.tex) {
                SDL_ReleaseGPUTexture(r, p.tex);
                p.tex = nullptr;
            }
        }
        pages.clear();
        cache.clear();
        if (xfer) {
            SDL_ReleaseGPUTransferBuffer(r, xfer);
            xfer = nullptr;
            xfer_capacity = 0;
        }
        dev = nullptr;
    }

    void clear_pages() {
        if (!dev) { return; }
        SDL_GPUDevice* r = dev->raw();
        for (page& p : pages) {
            if (p.tex) { SDL_ReleaseGPUTexture(r, p.tex); }
        }
        pages.clear();
        cache.clear();
    }

    // ---- allocation -----------------------------------------------

    gpu_atlas_slot allocate(int w, int h) {
        if (!dev) { return {}; }
        // Try existing pages first.
        for (std::size_t i = 0; i < pages.size(); ++i) {
            if (auto r = pages[i].packer.pack(w, h)) { return make_slot(i, *r); }
        }
        // Need a new page.
        page np{};
        np.packer = stripe_packer(page_w, page_h, min_w);

        SDL_GPUTextureCreateInfo tci{};
        tci.type = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        // COMPUTE_STORAGE_READ so occ_raster.comp can Texture2D.Load sprite alpha
        // when rasterising the SDF seed (grid-decoupled lighting, Step 3).
        tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ;
        tci.width = static_cast<Uint32>(page_w);
        tci.height = static_cast<Uint32>(page_h);
        tci.layer_count_or_depth = 1;
        tci.num_levels = 1;
        tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        tci.props = 0;

        np.tex = SDL_CreateGPUTexture(dev->raw(), &tci);
        if (!np.tex) {
            dbg(DL::Error) << "gpu_atlas: CreateGPUTexture failed: " << SDL_GetError();
            return {};
        }
        pages.push_back(std::move(np));

        const std::size_t idx = pages.size() - 1;
        auto r = pages[idx].packer.pack(w, h);
        if (!r) {
            // Should be impossible — sprite is smaller than the page and
            // the page is empty. Belt + suspenders.
            dbg(DL::Error) << "gpu_atlas: fresh page rejected " << w << "x" << h;
            return {};
        }
        return make_slot(idx, *r);
    }

    // ---- upload ---------------------------------------------------

    bool upload_surface(SDL_GPUCommandBuffer* cb, const gpu_atlas_slot& slot, SDL_Surface* surf) {
        if (!dev || !cb || !slot || !surf) { return false; }
        if (slot.page_index >= pages.size() || pages[slot.page_index].tex != slot.page) {
            dbg(DL::Error) << "gpu_atlas::upload_surface: slot/page mismatch";
            return false;
        }
        // Convert if needed — sprite_batcher expects RGBA8.
        SDL_Surface* src = surf;
        SDL_Surface* converted = nullptr;
        if (surf->format != SDL_PIXELFORMAT_RGBA32 && surf->format != SDL_PIXELFORMAT_ABGR8888) {
            converted = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
            if (!converted) {
                dbg(DL::Error) << "gpu_atlas: ConvertSurface failed: " << SDL_GetError();
                return false;
            }
            src = converted;
        }

        const std::uint32_t bytes_per_row = static_cast<std::uint32_t>(slot.px_w) * 4;
        const std::uint32_t needed = bytes_per_row * slot.px_h;
        if (!ensure_xfer_capacity(needed)) {
            if (converted) { SDL_DestroySurface(converted); }
            return false;
        }

        void* mapped = SDL_MapGPUTransferBuffer(dev->raw(), xfer, /*cycle=*/true);
        if (!mapped) {
            dbg(DL::Error) << "gpu_atlas: MapGPUTransferBuffer failed: " << SDL_GetError();
            if (converted) { SDL_DestroySurface(converted); }
            return false;
        }
        // Pack source rows tight — SDL_Surfaces commonly have pitch > w*4.
        auto* dstp = static_cast<unsigned char*>(mapped);
        const auto* srcp = static_cast<const unsigned char*>(src->pixels);
        for (int row = 0; row < slot.px_h; ++row) {
            std::memcpy(dstp, srcp, bytes_per_row);
            dstp += bytes_per_row;
            srcp += src->pitch;
        }
        SDL_UnmapGPUTransferBuffer(dev->raw(), xfer);

        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
        if (!cp) {
            dbg(DL::Error) << "gpu_atlas: BeginGPUCopyPass failed: " << SDL_GetError();
            if (converted) { SDL_DestroySurface(converted); }
            return false;
        }

        SDL_GPUTextureTransferInfo info{};
        info.transfer_buffer = xfer;
        info.offset = 0;
        // 0 rows_per_layer + 0 pixels_per_row → tightly packed, which is
        // what we wrote.
        info.pixels_per_row = 0;
        info.rows_per_layer = 0;

        SDL_GPUTextureRegion region{};
        region.texture = slot.page;
        region.mip_level = 0;
        region.layer = 0;
        region.x = slot.px_x;
        region.y = slot.px_y;
        region.z = 0;
        region.w = slot.px_w;
        region.h = slot.px_h;
        region.d = 1;

        SDL_UploadToGPUTexture(cp, &info, &region, /*cycle=*/false);
        SDL_EndGPUCopyPass(cp);

        if (converted) { SDL_DestroySurface(converted); }
        return true;
    }

private:
    gpu_atlas_slot make_slot(std::size_t page_idx, const SDL_Rect& r) const {
        gpu_atlas_slot s{};
        s.page = pages[page_idx].tex;
        s.page_index = static_cast<std::uint16_t>(page_idx);
        s.px_x = static_cast<std::uint16_t>(r.x);
        s.px_y = static_cast<std::uint16_t>(r.y);
        s.px_w = static_cast<std::uint16_t>(r.w);
        s.px_h = static_cast<std::uint16_t>(r.h);
        s.u = static_cast<float>(r.x) / static_cast<float>(page_w);
        s.v = static_cast<float>(r.y) / static_cast<float>(page_h);
        s.uw = static_cast<float>(r.w) / static_cast<float>(page_w);
        s.vh = static_cast<float>(r.h) / static_cast<float>(page_h);
        return s;
    }

    bool ensure_xfer_capacity(std::uint32_t needed) {
        if (xfer && xfer_capacity >= needed) { return true; }
        if (xfer) {
            SDL_ReleaseGPUTransferBuffer(dev->raw(), xfer);
            xfer = nullptr;
            xfer_capacity = 0;
        }
        // Round up to 1 MiB so we don't churn allocations on every
        // tileset reload.
        const std::uint32_t round = (needed + (1u << 20) - 1) & ~((1u << 20) - 1);

        SDL_GPUTransferBufferCreateInfo tci{};
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size = round;
        tci.props = 0;
        xfer = SDL_CreateGPUTransferBuffer(dev->raw(), &tci);
        if (!xfer) {
            dbg(DL::Error) << "gpu_atlas: CreateGPUTransferBuffer failed: " << SDL_GetError();
            return false;
        }
        xfer_capacity = round;
        return true;
    }
};

// ---- gpu_atlas trampolines --------------------------------------------

gpu_atlas::gpu_atlas(int pw, int ph, int mw, int mh)
    : p(std::make_unique<gpu_atlas_impl>(pw, ph, mw, mh)) {}
gpu_atlas::gpu_atlas(gpu_atlas&&) noexcept = default;
gpu_atlas& gpu_atlas::operator=(gpu_atlas&&) noexcept = default;
gpu_atlas::~gpu_atlas() = default;

void gpu_atlas::init(gpu_device& dev) { p->init(dev); }
void gpu_atlas::shutdown() noexcept {
    if (p) { p->shutdown(); }
}

gpu_atlas_slot gpu_atlas::allocate(int w, int h) { return p->allocate(w, h); }

gpu_atlas_slot gpu_atlas::upload(SDL_GPUCommandBuffer* cb, SDL_Surface* surf) {
    if (!surf) { return {}; }
    gpu_atlas_slot s = p->allocate(surf->w, surf->h);
    if (!s) { return s; }
    if (!p->upload_surface(cb, s, surf)) { return {}; }
    return s;
}

bool gpu_atlas::upload_surface(
    SDL_GPUCommandBuffer* cb, const gpu_atlas_slot& slot, SDL_Surface* surf) {
    return p->upload_surface(cb, slot, surf);
}

bool gpu_atlas::cache_assign(std::size_t id, const gpu_atlas_slot& slot) {
    return p->cache.emplace(id, slot).second;
}

const gpu_atlas_slot* gpu_atlas::cache_lookup(std::size_t id) const noexcept {
    const auto it = p->cache.find(id);
    return (it == p->cache.end()) ? nullptr : &it->second;
}

void gpu_atlas::clear() {
    if (p) { p->clear_pages(); }
}

std::size_t gpu_atlas::page_count() const noexcept { return p ? p->pages.size() : 0; }

} // namespace lighting
