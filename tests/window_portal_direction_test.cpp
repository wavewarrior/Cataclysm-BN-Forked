#include "avatar.h"
#include "catch/catch_amalgamated.hpp"
#include "coordinates.h"
#include "lighting/event_queue.h"
#include "lighting/gpu_emitter.h"
#include "lighting/snapshot.h"
#include "level_cache_freshness.h"
#include "map.h"
#include "map_helpers.h"
#include "state_helpers.h"
#include "type_id.h"

#include <algorithm>
#include <optional>

// Window-portal CONE emitters (lighting/snapshot.cpp collect_zlev) feed the godray
// shaft/dust pass. A window embedded in a wall run must aim its cone along the wall's
// inward normal and be tagged WINDOW_PORTAL_WALL; anything else keeps its room light
// but is tagged UNCLASSIFIED so it never draws a shaft.
//
// The house roof covers the footprint only, as mapgen roofs do. outside_cache is
// dilated (any open sky in the 3x3 above), so the perimeter walls flanking every
// window read "outside"; an overhanging roof would hide that trap.

namespace {

constexpr auto house_x0 = 55;
constexpr auto house_x1 = 65;
constexpr auto house_y0 = 60;
constexpr auto house_y1 = 66;

auto build_house() -> void {
    clear_all_state();
    auto& here = get_map();
    const auto wall = ter_id("t_wall");
    const auto floor = ter_id("t_floor");
    const auto roof = ter_id("t_flat_roof");
    for (auto x = house_x0; x <= house_x1; ++x) {
        for (auto y = house_y0; y <= house_y1; ++y) {
            const auto perimeter = x == house_x0 || x == house_x1 || y == house_y0 || y == house_y1;
            here.ter_set(tripoint_bub_ms(x, y, 0), perimeter ? wall : floor);
            here.ter_set(tripoint_bub_ms(x, y, 1), roof);
        }
    }
    // build_emitter_snapshot scans only the avatar's z-level.
    get_avatar().setpos(tripoint_bub_ms(60, 63, 0));
}

auto rebuild_caches() -> void {
    auto& here = get_map();
    level_cache_freshness::invalidate_level( here, 1 );
    rebuild_level_cache(0);
}

auto sun_travelling(float dir_x, float dir_y) -> lighting::sun_params {
    return {
        .sun_dir_x = dir_x,
        .sun_dir_y = dir_y,
        .sun_sin_elev = 0.5f,
        .sun_intensity = 1.0f,
        .sun_r = 1.0f,
        .sun_g = 0.95f,
        .sun_b = 0.8f,
        .sky_r = 0.5f,
        .sky_g = 0.6f,
        .sky_b = 0.9f,
        .sky_intensity = 0.5f,
        .sp_pad = 0.0f};
}

auto window_cone_at(const tripoint_bub_ms& p, const lighting::sun_params& sun)
    -> std::optional<lighting::gpu_emitter> {
    auto eq = lighting::event_queue{};
    const auto snap = lighting::build_emitter_snapshot(eq, 16.0f, sun);
    const auto it = std::ranges::find_if(snap, [&](const lighting::gpu_emitter& e) {
        return e.shape == static_cast<std::uint32_t>(lighting::emitter_shape::CONE)
            && e.window_portal > 0.0f && static_cast<int>(e.pos_x) == p.x()
            && static_cast<int>(e.pos_y) == p.y();
    });
    if (it == snap.end()) { return std::nullopt; }
    return *it;
}

} // namespace

TEST_CASE("window in a wall run aims its cone into the room", "[lighting][window_portal]") {
    build_house();
    auto& here = get_map();

    SECTION("north wall window, sun shining in from the north") {
        const auto win = tripoint_bub_ms(60, house_y0, 0);
        here.ter_set(win, ter_id("t_window"));
        rebuild_caches();
        REQUIRE(here.is_outside(win + point(-1, 0)));
        REQUIRE(here.is_outside(win + point(1, 0)));

        const auto cone = window_cone_at(win, sun_travelling(0.0f, 1.0f));
        REQUIRE(cone);
        CHECK(cone->window_portal == lighting::WINDOW_PORTAL_WALL);
        CHECK(cone->cone_dir_x == 0.0f);
        CHECK(cone->cone_dir_y == 1.0f);
        CHECK(cone->window_direct > 6.0f);

        const auto grazing = window_cone_at(win, sun_travelling(1.0f, 0.0f));
        REQUIRE(grazing); // diffuse sky keeps the room light
        CHECK(grazing->window_portal == lighting::WINDOW_PORTAL_WALL);
        CHECK(grazing->window_direct < 6.0f);
    }

    SECTION("east wall window points west, not along the wall") {
        const auto win = tripoint_bub_ms(house_x1, 63, 0);
        here.ter_set(win, ter_id("t_window"));
        rebuild_caches();

        const auto cone = window_cone_at(win, sun_travelling(-1.0f, 0.0f));
        REQUIRE(cone);
        CHECK(cone->window_portal == lighting::WINDOW_PORTAL_WALL);
        CHECK(cone->cone_dir_x == -1.0f);
        CHECK(cone->cone_dir_y == 0.0f);
        CHECK(cone->window_direct > 6.0f);
    }

    SECTION("window with an open flank keeps its light but is unclassified") {
        const auto win = tripoint_bub_ms(60, house_y0, 0);
        here.ter_set(win, ter_id("t_window"));
        here.ter_set(win + point(1, 0), ter_id("t_floor"));
        rebuild_caches();

        const auto cone = window_cone_at(win, sun_travelling(0.0f, 1.0f));
        REQUIRE(cone);
        CHECK(cone->window_portal == lighting::WINDOW_PORTAL_UNCLASSIFIED);
    }
}
