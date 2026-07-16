#pragma once

// Process-wide singleton bundling the SDL_GPU stack: device, sprite batcher,
// font engine, atlas, geometry helper. Phase 2i-A wires this up alongside
// the legacy SDL_Renderer so a Win11 / RTX 4090 build can verify that the
// device + DXC shadercross + vendored SPIRV-Cross all initialise correctly
// against a real D3D12/Vulkan backend before the renderer cutover
// (phase 2i-B) removes the legacy path.
//
// During phase 2i-A the lighting stack runs on a *hidden secondary window*
// — SDL_GPU exclusive-claims a window, so it cannot share the one that
// SDL_CreateRenderer already owns. The cost is a few MB of GPU resources;
// the benefit is that init failures surface in the game log on day one
// instead of inside the cutover commit, where they'd be conflated with
// dozens of unrelated render-call substitutions.

#include "bloom_pass.h"
#include "emitter_collector.h"
#include "event_queue.h"
#include "font_engine.h"
#include "gi_compute_pass.h"
#include "gpu_atlas.h"
#include "gpu_device.h"
#include "gpu_geometry.h"
#include "gpu_sdf_pass.h"
#include "rain_effect.h"
#include "hud_particle_effect.h"
#include "sdf_pass.h"
#include "sound_wave_pass.h"
#include "sky_sun_pass.h"
#include "sprite_batcher.h"
#include "tonemap_pass.h"
#include "ui_adaptor_draw_slices.h"
#include "ui_composite_target.h"
#include "ui_post_pass.h"
#include "volumetric_pass.h"

#include <memory>
#include <vector>

namespace lighting {

// Custom deleter that releases an SDL_GPUTexture against the live
// render_state device. Safe to invoke even if the render_state has
// already been shut down — leaks the texture in that case (process
// exit path; SDL releases everything on device teardown anyway).
struct gpu_texture_deleter {
    void operator()(SDL_GPUTexture* t) const noexcept;
};
using gpu_texture_unique_ptr = std::unique_ptr<SDL_GPUTexture, gpu_texture_deleter>;


class render_state {
public:
    // Construct the stack. `host_window` must outlive this object and
    // is the window the GPU device claims. Throws std::runtime_error on
    // any sub-component init failure; partial state is rolled back.
    void init(SDL_Window* host_window);

    // Idempotent. Safe to call from WinDestroy regardless of init
    // outcome.
    void shutdown() noexcept;

    // True once init has completed and not been shut down.
    bool ready() const noexcept { return device_.ready(); }

    gpu_device& device() noexcept { return device_; }
    sprite_batcher& tile_batcher() noexcept { return tile_batcher_; }
    sprite_batcher& ui_batcher() noexcept { return ui_batcher_; }
    // Silhouette sun-shadow batcher (shadow.vert/shadow.frag, MAX blend).
    sprite_batcher& shadow_batcher() noexcept { return shadow_batcher_; }
    font_engine& fonts() noexcept { return fonts_; }
    gpu_atlas& atlas() noexcept { return atlas_; }
    gpu_geometry& geometry() noexcept { return geometry_; }

    // Phase 2i-B-2 deferred-UI queue. Legacy GeometryRenderer calls
    // (sdl_geometry.cpp) push pre-baked sprite_instances here; the
    // sdltiles refresh_display() drains them through ui_batcher each
    // frame so existing call sites need no source edits.
    void queue_ui_rect(float x, float y, float w, float h, float r, float g, float b, float a);
    bool ui_rects_empty() const noexcept {
        return ui_rect_queue_.empty() && ui_rect_transient_.empty();
    }
    // Per-slice ordered flush of the composited UI. Walks ui_slice_spans_,
    // drawing each adaptor slice's background rects (white texture) THEN its
    // glyphs, slices in z-order, so a higher slice's opaque backgrounds
    // occlude lower slices' glyphs (the old two-phase all-rects-then-all-
    // glyphs flush could not, so overlapping windows mashed together).
    // Binds textures internally; caller only needs an active begin_pass.
    // Drains transient overlays last (on top). Drains WITHOUT clearing the
    // composited queues (ui_manager owns the reset via clear_ui_queues).
    void flush_ui(sprite_batcher& dst, SDL_GPUSampler* sampler);

    // Phase 2i-B-6 scaffolding (additive — no callers wired yet).
    // Upload an RGBA SDL_Surface into a fresh SDL_GPUTexture (one
    // texture per surface, sized to the surface). Caller owns the
    // returned handle and must call SDL_ReleaseGPUTexture on the
    // device when done. Used by the font glyph cache: each cached
    // TTF glyph mirrors its CPU bitmap into one of these so the
    // ui_batcher can sample it.
    //
    // Returns nullptr on any failure (device not ready, transfer
    // buffer alloc, copy submit). Logs via DC::SDL.
    SDL_GPUTexture* upload_surface_to_gpu_texture(SDL_Surface* surface);

    // Phase 2i-B-5 helper. Upload pixels from `src` into a
    // sub-rectangle of `dst` at (dst_x,dst_y). If `src_rect` is
    // non-null, only that sub-rect of `src` is uploaded (size of
    // that rect, not the full surface); otherwise the entire `src`
    // surface is uploaded. The destination texture must be sized
    // large enough for the upload region and have
    // SDL_GPU_TEXTUREUSAGE_SAMPLER. Used by dynamic_atlas to copy
    // each tile's CPU bitmap onto its sheet's GPU mirror at the
    // same atlas-packed offset the legacy SDL_Renderer path uses —
    // staging surfaces in cata_tiles are size-rounded so the
    // sub-rect form is mandatory there to avoid clobbering
    // neighbouring sprites in the atlas with stale padding bytes.
    //
    // Returns true on success; false if any step fails (logs via
    // DC::SDL). Idempotent — caller owns the destination texture.
    bool upload_surface_subregion_to_gpu_texture(
        SDL_GPUTexture* dst, int dst_x, int dst_y, SDL_Surface* src,
        const SDL_Rect* src_rect = nullptr);

    // Allocate a SAMPLER-only SDL_GPUTexture of the given size and
    // RGBA format. Returns nullptr on failure. Caller owns the
    // returned handle — pair with SDL_ReleaseGPUTexture or wrap in
    // gpu_texture_unique_ptr.
    SDL_GPUTexture* create_rgba_gpu_texture(int w, int h);

    // Font glyph draw queue. Each entry binds its own texture (no
    // shared atlas yet), so flush iterates and issues one set_texture
    // + one draw per glyph. Acceptable for the typical 500–2000
    // glyphs/frame the game renders; an atlas pack is a later opt.
    //
    // The full-texture overload defaults UV to (0,0)..(1,1) — useful
    // for per-glyph textures (CachedTTFFont). The UV overload accepts
    // a sub-rect for callers that pack multiple glyphs into one
    // atlas sheet (BitmapFont's per-colour `ascii` textures).
    // `lit` controls whether the lighting fragment-shader path runs
    // for this glyph. Default false treats glyphs as screen-space HUD
    // (lighting math short-circuited so it cannot dim HUD text). Pass
    // true for in-world floating text that should respect ambient /
    // emitter / sun contributions.
    // `rotation` (radians, clockwise, about the quad centre) lets HUD glyphs
    // spin — used by animated sidebar icons. Default 0 (no rotation).
    void queue_font_glyph(
        SDL_GPUTexture* glyph_tex, float dst_x, float dst_y, float dst_w, float dst_h, float r,
        float g, float b, float a, bool lit = false, float rotation = 0.0f);
    void queue_font_glyph(
        SDL_GPUTexture* glyph_tex, float dst_x, float dst_y, float dst_w, float dst_h, float src_u,
        float src_v, float src_uw, float src_vh, float r, float g, float b, float a,
        bool lit = false, float rotation = 0.0f);

    // Phase 2i-B-5 part 3: tile sprite queue. cata_tiles' draw paths
    // enqueue every map sprite here during the per-window redraw;
    // refresh_display drains them inside the tile_batcher pass after
    // the bridge blit. Per-entry texture binding mirrors the font
    // queue — set_texture on each sprite_batcher::draw call. Atlas
    // packing already groups runs of sprites onto the same SDL_GPU
    // texture, so consecutive same-texture entries get one
    // set_texture and N draws.
    void queue_tile_sprite(SDL_GPUTexture* atlas_tex, const sprite_instance& inst);
    // When set, queue_tile_sprite redirects into the UNLIT UI/font-glyph
    // path (current adaptor slice) instead of the lit world tile queue.
    // Used by the GPU minimap to draw OMT atlas sprites as an unlit overlay
    // without a separate pass or shader change. See camera_modernization.md P3.
    void set_unlit_overlay_route(bool on) noexcept { unlit_overlay_route_ = on; }
    bool tile_sprites_empty() const noexcept { return tile_sprite_queue_.empty(); }
    void flush_tile_sprites(sprite_batcher& dst, SDL_GPUSampler* sampler);

    // Hover-outline (HOVER_OUTLINE_PLAN.md). Number of tile sprites queued so
    // far — cata_tiles records this before/after a creature's sprites to mark
    // the range to outline.
    std::size_t tile_sprite_count() const noexcept { return tile_sprite_queue_.size(); }
    // Read the tile sprites in [start,end), generate 8 offset silhouette copies
    // of each (flat outline colour, outline flag set), and splice them in at
    // `start` so they render BEHIND the originals. The union of the offset
    // copies forms ONE clean composite ring around the whole creature (base
    // body + every worn-item overlay), with no per-item inner seams.
    void build_outline_ring(
        std::size_t start, std::size_t end, float r, float g, float b, float a, float radius_px,
        float alpha_cut);

    // Silhouette sun-shadow mask (Phase 1). Opens a pass on shadow_batcher_
    // into the shadow_mask_ target (LOADOP_CLEAR), stamps it with the cached
    // per-frame sun/geometry (vertex shear only — no lighting storage
    // buffers), and drains the TALL subset of tile_sprite_queue_
    // (dst_h > 1.5*tile_px) as sheared coverage. Drains WITHOUT clearing the
    // queue (Pass W re-drains the full set after). Drive before Pass W.
    // proj_w/h = the game-view projection extent (matches Pass W's push).
    void flush_shadow_casters(SDL_GPUCommandBuffer* cb, std::uint32_t proj_w, std::uint32_t proj_h);
    // Apply a GPU scissor rect to tile_sprite draws from this point forward.
    // Pass nullptr to restore full-viewport rendering. Closes the current
    // batcher segment so previously queued sprites are unaffected.
    void set_tile_scissor(const SDL_Rect* rect);
    void clear_tile_scissor();
    // Per-frame light state the caller has to fill in (camera + time +
    // tile geometry) before each frame is drawn. Everything the
    // *lighting subsystem itself* owns (emitter texture, SDF + sky_vis
    // textures, sampler, emitter count, SDF dimensions) is resolved
    // inside `begin_lighting_frame` — the caller does not pass those.
    // Q10 refactor: replaces the 13-positional-arg `set_tile_lighting`
    // god-call with a struct-grouped per-frame inputs object, matching
    // the canonical "frequency-tiered uniform struct" pattern used in
    // Vulkan / D3D12 / Metal render APIs.
    struct frame_light_inputs {
        float tile_pixel_size = 32.0f;
        float z_level = 0.0f;
        float ambient = 0.05f;
        float camera_off_x = 0.0f;
        float camera_off_y = 0.0f;
        sun_params sun = {};
        // Debug visualisation + runtime tuning knobs. Driven by the
        // F5-toggled debug widget in sdltiles.cpp; defaults are no-ops.
        debug_params debug = {};
    };

    // Stamp per-frame lighting state on the tile_batcher. Must be
    // called BEFORE begin_pass each frame so end_pass's per-segment
    // uniform push reads this frame's values, not the previous
    // frame's. See Q1 fix in /Users/.../plans/i-want-you-to-wise-nygaard.md.
    void begin_lighting_frame(const frame_light_inputs& in);

    // Phase 2i-B-7g: per-adaptor draw routing. ui_manager calls this
    // around each ui_adaptor's redraw_cb so the queue_* helpers below
    // push into the adaptor's retained slice (cleared per-adaptor at
    // the start of its callback) rather than the composited output.
    // Pass nullptr to restore composite-direct routing (used by code
    // paths outside the ui_manager redraw loop, e.g. cata_tiles in-game
    // map draws that happen inside an adaptor callback but use their
    // own tile queue).
    void set_current_slices(ui_adaptor_draw_slices* s) noexcept { current_slices_ = s; }

    // True while an adaptor's redraw_cb is routing draws into its retained
    // slice. The curses cell→GPU path (sdltiles.cpp draw_window) checks
    // this to bypass its dirty-cell diff: a redrawing adaptor must re-push
    // its FULL window into the slice (the slice was just cleared), not only
    // the cells that changed vs the persistent framebuffer cache.
    bool slice_routing_active() const noexcept { return current_slices_ != nullptr; }

    // Transient routing for per-frame ephemeral overlays (e.g. the
    // LIGHT-DBG widget rendered inside refresh_display). When enabled
    // and no slice is active, queue_ui_rect / queue_font_glyph push
    // into transient queues that flush_* drain + clear every frame —
    // unlike the composited queues, which are cleared only by the
    // ui_manager redraw cycle and would accumulate widget pushes on
    // no-input frames.
    void set_transient_routing(bool on) noexcept { transient_routing_ = on; }

    // ui_manager-side composition. Called after the redraw loop with each
    // ui_adaptor's slice, in stack order (bottom-up = z-order). Appends the
    // slice's rects + glyphs to the composited output and records a slice
    // boundary so flush_ui preserves per-slice z-order (rects-then-glyphs).
    void append_slice(
        const std::vector<sprite_instance>& rects, const std::vector<font_glyph_draw>& glyphs);

    // Phase 2i-B-5 lifecycle fix. Legacy SDL_Renderer's display_buffer
    // is a persistent render target — content stays between redraws.
    // The GPU queues here are transient and used to be cleared by
    // flush, which made refresh_display run with empty queues on
    // every no-input frame and blanked the swapchain to black.
    //
    // Fix: ui_adaptor::redraw_invalidated() now calls
    // clear_frame_queues() at the start of each redraw cycle. The
    // flush_* methods drain WITHOUT clearing, so refresh_display
    // can re-fire the same queues on no-input frames and reproduce
    // the same draws as long as the last redraw cycle's content
    // remains valid. The next redraw clears + repopulates.
    // Clear only the UI and font queues — call at the start of any
    // partial UI redraw (e.g. sidebar, tooltip) that does NOT also
    // redraw the tile map. Leaves tile_sprite_queue intact so terrain
    // remains visible on frames where cata_tiles::draw() isn't called.
    void clear_ui_queues() noexcept;
    // Clear only the tile sprite queue — call at the top of
    // cata_tiles::draw() before re-enqueuing all terrain/mob/vehicle sprites.
    void clear_tile_queue() noexcept;
    // Clear all three queues — used for full redraws.
    void clear_frame_queues() noexcept;
    bool font_glyphs_empty() const noexcept {
        return font_glyph_queue_.empty() && font_glyph_transient_.empty();
    }

    // Nearest-filter sampler used by all GPU draw passes (tile sprites,
    // UI rects, font glyphs). Created in init(); live for the full
    // lifetime of the render_state.
    SDL_GPUSampler* gpu_sampler() const noexcept { return gpu_sampler_; }

    // Phase 3: emitter pipeline ------------------------------------------
    event_queue& emitter_events() noexcept { return emitter_events_; }
    emitter_collector* collector() noexcept { return collector_.get(); }

    // Phase 4: SDF + transparency ----------------------------------------
    sdf_pass& sdf() noexcept { return sdf_; }

    // UI compositor target. Persistent offscreen texture the UI renders
    // into; refresh_display blits it over the lit-world pass. nullptr
    // until init() succeeds. Owned here; lives for the render_state's
    // lifetime.
    ui_composite_target* ui_target() noexcept { return ui_target_.get(); }

    // World accumulation layer. Same persistent-texture class as the UI
    // compositor, but used as a full-frame accumulation buffer: tiles
    // render into it with LOADOP_LOAD (preserve), so a frame that enqueues
    // no tiles (partial UI redraw) retains the last world instead of
    // flashing black. consume_dirty() is reused here as the "needs full
    // clear" signal (set on init + resize). nullptr until init() succeeds.
    ui_composite_target* world_target() noexcept { return world_target_.get(); }

    // Tonemapped (LDR) world target: the HDR world_target() resolved through
    // the tonemap pass for display. Pass B blits this instead of the raw
    // HDR world. Swapchain format. nullptr until init() succeeds.
    ui_composite_target* world_ldr_target() noexcept { return world_ldr_target_.get(); }
    // Intermediate UI composite target for post-processing (Phase 9).
    // Renders world + UI into this, then ui_post_pass composites to swapchain.
    ui_composite_target* ui_post_target() noexcept { return ui_post_target_.get(); }

    // Fullscreen tonemap pass (HDR world_target → world_ldr_target).
    tonemap_pass& tonemap() noexcept { return tonemap_; }

    // GPU compute GI pass (Stage 1 of GI_COMPUTE_AND_PERF_PLAN — replaced the
    // fragment radiance_cascade_pass, which failed D3D12 pipeline creation).
    // Driven from refresh_display; its gi_buffer() is the sprite's sole GI
    // input. Named gi() (was rc()).
    gi_compute_pass& gi() noexcept { return gi_; }

    // GPU compute sky/sun directional skylight pass (Stage 2a of
    // GI_COMPUTE_AND_PERF_PLAN). Driven from refresh_display alongside gi();
    // its sky_buffer() feeds sprite.frag as SkyBuf (directional sky-access +
    // sun occlusion), replacing the flat sky ambient + inline sun shadow.
    sky_sun_pass& sky() noexcept { return sky_; }

    // Bloom post pass (Step-4). Driven from refresh_display between Pass W
    // and the tonemap resolve; composites additively into world_target.
    bloom_pass& bloom() noexcept { return bloom_; }

    // Volumetric sun-shaft "lit fog" (Step-6 / C2). Driven from
    // refresh_display inside the Pass-W-ran block, before bloom.
    volumetric_pass& volumetric() noexcept { return volumetric_; }

    // High-fidelity rain effect (droplets + splat map). Driven from
    // refresh_display between world pass and tonemap; draws droplets onto
    // world_target then runs a fullscreen splat fade/accumulate pass.
    rain_effect& rain() noexcept { return rain_; }
    // GPU shader-based sound wave visualization (expanding ring wavefronts).
    // Driven from refresh_display inside the world pass, after tiles.
    sound_wave_pass& sound_waves() noexcept { return sound_waves_; }
    // Atmospheric HUD particle effects (embers, dust, pollen, snow).
    // Driven from composite_swapchain_pass_b after RmlUi renders.
    hud_particle_effect& hud_particles() noexcept { return hud_particles_; }
    // UI post-processing: bloom + chromatic aberration (Phase 9).
    ui_post_pass& ui_post() noexcept { return ui_post_; }

    // GPU JFA SDF pass (P3). Three compute dispatches: seed → flood → resolve.
    gpu_sdf_pass& gpu_sdf() noexcept { return gpu_sdf_; }

    // Silhouette sun-shadow mask (Phase 1). Screen-space coverage texture
    // (swapchain format) the shadow_batcher_ renders sheared caster
    // silhouettes into; sized to world_target. nullptr until init() succeeds.
    ui_composite_target* shadow_mask() noexcept { return shadow_mask_.get(); }

    // Synchronous GPU→CPU readback of an RGBA GPU texture. Downloads the
    // given texture into `pixels` (resized to w*h*4 bytes, RGBA order).
    // The texture must be SAMPLER | COLOR_TARGET and sized to (w,h).
    // Returns true on success. Used by save_screenshot and the render
    // regression test harness.
    auto capture_texture_to_rgba(SDL_GPUTexture* tex, int w, int h, std::vector<uint8_t>& pixels)
        -> bool;

private:
    gpu_device device_;
    sprite_batcher tile_batcher_;
    sprite_batcher ui_batcher_;
    sprite_batcher shadow_batcher_;
    font_engine fonts_;
    gpu_atlas atlas_{2048, 2048, 32, 32};
    gpu_geometry geometry_;

    // Composited UI rect output. ui_manager fills this by concatenating
    // per-adaptor slices in z-order after the redraw loop completes.
    std::vector<sprite_instance> ui_rect_queue_;

    // Composited font glyph output. Same lifecycle as ui_rect_queue_.
    std::vector<font_glyph_draw> font_glyph_queue_;

    // Per-slice boundaries into the two composited queues above, in z-order.
    // Each entry is the cumulative END index (ui_rect_queue_.size(),
    // font_glyph_queue_.size()) after appending one adaptor's slice, so
    // flush_ui can replay each slice as rects-then-glyphs and let higher
    // slices occlude lower ones. Same lifecycle as the queues.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ui_slice_spans_;

    // When non-null, queue_ui_rect / queue_font_glyph route into this
    // adaptor's per-window slice instead of the composited output above.
    // Set by ui_manager around each redraw_cb; cleared between callbacks.
    ui_adaptor_draw_slices* current_slices_ = nullptr;

    // Per-frame transient overlay queues. Populated when
    // transient_routing_ is on (no slice active); drained + cleared by
    // flush_ui every frame so refresh_display overlays do not pile up on
    // no-input frames.
    std::vector<sprite_instance> ui_rect_transient_;
    std::vector<font_glyph_draw> font_glyph_transient_;
    bool transient_routing_ = false;

    // Tile sprite queue (2i-B-5 part 3). Same shape as font glyph
    // queue; semantically separate so cata_tiles' map draws can land
    // in the tile_batcher pass instead of the ui_batcher pass.
    struct tile_sprite_draw {
        SDL_GPUTexture* texture;
        sprite_instance inst;
    };
    std::vector<tile_sprite_draw> tile_sprite_queue_;

    // When true, queue_tile_sprite redirects sprites into the unlit
    // UI/font-glyph path (GPU minimap overlay). See set_unlit_overlay_route.
    bool unlit_overlay_route_ = false;

    SDL_GPUSampler* gpu_sampler_ = nullptr;

    // Phase 3: emitter pipeline
    event_queue emitter_events_;
    std::unique_ptr<emitter_collector> collector_;

    // Phase 4: SDF + transparency
    sdf_pass sdf_;

    // UI compositor target (offscreen UI render-to-texture).
    std::unique_ptr<ui_composite_target> ui_target_;

    // World accumulation target (persistent lit-world layer, HDR once
    // step 1b lands).
    std::unique_ptr<ui_composite_target> world_target_;

    // Silhouette sun-shadow mask (Phase 1). Sized to world_target_; the
    // shadow_batcher_ renders sheared caster coverage into it before Pass W.
    std::unique_ptr<ui_composite_target> shadow_mask_;

    // Per-frame inputs cached by begin_lighting_frame so flush_shadow_casters
    // can stamp the shadow_batcher_ with the same sun/geometry (the shear is
    // vertex-side; no lighting storage buffers are bound for the mask).
    frame_light_inputs last_frame_inputs_;

    // Tonemapped LDR resolve of world_target_ (swapchain format) + the
    // fullscreen tonemap pass that produces it.
    // Intermediate UI composite target for post-processing (Phase 9).
    // Renders world + UI into this, then ui_post_pass composites to swapchain.
    std::unique_ptr<ui_composite_target> ui_post_target_;
    std::unique_ptr<ui_composite_target> world_ldr_target_;
    tonemap_pass tonemap_;
    gi_compute_pass gi_;
    sky_sun_pass sky_;
    bloom_pass bloom_;
    hud_particle_effect hud_particles_;
    volumetric_pass volumetric_;
    rain_effect rain_;
    sound_wave_pass sound_waves_;
    ui_post_pass ui_post_;
    gpu_sdf_pass gpu_sdf_;
};

// Process-wide accessor. The object is constructed in init() and torn down
// in shutdown(); calling either more than once is a no-op.
render_state& get_render_state();

// Bring up the GPU stack against `visible_window`. The window must outlive
// the render_state. Returns true on success; on failure, logs a Warn and
// returns false (the caller is expected to fall back to whatever legacy
// path it still has). Idempotent: calling more than once with a live state
// is a no-op.
bool init_render_state_on(SDL_Window* visible_window);

// Idempotent. Safe to call from atexit / WinDestroy regardless of init
// outcome.
void shutdown_render_state() noexcept;

} // namespace lighting
