#include "sound_wave_pass.h"

#include "debug.h"
#include "gpu_device.h"
#include "shader_compiler.h"

#include <algorithm>
#include <cstring>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

// ---- Pipeline helper ------------------------------------------------------

static SDL_GPUGraphicsPipeline* make_sound_wave_pipeline(
    SDL_GPUDevice* dev, SDL_GPUShader* vert, SDL_GPUShader* frag,
    SDL_GPUTextureFormat fmt)
{
    // Premultiplied alpha blend (same as rain_effect).
    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.color_write_mask =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B
        | SDL_GPU_COLORCOMPONENT_A;

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = fmt;
    ctd.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = vert;
    pi.fragment_shader = frag;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pi.target_info.num_color_targets = 1;
    pi.target_info.color_target_descriptions = &ctd;
    pi.target_info.has_depth_stencil_target = false;
    return SDL_CreateGPUGraphicsPipeline(dev, &pi);
}

// ---- Constructor / Destructor ---------------------------------------------

sound_wave_pass::~sound_wave_pass() { shutdown(); }

// ---- Init -----------------------------------------------------------------

auto sound_wave_pass::init(gpu_device& dev, SDL_GPUTextureFormat target_format) -> bool
{
    shutdown();
    dev_ = &dev;
    target_format_ = target_format;

    if (!dev.ready()) {
        dbg(DL::Error) << "sound_wave_pass::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();

    // ---- Shaders -----------------------------------------------------------
    const std::string vert_src = load_lighting_shader_source("sound_wave.vert.hlsl");
    const std::string frag_src = load_lighting_shader_source("sound_wave.frag.hlsl");
    if (vert_src.empty() || frag_src.empty()) {
        dbg(DL::Error) << "sound_wave_pass: failed to load shader source";
        return false;
    }

    auto v = compile_graphics_shader(
        dev, vert_src, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "sound_wave.vert");
    auto f = compile_graphics_shader(
        dev, frag_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "sound_wave.frag");
    if (!v || !f) {
        dbg(DL::Error) << "sound_wave_pass: shader compile failed";
        return false;
    }
    vert_ = v.shader;
    frag_ = f.shader;

    pipeline_ = make_sound_wave_pipeline(dev.raw(), vert_, frag_, target_format_);
    if (!pipeline_) {
        dbg(DL::Error) << "sound_wave_pass: pipeline create failed";
        return false;
    }

    // ---- Instance buffers --------------------------------------------------
    const Uint32 bytes = static_cast<Uint32>(MAX_INSTANCES * sizeof(sound_wave_instance));

    SDL_GPUBufferCreateInfo bci{};
    bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bci.size = bytes;
    storage_ = SDL_CreateGPUBuffer(dev.raw(), &bci);

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = bytes;
    xfer_ = SDL_CreateGPUTransferBuffer(dev.raw(), &tbi);

    if (!storage_ || !xfer_) {
        dbg(DL::Error) << "sound_wave_pass: instance buffer create failed";
        if (storage_) { SDL_ReleaseGPUBuffer(dev_->raw(), storage_); storage_ = nullptr; }
        if (xfer_) { SDL_ReleaseGPUTransferBuffer(dev_->raw(), xfer_); xfer_ = nullptr; }
        return false;
    }

    DebugLogFL(DL::Info, DC::Main)
        << "sound_wave_pass: initialised (cap=" << MAX_INSTANCES << ")";
    return true;
}

// ---- Shutdown -------------------------------------------------------------

auto sound_wave_pass::shutdown() noexcept -> void
{
    if (dev_ && dev_->ready()) {
        SDL_ReleaseGPUGraphicsPipeline(dev_->raw(), pipeline_);
        SDL_ReleaseGPUShader(dev_->raw(), vert_);
        SDL_ReleaseGPUShader(dev_->raw(), frag_);
        SDL_ReleaseGPUBuffer(dev_->raw(), storage_);
        SDL_ReleaseGPUTransferBuffer(dev_->raw(), xfer_);
    }

    pipeline_ = nullptr;
    vert_ = nullptr;
    frag_ = nullptr;
    storage_ = nullptr;
    xfer_ = nullptr;
    dev_ = nullptr;
}

// ---- Upload ---------------------------------------------------------------

auto sound_wave_pass::upload_instances(
    SDL_GPUCommandBuffer* cb, const std::vector<sound_wave_instance>& insts) -> bool
{
    if (insts.empty()) { return false; }

    const Uint32 count = static_cast<Uint32>(
        std::min(insts.size(), std::vector<sound_wave_instance>::size_type(MAX_INSTANCES)));
    const Uint32 bytes = count * sizeof(sound_wave_instance);

    void* mapped = SDL_MapGPUTransferBuffer(dev_->raw(), xfer_, /*cycle=*/true);
    if (!mapped) {
        dbg(DL::Error) << "sound_wave_pass: MapGPUTransferBuffer failed";
        return false;
    }
    std::memcpy(mapped, insts.data(), bytes);
    SDL_UnmapGPUTransferBuffer(dev_->raw(), xfer_);

    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    if (!cp) {
        dbg(DL::Error) << "sound_wave_pass: copy pass begin failed";
        return false;
    }
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = xfer_;
    src.offset = 0;
    SDL_GPUBufferRegion dst{};
    dst.buffer = storage_;
    dst.offset = 0;
    dst.size = bytes;
    SDL_UploadToGPUBuffer(cp, &src, &dst, /*cycle=*/true);
    SDL_EndGPUCopyPass(cp);
    return true;
}

// ---- Per-frame record -----------------------------------------------------

auto sound_wave_pass::record(const sound_wave_record_options& opts) -> void
{
    if (!ready() || !opts.cb || !opts.target || opts.proj_w == 0 || opts.proj_h == 0) { return; }
    if (!opts.instances || opts.instances->empty()) { return; }

    const auto& instances = *opts.instances;
    const Uint32 count = static_cast<Uint32>(
        std::min(instances.size(), std::vector<sound_wave_instance>::size_type(MAX_INSTANCES)));

    if (!upload_instances(opts.cb, instances)) { return; }

    // Vertex uniform: SoundWaveParams cbuffer (b0/space1) — just projection dims.
    // Per-pulse data (source, radius_px, life) is now in the instance buffer.
    struct alignas(16) sound_wave_params {
        float proj_w;
        float proj_h;
        float pad0 = 0.f;
        float pad1 = 0.f;
    };
    sound_wave_params params{};
    params.proj_w = static_cast<float>(opts.proj_w);
    params.proj_h = static_cast<float>(opts.proj_h);

    SDL_GPUColorTargetInfo ct{};
    ct.texture = opts.target;
    ct.load_op = SDL_GPU_LOADOP_LOAD;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(opts.cb, &ct, 1, nullptr);
    if (!rp) {
        dbg(DL::Error) << "sound_wave_pass: render pass begin failed";
        return;
    }

    SDL_BindGPUGraphicsPipeline(rp, pipeline_);
    SDL_BindGPUVertexStorageBuffers(rp, 0, &storage_, 1);
    SDL_PushGPUVertexUniformData(opts.cb, 0, &params, sizeof(params));

    // Fragment uniform: SDF transform params (b0/space3).
    SDL_PushGPUFragmentUniformData(opts.cb, 0, &opts.snd_frag_params, sizeof(opts.snd_frag_params));

    // SDF storage buffer (t0/space2) — only bind when available.
    if (opts.sdf_buffer) {
        SDL_BindGPUFragmentStorageBuffers(rp, 0, &opts.sdf_buffer, 1);
    }

    // Draw 6 vertices (unit quad) × N instances.
    SDL_DrawGPUPrimitives(rp, /*vertex_count=*/6, /*instance_count=*/count,
                          /*first_vertex=*/0, /*first_instance=*/0);

    SDL_EndGPURenderPass(rp);
}

} // namespace lighting