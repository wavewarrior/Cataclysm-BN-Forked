#include "rain_effect.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"
#include "rng.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

// ---- Quad instance layout (matches rain_droplet.vert.hlsl SpriteInstance) --
// Shared by both droplets (streaks) and splash rings — same procedural quad
// vertex shader, only the fragment shader differs.
struct quad_instance {
    float dst_x;     // quad top-left X (screen px)
    float dst_y;     // quad top-left Y (screen px)
    float dst_w;     // quad width
    float dst_h;     // quad height
    float src_u;     // unused
    float src_v;     // unused
    float src_uw;    // unused
    float src_vh;    // unused
    float tint_r;    // colour R
    float tint_g;    // colour G
    float tint_b;    // colour B
    float tint_a;    // colour A (opacity / lifetime fade)
    float rotation;  // tilt (radians)
    float light_mul; // unused
    float pad1;      // unused
    float pad2;      // unused
};
static_assert(sizeof(quad_instance) == 64, "quad_instance must be 64 bytes (wire-stable with vert "
                                           "shader)");

// ---- Constructor / Destructor --------------------------------------------

rain_effect::~rain_effect() { shutdown(); }

// ---- Pipeline helper ------------------------------------------------------

static SDL_GPUGraphicsPipeline* make_quad_pipeline(
    SDL_GPUDevice* dev, SDL_GPUShader* vert, SDL_GPUShader* frag, SDL_GPUTextureFormat fmt) {
    // Both rain pipelines emit PREMULTIPLIED alpha (col*alpha, alpha), so the
    // source factor is ONE (using SRC_ALPHA would apply alpha twice → dim).
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

// ---- Init ---------------------------------------------------------------

bool rain_effect::init(
    gpu_device& dev, SDL_GPUTextureFormat hdr_format, std::uint32_t screen_w,
    std::uint32_t screen_h) {
    shutdown();
    dev_ = &dev;
    hdr_format_ = hdr_format;
    (void)screen_w; // droplets are world-tile-targeted now, not screen-spawned
    (void)screen_h;

    if (!dev.ready()) {
        dbg(DL::Error) << "rain_effect::init: gpu_device not ready";
        return false;
    }

    dbg(DL::Info) << "rain_effect: init called (screen=" << screen_w << "x" << screen_h << ")";
    init_shader_compiler();

    // ---- Shaders ---------------------------------------------------------
    const std::string vert_src = load_lighting_shader_source("rain_droplet.vert.hlsl");
    const std::string droplet_src = load_lighting_shader_source("rain_droplet.frag.hlsl");
    const std::string splash_src = load_lighting_shader_source("rain_splash.frag.hlsl");
    if (vert_src.empty() || droplet_src.empty() || splash_src.empty()) {
        dbg(DL::Error) << "rain_effect: failed to load rain shader source";
        return false;
    }

    auto v = compile_graphics_shader(
        dev, vert_src, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "rain_droplet.vert");
    auto fd = compile_graphics_shader(
        dev, droplet_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "rain_droplet.frag");
    auto fs = compile_graphics_shader(
        dev, splash_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "rain_splash.frag");
    if (!v || !fd || !fs) {
        dbg(DL::Error) << "rain_effect: rain shader compile failed";
        return false;
    }
    quad_vert_ = v.shader;
    droplet_frag_ = fd.shader;
    splash_frag_ = fs.shader;

    droplet_pipeline_ = make_quad_pipeline(dev.raw(), quad_vert_, droplet_frag_, hdr_format_);
    splash_pipeline_ = make_quad_pipeline(dev.raw(), quad_vert_, splash_frag_, hdr_format_);
    if (!droplet_pipeline_ || !splash_pipeline_) {
        dbg(DL::Error) << "rain_effect: rain pipeline create failed";
        return false;
    }

    // ---- Particle pools --------------------------------------------------
    droplets_.reserve(MAX_DROPLETS);
    splashes_.reserve(MAX_SPLASHES);

    // ---- Persistent instance buffers (transfer + storage, reused each frame) --
    auto make_buffers =
        [&](SDL_GPUTransferBuffer*& xfer, SDL_GPUBuffer*& storage, std::size_t count) -> bool {
        const Uint32 bytes = static_cast<Uint32>(count * sizeof(quad_instance));
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        bci.size = bytes;
        storage = SDL_CreateGPUBuffer(dev.raw(), &bci);

        SDL_GPUTransferBufferCreateInfo tbi{};
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbi.size = bytes;
        xfer = SDL_CreateGPUTransferBuffer(dev.raw(), &tbi);
        return storage != nullptr && xfer != nullptr;
    };
    if (!make_buffers(droplet_xfer_, droplet_storage_, MAX_DROPLETS)
        || !make_buffers(splash_xfer_, splash_storage_, MAX_SPLASHES)) {
        dbg(DL::Error) << "rain_effect: instance buffer create failed";
        // Release the buffer that DID succeed before returning false.
        if (droplet_storage_) {
            SDL_ReleaseGPUBuffer(dev_->raw(), droplet_storage_);
            droplet_storage_ = nullptr;
        }
        if (droplet_xfer_) {
            SDL_ReleaseGPUTransferBuffer(dev_->raw(), droplet_xfer_);
            droplet_xfer_ = nullptr;
        }
        if (splash_storage_) {
            SDL_ReleaseGPUBuffer(dev_->raw(), splash_storage_);
            splash_storage_ = nullptr;
        }
        if (splash_xfer_) {
            SDL_ReleaseGPUTransferBuffer(dev_->raw(), splash_xfer_);
            splash_xfer_ = nullptr;
        }
        return false;
    }

    DebugLogFL(DL::Info, DC::Main)
        << "rain_effect: initialised (droplet_cap=" << MAX_DROPLETS
        << ", splash_cap=" << MAX_SPLASHES << ")";
    return true;
}

// ---- Shutdown -----------------------------------------------------------

void rain_effect::shutdown() noexcept {
    if (dev_ && dev_->ready()) {
        SDL_ReleaseGPUGraphicsPipeline(dev_->raw(), droplet_pipeline_);
        SDL_ReleaseGPUGraphicsPipeline(dev_->raw(), splash_pipeline_);
        SDL_ReleaseGPUShader(dev_->raw(), quad_vert_);
        SDL_ReleaseGPUShader(dev_->raw(), droplet_frag_);
        SDL_ReleaseGPUShader(dev_->raw(), splash_frag_);
        SDL_ReleaseGPUBuffer(dev_->raw(), droplet_storage_);
        SDL_ReleaseGPUTransferBuffer(dev_->raw(), droplet_xfer_);
        SDL_ReleaseGPUBuffer(dev_->raw(), splash_storage_);
        SDL_ReleaseGPUTransferBuffer(dev_->raw(), splash_xfer_);
    }

    droplet_pipeline_ = nullptr;
    splash_pipeline_ = nullptr;
    quad_vert_ = nullptr;
    droplet_frag_ = nullptr;
    splash_frag_ = nullptr;
    droplet_storage_ = nullptr;
    droplet_xfer_ = nullptr;
    splash_storage_ = nullptr;
    splash_xfer_ = nullptr;
    dev_ = nullptr;
    droplets_.clear();
    splashes_.clear();
}

// ---- Particle management ------------------------------------------------

void rain_effect::add_drop(float wx, float wy, float opacity) {
    if (static_cast<int>(droplets_.size()) >= MAX_DROPLETS) { return; }
    // Short fall (~1.3 tiles, jittered) so the streak appears just above its
    // landing tile and visibly drops onto it — in top-down ortho a long fall
    // reads as sliding across the ground rather than falling from the sky.
    const float fall0 = 1.0f + static_cast<float>(rng(0, 60)) / 100.f; // tiles
    rain_droplet d{};
    d.world_x = wx;
    d.world_y = wy;
    d.fall0 = fall0;
    d.fall = fall0;
    d.opacity = opacity;
    droplets_.push_back(d);
}

void rain_effect::add_splash(float wx, float wy, float intensity) {
    if (static_cast<int>(splashes_.size()) >= MAX_SPLASHES) { return; }
    splashes_.push_back(rain_splash{
        wx,
        wy,
        intensity,
        0u,
        8u + static_cast<uint32_t>(rng(0, 4)),
    });
}

void rain_effect::update_splashes() {
    auto it = splashes_.begin();
    while (it != splashes_.end()) {
        ++it->age;
        if (it->age >= it->max_age) {
            it = splashes_.erase(it);
        } else {
            ++it;
        }
    }
}

template <typename Inst>
bool rain_effect::upload_instances(
    SDL_GPUCommandBuffer* cb, SDL_GPUTransferBuffer* xfer, SDL_GPUBuffer* storage,
    const std::vector<Inst>& insts) {
    if (insts.empty()) { return false; }
    const Uint32 bytes = static_cast<Uint32>(insts.size() * sizeof(Inst));
    void* mapped = SDL_MapGPUTransferBuffer(dev_->raw(), xfer, /*cycle=*/true);
    if (!mapped) {
        dbg(DL::Error) << "rain_effect: MapGPUTransferBuffer failed: " << SDL_GetError();
        return false;
    }
    std::memcpy(mapped, insts.data(), bytes);
    SDL_UnmapGPUTransferBuffer(dev_->raw(), xfer);

    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    if (!cp) {
        dbg(DL::Error) << "rain_effect: copy pass begin failed";
        return false;
    }
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = xfer;
    src.offset = 0;
    SDL_GPUBufferRegion dst{};
    dst.buffer = storage;
    dst.offset = 0;
    dst.size = bytes;
    SDL_UploadToGPUBuffer(cp, &src, &dst, /*cycle=*/true);
    SDL_EndGPUCopyPass(cp);
    return true;
}

// ---- Per-frame record ---------------------------------------------------

void rain_effect::record(
    SDL_GPUCommandBuffer* cb, SDL_GPUTexture* world_tex, std::uint32_t world_w,
    std::uint32_t world_h, const rain_params& params) {
    if (!ready() || !cb || !world_tex || world_w == 0 || world_h == 0) { return; }

    const float tp = params.tile_pixel_size > 0.f ? params.tile_pixel_size : 32.f;
    const float wind_rad = params.wind_angle * 3.14159265f / 180.f;
    const float drop_rot = std::sin(-wind_rad) * 0.3f;

    // ---- Advance drops + build streak instances (WORLD-targeted) ---------
    // Each drop falls (in tiles) toward its landing tile; on impact it dies and
    // spawns a splash ring there, so the streak you see is the splash you see.
    constexpr float DESCENT = 0.13f;   // tiles/frame
    constexpr float LEAN_TILES = 0.5f; // wind drift at spawn, straightens on impact
    std::vector<quad_instance> droplet_inst;
    droplet_inst.reserve(droplets_.size());
    auto it = droplets_.begin();
    while (it != droplets_.end()) {
        rain_droplet& d = *it;
        const float landing_x = (d.world_x + params.camera_off_x) * tp;
        const float landing_y = (d.world_y + params.camera_off_y) * tp;
        const float prog = d.fall0 > 0.f ? d.fall / d.fall0 : 0.f; // 1=spawn → 0=impact
        const float lean = std::sin(-wind_rad) * LEAN_TILES * tp * prog;
        const float sx = landing_x + lean;
        const float sy = landing_y - d.fall * tp; // fall in tiles → px above landing

        // Streak: thin, elongated; only draw if roughly on-screen.
        const bool on_screen =
            sx > -16.f && sy > -32.f && sx < static_cast<float>(world_w) + 16.f
            && sy < static_cast<float>(world_h) + 16.f;
        if (on_screen) {
            quad_instance qi{};
            qi.dst_x = sx;
            qi.dst_y = sy;
            qi.dst_w = 1.6f;
            qi.dst_h = 7.f + d.opacity * 7.f;
            qi.src_uw = 1.f;
            qi.src_vh = 1.f;
            qi.tint_r = 0.6f;
            qi.tint_g = 0.75f;
            qi.tint_b = 0.9f;
            qi.tint_a = d.opacity;
            qi.rotation = drop_rot;
            droplet_inst.push_back(qi);
        }

        d.fall -= DESCENT;
        if (d.fall <= 0.f) {
            add_splash(d.world_x, d.world_y, d.opacity); // ring at the landing tile
            it = droplets_.erase(it);
        } else {
            ++it;
        }
    }

    update_splashes();

    // ---- Build splash-ring instances (WORLD-locked, projected) -----------
    std::vector<quad_instance> splash_inst;
    splash_inst.reserve(splashes_.size());
    for (const auto& s : splashes_) {
        // screen_px = (world_tile + camera_off) * tile_px  — mirrors sprite.vert.
        const float sx = (s.world_x + params.camera_off_x) * tp;
        const float sy = (s.world_y + params.camera_off_y) * tp;
        const float life =
            static_cast<float>(s.age) / static_cast<float>(std::max<uint32_t>(1u, s.max_age));
        // Small, quick ground ripple: grows ~0.18→0.6 tiles and fades fast.
        // (Large/bright rings read as floating bubbles, not rain on the ground.)
        const float diam = tp * (0.18f + 0.42f * life);
        const float fade = (1.0f - life) * s.intensity * 0.3f;
        if (fade <= 0.01f) { continue; }
        // Cheap off-screen cull.
        if (sx < -diam || sy < -diam || sx > static_cast<float>(world_w) + diam
            || sy > static_cast<float>(world_h) + diam) {
            continue;
        }
        quad_instance qi{};
        qi.dst_x = sx - diam * 0.5f;
        qi.dst_y = sy - diam * 0.5f;
        qi.dst_w = diam;
        qi.dst_h = diam;
        qi.src_uw = 1.f;
        qi.src_vh = 1.f;
        qi.tint_r = 0.55f;
        qi.tint_g = 0.7f;
        qi.tint_b = 0.95f;
        qi.tint_a = fade;
        splash_inst.push_back(qi);
    }

    if (droplet_inst.empty() && splash_inst.empty()) { return; }

    // ---- Upload instances (copy passes, before the render pass) ----------
    const bool have_droplets = upload_instances(cb, droplet_xfer_, droplet_storage_, droplet_inst);
    const bool have_splashes = upload_instances(cb, splash_xfer_, splash_storage_, splash_inst);

    // ---- One render pass on world_target ---------------------------------
    SDL_GPUColorTargetInfo ct{};
    ct.texture = world_tex;
    ct.load_op = SDL_GPU_LOADOP_LOAD; // preserve the lit scene
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cb, &ct, 1, nullptr);
    if (!rp) {
        dbg(DL::Error) << "rain_effect: render pass begin failed";
        return;
    }

    struct {
        float tw;
        float th;
        uint32_t base;
        uint32_t pad;
    } fp;
    fp.tw = static_cast<float>(world_w);
    fp.th = static_cast<float>(world_h);
    fp.base = 0u;
    fp.pad = 0u;
    SDL_PushGPUVertexUniformData(cb, 0, &fp, sizeof(fp));

    // Splash rings first (ground impacts), then droplets on top (falling).
    if (have_splashes) {
        SDL_BindGPUGraphicsPipeline(rp, splash_pipeline_);
        SDL_BindGPUVertexStorageBuffers(rp, 0, &splash_storage_, 1);
        SDL_DrawGPUPrimitives(rp, 6, static_cast<Uint32>(splash_inst.size()), 0, 0);
    }
    if (have_droplets) {
        SDL_BindGPUGraphicsPipeline(rp, droplet_pipeline_);
        SDL_BindGPUVertexStorageBuffers(rp, 0, &droplet_storage_, 1);
        SDL_DrawGPUPrimitives(rp, 6, static_cast<Uint32>(droplet_inst.size()), 0, 0);
    }
    SDL_EndGPURenderPass(rp);

    static int log_throttle = 0;
    if (++log_throttle >= 120) {
        log_throttle = 0;
        dbg(DL::Info) << "rain_effect: droplets=" << droplet_inst.size()
                      << " splashes=" << splash_inst.size() << " (pool d=" << droplets_.size()
                      << " s=" << splashes_.size() << ")";
    }
}

} // namespace lighting
