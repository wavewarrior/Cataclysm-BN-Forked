#pragma once

// UI compositor render target.
//
// A persistent offscreen GPU texture the UI (rects + font glyphs) renders into
// once per dirty frame. refresh_display then blits it over the lit-world pass
// on the swapchain. This decouples UI rasterisation from the per-frame
// swapchain clear, which fixes two coupled bugs:
//   1. Transparent UI backdrops — empty cells let the lit world bleed through.
//   2. Partial-redraw flicker — a tooltip-only redraw no longer blanks the
//      rest of the screen, because the compositor texture retains the last
//      composited UI and is only re-rendered when something invalidates it.
//
// The texture format mirrors the swapchain (no conversion on the composite
// blit) and is sized to the PHYSICAL swapchain (drawable) pixels so the blit
// is 1:1; the projection stays logical, matching the existing HiDPI path.
//
// Usage flags:
//   COLOR_TARGET — Pass A renders the UI into it.
//   SAMPLER      — Pass B samples it for the fullscreen composite quad.

#include <SDL3/SDL_gpu.h> // SDL_GPUTextureFormat
#include <cstdint>

struct SDL_GPUTexture;

namespace lighting {

class gpu_device;

class ui_composite_target {
public:
    ui_composite_target() = default;
    ui_composite_target(const ui_composite_target&) = delete;
    ui_composite_target& operator=(const ui_composite_target&) = delete;
    ~ui_composite_target();

    // Allocate the texture at w×h. `format` defaults to the device's
    // swapchain format (SDL_GPU_TEXTUREFORMAT_INVALID resolves to it) so a
    // composite blit needs no conversion; pass an explicit format (e.g.
    // RGBA16F) for an HDR scene target. A re-init releases any prior
    // texture first. Returns false on failure (logs via DC::SDL). Marks the
    // target dirty so the first frame composites.
    bool init(
        gpu_device& dev, int w, int h, SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID,
        // Texture usage flags. 0 = the default COLOR_TARGET|SAMPLER
        // (render into + composite-blit). Pass extra flags (e.g.
        // GRAPHICS_STORAGE_READ for a target a later fragment shader
        // .Load()s, like the silhouette shadow mask). resize() re-uses
        // this so the usage survives.
        SDL_GPUTextureUsageFlags usage = 0);

    // Release the texture. Idempotent. Safe while the device is live.
    void shutdown() noexcept;

    // Recreate at a new size (swapchain / window resize). No-op when the
    // size is unchanged. Marks dirty on a real resize.
    void resize(int w, int h);

    SDL_GPUTexture* texture() const noexcept { return tex_; }
    std::uint32_t width() const noexcept { return w_; }
    std::uint32_t height() const noexcept { return h_; }
    SDL_GPUTextureFormat format() const noexcept { return fmt_; }

    // One-shot dirty flag. invalidate() requests a recomposite;
    // consume_dirty() returns the pending state and clears it.
    void invalidate() noexcept { dirty_ = true; }
    bool consume_dirty() noexcept {
        const bool d = dirty_;
        dirty_ = false;
        return d;
    }

private:
    gpu_device* dev_ = nullptr;
    SDL_GPUTexture* tex_ = nullptr;
    std::uint32_t w_ = 0;
    std::uint32_t h_ = 0;
    // Resolved texture format (swapchain format unless an explicit one was
    // passed to init). resize() re-uses this so the format survives.
    SDL_GPUTextureFormat fmt_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    // Resolved usage flags (default COLOR_TARGET|SAMPLER). resize() re-uses
    // this so storage-read targets keep their usage across a window resize.
    SDL_GPUTextureUsageFlags usage_ =
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    // First frame must composite; init() also sets this.
    bool dirty_ = true;
};

} // namespace lighting
