#include "shader_compiler.h"

#include "debug.h"
#include "filesystem.h"
#include "path_info.h"

#include <SDL3_shadercross/SDL_shadercross.h>
#include <atomic>
#include <string>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

namespace {

std::atomic<bool> compiler_initialised{false};

} // namespace

void init_shader_compiler() {
    bool expected = false;
    if (!compiler_initialised.compare_exchange_strong(expected, true)) {
        return; // already up
    }
    if (!SDL_ShaderCross_Init()) {
        compiler_initialised.store(false);
        dbg(DL::Error) << "SDL_ShaderCross_Init failed: " << SDL_GetError();
        // Caller's error policy: fatal. Keep the failure observable via SDL
        // error stack so the caller can decide whether to throw.
    } else {
        dbg(DL::Info) << "SDL_ShaderCross initialised.";
    }
}

void shutdown_shader_compiler() noexcept {
    bool expected = true;
    if (!compiler_initialised.compare_exchange_strong(expected, false)) { return; }
    SDL_ShaderCross_Quit();
}

std::string load_lighting_shader_source(const std::string& name) {
    const std::string path = PATH_INFO::datadir() + "shaders/lighting/src/" + name;
    std::string src = read_entire_file(path);
    if (src.empty()) { dbg(DL::Error) << "load_lighting_shader_source: empty/missing " << path; }
    return src;
}

compiled_shader compile_graphics_shader(
    gpu_device& dev, std::string_view source, const char* entrypoint,
    SDL_ShaderCross_ShaderStage stage, const char* debug_name) {
    compiled_shader out;
    if (!dev.ready()) {
        dbg(DL::Error) << "compile_graphics_shader: gpu_device not ready";
        return out;
    }
    if (!compiler_initialised.load()) {
        dbg(DL::Error) << "compile_graphics_shader: SDL_ShaderCross not initialised";
        return out;
    }

    // shadercross expects a null-terminated source string.
    const std::string src_z(source);

    // Resolve `#include` directives (the jfa_*.comp shaders pull in
    // jfa_shared.hlsl) relative to the lighting shader src dir. DXC needs an
    // explicit include search dir; a null include_dir makes the include fail
    // with "file not found" so the pipeline never compiles.
    const std::string inc_dir = PATH_INFO::datadir() + "shaders/lighting/src";

    SDL_ShaderCross_HLSL_Info hlsl_info{};
    hlsl_info.source = src_z.c_str();
    hlsl_info.entrypoint = entrypoint;
    hlsl_info.include_dir = inc_dir.c_str();
    hlsl_info.defines = nullptr;
    hlsl_info.shader_stage = stage;
    hlsl_info.props = 0;

    // Step A: HLSL → SPIR-V.
    size_t spirv_size = 0;
    void* spirv = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlsl_info, &spirv_size);
    if (!spirv || spirv_size == 0) {
        dbg(DL::Error) << "HLSL→SPIRV failed (" << (debug_name ? debug_name : "?")
                       << "): " << SDL_GetError();
        return out;
    }

    // Step B: reflect SPIR-V to learn the resource layout the SDL_GPU shader
    // object needs.
    SDL_ShaderCross_GraphicsShaderMetadata* meta = SDL_ShaderCross_ReflectGraphicsSPIRV(
        static_cast<const Uint8*>(spirv), spirv_size, /*props=*/0);
    if (!meta) {
        dbg(DL::Error) << "SPIR-V reflect failed (" << (debug_name ? debug_name : "?")
                       << "): " << SDL_GetError();
        SDL_free(spirv);
        return out;
    }

    // Step C: SPIR-V → backend shader.
    SDL_ShaderCross_SPIRV_Info spirv_info{};
    spirv_info.bytecode = static_cast<const Uint8*>(spirv);
    spirv_info.bytecode_size = spirv_size;
    spirv_info.entrypoint = entrypoint;
    spirv_info.shader_stage = stage;
    spirv_info.props = 0;

    SDL_GPUShader* gpu_shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
        dev.raw(), &spirv_info, &meta->resource_info, /*props=*/0);

    // Pluck out the resource counts before we free the metadata.
    out.resources.num_samplers = meta->resource_info.num_samplers;
    out.resources.num_storage_textures = meta->resource_info.num_storage_textures;
    out.resources.num_storage_buffers = meta->resource_info.num_storage_buffers;
    out.resources.num_uniform_buffers = meta->resource_info.num_uniform_buffers;

    SDL_free(spirv);
    SDL_free(meta);

    if (!gpu_shader) {
        dbg(DL::Error) << "SPIRV→GPUShader failed (" << (debug_name ? debug_name : "?")
                       << "): " << SDL_GetError();
        return out;
    }

    out.shader = gpu_shader;
    return out;
}

compiled_compute_pipeline compile_compute_pipeline(
    gpu_device& dev, std::string_view source, const char* entrypoint, const char* debug_name) {
    compiled_compute_pipeline out;
    if (!dev.ready()) {
        dbg(DL::Error) << "compile_compute_pipeline: gpu_device not ready";
        return out;
    }
    if (!compiler_initialised.load()) {
        dbg(DL::Error) << "compile_compute_pipeline: SDL_ShaderCross not initialised";
        return out;
    }

    const std::string src_z(source);

    // Resolve `#include` directives (jfa_*.comp pull in jfa_shared.hlsl) relative
    // to the lighting shader src dir. A null include_dir makes the include fail
    // with "file not found" so the compute pipeline never compiles.
    const std::string inc_dir = PATH_INFO::datadir() + "shaders/lighting/src";

    SDL_ShaderCross_HLSL_Info hlsl_info{};
    hlsl_info.source = src_z.c_str();
    hlsl_info.entrypoint = entrypoint;
    hlsl_info.include_dir = inc_dir.c_str();
    hlsl_info.defines = nullptr;
    hlsl_info.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;
    hlsl_info.props = 0;

    // Step A: HLSL → SPIR-V.
    size_t spirv_size = 0;
    void* spirv = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlsl_info, &spirv_size);
    if (!spirv || spirv_size == 0) {
        dbg(DL::Error) << "compute HLSL→SPIRV failed (" << (debug_name ? debug_name : "?")
                       << "): " << SDL_GetError();
        return out;
    }

    // Step B: reflect the compute resource model (readonly/readwrite split +
    // [numthreads] workgroup size). Passed verbatim to the pipeline create.
    SDL_ShaderCross_ComputePipelineMetadata* meta = SDL_ShaderCross_ReflectComputeSPIRV(
        static_cast<const Uint8*>(spirv), spirv_size, /*props=*/0);
    if (!meta) {
        dbg(DL::Error) << "compute SPIR-V reflect failed (" << (debug_name ? debug_name : "?")
                       << "): " << SDL_GetError();
        SDL_free(spirv);
        return out;
    }

    // Step C: SPIR-V + metadata → backend compute pipeline (one call, unlike
    // the graphics shader+pipeline split).
    SDL_ShaderCross_SPIRV_Info spirv_info{};
    spirv_info.bytecode = static_cast<const Uint8*>(spirv);
    spirv_info.bytecode_size = spirv_size;
    spirv_info.entrypoint = entrypoint;
    spirv_info.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;
    spirv_info.props = 0;

    SDL_GPUComputePipeline* pipe =
        SDL_ShaderCross_CompileComputePipelineFromSPIRV(dev.raw(), &spirv_info, meta, /*props=*/0);

    out.resources.num_samplers = meta->num_samplers;
    out.resources.num_readonly_storage_textures = meta->num_readonly_storage_textures;
    out.resources.num_readonly_storage_buffers = meta->num_readonly_storage_buffers;
    out.resources.num_readwrite_storage_textures = meta->num_readwrite_storage_textures;
    out.resources.num_readwrite_storage_buffers = meta->num_readwrite_storage_buffers;
    out.resources.num_uniform_buffers = meta->num_uniform_buffers;
    out.resources.threadcount_x = meta->threadcount_x;
    out.resources.threadcount_y = meta->threadcount_y;
    out.resources.threadcount_z = meta->threadcount_z;

    SDL_free(spirv);
    SDL_free(meta);

    if (!pipe) {
        dbg(DL::Error) << "SPIRV→ComputePipeline failed (" << (debug_name ? debug_name : "?")
                       << "): " << SDL_GetError();
        return out;
    }

    out.pipeline = pipe;
    return out;
}

} // namespace lighting
