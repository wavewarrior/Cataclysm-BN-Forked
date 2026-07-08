#pragma once

// HLSL → backend-native SDL_GPUShader compiler — phase 2c of lighting rework.
//
// Thin C++ wrapper over SDL_shadercross. We ship HLSL source files in
// data/shaders/lighting/src/ and translate at runtime on first use:
//
//   * macOS arm64 / Metal      — HLSL → SPIR-V → MSL.
//   * Windows 11 / D3D12       — HLSL → SPIR-V → DXIL (via DXC).
//   * Windows 11 / Vulkan      — HLSL → SPIR-V.
//   * Linux / Vulkan           — HLSL → SPIR-V.
//
// The wrapper exposes:
//   - init() / shutdown()                 — call once per process.
//   - compile_graphics_shader(...)        — load + compile + reflect a single
//                                           HLSL source string into an
//                                           SDL_GPUShader, returning the
//                                           reflection metadata alongside it.
//
// Inert in this commit (sub-phase 2c): no in-tree caller until the
// sprite_batcher implementation lands in sub-phase 2d.

#include "gpu_device.h"

#include <SDL3_shadercross/SDL_shadercross.h>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lighting {

// Resource bindings the compiled shader uses. Mirrors
// SDL_ShaderCross_GraphicsShaderResourceInfo so callers don't need to include
// the shadercross header.
struct shader_resource_info {
    std::uint32_t num_samplers = 0;
    std::uint32_t num_storage_textures = 0;
    std::uint32_t num_storage_buffers = 0;
    std::uint32_t num_uniform_buffers = 0;
};

// Result of a successful compile. `shader` is owned — caller must release
// with SDL_ReleaseGPUShader(device, shader).
struct compiled_shader {
    SDL_GPUShader* shader = nullptr;
    shader_resource_info resources{};
    explicit operator bool() const noexcept { return shader != nullptr; }
};

// Compute resource model. Mirrors SDL_ShaderCross_ComputePipelineMetadata.
// Distinct from the graphics reflection: compute splits storage into
// readonly/readwrite and carries the [numthreads] workgroup size.
struct compute_resource_info {
    std::uint32_t num_samplers = 0;
    std::uint32_t num_readonly_storage_textures = 0;
    std::uint32_t num_readonly_storage_buffers = 0;
    std::uint32_t num_readwrite_storage_textures = 0;
    std::uint32_t num_readwrite_storage_buffers = 0;
    std::uint32_t num_uniform_buffers = 0;
    std::uint32_t threadcount_x = 0;
    std::uint32_t threadcount_y = 0;
    std::uint32_t threadcount_z = 0;
};

// Result of a successful compute compile. `pipeline` is owned — caller must
// release with SDL_ReleaseGPUComputePipeline(device, pipeline). Unlike the
// graphics path (shader → separate pipeline build), shadercross emits the
// compute pipeline directly from SPIR-V + reflected metadata.
struct compiled_compute_pipeline {
    SDL_GPUComputePipeline* pipeline = nullptr;
    compute_resource_info resources{};
    explicit operator bool() const noexcept { return pipeline != nullptr; }
};

// Init the global SDL_shadercross state. Must be called after gpu_device::init
// and before any compile_graphics_shader call. Idempotent — safe to call once
// per process.
void init_shader_compiler();

// Tear down. Idempotent.
void shutdown_shader_compiler() noexcept;

// Single-call HLSL → SDL_GPUShader. Performs the three-step shadercross
// pipeline internally: HLSL → SPIR-V → reflection → backend-native shader.
//
// `source`     — HLSL text. Must remain valid only for the duration of the
//                call (copied internally).
// `entrypoint` — e.g. "main". Must remain valid for the duration of the call.
// `stage`      — SDL_SHADERCROSS_SHADERSTAGE_VERTEX or
//                SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT.
// `debug_name` — optional label for GPU-debugger inspection.
//
// On failure logs to the SDL debug channel and returns an empty
// compiled_shader (operator bool == false). The caller is expected to treat
// shader-compile failure as fatal.
compiled_shader compile_graphics_shader(
    gpu_device& dev, std::string_view source, const char* entrypoint,
    SDL_ShaderCross_ShaderStage stage, const char* debug_name = nullptr);

// Single-call HLSL → SDL_GPUComputePipeline. HLSL → SPIR-V → reflect compute
// metadata → backend-native compute pipeline. The reflected metadata (resource
// counts + [numthreads] workgroup size) is passed straight through to
// SDL_CreateGPUComputePipeline, so the shader declares its own resource model.
//
// `source`     — HLSL compute text (copied internally).
// `entrypoint` — e.g. "main".
// `debug_name` — optional label for diagnostics.
//
// On failure logs to the SDL debug channel and returns an empty result
// (operator bool == false). Used both by the A0 go/no-go compute spike and the
// real gi_compute_pass.
compiled_compute_pipeline compile_compute_pipeline(
    gpu_device& dev, std::string_view source, const char* entrypoint,
    const char* debug_name = nullptr);

// Load an HLSL shader source from the lighting shader dir
// (data/shaders/lighting/src/<name>). Single source of truth for the live
// shaders (no longer embedded in sprite_batcher.cpp). Returns the file text, or
// an empty string (logged) when missing/unreadable — the caller treats an empty
// source as a fatal compile failure, same as compile_graphics_shader.
std::string load_lighting_shader_source(const std::string& name);

} // namespace lighting
