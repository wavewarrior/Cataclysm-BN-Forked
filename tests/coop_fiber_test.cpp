#include "catch/catch_amalgamated.hpp"

#include "coop_fiber.h"
#include "driver_loop.h"

#include <vector>

// The agent driver rethrows what escapes a fiber; the co-op host must not be touched by that.
// Outside the driver a fiber runs, yields the host's events to its body and finishes exactly as
// it did before the driver existed.

TEST_CASE("a_fiber_outside_the_driver_yields_and_finishes_as_before", "[coop][fiber][driver]")
{
    REQUIRE_FALSE(driver_mode_active());

    auto steps = std::vector<int>();
    auto fiber = coop_fiber([&steps] {
        steps.push_back(1);
        coop_fiber::yield_event();
        steps.push_back(2);
    });

    CHECK_FALSE(fiber.done());
    fiber.resume(input_event());
    CHECK(steps == std::vector<int>{1});
    CHECK_FALSE(fiber.done());
    fiber.resume(input_event());
    CHECK(steps == std::vector<int>{1, 2});
    CHECK(fiber.done());
    CHECK_FALSE(coop_fiber::active());
}
