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

#include "gpu_device.h"
#include "sprite_batcher.h"
#include "font_engine.h"
#include "gpu_atlas.h"
#include "gpu_geometry.h"
#include "event_queue.h"
#include "emitter_collector.h"
#include "sdf_pass.h"
#include "ui_adaptor_draw_slices.h"
#include "ui_composite_target.h"
#include "tonemap_pass.h"
#include "radiance_cascade_pass.h"

#include <memory>

#include <memory>
#include <vector>

namespace lighting
{

// Custom deleter that releases an SDL_GPUTexture against the live
// render_state device. Safe to invoke even if the render_state has
// already been shut down — leaks the texture in that case (process
// exit path; SDL releases everything on device teardown anyway).
struct gpu_texture_deleter {
    void operator()( SDL_GPUTexture *t ) const noexcept;
};
using gpu_texture_unique_ptr = std::unique_ptr<SDL_GPUTexture, gpu_texture_deleter>;


class render_state
{
    public:
        // Construct the stack. `host_window` must outlive this object and
        // is the window the GPU device claims. Throws std::runtime_error on
        // any sub-component init failure; partial state is rolled back.
        void init( SDL_Window *host_window );

        // Idempotent. Safe to call from WinDestroy regardless of init
        // outcome.
        void shutdown() noexcept;

        // True once init has completed and not been shut down.
        bool ready() const noexcept { return device_.ready(); }

        gpu_device     &device()        noexcept { return device_; }
        sprite_batcher &tile_batcher()  noexcept { return tile_batcher_; }
        sprite_batcher &ui_batcher()    noexcept { return ui_batcher_; }
        font_engine    &fonts()         noexcept { return fonts_; }
        gpu_atlas      &atlas()         noexcept { return atlas_; }
        gpu_geometry   &geometry()      noexcept { return geometry_; }

        // Phase 2i-B-2 deferred-UI queue. Legacy GeometryRenderer calls
        // (sdl_geometry.cpp) push pre-baked sprite_instances here; the
        // sdltiles refresh_display() drains them through ui_batcher each
        // frame so existing call sites need no source edits.
        void queue_ui_rect( float x, float y, float w, float h,
                            float r, float g, float b, float a );
        bool ui_rects_empty() const noexcept
        {
            return ui_rect_queue_.empty() && ui_rect_transient_.empty();
        }
        // Drains `ui_rect_queue_` into `dst` (assumes the caller has an
        // active pass + white-tex segment open). Clears the queue.
        void flush_ui_rects( sprite_batcher &dst );

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
        SDL_GPUTexture *upload_surface_to_gpu_texture( SDL_Surface *surface );

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
            SDL_GPUTexture *dst, int dst_x, int dst_y,
            SDL_Surface *src, const SDL_Rect *src_rect = nullptr );

        // Allocate a SAMPLER-only SDL_GPUTexture of the given size and
        // RGBA format. Returns nullptr on failure. Caller owns the
        // returned handle — pair with SDL_ReleaseGPUTexture or wrap in
        // gpu_texture_unique_ptr.
        SDL_GPUTexture *create_rgba_gpu_texture( int w, int h );

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
        void queue_font_glyph( SDL_GPUTexture *glyph_tex,
                               float dst_x, float dst_y, float dst_w, float dst_h,
                               float r, float g, float b, float a,
                               bool lit = false, float rotation = 0.0f );
        void queue_font_glyph( SDL_GPUTexture *glyph_tex,
                               float dst_x, float dst_y, float dst_w, float dst_h,
                               float src_u, float src_v, float src_uw, float src_vh,
                               float r, float g, float b, float a,
                               bool lit = false, float rotation = 0.0f );

        // Phase 2i-B-5 part 3: tile sprite queue. cata_tiles' draw paths
        // enqueue every map sprite here during the per-window redraw;
        // refresh_display drains them inside the tile_batcher pass after
        // the bridge blit. Per-entry texture binding mirrors the font
        // queue — set_texture on each sprite_batcher::draw call. Atlas
        // packing already groups runs of sprites onto the same SDL_GPU
        // texture, so consecutive same-texture entries get one
        // set_texture and N draws.
        void queue_tile_sprite( SDL_GPUTexture *atlas_tex,
                                const sprite_instance &inst );
        bool tile_sprites_empty() const noexcept { return tile_sprite_queue_.empty(); }
        void flush_tile_sprites( sprite_batcher &dst, SDL_GPUSampler *sampler );
        // Apply a GPU scissor rect to tile_sprite draws from this point forward.
        // Pass nullptr to restore full-viewport rendering. Closes the current
        // batcher segment so previously queued sprites are unaffected.
        void set_tile_scissor( const SDL_Rect *rect );
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
            float        tile_pixel_size = 32.0f;
            float        z_level         = 0.0f;
            float        ambient         = 0.05f;
            float        camera_off_x    = 0.0f;
            float        camera_off_y    = 0.0f;
            sun_params   sun             = {};
            // Debug visualisation + runtime tuning knobs. Driven by the
            // F5-toggled debug widget in sdltiles.cpp; defaults are no-ops.
            debug_params debug           = {};
        };

        // Stamp per-frame lighting state on the tile_batcher. Must be
        // called BEFORE begin_pass each frame so end_pass's per-segment
        // uniform push reads this frame's values, not the previous
        // frame's. See Q1 fix in /Users/.../plans/i-want-you-to-wise-nygaard.md.
        void begin_lighting_frame( const frame_light_inputs &in );

        // Phase 2i-B-7g: per-adaptor draw routing. ui_manager calls this
        // around each ui_adaptor's redraw_cb so the queue_* helpers below
        // push into the adaptor's retained slice (cleared per-adaptor at
        // the start of its callback) rather than the composited output.
        // Pass nullptr to restore composite-direct routing (used by code
        // paths outside the ui_manager redraw loop, e.g. cata_tiles in-game
        // map draws that happen inside an adaptor callback but use their
        // own tile queue).
        void set_current_slices( ui_adaptor_draw_slices *s ) noexcept { current_slices_ = s; }

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
        void set_transient_routing( bool on ) noexcept { transient_routing_ = on; }

        // ui_manager-side composition. Called after the redraw loop with
        // each ui_adaptor's slice, in stack order (bottom-up = z-order).
        // Appends to the composited output that refresh_display drains.
        void append_ui_rects( const std::vector<sprite_instance> &src );
        void append_font_glyphs( const std::vector<font_glyph_draw> &src );

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
        bool font_glyphs_empty() const noexcept
        {
            return font_glyph_queue_.empty() && font_glyph_transient_.empty();
        }
        // Drains `font_glyph_queue_` into `dst` using `sampler`. Caller
        // must have an active begin_pass on `dst`. Internally rebinds
        // the texture per glyph; trivial atlas-packing optimisation
        // belongs in a later commit.
        void flush_font_glyphs( sprite_batcher &dst, SDL_GPUSampler *sampler );

        // Nearest-filter sampler used by all GPU draw passes (tile sprites,
        // UI rects, font glyphs). Created in init(); live for the full
        // lifetime of the render_state.
        SDL_GPUSampler  *gpu_sampler() const noexcept { return gpu_sampler_; }

        // Phase 3: emitter pipeline ------------------------------------------
        event_queue       &emitter_events() noexcept { return emitter_events_; }
        emitter_collector *collector()      noexcept { return collector_.get(); }

        // Phase 4: SDF + transparency ----------------------------------------
        sdf_pass &sdf() noexcept { return sdf_; }

        // UI compositor target. Persistent offscreen texture the UI renders
        // into; refresh_display blits it over the lit-world pass. nullptr
        // until init() succeeds. Owned here; lives for the render_state's
        // lifetime.
        ui_composite_target *ui_target() noexcept { return ui_target_.get(); }

        // World accumulation layer. Same persistent-texture class as the UI
        // compositor, but used as a full-frame accumulation buffer: tiles
        // render into it with LOADOP_LOAD (preserve), so a frame that enqueues
        // no tiles (partial UI redraw) retains the last world instead of
        // flashing black. consume_dirty() is reused here as the "needs full
        // clear" signal (set on init + resize). nullptr until init() succeeds.
        ui_composite_target *world_target() noexcept { return world_target_.get(); }

        // Tonemapped (LDR) world target: the HDR world_target() resolved through
        // the tonemap pass for display. Pass B blits this instead of the raw
        // HDR world. Swapchain format. nullptr until init() succeeds.
        ui_composite_target *world_ldr_target() noexcept { return world_ldr_target_.get(); }

        // Fullscreen tonemap pass (HDR world_target → world_ldr_target).
        tonemap_pass &tonemap() noexcept { return tonemap_; }

        // Radiance-cascade GI pass (Step-3 Phase 2). Driven from refresh_display.
        radiance_cascade_pass &rc() noexcept { return rc_; }

        // Select the sprite's GI source: true = GPU radiance cascade (default),
        // false = CPU 1-bounce indirect texture (the A/B oracle). F4 toggle.
        void set_gi_use_rc( bool use_rc ) noexcept { gi_use_rc_ = use_rc; }
        bool gi_use_rc() const noexcept { return gi_use_rc_; }

    private:
        gpu_device     device_;
        sprite_batcher tile_batcher_;
        sprite_batcher ui_batcher_;
        font_engine    fonts_;
        gpu_atlas      atlas_{ 2048, 2048, 32, 32 };
        gpu_geometry   geometry_;

        // Composited UI rect output. ui_manager fills this by concatenating
        // per-adaptor slices in z-order after the redraw loop completes.
        std::vector<sprite_instance> ui_rect_queue_;

        // Composited font glyph output. Same lifecycle as ui_rect_queue_.
        std::vector<font_glyph_draw> font_glyph_queue_;

        // When non-null, queue_ui_rect / queue_font_glyph route into this
        // adaptor's per-window slice instead of the composited output above.
        // Set by ui_manager around each redraw_cb; cleared between callbacks.
        ui_adaptor_draw_slices *current_slices_ = nullptr;

        // Per-frame transient overlay queues. Populated when
        // transient_routing_ is on (no slice active); drained + cleared by
        // flush_ui_rects / flush_font_glyphs every frame so refresh_display
        // overlays do not pile up on no-input frames.
        std::vector<sprite_instance> ui_rect_transient_;
        std::vector<font_glyph_draw> font_glyph_transient_;
        bool                         transient_routing_ = false;

        // Tile sprite queue (2i-B-5 part 3). Same shape as font glyph
        // queue; semantically separate so cata_tiles' map draws can land
        // in the tile_batcher pass instead of the ui_batcher pass.
        struct tile_sprite_draw {
            SDL_GPUTexture *texture;
            sprite_instance inst;
        };
        std::vector<tile_sprite_draw> tile_sprite_queue_;

        SDL_GPUSampler        *gpu_sampler_ = nullptr;

        // Phase 3: emitter pipeline
        event_queue                          emitter_events_;
        std::unique_ptr<emitter_collector>   collector_;

        // Phase 4: SDF + transparency
        sdf_pass  sdf_;

        // UI compositor target (offscreen UI render-to-texture).
        std::unique_ptr<ui_composite_target> ui_target_;

        // World accumulation target (persistent lit-world layer, HDR once
        // step 1b lands).
        std::unique_ptr<ui_composite_target> world_target_;

        // Tonemapped LDR resolve of world_target_ (swapchain format) + the
        // fullscreen tonemap pass that produces it.
        std::unique_ptr<ui_composite_target> world_ldr_target_;
        tonemap_pass                         tonemap_;
        radiance_cascade_pass                rc_;
        bool                                 gi_use_rc_ = true; // RC vs CPU GI (F4 A/B)
};

// Process-wide accessor. The object is constructed in init() and torn down
// in shutdown(); calling either more than once is a no-op.
render_state &get_render_state();

// Bring up the GPU stack against `visible_window`. The window must outlive
// the render_state. Returns true on success; on failure, logs a Warn and
// returns false (the caller is expected to fall back to whatever legacy
// path it still has). Idempotent: calling more than once with a live state
// is a no-op.
bool init_render_state_on( SDL_Window *visible_window );

// Idempotent. Safe to call from atexit / WinDestroy regardless of init
// outcome.
void shutdown_render_state() noexcept;

} // namespace lighting
