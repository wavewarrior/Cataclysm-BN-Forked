#include "frame_build.h"

#include "avatar.h"
#include "game.h"
#include "game_constants.h"
#include "lighting/emitter_collector.h"
#include "lighting/render_state.h"
#include "lighting/sdf_pass.h"
#include "lighting/snapshot.h"
#include "lightmap.h"
#include "map.h"
#include "profile.h"
#include "worldfactory.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

namespace {
// Separable Gaussian blur of a tile-resolution scalar field (x-major,
// idx = x*H+y), in place. sigma is in tiles; < 0.05 → no-op. Used to soften the
// FOV / sky-vis masks so the tile-quantised shadowcast staircase reads as a
// smooth gradient (Stoneshard-style mask blur) instead of hard tile steps.
// Two passes (H then V), clamp at borders.
void gaussian_blur_tilefield(std::vector<float>& f, int W, int H, float sigma) {
    if (sigma < 0.05f || W <= 0 || H <= 0 || static_cast<int>(f.size()) < W * H) { return; }
    const int rad = std::min(8, static_cast<int>(std::ceil(2.5f * sigma)));
    std::vector<float> kernel(2 * rad + 1);
    float ksum = 0.0f;
    const float inv2s2 = 1.0f / (2.0f * sigma * sigma);
    for (int k = -rad; k <= rad; ++k) {
        const float w = std::exp(-static_cast<float>(k * k) * inv2s2);
        kernel[k + rad] = w;
        ksum += w;
    }
    for (float& w : kernel) { w /= ksum; }
    std::vector<float> tmp(f.size(), 0.0f);
    for (int x = 0; x < W; ++x) { // horizontal pass (along x)
        for (int y = 0; y < H; ++y) {
            float acc = 0.0f;
            for (int k = -rad; k <= rad; ++k) {
                const int sx = std::clamp(x + k, 0, W - 1);
                acc += kernel[k + rad] * f[sx * H + y];
            }
            tmp[x * H + y] = acc;
        }
    }
    for (int x = 0; x < W; ++x) { // vertical pass (along y)
        for (int y = 0; y < H; ++y) {
            float acc = 0.0f;
            for (int k = -rad; k <= rad; ++k) {
                const int sy = std::clamp(y + k, 0, H - 1);
                acc += kernel[k + rad] * tmp[x * H + sy];
            }
            f[x * H + y] = acc;
        }
    }
}
} // namespace

frame_lighting_result build_and_submit_lighting(
    render_state& rs, lighting_rebuild_flags rebuild, bool want_hud_snapshot, float skylight_bleed,
    float vision_blur, int cam_x0, int cam_y0, int cam_w, int cam_h) {
    ZoneScoped;
    frame_lighting_result result;

    if (!rs.collector()) { return result; }

    constexpr float FRAME_MS = 25.0f;

    auto snapshot = [&]() {
        ZoneScopedN("light_emitter_snapshot");
        return lighting::build_emitter_snapshot(rs.emitter_events(), FRAME_MS);
    }();

    // Phase 4: compute transparency + occ + sky_vis from the current map cache.
    // P3.3: SDF is now GPU-only (JFA) — no CPU sdf vector needed.
    // Gate on active_world: g exists during the main menu but get_map()
    // returns the default-constructed map (all transparent), which would
    // upload a populated transparency buffer and cause the fragment shadow march to
    // shadow=0 every emitter beyond ~1 tile.
    std::vector<uint8_t> transparency;
    std::vector<float> occ; // Stage 2b: unified coverage occluder (height,roof) /tile
    std::vector<uint8_t> sky_vis;
    std::vector<float> vis; // per-tile visibility for soft vision falloff (x-major)
    int sdf_runtime_w = 0;
    int sdf_runtime_h = 0;

    const bool have_world =
        g && world_generator && world_generator->active_world && rs.sdf().ready();

    if (!have_world) {
        // No active map — skip all per-tile computation. Emitters still update
        // (main menu decorative emitter) but no SDF/vis/sky_vis buffers upload.
        dbg(DL::Debug) << "[lighting] have_world=false, skipping per-tile";
    } else {
        map& m = get_map(); // non-const for i_at etc.
        const int zlev = g->u.bub_pos().z();
        const level_cache& mc = m.access_cache(zlev);
        const int mapsize = m.getmapsize();
        const int W = mapsize * SEEX;
        const int H = mapsize * SEEY;
        const int total = W * H;

        dbg(DL::Debug) << "[lighting] have_world=true, z=" << zlev << " mapsize=" << mapsize
                       << " W=" << W << " H=" << H << " total=" << total << " rebuild.structure="
                       << rebuild.structure << " rebuild.vis=" << rebuild.vis;

        // Sanity: check cache sizes before proceeding.
        const auto trans_sz = mc.transparency_cache.size();
        const auto seen_sz = mc.seen_cache.size();
        const auto cam_sz = mc.camera_cache.size();
        const auto out_sz = mc.outside_cache.size();

        if (trans_sz < static_cast<size_t>(total)) {
            dbg(DL::Warn) << "[lighting] transparency_cache undersized! "
                          << "have=" << trans_sz << " need=" << total;
        }
        if (seen_sz < static_cast<size_t>(total)) {
            dbg(DL::Warn) << "[lighting] seen_cache undersized! "
                          << "have=" << seen_sz << " need=" << total;
        }

        // Common supersample dimensions — needed by both structure and vis paths.
        constexpr int ss = lighting::SDF_SUPERSAMPLE;
        const int SW = W * ss;
        const int SH = H * ss;

        // ── Structure rebuild: transparency, occ, sky_vis (SDF → GPU JFA) ────
        if (rebuild.structure && static_cast<int>(mc.transparency_cache.size()) >= total) {
            ZoneScopedN("light_pertile_rebuild");
            const auto _perf_struct_t0 = std::chrono::steady_clock::now();

            dbg(DL::Debug) << "[lighting] structure_rebuild: trans=" << mc.transparency_cache.size()
                           << " out=" << mc.outside_cache.size() << " total=" << total;

            // Pack transparency_cache → uint8 occluder mask (0=opaque, 255=open).
            //
            // The cache is an ATTENUATION COEFFICIENT, not a 0..1 fraction:
            // LIGHT_TRANSPARENCY_SOLID is the exact 0.0 sentinel for opaque and
            // open air is only LIGHT_TRANSPARENCY_OPEN_AIR = 0.0384. The old
            // `t * 255` therefore spanned 0..9, not 0..255 — so after sdf_pass's
            // /255 the JFA seed saw 0.035 for OPEN AIR and its `trans < 0.5`
            // opaque test matched EVERY tile. Every subcell seeded, the JFA
            // resolved distance 0 everywhere, and the whole SDF collapsed to
            // zero: debug mode 6 rendered uniform red, trace_shadow's
            // self-shadow guard fired on every fragment, and point lights
            // stopped respecting walls entirely.
            //
            // Emit a BINARY mask against the same `> LIGHT_TRANSPARENCY_SOLID`
            // test the rest of the engine uses (lightmap.cpp) and that the
            // sun/sky occluder height gate below already uses — so the point-light
            // SDF and the sun shadow agree on what a wall is: anything that
            // transmits any light at all (glass, bars, chain-link) does not occlude.
            transparency.resize(total);
            for (int i = 0; i < total; ++i) {
                transparency[i] =
                    mc.transparency_cache[i] > LIGHT_TRANSPARENCY_SOLID ? uint8_t{255} : uint8_t{0};
            }

            // P3.3: SDF is now computed on GPU via JFA (gpu_sdf_pass). The CPU no
            // longer builds the distance transform — transparency feeds trans_storage_
            // which the seed shader reads directly. sdf_runtime_w/h still set so the
            // collector submit carries correct dimensions for populated_.

            // B1 region limits (still used for occ build below):
            constexpr int MARGIN = 8; // tiles of shadow-reach slack
            int rx0 = 0, ry0 = 0, rx1 = W, ry1 = H;
            if (cam_w > 0 && cam_h > 0) {
                rx0 = std::clamp(cam_x0 - MARGIN, 0, W);
                ry0 = std::clamp(cam_y0 - MARGIN, 0, H);
                rx1 = std::clamp(cam_x0 + cam_w + MARGIN, 0, W);
                ry1 = std::clamp(cam_y0 + cam_h + MARGIN, 0, H);
            }

            sdf_runtime_w = W; // tile dims; the SS factor is implicit (shader SDF_SS)
            sdf_runtime_h = H;

            // Stage 2b: unified coverage occluder field (tile-res, 2 floats/tile:
            // [0] = occluder height from map::coverage(p)/100 — wall ~1, half-wall
            // ~0.5, fence/furniture low; [1] = roof bit from floor_cache(z+1)).
            // Marched by sky_sun.comp for sun/moon/sky occlusion — ONE occlusion
            // source covering walls/half-walls/furniture/roofs. Region-limited to
            // the same cam rect as the SDF (off-region = 0 = no occluder; off-screen
            // tiles are never marched within reach). Full-size buffer, x-major
            // occ[(x*H+y)*2 + c] (no SS — tile-res, like sky_vis).
            occ.assign(static_cast<size_t>(total) * 2, 0.0f);
            {
                const level_cache* above =
                    (zlev + 1 <= OVERMAP_HEIGHT) ? &m.access_cache(zlev + 1) : nullptr;
                const bool have_above =
                    above && static_cast<int>(above->floor_cache.size()) >= total;
                for (int x = rx0; x < rx1; ++x) {
                    for (int y = ry0; y < ry1; ++y) {
                        const int idx = x * H + y;
                        const tripoint_bub_ms tp(point_bub_ms(x, y), zlev);
                        // map::coverage() ends in ter(tp)->coverage, an obj() deref.
                        // refresh_display runs during world load BEFORE terrain JSON
                        // is finalized (factory empty), where that deref debugmsgs
                        // "invalid terrain id 0". Guard on the ter id being valid;
                        // until terrain loads, occ stays 0 (no occluder), correct for
                        // the loading screen (no real map to shadow yet).
                        float h =
                            m.ter(tp).is_valid()
                                ? std::clamp(static_cast<float>(m.coverage(tp)) / 100.0f, 0.0f, 1.0f)
                                : 0.0f;
                        // map::coverage() is the ranged-COVER gameplay stat, not a
                        // light-transmission value: a window has coverage 60 (stops
                        // bullets, blocks a sightline through the frame) yet is
                        // TRANSPARENT to light. Because coverage 60 lands exactly on
                        // sky_sun.comp's SKY_WALL_H = 0.60 blocking threshold, every
                        // window read as a solid wall and no daylight reached any
                        // interior — sprite.frag's own comment ("a roofed tile lit
                        // only through window directions gets partial sky FROM the
                        // opening") describes behaviour this silently prevented.
                        //
                        // Coverage supplies the occluder's HEIGHT (full wall ~1,
                        // half-wall ~0.5); the transparency cache — the same field
                        // the game's own LOS/vision trusts, and which already
                        // discounts glass, bars and chain-link — decides whether it
                        // blocks LIGHT at all. Anything that transmits (> SOLID)
                        // stops casting a sun/sky shadow.
                        if (static_cast<int>(mc.transparency_cache.size()) > idx &&
                            mc.transparency_cache[idx] > LIGHT_TRANSPARENCY_SOLID) {
                            h = 0.0f;
                        }
                        // P6b: parked vehicles are solid occluders for shadowing.
                        if (const auto vpart = m.veh_at(tp); vpart && vpart->obstacle_at_part()) {
                            h = std::max(h, 1.0f);
                        }
                        const float roof = (have_above && above->floor_cache[idx]) ? 1.0f : 0.0f;
                        occ[static_cast<size_t>(idx) * 2 + 0] = h;
                        occ[static_cast<size_t>(idx) * 2 + 1] = roof;
                    }
                }
            }

            // Sky visibility from outside_cache (same x-major layout as
            // transparency_cache, idx = x*H+y). 255 = open sky overhead,
            // 0 = roofed/indoor. Falls back to all-open if the cache
            // isn't built yet.
            sky_vis.assign(total, 255u);
            if (static_cast<int>(mc.outside_cache.size()) >= total) {
                for (int i = 0; i < total; ++i) { sky_vis[i] = mc.outside_cache[i] ? 255u : 0u; }
            }

            // Stage 2a (GI_COMPUTE_AND_PERF_PLAN): the CPU indoor-bleed
            // flood-fill (K=8 wall-aware diffusion over the full bubble) and the
            // sky_vis edge Gaussian that used to live here are GONE. The new
            // sky_sun.comp directional-portal march owns indoor daylight
            // propagation on the GPU: it reaches open sky through window/door
            // gaps per-direction, so sky_vis stays the RAW binary open/roofed
            // field (above) and the heavy CPU flood-fill leaves the main thread
            // (the Part-B CPU win). skylight_bleed is no longer consumed here
            // (knob retained to avoid a cbuffer-layout churn this commit).
            (void)skylight_bleed;

            // Snapshot for HUD: transparency at the player tile (SDF now lives
            // on GPU via JFA — no longer available CPU-side).
            const int pi = g->u.bub_pos().x() * H + g->u.bub_pos().y();
            if (pi >= 0 && pi < static_cast<int>(mc.transparency_cache.size())) {
                result.trans_at_player = mc.transparency_cache[pi];
            }
            result.sdf_W = W;
            result.sdf_size = 0; // P3.3: SDF is GPU-only (JFA), no CPU vector
            result.built_pertile = true;
            const double _perf_struct_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - _perf_struct_t0)
                    .count();
            DebugLogFL(DL::Info, DC::Main)
                << "[lighting][perf] structure_rebuild ms=" << _perf_struct_ms << " grid="
                << (static_cast<int>(mc.transparency_cache.size())) << "tiles x" << (ss * ss);
        } // if rebuild.structure

        // ── Vis rebuild: FOV visibility mask ───────────────────────────────
        // Independent of structure — runs when player position changes even if
        // terrain hasn't. The seen_cache shadowcast origin follows the player,
        // so walking in static terrain only requires a vis rebuild.
        if (rebuild.vis && static_cast<int>(mc.seen_cache.size()) >= total) {
            ZoneScopedN("light_vis_build");
            const auto _perf_vis_t0 = std::chrono::steady_clock::now();

            dbg(DL::Debug) << "[lighting] vis_rebuild: seen=" << mc.seen_cache.size()
                           << " cam=" << mc.camera_cache.size() << " total=" << total;

            // Per-tile visibility for the soft vision falloff (effect 1+2).
            // Raw max(seen_cache, camera_cache) — the SAME float
            // apparent_light_helper reads, but the render path otherwise
            // discards it by bucketing to discrete lit_level (the hard
            // edge). seen_cache already encodes a continuous radial decay.
            // x-major (idx = x*H+y), matching transparency_cache. Live-only
            // (>=0); memorized-tile fade is handled CPU-side at draw time
            // (ll==MEMORIZED), not via this buffer.
            // Built at the SAME SDF_SUPERSAMPLE grid as the SDF so the shader's
            // vision-edge falloff is sampled as finely as the lighting shadows
            // (tile-res bilinear smeared the rim ~1 tile; the SS grid sharpens
            // the inter-tile interpolation to ~1/SS tile). Per-tile value is
            // replicated into its SS×SS subcells (no sub-tile LOS data exists).

            // Per-tile visibility FIRST, so the blur radius is in tile units.
            std::vector<float> vtile(total, 0.0f);
            const bool have_cam = static_cast<int>(mc.camera_cache.size()) >= total;
            for (int x = 0; x < W; ++x) {
                for (int y = 0; y < H; ++y) {
                    const float s = mc.seen_cache[x * H + y];
                    const float c = have_cam ? mc.camera_cache[x * H + y] : 0.0f;
                    vtile[x * H + y] = std::max(s, c);
                }
            }
            // Blur the FOV mask: shadowcasting through a narrow aperture
            // (window) expands the visible cone in tile-sized jumps, so the
            // beam is a hard staircase in the source. A tile-radius Gaussian
            // smears the steps into a smooth diagonal (Stoneshard mask blur).
            // Render-only — does not change gameplay LOS. vision_blur=0 = no-op.
            gaussian_blur_tilefield(vtile, W, H, vision_blur);
            // Replicate each (now-smoothed) tile value into its SS×SS subcells
            // for the shader's SS-grid vis_bilinear (sub-tile interpolation on
            // top of the blur).
            vis.assign(static_cast<size_t>(SW) * SH, 0.0f);
            for (int x = 0; x < W; ++x) {
                for (int y = 0; y < H; ++y) {
                    const float v = vtile[x * H + y];
                    const int bx = x * ss;
                    const int by = y * ss;
                    for (int sx = 0; sx < ss; ++sx) {
                        for (int sy = 0; sy < ss; ++sy) {
                            vis[static_cast<size_t>(bx + sx) * SH + (by + sy)] = v;
                        }
                    }
                }
            }
            const double _perf_vis_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - _perf_vis_t0)
                    .count();
            DebugLogFL(DL::Info, DC::Main) << "[lighting][perf] vis_rebuild ms=" << _perf_vis_ms;
        } // if rebuild.vis
    } // if have_world

    if (want_hud_snapshot) {
        // Mirror snapshot to HUD on both in-game AND main menu so the
        // decorative amber emitter (snapshot.cpp:205 path) shows up in
        // the emit[0] HUD line.
        result.snapshot_copy = snapshot;
    }
    rs.collector()->submit(
        std::move(snapshot), std::move(transparency), {}, // P3.3: SDF is GPU-only (JFA), no CPU
                                                          // upload needed
        std::move(sky_vis), std::move(vis), sdf_runtime_w, sdf_runtime_h, std::move(occ));

    dbg(DL::Debug) << "[lighting] frame_build COMPLETE";

    return result;
}

} // namespace lighting
