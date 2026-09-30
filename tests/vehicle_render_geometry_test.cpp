#include "catch/catch_amalgamated.hpp"
#include "coordinates.h"
#include "map.h"
#include "map_helpers.h"
#include "state_helpers.h"
#include "string_formatter.h"
#include "type_id.h"
#include "units.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vehicle_render_geometry.h"

#include <cmath>
#include <vector>

namespace {

auto spawn_car(const units::angle dir) -> vehicle* {
    clear_all_state();
    map& here = get_map();
    build_test_map(ter_id("t_pavement"));
    vehicle* veh = here.add_vehicle(vproto_id("car_test"), tripoint_bub_ms(60, 60, 0), dir, 100, 0);
    REQUIRE(veh != nullptr);
    return veh;
}

/// Shortest signed arc from `a` to `b`, in (-pi, pi].
auto arc(float a, float b) -> float {
    constexpr float two_pi = 2.0f * static_cast<float>(M_PI);
    float d = std::fmod(b - a + static_cast<float>(M_PI), two_pi);
    if (d < 0.0f) { d += two_pi; }
    return d - static_cast<float>(M_PI);
}

/// True when every standalone part's precalc[0] is the refresh_precalc( physics_angle )
/// layout, i.e. the frame's authority branch reproduces this vehicle's tiles.
auto layout_is_continuous(const vehicle& veh) -> bool {
    const float c = std::cos(veh.physics_angle);
    const float s = std::sin(veh.physics_angle);
    for (const int p : veh.all_standalone_parts()) {
        const vehicle_part& vp = veh.cpart(p);
        const float mx = static_cast<float>(vp.mount.x());
        const float my = static_cast<float>(vp.mount.y());
        const point_rel_ms
            want{static_cast<int>(std::round(mx * c - my * s)),
                 static_cast<int>(std::round(mx * s + my * c))};
        if (vp.precalc[0] != want) { return false; }
    }
    return true;
}

/// Every standalone part's tile must sit where the frame puts its mount.
void check_parts_match_frame(const vehicle& veh, const vehicle_render_frame& frame) {
    const std::vector<int> standalone = veh.all_standalone_parts();
    REQUIRE_FALSE(standalone.empty());
    for (const int p : standalone) {
        const tripoint_mnt_veh& m = veh.cpart(p).mount;
        const vehicle_render_point pt =
            vehicle_mount_to_bubble(frame, static_cast<float>(m.x()), static_cast<float>(m.y()));
        const point_bub_ms want = veh.bub_part_location(p).xy();
        INFO("part " << p << " mount " << m.x() << ',' << m.y());
        CHECK(std::lround(pt.x) == want.x());
        CHECK(std::lround(pt.y) == want.y());
    }
}

} // namespace

TEST_CASE("vehicle_render_frame_matches_part_tiles", "[vehicle][render]") {
    // Branch B: the discrete rotate_to_world layout, which the frame reproduces exactly at
    // the cardinal headings (it is sheared at the intermediate 15 degree steps).
    for (const units::angle dir : {0_degrees, 90_degrees, 180_degrees, 270_degrees}) {
        SECTION(string_format("cardinal %d before any physics step", units::to_degrees(dir))) {
            vehicle* veh = spawn_car(dir);
            check_parts_match_frame(*veh, make_vehicle_render_frame(*veh));
        }
    }

    // Branch A: one turn of Box2D authority rewrites precalc[0] from the continuous angle.
    // 30 degrees is included because at the cardinal headings both branches agree, so only a
    // skewed heading proves the continuous layout is the one being modelled.
    for (const units::angle dir : {0_degrees, 30_degrees, 90_degrees, 180_degrees, 270_degrees}) {
        SECTION(string_format("continuous %d after one vehmove", units::to_degrees(dir))) {
            vehicle* veh = spawn_car(dir);
            REQUIRE(veh->box2d_position_authority);
            get_map().vehmove();
            CHECK(layout_is_continuous(*veh));
            // The sub-tile residual is not part of the tile layout, and a mount landing on a
            // half-tile boundary would round either way; pin it for the tile comparison.
            veh->render_offset_x = 0.0f;
            veh->render_offset_y = 0.0f;
            const vehicle_render_frame frame = make_vehicle_render_frame(*veh);
            CHECK(std::abs(arc(frame.angle, units::to_radians(veh->face.dir()))) < 1e-3f);
            check_parts_match_frame(*veh, frame);
        }
    }
}

TEST_CASE("vehicle_motion_ease", "[vehicle][render]") {
    vehicle_motion_state state;
    reset_vehicle_motion(state, vehicle_render_pose{}, 0.0);

    SECTION("eases toward a nearby target without arriving in one frame") {
        const vehicle_render_pose p = advance_vehicle_motion(
            state, {.target = vehicle_render_pose{1.0f, 0.0f, 0.0f}, .now = 0.05});
        CHECK(p.x > 0.0f);
        CHECK(p.x < 1.0f);
        CHECK(p == state.shown);
    }

    SECTION("snaps a jump past the snap radius") {
        const vehicle_render_pose p = advance_vehicle_motion(
            state, {.target = vehicle_render_pose{70.0f, 0.0f, 0.0f}, .now = 0.05});
        CHECK(p.x == 70.0f);
    }

    SECTION("takes the shortest arc through zero") {
        reset_vehicle_motion(state, vehicle_render_pose{0.0f, 0.0f, 6.109f}, 0.0);
        const vehicle_render_pose p = advance_vehicle_motion(
            state, {.target = vehicle_render_pose{0.0f, 0.0f, 0.1745f}, .now = 0.05});
        // Forward across the wrap, not backwards through 6.109 towards 0.1745.
        CHECK(p.angle > 6.109f);
    }

    SECTION("tail-snaps a residual under the epsilon") {
        const vehicle_render_pose p = advance_vehicle_motion(
            state, {.target = vehicle_render_pose{0.005f, 0.0f, 0.0f}, .now = 0.05});
        CHECK(p.x == 0.005f);
    }
}

TEST_CASE("vehicle_motion_tick_paced", "[vehicle][render]") {
    vehicle_motion_state state;
    reset_vehicle_motion(state, vehicle_render_pose{}, 0.0);
    constexpr vehicle_motion_mode mode = vehicle_motion_mode::tick_paced;

    // Committed at 1.0: the segment starts here, so nothing has moved yet.
    advance_vehicle_motion(
        state, {.target = vehicle_render_pose{1.0f, 0.0f, 0.0f}, .now = 1.0, .mode = mode});
    CHECK(state.shown.x == 0.0f);

    advance_vehicle_motion(
        state, {.target = vehicle_render_pose{1.0f, 0.0f, 0.0f}, .now = 1.5, .mode = mode});
    CHECK(state.shown.x == Catch::Approx(0.5f).margin(1e-4f));

    advance_vehicle_motion(
        state, {.target = vehicle_render_pose{1.0f, 0.0f, 0.0f}, .now = 2.0, .mode = mode});
    CHECK(state.shown.x == Catch::Approx(1.0f).margin(1e-4f));

    // New commit at 2.25 travels 1 -> 2 over the previous inter-commit gap, which is
    // 2.25 - 1.0 clamped to 1.0s; halfway through it, at 2.75, is 1.5.
    advance_vehicle_motion(
        state, {.target = vehicle_render_pose{2.0f, 0.0f, 0.0f}, .now = 2.25, .mode = mode});
    advance_vehicle_motion(
        state, {.target = vehicle_render_pose{2.0f, 0.0f, 0.0f}, .now = 2.75, .mode = mode});
    CHECK(state.shown.x == Catch::Approx(1.5f).margin(1e-4f));
}

TEST_CASE("vehicle_path_band_geometry", "[vehicle][render]") {
    const vehicle_render_frame frame{
        .angle = 0.0f,
        .origin_x = 10.0f,
        .origin_y = 10.0f,
        .mount_min_x = -1,
        .mount_min_y = -1,
        .mount_max_x = 2,
        .mount_max_y = 1,
    };

    SECTION("straight ahead at a standstill") {
        const vehicle_path_band band = make_vehicle_path_band({.frame = frame});
        CHECK(band.centre[0].x == Catch::Approx(12.5f).margin(1e-4f));
        CHECK(band.centre[0].y == Catch::Approx(10.0f).margin(1e-4f));
        // Stopped, so the minimum length of 4 tiles applies.
        CHECK(band.centre.back().x == Catch::Approx(16.5f).margin(1e-4f));
        CHECK(band.centre.back().y == Catch::Approx(10.0f).margin(1e-4f));
        for (int i = 0; i < vehicle_path_band::samples; ++i) {
            // Three mounts wide; the pair straddles the centre line perpendicular to travel.
            const float dx = band.left[i].x - band.right[i].x;
            const float dy = band.left[i].y - band.right[i].y;
            CHECK(std::hypot(dx, dy) == Catch::Approx(3.0f).margin(1e-4f));
            CHECK(band.centre[i].x
                  == Catch::Approx(0.5f * (band.left[i].x + band.right[i].x)).margin(1e-4f));
            CHECK(band.centre[i].y
                  == Catch::Approx(0.5f * (band.left[i].y + band.right[i].y)).margin(1e-4f));
        }
    }

    SECTION("steered right bends the end sideways") {
        const vehicle_path_band band = make_vehicle_path_band(
            {.frame = frame, .steer_angle = units::to_radians(15_degrees)});
        CHECK(band.centre.back().y > 10.0f);
    }

    SECTION("reversing leaves the rear edge") {
        const vehicle_path_band band = make_vehicle_path_band({.frame = frame, .velocity = -500});
        CHECK(band.centre[0].x == Catch::Approx(8.5f).margin(1e-4f));
        CHECK(band.centre.back().x < 8.5f);
    }

    SECTION("length tracks two turns of look-ahead") {
        // 1788 cm/s is 10 tiles/s, so two turns of look-ahead is 20 tiles.
        const vehicle_path_band band = make_vehicle_path_band({.frame = frame, .velocity = 1788});
        CHECK(band.centre.back().x == Catch::Approx(32.5f).margin(0.05f));
    }
}
