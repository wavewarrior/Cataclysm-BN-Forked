#pragma once

// Box2D debug-overlay line/point pass — GPU line-draw pipeline for diagnostic
// wireframe overlays.  Renders colored line segments over the world target.
//
// Designed for the b2DebugDraw callback family: DrawSegment, DrawPolygon,
// DrawCircle, etc.  Callbacks push line segments in world-tile coordinates via
// add_line(); record() uploads them to a GPU structured buffer and issues a
// single instanced line-list draw.  clear() resets the buffer each frame.
//
// Follows the hud_particle_effect / rain_effect pattern: structured-buffer
// instanced draw, no vertex buffer, vertex shader reads from storage.

#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <vector>

namespace lighting {

class gpu_device;

/// GPU-side per-line-segment instance (matches debug_line.vert.hlsl LineSegment).
/// All scalar floats to avoid HLSL alignment pitfalls.
struct debug_line_vertex {
    float ax, ay;         ///< start point (world tile coords)
    float bx, by;         ///< end point (world tile coords)
    float r, g, b, a;     ///< RGBA colour
};
static_assert( sizeof( debug_line_vertex ) == 32 );

class debug_line_pass {
public:
    debug_line_pass() = default;
    debug_line_pass( const debug_line_pass & ) = delete;
    debug_line_pass &operator=( const debug_line_pass & ) = delete;
    ~debug_line_pass();

    /// Build GPU pipeline + instance buffers.
    auto init( gpu_device &dev, SDL_GPUTextureFormat target_format ) -> bool;

    auto shutdown() noexcept -> void;

    /// True when initialized and GPU resources are live.
    auto ready() const noexcept -> bool {
        return dev_ != nullptr && pipeline_ != nullptr
            && xfer_ != nullptr && storage_ != nullptr;
    }

    /// Buffer a line segment (world-tile coords, RGBA colour).
    auto add_line( float x0, float y0, float x1, float y1,
                   float r, float g, float b, float a ) -> void;

    /// Buffer a point as a small cross (world-tile coords).
    auto add_point( float x, float y, float r, float g, float b, float a ) -> void;

    /// Upload buffered lines and draw them onto @p target.
    /// Camera params convert world-tile to NDC:
    ///   pixel = (tile - cam) * tile_px
    ///   ndc   = pixel / (target * 0.5) - 1.0
    auto record( SDL_GPUCommandBuffer *cb, SDL_GPUTexture *target,
                 std::uint32_t target_w, std::uint32_t target_h,
                 float cam_x, float cam_y,
                 float tile_w, float tile_h ) -> void;

    /// Reset the per-frame line buffer.
    auto clear() noexcept -> void;

    /// Number of lines buffered this frame.
    auto count() const noexcept -> std::size_t { return lines_.size(); }

private:
    auto upload( SDL_GPUCommandBuffer *cb ) -> bool;

    gpu_device *dev_ = nullptr;

    SDL_GPUShader *vert_ = nullptr;
    SDL_GPUShader *frag_ = nullptr;
    SDL_GPUGraphicsPipeline *pipeline_ = nullptr;

    SDL_GPUTransferBuffer *xfer_ = nullptr;
    SDL_GPUBuffer *storage_ = nullptr;

    static constexpr int MAX_LINES = 8192;
    std::vector<debug_line_vertex> lines_;
};

} // namespace lighting
