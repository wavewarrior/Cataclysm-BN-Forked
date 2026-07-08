#pragma once

// SDL_GPU device wrapper — phase 2a of lighting rework.
// Owns the SDL_GPUDevice, the window-swapchain claim, and per-frame command
// buffer / swapchain-texture acquisition. Inert in this commit — no caller in
// the engine. Wired into sdltiles.cpp WinCreate in sub-phase 2i (cutover).

#include "sdl_wrappers.h"

#include <memory>

namespace lighting {

struct gpu_device_deleter {
    void operator()(SDL_GPUDevice* device) const noexcept;
};
using gpu_device_ptr = std::unique_ptr<SDL_GPUDevice, gpu_device_deleter>;

// Frame begin/end pair returned by gpu_device::begin_frame().
// `cmd_buffer` is non-null iff acquire succeeded; `swapchain_tex` may still be
// null when the window is minimised (skip drawing, still submit).
struct frame_context {
    SDL_GPUCommandBuffer* cmd_buffer = nullptr;
    SDL_GPUTexture* swapchain_tex = nullptr;
    Uint32 swapchain_w = 0;
    Uint32 swapchain_h = 0;
    bool valid() const noexcept { return cmd_buffer != nullptr; }
};

class gpu_device {
public:
    gpu_device() = default;
    gpu_device(const gpu_device&) = delete;
    gpu_device& operator=(const gpu_device&) = delete;
    gpu_device(gpu_device&&) = default;
    gpu_device& operator=(gpu_device&&) = default;
    ~gpu_device();

    // Create device + claim window. `debug` enables validation/labels.
    // Throws std::runtime_error on failure; the only sensible recovery is
    // exit because we have no SDL_Renderer fallback after the phase 2
    // cutover.
    void init(SDL_Window* window, bool debug, bool vsync);

    // Tear down. Idempotent.
    void shutdown() noexcept;

    // Window changed size; SDL_GPU auto-resizes the swapchain on the next
    // acquire, so this is currently a no-op stub — left as a hook for the
    // few backends (e.g. wayland fractional scaling) that need a manual
    // SDL_SetGPUSwapchainParameters round-trip.
    void on_window_resized() noexcept;

    // Begin a frame: acquire command buffer + wait for swapchain texture.
    // Returns an invalid frame_context on failure; caller skips draw.
    frame_context begin_frame() noexcept;

    // Submit (or cancel) the frame returned by begin_frame.
    void submit_frame(frame_context& ctx) noexcept;
    void cancel_frame(frame_context& ctx) noexcept;

    // Toggle vsync between frames. Cheap.
    void set_vsync(bool enable);

    SDL_GPUDevice* raw() const noexcept { return device.get(); }
    SDL_Window* window_ptr() const noexcept { return claimed_window; }
    SDL_GPUTextureFormat swapchain_format() const noexcept { return swap_format; }
    bool ready() const noexcept { return device != nullptr && claimed_window != nullptr; }

private:
    gpu_device_ptr device;
    SDL_Window* claimed_window = nullptr;
    SDL_GPUTextureFormat swap_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    bool vsync_enabled = true;
};

// Process-wide singleton accessor. Created on demand by sdltiles.cpp; nullptr
// until init_gpu_device is called. Defined in gpu_device.cpp.
gpu_device& get_gpu_device();

} // namespace lighting
