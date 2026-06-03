#include "frame_build.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "avatar.h"
#include "game.h"
#include "game_constants.h"
#include "map.h"
#include "lighting/emitter_collector.h"
#include "lighting/render_state.h"
#include "lighting/sdf_pass.h"
#include "lighting/snapshot.h"
#include "worldfactory.h"

#define dbg(x) DebugLogFL((x),DC::SDL)

namespace lighting
{

frame_lighting_result build_and_submit_lighting( render_state &rs,
        bool rebuild_pertile, bool want_hud_snapshot )
{
    frame_lighting_result result;

    if( !rs.collector() ) {
        return result;
    }

    constexpr float FRAME_MS = 25.0f;

    auto snapshot = lighting::build_emitter_snapshot( rs.emitter_events(), FRAME_MS );

    // Phase 4: compute transparency + SDF from the current map cache.
    // Gate on active_world: g exists during the main menu but get_map()
    // returns the default-constructed map (all transparent), which would
    // upload a populated SDF and cause the fragment shadow march to
    // shadow=0 every emitter beyond ~1 tile.
    std::vector<uint8_t> transparency;
    std::vector<float>   sdf;
    std::vector<uint8_t> sky_vis;
    std::vector<float>   indirect; // 1-bounce GI, 3 floats/tile RGB (x-major)
    std::vector<float>   vis;      // per-tile visibility for soft vision falloff (x-major)
    int sdf_runtime_w = 0;
    int sdf_runtime_h = 0;
    if( rebuild_pertile && g && world_generator && world_generator->active_world
        && rs.sdf().ready() ) {
        map &m = get_map(); // non-const for i_at etc.
        const int zlev = g->u.bub_pos().z();
        const level_cache &mc = m.access_cache( zlev );
        const int mapsize = m.getmapsize();
        const int W = mapsize * SEEX;
        const int H = mapsize * SEEY;
        const int total = W * H;

        // Guard transparency loop too — pre-fix this was UB if the cache
        // hadn't been built yet (size 0). sdf+transparency now share one
        // gate; either both populate or neither.
        if( static_cast<int>( mc.transparency_cache.size() ) >= total ) {
            // Pack float transparency_cache → uint8 (0=opaque, 255=transparent).
            transparency.resize( total );
            for( int i = 0; i < total; ++i ) {
                const float t = mc.transparency_cache[ i ];
                transparency[i] = static_cast<uint8_t>(
                    std::min( 255.0f, std::max( 0.0f, t * 255.0f ) ) );
            }
            // CPU BFS distance transform.
            sdf = lighting::compute_sdf_cpu( mc.transparency_cache.data(), W, H );
            sdf_runtime_w = W;
            sdf_runtime_h = H;

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

            // Per-tile visibility for the soft vision falloff (effect 1+2).
            // Raw max(seen_cache, camera_cache) — the SAME float
            // apparent_light_helper reads, but the render path otherwise
            // discards it by bucketing to discrete lit_level (the hard
            // edge). seen_cache already encodes a continuous radial decay.
            // x-major (idx = x*H+y), matching transparency_cache. Live-only
            // (>=0); memorized-tile fade is handled CPU-side at draw time
            // (ll==MEMORIZED), not via this buffer.
            if( static_cast<int>( mc.seen_cache.size() ) >= total ) {
                vis.assign( total, 0.0f );
                const bool have_cam =
                    static_cast<int>( mc.camera_cache.size() ) >= total;
                for( int i = 0; i < total; ++i ) {
                    const float s = mc.seen_cache[i];
                    const float c = have_cam ? mc.camera_cache[i] : 0.0f;
                    vis[i] = std::max( s, c );
                }
            }

            // 1-bounce indirect light (fake GI). Seed per-tile colored
            // direct radiance from the CPU lightmap — `lm` is PHYSICAL and
            // turn-stable (generate_lightmap; vision is applied later in
            // apparent_light), so reading it per-frame is shimmer-free.
            // Normalise by LIGHT_AMBIENT_LIT so a well-lit tile ≈ 1.0, to
            // match the GPU dynamic-light scale. light_color_cache supplies
            // hue only when coloured (fire/twilight); else white.
            // Then diffuse 2 passes through OPEN tiles (blocked by walls,
            // leaks through windows) → soft colored fill that spills deeper
            // than the direct cone. Shader adds gi_strength * this.
            if( static_cast<int>( mc.lm.size() ) >= total ) {
                std::vector<float> seed( total * 3, 0.0f );
                const bool colored = mc.has_colored_lights
                    && static_cast<int>( mc.light_color_cache.size() ) >= total;
                for( int i = 0; i < total; ++i ) {
                    const float lum = mc.lm[i].max() / LIGHT_AMBIENT_LIT;
                    if( lum <= 0.0f ) { continue; }
                    float cr = 1.0f, cg = 1.0f, cb = 1.0f;
                    if( colored ) {
                        const light_color_rgb &lc = mc.light_color_cache[i];
                        const float m = std::max( lc.r, std::max( lc.g, lc.b ) );
                        if( m > 0.0001f ) { cr = lc.r / m; cg = lc.g / m; cb = lc.b / m; }
                    }
                    seed[i * 3 + 0] = lum * cr;
                    seed[i * 3 + 1] = lum * cg;
                    seed[i * 3 + 2] = lum * cb;
                }
                // Wall-gated diffusion. Sources stay pinned (seed re-added
                // each pass); light relaxes outward only through tiles whose
                // transparency is above SOLID (0). Averaging keeps it bounded.
                std::vector<float> cur = seed;
                std::vector<float> nxt( total * 3, 0.0f );
                const float decay = 0.55f;
                for( int pass = 0; pass < 2; ++pass ) {
                    for( int x = 0; x < W; ++x ) {
                        for( int y = 0; y < H; ++y ) {
                            const int i = x * H + y;
                            float ar = seed[i * 3 + 0];
                            float ag = seed[i * 3 + 1];
                            float ab = seed[i * 3 + 2];
                            float wsum = 1.0f;
                            for( int dx = -1; dx <= 1; ++dx ) {
                                for( int dy = -1; dy <= 1; ++dy ) {
                                    if( dx == 0 && dy == 0 ) { continue; }
                                    const int nx = x + dx, ny = y + dy;
                                    if( nx < 0 || ny < 0 || nx >= W || ny >= H ) { continue; }
                                    const int ni = nx * H + ny;
                                    if( mc.transparency_cache[ni] <= 0.01f ) { continue; }
                                    ar += decay * cur[ni * 3 + 0];
                                    ag += decay * cur[ni * 3 + 1];
                                    ab += decay * cur[ni * 3 + 2];
                                    wsum += decay;
                                }
                            }
                            nxt[i * 3 + 0] = ar / wsum;
                            nxt[i * 3 + 1] = ag / wsum;
                            nxt[i * 3 + 2] = ab / wsum;
                        }
                    }
                    cur.swap( nxt );
                }
                indirect = std::move( cur );
            }
        }

        // Snapshot for HUD: what's the SDF / transparency at the player tile?
        const int pi = g->u.bub_pos().x() * H + g->u.bub_pos().y();
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
    }

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
                            std::move( indirect ),
                            std::move( vis ),
                            sdf_runtime_w,
                            sdf_runtime_h );

    return result;
}

} // namespace lighting
