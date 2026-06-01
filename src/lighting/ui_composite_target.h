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

#include <cstdint>

struct SDL_GPUTexture;

namespace lighting
{

class gpu_device;

class ui_composite_target
{
    public:
        ui_composite_target() = default;
        ui_composite_target( const ui_composite_target & ) = delete;
        ui_composite_target &operator=( const ui_composite_target & ) = delete;
        ~ui_composite_target();

        // Allocate the texture at w×h in the device's swapchain format. A
        // re-init releases any prior texture first. Returns false on failure
        // (logs via DC::SDL). Marks the target dirty so the first frame
        // composites.
        bool init( gpu_device &dev, int w, int h );

        // Release the texture. Idempotent. Safe while the device is live.
        void shutdown() noexcept;

        // Recreate at a new size (swapchain / window resize). No-op when the
        // size is unchanged. Marks dirty on a real resize.
        void resize( int w, int h );

        SDL_GPUTexture *texture() const noexcept { return tex_; }
        std::uint32_t   width()  const noexcept { return w_; }
        std::uint32_t   height() const noexcept { return h_; }

        // One-shot dirty flag. invalidate() requests a recomposite;
        // consume_dirty() returns the pending state and clears it.
        void invalidate() noexcept { dirty_ = true; }
        bool consume_dirty() noexcept
        {
            const bool d = dirty_;
            dirty_ = false;
            return d;
        }

    private:
        gpu_device     *dev_   = nullptr;
        SDL_GPUTexture *tex_   = nullptr;
        std::uint32_t   w_     = 0;
        std::uint32_t   h_     = 0;
        // First frame must composite; init() also sets this.
        bool            dirty_ = true;
};

} // namespace lighting
