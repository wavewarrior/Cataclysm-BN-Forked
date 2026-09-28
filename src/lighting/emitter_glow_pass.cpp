#include "emitter_glow_pass.h"

#include "debug.h"
#include "gpu_device.h"
#include "shader_compiler.h"

#include <algorithm>
#include <cstring>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

// ---- Pipeline helper -------------------------------------------------------

static auto make_emitter_glow_pipeline(
    SDL_GPUDevice* dev, SDL_GPUShader* vert, SDL_GPUShader* frag,
    SDL_GPUTextureFormat fmt) -> SDL_GPUGraphicsPipeline*
{
    // Pure additive blend (same convention as bloom_composite.frag.hlsl):
    // colour accumulates, alpha stays 0 so target alpha is unaffected.
    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.color_write_mask =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B
        | SDL_GPU_COLORCOMPONENT_A;

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = fmt;
    color_target.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = vert;
    pi.fragment_shader = frag;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pi.target_info.num_color_targets = 1;
    pi.target_info.color_target_descriptions = &color_target;
    pi.target_info.has_depth_stencil_target = false;
    return SDL_CreateGPUGraphicsPipeline(dev, &pi);
}

// ---- Constructor / Destructor ----------------------------------------------

emitter_glow_pass::~emitter_glow_pass() { shutdown(); }

// ---- Init -------------------------------------------------------------------

auto emitter_glow_pass::init(gpu_device& dev, SDL_GPUTextureFormat target_format) -> bool
{
    shutdown();
    dev_ = &dev;
    target_format_ = target_format;
    if (!dev.ready()) {
        dbg(DL::Error) << "emitter_glow_pass::init: gpu_device not ready";
        return false;
    }

    // ---- Shaders ----
    const std::string vert_src = load_lighting_shader_source("emitter_glow.vert.hlsl");
    const std::string frag_src = load_lighting_shader_source("emitter_glow.frag.hlsl");
    if (vert_src.empty() || frag_src.empty()) {
        dbg(DL::Error) << "emitter_glow_pass: failed to load shader source";
        return false;
    }
    auto v = compile_graphics_shader(
        dev, vert_src, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "emitter_glow.vert");
    if (!v.shader) {
        dbg(DL::Error) << "emitter_glow_pass: vertex compile failed";
        return false;
    }
    auto f = compile_graphics_shader(
        dev, frag_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "emitter_glow.frag");
    if (!f.shader) {
        dbg(DL::Error) << "emitter_glow_pass: fragment compile failed";
        return false;
    }
    vert_ = v.shader;
    frag_ = f.shader;

    pipeline_ = make_emitter_glow_pipeline(dev.raw(), vert_, frag_, target_format);
    if (!pipeline_) {
        dbg(DL::Error) << "emitter_glow_pass: pipeline create failed: " << SDL_GetError();
        return false;
    }

    // ---- Instance buffers ----
    const Uint32 bytes = static_cast<Uint32>(MAX_INSTANCES * sizeof(emitter_glow_instance));
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bci.size = bytes;
    storage_ = SDL_CreateGPUBuffer(dev.raw(), &bci);

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = bytes;
    xfer_ = SDL_CreateGPUTransferBuffer(dev.raw(), &tbi);

    if (!storage_ || !xfer_) {
        dbg(DL::Error) << "emitter_glow_pass: instance buffer create failed";
        if (storage_) { SDL_ReleaseGPUBuffer(dev.raw(), storage_); storage_ = nullptr; }
        if (xfer_) { SDL_ReleaseGPUTransferBuffer(dev.raw(), xfer_); xfer_ = nullptr; }
        return false;
    }

    DebugLogFL(DL::Info, DC::Main)
        << "emitter_glow_pass: initialised (cap=" << MAX_INSTANCES << ")";
    return true;
}

// ---- Shutdown ---------------------------------------------------------------

auto emitter_glow_pass::shutdown() noexcept -> void
{
    if (dev_ && dev_->ready()) {
        if (pipeline_) { SDL_ReleaseGPUGraphicsPipeline(dev_->raw(), pipeline_); }
        if (vert_) { SDL_ReleaseGPUShader(dev_->raw(), vert_); }
        if (frag_) { SDL_ReleaseGPUShader(dev_->raw(), frag_); }
        if (storage_) { SDL_ReleaseGPUBuffer(dev_->raw(), storage_); }
        if (xfer_) { SDL_ReleaseGPUTransferBuffer(dev_->raw(), xfer_); }
    }
    pipeline_ = nullptr;
    vert_ = nullptr;
    frag_ = nullptr;
    storage_ = nullptr;
    xfer_ = nullptr;
    dev_ = nullptr;
}

// ---- Upload -------------------------------------------------------------------

auto emitter_glow_pass::upload_instances(
    SDL_GPUCommandBuffer* cb, const std::vector<emitter_glow_instance>& insts) -> bool
{
    if (insts.empty()) { return false; }
    const Uint32 count = static_cast<Uint32>(std::min(insts.size(), size_t(MAX_INSTANCES)));
    const Uint32 bytes = count * sizeof(emitter_glow_instance);

    void* mapped = SDL_MapGPUTransferBuffer(dev_->raw(), xfer_, /*cycle=*/true);
    if (!mapped) {
        dbg(DL::Error) << "emitter_glow_pass: MapGPUTransferBuffer failed";
        return false;
    }
    std::memcpy(mapped, insts.data(), bytes);
    SDL_UnmapGPUTransferBuffer(dev_->raw(), xfer_);

    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    if (!cp) {
        dbg(DL::Error) << "emitter_glow_pass: copy pass begin failed";
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

auto emitter_glow_pass::record(const emitter_glow_record_options& opts) -> void
{
    if (!ready() || !opts.cb || !opts.target || opts.proj_w == 0 || opts.proj_h == 0) { return; }
    if (!opts.instances || opts.instances->empty()) { return; }

    const auto& instances = *opts.instances;
    const Uint32 count = static_cast<Uint32>(std::min(instances.size(), size_t(MAX_INSTANCES)));
    if (!upload_instances(opts.cb, instances)) { return; }

    struct alignas(16) glow_params {
        float proj_w;
        float proj_h;
        float pad0 = 0.f;
        float pad1 = 0.f;
    } params{ .proj_w = static_cast<float>( opts.proj_w ), .proj_h = static_cast<float>( opts.proj_h ) };

    SDL_GPUColorTargetInfo ct{};
    ct.texture = opts.target;
    ct.load_op = SDL_GPU_LOADOP_LOAD; // additive on top of the already-composited scene
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(opts.cb, &ct, 1, nullptr);
    if (!rp) {
        dbg(DL::Error) << "emitter_glow_pass: render pass begin failed: " << SDL_GetError();
        return;
    }
    SDL_BindGPUGraphicsPipeline(rp, pipeline_);
    SDL_BindGPUVertexStorageBuffers(rp, 0, &storage_, 1);
    SDL_PushGPUVertexUniformData(opts.cb, 0, &params, sizeof(params));
    // Draw 6 vertices (unit quad) x N instances.
    SDL_DrawGPUPrimitives(rp, /*vertex_count=*/6, /*instance_count=*/count,
                          /*first_vertex=*/0, /*first_instance=*/0);
    SDL_EndGPURenderPass(rp);
}

} // namespace lighting
