#include "gpu_device.h"

#include "driver_capture.h"
#include "debug.h"
#include "options.h"

#include <optional>
#include <system_error>
#include <utility>
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
    SDL_GPUPresentMode present_mode =
        vsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_MAILBOX;
    // Compositions: SDR is the only universally supported one. HDR support
    // negotiated later in the bloom/tonemap phase.
    bool present_ok = SDL_SetGPUSwapchainParameters(
        device.get(), window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, present_mode);
    // Measurement-only: benchmark harnesses set CATA_MEASURE_IMMEDIATE=1 so an
    // unsupported MAILBOX (Metal) does not leave frame_period vsync-quantized.
    // Production never takes this branch.
    const char* measure_env = std::getenv("CATA_MEASURE_IMMEDIATE");
    if (!present_ok && !vsync && measure_env != nullptr && std::strcmp(measure_env, "1") == 0) {
        DebugLogFL( DL::Info, DC::Main ) << "present mode mailbox rejected (" << SDL_GetError()
                      << "); CATA_MEASURE_IMMEDIATE retrying immediate";
        present_mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
        present_ok = SDL_SetGPUSwapchainParameters(
            device.get(), window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, present_mode);
    }
    // present= is the mode the last request selected; on ret=0 the swapchain
    // keeps SDL_ClaimWindowForGPUDevice's default (VSYNC).
    const char* present_name = !present_ok ? "vsync"
                               : present_mode == SDL_GPU_PRESENTMODE_MAILBOX ? "mailbox"
                               : present_mode == SDL_GPU_PRESENTMODE_IMMEDIATE ? "immediate"
                               : "vsync";

    const char* driver = SDL_GetGPUDeviceDriver(device.get());
    DebugLogFL( DL::Info, DC::Main ) << "SDL_GPU device created. driver=" << (driver ? driver : "?")
                  << " swapchain_format=" << static_cast<int>(swap_format)
                  << " vsync=" << (vsync ? "on" : "off")
                  << " present=" << present_name << " ret=" << (present_ok ? 1 : 0);
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

namespace
{
// The driver's `capture`: one frame, dumped to a path of the caller's (see driver_capture.h).
struct armed_capture {
    bool armed = false;
    std::string path;
    std::optional<frame_capture_report> report;
};
armed_capture g_capture;
}

void arm_frame_capture( std::string path )
{
    g_capture = { .armed = true, .path = std::move( path ), .report = std::nullopt };
}

std::optional<frame_capture_report> take_frame_capture()
{
    g_capture.armed = false;
    return std::exchange( g_capture.report, std::nullopt );
}

bool gpu_device::maybe_dump_frame(frame_context& ctx) noexcept {
    if (!ctx.swapchain_tex) { return false; }
    ++frame_count_;
    if (frame_count_ % 100 == 0) {
        DebugLogFL( DL::Info, DC::Main ) << "frame heartbeat: " << frame_count_;
    }
    std::string path;
    // The driver's capture is one-shot: the frame that takes it disarms it, so no later frame
    // can be taken for the request.
    const bool capturing = g_capture.armed;
    g_capture.armed = false;
    const bool file_trigger = !capturing && std::filesystem::exists( "/tmp/cata_dump_trigger" );
    if (capturing) {
        path = g_capture.path + ".part";
    } else if (g_frame_dump_request > 0 || file_trigger) {
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
    // Once the command buffer is submitted it is gone, so a failure after that must not have
    // submit_frame submit it again.
    bool submitted = false;
    const auto fail = [&]( const std::string& why ) {
        if (capturing) {
            std::error_code ec;
            std::filesystem::remove( path, ec );
            g_capture.report = frame_capture_report{ .error = why };
        }
        return submitted;
    };
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
        return fail( "transfer buffer create failed" );
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass( ctx.cmd_buffer );
    if (!cp) {
        DebugLogFL( DL::Warn, DC::Main ) << "frame dump: copy pass failed: " << SDL_GetError();
        return fail( "copy pass failed" );
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
    submitted = true;
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence( ctx.cmd_buffer );
    if (fence == nullptr) {
        DebugLogFL( DL::Warn, DC::Main ) << "frame dump: fence acquire failed: " << SDL_GetError();
        return fail( "fence acquire failed" );
    }
    if (!SDL_WaitForGPUFences( device.get(), true, &fence, 1 )) {
        DebugLogFL( DL::Warn, DC::Main ) << "frame dump: fence wait failed: " << SDL_GetError();
        SDL_ReleaseGPUFence( device.get(), fence );
        return fail( "fence wait failed" );
    }
    SDL_ReleaseGPUFence( device.get(), fence );
    void* mapped = SDL_MapGPUTransferBuffer( device.get(), dump_xfer_, false );
    if (mapped == nullptr) {
        DebugLogFL( DL::Warn, DC::Main ) << "frame dump: map failed: " << SDL_GetError();
        return fail( "map failed" );
    }
    const auto* px = static_cast<const unsigned char*>( mapped );
    std::ofstream f( path, std::ios::binary );
    if (!f) {
        DebugLogFL( DL::Warn, DC::Main ) << "frame dump: cannot open " << path;
        SDL_UnmapGPUTransferBuffer( device.get(), dump_xfer_ );
        return fail( "cannot open " + path );
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
    if (capturing) {
        f.close();
        std::error_code ec;
        if (!f) { return fail( "cannot write " + path ); }
        std::filesystem::rename( path, g_capture.path, ec );
        if (ec) { return fail( "cannot publish " + g_capture.path + ": " + ec.message() ); }
        g_capture.report = frame_capture_report{ .written = true, .width = w, .height = h,
                                                 .frame = frame_count_ };
        DebugLogFL( DL::Info, DC::Main ) << "frame capture: wrote " << g_capture.path << " (" << w
                                         << "x" << h << ")";
        return true;
    }
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
