#include "tonemap_pass.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"

#include <string>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

tonemap_pass::~tonemap_pass() { shutdown(); }

bool tonemap_pass::init(gpu_device& dev, SDL_GPUTextureFormat dst_format) {
    shutdown();
    dev_ = &dev;
    if (!dev.ready()) {
        dbg(DL::Error) << "tonemap_pass::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();

    const std::string vert_src = load_lighting_shader_source("tonemap.vert.hlsl");
    const std::string frag_src = load_lighting_shader_source("tonemap.frag.hlsl");
    auto v = compile_graphics_shader(
        dev, vert_src, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "tonemap.vert");
    if (!v) {
        dbg(DL::Error) << "tonemap_pass: vert shader compile failed";
        return false;
    }
    auto f = compile_graphics_shader(
        dev, frag_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "tonemap.frag");
    if (!f) {
        SDL_ReleaseGPUShader(dev.raw(), v.shader);
        dbg(DL::Error) << "tonemap_pass: frag shader compile failed";
        return false;
    }
    vert_ = v.shader;
    frag_ = f.shader;

    // Opaque overwrite — the fullscreen triangle fully covers the target.
    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = false;
    blend.color_write_mask =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B
        | SDL_GPU_COLORCOMPONENT_A;

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = dst_format;
    color_target.blend_state = blend;

    // No vertex input: the vertex shader synthesises the triangle from
    // SV_VertexID (vertex_input_state left zeroed → 0 buffers / 0 attributes).
    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert_;
    pci.fragment_shader = frag_;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.target_info.num_color_targets = 1;
    pci.target_info.color_target_descriptions = &color_target;
    pci.target_info.has_depth_stencil_target = false;
    pci.props = 0;

    // Phase 1a spike: log what shadercross reflected the frag as. The gate is
    // only meaningful if the probe was classified as a storage texture (1) and
    // SrcTex stayed the sole sampler (1) — if the probe instead bumped samplers
    // to 2 it reflected as a sampled image (the known Metal sampler-zero path),
    // and a black screen would be a false negative, not a real gate failure.
    pipeline_ = SDL_CreateGPUGraphicsPipeline(dev.raw(), &pci);
    if (!pipeline_) {
        dbg(DL::Error) << "tonemap_pass pipeline: " << SDL_GetError();
        return false;
    }
    return true;
}

void tonemap_pass::shutdown() noexcept {
    if (dev_ && dev_->ready()) {
        if (pipeline_) { SDL_ReleaseGPUGraphicsPipeline(dev_->raw(), pipeline_); }
        if (vert_) { SDL_ReleaseGPUShader(dev_->raw(), vert_); }
        if (frag_) { SDL_ReleaseGPUShader(dev_->raw(), frag_); }
    }
    pipeline_ = nullptr;
    vert_ = nullptr;
    frag_ = nullptr;
}

void tonemap_pass::record(
    SDL_GPUCommandBuffer* cb, SDL_GPUTexture* src, SDL_GPUSampler* sampler, SDL_GPUTexture* dst,
    std::uint32_t dst_w, std::uint32_t dst_h, float exposure, float min_ev, float max_ev,
    const grade_params& grade) {
    if (!pipeline_ || !cb || !src || !sampler || !dst || dst_w == 0 || dst_h == 0) { return; }

    // Fragment uniform (b0/space3): exposure + EV range + 4B pad → 16B.
    struct TonemapParams {
        float exposure;
        float min_ev;
        float max_ev;
        float pad0;
    } params{exposure, min_ev, max_ev, 0.0f};
    SDL_PushGPUFragmentUniformData(cb, /*slot=*/0, &params, sizeof(params));
    // Fragment uniform (b1/space3): ASC-CDL grade + post effects → 64B.
    SDL_PushGPUFragmentUniformData(cb, /*slot=*/1, &grade, sizeof(grade));

    SDL_GPUColorTargetInfo ct{};
    ct.texture = dst;
    // The triangle covers the whole target every frame → no need to preserve or
    // clear prior contents.
    ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.cycle = false;

    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cb, &ct, 1, nullptr);
    if (!rp) {
        dbg(DL::Error) << "tonemap_pass: BeginGPURenderPass failed: " << SDL_GetError();
        return;
    }

    SDL_BindGPUGraphicsPipeline(rp, pipeline_);

    const SDL_GPUViewport
        vp{0.0f, 0.0f, static_cast<float>(dst_w), static_cast<float>(dst_h), 0.0f, 1.0f};
    SDL_SetGPUViewport(rp, &vp);

    SDL_GPUTextureSamplerBinding tsb{};
    tsb.texture = src;
    tsb.sampler = sampler;
    SDL_BindGPUFragmentSamplers(rp, /*first_slot=*/0, &tsb, 1);

    SDL_DrawGPUPrimitives(
        rp, /*num_vertices=*/3, /*num_instances=*/1,
        /*first_vertex=*/0, /*first_instance=*/0);

    SDL_EndGPURenderPass(rp);
}

} // namespace lighting
