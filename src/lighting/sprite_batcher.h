#pragma once

// SDL_GPU instance-batched sprite renderer — phase 2b of lighting rework.
//
// One batcher = one graphics pipeline + one persistent vertex buffer (a unit
// quad) + a ring of per-frame transfer/storage buffers holding sprite
// instances. Each draw call goes through SDL_DrawGPUPrimitives with an
// instance count, so a single pipeline binds the unit quad once and issues
// one draw per atlas-texture run.
//
// Phase 2 will instantiate two batchers:
//   1. tile_batcher  — full tile sprites (terrain, furniture, vehicles, mobs,
//                      effects, animated frames). Color-mod path emulates the
//                      legacy SDL_SetTextureColorMod tint exactly so the
//                      golden-image regression in sub-phase 2h passes.
//   2. ui_batcher    — UI glyphs / framebuffer chars (font texture pages
//                      produced by sdl_font.cpp's GPU text engine in 2f).
//
// Both share this class — different pipelines are achieved purely by
// supplying a different `pipeline_desc` at init().
//
// This is the *header* for sub-phase 2b. Implementation lands in 2d; the
// translation unit will reference no symbols until then so the build stays
// green. The struct layouts below are wire-stable: changing them requires a
// matching shader update in data/shaders/lighting/src/sprite.{vert,frag}.

#include "gpu_device.h"

#include <cstdint>
#include <functional>
#include <memory>

// Forward-declared so the header stays self-contained (no SDL_gpu.h include).
struct SDL_GPURenderPass;
struct SDL_GPUTexture;

namespace lighting {

// One sprite = one instance. 96 bytes, packed std140-friendly.
// Layout must match data/shaders/lighting/src/sprite.vert (and shadow.vert,
// which is drawn on a second batcher over the SAME instance buffer).
//
//   dst_*    — pixel-space destination quad in the bound render target.
//   src_*    — normalised UV rect within the bound texture (0..1).
//   tint_*   — RGBA multiplier applied per-pixel (1.0 == passthrough).
//              Legacy SDL_SetTextureColorMod/SetTextureAlphaMod fold into this.
//   rotation — Radians, applied to the destination quad around its centre.
//              Sprite content is unchanged; only the quad's pixel coverage
//              rotates. 0 = no rotation; π/2 = 90° clockwise.
//   pad*     — keep struct 16-byte-aligned for std140; required by the
//              vertex shader's StructuredBuffer<SpriteInstance> binding.
struct sprite_instance {
    float dst_x;
    float dst_y;
    float dst_w;
    float dst_h;
    float src_u;
    float src_v;
    float src_uw;
    float src_vh;
    float tint_r;
    float tint_g;
    float tint_b;
    float tint_a;
    float rotation;
    // Memory-fade marker (effect 3): 0 or positive = normal sprite (no-op);
    // negative = memorized tile carrying -(distance from player in tiles), which
    // the fragment shader decodes to dim+fade remembered terrain. Default 0 so
    // every existing sprite (UI/font/zero-init) is untouched.
    float light_mul;
    float pad1;
    float pad2;
    float extrude_px;   // screen-px the quad extends above natural top (0 = no effect)
    float extrude_dark; // darkness at canopy (0..1); fragment multiplies final_rgb by (1 -
                        // dark_frac)
    float extrude_lean; // horizontal shear per px from viewport centre, per vertical fraction
    /// Per-sprite "this is a vertical surface" amount, 0..1. Drives the facing arc in
    /// sprite.frag (a vertical gradient so a wall face responds to light direction).
    /// 0 = flat/horizontal surface (ground, most items) => arc disabled for this sprite.
    float face_amt;
    // Which lighting composite sprite.frag applies to this sprite:
    // 0 = unlit (albedo x tint), 1 = gpu_lit (albedo x tint x gpu_total),
    // 2 = memory (cross-fade to the remembered look). Values are
    // `sprite_light_mode` from src/tile_light_mode.h, cast to float.
    //
    // A zero-initialised `sprite_instance{}` is therefore `unlit`, which is the
    // correct default for every UI rect, flat overlay and fullscreen blit.
    // World tiles get their mode from cata_tiles' classification, and the
    // fragment shader compares this by BAND (> 1.5, > 0.5) rather than for
    // equality, since it crosses the vertex/fragment boundary as a float.
    float light_mode;
    // Coloured light OVERRIDE, not a plain emissive: `colour * strength` with
    // max(colour) == 1, so the fragment shader recovers strength as max3(flash)
    // and composites lerp(radiance, colour, strength) as
    // `radiance * (1 - strength) + flash` — one lane, hue preserved at both
    // brightness extremes. Used by the melee hit-flash (cata_tiles_anim) and the
    // main-menu backdrop's blue base (sdl_render_frame). Zero = exact no-op.
    //
    // Additive emissive was rejected: in daylight gpu_total ~ 1, so adding a red
    // (0.6, 0, 0) clips to white and loses the hue that says WHAT was hit. Folding
    // the flash into `tint` was also rejected: that is multiplicative, so it
    // vanishes at night — the only place the previous max()-based flash ever showed.
    float flash_r;
    float flash_g;
    float flash_b;
};
static_assert(
    sizeof(sprite_instance) == 96,
    "sprite_instance is wire-stable with the vertex shader; "
    "changing its layout requires shader edits.");

// Describes the graphics pipeline a batcher should build at init.
// `color_target_format` must match the SDL_GPUTexture the batcher will
// render into — usually the swapchain format for direct presentation, or an
// offscreen RT format (RGBA8 / RGBA16F) for the deferred lighting passes.
struct pipeline_desc {
    SDL_GPUTextureFormat color_target_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUBlendFactor src_color_blend = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    SDL_GPUBlendFactor dst_color_blend = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    SDL_GPUBlendOp color_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUBlendFactor src_alpha_blend = SDL_GPU_BLENDFACTOR_ONE;
    SDL_GPUBlendFactor dst_alpha_blend = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    SDL_GPUBlendOp alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    bool enable_blend = true;
    // Shader source filenames (under data/shaders/lighting/src/), loaded at
    // init via load_lighting_shader_source. Default = the sprite shaders, so
    // existing tile/UI batchers are unchanged; the silhouette-shadow batcher
    // passes shadow.vert/shadow.frag with a MAX color_blend_op.
    const char* vert_name = "sprite.vert.hlsl";
    const char* frag_name = "sprite.frag.hlsl";
    // When true (default), end_pass pushes the 3 fragment lighting uniform slots
    // (LightParams/SunParams/DebugParams) every segment, matching sprite.frag's
    // 3 fragment cbuffers. The silhouette-shadow frag declares ZERO cbuffers, so
    // its batcher sets this false to skip those pushes — otherwise the push count
    // would not match the shader's reflected uniform-buffer count. The vertex
    // uniform pushes (FrameParams + LightParams) are always issued; shadow.vert
    // uses both.
    bool push_frag_lighting_uniforms = true;
};

// PIMPL — keeps SDL_GPU storage-buffer / pipeline details out of the header
// so the rest of the codebase compiles without the SDL_gpu.h surface.
// Phase 8: sun + skylight parameters.  Wire-stable with SunParams cbuffer (b1, space3).
struct sun_params {
    float sun_dir_x, sun_dir_y; // direction sun comes FROM (normalized 2D)
    float sun_sin_elev;         // sin(elevation): 0=horizon, 1=zenith
    float sun_intensity;        // 0=night, 1=noon
    float sun_r, sun_g, sun_b;  // sun color RGB
    float sky_r, sky_g, sky_b;  // sky ambient RGB
    float sky_intensity;        // overall sky brightness
    float sp_pad;               // deprecated; debug visualisation moved to debug_params
};

// Debug visualisation + runtime tuning knobs (DebugParams cbuffer at
// register(b2, space3); 48 bytes; wire-stable). debug_mode dispatches per-
// component visualisations in the fragment shader; emitter/sun/sky_scale
// multiply the corresponding contributions; shadow_k and shadow_steps tune
// the shared sphere-trace (emitter + sun); dither_amt/dither_bands tune the
// world-locked ordered (Bayer) dither. Defaults are the shipping look
// (scale=1, k=8, steps=16, dither on at 6 bands).
struct debug_params {
    uint32_t debug_mode = 0u;
    float debug_opacity = 0.6f;
    float emitter_scale = 1.0f;
    float sun_scale = 1.0f;
    float sky_scale = 1.0f;
    float shadow_k = 8.0f;
    uint32_t shadow_steps = 16u;
    float dither_amt = 1.0f;
    // The ordered dither quantises the dynamic light into this many bands. At 12
    // bands each step is 1/12 of the light range, which over a scene whose total
    // dynamic light sat near 0.5 left only ~6 usable levels — coarse enough to
    // read as blotching rather than as dither. 32 keeps the mean-preserving
    // stipple while dropping the step below visual threshold.
    float dither_bands = 32.0f;
    // 1-bounce indirect multiplier (0=off); Alt+F8/F9 to tune. GI is computed at
    // ONE VALUE PER MAP TILE and bilinearly upsampled (gi_bounce.comp.hlsl +
    // sprite.frag's indirect_bilinear), so its error is tile-scale soft blobs.
    // 0.60 made those blobs the dominant large-scale structure in the image;
    // 0.35 keeps colour bleed without letting the low-res term drive the look.
    // The real fix is a higher-resolution / temporally-filtered GI pass.
    float gi_strength = 0.35f;
    // Vision rework knobs (Stoneshard-style). All default ON so the effect ships;
    // set any to its off-value to bisect live. Wire-stable with DebugParams cbuffer.
    float vis_curve = 1.0f;    // vision-edge falloff exponent (0=off → no falloff)
    float mem_dim = 0.35f;     // memorized-tile brightness floor (effect 3)
    float mem_desat = 0.70f;   // memorized-tile desaturation 0..1 (effect 3)
    float night_floor = 0.02f; // ambient floor at night   (effect 4)
    float day_floor = 0.05f;   // ambient floor at noon     (effect 4)
    // Tone grade (Stoneshard wash) + radial vision bubble. Applied to LIT world
    // tiles only (tint≈0). Full-strength defaults; each knob disables at its
    // off-value (grade_desat/cool=0, grade_bright=1, vis_radius=0).
    float grade_desat = 0.55f;  // 0=full colour … 1=greyscale
    float grade_cool = 0.20f;   // blend toward cool teal tint (0=off)
    float grade_bright = 0.80f; // brightness multiplier on lit world tiles
    // Radial player-distance falloff radius (tiles; 0 = off). Read by the Step 5b
    // sub-tile vision carve. Ships at 0: the carve's LOS term is the point of the
    // effect, and a 16-tile radial dim would silently darken daylight scenes.
    float vis_radius = 0.0f;
    float player_x = 0.0f;      // DATA (not a knob): player map-tile centre x
    float player_y = 0.0f;      // DATA: player map-tile centre y
    float mem_radius = 30.0f;   // memory distance-fade scale in tiles (effect 3)
    // Bucket A / A1 surface-normal knobs (live; sprite.frag surface_normal + Lambert).
    float nrm_amount = 0.9f;  // normal Lambert blend: 0=flat(off) .. 1=full
    float nrm_relief = -2.0f; // tilt magnitude; SIGNED — negative flips global relief dir
    float nrm_elev = 0.3f;    // implied light height; LOWER=more grazing=stronger relief
    float sdf_sharp = 0.0f;   // SDF sample: 0=bilinear(smooth) .. 1=nearest(tight/grid-snap)
    float ao_strength = 0.35f; // A4 ambient occlusion: 0=off .. 1=full SDF-cavity darkening (ships ON)
    float shadow_mask_str = 0.0f; // Phase 2 silhouette sun-shadow mask on ground: 0=off(default) ..
                                  // 1=full
    // Foliage sway (vertex stage; sprite.vert reads these via DebugParams b2/space1).
    float sway_amp = 3.0f;  // wind displacement amplitude in pixels (0=off)
    float sway_freq = 1.2f; // wind oscillation frequency (Hz-ish)
    float anim_time = 0.0f; // DATA (not a knob): wrapped render seconds, injected per-frame
    // Wet specular glint strength. DATA (not a slider): the frame code folds the
    // user knob g_spec_strength with rain intensity per-frame so the sheen only
    // shows while raining. 0 = off.
    float spec_strength = 0.0f;
    // P1: contribution epsilon for shadow march gating. 0 means use shader default.
    float light_eps = 0.0f;
    // P2: max emitters per pixel that get full shadow trace (cast as uint in HLSL).
    float max_shadow_k = 16.0f;
    // P5b: sky/sun quality knobs (sky_sun.comp cbuffer). Float cast to uint in HLSL.
    float sky_dirs = 8.0f;     // sky hemisphere directions (1=flat sky, 16=high quality)
    float sky_reach = 10.0f;   // sky march max distance (tiles)
    float sun_steps = 24.0f;   // celestial march steps
    float sun_penumbra = 4.0f; // penumbra angular samples (1=hard edge, 6=very soft)
    // Wave 2: vegetation life knobs (vertex stage; sprite.vert reads via DebugParams b2/space1).
    float ripple_k = 1.5f;      // intra-sprite column UV desync (0=rigid, 2=heavy shear)
    float gust_amp = 0.4f;      // multi-octave wind gust envelope amplitude (0=steady)
    float gust_freq = 0.3f;     // gust envelope frequency (Hz-ish, slow)
    float part_radius = 2.5f;   // player foliage parting radius in tiles (0=off)
    float part_strength = 0.5f; // player foliage parting push strength (0=off)
    float nrm_entity_amount = 0.3f; // entity (tall sprite) normal relief: 0=flat .. 1=full bevel
    // Pixel-art light quantisation (Step 1).
    float texels_per_tile = 32.0f; // DATA: tileset native tile width in art texels
    float light_quant = 1.0f;      // 1 = snap light sample to art texels, 0 = per-screen-pixel
    // Sub-tile occluders (Step 3/4).
    float occ_soft_gain = 1.0f; // partial-occluder block gain (0 = hard occluders only)
    float self_eps_tall = 0.55f; // trace_shadow self-shadow escape radius for TALL sprites
    // Palette shade ramps (Step 7).
    float ramp_enable = 1.0f;  // 0 = plain multiply, 1 = full ramp resolve
    float ramp_steps = 8.0f;   // shade steps per palette row (must match built LUT)
    float ramp_chroma = 0.35f; // how much coloured light tints the ramped surface
    // Step 6: SDF-guided bilateral GI upsample. 1 = reject GI taps across an SDF
    // discontinuity (bounce stops at walls), 0 = plain bilinear (the pre-Step-6
    // behaviour). Occupies what was dbg_pad2; size unchanged.
    float gi_bilat = 1.0f;
    // Step 8: sub-tile vision FRONTIER. The outward edge of the seen region is drawn
    // as full-tile `lighting_*` overlay sprites, so it can only ever be a tile
    // staircase — the one grid artefact the rest of this work leaves behind, and it
    // reads badly next to the now-smooth lighting. 1 = feather the overlay across
    // the tile from its neighbours' visibility, 0 = the old hard tile edge.
    float vis_edge = 1.0f;
    // Normalised V offset from a colour texel to its NORMAL texel in the same atlas
    // page (double-height page => 0.5). 0.0 disables the procedural normal atlas
    // entirely and sprite.frag falls back to surface_normal() unchanged, which is why
    // it ships at 0.0: the value is supplied per-frame from the atlas once a page
    // actually carries normals.
    float nrm_atlas_v = 0.0f;
    // Signed strength of the per-sprite vertical-FACE arc in sprite.frag: how far the
    // surface normal tilts toward the viewer at a sprite's base and away at its top,
    // scaled by sprite_instance::face_amt. Signed so the F4 panel can flip the sense
    // (which end reads as "lit from the south") without a rebuild. face_amt is 0 for
    // everything that is not a wall/window/tall furniture, so this is inert there
    // regardless of its value.
    float face_arc = 1.5f;
    float vis_edge_pad2 = 0.0f;
};

// Returns sun/sky params interpolated from a 24h LUT for the given hour (0..24).
sun_params make_sun_params(float hour_of_day) noexcept;

class sprite_batcher_impl;

class sprite_batcher {
public:
    sprite_batcher();
    sprite_batcher(const sprite_batcher&) = delete;
    sprite_batcher& operator=(const sprite_batcher&) = delete;
    sprite_batcher(sprite_batcher&&) noexcept;
    sprite_batcher& operator=(sprite_batcher&&) noexcept;
    ~sprite_batcher();

    // Build pipeline + persistent buffers. Throws on shader / pipeline
    // creation failure — caller treats as fatal (same policy as
    // gpu_device::init).
    void init(
        gpu_device& dev, const pipeline_desc& desc, const char* debug_label = "sprite_batcher");

    void shutdown() noexcept;

    // Open a render pass that targets `target`. The batcher records all
    // subsequent draw() calls into `cb` until end_pass(). The target
    // resolution drives the viewport / orthographic projection sent to
    // the vertex shader as a push-constant.
    //
    //   clear_color present => LOAD_OP_CLEAR with that color
    //   clear_color absent  => LOAD_OP_LOAD (preserve)
    // target_w/h drive the GPU viewport (rasterizer extent — physical
    // swapchain pixels). proj_w/h drive the shader's pixel→NDC math
    // (the orthographic "logical" coordinate system the UI draws in).
    // When proj_w/h are 0 (default) they fall back to target_w/h, which
    // preserves legacy behaviour for callers that don't need HiDPI
    // decoupling (offscreen captures, fixed-res render-to-texture).
    // HiDPI render path passes target = physical, proj = logical so the
    // GPU stretches logical-coord draws across the full physical fb.
    // `target_format` selects the cached graphics pipeline for this pass
    // (built lazily). INVALID (default) uses the format the batcher was
    // init'd with (the swapchain format) — so existing swapchain/UI passes
    // need no change; an HDR (RGBA16F) target passes its format explicitly.
    void begin_pass(
        SDL_GPUCommandBuffer* cb, SDL_GPUTexture* target, std::uint32_t target_w,
        std::uint32_t target_h, const float* clear_color_rgba = nullptr, std::uint32_t proj_w = 0,
        std::uint32_t proj_h = 0,
        SDL_GPUTextureFormat target_format = SDL_GPU_TEXTUREFORMAT_INVALID);

    // Bind a different atlas page. Calling this with the currently bound
    // page and the same is_lit state is a no-op; otherwise flushes the
    // pending batch implicitly before re-binding.
    //
    // `is_lit` controls whether the lighting fragment-shader path runs
    // for instances drawn in this segment. Default true preserves
    // existing tile-sprite behaviour. UI rects + HUD font glyphs pass
    // false; the shader's existing emitter_count==0 / sdf_map_w==0
    // guards short-circuit both the per-emitter loop and the sun
    // march for these segments, recovering the GPU cost of running
    // lighting math on fragments whose result is discarded anyway: an
    // `unlit` sprite's composite never reads the radiance term.
    void set_texture(SDL_GPUTexture* atlas, SDL_GPUSampler* sampler, bool is_lit = true);
    // Set a GPU scissor rect for subsequent draws. nullptr = full viewport.
    void set_scissor(const SDL_Rect* rect);
    // Phase 7/8: supply per-frame lighting resources.
    // emitter_buf:  GRAPHICS_STORAGE_READ buffer of gpu_emitter records.
    //               Bound as fragment storage buffer slot 0 → HLSL
    //               StructuredBuffer<GpuEmitter> at register(t2, space2).
    // sdf_buf:      GRAPHICS_STORAGE_READ buffer of W×H floats (sdf[x*H+y])
    //               from sdf_pass — fragment storage slot 1 → HLSL
    //               StructuredBuffer<float> at register(t3, space2).
    //               (Was a sampler texture; Metal mis-binds those.)
    // sky_vis_buf:  GRAPHICS_STORAGE_READ buffer of W×H floats (1.0=open
    //               sky, 0.0=roofed) — fragment storage slot 2 → HLSL
    //               StructuredBuffer<float> at register(t4, space2).
    // gi_buf:       compute GI output — fragment storage slot 3 → t5/space2.
    // sky_buf:      sky_sun.comp output — fragment storage slot 4 → t6/space2.
    // sp:           sun+sky params pointer (nullptr = no sun)
    void set_lighting_resources(
        float tile_pixel_size, float z_level, Uint32 emitter_count, float ambient,
        float cam_off_x = 0.0f, float cam_off_y = 0.0f, Uint32 sdf_map_w = 0u,
        Uint32 sdf_map_h = 0u, SDL_GPUBuffer* emitter_buf = nullptr,
        SDL_GPUBuffer* sdf_buf = nullptr, SDL_GPUSampler* data_sampler = nullptr,
        SDL_GPUBuffer* sky_vis_buf = nullptr, SDL_GPUBuffer* gi_buf = nullptr,
        const sun_params* sp = nullptr,
        const debug_params* dbg = nullptr, SDL_GPUBuffer* sky_buf = nullptr,
        // Step 7 palette shade ramps — fragment storage slots 5/6 → t7/t8.
        SDL_GPUBuffer* ramp_buf = nullptr, SDL_GPUBuffer* pal_index_buf = nullptr);

    // Silhouette sun-shadow mask (Phase 2). Now the sole fragment storage-read
    // texture, bound at slot 0 (t1/space2) for sprite.frag. Set separately from
    // the lighting god-call; only the tile batcher uses it. Persists until
    // re-set. (GI moved off a storage texture to the GiBuf storage buffer in
    // Stage 1 — set_indirect_tex is gone; the GI buffer rides the lighting
    // god-call's gi_buf param.)
    void set_shadow_mask(SDL_GPUTexture* tex);

    // Append one sprite to the pending batch. Triggers an automatic
    // flush when the per-frame instance budget is reached so callers can
    // ignore buffer overflow.
    void draw(const sprite_instance& inst);
    void draw(const sprite_instance* insts, std::size_t count);

    // Force-flush the pending instances as one SDL_DrawGPUPrimitives
    // call. Normally implicit via set_texture() / end_pass(), exposed for
    // call sites that want explicit grouping.
    void flush();

    // Overlay hook: invoked inside end_pass() with the live render pass +
    // command buffer, AFTER this batcher's segments are replayed and just
    // BEFORE SDL_EndGPURenderPass. Lets an external renderer (Dear ImGui)
    // draw into the SAME swapchain pass — required because D3D12 drops
    // prior-pass draws if a second pass targets the same swapchain texture.
    // Passed by value per-call (NOT stored): early-return paths in end_pass
    // simply drop it, so it can never leak into the next frame's pass.
    using pass_overlay_fn = std::function<void(SDL_GPURenderPass*, SDL_GPUCommandBuffer*)>;

    // Close the render pass started by begin_pass(). If `overlay` is set it
    // runs as the last draw inside the pass (see pass_overlay_fn).
    void end_pass(const pass_overlay_fn& overlay = {});

    // Per-frame reset — called by the render orchestrator at the start
    // of each frame to roll the instance-buffer ring forward. No GPU
    // sync; the buffers are sized so the in-flight frames never alias.
    void begin_frame();

private:
    std::unique_ptr<sprite_batcher_impl> p;
};

} // namespace lighting
