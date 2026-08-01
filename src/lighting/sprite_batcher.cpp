#include "sprite_batcher.h"

#include "debug.h"
#include "shader_compiler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

// The sprite vertex + fragment HLSL now lives on disk under
// data/shaders/lighting/src/{sprite.vert,sprite.frag}.hlsl and is loaded at
// runtime via load_lighting_shader_source() — single source of truth, no
// longer embedded here (LIGHTING_REWORK_PLAN.md step 0).

// FrameParams cbuffer payload pushed as a vertex uniform per draw segment.
// Layout must match the cbuffer in sprite.vert.hlsl exactly.
struct frame_params {
    float target_w;
    float target_h;
    Uint32 instance_base;
    Uint32 pad;
};
static_assert(sizeof(frame_params) == 16, "frame_params is wire-stable with vert shader");

// Phase 6/6b: per-frame lighting params (cbuffer slot 1, vertex shader).
// 48 bytes, wire-stable with the LightParams cbuffer in sprite.vert.hlsl /
// shadow.vert.hlsl. Grew 32→48 for the silhouette-shadow shear: shadow.vert
// needs the sun direction + cot(elevation) in the VERTEX stage, and the
// fragment-only sun_params (b1/space3) is not visible there. The three sun
// fields land in one new 16-byte row (no straddle); existing offsets unchanged,
// so the normal world/tile render reads identically.
struct light_params {
    float tile_pixel_size; // screen pixels per tile (e.g. 32.0)
    float current_z;       // player z-level
    Uint32 emitter_count;  // live entries in the emitter SSBO
    float ambient;         // base ambient (0=dark dungeon, 0.05-0.3=outdoors)
    // Phase 6b: camera offset (screen tile → map tile: map_pos = tile_tu - offset)
    float camera_off_x; // = tile_map_origin_px.x / tile_px + 0.5
    float camera_off_y;
    Uint32 sdf_map_w; // SDF/map width  in tiles
    Uint32 sdf_map_h; // SDF/map height in tiles (was lp_pad; same type, sizeof unchanged)
    // Silhouette-shadow shear (VERTEX stage; shadow.vert). Filled from sun_params.
    float sun_dir_x;    // shadow-fall direction (= away from sun, unit 2D)
    float sun_dir_y;    // world y-down; CPU/shader flip sign if shadows invert
    float sun_cot_elev; // cot(sun elevation), clamped; low sun → long shadow
    float lp_sun_pad;   // pad to 48 (one full float4 row)
};
static_assert(sizeof(light_params) == 48, "light_params wire-stable with LightParams cbuffer");

static_assert(sizeof(sun_params) == 48, "sun_params wire-stable with SunParams cbuffer");

// debug_params struct now lives in sprite_batcher.h so render_state.h can
// embed it by value in frame_light_inputs. Wire-stable layout enforced here.
static_assert(sizeof(debug_params) == 208, "debug_params wire-stable with DebugParams cbuffer");

// ---- 24h sun LUT -------------------------------------------------------
// Defined at file scope so MSVC won't complain about static-local in nested block.
namespace {
struct sun_lut_key {
    float hr, si, sr, sg, sb, sky_i, sky_r, sky_g, sky_b, elev;
};
// Daytime intensities are radiometric drivers, not a look preference: the shader
// multiplies sun_intensity by a Lambert term that is only sin_elev/sqrt(1+sin_elev^2)
// (0.45 at hour 8), so the old si=0.60 / sky_i=0.50 pair summed to just
//   sky(0.55,0.65,0.85)*0.50 + sun(1.0,0.80,0.50)*0.60*0.45 ~= 0.54
// i.e. a sunlit outdoor tile rendered at ~54% of its own albedo before the AgX
// tonemap — which is why mid-morning outdoors looked as dark as an interior.
// Daylight rows below are scaled so an open tile lands near 1.0 at hour 8 and
// slightly above 1.0 at noon (the HDR target + tonemap absorb the overshoot).
// Night rows (0, 5, 21, 24) are untouched, so nights stay as dark as before and
// the indoor/outdoor contrast the ambient floor provides is preserved.
static const sun_lut_key k_sun[] = {
    //  hr    si     sr     sg     sb    sky_i  sky_r  sky_g  sky_b  elev
    {0, 0.00f, 0.f, 0.f, 0.f, 0.03f, 0.05f, 0.05f, 0.15f, 0.f},
    {5, 0.00f, 0.f, 0.f, 0.f, 0.05f, 0.05f, 0.10f, 0.25f, 0.f},
    {6, 0.18f, 0.90f, 0.50f, 0.20f, 0.25f, 0.60f, 0.40f, 0.30f, 0.15f},
    {8, 0.95f, 1.00f, 0.80f, 0.50f, 0.85f, 0.55f, 0.65f, 0.85f, 0.50f},
    {12, 1.25f, 1.00f, 0.95f, 0.80f, 1.05f, 0.50f, 0.60f, 0.90f, 0.87f},
    {16, 1.10f, 1.00f, 0.90f, 0.60f, 0.95f, 0.50f, 0.60f, 0.85f, 0.70f},
    {19, 0.30f, 1.00f, 0.40f, 0.10f, 0.32f, 0.70f, 0.40f, 0.30f, 0.20f},
    {21, 0.00f, 0.f, 0.f, 0.f, 0.05f, 0.10f, 0.08f, 0.15f, 0.f},
    {24, 0.00f, 0.f, 0.f, 0.f, 0.03f, 0.05f, 0.05f, 0.15f, 0.f},
};
static constexpr int k_sun_n = static_cast<int>(sizeof(k_sun) / sizeof(k_sun[0]));
} // anonymous namespace

sun_params make_sun_params(float sun_hour) noexcept {
    int ki = 0;
    while (ki < k_sun_n - 2 && k_sun[ki + 1].hr <= sun_hour) { ++ki; }
    const auto &a = k_sun[ki], &b = k_sun[ki + 1];
    const float dt = (b.hr > a.hr) ? (sun_hour - a.hr) / (b.hr - a.hr) : 0.f;
    auto lp = [dt](float x, float y) { return x + dt * (y - x); };
    sun_params sp{};
    sp.sun_intensity = lp(a.si, b.si);
    sp.sun_r = lp(a.sr, b.sr);
    sp.sun_g = lp(a.sg, b.sg);
    sp.sun_b = lp(a.sb, b.sb);
    sp.sky_intensity = lp(a.sky_i, b.sky_i);
    sp.sky_r = lp(a.sky_r, b.sky_r);
    sp.sky_g = lp(a.sky_g, b.sky_g);
    sp.sky_b = lp(a.sky_b, b.sky_b);
    sp.sun_sin_elev = lp(a.elev, b.elev);
    // Sun horizontal direction (light→ground), a UNIT vector — the shader marches
    // trace_shadow with `toward_sun = -sun_dir` UN-normalized, so a non-unit dir
    // breaks the shadow step distance. Hour angle h: 6am=-pi/2, noon=0, 6pm=+pi/2.
    //   toward_sun = -sun_dir = (-sin h, cos h)  sweeps E(+x)→S(+y)→W(-x) and is
    //   NEVER the zero vector. The OLD (cos h, 0) collapsed to (0,0) at 6am/6pm
    //   (and was tiny near them) → degenerate normalize → the SDF sun-shadow
    //   vanished → flat, "blocky" sun shaped only by the tile sky_vis mask.
    // Grazing-vs-overhead is carried separately by sun_sin_elev (shader lambert),
    // so the horizontal dir stays unit at all hours. (y sign = world y-down; flip
    // sun_dir_y if morning shadows point the wrong way — cosmetic.)
    const float h = (sun_hour - 12.f) * 3.14159265f / 12.f;
    sp.sun_dir_x = static_cast<float>(sin(static_cast<double>(h)));
    sp.sun_dir_y = static_cast<float>(-cos(static_cast<double>(h)));
    sp.sp_pad = 0.f;
    return sp;
}

// ---- PIMPL body --------------------------------------------------------

class sprite_batcher_impl {
public:
    // Sized so a 1080p screen worth of tiles + UI elements has comfortable
    // headroom (a tile screen at the densest zoom is ~3-4 k instances).
    // 262144 × 64 bytes = 16 MB per ring slot (×3 = 48 MB).
    // Needed for 4K displays: minimap (~17K) + large terminal sidebar (~27K) +
    // tile sprites (~10K) easily exceed 65536 on high-res setups.
    static constexpr Uint32 MAX_INSTANCES = 262144;
    // SDL_GPU defaults to 2-3 frames in flight; 3 ring slots is enough to
    // avoid waiting for the GPU to finish reading the previous frame's
    // storage buffer before we overwrite it.
    static constexpr Uint32 RING_SLOTS = 3;

    gpu_device* dev = nullptr;
    pipeline_desc desc{};

    // One graphics pipeline per color-target format we render into — same
    // shaders + resource layout, only the target format differs (swapchain
    // 8-bit for UI/composite, RGBA16F for the HDR world target). Lazily
    // built by get_or_build_pipeline() the first time begin_pass sees a
    // format; un-stubs pipeline_desc.color_target_format
    // (LIGHTING_REWORK_PLAN.md step 1).
    std::map<SDL_GPUTextureFormat, SDL_GPUGraphicsPipeline*> pipelines;
    SDL_GPUShader* vert_shader = nullptr;
    SDL_GPUShader* frag_shader = nullptr;
    SDL_GPUSampler* default_sampler = nullptr;

    SDL_GPUBuffer* storage_bufs[RING_SLOTS] = {};
    SDL_GPUTransferBuffer* xfer_bufs[RING_SLOTS] = {};
    Uint32 cur_slot = 0;

    struct segment {
        SDL_GPUTexture* tex;
        SDL_GPUSampler* sampler;
        Uint32 start;
        Uint32 count;
        SDL_Rect scissor = {};
        bool has_scissor = false;
        // When false, end_pass pushes a zeroed light_params / sun_params
        // for this segment so the fragment shader skips the per-emitter
        // loop and the sun march (saves wasted GPU on HUD/UI fragments
        // whose lighting result is discarded by max(tint, gpu_total)).
        // Default true preserves tile-sprite behaviour.
        bool is_lit = true;
    };
    std::vector<sprite_instance> pending;
    std::vector<segment> segments;

    // Pending segment in progress (not yet pushed to `segments`).
    SDL_GPUTexture* bound_tex = nullptr;
    SDL_GPUSampler* bound_sampler = nullptr;
    Uint32 seg_start = 0;
    SDL_Rect bound_scissor = {};
    bool bound_has_scissor = false;
    // Lighting state of the segment currently being accumulated.
    // True = run full fragment-shader lighting; false = push zeroed
    // light_params + sun_params so the loop + sun march short-circuit.
    bool bound_is_lit = true;

    // Pass-scope state captured by begin_pass().
    SDL_GPUCommandBuffer* cur_cb = nullptr;
    SDL_GPUTexture* cur_target = nullptr;
    Uint32 cur_target_w = 0;
    Uint32 cur_target_h = 0;
    // Shader pixel→NDC projection extents. Equal to cur_target_w/h
    // when caller doesn't override (legacy); decoupled for HiDPI so
    // logical-coord UI draws fill the physical swapchain via the
    // larger viewport.
    Uint32 cur_proj_w = 0;
    Uint32 cur_proj_h = 0;
    // Color-target format of the current pass → selects which cached
    // pipeline end_pass binds (resolved from begin_pass's target_format).
    SDL_GPUTextureFormat cur_target_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    bool cur_clear = false;
    float cur_clear_color[4] = {};
    bool pass_open = false;

    // Phase 7/8: per-frame lighting resources. Emitter, SDF AND sky-vis
    // data all live in fragment storage buffers (slots 0/1/2), not
    // sampler textures — Metal mis-binds sampler-texture Load (see
    // SPRITE_FRAG_HLSL comment). Atlas is the only sampler texture.
    SDL_GPUBuffer* lp_emitter_buf = nullptr; // fragment storage BUFFER slot 0 (t2)
    SDL_GPUBuffer* lp_sdf_buf = nullptr;     // fragment storage BUFFER slot 1 (t3)
    SDL_GPUBuffer* lp_sky_vis_buf = nullptr; // fragment storage BUFFER slot 2 (t4)
    // 1-bounce GI (Stage 1): GPU compute GI output, a fragment storage
    // BUFFER (GiBuf), not the old IndirectTex storage texture. Storage-buffer
    // slot 3 ⇒ t5/space2 — both Stage 2b's SunSdfBuf and the vision overlay's
    // VisBuf used to sit ahead of it here.
    SDL_GPUBuffer* lp_gi_buf = nullptr; // fragment storage BUFFER slot 3 (GiBuf, t5)
    // Sky/sun directional skylight (Stage 2a/2b): GPU compute sky_sun.comp
    // output (rgb sky-access + a celestial-occ). Storage-buffer slot 4 ⇒
    // t6/space2 (LAST). Null on the shadow/UI batchers.
    SDL_GPUBuffer* lp_sky_buf = nullptr; // fragment storage BUFFER slot 4 (SkyBuf, t6)
    // Step 7 palette shade ramps. Storage-buffer slots 5 and 6 ⇒ t7/t8 (appended so
    // no existing slot renumbers). Storage BUFFERS, not sampled textures: shadercross
    // mis-binds sampler textures on Metal (CLAUDE.md), adding samplers would shift
    // every storage register in lockstep, and the lookup wants integer indexing.
    SDL_GPUBuffer* lp_ramp_buf = nullptr;      // RampBuf   (t7)
    SDL_GPUBuffer* lp_pal_index_buf = nullptr; // PalIdxBuf (t8)
    // Silhouette sun-shadow mask (Phase 2). The SOLE storage-READ texture now
    // (GI moved to GiBuf) ⇒ storage-texture slot 0 ⇒ t1/space2, ahead of the
    // storage buffers (t2..t6). Null on the shadow/UI batchers.
    SDL_GPUTexture* lp_shadow_mask = nullptr;
    SDL_GPUSampler* lp_data_sampler = nullptr;
    light_params lp = {};       // defaults: all zero
    sun_params lp_sun = {};     // Phase 8: sun/sky params
    debug_params lp_debug = {}; // Debug viz + tuning knobs (DebugParams cbuffer)

    void set_lighting_resources(
        float tile_pixel_size, float z_level, Uint32 count, float ambient, float cam_off_x = 0.0f,
        float cam_off_y = 0.0f, Uint32 sdf_map_w = 0u, Uint32 sdf_map_h = 0u,
        SDL_GPUBuffer* emitter_buf = nullptr, SDL_GPUBuffer* sdf_buf = nullptr,
        SDL_GPUSampler* data_sampler = nullptr, SDL_GPUBuffer* sky_vis_buf = nullptr,
        SDL_GPUBuffer* gi_buf = nullptr,
        const sun_params* sp = nullptr, const debug_params* dbg = nullptr,
        SDL_GPUBuffer* sky_buf = nullptr, SDL_GPUBuffer* ramp_buf = nullptr,
        SDL_GPUBuffer* pal_index_buf = nullptr) noexcept {
        // data_sampler is vestigial now that all lighting data (emitters,
        // SDF, sky-vis) lives in storage buffers — Atlas is the only
        // sampler texture and carries its own sampler from set_texture().
        // Kept for signature stability; fall back to default if null.
        if (!data_sampler) { data_sampler = default_sampler; }
        lp_emitter_buf = emitter_buf;
        lp_sdf_buf = sdf_buf;
        lp_sky_vis_buf = sky_vis_buf;
        lp_gi_buf = gi_buf;
        lp_sky_buf = sky_buf;
        lp_ramp_buf = ramp_buf;
        lp_pal_index_buf = pal_index_buf;
        lp_data_sampler = data_sampler;
        lp.tile_pixel_size = tile_pixel_size;
        lp.current_z = z_level;
        lp.emitter_count = emitter_buf ? count : 0u;
        lp.ambient = ambient;
        lp.camera_off_x = cam_off_x;
        lp.camera_off_y = cam_off_y;
        lp.sdf_map_w = sdf_buf ? sdf_map_w : 0u;
        lp.sdf_map_h = sdf_buf ? sdf_map_h : 0u;
        // Silhouette-shadow shear inputs for the VERTEX stage (shadow.vert).
        // Derived from the same sun_params the fragment stage uses, so the
        // shear direction tracks the sun exactly. cot(elev)=cos/sin from
        // sun_sin_elev, clamped away from the horizon so dawn/dusk shadows
        // stay finite (sin=0.15 → cot≈6.6 = ~6.6× sprite-height reach).
        if (sp) {
            lp.sun_dir_x = sp->sun_dir_x;
            lp.sun_dir_y = sp->sun_dir_y;
            const float se = std::clamp(sp->sun_sin_elev, 0.15f, 1.0f);
            lp.sun_cot_elev = std::sqrt(1.0f - se * se) / se;
        } else {
            lp.sun_dir_x = 0.0f;
            lp.sun_dir_y = 0.0f;
            lp.sun_cot_elev = 0.0f;
        }
        lp.lp_sun_pad = 0.0f;
        if (sp) {
            lp_sun = *sp;
        } else {
            lp_sun = {};
        }
        // Default-construct debug_params when none passed — the member
        // defaults provide sensible runtime values (emitter_scale=1,
        // sun_scale=1, sky_scale=1, shadow_k=8, shadow_steps=16,
        // debug_mode=0) so the shader behaves identically to the pre-
        // debug-widget code path.
        if (dbg) {
            lp_debug = *dbg;
        } else {
            lp_debug = {};
        }
    }

    // Silhouette sun-shadow mask (Phase 2). Set separately from the lighting
    // god-call: only the tile batcher reads it (sprite.frag storage-tex slot
    // 1). Left null on the shadow/UI batchers → bind_lighting_resources skips
    // it for them. Persists across frames until re-set.
    void set_shadow_mask(SDL_GPUTexture* tex) noexcept { lp_shadow_mask = tex; }

    // ---- lifecycle -------------------------------------------------

    // Build a graphics pipeline for a specific color-target format. Shaders
    // + blend + raster come from the shared shaders / desc; only the target
    // format varies. Returns nullptr on failure (caller logs/throws).
    SDL_GPUGraphicsPipeline* build_pipeline(SDL_GPUTextureFormat fmt) {
        SDL_GPUColorTargetBlendState blend{};
        blend.enable_blend = desc.enable_blend;
        blend.src_color_blendfactor = desc.src_color_blend;
        blend.dst_color_blendfactor = desc.dst_color_blend;
        blend.color_blend_op = desc.color_blend_op;
        blend.src_alpha_blendfactor = desc.src_alpha_blend;
        blend.dst_alpha_blendfactor = desc.dst_alpha_blend;
        blend.alpha_blend_op = desc.alpha_blend_op;
        blend.color_write_mask =
            SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B
            | SDL_GPU_COLORCOMPONENT_A;
        blend.enable_color_write_mask = false;

        SDL_GPUColorTargetDescription color_target{};
        color_target.format = fmt;
        color_target.blend_state = blend;

        SDL_GPUGraphicsPipelineCreateInfo pci{};
        pci.vertex_shader = vert_shader;
        pci.fragment_shader = frag_shader;
        pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pci.target_info.num_color_targets = 1;
        pci.target_info.color_target_descriptions = &color_target;
        pci.target_info.has_depth_stencil_target = false;
        pci.props = 0;
        return SDL_CreateGPUGraphicsPipeline(dev->raw(), &pci);
    }

    // Return the cached pipeline for `fmt`, lazily building it on first use.
    // nullptr on build failure (logged).
    SDL_GPUGraphicsPipeline* get_or_build_pipeline(SDL_GPUTextureFormat fmt) {
        auto it = pipelines.find(fmt);
        if (it != pipelines.end()) { return it->second; }
        SDL_GPUGraphicsPipeline* pl = build_pipeline(fmt);
        if (pl) {
            pipelines[fmt] = pl;
        } else {
            dbg(DL::Error) << "sprite_batcher: pipeline build failed for format "
                           << static_cast<int>(fmt) << ": " << SDL_GetError();
        }
        return pl;
    }

    void init(gpu_device& d, const pipeline_desc& pd, const char* label) {
        if (!d.ready()) { throw std::runtime_error("sprite_batcher::init: gpu_device not ready"); }
        dev = &d;
        desc = pd;

        init_shader_compiler();

        // Compile shaders. HLSL lives on disk under
        // data/shaders/lighting/src/ as the single source of truth (no
        // longer embedded in this file) — LIGHTING_REWORK_PLAN.md step 0.
        const std::string vert_src = load_lighting_shader_source(desc.vert_name);
        const std::string frag_src = load_lighting_shader_source(desc.frag_name);
        auto v = compile_graphics_shader(
            d, vert_src, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "sprite_batcher.vert");
        if (!v) { throw std::runtime_error("sprite_batcher vert shader compile failed"); }
        auto f = compile_graphics_shader(
            d, frag_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "sprite_batcher.frag");
        if (!f) {
            SDL_ReleaseGPUShader(d.raw(), v.shader);
            throw std::runtime_error("sprite_batcher frag shader compile failed");
        }
        vert_shader = v.shader;
        frag_shader = f.shader;

        // Step-3 Phase 1b structural gate: confirm shadercross reflected the
        // renumbered sprite frag as 1 storage texture (IndirectTex at t1) +
        // 4 storage buffers (emitter/sdf/skyvis/vis at t2..t5), not the old
        // 5-buffer layout. Logged to DC::Main (DC::SDL is filtered on this
        // build) so a wrong count surfaces before any pixel check — this
        // separates the layout axis from the data axis.
        DebugLogFL(DL::Info, DC::Main)
            << "sprite_batcher frag reflection [" << (label ? label : "?") << "]: samplers="
            << f.resources.num_samplers << " storage_textures=" << f.resources.num_storage_textures
            << " storage_buffers=" << f.resources.num_storage_buffers
            << " uniform_buffers=" << f.resources.num_uniform_buffers
            << " (sprite frag expects samplers=1 st=1 sb=7 ub=3; shadow frag expects "
               "samplers=1 st=0 sb=0 ub=0)";

        // Build the pipeline for the configured (swapchain) target format.
        // Other formats (e.g. the RGBA16F HDR world target) are built
        // lazily by get_or_build_pipeline() when begin_pass first sees them.
        SDL_GPUGraphicsPipeline* pl = build_pipeline(desc.color_target_format);
        if (!pl) {
            throw std::runtime_error(std::string("sprite_batcher pipeline: ") + SDL_GetError());
        }
        pipelines[desc.color_target_format] = pl;

        // Default sampler: NEAREST to keep pixel-art parity with the
        // legacy SDL_Renderer SCALEMODE_NEAREST atlas.
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter = SDL_GPU_FILTER_NEAREST;
        si.mag_filter = SDL_GPU_FILTER_NEAREST;
        si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        default_sampler = SDL_CreateGPUSampler(d.raw(), &si);
        if (!default_sampler) {
            throw std::runtime_error(std::string("sprite_batcher sampler: ") + SDL_GetError());
        }

        // Ring of (transfer, storage) buffer pairs.
        const Uint32 byte_size = MAX_INSTANCES * sizeof(sprite_instance);
        for (Uint32 i = 0; i < RING_SLOTS; ++i) {
            SDL_GPUBufferCreateInfo bci{};
            bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
            bci.size = byte_size;
            bci.props = 0;
            storage_bufs[i] = SDL_CreateGPUBuffer(d.raw(), &bci);

            SDL_GPUTransferBufferCreateInfo tci{};
            tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tci.size = byte_size;
            tci.props = 0;
            xfer_bufs[i] = SDL_CreateGPUTransferBuffer(d.raw(), &tci);

            if (!storage_bufs[i] || !xfer_bufs[i]) {
                throw std::runtime_error(std::string("sprite_batcher buffers: ") + SDL_GetError());
            }
        }

        pending.reserve(MAX_INSTANCES);
        segments.reserve(64);

        dbg(DL::Info) << "sprite_batcher initialised (" << (label ? label : "?")
                      << ", max_instances=" << MAX_INSTANCES << ")";
    }

    void shutdown() noexcept {
        if (!dev) { return; }
        SDL_GPUDevice* r = dev->raw();
        for (Uint32 i = 0; i < RING_SLOTS; ++i) {
            if (storage_bufs[i]) {
                SDL_ReleaseGPUBuffer(r, storage_bufs[i]);
                storage_bufs[i] = nullptr;
            }
            if (xfer_bufs[i]) {
                SDL_ReleaseGPUTransferBuffer(r, xfer_bufs[i]);
                xfer_bufs[i] = nullptr;
            }
        }
        if (default_sampler) {
            SDL_ReleaseGPUSampler(r, default_sampler);
            default_sampler = nullptr;
        }
        for (auto& kv : pipelines) {
            if (kv.second) { SDL_ReleaseGPUGraphicsPipeline(r, kv.second); }
        }
        pipelines.clear();
        if (vert_shader) {
            SDL_ReleaseGPUShader(r, vert_shader);
            vert_shader = nullptr;
        }
        if (frag_shader) {
            SDL_ReleaseGPUShader(r, frag_shader);
            frag_shader = nullptr;
        }
        pending.clear();
        segments.clear();
        dev = nullptr;
    }

    // ---- per-frame ------------------------------------------------

    void begin_frame() noexcept { cur_slot = (cur_slot + 1) % RING_SLOTS; }

    // ---- pass scope -----------------------------------------------

    void begin_pass(
        SDL_GPUCommandBuffer* cb, SDL_GPUTexture* target, Uint32 w, Uint32 h, const float* clear,
        Uint32 proj_w, Uint32 proj_h, SDL_GPUTextureFormat target_format) {
        if (pass_open) { throw std::runtime_error("sprite_batcher: begin_pass without end_pass"); }
        cur_cb = cb;
        cur_target = target;
        cur_target_w = w;
        cur_target_h = h;
        cur_proj_w = proj_w ? proj_w : w;
        cur_proj_h = proj_h ? proj_h : h;
        // INVALID → the format the batcher was init'd with (swapchain).
        cur_target_format =
            (target_format == SDL_GPU_TEXTUREFORMAT_INVALID)
                ? desc.color_target_format
                : target_format;
        cur_clear = clear != nullptr;
        if (clear) { std::memcpy(cur_clear_color, clear, sizeof(cur_clear_color)); }
        pending.clear();
        segments.clear();
        bound_tex = nullptr;
        bound_sampler = nullptr;
        seg_start = 0;
        bound_has_scissor = false;
        bound_scissor = {};
        bound_is_lit = true;
        pass_open = true;
    }

    void set_texture(SDL_GPUTexture* atlas, SDL_GPUSampler* sampler, bool is_lit) {
        if (!sampler) { sampler = default_sampler; }
        if (atlas == bound_tex && sampler == bound_sampler && is_lit == bound_is_lit) { return; }
        close_segment();
        bound_tex = atlas;
        bound_sampler = sampler;
        bound_is_lit = is_lit;
        seg_start = static_cast<Uint32>(pending.size());
    }

    void set_scissor(const SDL_Rect* rect) {
        const bool want = (rect != nullptr);
        const SDL_Rect r = want ? *rect : SDL_Rect{};
        if (want == bound_has_scissor
            && (!want
                || (r.x == bound_scissor.x && r.y == bound_scissor.y && r.w == bound_scissor.w
                    && r.h == bound_scissor.h))) {
            return;
        }
        close_segment();
        bound_has_scissor = want;
        bound_scissor = r;
    }

    void draw(const sprite_instance& inst) {
        if (pending.size() >= MAX_INSTANCES) {
            dbg(DL::Warn) << "sprite_batcher: per-pass instance cap reached, dropping";
            return;
        }
        pending.push_back(inst);
    }

    void draw_many(const sprite_instance* insts, std::size_t count) {
        const std::size_t headroom = MAX_INSTANCES - pending.size();
        const std::size_t to_add = std::min(headroom, count);
        if (to_add < count) {
            dbg(DL::Warn) << "sprite_batcher: dropped " << (count - to_add)
                          << " instances over per-pass cap";
        }
        pending.insert(pending.end(), insts, insts + to_add);
    }

    void flush() { close_segment(); }

    void end_pass(const sprite_batcher::pass_overlay_fn& overlay) {
        if (!pass_open) { return; }
        close_segment();

        // No work and no clear → trivially nothing to do.
        if (segments.empty() && !cur_clear) {
            pass_open = false;
            return;
        }

        // Upload pending instances if any.
        if (!pending.empty()) {
            const Uint32 slot = cur_slot;
            const Uint32 byte_size = static_cast<Uint32>(pending.size() * sizeof(sprite_instance));

            void* mapped = SDL_MapGPUTransferBuffer(
                dev->raw(), xfer_bufs[slot],
                /*cycle=*/true);
            if (!mapped) {
                dbg(DL::Error) << "MapGPUTransferBuffer failed: " << SDL_GetError();
                pass_open = false;
                return;
            }
            std::memcpy(mapped, pending.data(), byte_size);
            SDL_UnmapGPUTransferBuffer(dev->raw(), xfer_bufs[slot]);

            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cur_cb);
            if (!cp) {
                dbg(DL::Error) << "BeginGPUCopyPass failed: " << SDL_GetError();
                pass_open = false;
                return;
            }
            SDL_GPUTransferBufferLocation src{};
            src.transfer_buffer = xfer_bufs[slot];
            src.offset = 0;
            SDL_GPUBufferRegion dst{};
            dst.buffer = storage_bufs[slot];
            dst.offset = 0;
            dst.size = byte_size;
            // 2026-05-22: cycle=true on the storage buffer upload.
            // RING_SLOTS=3 lets the application rotate buffers across
            // frames, but on Win11 D3D12 with heavy frames (5000+
            // instances) the GPU still reads the previous frame's
            // contents of this slot when the CPU starts overwriting,
            // because the SDL_GPU device queues more than RING_SLOTS
            // frames in flight under load. Forcing cycle here makes
            // SDL discard the in-use allocation and provide a fresh
            // one, removing the race. Bridge (1 inst) and per-glyph
            // segments (1 inst each) never showed the symptom; the
            // tile-sprite white-tex segment with thousands of
            // instances did.
            SDL_UploadToGPUBuffer(cp, &src, &dst, /*cycle=*/true);
            SDL_EndGPUCopyPass(cp);
        }

        // Render pass.
        SDL_GPUColorTargetInfo ct{};
        ct.texture = cur_target;
        ct.load_op = cur_clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        if (cur_clear) {
            ct.clear_color.r = cur_clear_color[0];
            ct.clear_color.g = cur_clear_color[1];
            ct.clear_color.b = cur_clear_color[2];
            ct.clear_color.a = cur_clear_color[3];
        }
        ct.cycle = false;

        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cur_cb, &ct, 1, nullptr);
        if (!rp) {
            dbg(DL::Error) << "BeginGPURenderPass failed: " << SDL_GetError();
            pass_open = false;
            return;
        }

        SDL_GPUGraphicsPipeline* pl =
            segments.empty() ? nullptr : get_or_build_pipeline(cur_target_format);
        if (!segments.empty() && pl) {
            SDL_BindGPUGraphicsPipeline(rp, pl);

            SDL_GPUBufferBinding storage_binding{};
            storage_binding.buffer = storage_bufs[cur_slot];
            storage_binding.offset = 0;
            SDL_BindGPUVertexStorageBuffers(rp, /*first_slot=*/0, &storage_binding.buffer, 1);

            // Fragment resource layout (space2), t-order sampled → storage
            // textures → storage buffers:
            //   sampler slot 0  → Atlas      (t0, per-segment)
            //   stor-tex slot 0 → ShadowMask (t1, silhouette sun shadow)
            //   stor-buf slot 0 → Emitters   (t2)
            //   stor-buf slot 1 → SdfBuf     (t3)
            //   stor-buf slot 2 → SkyVisBuf  (t4)
            //   stor-buf slot 3 → GiBuf      (t5)
            //   stor-buf slot 4 → SkyBuf     (t6)
            // Textures and buffers are SEPARATE SDL binding arrays. All
            // per-tile reads are gated by sdf_map_w>0 in the shader; the
            // declared slots must still be bound on D3D12 (see
            // bind_lighting_resources).
            bind_lighting_resources(rp);

            const SDL_GPUViewport
                vp{0.0f, 0.0f, static_cast<float>(cur_target_w), static_cast<float>(cur_target_h),
                   0.0f, 1.0f};
            SDL_SetGPUViewport(rp, &vp);

            bool last_has_scissor = false;
            SDL_Rect last_scissor = {};
            for (const segment& s : segments) {
                const bool changed =
                    (s.has_scissor != last_has_scissor)
                    || (s.has_scissor
                        && (s.scissor.x != last_scissor.x || s.scissor.y != last_scissor.y
                            || s.scissor.w != last_scissor.w || s.scissor.h != last_scissor.h));
                if (changed) {
                    if (s.has_scissor) {
                        SDL_SetGPUScissor(rp, &s.scissor);
                    } else {
                        const SDL_Rect full{
                            0, 0, static_cast<int>(cur_target_w), static_cast<int>(cur_target_h)};
                        SDL_SetGPUScissor(rp, &full);
                    }
                    last_has_scissor = s.has_scissor;
                    last_scissor = s.scissor;
                }

                SDL_GPUTextureSamplerBinding tsb{};
                tsb.texture = s.tex;
                tsb.sampler = s.sampler;
                SDL_BindGPUFragmentSamplers(rp, /*first_slot=*/0, &tsb, 1);


                // Re-bind the lighting storage texture + buffers each lit
                // segment. SDL_BindGPUFragmentStorage* can rebuild the
                // descriptor table and zero any slot not included, so we
                // re-issue for lit segments. Unlit (HUD/UI) segments skip —
                // emitter_count==0 / sdf_map_w==0 guards in the shader
                // short-circuit any reads there.
                if (s.is_lit) { bind_lighting_resources(rp); }

                // Shader pixel→NDC math uses the projection extent
                // (logical UI space), NOT the viewport extent. The
                // SDL_GPUViewport above is set to physical pixels so
                // logical-coord draws stretch to fill the full
                // physical framebuffer on HiDPI.
                frame_params
                    fp{static_cast<float>(cur_proj_w), static_cast<float>(cur_proj_h), s.start, 0u};
                SDL_PushGPUVertexUniformData(cur_cb, /*slot=*/0, &fp, sizeof(fp));

                // For unlit segments push a copy of lp with all
                // light-driving counts zeroed; the vertex shader still
                // needs camera_off + tile_pixel_size for world_pos
                // (consistent geometry), and the fragment shader's
                // existing guards then skip the loop and march.
                // Similarly, lp_sun gets sun_intensity zeroed, and
                // lp_debug gets debug_mode=0 so the visualisation
                // dispatch never fires on HUD fragments.
                light_params lp_use = lp;
                sun_params lp_sun_use = lp_sun;
                debug_params lp_dbg_use = lp_debug;
                if (!s.is_lit) {
                    lp_use.emitter_count = 0u;
                    lp_use.sdf_map_w = 0u;
                    lp_use.sdf_map_h = 0u;
                    lp_sun_use.sun_intensity = 0.0f;
                    lp_sun_use.sky_intensity = 0.0f;
                    lp_dbg_use.debug_mode = 0u;
                }
                // Vertex slot 1: LightParams (world_pos computation + the
                // shadow shear). Always pushed — both sprite.vert and
                // shadow.vert declare FrameParams + LightParams.
                SDL_PushGPUVertexUniformData(cur_cb, /*slot=*/1, &lp_use, sizeof(lp_use));
                // Vertex slot 2: DebugParams (foliage sway reads sway_amp/
                // sway_freq/anim_time here). sprite.vert declares b2/space1;
                // shadow.vert is a separate pipeline and ignores it.
                SDL_PushGPUVertexUniformData(cur_cb, /*slot=*/2, &lp_dbg_use, sizeof(lp_dbg_use));
                // Fragment lighting cbuffers. The silhouette-shadow frag
                // declares ZERO fragment cbuffers, so its batcher disables
                // these pushes (push_frag_lighting_uniforms=false) to keep
                // the push count == the shader's reflected uniform-buffer
                // count. Default-true preserves the sprite/UI/font path.
                if (desc.push_frag_lighting_uniforms) {
                    // Fragment slot 0: LightParams (ambient, emitter_count, sdf_map_w)
                    SDL_PushGPUFragmentUniformData(cur_cb, /*slot=*/0, &lp_use, sizeof(lp_use));
                    // Fragment slot 1: SunParams (sun/sky direction + color)
                    SDL_PushGPUFragmentUniformData(
                        cur_cb, /*slot=*/1, &lp_sun_use, sizeof(lp_sun_use));
                    // Fragment slot 2: DebugParams (visualisation + tunable scales)
                    SDL_PushGPUFragmentUniformData(
                        cur_cb, /*slot=*/2, &lp_dbg_use, sizeof(lp_dbg_use));
                }

                SDL_DrawGPUPrimitives(
                    rp, /*num_vertices=*/6, /*num_instances=*/s.count,
                    /*first_vertex=*/0, /*first_instance=*/0);
            }
        }

        // External overlay (Dear ImGui) draws last, inside this same pass.
        if (overlay) { overlay(rp, cur_cb); }

        SDL_EndGPURenderPass(rp);
        pass_open = false;
    }

private:
    // Move the currently bound (tex, sampler) + accumulated count into
    // `segments`. Safe to call when no instances are pending — no-ops.
    void close_segment() {
        const Uint32 end = static_cast<Uint32>(pending.size());
        if (!bound_tex || end <= seg_start) { return; }
        segment s{bound_tex, bound_sampler, seg_start, end - seg_start};
        s.scissor = bound_scissor;
        s.has_scissor = bound_has_scissor;
        s.is_lit = bound_is_lit;
        segments.push_back(s);
        seg_start = end;
    }

    // Bind the lighting storage texture + storage buffers for a lit segment
    // (shared by the initial bind and each per-segment rebind, so the two
    // sites can never drift). Textures and buffers are separate SDL binding
    // arrays. All per-tile reads are gated by sdf_map_w>0 in the shader, so
    // a not-yet-ready (null) resource is safe to leave unbound.
    void bind_lighting_resources(SDL_GPURenderPass* rp) {
        // Storage TEXTURE slot 0 → t1: the silhouette ShadowMask, now the SOLE
        // storage texture (GI moved to GiBuf, a storage buffer, in Stage 1 —
        // this is what removed the old all-or-none 2-slot hazard). Created
        // unconditionally at init, so it is non-null on the tile batcher. The
        // shadow/UI batchers leave it null → nothing bound (they declare no
        // storage textures). On D3D12 a declared-but-unbound SRV slot corrupts
        // the command list, so the tile pipeline (which declares it) must
        // always have it bound — the producer guarantees that.
        if (lp_shadow_mask) {
            SDL_BindGPUFragmentStorageTextures(rp, /*first_slot=*/0, &lp_shadow_mask, 1);
        }
        // Storage BUFFER slots 0..4 → t2..t6 (after the 1 storage texture):
        // Emitters, SdfBuf, SkyVisBuf, GiBuf, SkyBuf. (Stage 2b dropped SunSdfBuf —
        // the sun shadow moved to the compute coverage march in SkyBuf.a. VisBuf was
        // dropped when the dead live-visibility field was deleted; the sub-tile
        // vision carve marches SdfBuf instead and needs no buffer of its own.) The
        // sprite pipeline declares ALL FIVE, so this is strictly all-or-none: a
        // PARTIAL bind
        // leaves declared SRV slots unbound → D3D12 "Missing fragment storage
        // buffer binding!" → device removed. All six producers allocate their
        // buffer unconditionally at init (emitter_collector ctor, sdf_pass::init,
        // gi_compute_pass::init, sky_sun_pass::init), so on the tile batcher they
        // are non-null whenever render_state is ready; the shader gates every
        // per-tile read on sdf_map_w>0 / emitter_count>0 / gi_strength>0, so
        // binding an unpopulated buffer is read-safe. The shadow/UI batchers
        // leave them null → bind nothing. Bound in ONE call so a later bind can't
        // zero an earlier slot.
        if (lp_emitter_buf && lp_sdf_buf && lp_sky_vis_buf && lp_gi_buf && lp_sky_buf
            && lp_ramp_buf && lp_pal_index_buf) {
            SDL_GPUBuffer* sbufs[7] = {lp_emitter_buf, lp_sdf_buf,  lp_sky_vis_buf, lp_gi_buf,
                                       lp_sky_buf,     lp_ramp_buf, lp_pal_index_buf};
            SDL_BindGPUFragmentStorageBuffers(rp, /*first_slot=*/0, sbufs, 7);
        }
    }
};

// ---- sprite_batcher trampolines ---------------------------------------

sprite_batcher::sprite_batcher(): p(std::make_unique<sprite_batcher_impl>()) {}
sprite_batcher::sprite_batcher(sprite_batcher&&) noexcept = default;
sprite_batcher& sprite_batcher::operator=(sprite_batcher&&) noexcept = default;
sprite_batcher::~sprite_batcher() {
    if (p) { p->shutdown(); }
}

void sprite_batcher::init(gpu_device& dev, const pipeline_desc& desc, const char* debug_label) {
    p->init(dev, desc, debug_label);
}

void sprite_batcher::shutdown() noexcept {
    if (p) { p->shutdown(); }
}

void sprite_batcher::set_shadow_mask(SDL_GPUTexture* tex) { p->set_shadow_mask(tex); }

void sprite_batcher::begin_pass(
    SDL_GPUCommandBuffer* cb, SDL_GPUTexture* target, std::uint32_t target_w,
    std::uint32_t target_h, const float* clear_color_rgba, std::uint32_t proj_w,
    std::uint32_t proj_h, SDL_GPUTextureFormat target_format) {
    p->begin_pass(cb, target, target_w, target_h, clear_color_rgba, proj_w, proj_h, target_format);
}

void sprite_batcher::set_texture(SDL_GPUTexture* atlas, SDL_GPUSampler* sampler, bool is_lit) {
    p->set_texture(atlas, sampler, is_lit);
}

void sprite_batcher::set_scissor(const SDL_Rect* rect) { p->set_scissor(rect); }

void sprite_batcher::set_lighting_resources(
    float tile_pixel_size, float z_level, Uint32 emitter_count, float ambient, float cam_off_x,
    float cam_off_y, Uint32 sdf_map_w, Uint32 sdf_map_h, SDL_GPUBuffer* emitter_buf,
    SDL_GPUBuffer* sdf_buf, SDL_GPUSampler* data_sampler, SDL_GPUBuffer* sky_vis_buf,
    SDL_GPUBuffer* gi_buf, const sun_params* sp, const debug_params* dbg,
    SDL_GPUBuffer* sky_buf, SDL_GPUBuffer* ramp_buf, SDL_GPUBuffer* pal_index_buf) {
    p->set_lighting_resources(
        tile_pixel_size, z_level, emitter_count, ambient, cam_off_x, cam_off_y, sdf_map_w,
        sdf_map_h, emitter_buf, sdf_buf, data_sampler, sky_vis_buf, gi_buf, sp, dbg,
        sky_buf, ramp_buf, pal_index_buf);
}

void sprite_batcher::draw(const sprite_instance& inst) { p->draw(inst); }

void sprite_batcher::draw(const sprite_instance* insts, std::size_t count) {
    p->draw_many(insts, count);
}

void sprite_batcher::flush() { p->flush(); }

void sprite_batcher::end_pass(const pass_overlay_fn& overlay) { p->end_pass(overlay); }

void sprite_batcher::begin_frame() { p->begin_frame(); }

} // namespace lighting
