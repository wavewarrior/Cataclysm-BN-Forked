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

#include <vector>

namespace lighting
{

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
        bool ui_rects_empty() const noexcept { return ui_rect_queue_.empty(); }
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

        // Font glyph draw queue. Each entry binds its own texture (no
        // shared atlas yet), so flush iterates and issues one set_texture
        // + one draw per glyph. Acceptable for the typical 500–2000
        // glyphs/frame the game renders; an atlas pack is a later opt.
        void queue_font_glyph( SDL_GPUTexture *glyph_tex,
                               float dst_x, float dst_y, float dst_w, float dst_h,
                               float r, float g, float b, float a );
        bool font_glyphs_empty() const noexcept { return font_glyph_queue_.empty(); }
        // Drains `font_glyph_queue_` into `dst` using `sampler`. Caller
        // must have an active begin_pass on `dst`. Internally rebinds
        // the texture per glyph; trivial atlas-packing optimisation
        // belongs in a later commit.
        void flush_font_glyphs( sprite_batcher &dst, SDL_GPUSampler *sampler );

        // Phase 2i-B-3 screen bridge: a GPU texture mirroring the legacy
        // SDL_Renderer display_buffer plus a persistent transfer buffer
        // sized to one frame. The bridge keeps every legacy draw path
        // (sprites, fonts, minimap, vehicle_preview …) visible in one go
        // via a CPU readback + full-screen blit on the swapchain. Phases
        // 2i-B-4..7 progressively emit GPU draws directly and tear this
        // bridge down once every legacy path is gone.
        bool             bridge_ready( int w, int h );
        void             bridge_upload( SDL_GPUCommandBuffer *cb,
                                        const void *pixels, std::uint32_t row_stride,
                                        int w, int h );
        SDL_GPUTexture  *bridge_texture() const noexcept { return bridge_tex_; }
        SDL_GPUSampler  *bridge_sampler() const noexcept { return bridge_sampler_; }
        int              bridge_width()   const noexcept { return bridge_w_; }
        int              bridge_height()  const noexcept { return bridge_h_; }

    private:
        gpu_device     device_;
        sprite_batcher tile_batcher_;
        sprite_batcher ui_batcher_;
        font_engine    fonts_;
        gpu_atlas      atlas_{ 2048, 2048, 32, 32 };
        gpu_geometry   geometry_;

        std::vector<sprite_instance> ui_rect_queue_;

        // Font glyph queue (additive scaffolding for 2i-B-6).
        struct font_glyph_draw {
            SDL_GPUTexture *texture;
            sprite_instance inst;
        };
        std::vector<font_glyph_draw> font_glyph_queue_;

        // Bridge state (phase 2i-B-3).
        SDL_GPUTexture        *bridge_tex_     = nullptr;
        SDL_GPUSampler        *bridge_sampler_ = nullptr;
        SDL_GPUTransferBuffer *bridge_xfer_    = nullptr;
        std::uint32_t          bridge_xfer_capacity_ = 0;
        int                    bridge_w_ = 0;
        int                    bridge_h_ = 0;
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
