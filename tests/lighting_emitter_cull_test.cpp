#include "catch/catch_amalgamated.hpp"
#include "lighting/gpu_emitter.h"
#include "lighting/snapshot.h"

#include <ranges>
#include <vector>

// partition_emitters_by_view keeps an emitter iff its radius disc reaches the camera
// rect grown by EMITTER_VIEW_MARGIN_TILES. Sprite shading reads only the kept prefix,
// so a wrong cull darkens on-screen lights and a wrong order changes which emitters
// the top-K shadow trace picks.

namespace {

// Grown rect is [10 - 4, 20 + 4] = [6, 24] on both axes.
constexpr auto view = lighting::emitter_view_rect{.x0 = 10, .y0 = 10, .w = 10, .h = 10};

auto emitter_at(float x, float y, float radius, std::uint32_t tag) -> lighting::gpu_emitter {
    auto e = lighting::gpu_emitter{};
    e.pos_x = x;
    e.pos_y = y;
    e.radius = radius;
    e.flicker_seed = tag;
    return e;
}

auto tags(const std::vector<lighting::gpu_emitter>& es) -> std::vector<std::uint32_t> {
    return es | std::views::transform(&lighting::gpu_emitter::flicker_seed)
         | std::ranges::to<std::vector>();
}

} // namespace

TEST_CASE("emitter_view_cull_degenerate_rect_keeps_all_in_order", "[lighting]") {
    auto es = std::vector{emitter_at(500.0f, 500.0f, 1.0f, 0), emitter_at(15.0f, 15.0f, 1.0f, 1)};
    const auto n = lighting::partition_emitters_by_view(es, {.x0 = 10, .y0 = 10, .w = 0, .h = 10});
    CHECK(n == 2);
    CHECK(tags(es) == std::vector<std::uint32_t>{0, 1});
}

TEST_CASE("emitter_view_cull_tangent_disc_is_culled", "[lighting]") {
    // 3 tiles right of the grown edge (x = 24), inside it vertically.
    auto tangent = std::vector{emitter_at(27.0f, 15.0f, 3.0f, 0)};
    CHECK(lighting::partition_emitters_by_view(tangent, view) == 0);

    auto reaching = std::vector{emitter_at(27.0f, 15.0f, 3.25f, 0)};
    CHECK(lighting::partition_emitters_by_view(reaching, view) == 1);

    // Corner: dx = dy = 3 from the grown corner (24, 24) → distance sqrt(18) ≈ 4.243.
    auto corner =
        std::vector{emitter_at(27.0f, 27.0f, 4.25f, 0), emitter_at(27.0f, 27.0f, 4.0f, 1)};
    CHECK(lighting::partition_emitters_by_view(corner, view) == 1);
    CHECK(corner.front().flicker_seed == 0);
}

TEST_CASE("emitter_view_cull_keeps_relative_order", "[lighting]") {
    auto es = std::vector{
        emitter_at(15.0f, 15.0f, 1.0f, 0),  // inside
        emitter_at(100.0f, 15.0f, 5.0f, 1), // far right
        emitter_at(2.0f, 15.0f, 4.5f, 2),   // 4 left of grown edge, reaches
        emitter_at(15.0f, -50.0f, 8.0f, 3), // far above
        emitter_at(24.0f, 24.0f, 0.5f, 4),  // on the grown corner
    };
    const auto n = lighting::partition_emitters_by_view(es, view);
    REQUIRE(n == 3);
    const auto order = tags(es);
    CHECK(std::vector(order.begin(), order.begin() + n) == std::vector<std::uint32_t>{0, 2, 4});
}
