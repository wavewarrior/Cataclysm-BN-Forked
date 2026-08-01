#include "render_state.h"

#include "debug.h"
#include "emitter_collector.h"
#include "game_constants.h"
#include "sdf_pass.h"
#include "shader_compiler.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

namespace {
// Atomic gate so try_init / shutdown are safe to call more than once.
// Phase 2i-A's hidden-window holder is gone — the GPU device now claims
// the *visible* window that the caller hands us.
std::atomic<bool> initialised{false};

} // namespace

void gpu_texture_deleter::operator()(SDL_GPUTexture* t) const noexcept {
    if (!t) { return; }
    auto& rs = get_render_state();
    if (rs.ready()) { SDL_ReleaseGPUTexture(rs.device().raw(), t); }
    // If render_state is already shut down, the GPU device is too —
    // SDL has released all underlying objects on device teardown. Leak
    // the handle (which now points at freed memory).
}

void render_state::init(SDL_Window* host_window) {
    device_.init(host_window, /*debug=*/true, /*vsync=*/false);

    init_shader_compiler();

    const SDL_GPUTextureFormat fmt = device_.swapchain_format();

    pipeline_desc tile_desc{};
    tile_desc.color_target_format = fmt;
    tile_batcher_.init(device_, tile_desc, "tile_batcher");

    pipeline_desc ui_desc{};
    ui_desc.color_target_format = fmt;
    ui_batcher_.init(device_, ui_desc, "ui_batcher");

    // Silhouette sun-shadow batcher (Phase 1). Same instance engine, but
    // shadow.vert (shear) + shadow.frag (alpha→coverage), MAX colour+alpha blend
    // so overlapping tree shadows take the max coverage (no double-darken), and
    // push_frag_lighting_uniforms=false because shadow.frag declares no fragment
    // cbuffers. Renders into shadow_mask_ (swapchain format).
    pipeline_desc shadow_desc{};
    shadow_desc.color_target_format = fmt;
    shadow_desc.vert_name = "shadow.vert.hlsl";
    shadow_desc.frag_name = "shadow.frag.hlsl";
    shadow_desc.push_frag_lighting_uniforms = false;
    shadow_desc.src_color_blend = SDL_GPU_BLENDFACTOR_ONE;
    shadow_desc.dst_color_blend = SDL_GPU_BLENDFACTOR_ONE;
    shadow_desc.color_blend_op = SDL_GPU_BLENDOP_MAX;
    shadow_desc.src_alpha_blend = SDL_GPU_BLENDFACTOR_ONE;
    shadow_desc.dst_alpha_blend = SDL_GPU_BLENDFACTOR_ONE;
    shadow_desc.alpha_blend_op = SDL_GPU_BLENDOP_MAX;
    shadow_batcher_.init(device_, shadow_desc, "shadow_batcher");

    fonts_.init(device_, fmt);
    atlas_.init(device_);
    geometry_.init(device_);

    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = SDL_GPU_FILTER_NEAREST;
    si.mag_filter = SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    gpu_sampler_ = SDL_CreateGPUSampler(device_.raw(), &si);

    // Phase 3: start the emitter collector thread.
    collector_ = std::make_unique<emitter_collector>(*this);

    // Phase 4: SDF / transparency / sky_vis textures.
    //
    // Size the GPU textures for the WORST-CASE bubble (REALITY_BUBBLE_SIZE_MAX
    // × SEEX). The CPU vectors and shader sampling use the *runtime* mapsize
    // passed into sdf_pass::upload() each frame — texture is just storage.
    //
    // Why not g_mapsize at init time:
    //   WinCreate runs BEFORE options are fully resolved into g_mapsize for
    //   some configurations (and before world load can override it). Init-time
    //   g_mapsize=11 + world load to g_mapsize=15 produced a 132x132 texture
    //   fed 180x180 of CPU data — the upload's first 17424 of 32400 floats
    //   landed in the texture with completely garbled row mapping → shader
    //   sampled wrong tiles → SDF view uniformly red.
    //
    // Max-size cost: 192² × (4 sdf + 1 trans + 1 sky_vis + 4 storage) ≈ 370 KB
    // GPU. Per-frame upload only touches runtime_w × runtime_h tiles.
    const int rt_tiles = REALITY_BUBBLE_SIZE_MAX * SEEX;
    sdf_.init(device_, rt_tiles, rt_tiles);

    // GPU compute GI pass (Stage 1 of GI_COMPUTE_AND_PERF_PLAN — replaced the
    // fragment radiance_cascade_pass). gi_buffer() is tile-res (4 floats/tile,
    // x-major), sized to the same max map extent as the SDF; the sprite reads it
    // as GiBuf. Emitter + SDF buffers carry COMPUTE_STORAGE_READ so the field
    // pass can gather them.
    gi_.init(device_, static_cast<std::uint32_t>(rt_tiles), static_cast<std::uint32_t>(rt_tiles));

    // GPU compute sky/sun directional skylight pass (Stage 2a/2b). Same max tile
    // extent as the SDF/GI; sky_buffer() feeds sprite.frag as SkyBuf (rgb
    // directional sky-access + a celestial occlusion). Reads sdf_'s unified
    // coverage occluder buffer (occ_buffer(), which carries COMPUTE_STORAGE_READ).
    sky_.init(device_, static_cast<std::uint32_t>(rt_tiles), static_cast<std::uint32_t>(rt_tiles));

    // UI compositor target. Sized to the PHYSICAL (drawable) swapchain pixels
    // so the composite blit is 1:1; the resize hook in sdltiles keeps it in
    // sync with window size changes. A failed alloc leaves ui_target() != null
    // with a null texture(); callers must guard on texture() before use.
    {
        int pw = 0;
        int ph = 0;
        SDL_GetWindowSizeInPixels(host_window, &pw, &ph);
        if (pw <= 0 || ph <= 0) {
            pw = 1;
            ph = 1;
        }
        ui_target_ = std::make_unique<ui_composite_target>();
        ui_target_->init(device_, pw, ph);
        // Intermediate UI composite target for post-processing (Phase 9).
        // World accumulation layer. HDR (RGBA16F) so the lit result keeps
        // values >1 for the tonemap pass instead of clipping at the 8-bit
        // store. Guard on format support (universal on the min-spec, but a
        // silent miss = black screen) and fall back to swapchain 8-bit, which
        // is the pre-1b behaviour (identity tonemap → unchanged output).
        // init() leaves it dirty → the world pass treats that as "needs full
        // clear" on the first frame.
        SDL_GPUTextureFormat world_fmt = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        if (!SDL_GPUTextureSupportsFormat(
                device_.raw(), world_fmt, SDL_GPU_TEXTURETYPE_2D,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
            dbg(DL::Error) << "render_state: RGBA16F unsupported as color+sampler "
                              "target; HDR world target falls back to swapchain 8-bit "
                              "(bright lighting will clip).";
            world_fmt = device_.swapchain_format();
        } else {
            dbg(DL::Info) << "render_state: HDR world target = RGBA16F.";
        }
        world_target_ = std::make_unique<ui_composite_target>();
        world_target_->init(device_, pw, ph, world_fmt);

        // Silhouette sun-shadow mask. Screen-space coverage, sized to
        // world_target_ (physical pixels). RGBA16F + GRAPHICS_STORAGE_READ so
        // sprite.frag can .Load() it as a 2nd fragment storage texture (Phase 2,
        // the proven IndirectTex/rc path — NOT a 2nd sampler), plus SAMPLER for
        // the Phase-1 debug blit. RGBA16F is the format with confirmed
        // COLOR_TARGET|STORAGE_READ support on Metal (matches rc cascade); the
        // triple usage is guarded, falling back to storage-only (drops the debug
        // blit, which then routes through the sprite.frag debug_mode) if the
        // combo is unsupported.
        {
            // SDL_GPU forbids SAMPLER + GRAPHICS_STORAGE_READ on a single texture
            // (D3D12 cannot express both; the Metal backend tolerates it, which is
            // why this only crashed on Win11). sprite.frag reads the mask via
            // .Load() as a fragment storage texture (the proven IndirectTex/rc
            // path), so COLOR_TARGET | GRAPHICS_STORAGE_READ is all it needs; the
            // Phase-1 debug blit routes through sprite.frag debug_mode instead.
            SDL_GPUTextureFormat mask_fmt = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
            const SDL_GPUTextureUsageFlags mask_usage =
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ;
            if (!SDL_GPUTextureSupportsFormat(
                    device_.raw(), mask_fmt, SDL_GPU_TEXTURETYPE_2D, mask_usage)) {
                dbg(DL::Warn) << "render_state: shadow_mask RGBA16F "
                                 "COLOR|STORAGE_READ unsupported.";
            }
            shadow_mask_ = std::make_unique<ui_composite_target>();
            shadow_mask_->init(device_, pw, ph, mask_fmt, mask_usage);
        }

        // Tonemapped LDR resolve target (swapchain format) + the fullscreen
        // tonemap pass that fills it from world_target_. In step 1a the world
        // target is still 8-bit and the tonemap shader is identity, so this is
        // a pixel-identical passthrough; the RGBA16F flip (1b) and AgX (1c)
        // build on this seam. (LIGHTING_REWORK_PLAN.md step 1.)
        world_ldr_target_ = std::make_unique<ui_composite_target>();
        world_ldr_target_->init(device_, pw, ph);
        tonemap_.init(device_, device_.swapchain_format());

        // Bloom post pass (Step-4): half-res bright-pass + blur, composited
        // additively into world_target (world_fmt) before the tonemap resolve.
        bloom_.init(device_, world_fmt, static_cast<std::uint32_t>(pw),
                    static_cast<std::uint32_t>(ph));
        // Volumetric "lit fog" sun shafts (Step-6 / C2): a fullscreen additive
        // pass into world_target (world_fmt), driven before bloom. Owns no
        // textures, so no resize hook is needed.
        volumetric_.init(device_, world_fmt);

        // High-fidelity rain effect: world-targeted falling droplets + splashes.
        rain_.init(device_, world_fmt, static_cast<std::uint32_t>(pw),
                   static_cast<std::uint32_t>(ph));
        // World-locked sub-tile decal splatmap: persistent per-submap textures
        // composited into world_target between the terrain and entity halves of
        // Pass W. No-ops (ready() == false) when gfx/splatmap/stamps.json is
        // missing or lists no loadable stamps.
        splatmap_.init( device_, world_fmt );
        // GPU sound wave visualization pass (expanding ring wavefronts).
        sound_waves_.init(device_, world_fmt);
        // Box2D debug overlay line pass (world-target format, line-list topology).
        debug_lines_.init( device_, world_fmt );
        // Atmospheric HUD particle effects (Phase 8).
        hud_particles_.init( device_, device_.swapchain_format(),
                             static_cast<std::uint32_t>( pw ),
                             static_cast<std::uint32_t>( ph ) );

        // GPU JFA SDF pass (P3): seed → flood → resolve on SS-grid. Same max tile
        // extent as the CPU SDF; jfa_sdf_buffer() is scratch output for A/B vs CPU DT.
        gpu_sdf_.init(
            device_, static_cast<std::uint32_t>(rt_tiles), static_cast<std::uint32_t>(rt_tiles));
    }
}

void render_state::shutdown() noexcept {
    // Phase 3: stop collector thread before releasing GPU resources.
    collector_.reset();

    // Release the compositor textures + tonemap pass while the device is live.
    ui_target_.reset();
    world_target_.reset();
    shadow_mask_.reset();
    world_ldr_target_.reset();
    tonemap_.shutdown();
    gi_.shutdown();
    sky_.shutdown();
    bloom_.shutdown();
    volumetric_.shutdown();
    rain_.shutdown();
    splatmap_.shutdown();
    sound_waves_.shutdown();
    hud_particles_.shutdown();
    debug_lines_.shutdown();
    gpu_sdf_.shutdown();

    // Phase 4: release SDF textures.
    sdf_.shutdown(device_);

    if (device_.ready() && gpu_sampler_) { SDL_ReleaseGPUSampler(device_.raw(), gpu_sampler_); }
    gpu_sampler_ = nullptr;

    // Tear-down order is the reverse of init so each component still sees
    // a live device when it releases its own GPU resources.
    geometry_.shutdown();
    atlas_.shutdown();
    fonts_.shutdown();
    ui_batcher_.shutdown();
    tile_batcher_.shutdown();
    shutdown_shader_compiler();
    device_.shutdown();
}

void render_state::queue_ui_rect(
    float x, float y, float w, float h, float r, float g, float b, float a) {
    if (!device_.ready()) { return; }
    sprite_instance s{};
    s.dst_x = x;
    s.dst_y = y;
    s.dst_w = w;
    s.dst_h = h;
    s.src_u = 0.5f;
    s.src_v = 0.5f;
    s.src_uw = 0.0f;
    s.src_vh = 0.0f;
    s.tint_r = r;
    s.tint_g = g;
    s.tint_b = b;
    s.tint_a = a;
    // Route into the current adaptor's retained slice if we're inside a
    // redraw_cb. Otherwise fall through to the composited output (e.g.
    // background fills queued outside the ui_manager redraw loop).
    if (current_slices_) {
        current_slices_->ui_rects.push_back(s);
    } else if (transient_routing_) {
        ui_rect_transient_.push_back(s);
    } else {
        ui_rect_queue_.push_back(s);
    }
}

void render_state::set_tile_scissor(const SDL_Rect* rect) { tile_batcher_.set_scissor(rect); }

void render_state::clear_tile_scissor() { tile_batcher_.set_scissor(nullptr); }

void render_state::begin_lighting_frame(const frame_light_inputs& in) {
    // Cache for flush_shadow_casters: the silhouette-shadow batcher is stamped
    // separately (own pass, before Pass W) with the same tile geometry + sun so
    // the vertex shear tracks the sun. No lighting storage buffers there.
    last_frame_inputs_ = in;

    // Resolve everything render_state owns internally: emitter storage
    // buffer + count from the collector, SDF + sky_vis textures + dims
    // from the sdf_pass, sampler from this object. Caller only provides
    // what it alone knows (camera, time, tile geometry, ambient).
    SDL_GPUBuffer* ebuf = collector_ ? collector_->emitter_buffer() : nullptr;
    // The SDF-family storage buffers are allocated unconditionally in
    // sdf_pass::init (non-null regardless of populated()). Bind them ALWAYS so
    // every declared fragment storage-buffer slot is filled: the sprite pipeline
    // declares and reflects 5 storage buffers, and on D3D12 leaving a declared SRV
    // unbound trips "Missing fragment storage buffer binding!" → device removed
    // (this was the crash — !sdf_ready frames nulled these, so a lit segment
    // bound <5). Read-safety is handled SEPARATELY: sw/sh below stay 0 until
    // populated, so the shader's sdf_map_w>0 gate never reads the (until then
    // undefined) bytes. So do NOT null these on !sdf_ready.
    // SDF + sky_vis are fragment storage buffers (Metal mis-binds sampler-texture
    // Load → returned 0 for every fragment).
    const bool sdf_ready = sdf_.populated();
    SDL_GPUBuffer* sbuf = sdf_.sdf_buffer();
    SDL_GPUBuffer* kvis = sdf_.sky_vis_buffer();
    // GI source for the sprite's GiBuf: the GPU compute GI pass output
    // (gi_compute_pass, single-bounce). The fragment radiance_cascade_pass it
    // replaced failed D3D12 pipeline creation; the gather now runs in compute.
    // gi_buffer() is always allocated + zeroed at init (even if the compute
    // pipelines failed on this backend), so the sprite.frag GiBuf storage-buffer
    // slot always has a valid handle. When GI is disabled or the SDF isn't ready
    // it reads as zero (no GI); the shader also gates the GI term by gi_strength
    // / sdf_map_w. A null here would leave a declared fragment storage-buffer slot
    // unbound → D3D12 "missing binding".
    SDL_GPUBuffer* gibuf = gi_.gi_buffer();
    // Sky/sun directional skylight source for the sprite's SkyBuf (Stage 2a).
    // Always allocated + zeroed at init (even if the compute pipeline failed),
    // so the fragment storage-buffer slot always has a valid handle; reads as
    // zero (dark) until the first dispatch. Same all-or-none rationale as gibuf.
    SDL_GPUBuffer* skybuf = sky_.sky_buffer();
    const Uint32 ne = collector_ ? static_cast<Uint32>(collector_->last_count()) : 0u;
    const Uint32 sw = sdf_ready ? static_cast<Uint32>(sdf_.map_w()) : 0u;
    const Uint32 sh = sdf_ready ? static_cast<Uint32>(sdf_.map_h()) : 0u;

    tile_batcher_.set_lighting_resources(
        in.tile_pixel_size, in.z_level, ne, in.ambient, in.camera_off_x, in.camera_off_y, sw, sh,
        ebuf, sbuf, gpu_sampler_, kvis, gibuf, &in.sun, &in.debug, skybuf);

    // Silhouette sun-shadow mask (Phase 2): bind it as the tile batcher's 2nd
    // fragment storage texture (sprite.frag ShadowMask, t2/space2). Always
    // non-null after init; the shadow pass writes it before Pass W reads it.
    tile_batcher_.set_shadow_mask(shadow_mask_ ? shadow_mask_->texture() : nullptr);
}

void render_state::flush_ui(sprite_batcher& dst, SDL_GPUSampler* sampler) {
    // Composited queues: drain WITHOUT clearing — clear_ui_queues() at the top
    // of each ui_manager redraw cycle resets them. Lets refresh_display re-flush
    // the same draws on no-input frames instead of going black.
    //
    // Walk slices in z-order (ui_slice_spans_, cumulative end indices). For each
    // slice draw its background rects (white texture, unlit) THEN its glyphs, so
    // a higher slice's opaque backgrounds occlude lower slices' glyphs. The old
    // all-rects-then-all-glyphs flush submitted every glyph after every rect, so
    // an overlapping window (e.g. the targeting UI over the sidebar) could never
    // hide the window beneath it. UI is alpha-blended with no depth test, so
    // submission order is the only occlusion lever.
    SDL_GPUTexture* white = geometry_.white_texture();
    std::uint32_t r0 = 0;
    std::uint32_t g0 = 0;
    const auto draw_range = [&](std::uint32_t r1, std::uint32_t g1) {
        if (r1 > r0 && white) {
            dst.set_texture(white, sampler, /*is_lit=*/false);
            dst.draw(ui_rect_queue_.data() + r0, r1 - r0);
        }
        for (std::uint32_t k = g0; k < g1; ++k) {
            const font_glyph_draw& gd = font_glyph_queue_[k];
            dst.set_texture(gd.texture, sampler, gd.lit);
            dst.draw(gd.inst);
        }
        r0 = r1;
        g0 = g1;
    };
    if (sampler) {
        for (const auto& span : ui_slice_spans_) { draw_range(span.first, span.second); }
        // Tail: any rects/glyphs pushed directly (no slice span recorded).
        draw_range(static_cast<std::uint32_t>(ui_rect_queue_.size()),
                   static_cast<std::uint32_t>(font_glyph_queue_.size()));
    }
    // Transient overlays last (on top); drain AND clear every frame.
    // refresh_display re-populates these each frame (e.g. LIGHT-DBG widget);
    // without the per-frame clear the same overlay pushes would pile up.
    if (!ui_rect_transient_.empty() && white) {
        dst.set_texture(white, sampler, /*is_lit=*/false);
        dst.draw(ui_rect_transient_.data(), ui_rect_transient_.size());
        ui_rect_transient_.clear();
    }
    for (const font_glyph_draw& g : font_glyph_transient_) {
        dst.set_texture(g.texture, sampler, g.lit);
        dst.draw(g.inst);
    }
    font_glyph_transient_.clear();
}

void render_state::clear_ui_queues() noexcept {
    ui_rect_queue_.clear();
    font_glyph_queue_.clear();
    ui_slice_spans_.clear();
    // Defensive: transient queues are normally drained by flush each
    // frame; clearing here too prevents leftovers leaking across a
    // ui_manager redraw cycle if a flush was skipped (no swapchain etc).
    ui_rect_transient_.clear();
    font_glyph_transient_.clear();
    // The composited UI is about to be rebuilt (ui_manager re-appends every
    // adaptor's slice after this). Mark the compositor dirty so Pass A
    // re-renders it this frame. On frames where this is NOT called (a bare
    // refresh_display re-fire with no ui_manager redraw cycle), the compositor
    // stays clean → Pass A is skipped → the persistent texture is reused,
    // which is the partial-redraw flicker fix.
    if (ui_target_) { ui_target_->invalidate(); }
}

void render_state::clear_tile_queue() noexcept {
    tile_sprite_queue_.clear();
    // The cut index and quad list index into the queue we just dropped, so they
    // must go with it: a frame where cata_tiles::draw() does not re-record them
    // falls back to the single-pass Pass W.
    splat_cut_ = static_cast<std::size_t>( -1 );
    splat_quads_.clear();
}

void render_state::clear_frame_queues() noexcept {
    clear_ui_queues();
    clear_tile_queue();
}

SDL_GPUTexture* render_state::upload_surface_to_gpu_texture(SDL_Surface* surface) {
    if (!device_.ready() || !surface || surface->w <= 0 || surface->h <= 0) { return nullptr; }

    // Force the source pixels into RGBA32 so the GPU format can be
    // hard-coded. SDL_ConvertSurface returns a new owning surface; only
    // free it if we actually converted.
    SDL_Surface* src = surface;
    SDL_Surface* converted = nullptr;
    if (surface->format != SDL_PIXELFORMAT_RGBA32) {
        converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        if (!converted) {
            dbg(DL::Warn) << "upload_surface_to_gpu_texture: SDL_ConvertSurface failed: "
                          << SDL_GetError();
            return nullptr;
        }
        src = converted;
    }

    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = static_cast<std::uint32_t>(src->w);
    tci.height = static_cast<std::uint32_t>(src->h);
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device_.raw(), &tci);
    if (!tex) {
        dbg(DL::Warn) << "upload_surface_to_gpu_texture: create tex: " << SDL_GetError();
        if (converted) { SDL_DestroySurface(converted); }
        return nullptr;
    }

    const std::uint32_t row_bytes = static_cast<std::uint32_t>(src->w) * 4;
    const std::uint32_t total_bytes = row_bytes * static_cast<std::uint32_t>(src->h);

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = total_bytes;
    SDL_GPUTransferBuffer* xfer = SDL_CreateGPUTransferBuffer(device_.raw(), &tbi);
    if (!xfer) {
        dbg(DL::Warn) << "upload_surface_to_gpu_texture: xfer alloc: " << SDL_GetError();
        SDL_ReleaseGPUTexture(device_.raw(), tex);
        if (converted) { SDL_DestroySurface(converted); }
        return nullptr;
    }
    void* mapped = SDL_MapGPUTransferBuffer(device_.raw(), xfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device_.raw(), xfer);
        SDL_ReleaseGPUTexture(device_.raw(), tex);
        if (converted) { SDL_DestroySurface(converted); }
        return nullptr;
    }
    // SDL_Surface pitch may exceed w*4 (alignment padding). Copy row by
    // row to drop any trailing bytes the GPU upload doesn't expect.
    auto* dst = static_cast<std::uint8_t*>(mapped);
    const auto* psrc = static_cast<const std::uint8_t*>(src->pixels);
    for (int y = 0; y < src->h; ++y) {
        std::memcpy(dst + static_cast<std::size_t>(y) * row_bytes,
                    psrc + static_cast<std::size_t>(y) * src->pitch, row_bytes);
    }
    SDL_UnmapGPUTransferBuffer(device_.raw(), xfer);

    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(device_.raw());
    if (!cb) {
        SDL_ReleaseGPUTransferBuffer(device_.raw(), xfer);
        SDL_ReleaseGPUTexture(device_.raw(), tex);
        if (converted) { SDL_DestroySurface(converted); }
        return nullptr;
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureTransferInfo ti{};
    ti.transfer_buffer = xfer;
    ti.offset = 0;
    ti.pixels_per_row = static_cast<std::uint32_t>(src->w);
    ti.rows_per_layer = static_cast<std::uint32_t>(src->h);
    SDL_GPUTextureRegion region{};
    region.texture = tex;
    region.w = static_cast<std::uint32_t>(src->w);
    region.h = static_cast<std::uint32_t>(src->h);
    region.d = 1;
    SDL_UploadToGPUTexture(cp, &ti, &region, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cb);

    SDL_ReleaseGPUTransferBuffer(device_.raw(), xfer);
    if (converted) { SDL_DestroySurface(converted); }
    return tex;
}

SDL_GPUTexture* render_state::create_rgba_gpu_texture(int w, int h) {
    if (!device_.ready() || w <= 0 || h <= 0) { return nullptr; }
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = static_cast<std::uint32_t>(w);
    tci.height = static_cast<std::uint32_t>(h);
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device_.raw(), &tci);
    if (!tex) { dbg(DL::Warn) << "create_rgba_gpu_texture: " << SDL_GetError(); }
    return tex;
}

bool render_state::upload_surface_subregion_to_gpu_texture(
    SDL_GPUTexture* dst, int dst_x, int dst_y, SDL_Surface* src, const SDL_Rect* src_rect) {
    if (!device_.ready() || !dst || !src || src->w <= 0 || src->h <= 0) { return false; }
    SDL_Surface* psrc = src;
    SDL_Surface* converted = nullptr;
    if (src->format != SDL_PIXELFORMAT_RGBA32) {
        converted = SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA32);
        if (!converted) {
            dbg(DL::Warn) << "upload_surface_subregion: convert: " << SDL_GetError();
            return false;
        }
        psrc = converted;
    }

    // Sub-rect of the (potentially converted) source surface to upload.
    int sx = 0;
    int sy = 0;
    int sw = psrc->w;
    int sh = psrc->h;
    if (src_rect) {
        sx = std::max(0, src_rect->x);
        sy = std::max(0, src_rect->y);
        sw = std::clamp(src_rect->w, 0, psrc->w - sx);
        sh = std::clamp(src_rect->h, 0, psrc->h - sy);
        if (sw <= 0 || sh <= 0) {
            if (converted) { SDL_DestroySurface(converted); }
            return false;
        }
    }

    const std::uint32_t row_bytes = static_cast<std::uint32_t>(sw) * 4;
    const std::uint32_t total_bytes = row_bytes * static_cast<std::uint32_t>(sh);

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = total_bytes;
    SDL_GPUTransferBuffer* xfer = SDL_CreateGPUTransferBuffer(device_.raw(), &tbi);
    if (!xfer) {
        dbg(DL::Warn) << "upload_surface_subregion: xfer: " << SDL_GetError();
        if (converted) { SDL_DestroySurface(converted); }
        return false;
    }
    void* mapped = SDL_MapGPUTransferBuffer(device_.raw(), xfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device_.raw(), xfer);
        if (converted) { SDL_DestroySurface(converted); }
        return false;
    }
    auto* out = static_cast<std::uint8_t*>(mapped);
    const auto* in = static_cast<const std::uint8_t*>(psrc->pixels);
    // Copy only the sub-rect rows, packed tightly into the transfer
    // buffer; the GPU upload region below is sized to the sub-rect
    // dimensions so the upload doesn't touch neighbouring atlas cells.
    for (int y = 0; y < sh; ++y) {
        const std::uint8_t* row_src =
            in + static_cast<std::size_t>(sy + y) * psrc->pitch + static_cast<std::size_t>(sx) * 4;
        std::memcpy(out + static_cast<std::size_t>(y) * row_bytes, row_src, row_bytes);
    }
    SDL_UnmapGPUTransferBuffer(device_.raw(), xfer);

    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(device_.raw());
    if (!cb) {
        SDL_ReleaseGPUTransferBuffer(device_.raw(), xfer);
        if (converted) { SDL_DestroySurface(converted); }
        return false;
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureTransferInfo ti{};
    ti.transfer_buffer = xfer;
    ti.offset = 0;
    // Tightly packed layout in the transfer buffer — sw pixels per row,
    // sh rows. SDL_GPU's 0-default for these means "use region.w/region.h",
    // but setting explicitly removes any ambiguity across backends.
    ti.pixels_per_row = static_cast<std::uint32_t>(sw);
    ti.rows_per_layer = static_cast<std::uint32_t>(sh);
    SDL_GPUTextureRegion region{};
    region.texture = dst;
    region.x = static_cast<std::uint32_t>(dst_x);
    region.y = static_cast<std::uint32_t>(dst_y);
    region.w = static_cast<std::uint32_t>(sw);
    region.h = static_cast<std::uint32_t>(sh);
    region.d = 1;
    SDL_UploadToGPUTexture(cp, &ti, &region, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cb);

    SDL_ReleaseGPUTransferBuffer(device_.raw(), xfer);
    if (converted) { SDL_DestroySurface(converted); }
    return true;
}

void render_state::queue_font_glyph(
    SDL_GPUTexture* glyph_tex, float dst_x, float dst_y, float dst_w, float dst_h, float r, float g,
    float b, float a, bool lit, float rotation) {
    // Full-texture sample — for callers with one texture per glyph
    // (CachedTTFFont).
    queue_font_glyph(
        glyph_tex, dst_x, dst_y, dst_w, dst_h, 0.0f, 0.0f, 1.0f, 1.0f, r, g, b, a, lit, rotation);
}

void render_state::queue_font_glyph(
    SDL_GPUTexture* glyph_tex, float dst_x, float dst_y, float dst_w, float dst_h, float src_u,
    float src_v, float src_uw, float src_vh, float r, float g, float b, float a, bool lit,
    float rotation) {
    if (!device_.ready() || !glyph_tex) { return; }
    font_glyph_draw d{};
    d.texture = glyph_tex;
    d.inst.dst_x = dst_x;
    d.inst.dst_y = dst_y;
    d.inst.dst_w = dst_w;
    d.inst.dst_h = dst_h;
    d.inst.src_u = src_u;
    d.inst.src_v = src_v;
    d.inst.src_uw = src_uw;
    d.inst.src_vh = src_vh;
    d.inst.tint_r = r;
    d.inst.tint_g = g;
    d.inst.tint_b = b;
    d.inst.tint_a = a;
    d.inst.rotation = rotation; // radians, clockwise about the quad centre
    d.lit = lit;
    if (current_slices_) {
        current_slices_->font_glyphs.push_back(d);
    } else if (transient_routing_) {
        font_glyph_transient_.push_back(d);
    } else {
        font_glyph_queue_.push_back(d);
    }
}

void render_state::append_slice(
    const std::vector<sprite_instance>& rects, const std::vector<font_glyph_draw>& glyphs) {
    ui_rect_queue_.insert(ui_rect_queue_.end(), rects.begin(), rects.end());
    font_glyph_queue_.insert(font_glyph_queue_.end(), glyphs.begin(), glyphs.end());
    // Record the cumulative end of each queue so flush_ui replays this slice as
    // a self-contained rects-then-glyphs group, ordered above earlier slices.
    ui_slice_spans_.emplace_back(
        static_cast<std::uint32_t>(ui_rect_queue_.size()),
        static_cast<std::uint32_t>(font_glyph_queue_.size()));
}

void render_state::queue_tile_sprite(SDL_GPUTexture* atlas_tex, const sprite_instance& inst) {
    if (!device_.ready() || !atlas_tex) { return; }
    if (unlit_overlay_route_) {
        // GPU minimap overlay: redirect this atlas sprite into the UNLIT
        // font-glyph path (current adaptor slice) so world lighting never
        // samples it. Reuses the existing textured-quad flush; no new pass.
        font_glyph_draw d{};
        d.texture = atlas_tex;
        d.inst = inst;
        d.lit = false;
        if (current_slices_) {
            current_slices_->font_glyphs.push_back(d);
        } else if (transient_routing_) {
            font_glyph_transient_.push_back(d);
        } else {
            font_glyph_queue_.push_back(d);
        }
        return;
    }
    tile_sprite_queue_.push_back({atlas_tex, inst});
}

void render_state::build_outline_ring(
    std::size_t start, std::size_t end, float r, float g, float b, float a, float radius_px,
    float alpha_cut) {
    if (end <= start || end > tile_sprite_queue_.size() || radius_px <= 0.0f) { return; }
    static constexpr float dirs[8][2] = {
        {-1.f, -1.f}, {0.f, -1.f}, {1.f, -1.f}, {-1.f, 0.f},
        {1.f, 0.f},   {-1.f, 1.f}, {0.f, 1.f},  {1.f, 1.f},
    };
    std::vector<tile_sprite_draw> ring;
    ring.reserve((end - start) * 8);
    for (std::size_t i = start; i < end; ++i) {
        const tile_sprite_draw& src = tile_sprite_queue_[i];
        for (const auto& d : dirs) {
            tile_sprite_draw s = src;
            s.inst.dst_x += d[0] * radius_px;
            s.inst.dst_y += d[1] * radius_px;
            s.inst.tint_r = r;
            s.inst.tint_g = g;
            s.inst.tint_b = b;
            s.inst.tint_a = a;
            s.inst.light_mul = alpha_cut; // outline branch reads this as alpha cutoff
            s.inst.pad1 = 0.0f;           // no foliage sway
            s.inst.pad2 = 1.0f;           // outline silhouette flag (sprite.frag)
            s.inst.extrude_px = 0.0f;     // outline copies must not darken or lean
            s.inst.extrude_dark = 0.0f;
            s.inst.extrude_lean = 0.0f;
            s.inst.extrude_pad = 0.0f;
            ring.push_back(s);
        }
    }
    tile_sprite_queue_.insert(
        tile_sprite_queue_.begin() + static_cast<std::ptrdiff_t>(start), ring.begin(), ring.end());
}

void render_state::flush_tile_sprites(sprite_batcher& dst, SDL_GPUSampler* sampler) {
    flush_tile_sprites(dst, sampler, 0, tile_sprite_queue_.size());
}

void render_state::flush_tile_sprites(
    sprite_batcher& dst, SDL_GPUSampler* sampler, std::size_t begin, std::size_t end) {
    if (tile_sprite_queue_.empty()) { return; }
    if (!sampler) {
        tile_sprite_queue_.clear();
        return;
    }
    end = std::min(end, tile_sprite_queue_.size());
    if (begin >= end) { return; }
    // Group consecutive same-texture draws under a single set_texture
    // call. set_texture is a no-op when the requested texture is
    // already bound, so atlas-packed runs naturally batch into one
    // segment.
    // Drain WITHOUT clearing — clear_frame_queues() handles reset at
    // the top of each redraw cycle.
    SDL_GPUTexture* bound = nullptr;
    for (const tile_sprite_draw& s : std::span(tile_sprite_queue_).subspan(begin, end - begin)) {
        if (s.texture != bound) {
            dst.set_texture(s.texture, sampler);
            bound = s.texture;
        }
        dst.draw(s.inst);
    }
}

void render_state::set_splat_frame(std::size_t cut, std::vector<splat_quad> quads) {
    splat_cut_ = cut;
    splat_quads_ = std::move(quads);
}

void render_state::flush_shadow_casters(
    SDL_GPUCommandBuffer* cb, std::uint32_t proj_w, std::uint32_t proj_h) {
    if (!cb || !shadow_mask_ || !shadow_mask_->texture() || !gpu_sampler_) { return; }

    const frame_light_inputs& in = last_frame_inputs_;

    // Stamp the shadow batcher's VERTEX light_params with this frame's tile
    // geometry + sun (the shear reads sun_dir + cot(elev)). Pass NO lighting
    // storage buffers (count=0, sdf_map_w/h=0, all buffers null) so
    // bind_lighting_resources no-ops — shadow.frag reads only the atlas. The
    // sampler is forwarded so set_lighting_resources' null-guard is satisfied.
    shadow_batcher_.set_lighting_resources(
        in.tile_pixel_size, in.z_level, 0u, in.ambient, in.camera_off_x, in.camera_off_y, 0u, 0u,
        /*emitter*/ nullptr, /*sdf*/ nullptr, gpu_sampler_,
        /*sky_vis*/ nullptr, /*gi*/ nullptr, &in.sun, &in.debug);

    // Clear to opaque black (alpha 1): shadow.frag writes alpha=1 and the
    // batcher MAX-blends alpha, so the mask stays opaque → the Phase-1 debug
    // blit reads as clean grey silhouettes on black. Always open the pass when
    // driven (even with zero tall casters) so the mask is defined each frame.
    const float clear_mask[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    shadow_batcher_.begin_pass(
        cb, shadow_mask_->texture(), shadow_mask_->width(), shadow_mask_->height(), clear_mask,
        proj_w, proj_h, shadow_mask_->format());

    // Drain the TALL subset of the (still-populated) tile queue as sheared
    // coverage. Same dst_h > 1.5*tile_px test the vertex shader uses for the
    // "lit on top" tall-sprite path. Drain WITHOUT clearing — Pass W re-drains
    // the full set right after.
    const float tall_threshold = in.tile_pixel_size * 1.5f;
    SDL_GPUTexture* bound = nullptr;
    for (const tile_sprite_draw& s : tile_sprite_queue_) {
        if (s.inst.dst_h <= tall_threshold) { continue; }
        // Hover-outline silhouette copies (pad2 > 0.5) must NOT cast shadows —
        // 8 offset casters would draw a black halo around the creature.
        if (s.inst.pad2 > 0.5f) { continue; }
        if (s.texture != bound) {
            shadow_batcher_.set_texture(s.texture, gpu_sampler_);
            bound = s.texture;
        }
        shadow_batcher_.draw(s.inst);
    }

    shadow_batcher_.end_pass();
}


auto render_state::capture_texture_to_rgba(
    SDL_GPUTexture* tex, int w, int h, std::vector<uint8_t>& pixels) -> bool {
    if (!device_.ready() || !tex || w <= 0 || h <= 0) { return false; }
    SDL_GPUDevice* d = device_.raw();
    const Uint32 row_pitch = static_cast<Uint32>(w) * 4;
    const Uint32 buf_size = row_pitch * static_cast<Uint32>(h);

    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tbci.size = buf_size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(d, &tbci);
    if (!tb) { return false; }

    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(d);
    if (!cb) {
        SDL_ReleaseGPUTransferBuffer(d, tb);
        return false;
    }

    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    dst.pixels_per_row = static_cast<Uint32>(w);
    SDL_GPUTextureRegion region{};
    region.texture = tex;
    region.w = static_cast<Uint32>(w);
    region.h = static_cast<Uint32>(h);
    region.d = 1;
    SDL_DownloadFromGPUTexture(cp, &region, &dst);
    SDL_EndGPUCopyPass(cp);

    SDL_SubmitGPUCommandBuffer(cb);
    SDL_WaitForGPUIdle(d);

    const auto* mapped = static_cast<const uint8_t*>(SDL_MapGPUTransferBuffer(d, tb, false));
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(d, tb);
        return false;
    }

    pixels.resize(buf_size);
    // The GPU texture uses the native swapchain format (RGBA or BGRA); the
    // caller is responsible for format conversion if needed. Copy the raw
    // bytes verbatim — the host-side row layout matches the transfer pitch.
    std::memcpy(pixels.data(), mapped, buf_size);

    SDL_UnmapGPUTransferBuffer(d, tb);
    SDL_ReleaseGPUTransferBuffer(d, tb);
    return true;
}


render_state& get_render_state() {
    static render_state instance;
    return instance;
}

bool init_render_state_on(SDL_Window* visible_window) {
    bool expected = false;
    if (!initialised.compare_exchange_strong(expected, true)) {
        return true; // already initialised
    }
    if (!visible_window) {
        initialised.store(false);
        dbg(DL::Warn) << "lighting: init_render_state_on(nullptr)";
        return false;
    }

    dbg(DL::Info) << "lighting: init_render_state_on called (window=" << visible_window << ")";

    try {
        get_render_state().init(visible_window);
        dbg(DL::Info) << "lighting: render_state up on visible window.";
        return true;
    } catch (const std::exception& e) {
        dbg(DL::Warn) << "lighting: render_state init failed: " << e.what();
        initialised.store(false);
        return false;
    }
}

void shutdown_render_state() noexcept {
    bool expected = true;
    if (!initialised.compare_exchange_strong(expected, false)) { return; }
    get_render_state().shutdown();
}

} // namespace lighting
