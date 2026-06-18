#include "frame_build.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "avatar.h"
#include "game.h"
#include "game_constants.h"
#include "map.h"
#include "profile.h"
#include "lighting/emitter_collector.h"
#include "lighting/render_state.h"
#include "lighting/sdf_pass.h"
#include "lighting/snapshot.h"
#include "worldfactory.h"

#define dbg(x) DebugLogFL((x),DC::SDL)

namespace lighting
{

namespace
{
// Separable Gaussian blur of a tile-resolution scalar field (x-major,
// idx = x*H+y), in place. sigma is in tiles; < 0.05 → no-op. Used to soften the
// FOV / sky-vis masks so the tile-quantised shadowcast staircase reads as a
// smooth gradient (Stoneshard-style mask blur) instead of hard tile steps.
// Two passes (H then V), clamp at borders.
void gaussian_blur_tilefield( std::vector<float> &f, int W, int H, float sigma )
{
    if( sigma < 0.05f || W <= 0 || H <= 0
        || static_cast<int>( f.size() ) < W * H ) {
        return;
    }
    const int rad = std::min( 8, static_cast<int>( std::ceil( 2.5f * sigma ) ) );
    std::vector<float> kernel( 2 * rad + 1 );
    float ksum = 0.0f;
    const float inv2s2 = 1.0f / ( 2.0f * sigma * sigma );
    for( int k = -rad; k <= rad; ++k ) {
        const float w = std::exp( -static_cast<float>( k * k ) * inv2s2 );
        kernel[k + rad] = w;
        ksum += w;
    }
    for( float &w : kernel ) {
        w /= ksum;
    }
    std::vector<float> tmp( f.size(), 0.0f );
    for( int x = 0; x < W; ++x ) {            // horizontal pass (along x)
        for( int y = 0; y < H; ++y ) {
            float acc = 0.0f;
            for( int k = -rad; k <= rad; ++k ) {
                const int sx = std::clamp( x + k, 0, W - 1 );
                acc += kernel[k + rad] * f[ sx * H + y ];
            }
            tmp[ x * H + y ] = acc;
        }
    }
    for( int x = 0; x < W; ++x ) {            // vertical pass (along y)
        for( int y = 0; y < H; ++y ) {
            float acc = 0.0f;
            for( int k = -rad; k <= rad; ++k ) {
                const int sy = std::clamp( y + k, 0, H - 1 );
                acc += kernel[k + rad] * tmp[ x * H + sy ];
            }
            f[ x * H + y ] = acc;
        }
    }
}
} // namespace

frame_lighting_result build_and_submit_lighting( render_state &rs,
        lighting_rebuild_flags rebuild, bool want_hud_snapshot, float skylight_bleed,
        float vision_blur, int cam_x0, int cam_y0, int cam_w, int cam_h )
{
    ZoneScoped;
    frame_lighting_result result;

    if( !rs.collector() ) {
        return result;
    }

    constexpr float FRAME_MS = 25.0f;

    auto snapshot = [&]() {
        ZoneScopedN( "light_emitter_snapshot" );
        return lighting::build_emitter_snapshot( rs.emitter_events(), FRAME_MS );
    }();

    // Phase 4: compute transparency + SDF from the current map cache.
    // Gate on active_world: g exists during the main menu but get_map()
    // returns the default-constructed map (all transparent), which would
    // upload a populated SDF and cause the fragment shadow march to
    // shadow=0 every emitter beyond ~1 tile.
    std::vector<uint8_t> transparency;
    std::vector<float>   sdf;
    std::vector<float>   sun_sdf;   // Phase 2.3: wall-only sun SDF (trees excluded)
    std::vector<uint8_t> sky_vis;
    std::vector<float>   vis;      // per-tile visibility for soft vision falloff (x-major)
    int sdf_runtime_w = 0;
    int sdf_runtime_h = 0;

    const bool have_world = g && world_generator && world_generator->active_world
                            && rs.sdf().ready();

    if( !have_world ) {
        // No active map — skip all per-tile computation. Emitters still update
        // (main menu decorative emitter) but no SDF/vis/sky_vis buffers upload.
        dbg( DL::Debug ) << "[lighting] have_world=false, skipping per-tile";
    } else {
        map &m = get_map(); // non-const for i_at etc.
        const int zlev = g->u.bub_pos().z();
        const level_cache &mc = m.access_cache( zlev );
        const int mapsize = m.getmapsize();
        const int W = mapsize * SEEX;
        const int H = mapsize * SEEY;
        const int total = W * H;

        dbg( DL::Debug ) << "[lighting] have_world=true, z=" << zlev
                          << " mapsize=" << mapsize << " W=" << W << " H=" << H
                          << " total=" << total
                          << " rebuild.structure=" << rebuild.structure
                          << " rebuild.vis=" << rebuild.vis;

        // Sanity: check cache sizes before proceeding.
        const auto trans_sz = mc.transparency_cache.size();
        const auto seen_sz  = mc.seen_cache.size();
        const auto cam_sz   = mc.camera_cache.size();
        const auto out_sz   = mc.outside_cache.size();

        if( trans_sz < static_cast<size_t>( total ) ) {
            dbg( DL::Warn ) << "[lighting] transparency_cache undersized! "
                               << "have=" << trans_sz << " need=" << total;
        }
        if( seen_sz < static_cast<size_t>( total ) ) {
            dbg( DL::Warn ) << "[lighting] seen_cache undersized! "
                               << "have=" << seen_sz << " need=" << total;
        }

        // Common supersample dimensions — needed by both structure and vis paths.
        constexpr int ss = lighting::SDF_SUPERSAMPLE;
        const int SW = W * ss;
        const int SH = H * ss;

        // ── Structure rebuild: SDF, sun_sdf, sky_vis ───────────────────────
        if( rebuild.structure
            && static_cast<int>( mc.transparency_cache.size() ) >= total ) {
            ZoneScopedN( "light_pertile_rebuild" );
            const auto _perf_struct_t0 = std::chrono::steady_clock::now();

            dbg( DL::Debug ) << "[lighting] structure_rebuild: trans=" << mc.transparency_cache.size()
                              << " out=" << mc.outside_cache.size()
                              << " total=" << total;

            // Pack float transparency_cache → uint8 (0=opaque, 255=transparent).
            transparency.resize( total );
            for( int i = 0; i < total; ++i ) {
                const float t = mc.transparency_cache[ i ];
                transparency[i] = static_cast<uint8_t>(
                    std::min( 255.0f, std::max( 0.0f, t * 255.0f ) ) );
            }

            // CPU Euclidean distance transform on an SDF_SUPERSAMPLE× finer grid:
            // replicate each tile's transparency into its SS×SS subcells (occluder
            // edges stay tile-aligned — correct for tile-walls), run the DT, then
            // rescale subcell distances to TILE units (÷SS) so the shader's cone
            // trace is unchanged. Sub-tile-fine penumbra → tight + smooth.
            //
            // B1 (perf): the DT over the full bubble × SS² (~520k cells) was the
            // ~10ms structure_rebuild spike, but only the on-screen tiles (+
            // shadow-reach margin) are ever sampled. Limit the SS replication +
            // DT to the camera tile-rect ± MARGIN; scatter the (tile-unit) result
            // into a full-size buffer whose off-region cells are a large
            // "no-occluder" sentinel. On-screen fragments never bilinear-tap or
            // shadow-march into the sentinel (MARGIN covers reach), so absolute
            // indexing + the full-size upload + the shader stay unchanged.
            // cam_w/h<=0 → whole-bubble region == pre-B1 behaviour.
            constexpr int   MARGIN       = 8;        // tiles of shadow-reach slack
            constexpr float SDF_SENTINEL = 1.0e6f;   // tile units; "no occluder near"
            int rx0 = 0, ry0 = 0, rx1 = W, ry1 = H;
            if( cam_w > 0 && cam_h > 0 ) {
                rx0 = std::clamp( cam_x0 - MARGIN, 0, W );
                ry0 = std::clamp( cam_y0 - MARGIN, 0, H );
                rx1 = std::clamp( cam_x0 + cam_w + MARGIN, 0, W );
                ry1 = std::clamp( cam_y0 + cam_h + MARGIN, 0, H );
            }
            const int   rw     = std::max( 0, rx1 - rx0 );
            const int   rh     = std::max( 0, ry1 - ry0 );
            const float inv_ss = 1.0f / static_cast<float>( ss );

            // SS transparency staging for the sub-rect, reused across the sdf +
            // sun_sdf builds (render thread only; sequential, fully overwritten).
            static std::vector<float> trans_ss;
            const auto region_sdf = [&]( std::vector<float> &out, auto trans_at ) {
                out.assign( static_cast<size_t>( SW ) * SH, SDF_SENTINEL );
                if( rw <= 0 || rh <= 0 ) {
                    return;
                }
                const int SRW = rw * ss;
                const int SRH = rh * ss;
                trans_ss.assign( static_cast<size_t>( SRW ) * SRH, 0.0f );
                for( int x = 0; x < rw; ++x ) {
                    for( int y = 0; y < rh; ++y ) {
                        const float t  = trans_at( rx0 + x, ry0 + y );
                        const int   bx = x * ss;
                        const int   by = y * ss;
                        for( int sx = 0; sx < ss; ++sx ) {
                            for( int sy = 0; sy < ss; ++sy ) {
                                trans_ss[ static_cast<size_t>( bx + sx ) * SRH + ( by + sy ) ] = t;
                            }
                        }
                    }
                }
                const std::vector<float> sub =
                    lighting::compute_sdf_cpu( trans_ss.data(), SRW, SRH );
                // Scatter sub-rect subcell distances into the full SS grid at the
                // region's subcell offset, rescaled to tile units.
                for( int x = 0; x < SRW; ++x ) {
                    const int gx = rx0 * ss + x;
                    for( int y = 0; y < SRH; ++y ) {
                        const int gy = ry0 * ss + y;
                        out[ static_cast<size_t>( gx ) * SH + gy ] =
                            sub[ static_cast<size_t>( x ) * SRH + y ] * inv_ss;
                    }
                }
            };

            // sdf: real transparency (trees stay opaque → they occlude emitters
            // + form AO cavities).
            region_sdf( sdf, [&]( int x, int y ) -> float {
                return mc.transparency_cache[ x * H + y ];
            } );
            sdf_runtime_w = W;   // tile dims; the SS factor is implicit (shader SDF_SS)
            sdf_runtime_h = H;

            // Phase 2.3: wall-only sun SDF — TREE tiles forced fully transparent
            // so the sun march ignores them (their silhouette comes from the
            // screen-space shadow mask, not a blocky SDF column). Same region DT.
            region_sdf( sun_sdf, [&]( int x, int y ) -> float {
                float t = mc.transparency_cache[ x * H + y ];
                if( m.has_flag( TFLAG_TREE,
                                tripoint_bub_ms( point_bub_ms( x, y ), zlev ) ) ) {
                    t = 1.0f;   // open air to the sun (max transparent)
                }
                return t;
            } );

            // Sky visibility from outside_cache (same x-major layout as
            // transparency_cache, idx = x*H+y). 255 = open sky overhead,
            // 0 = roofed/indoor. Falls back to all-open if the cache
            // isn't built yet.
            sky_vis.assign( total, 255u );
            if( static_cast<int>( mc.outside_cache.size() ) >= total ) {
                for( int i = 0; i < total; ++i ) {
                    sky_vis[i] = mc.outside_cache[i] ? 255u : 0u;
                }
            }

            // Indoor daylight bleed (Step 5 / indoor fix). The binary sky_vis
            // above gives roofed tiles ZERO sun + sky, so daytime interiors read
            // as cave-dark even when the sim says "bright". Propagate open-sky
            // luminance into roofed tiles through TRANSPARENT cells (windows /
            // doorways), blocked by opaque walls — a cheap wall-aware flood-fill
            // on the tile grid. The result overwrites the interior sky_vis bytes
            // with a soft gradient (the upload converts /255 → continuous float),
            // so the shader's sky-ambient term fills the room near openings and
            // decays inward. Open-sky tiles stay 255. Pure sky-ambient — the sun
            // DIRECT term is decoupled (shader gate sky_vis>0.99) so this never
            // fakes a sunbeam; artificial light is GPU-side, so no double-count.
            // strength==0 → exact binary behaviour (off / bisect). Runs under the
            // rebuild.structure gate, alongside the SDF DT.
            // Gated on outside_cache_dirty: skip flood-fill when outside data hasn't changed.
            if( skylight_bleed > 0.001f && mc.outside_cache_dirty.any()
                && static_cast<int>( transparency.size() ) >= total ) {
                constexpr int   K     = 8;      // bleed radius in tiles
                constexpr float decay = 0.80f;  // per-step falloff
                std::vector<float> bleed( total );
                for( int i = 0; i < total; ++i ) {
                    bleed[i] = sky_vis[i] ? 1.0f : 0.0f;
                }
                std::vector<float> next = bleed;
                for( int it = 0; it < K; ++it ) {
                    for( int x = 0; x < W; ++x ) {
                        for( int y = 0; y < H; ++y ) {
                            const int idx = x * H + y;
                            if( sky_vis[idx] ) {       // open sky stays full
                                next[idx] = 1.0f;
                                continue;
                            }
                            float m = bleed[idx];
                            // Receive from a 4-neighbour ONLY if that neighbour is
                            // transparent (light can exit it toward us). Opaque
                            // walls (transparency==0) neither receive-through nor
                            // relay, so sealed rooms stay dark and walls are only
                            // lit on their open-facing side.
                            const auto recv = [&]( int nx, int ny ) {
                                if( nx < 0 || nx >= W || ny < 0 || ny >= H ) {
                                    return;
                                }
                                const int ni = nx * H + ny;
                                if( transparency[ni] > 0u ) {
                                    m = std::max( m, bleed[ni] * decay );
                                }
                            };
                            recv( x - 1, y );
                            recv( x + 1, y );
                            recv( x, y - 1 );
                            recv( x, y + 1 );
                            next[idx] = m;
                        }
                    }
                    bleed.swap( next );
                }
                const float s = std::min( skylight_bleed, 1.0f );
                for( int i = 0; i < total; ++i ) {
                    if( !sky_vis[i] ) {     // interior only; open sky untouched
                        const float v = std::clamp( bleed[i] * s, 0.0f, 1.0f );
                        sky_vis[i] = static_cast<uint8_t>( v * 255.0f );
                    }
                }
            }

            // Soften the open↔roof boundary so the sun/sky edge isn't a 1-tile
            // bilinear step. Plain separable Gaussian over the tile field (stacks
            // on top of the wall-aware bleed above; the bleed fills interiors, this
            // rounds the edge). vision_blur=0 → exact prior behaviour.
            if( vision_blur > 0.05f
                && static_cast<int>( sky_vis.size() ) >= total ) {
                std::vector<float> sf( total );
                for( int i = 0; i < total; ++i ) {
                    sf[i] = static_cast<float>( sky_vis[i] );
                }
                gaussian_blur_tilefield( sf, W, H, vision_blur );
                for( int i = 0; i < total; ++i ) {
                    sky_vis[i] = static_cast<uint8_t>(
                                     std::clamp( sf[i], 0.0f, 255.0f ) );
                }
            }

            // Snapshot for HUD: what's the SDF / transparency at the player tile?
            // sdf is SS-grid indexed now → sample the player tile's centre subcell.
            const int ss_hud = lighting::SDF_SUPERSAMPLE;
            const int pi = ( g->u.bub_pos().x() * ss_hud + ss_hud / 2 ) * ( H * ss_hud )
                           + ( g->u.bub_pos().y() * ss_hud + ss_hud / 2 );
            if( pi >= 0 && pi < static_cast<int>( sdf.size() ) ) {
                result.sdf_at_player = sdf[pi];
                dbg( DL::Debug ) << "sdf[player]: " << sdf[pi];
            }
            if( pi >= 0 && pi < static_cast<int>( mc.transparency_cache.size() ) ) {
                result.trans_at_player = mc.transparency_cache[pi];
            }
            result.sdf_W    = W;
            result.sdf_size = sdf.size();
            result.built_pertile = true;
            const double _perf_struct_ms = std::chrono::duration<double, std::milli>(
                                               std::chrono::steady_clock::now() - _perf_struct_t0 ).count();
            DebugLogFL( DL::Info, DC::Main ) << "[lighting][perf] structure_rebuild ms=" << _perf_struct_ms
                                             << " grid=" << ( static_cast<int>( mc.transparency_cache.size() ) ) << "tiles x" << ( ss * ss );
        } // if rebuild.structure

        // ── Vis rebuild: FOV visibility mask ───────────────────────────────
        // Independent of structure — runs when player position changes even if
        // terrain hasn't. The seen_cache shadowcast origin follows the player,
        // so walking in static terrain only requires a vis rebuild.
        if( rebuild.vis
            && static_cast<int>( mc.seen_cache.size() ) >= total ) {
            ZoneScopedN( "light_vis_build" );
            const auto _perf_vis_t0 = std::chrono::steady_clock::now();

            dbg( DL::Debug ) << "[lighting] vis_rebuild: seen=" << mc.seen_cache.size()
                              << " cam=" << mc.camera_cache.size()
                              << " total=" << total;

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
            std::vector<float> vtile( total, 0.0f );
            const bool have_cam =
                static_cast<int>( mc.camera_cache.size() ) >= total;
            for( int x = 0; x < W; ++x ) {
                for( int y = 0; y < H; ++y ) {
                    const float s = mc.seen_cache[ x * H + y ];
                    const float c = have_cam ? mc.camera_cache[ x * H + y ] : 0.0f;
                    vtile[ x * H + y ] = std::max( s, c );
                }
            }
            // Blur the FOV mask: shadowcasting through a narrow aperture
            // (window) expands the visible cone in tile-sized jumps, so the
            // beam is a hard staircase in the source. A tile-radius Gaussian
            // smears the steps into a smooth diagonal (Stoneshard mask blur).
            // Render-only — does not change gameplay LOS. vision_blur=0 = no-op.
            gaussian_blur_tilefield( vtile, W, H, vision_blur );
            // Replicate each (now-smoothed) tile value into its SS×SS subcells
            // for the shader's SS-grid vis_bilinear (sub-tile interpolation on
            // top of the blur).
            vis.assign( static_cast<size_t>( SW ) * SH, 0.0f );
            for( int x = 0; x < W; ++x ) {
                for( int y = 0; y < H; ++y ) {
                    const float v  = vtile[ x * H + y ];
                    const int   bx = x * ss;
                    const int   by = y * ss;
                    for( int sx = 0; sx < ss; ++sx ) {
                        for( int sy = 0; sy < ss; ++sy ) {
                            vis[ static_cast<size_t>( bx + sx ) * SH + ( by + sy ) ] = v;
                        }
                    }
                }
            }
            const double _perf_vis_ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - _perf_vis_t0 ).count();
            DebugLogFL( DL::Info, DC::Main ) << "[lighting][perf] vis_rebuild ms=" << _perf_vis_ms;
        } // if rebuild.vis
    } // if have_world

    if( want_hud_snapshot ) {
        // Mirror snapshot to HUD on both in-game AND main menu so the
        // decorative amber emitter (snapshot.cpp:205 path) shows up in
        // the emit[0] HUD line.
        result.snapshot_copy = snapshot;
    }
    rs.collector()->submit( std::move( snapshot ),
                            std::move( transparency ),
                            std::move( sdf ),
                            std::move( sky_vis ),
                            std::move( vis ),
                            sdf_runtime_w,
                            sdf_runtime_h,
                            std::move( sun_sdf ) );

    dbg( DL::Debug ) << "[lighting] frame_build COMPLETE";

    return result;
}

} // namespace lighting
