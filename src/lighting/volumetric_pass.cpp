#include "volumetric_pass.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"

#include <string>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

static_assert(sizeof(vol_params) == 84, "vol_params wire-stable with the VolParams cbuffer in "
                                        "vol.frag.hlsl");

// Fullscreen-tri pipeline writing into hdr_format with ADDITIVE blend (ONE/ONE)
// so the sun-shaft haze accumulates on top of the freshly-drawn world_target.
static SDL_GPUGraphicsPipeline* make_pipeline(
    SDL_GPUDevice* d, SDL_GPUShader* vert, SDL_GPUShader* frag, SDL_GPUTextureFormat fmt) {
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
    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert;
    pci.fragment_shader = frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.target_info.num_color_targets = 1;
    pci.target_info.color_target_descriptions = &color_target;
    pci.target_info.has_depth_stencil_target = false;
    return SDL_CreateGPUGraphicsPipeline(d, &pci);
}

volumetric_pass::~volumetric_pass() { shutdown(); }

bool volumetric_pass::init(gpu_device& dev, SDL_GPUTextureFormat hdr_format) {
    shutdown();
    dev_ = &dev;
    hdr_format_ = hdr_format;
    if (!dev.ready()) {
        dbg(DL::Error) << "volumetric_pass::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();
    const std::string vert_src = load_lighting_shader_source("tonemap.vert.hlsl");
    const std::string frag_src = load_lighting_shader_source("vol.frag.hlsl");
    auto v = compile_graphics_shader(
        dev, vert_src, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "vol.vert");
    auto f = compile_graphics_shader(
        dev, frag_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "vol.frag");
    if (!v || !f) {
        if (v) { SDL_ReleaseGPUShader(dev.raw(), v.shader); }
        if (f) { SDL_ReleaseGPUShader(dev.raw(), f.shader); }
        dbg(DL::Error) << "volumetric_pass: shader compile failed";
        return false;
    }
    vert_ = v.shader;
    frag_ = f.shader;

    // Structural gate (DC::Main — DC::SDL is filtered): vol.frag is sampler-less
    // and reads SdfBuf (t0) + SkyVisBuf (t1) → must reflect storage_buffers=2.
    DebugLogFL(DL::Info, DC::Main)
        << "vol.frag reflection: samplers=" << f.resources.num_samplers
        << " storage_textures=" << f.resources.num_storage_textures << " storage_buffers="
        << f.resources.num_storage_buffers << " (expects samplers=0 storage_buffers=2)";

    pipeline_ = make_pipeline(dev.raw(), vert_, frag_, hdr_format_);
    if (!pipeline_) {
        DebugLogFL(DL::Error, DC::Main) << "volumetric_pass pipeline: " << SDL_GetError();
        return false;
    }
    return true;
}

void volumetric_pass::shutdown() noexcept {
    if (dev_ && dev_->ready()) {
        if (pipeline_) { SDL_ReleaseGPUGraphicsPipeline(dev_->raw(), pipeline_); }
        if (vert_) { SDL_ReleaseGPUShader(dev_->raw(), vert_); }
        if (frag_) { SDL_ReleaseGPUShader(dev_->raw(), frag_); }
    }
    pipeline_ = nullptr;
    vert_ = frag_ = nullptr;
}

void volumetric_pass::record(
    SDL_GPUCommandBuffer* cb, SDL_GPUTexture* hdr_tex, std::uint32_t full_w, std::uint32_t full_h,
    SDL_GPUBuffer* sdf_buf, SDL_GPUBuffer* skyvis_buf, const vol_params& params) {
    if (!ready() || !cb || !hdr_tex || !sdf_buf || !skyvis_buf || full_w == 0 || full_h == 0) {
        return;
    }

    SDL_GPUColorTargetInfo ct{};
    ct.texture = hdr_tex;
    ct.load_op = SDL_GPU_LOADOP_LOAD; // preserve the freshly-drawn world; add onto it
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cb, &ct, 1, nullptr);
    if (!rp) {
        dbg(DL::Error) << "volumetric pass: BeginGPURenderPass failed: " << SDL_GetError();
        return;
    }
    SDL_BindGPUGraphicsPipeline(rp, pipeline_);
    const SDL_GPUViewport
        vp{0.0f, 0.0f, static_cast<float>(full_w), static_cast<float>(full_h), 0.0f, 1.0f};
    SDL_SetGPUViewport(rp, &vp);
    SDL_PushGPUFragmentUniformData(cb, /*slot=*/0, &params, sizeof(params));
    SDL_GPUBuffer* sbufs[2] = {sdf_buf, skyvis_buf}; // t0, t1
    SDL_BindGPUFragmentStorageBuffers(rp, /*first_slot=*/0, sbufs, 2);
    SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
    SDL_EndGPURenderPass(rp);
}

} // namespace lighting
