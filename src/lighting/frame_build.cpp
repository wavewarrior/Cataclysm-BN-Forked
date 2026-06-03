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
            // CPU Euclidean distance transform on an SDF_SUPERSAMPLE× finer grid:
            // replicate each tile's transparency into its SS×SS subcells (occluder
            // edges stay tile-aligned — correct for tile-walls), run the same DT,
            // then rescale subcell distances to TILE units (÷SS) so the shader's
            // cone trace is unchanged. Sub-tile-fine penumbra → tight + smooth.
            const int ss = lighting::SDF_SUPERSAMPLE;
            const int SW = W * ss;
            const int SH = H * ss;
            std::vector<float> trans_ss( static_cast<size_t>( SW ) * SH );
            for( int x = 0; x < W; ++x ) {
                for( int y = 0; y < H; ++y ) {
                    const float t  = mc.transparency_cache[ x * H + y ];
                    const int   bx = x * ss;
                    const int   by = y * ss;
                    for( int sx = 0; sx < ss; ++sx ) {
                        for( int sy = 0; sy < ss; ++sy ) {
                            trans_ss[ static_cast<size_t>( bx + sx ) * SH + ( by + sy ) ] = t;
                        }
                    }
                }
            }
            sdf = lighting::compute_sdf_cpu( trans_ss.data(), SW, SH );
            const float inv_ss = 1.0f / static_cast<float>( ss );
            for( float &d : sdf ) {
                d *= inv_ss;   // subcell distance → tile units
            }
            sdf_runtime_w = W;   // tile dims; the SS factor is implicit (shader SDF_SS)
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
            // Built at the SAME SDF_SUPERSAMPLE grid as the SDF so the shader's
            // vision-edge falloff is sampled as finely as the lighting shadows
            // (tile-res bilinear smeared the rim ~1 tile; the SS grid sharpens
            // the inter-tile interpolation to ~1/SS tile). Per-tile value is
            // replicated into its SS×SS subcells (no sub-tile LOS data exists).
            if( static_cast<int>( mc.seen_cache.size() ) >= total ) {
                vis.assign( static_cast<size_t>( SW ) * SH, 0.0f );
                const bool have_cam =
                    static_cast<int>( mc.camera_cache.size() ) >= total;
                for( int x = 0; x < W; ++x ) {
                    for( int y = 0; y < H; ++y ) {
                        const float s = mc.seen_cache[ x * H + y ];
                        const float c = have_cam ? mc.camera_cache[ x * H + y ] : 0.0f;
                        const float v = std::max( s, c );
                        const int bx = x * ss;
                        const int by = y * ss;
                        for( int sx = 0; sx < ss; ++sx ) {
                            for( int sy = 0; sy < ss; ++sy ) {
                                vis[ static_cast<size_t>( bx + sx ) * SH + ( by + sy ) ] = v;
                            }
                        }
                    }
                }
            }

            // (1-bounce indirect light is now computed on the GPU by the
            // radiance_cascade_pass — Step-3 Phase 2/3. The old CPU seed +
            // wall-gated diffusion that filled `indirect` was removed in
            // Phase 4; gi_strength still scales the GPU result in the sprite.)
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
                            std::move( vis ),
                            sdf_runtime_w,
                            sdf_runtime_h );

    return result;
}

} // namespace lighting
