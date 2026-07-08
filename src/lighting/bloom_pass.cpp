#include "bloom_pass.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"

#include <string>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

static constexpr SDL_GPUTextureFormat BLOOM_FORMAT = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

bloom_pass::~bloom_pass() { shutdown(); }

static SDL_GPUGraphicsPipeline* make_pipeline(
    SDL_GPUDevice* d, SDL_GPUShader* vert, SDL_GPUShader* frag, SDL_GPUTextureFormat fmt,
    bool additive) {
    SDL_GPUColorTargetBlendState blend{};
    if (additive) {
        blend.enable_blend = true;
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    } else {
        blend.enable_blend = false;
    }
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

bool bloom_pass::init(
    gpu_device& dev, SDL_GPUTextureFormat hdr_format, std::uint32_t full_w, std::uint32_t full_h) {
    shutdown();
    dev_ = &dev;
    hdr_format_ = hdr_format;
    if (!dev.ready()) {
        dbg(DL::Error) << "bloom_pass::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();
    const std::string vert_src = load_lighting_shader_source("tonemap.vert.hlsl");
    const std::string extract_src = load_lighting_shader_source("bloom_extract.frag.hlsl");
    const std::string blur_src = load_lighting_shader_source("bloom_blur.frag.hlsl");
    const std::string comp_src = load_lighting_shader_source("bloom_composite.frag.hlsl");
    auto v = compile_graphics_shader(
        dev, vert_src, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "bloom.vert");
    auto e = compile_graphics_shader(
        dev, extract_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "bloom_extract.frag");
    auto b = compile_graphics_shader(
        dev, blur_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "bloom_blur.frag");
    auto c = compile_graphics_shader(
        dev, comp_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "bloom_composite.frag");
    if (!v || !e || !b || !c) {
        if (v) { SDL_ReleaseGPUShader(dev.raw(), v.shader); }
        if (e) { SDL_ReleaseGPUShader(dev.raw(), e.shader); }
        if (b) { SDL_ReleaseGPUShader(dev.raw(), b.shader); }
        if (c) { SDL_ReleaseGPUShader(dev.raw(), c.shader); }
        dbg(DL::Error) << "bloom_pass: shader compile failed";
        return false;
    }
    vert_ = v.shader;
    extract_frag_ = e.shader;
    blur_frag_ = b.shader;
    composite_frag_ = c.shader;

    DebugLogFL(DL::Info, DC::Main)
        << "bloom shaders reflection: extract(s=" << e.resources.num_samplers
        << ") blur(s=" << b.resources.num_samplers << ") composite(s=" << c.resources.num_samplers
        << ") (each expects samplers=1)";

    extract_pipeline_ = make_pipeline(dev.raw(), vert_, extract_frag_, BLOOM_FORMAT, false);
    blur_pipeline_ = make_pipeline(dev.raw(), vert_, blur_frag_, BLOOM_FORMAT, false);
    composite_pipeline_ = make_pipeline(dev.raw(), vert_, composite_frag_, hdr_format_, true);
    if (!extract_pipeline_ || !blur_pipeline_ || !composite_pipeline_) {
        DebugLogFL(DL::Error, DC::Main) << "bloom_pass pipeline: " << SDL_GetError();
        return false;
    }

    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler(dev.raw(), &sci);
    if (!sampler_) {
        DebugLogFL(DL::Error, DC::Main) << "bloom_pass sampler: " << SDL_GetError();
        return false;
    }

    return create_textures(full_w, full_h);
}

bool bloom_pass::create_textures(std::uint32_t full_w, std::uint32_t full_h) {
    half_w_ = full_w > 1u ? full_w / 2u : 1u;
    half_h_ = full_h > 1u ? full_h / 2u : 1u;
    if (!SDL_GPUTextureSupportsFormat(
            dev_->raw(), BLOOM_FORMAT, SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        DebugLogFL(DL::Error, DC::Main) << "bloom_pass: RGBA16F COLOR_TARGET|SAMPLER unsupported";
        return false;
    }
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = BLOOM_FORMAT;
    tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = half_w_;
    tci.height = half_h_;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    bloom_a_ = SDL_CreateGPUTexture(dev_->raw(), &tci);
    bloom_b_ = SDL_CreateGPUTexture(dev_->raw(), &tci);
    if (!bloom_a_ || !bloom_b_) {
        DebugLogFL(DL::Error, DC::Main) << "bloom_pass: texture create: " << SDL_GetError();
        return false;
    }
    return true;
}

bool bloom_pass::resize(std::uint32_t full_w, std::uint32_t full_h) {
    const std::uint32_t hw = full_w > 1u ? full_w / 2u : 1u;
    const std::uint32_t hh = full_h > 1u ? full_h / 2u : 1u;
    if (bloom_a_ && bloom_b_ && hw == half_w_ && hh == half_h_) { return true; }
    if (dev_ && dev_->ready()) {
        if (bloom_a_) {
            SDL_ReleaseGPUTexture(dev_->raw(), bloom_a_);
            bloom_a_ = nullptr;
        }
        if (bloom_b_) {
            SDL_ReleaseGPUTexture(dev_->raw(), bloom_b_);
            bloom_b_ = nullptr;
        }
    }
    return create_textures(full_w, full_h);
}

void bloom_pass::shutdown() noexcept {
    if (dev_ && dev_->ready()) {
        if (extract_pipeline_) { SDL_ReleaseGPUGraphicsPipeline(dev_->raw(), extract_pipeline_); }
        if (blur_pipeline_) { SDL_ReleaseGPUGraphicsPipeline(dev_->raw(), blur_pipeline_); }
        if (composite_pipeline_) {
            SDL_ReleaseGPUGraphicsPipeline(dev_->raw(), composite_pipeline_);
        }
        if (vert_) { SDL_ReleaseGPUShader(dev_->raw(), vert_); }
        if (extract_frag_) { SDL_ReleaseGPUShader(dev_->raw(), extract_frag_); }
        if (blur_frag_) { SDL_ReleaseGPUShader(dev_->raw(), blur_frag_); }
        if (composite_frag_) { SDL_ReleaseGPUShader(dev_->raw(), composite_frag_); }
        if (sampler_) { SDL_ReleaseGPUSampler(dev_->raw(), sampler_); }
        if (bloom_a_) { SDL_ReleaseGPUTexture(dev_->raw(), bloom_a_); }
        if (bloom_b_) { SDL_ReleaseGPUTexture(dev_->raw(), bloom_b_); }
    }
    vert_ = extract_frag_ = blur_frag_ = composite_frag_ = nullptr;
    extract_pipeline_ = blur_pipeline_ = composite_pipeline_ = nullptr;
    sampler_ = nullptr;
    bloom_a_ = bloom_b_ = nullptr;
    half_w_ = half_h_ = 0;
}

// One fullscreen sub-pass: bind pipeline, sample `src` with the linear sampler,
// draw into `dst` at (dst_w × dst_h). load_op LOAD only for the additive
// composite (preserve the scene); DONT_CARE otherwise (the tri fully covers).
static void run_subpass(
    SDL_GPUCommandBuffer* cb, SDL_GPUGraphicsPipeline* pipe, SDL_GPUTexture* src,
    SDL_GPUSampler* smp, SDL_GPUTexture* dst, std::uint32_t dst_w, std::uint32_t dst_h, bool load) {
    SDL_GPUColorTargetInfo ct{};
    ct.texture = dst;
    ct.load_op = load ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cb, &ct, 1, nullptr);
    if (!rp) {
        dbg(DL::Error) << "bloom subpass: BeginGPURenderPass failed: " << SDL_GetError();
        return;
    }
    SDL_BindGPUGraphicsPipeline(rp, pipe);
    const SDL_GPUViewport
        vp{0.0f, 0.0f, static_cast<float>(dst_w), static_cast<float>(dst_h), 0.0f, 1.0f};
    SDL_SetGPUViewport(rp, &vp);
    SDL_GPUTextureSamplerBinding tsb{};
    tsb.texture = src;
    tsb.sampler = smp;
    SDL_BindGPUFragmentSamplers(rp, /*first_slot=*/0, &tsb, 1);
    SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
    SDL_EndGPURenderPass(rp);
}

void bloom_pass::record(
    SDL_GPUCommandBuffer* cb, SDL_GPUTexture* hdr_tex, std::uint32_t full_w, std::uint32_t full_h,
    float threshold, float intensity) {
    if (!ready() || !cb || !hdr_tex || full_w == 0 || full_h == 0) { return; }

    // 1. EXTRACT: world_target → bloom_a_ (half). Uniform: threshold.
    {
        struct {
            float threshold;
            float pad0, pad1, pad2;
        } u{threshold, 0, 0, 0};
        SDL_PushGPUFragmentUniformData(cb, 0, &u, sizeof(u));
        run_subpass(cb, extract_pipeline_, hdr_tex, sampler_, bloom_a_, half_w_, half_h_, false);
    }
    // 2. BLUR H: bloom_a_ → bloom_b_. Uniform: texel direction.
    {
        struct {
            float dx, dy, pad0, pad1;
        } u{1.0f / static_cast<float>(half_w_), 0.0f, 0, 0};
        SDL_PushGPUFragmentUniformData(cb, 0, &u, sizeof(u));
        run_subpass(cb, blur_pipeline_, bloom_a_, sampler_, bloom_b_, half_w_, half_h_, false);
    }
    // 3. BLUR V: bloom_b_ → bloom_a_.
    {
        struct {
            float dx, dy, pad0, pad1;
        } u{0.0f, 1.0f / static_cast<float>(half_h_), 0, 0};
        SDL_PushGPUFragmentUniformData(cb, 0, &u, sizeof(u));
        run_subpass(cb, blur_pipeline_, bloom_b_, sampler_, bloom_a_, half_w_, half_h_, false);
    }
    // 4. COMPOSITE: bloom_a_ (half) → hdr_tex (full), additive × intensity.
    {
        struct {
            float intensity;
            float pad0, pad1, pad2;
        } u{intensity, 0, 0, 0};
        SDL_PushGPUFragmentUniformData(cb, 0, &u, sizeof(u));
        run_subpass(cb, composite_pipeline_, bloom_a_, sampler_, hdr_tex, full_w, full_h, true);
    }
}

} // namespace lighting
