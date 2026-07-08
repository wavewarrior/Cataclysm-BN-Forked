#include "lighting/ui_composite_target.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "sdl_wrappers.h"

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

ui_composite_target::~ui_composite_target() { shutdown(); }

bool ui_composite_target::init(
    gpu_device& dev, int w, int h, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage) {
    shutdown();

    dev_ = &dev;
    if (!dev.ready() || w <= 0 || h <= 0) {
        dbg(DL::Warn) << "ui_composite_target::init: device not ready or bad size " << w << "x" << h;
        return false;
    }

    // INVALID resolves to the swapchain format (the default: a composite blit
    // then needs no conversion). An explicit format (e.g. RGBA16F) gives an HDR
    // scene target. resize() re-calls init() with fmt_ so the format sticks.
    fmt_ = (format == SDL_GPU_TEXTUREFORMAT_INVALID) ? dev.swapchain_format() : format;
    // 0 keeps the default COLOR_TARGET|SAMPLER. resize() re-uses usage_.
    usage_ =
        (usage == 0) ? (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER) : usage;

    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = fmt_;
    tci.usage = usage_;
    tci.width = static_cast<std::uint32_t>(w);
    tci.height = static_cast<std::uint32_t>(h);
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;

    tex_ = SDL_CreateGPUTexture(dev.raw(), &tci);
    if (!tex_) {
        dbg(DL::Warn) << "ui_composite_target::init: SDL_CreateGPUTexture: " << SDL_GetError();
        return false;
    }
    SDL_SetGPUTextureName(dev.raw(), tex_, "ui_composite_target");

    w_ = static_cast<std::uint32_t>(w);
    h_ = static_cast<std::uint32_t>(h);
    dirty_ = true;
    return true;
}

void ui_composite_target::shutdown() noexcept {
    if (tex_ && dev_ && dev_->ready()) { SDL_ReleaseGPUTexture(dev_->raw(), tex_); }
    tex_ = nullptr;
    w_ = 0;
    h_ = 0;
}

void ui_composite_target::resize(int w, int h) {
    if (w <= 0 || h <= 0) { return; }
    if (static_cast<std::uint32_t>(w) == w_ && static_cast<std::uint32_t>(h) == h_ && tex_) {
        return;
    }
    if (!dev_) { return; }
    init(*dev_, w, h, fmt_, usage_);
}

} // namespace lighting
