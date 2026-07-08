#include "gpu_device.h"

#include "debug.h"
#include "options.h"

#include <stdexcept>
#include <string>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

void gpu_device_deleter::operator()(SDL_GPUDevice* d) const noexcept {
    if (d) { SDL_DestroyGPUDevice(d); }
}

gpu_device::~gpu_device() { shutdown(); }

void gpu_device::init(SDL_Window* window, bool debug, bool vsync) {
    if (!window) { throw std::runtime_error("gpu_device::init: null window"); }
    if (device) {
        // Already initialised — treat as programmer error so we surface
        // double-init quickly.
        throw std::runtime_error("gpu_device::init: device already initialised");
    }

    // Format mask = "any of these, pick driver-native". SDL_GPU loader picks
    // the matching backend: Vulkan->SPIRV, D3D12->DXIL, Metal->MSL. We embed
    // pre-compiled bytecode for all three (see phase 2c).
    const SDL_GPUShaderFormat formats =
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL;

    // Backend chosen by the GPU_DRIVER option ("auto" → nullptr → SDL picks the
    // platform default). Windows defaults to "vulkan" (some D3D12 drivers reject
    // the lighting pipelines — SDL_shadercross root-signature mismatch). If the
    // requested driver is unavailable (e.g. "vulkan" on macOS, where only Metal
    // exists), fall back to auto so we never hard-fail to a black window.
    const std::string gpu_driver = get_option<std::string>("GPU_DRIVER");
    const char* driver_name =
        (gpu_driver.empty() || gpu_driver == "auto") ? nullptr : gpu_driver.c_str();
    device.reset(SDL_CreateGPUDevice(formats, debug, driver_name));
    if (!device && driver_name) {
        dbg(DL::Warn) << "SDL_CreateGPUDevice(driver='" << gpu_driver
                      << "') failed: " << SDL_GetError() << " — falling back to auto driver";
        device.reset(SDL_CreateGPUDevice(formats, debug, /*name=*/nullptr));
    }
    if (!device) {
        const std::string msg = std::string("SDL_CreateGPUDevice failed: ") + SDL_GetError();
        dbg(DL::Error) << msg;
        throw std::runtime_error(msg);
    }

    if (!SDL_ClaimWindowForGPUDevice(device.get(), window)) {
        const std::string msg =
            std::string("SDL_ClaimWindowForGPUDevice failed: ") + SDL_GetError();
        dbg(DL::Error) << msg;
        device.reset();
        throw std::runtime_error(msg);
    }
    claimed_window = window;

    swap_format = SDL_GetGPUSwapchainTextureFormat(device.get(), window);
    vsync_enabled = vsync;
    const SDL_GPUPresentMode present_mode =
        vsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_MAILBOX;
    // Compositions: SDR is the only universally supported one. HDR support
    // negotiated later in the bloom/tonemap phase.
    SDL_SetGPUSwapchainParameters(
        device.get(), window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, present_mode);

    const char* driver = SDL_GetGPUDeviceDriver(device.get());
    dbg(DL::Info) << "SDL_GPU device created. driver=" << (driver ? driver : "?")
                  << " swapchain_format=" << static_cast<int>(swap_format)
                  << " vsync=" << (vsync ? "on" : "off");
}

void gpu_device::shutdown() noexcept {
    if (device && claimed_window) { SDL_ReleaseWindowFromGPUDevice(device.get(), claimed_window); }
    claimed_window = nullptr;
    swap_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    device.reset();
}

void gpu_device::on_window_resized() noexcept {
    // SDL_GPU rebuilds the swapchain on the next acquire automatically.
}

frame_context gpu_device::begin_frame() noexcept {
    frame_context ctx;
    if (!device || !claimed_window) { return ctx; }

    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(device.get());
    if (!cb) {
        dbg(DL::Warn) << "SDL_AcquireGPUCommandBuffer failed: " << SDL_GetError();
        return ctx;
    }

    SDL_GPUTexture* swap_tex = nullptr;
    Uint32 w = 0;
    Uint32 h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cb, claimed_window, &swap_tex, &w, &h)) {
        dbg(DL::Warn) << "SDL_WaitAndAcquireGPUSwapchainTexture failed: " << SDL_GetError();
        // We still own the command buffer — cancel it.
        SDL_CancelGPUCommandBuffer(cb);
        return ctx;
    }

    ctx.cmd_buffer = cb;
    ctx.swapchain_tex = swap_tex; // may be null on minimise
    ctx.swapchain_w = w;
    ctx.swapchain_h = h;
    return ctx;
}

void gpu_device::submit_frame(frame_context& ctx) noexcept {
    if (!ctx.cmd_buffer) { return; }
    SDL_SubmitGPUCommandBuffer(ctx.cmd_buffer);
    ctx.cmd_buffer = nullptr;
    ctx.swapchain_tex = nullptr;
}

void gpu_device::cancel_frame(frame_context& ctx) noexcept {
    if (!ctx.cmd_buffer) { return; }
    SDL_CancelGPUCommandBuffer(ctx.cmd_buffer);
    ctx.cmd_buffer = nullptr;
    ctx.swapchain_tex = nullptr;
}

void gpu_device::set_vsync(bool enable) {
    if (!device || !claimed_window || enable == vsync_enabled) { return; }
    vsync_enabled = enable;
    const SDL_GPUPresentMode present_mode =
        enable ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_MAILBOX;
    SDL_SetGPUSwapchainParameters(
        device.get(), claimed_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, present_mode);
}

gpu_device& get_gpu_device() {
    static gpu_device instance;
    return instance;
}

} // namespace lighting
