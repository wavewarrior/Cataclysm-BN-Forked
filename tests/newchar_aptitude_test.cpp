#include "catch/catch_amalgamated.hpp"
#include "newchar_aptitude.h"

#include <vector>

// The aptitude radar on the SKILLS creator step is a square dot grid that has to read as a circular
// display with a beam turning in one direction. Four things carry that illusion, and each of them
// fails silently — the disc still renders, just wrong — so each gets a case here:
//
//   * the disc MASK, which is the only reason a square grid looks round;
//   * the ANGLE frame, up-and-clockwise rather than the mathematical convention, because a radar is
//     read that way and swapping atan2's arguments back mirrors the whole display;
//   * the trailing glow, including its continuity across the 2pi wrap that a linear sweep like
//     nc_bio_scan's does not have — a jump there reads as a flicker once per revolution;
//   * sector membership, since the wedges have to TILE the turn — a gap leaves dots no category can
//     claim, an overlap makes a dot's colour depend on which category was asked first.

namespace {
constexpr float turn = 2.0F * std::numbers::pi_v<float>;
} // namespace

TEST_CASE("nc_apt_the_disc_mask_is_a_circle_on_a_square_grid", "[newchar][aptitude]") {
    // Dots outside the disc are drawn as nothing, so this mask IS the circle. It must be symmetric
    // under both reflections or the disc sits off-centre in its box, and the corners must be out or
    // the display is simply a square.
    for (int row = 0; row < nc_apt::grid; row++) {
        for (int col = 0; col < nc_apt::grid; col++) {
            const bool here = nc_apt::inside(nc_apt::offset_of(col, row));
            CHECK(here == nc_apt::inside(nc_apt::offset_of(nc_apt::grid - 1 - col, row)));
            CHECK(here == nc_apt::inside(nc_apt::offset_of(col, nc_apt::grid - 1 - row)));
        }
    }

    CHECK(nc_apt::inside(nc_apt::offset_of(nc_apt::mid, nc_apt::mid)));
    CHECK_FALSE(nc_apt::inside(nc_apt::offset_of(0, 0)));
    CHECK_FALSE(nc_apt::inside(nc_apt::offset_of(nc_apt::grid - 1, 0)));
    CHECK_FALSE(nc_apt::inside(nc_apt::offset_of(0, nc_apt::grid - 1)));
    CHECK_FALSE(nc_apt::inside(nc_apt::offset_of(nc_apt::grid - 1, nc_apt::grid - 1)));
    // The rim ring is part of the disc: the four dots at exactly `mid` cells must survive the mask,
    // otherwise the outermost ring and the deepest wedges have nothing to draw on.
    CHECK(nc_apt::inside(nc_apt::offset_of(nc_apt::mid, 0)));
    CHECK(nc_apt::inside(nc_apt::offset_of(0, nc_apt::mid)));
}

TEST_CASE("nc_apt_angles_start_up_and_run_clockwise", "[newchar][aptitude]") {
    // Row 0 is the TOP row, so "one row above centre" is up. Getting this frame wrong mirrors the
    // sweep and renumbers every sector, while leaving a display that still looks plausible.
    const float up = nc_apt::angle_of(nc_apt::offset_of(nc_apt::mid, nc_apt::mid - 1));
    const float right = nc_apt::angle_of(nc_apt::offset_of(nc_apt::mid + 1, nc_apt::mid));
    const float down = nc_apt::angle_of(nc_apt::offset_of(nc_apt::mid, nc_apt::mid + 1));
    const float left = nc_apt::angle_of(nc_apt::offset_of(nc_apt::mid - 1, nc_apt::mid));

    CHECK(up == Catch::Approx(0.0F).margin(1e-5));
    CHECK(right == Catch::Approx(turn * 0.25F).margin(1e-5));
    CHECK(down == Catch::Approx(turn * 0.5F).margin(1e-5));
    CHECK(left == Catch::Approx(turn * 0.75F).margin(1e-5));
    CHECK(nc_apt::angle_of(nc_apt::offset_of(nc_apt::mid, nc_apt::mid)) == 0.0F);

    // Half-open range over every dot: the producer compares these against a beam bearing, and a
    // single dot reporting a full turn is a dot that never lights.
    for (int row = 0; row < nc_apt::grid; row++) {
        for (int col = 0; col < nc_apt::grid; col++) {
            const float a = nc_apt::angle_of(nc_apt::offset_of(col, row));
            CHECK(a >= 0.0F);
            CHECK(a < turn);
        }
    }
}

TEST_CASE("nc_apt_the_beam_turns_once_per_period_and_loops", "[newchar][aptitude]") {
    // One revolution per sweep_secs, so the sweep cannot silently stop or run at some other rate.
    CHECK(nc_apt::beam_at(0.0F) == Catch::Approx(0.0F).margin(1e-5));
    CHECK(nc_apt::beam_at(nc_apt::sweep_secs * 0.25F) == Catch::Approx(turn * 0.25F).margin(1e-4));

    float prev = -1.0F;
    for (float s = 0.0F; s < nc_apt::sweep_secs * 0.999F; s += 0.05F) {
        const float b = nc_apt::beam_at(s);
        CHECK(b >= 0.0F);
        CHECK(b < turn);
        CHECK(b > prev);
        prev = b;
    }

    // Exactly periodic: one period later the beam is back where it was, at the period boundary and
    // at an arbitrary offset into it.
    CHECK(nc_apt::beam_at(nc_apt::sweep_secs) == Catch::Approx(0.0F).margin(1e-4));
    CHECK(nc_apt::beam_at(nc_apt::sweep_secs + 1.3F)
          == Catch::Approx(nc_apt::beam_at(1.3F)).margin(1e-4));
    CHECK(nc_apt::beam_at(4.0F * nc_apt::sweep_secs + 1.3F)
          == Catch::Approx(nc_apt::beam_at(1.3F)).margin(1e-3));
}

TEST_CASE("nc_apt_the_glow_trails_the_beam_head", "[newchar][aptitude]") {
    constexpr float beam = 2.0F;

    // Full brightness at the head, nothing at all ahead of it. Ahead is zero rather than a
    // symmetric falloff because a symmetric glow makes the direction of travel ambiguous, which is
    // the entire content of the animation.
    CHECK(nc_apt::glow(beam, beam) == Catch::Approx(1.0F));
    for (float ahead = 0.02F; ahead < turn - nc_apt::tail - 0.02F; ahead += 0.07F) {
        CHECK(nc_apt::glow(beam, beam + ahead) == 0.0F);
    }

    // Behind it: strictly fading, reaching zero exactly at the end of the tail and staying there.
    float prev = 1.0F + 1e-5F;
    for (float back = 0.0F; back <= nc_apt::tail; back += 0.05F) {
        const float v = nc_apt::glow(beam, beam - back);
        CHECK(v <= prev + 1e-5F);
        CHECK(v >= 0.0F);
        CHECK(v <= 1.0F);
        prev = v;
    }
    CHECK(nc_apt::glow(beam, beam - nc_apt::tail) == Catch::Approx(0.0F).margin(1e-5));
    CHECK(nc_apt::glow(beam, beam - nc_apt::tail - 0.3F) == 0.0F);
    CHECK(nc_apt::alpha_of(nc_apt::glow(beam, beam - nc_apt::tail - 0.3F)) == 0);
}

TEST_CASE("nc_apt_the_glow_is_continuous_across_the_wrap", "[newchar][aptitude]") {
    // The one defect worth a case of its own: unlike a linear scan, this loop closes on itself, and
    // any angular arithmetic that does not wrap identically on both sides of 2pi shows up as a
    // flicker once per revolution rather than as a sweep.
    constexpr float eps = 1e-3F;
    const float below = turn - eps;
    const float above = eps;

    // Thetas near the head itself are skipped on purpose: the LEADING edge is a deliberate step
    // from dark to full, so only the rest of the disc is expected to be continuous here.
    for (float theta = 0.3F; theta < turn - 0.3F; theta += 0.05F) {
        CHECK(nc_apt::glow(below, theta) == Catch::Approx(nc_apt::glow(above, theta)).margin(1e-2));
    }

    // And the dots the tail is actually on at that moment are lit on both sides, not dark on one.
    CHECK(nc_apt::glow(below, turn - 0.5F) > 0.3F);
    CHECK(nc_apt::glow(above, turn - 0.5F) > 0.3F);
    CHECK(nc_apt::behind(above, below) == Catch::Approx(2.0F * eps).margin(1e-4));

    // The wrap is HALF-OPEN, and that is not pedantry: subtracting two bearings a hair apart gives
    // a tiny negative, and adding a turn to a tiny negative rounds to exactly a turn in float. A
    // `behind` of one whole turn is "further back than any tail", so every dot the beam crosses
    // would go dark for the one frame its bearing sits a hair ahead of the head — a seam travelling
    // round the disc.
    const float hair_ahead = std::nextafterf(1.0F, 2.0F);
    CHECK(nc_apt::wrap_turn(-1e-9F) < turn);
    CHECK(nc_apt::behind(1.0F, hair_ahead) < turn);
    CHECK(nc_apt::glow(1.0F, hair_ahead) > 0.9F);
}

TEST_CASE("nc_apt_sectors_partition_the_disc", "[newchar][aptitude]") {
    // Every category owns a wedge, and the wedges must tile the turn exactly: a gap leaves dots no
    // category can claim, an overlap makes a dot's colour depend on which category was asked first.
    for (int n = 1; n <= 9; n++) {
        std::vector<int> seen(static_cast<size_t>(n), 0);
        for (int row = 0; row < nc_apt::grid; row++) {
            for (int col = 0; col < nc_apt::grid; col++) {
                const nc_apt::vec off = nc_apt::offset_of(col, row);
                if (!nc_apt::inside(off)) { continue; }
                const int k = nc_apt::sector_of(nc_apt::angle_of(off), n);
                CHECK(k >= 0);
                CHECK(k < n);
                seen[static_cast<size_t>(k)]++;
                // Exactly one wedge claims it once the wedges are deep enough to cover the disc.
                int claims = 0;
                for (int j = 0; j < n; j++) {
                    if (nc_apt::in_sector(off, j, n, nc_apt::disc_radius())) { claims++; }
                }
                CHECK(claims == 1);
            }
        }
        // No wedge is empty at this resolution, or a category would be invisible however much the
        // player invested in it.
        for (int k = 0; k < n; k++) { CHECK(seen[static_cast<size_t>(k)] > 0); }
    }
}

TEST_CASE("nc_apt_sector_zero_starts_at_twelve_o_clock", "[newchar][aptitude]") {
    // The legend lists categories in sector order, so the first one has to be the wedge a player
    // finds at the top. A half-step offset would still look like a pie while mislabelling all of
    // it.
    CHECK(nc_apt::sector_of(0.0F, 4) == 0);
    CHECK(nc_apt::sector_of(turn * 0.24F, 4) == 0);
    CHECK(nc_apt::sector_of(turn * 0.26F, 4) == 1);
    CHECK(nc_apt::sector_of(turn * 0.51F, 4) == 2);
    CHECK(nc_apt::sector_of(turn * 0.76F, 4) == 3);
    // A bearing a hair under a full turn belongs to the LAST sector, never a phantom n-th one.
    CHECK(nc_apt::sector_of(std::nextafter(turn, 0.0F), 4) == 3);
    CHECK(nc_apt::sector_of(turn, 4) == 0);
    // An empty category list must not divide by zero.
    CHECK(nc_apt::sector_of(2.0F, 0) == 0);
    CHECK(nc_apt::sector_of(2.0F, -3) == 0);
}

TEST_CASE("nc_apt_growing_a_sector_only_ever_adds_dots", "[newchar][aptitude]") {
    // Buying a skill level deepens a wedge. If membership were not monotone in reach, a dot could
    // go dark as the aptitude it reports goes UP.
    constexpr int n = 6;
    const float max_reach = nc_apt::disc_radius();

    for (int k = 0; k < n; k++) {
        for (int row = 0; row < nc_apt::grid; row++) {
            for (int col = 0; col < nc_apt::grid; col++) {
                const nc_apt::vec off = nc_apt::offset_of(col, row);
                bool lit = false;
                for (float reach = 0.0F; reach <= max_reach; reach += 0.25F) {
                    const bool hit = nc_apt::in_sector(off, k, n, reach);
                    CHECK((hit || !lit));
                    lit = lit || hit;
                    if (hit) {
                        // Never beyond the reach: a wedge that overshoots reports an aptitude
                        // nobody bought.
                        CHECK(nc_apt::radius_of(off) <= reach + 1e-4F);
                    }
                }
            }
        }
    }

    // A wedge of zero reach is just the hub dot, and only for the sector that owns bearing 0.
    for (int row = 0; row < nc_apt::grid; row++) {
        for (int col = 0; col < nc_apt::grid; col++) {
            const bool centre = col == nc_apt::mid && row == nc_apt::mid;
            CHECK(nc_apt::in_sector(nc_apt::offset_of(col, row), 0, n, 0.0F) == centre);
        }
    }
}

TEST_CASE("nc_apt_rings_sit_at_thirds_of_the_radius", "[newchar][aptitude]") {
    // The rings are the disc's scale. They must not touch each other, or the field between them
    // disappears and the radar reads as a solid blob.
    for (int k = 1; k <= nc_apt::rings; k++) {
        const float r =
            static_cast<float>(nc_apt::mid) * static_cast<float>(k)
            / static_cast<float>(nc_apt::rings);
        CHECK(nc_apt::on_ring(r));
        CHECK_FALSE(nc_apt::on_ring(r + nc_apt::ring_tol + 0.05F));
        CHECK_FALSE(nc_apt::on_ring(r - nc_apt::ring_tol - 0.05F));
    }
    CHECK_FALSE(nc_apt::on_ring(0.0F));
}

TEST_CASE("nc_apt_alpha_covers_the_full_byte_range", "[newchar][aptitude]") {
    CHECK(nc_apt::alpha_of(0.0F) == 0);
    CHECK(nc_apt::alpha_of(1.0F) == 255);
    // Clamped rather than trusted: a future easing curve overshooting would otherwise wrap the byte
    // and the brightest dot of the sweep would render transparent.
    CHECK(nc_apt::alpha_of(1.6F) == 255);
    CHECK(nc_apt::alpha_of(-0.4F) == 0);
}
