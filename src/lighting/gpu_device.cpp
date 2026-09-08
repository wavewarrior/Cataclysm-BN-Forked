#include "gpu_device.h"

#include "debug.h"
#include "options.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

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
    if (dump_xfer_ != nullptr) { SDL_ReleaseGPUTransferBuffer( device.get(), dump_xfer_ ); dump_xfer_ = nullptr; }
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

namespace
{
// F13 on-demand dump: the input handler bumps the request; the next frame
// consumes it and dumps to /tmp/cata_frame_<n>.bmp (+ cata_map_<n>.json).
std::uint64_t g_frame_dump_request = 0;
}

void request_frame_dump()
{
    ++g_frame_dump_request;
}

std::uint64_t frame_dump_request()
{
    return g_frame_dump_request;
}

bool gpu_device::maybe_dump_frame(frame_context& ctx) noexcept {
    if (!ctx.swapchain_tex) { return false; }
    ++frame_count_;
    if (frame_count_ % 100 == 0) {
        DebugLogFL( DL::Info, DC::Main ) << "frame heartbeat: " << frame_count_;
    }
    std::string path;
    const bool file_trigger = std::filesystem::exists( "/tmp/cata_dump_trigger" );
    if (g_frame_dump_request > 0 || file_trigger) {
        g_frame_dump_request = 0;
        if (file_trigger) { std::filesystem::remove( "/tmp/cata_dump_trigger" ); }
        path = "/tmp/cata_frame_" + std::to_string( frame_count_ ) + ".bmp";
    } else {
        static const char* spec = std::getenv( "CATA_FRAME_DUMP" );
        if (spec == nullptr) { return false; }
        const char* colon = std::strchr( spec, ':' );
        if (colon == nullptr) { return false; }
        if (std::strtoull( spec, nullptr, 10 ) != frame_count_) { return false; }
        path.assign( colon + 1 );
    }
    const Uint32 w = ctx.swapchain_w;
    const Uint32 h = ctx.swapchain_h;
    const Uint32 bytes = w * h * 4;
    if (dump_xfer_ == nullptr || dump_xfer_bytes_ < bytes) {
        if (dump_xfer_ != nullptr) { SDL_ReleaseGPUTransferBuffer( device.get(), dump_xfer_ ); }
        SDL_GPUTransferBufferCreateInfo tbi{};
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        tbi.size = bytes;
        dump_xfer_ = SDL_CreateGPUTransferBuffer( device.get(), &tbi );
        dump_xfer_bytes_ = dump_xfer_ ? bytes : 0u;
    }
    if (dump_xfer_ == nullptr) {
        DebugLogFL( DL::Warn, DC::Main ) << "frame dump: transfer buffer create failed: " << SDL_GetError();
        return false;
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass( ctx.cmd_buffer );
    if (!cp) {
        DebugLogFL( DL::Warn, DC::Main ) << "frame dump: copy pass failed: " << SDL_GetError();
        return false;
    }
    SDL_GPUTextureRegion src{};
    src.texture = ctx.swapchain_tex;
    src.mip_level = 0;
    src.layer = 0;
    src.x = 0;
    src.y = 0;
    src.w = w;
    src.h = h;
    src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = dump_xfer_;
    dst.offset = 0;
    dst.pixels_per_row = w;
    dst.rows_per_layer = h;
    SDL_DownloadFromGPUTexture( cp, &src, &dst );
    SDL_EndGPUCopyPass( cp );
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence( ctx.cmd_buffer );
    if (fence == nullptr) {
        DebugLogFL( DL::Warn, DC::Main ) << "frame dump: fence acquire failed: " << SDL_GetError();
        return false;
    }
    if (!SDL_WaitForGPUFences( device.get(), true, &fence, 1 )) {
        DebugLogFL( DL::Warn, DC::Main ) << "frame dump: fence wait failed: " << SDL_GetError();
        SDL_ReleaseGPUFence( device.get(), fence );
        return false;
    }
    SDL_ReleaseGPUFence( device.get(), fence );
    void* mapped = SDL_MapGPUTransferBuffer( device.get(), dump_xfer_, false );
    if (mapped == nullptr) {
        DebugLogFL( DL::Warn, DC::Main ) << "frame dump: map failed: " << SDL_GetError();
        return false;
    }
    const auto* px = static_cast<const unsigned char*>( mapped );
    std::ofstream f( path, std::ios::binary );
    if (!f) {
        DebugLogFL( DL::Warn, DC::Main ) << "frame dump: cannot open " << path;
        SDL_UnmapGPUTransferBuffer( device.get(), dump_xfer_ );
        return false;
    }
    const unsigned int row_bytes = w * 3;
    const unsigned int row_pitch = ( row_bytes + 3u ) & ~3u;
    const unsigned int img_size = row_pitch * h;
    unsigned char hdr[54] = {};
    hdr[0] = 'B';
    hdr[1] = 'M';
    const auto put32 = [&]( int off, unsigned int v ) {
        hdr[off] = static_cast<unsigned char>( v & 0xFF );
        hdr[off + 1] = static_cast<unsigned char>( ( v >> 8 ) & 0xFF );
        hdr[off + 2] = static_cast<unsigned char>( ( v >> 16 ) & 0xFF );
        hdr[off + 3] = static_cast<unsigned char>( ( v >> 24 ) & 0xFF );
    };
    const auto put16 = [&]( int off, unsigned int v ) {
        hdr[off] = static_cast<unsigned char>( v & 0xFF );
        hdr[off + 1] = static_cast<unsigned char>( ( v >> 8 ) & 0xFF );
    };
    put32( 2, 54u + img_size );
    put32( 10, 54u );
    put32( 14, 40u );
    put32( 18, w );
    put32( 22, h );
    put16( 26, 1u );
    put16( 28, 24u );
    put32( 34, img_size );
    f.write( reinterpret_cast<const char*>( hdr ), 54 );
    std::vector<unsigned char> row( row_pitch, 0 );
    for ( Uint32 y = 0; y < h; ++y ) {
        const Uint32 src_row = ( h - 1 - y ) * w; // BMP is bottom-up
        for ( Uint32 x = 0; x < w; ++x ) {
            const unsigned char* s = &px[ ( src_row + x ) * 4 ];
            row[ x * 3 + 0 ] = s[0];
            row[ x * 3 + 1 ] = s[1];
            row[ x * 3 + 2 ] = s[2];
        }
        f.write( reinterpret_cast<const char*>( row.data()), static_cast<std::streamsize>( row_pitch ) );
    }
    SDL_UnmapGPUTransferBuffer( device.get(), dump_xfer_ );
    last_dump_frame_ = frame_count_;
    DebugLogFL( DL::Info, DC::Main ) << "frame dump: wrote " << path << " (" << w << "x" << h
                                     << ", fmt=" << static_cast<int>( swap_format ) << ")";
    return true;
}

void gpu_device::submit_frame(frame_context& ctx) noexcept {
    if (!ctx.cmd_buffer) { return; }
    if (maybe_dump_frame( ctx )) {
        // Dump path already submitted the buffer (with fence) and waited.
        ctx.cmd_buffer = nullptr;
        ctx.swapchain_tex = nullptr;
        return;
    }
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
