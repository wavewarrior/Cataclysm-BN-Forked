#pragma once

// Solid-color geometry primitives (rectangles, lines) — phase 2g of the
// lighting rework. Mirrors the legacy `GeometryRenderer` interface in
// src/sdl_geometry.h but writes through the SDL_GPU sprite_batcher instead
// of `SDL_RenderFillRect` / color-modulated textures.
//
// Implementation reuses the existing sprite pipeline: every rect / line is
// pushed as a `sprite_instance` whose texture is a 1×1 fully-opaque white
// pixel. The fragment shader's `texel * tint` then degenerates to
// `white * tint = tint`, giving us a flat-colour fill for the cost of a
// single batched draw — no separate pipeline or shader needed.
//
// Inert until sub-phase 2i.

#include "gpu_device.h"
#include "sprite_batcher.h"

#include <cstdint>
#include <memory>

namespace lighting {

class gpu_geometry_impl;

class gpu_geometry {
public:
    gpu_geometry();
    gpu_geometry(const gpu_geometry&) = delete;
    gpu_geometry& operator=(const gpu_geometry&) = delete;
    gpu_geometry(gpu_geometry&&) noexcept;
    gpu_geometry& operator=(gpu_geometry&&) noexcept;
    ~gpu_geometry();

    // Build the 1×1 white texture + sampler. The caller's
    // sprite_batcher is borrowed (not owned).
    void init(gpu_device& dev);
    void shutdown() noexcept;

    // SDL_GPUTexture handle suitable for sprite_batcher::set_texture
    // so callers can interleave geometry + sprite draws on the same
    // batcher (one segment with the white texture, one with the atlas
    // page, etc.).
    SDL_GPUTexture* white_texture() const noexcept;

    // Convenience emitters — append a sprite_instance to `dst` rather
    // than driving the batcher directly so callers can group all
    // geometry under one set_texture(white_texture) call.
    void rect(sprite_batcher& dst, float x, float y, float w, float h, const float rgba[4]) const;
    void horizontal_line(
        sprite_batcher& dst, float x, float y, float x2, float thickness,
        const float rgba[4]) const;
    void vertical_line(
        sprite_batcher& dst, float x, float y, float y2, float thickness,
        const float rgba[4]) const;

private:
    std::unique_ptr<gpu_geometry_impl> p;
};

} // namespace lighting
