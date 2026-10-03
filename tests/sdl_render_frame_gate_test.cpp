#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <optional>
#include "avatar.h"

#include "calendar.h"
#include "catch/catch_amalgamated.hpp"
#include "coordinates.h"
#include "game.h"
#include "game_constants.h"
#include "level_cache_freshness.h"
#include "map.h"
#include "map_helpers.h"
#include "point.h"
#include "state_helpers.h"
#include "type_id.h"

// T8 equivalence pin (ADR-0002): the render-frame gate in sdl_render_frame.cpp used
// to fold `transparency_generation ^ ( outside_generation * prime )` from the live
// cache and compare it against file-static stamps. The gate now consumes the plan's
// `occluder` stamp and `pose` tuple. These pins replay BOTH decision
// functions - the retired fold (kept verbatim below as `old_gate`) and the new
// plan-fed gate - over the same scenario matrix and require identical verdicts, so
// the cutover is proven equal on the frame-gate contract, not merely plausible.
// The frame gate itself is TU-static in the renderer and unreachable headlessly;
// this is the deepest reachable seam where the equivalence is checkable.

namespace {

using K = level_cache_freshness;

constexpr std::uint64_t OUTSIDE_PRIME = 1099511628211ull;
constexpr int DRIFT_TILES = 4;
struct lighting_like_pair {
    bool structure = false;
    bool vis = false;
};

// The retired gate, transcribed from the pre-T8 sdl_render_frame.cpp.
struct old_gate {
    std::uint64_t last_gen = 0;
    int last_z = INT_MIN;
    point last_origin{ INT_MIN, INT_MIN };
    std::optional<tripoint_bub_ms> last_player;
    int last_struct_px = INT_MIN;
    int last_struct_py = INT_MIN;

    auto decide( std::uint64_t gen, int z, point origin, tripoint_bub_ms player )
    -> lighting_like_pair {
        lighting_like_pair r;
        const bool cam_drifted = last_struct_px == INT_MIN
            || std::abs( player.x() - last_struct_px ) >= DRIFT_TILES
            || std::abs( player.y() - last_struct_py ) >= DRIFT_TILES;
        r.structure = gen != last_gen || z != last_z || origin != last_origin || cam_drifted;
        r.vis = !last_player || *last_player != player;
        if( r.structure ) {
            last_gen = gen;
            last_z = z;
            last_origin = origin;
            last_struct_px = player.x();
            last_struct_py = player.y();
        }
        if( r.vis ) {
            last_player = player;
        }
        return r;
    }
};

// The shipped gate, transcribed from the post-T8 sdl_render_frame.cpp.
struct new_gate {
    std::optional<K::pose_stamps> last_struct_pose;
    std::optional<K::pose_stamps> last_vis_pose;
    std::uint64_t last_occluder = 0;
    int last_struct_px = INT_MIN;
    int last_struct_py = INT_MIN;

    auto decide( const K::rebuild_plan &plan ) -> lighting_like_pair {
        lighting_like_pair r;
        const int z = plan.pose.viewer.z();
        const std::uint64_t gen =
            plan.occluder[static_cast<size_t>( z + OVERMAP_DEPTH )];
        const int px = plan.pose.viewer.x();
        const int py = plan.pose.viewer.y();
        const bool cam_drifted = last_struct_px == INT_MIN
            || std::abs( px - last_struct_px ) >= DRIFT_TILES
            || std::abs( py - last_struct_py ) >= DRIFT_TILES;
        const bool pose_shifted = !last_struct_pose
            || last_struct_pose->bubble_origin != plan.pose.bubble_origin
            || last_struct_pose->viewer.z() != plan.pose.viewer.z();
        r.structure = gen != last_occluder || pose_shifted || cam_drifted;
        r.vis = !last_vis_pose || last_vis_pose->viewer != plan.pose.viewer;
        if( r.structure ) {
            last_occluder = gen;
            last_struct_pose = plan.pose;
            last_struct_px = px;
            last_struct_py = py;
        }
        if( r.vis ) {
            last_vis_pose = plan.pose;
        }
        return r;
    }
};

auto set_up_gate_map() -> void {
    clear_all_state();
    build_test_map( ter_id( "t_dirt" ) );
    g->place_player( tripoint_bub_ms( 60, 60, 0 ) );
    set_time( calendar::turn_zero + 12_hours );
    get_avatar().recalc_sight_limits();
    refresh_level_cache();
}

auto current_fold( map &here, const int z ) -> std::uint64_t {
    const level_cache &ch = here.get_cache_ref( z );
    return K::transparency_generation( ch ) ^ ( K::outside_generation( ch ) * OUTSIDE_PRIME );
}

} // namespace

TEST_CASE( "the plan's occluder stamp equals the retired frame-gate fold",
           "[render_frame_gate][level_cache_freshness]" ) {
    set_up_gate_map();
    map &here = get_map();
    const auto pose = K::pose_of_viewer( get_avatar(), 0 );
    const auto plan = K::plan_for( here, pose, K::lightmap_policy::skip );
    CHECK( plan.occluder[static_cast<size_t>( OVERMAP_DEPTH )] ==
           current_fold( here, 0 ) );

    // A terrain edit advances the transparency generation; the stamp follows.
    here.ter_set( tripoint_bub_ms( 62, 60, 0 ), ter_id( "t_wall" ) );
    const auto edited = K::plan_for( here, pose, K::lightmap_policy::skip );
    CHECK( edited.occluder[static_cast<size_t>( OVERMAP_DEPTH )] ==
           current_fold( here, 0 ) );
    CHECK( edited.occluder[static_cast<size_t>( OVERMAP_DEPTH )] !=
           plan.occluder[static_cast<size_t>( OVERMAP_DEPTH )] );
}

TEST_CASE( "the plan-fed frame gate agrees with the retired fold on the matrix",
           "[render_frame_gate][level_cache_freshness]" ) {
    old_gate old_g;
    new_gate new_g;

    auto step = [&]( const tripoint_bub_ms &viewer ) {
        map &here = get_map();
        const auto plan = K::plan_for( here, K::pose_of_viewer( get_avatar(), viewer.z() ),
                                       K::lightmap_policy::skip );
        const auto a = old_g.decide( current_fold( here, viewer.z() ), viewer.z(),
                                     here.get_abs_sub().raw(), viewer );
        const auto b = new_g.decide( plan );
        CHECK( a.structure == b.structure );
        CHECK( a.vis == b.vis );
    };

    SECTION( "quiet frames rebuild neither buffer" ) {
        set_up_gate_map();
        step( get_avatar().bub_pos() );   // first frame forces both
        step( get_avatar().bub_pos() );
        step( get_avatar().bub_pos() );
    }

    SECTION( "a terrain edit forces structure only" ) {
        set_up_gate_map();
        map &here = get_map();
        step( get_avatar().bub_pos() );
        here.ter_set( tripoint_bub_ms( 62, 60, 0 ), ter_id( "t_wall" ) );
        step( get_avatar().bub_pos() );
    }

    SECTION( "a player move forces vis only" ) {
        set_up_gate_map();
        step( get_avatar().bub_pos() );
        g->place_player( tripoint_bub_ms( 61, 60, 0 ) );
        step( get_avatar().bub_pos() );
    }

    SECTION( "camera drift within tolerance forces nothing" ) {
        set_up_gate_map();
        step( get_avatar().bub_pos() );
        g->place_player( tripoint_bub_ms( 62, 60, 0 ) );
        step( get_avatar().bub_pos() );   // 2 tiles: under DRIFT_TILES
    }

    SECTION( "camera drift past tolerance forces structure" ) {
        set_up_gate_map();
        step( get_avatar().bub_pos() );
        g->place_player( tripoint_bub_ms( 64, 60, 0 ) );
        step( get_avatar().bub_pos() );   // 4 tiles: at DRIFT_TILES
    }

    SECTION( "a z change forces both" ) {
        set_up_gate_map();
        step( get_avatar().bub_pos() );
        g->place_player( tripoint_bub_ms( 60, 60, 1 ) );
        step( get_avatar().bub_pos() );
    }
}
