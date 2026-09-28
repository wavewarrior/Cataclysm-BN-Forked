#include "dust_mote_effect.h"

#include "debug.h"
#include "gpu_device.h"
#include "shader_compiler.h"

#include <algorithm>
#include <cstring>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

namespace {

// Cheap xorshift32 — this is a purely cosmetic particle scatter, not gameplay
// RNG, so it does not need to route through the game's seeded rng.h.
auto xorshift_unit(std::uint32_t& state) -> float {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state) / 4294967295.0f;
}

} // namespace

// ---- Constructor / Destructor ----------------------------------------------

dust_mote_effect::~dust_mote_effect() { shutdown(); }

// ---- Pipeline helper -------------------------------------------------------

static auto make_dust_mote_pipeline(
    SDL_GPUDevice* dev, SDL_GPUShader* vert, SDL_GPUShader* frag, SDL_GPUTextureFormat fmt)
    -> SDL_GPUGraphicsPipeline* {
    // Premultiplied alpha (col*alpha, alpha) — same convention as
    // rain_effect's droplet/splash pipelines: source factor ONE, since using
    // SRC_ALPHA on already-premultiplied colour would apply alpha twice.
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

// ---- Init -------------------------------------------------------------------

auto dust_mote_effect::init(gpu_device& dev, SDL_GPUTextureFormat hdr_format) -> bool {
    shutdown();
    dev_ = &dev;
    hdr_format_ = hdr_format;
    if (!dev.ready()) {
        dbg(DL::Error) << "dust_mote_effect::init: gpu_device not ready";
        return false;
    }

    const std::string vert_src = load_lighting_shader_source("dust_mote.vert.hlsl");
    const std::string frag_src = load_lighting_shader_source("dust_mote.frag.hlsl");
    if (vert_src.empty() || frag_src.empty()) {
        dbg(DL::Error) << "dust_mote_effect: failed to load shader source";
        return false;
    }
    auto v = compile_graphics_shader(
        dev, vert_src, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "dust_mote.vert");
    if (!v.shader) {
        dbg(DL::Error) << "dust_mote_effect: vertex compile failed";
        return false;
    }
    auto f = compile_graphics_shader(
        dev, frag_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "dust_mote.frag");
    if (!f.shader) {
        dbg(DL::Error) << "dust_mote_effect: fragment compile failed";
        return false;
    }
    vert_ = v.shader;
    frag_ = f.shader;

    pipeline_ = make_dust_mote_pipeline(dev.raw(), vert_, frag_, hdr_format);
    if (!pipeline_) {
        dbg(DL::Error) << "dust_mote_effect: pipeline create failed: " << SDL_GetError();
        return false;
    }

    const Uint32 bytes = static_cast<Uint32>(MAX_MOTES * sizeof(dust_mote_instance));
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bci.size = bytes;
    storage_ = SDL_CreateGPUBuffer(dev.raw(), &bci);

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = bytes;
    xfer_ = SDL_CreateGPUTransferBuffer(dev.raw(), &tbi);

    if (!storage_ || !xfer_) {
        dbg(DL::Error) << "dust_mote_effect: instance buffer create failed";
        if (storage_) {
            SDL_ReleaseGPUBuffer(dev.raw(), storage_);
            storage_ = nullptr;
        }
        if (xfer_) {
            SDL_ReleaseGPUTransferBuffer(dev.raw(), xfer_);
            xfer_ = nullptr;
        }
        return false;
    }

    motes_.clear();
    motes_.reserve(MAX_MOTES);
    DebugLogFL(DL::Info, DC::Main) << "dust_mote_effect: initialised (cap=" << MAX_MOTES << ")";
    return true;
}

// ---- Shutdown ---------------------------------------------------------------

auto dust_mote_effect::shutdown() noexcept -> void {
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
    motes_.clear();
}

// ---- Update + spawn ---------------------------------------------------------

auto dust_mote_effect::update_and_spawn(
    const dust_mote_params& params, const std::vector<dust_mote_shaft_source>* active_shafts)
    -> void {
    const float dt = std::max(0.f, params.dt);

    // Age + drift existing motes; drop expired ones.
    for (auto it = motes_.begin(); it != motes_.end();) {
        it->age += dt;
        if (it->age >= it->max_age) {
            it = motes_.erase(it);
            continue;
        }
        it->world_x += it->vx * dt;
        it->world_y += it->vy * dt;
        ++it;
    }

    if (!active_shafts || active_shafts->empty() || dt <= 0.f || params.density <= 0.f) { return; }
    if (static_cast<int>(motes_.size()) >= MAX_MOTES) { return; }

    // Spawn rate proportional to density * dt * total shaft length, so a
    // longer/more-numerous set of visible windows gets proportionally more
    // motes rather than a fixed count regardless of scene content.
    float total_len = 0.f;
    for (const auto& s : *active_shafts) { total_len += std::max(0.f, s.length_tiles); }
    if (total_len <= 0.f) { return; }

    constexpr float SPAWN_RATE_PER_TILE = 1.2f; // motes/second per tile of shaft length, at
                                                // density=1
    const float expected = params.density * SPAWN_RATE_PER_TILE * total_len * dt;
    int to_spawn = static_cast<int>(expected);
    const float frac = expected - static_cast<float>(to_spawn);
    if (xorshift_unit(rng_state_) < frac) { ++to_spawn; }

    for (int i = 0; i < to_spawn && static_cast<int>(motes_.size()) < MAX_MOTES; ++i) {
        // Pick a shaft weighted by its length.
        float pick = xorshift_unit(rng_state_) * total_len;
        const dust_mote_shaft_source* src = &active_shafts->front();
        for (const auto& s : *active_shafts) {
            pick -= std::max(0.f, s.length_tiles);
            if (pick <= 0.f) {
                src = &s;
                break;
            }
        }

        const float t = xorshift_unit(rng_state_);
        dust_mote m;
        m.world_x = src->world_x + src->dir_x * (t * src->length_tiles);
        m.world_y = src->world_y + src->dir_y * (t * src->length_tiles);
        // Slow Brownian drift plus a faint bias "rising in the light" —
        // opposite the sun's travel direction, so motes drift upward through
        // the beam like real dust catching an updraft, not falling with it.
        const float rise = params.drift * 0.2f;
        m.vx = (xorshift_unit(rng_state_) - 0.5f) * params.drift * 0.4f - params.sun_dir_x * rise;
        m.vy = (xorshift_unit(rng_state_) - 0.5f) * params.drift * 0.4f - params.sun_dir_y * rise;
        m.age = 0.f;
        m.max_age = 3.f + xorshift_unit(rng_state_) * 3.f;
        m.brightness = src->strength;
        m.r = src->r;
        m.g = src->g;
        m.b = src->b;
        motes_.push_back(m);
    }
}

// ---- Upload -------------------------------------------------------------------

auto dust_mote_effect::upload_instances(SDL_GPUCommandBuffer* cb, const dust_mote_params& params)
    -> bool {
    uploaded_count_ = 0;
    if (motes_.empty()) { return false; }

    std::vector<dust_mote_instance> insts;
    insts.reserve(motes_.size());
    const float base_radius_tiles = 0.08f; // small speck, not a blob
    for (const auto& m : motes_) {
        // Triangular fade envelope: in over the first 20% of life, out over
        // the last 30%, full brightness in between.
        const float life_frac = m.max_age > 0.f ? m.age / m.max_age : 1.f;
        float fade = 1.f;
        if (life_frac < 0.2f) {
            fade = life_frac / 0.2f;
        } else if (life_frac > 0.7f) {
            fade = std::max(0.f, (1.f - life_frac) / 0.3f);
        }
        const float alpha = std::clamp(fade * m.brightness * 0.6f, 0.f, 1.f);
        if (alpha <= 0.003f) { continue; }
        const float sx = (m.world_x + params.camera_off_x) * params.tile_pixel_size;
        const float sy = (m.world_y + params.camera_off_y) * params.tile_pixel_size;
        insts.push_back(
            {.cx = sx,
             .cy = sy,
             .radius_px = base_radius_tiles * params.size * params.tile_pixel_size,
             .r = m.r * alpha,
             .g = m.g * alpha,
             .b = m.b * alpha,
             .alpha = alpha,
             .pad0 = 0.f});
    }
    if (insts.empty()) { return false; }

    const Uint32 count = static_cast<Uint32>(std::min(insts.size(), size_t(MAX_MOTES)));
    const Uint32 bytes = count * sizeof(dust_mote_instance);

    void* mapped = SDL_MapGPUTransferBuffer(dev_->raw(), xfer_, /*cycle=*/true);
    if (!mapped) {
        dbg(DL::Error) << "dust_mote_effect: MapGPUTransferBuffer failed";
        return false;
    }
    std::memcpy(mapped, insts.data(), bytes);
    SDL_UnmapGPUTransferBuffer(dev_->raw(), xfer_);

    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    if (!cp) {
        dbg(DL::Error) << "dust_mote_effect: copy pass begin failed";
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
    uploaded_count_ = count;
    return true;
}

// ---- Per-frame record -----------------------------------------------------

auto dust_mote_effect::record(
    SDL_GPUCommandBuffer* cb, SDL_GPUTexture* world_tex, std::uint32_t world_w,
    std::uint32_t world_h, const dust_mote_params& params,
    const std::vector<dust_mote_shaft_source>* active_shafts) -> void {
    if (!ready() || !cb || !world_tex || world_w == 0 || world_h == 0) { return; }

    update_and_spawn(params, active_shafts);
    if (!upload_instances(cb, params)) { return; }

    struct alignas(16) mote_params {
        float proj_w;
        float proj_h;
        float pad0 = 0.f;
        float pad1 = 0.f;
    } gp{.proj_w = params.proj_w > 0.f ? params.proj_w : static_cast<float>(world_w),
         .proj_h = params.proj_h > 0.f ? params.proj_h : static_cast<float>(world_h)};

    SDL_GPUColorTargetInfo ct{};
    ct.texture = world_tex;
    ct.load_op = SDL_GPU_LOADOP_LOAD; // additive on top of the already-composited scene
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cb, &ct, 1, nullptr);
    if (!rp) {
        dbg(DL::Error) << "dust_mote_effect: render pass begin failed: " << SDL_GetError();
        return;
    }
    SDL_BindGPUGraphicsPipeline(rp, pipeline_);
    SDL_BindGPUVertexStorageBuffers(rp, 0, &storage_, 1);
    SDL_PushGPUVertexUniformData(cb, 0, &gp, sizeof(gp));
    SDL_DrawGPUPrimitives(
        rp, /*vertex_count=*/6, /*instance_count=*/uploaded_count_,
        /*first_vertex=*/0, /*first_instance=*/0);
    SDL_EndGPURenderPass(rp);
}

} // namespace lighting
