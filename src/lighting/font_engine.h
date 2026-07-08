#pragma once

// SDL_ttf GPU text engine wrapper — phase 2f of lighting rework.
//
// Wraps `TTF_CreateGPUTextEngine` + draw of `TTF_Text` so callers don't have
// to talk SDL_GPU directly when laying down UI / overmap / message log
// glyphs. Owns its own graphics pipeline and per-frame vertex/index buffer
// ring.
//
// Image-type coverage:
//   ALPHA      — supported (the common path; standard TTF_RenderText output).
//   COLOR      — emoji / colored glyphs.  Deferred to phase 2f2; current
//                impl logs a warning + skips the sequence.
//   SDF        — distance-field rendering for the future bloom pass. Same
//                deferral as COLOR.
//
// Inert: no caller until sub-phase 2i cutover.

#include "gpu_device.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <cstdint>
#include <memory>

namespace lighting {

class font_engine_impl;

class font_engine {
public:
    font_engine();
    font_engine(const font_engine&) = delete;
    font_engine& operator=(const font_engine&) = delete;
    font_engine(font_engine&&) noexcept;
    font_engine& operator=(font_engine&&) noexcept;
    ~font_engine();

    // Set up the engine. `target_format` must match the swapchain (or
    // offscreen RT) the text will be drawn into.
    void init(gpu_device& dev, SDL_GPUTextureFormat target_format);
    void shutdown() noexcept;

    // Raw TTF engine handle for `TTF_CreateText`. Callers manage the
    // TTF_Text lifetime — destroy all texts before this engine is
    // shut down.
    TTF_TextEngine* raw() const noexcept;

    // Draw a TTF_Text at pixel-space (x, y) into the bound render
    // target. Opens its own render pass (LOAD_OP_LOAD so existing
    // pixels are preserved), draws, closes.
    //
    // `rgba` is multiplied with the alpha glyph at fragment time.
    void draw_text(
        SDL_GPUCommandBuffer* cb, SDL_GPUTexture* target, std::uint32_t target_w,
        std::uint32_t target_h, TTF_Text* text, float x, float y, const float rgba[4]);

    // Per-frame: advances the vertex/index buffer ring.
    void begin_frame();

private:
    std::unique_ptr<font_engine_impl> p;
};

} // namespace lighting
