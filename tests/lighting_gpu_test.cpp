// Phase 2h of the lighting rework — golden-image regression harness.
//
// First iteration covers only the SDL_GPU device lifecycle (phase 2a) and
// the shader-compiler init/quit (phase 2c). The visual pixel-diff battery
// against reference scenes (sprite_batcher / gpu_atlas / font_engine /
// gpu_geometry) lands incrementally as the cutover (phase 2i) wires those
// pieces into a headless render target we can read back.
//
// All tests are tagged `[.gpu]` so the existing CI lane that runs
// `cata_test-tiles` without a GPU isn't asked to spin up Vulkan/Metal/D3D12.
// To run locally: `cata_test-tiles "[gpu]"`.

#include "catch/catch_amalgamated.hpp"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"
#include "lighting/sound_wave_pass.h"

#include <SDL3/SDL.h>

namespace {

// Minimum SDL init we need to create a window and claim it for the GPU
// device. SDL_INIT_VIDEO is enough; no audio / joystick.
struct sdl_video_session {
    bool ok = false;
    sdl_video_session() noexcept { ok = SDL_InitSubSystem(SDL_INIT_VIDEO); }
    ~sdl_video_session() noexcept {
        if (ok) { SDL_QuitSubSystem(SDL_INIT_VIDEO); }
    }
    sdl_video_session(const sdl_video_session&) = delete;
    sdl_video_session& operator=(const sdl_video_session&) = delete;
};

} // namespace

TEST_CASE("gpu_device lifecycle", "[.gpu][lighting]") {
    sdl_video_session sdl;
    if (!sdl.ok) {
        WARN("SDL_InitSubSystem(VIDEO) failed: " << SDL_GetError());
        return;
    }

    // Hidden window — keeps headless / CI-friendly without burning a real
    // present, but still lets us claim a swapchain.
    SDL_Window* win = SDL_CreateWindow("lighting_gpu_test", 320, 240, SDL_WINDOW_HIDDEN);
    REQUIRE(win != nullptr);

    {
        lighting::gpu_device dev;
        REQUIRE_NOTHROW(dev.init(win, /*debug=*/true, /*vsync=*/false));
        REQUIRE(dev.ready());
        REQUIRE(dev.raw() != nullptr);
        REQUIRE(dev.window_ptr() == win);
        REQUIRE(dev.swapchain_format() != SDL_GPU_TEXTUREFORMAT_INVALID);

        // begin/cancel a frame to exercise the acquire path without
        // actually presenting (would require a visible window).
        lighting::frame_context ctx = dev.begin_frame();
        if (ctx.valid()) {
            // Hidden windows may yield a null swapchain texture; either
            // way we must end the command buffer cleanly.
            dev.cancel_frame(ctx);
        }

        dev.shutdown();
        REQUIRE_FALSE(dev.ready());
    }

    SDL_DestroyWindow(win);
}

TEST_CASE("shader compiler init+quit is idempotent", "[.gpu][lighting]") {
    sdl_video_session sdl;
    if (!sdl.ok) {
        WARN("SDL_InitSubSystem(VIDEO) failed: " << SDL_GetError());
        return;
    }

    // Two init/shutdown cycles should both succeed (atomic-flag guard).
    REQUIRE_NOTHROW(lighting::init_shader_compiler());
    REQUIRE_NOTHROW(lighting::init_shader_compiler()); // double-init no-op
    REQUIRE_NOTHROW(lighting::shutdown_shader_compiler());
    REQUIRE_NOTHROW(lighting::shutdown_shader_compiler()); // double-quit no-op

    REQUIRE_NOTHROW(lighting::init_shader_compiler());
    REQUIRE_NOTHROW(lighting::shutdown_shader_compiler());
}

TEST_CASE("sound_wave_pass shader compiles", "[.gpu][lighting]") {
    sdl_video_session sdl;
    if (!sdl.ok) {
        WARN("SDL_InitSubSystem(VIDEO) failed: " << SDL_GetError());
        return;
    }

    SDL_Window* win = SDL_CreateWindow("lighting_gpu_test_sound_wave", 320, 240, SDL_WINDOW_HIDDEN);
    REQUIRE(win != nullptr);

    {
        lighting::gpu_device dev;
        REQUIRE_NOTHROW(dev.init(win, /*debug=*/true, /*vsync=*/false));
        REQUIRE(dev.ready());

        // Exercises the full HLSL -> SPIR-V/MSL cross-compile of the sound
        // wave shaders — including the ray-marched SDF visibility test in
        // sound_wave.frag.hlsl — through the real SDL_ShaderCross pipeline.
        // Catches HLSL syntax/semantic errors a pure C++ build can't.
        lighting::sound_wave_pass pass;
        REQUIRE(pass.init(dev, dev.swapchain_format()));
        REQUIRE(pass.ready());

        pass.shutdown();
        dev.shutdown();
    }

    SDL_DestroyWindow(win);
}
