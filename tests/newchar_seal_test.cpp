#include "catch/catch_amalgamated.hpp"
#include "newchar_seal.h"

#include <set>
#include <vector>

// The record seal on the OVERVIEW creator step is a ring of seven step glyphs welded shut once per
// cycle. Four things carry that, and each of them fails silently — the ring still renders, just
// wrong — so each gets a case here:
//
//   * the RING itself: it must be closed (no cell of it isolated from its neighbours) and every
//   node
//     must sit ON it, because a node off the thread breaks the ring visibly at that node;
//   * the seven nodes must land in seven DISTINCT cells, or two steps share a glyph socket and one
//   of
//     them is simply absent from the seal;
//   * the bead: dark ahead, brightest at the head, and behind it a floor that PERSISTS. A decay to
//     zero would make the seal a scan, and progress round the ring would stop being visible;
//   * the strike: `struck` is a flag rather than `arc == 2pi`, because nc_apt::wrap_turn is
//   half-open
//     and a full turn wraps to 0 — the closed ring would render dark at the exact moment it closes.

namespace {
constexpr float turn = 2.0F * std::numbers::pi_v<float>;
} // namespace

TEST_CASE("nc_seal_the_ring_is_closed_and_one_dot_thick", "[newchar][seal]") {
    std::vector<nc_seal::cell> ring;
    for (int row = 0; row < nc_apt::grid; row++) {
        for (int col = 0; col < nc_apt::grid; col++) {
            if (nc_seal::classify(col, row) != nc_seal::layer::empty
                && nc_seal::classify(col, row) != nc_seal::layer::hub) {
                ring.push_back({.col = col, .row = row});
            }
        }
    }
    // Enough dots to read as a circle rather than as a polygon of blobs, and few enough that the
    // band has not silently widened into a disc.
    CHECK(ring.size() >= 24);
    CHECK(ring.size() <= 48);

    // Closed: every ring cell has a ring neighbour within one and a half cells in both directions
    // round the loop. A gap is the one defect a seal cannot have, and it is invisible in code — it
    // shows up as a ring with a bite out of it.
    for (const nc_seal::cell& a : ring) {
        int near = 0;
        for (const nc_seal::cell& b : ring) {
            if (a == b) { continue; }
            const float dx = static_cast<float>(a.col - b.col);
            const float dy = static_cast<float>(a.row - b.row);
            if (std::hypot(dx, dy) <= 1.5F) { near++; }
        }
        CHECK(near >= 2);
    }

    // The hub is the centre cell and nothing else, so the OVERVIEW glyph cannot end up drawn twice.
    int hubs = 0;
    for (int row = 0; row < nc_apt::grid; row++) {
        for (int col = 0; col < nc_apt::grid; col++) {
            if (nc_seal::classify(col, row) == nc_seal::layer::hub) {
                hubs++;
                CHECK(col == nc_apt::mid);
                CHECK(row == nc_apt::mid);
            }
        }
    }
    CHECK(hubs == 1);
}

TEST_CASE("nc_seal_the_seven_nodes_are_distinct_cells_on_the_ring", "[newchar][seal]") {
    std::set<nc_seal::cell> seen;
    for (int i = 0; i < nc_seal::nodes; i++) {
        const nc_seal::cell c = nc_seal::node_cell(i);
        // Inside the lattice: a node rounded off the grid would be a step missing from the seal.
        CHECK(c.col >= 0);
        CHECK(c.col < nc_apt::grid);
        CHECK(c.row >= 0);
        CHECK(c.row < nc_apt::grid);
        // On the thread band, so the ring runs through it rather than past it.
        CHECK(nc_seal::on_ring(c.col, c.row));
        CHECK(nc_seal::classify(c.col, c.row) == nc_seal::layer::node);
        CHECK(nc_seal::node_at(c.col, c.row) == i);
        CHECK(seen.insert(c).second);
    }
    CHECK(seen.size() == static_cast<size_t>(nc_seal::nodes));

    // Node 0 is at twelve o'clock and they run clockwise, so the ring reads as the step rail closed
    // into a loop: the rail's first step has to be the node a player finds at the top.
    CHECK(nc_seal::node_bearing(0) == 0.0F);
    CHECK(nc_seal::node_cell(0).col == nc_apt::mid);
    CHECK(nc_seal::node_cell(0).row < nc_apt::mid);
    CHECK(nc_seal::node_cell(1).col > nc_apt::mid);
    CHECK(nc_seal::node_cell(nc_seal::nodes - 1).col < nc_apt::mid);
    for (int i = 0; i < nc_seal::nodes; i++) {
        CHECK(nc_seal::node_bearing(i) >= 0.0F);
        CHECK(nc_seal::node_bearing(i) < turn);
    }
}

TEST_CASE("nc_seal_the_bead_runs_once_per_cycle_and_loops", "[newchar][seal]") {
    CHECK(nc_seal::at(0.0F).arc == Catch::Approx(0.0F).margin(1e-5));
    CHECK_FALSE(nc_seal::at(0.0F).struck);
    CHECK(nc_seal::at(nc_seal::weld_total * 0.5F).arc == Catch::Approx(turn * 0.5F).margin(1e-4));

    // Strictly advancing through the weld, and never outside a turn: the producer feeds these
    // bearings to angle arithmetic that would wrap an overshoot into the start of the ring.
    float prev = -1.0F;
    for (float s = 0.0F; s < nc_seal::weld_total * 0.999F; s += 0.02F) {
        const nc_seal::phase p = nc_seal::at(s);
        CHECK_FALSE(p.struck);
        CHECK(p.arc >= 0.0F);
        CHECK(p.arc <= turn);
        CHECK(p.arc > prev);
        prev = p.arc;
    }

    // Exactly periodic, at the boundary and at an arbitrary offset into a later cycle, so the loop
    // cannot drift over a long session on this screen.
    CHECK(nc_seal::at(nc_seal::cycle_secs).arc == Catch::Approx(0.0F).margin(1e-4));
    CHECK(nc_seal::at(nc_seal::cycle_secs + 0.7F).arc
          == Catch::Approx(nc_seal::at(0.7F).arc).margin(1e-3));
    CHECK(nc_seal::at(6.0F * nc_seal::cycle_secs + 0.7F).struck == nc_seal::at(0.7F).struck);
}

TEST_CASE("nc_seal_the_glow_trails_the_bead_and_leaves_a_floor", "[newchar][seal]") {
    const nc_seal::phase p = {.arc = 2.0F, .struck = false, .hub = 0.0F};

    // At the bead: full. Ahead of it: nothing at all — this pass has not reached that bearing, and
    // a symmetric falloff would make the direction of travel ambiguous.
    CHECK(nc_seal::glow_at(p, p.arc) == Catch::Approx(1.0F));
    for (float ahead = 0.05F; ahead < turn - p.arc - 0.05F; ahead += 0.07F) {
        CHECK(nc_seal::glow_at(p, p.arc + ahead) == 0.0F);
    }

    // Behind it: fading to the weld floor and STAYING there. This is what separates a weld from a
    // scan — the ring being closed so far is the content of the animation, so it must persist.
    float prev = 1.0F + 1e-5F;
    for (float back = 0.0F; back <= nc_seal::tail; back += 0.03F) {
        const float v = nc_seal::glow_at(p, p.arc - back);
        CHECK(v <= prev + 1e-5F);
        CHECK(v >= nc_seal::weld_level - 1e-5F);
        prev = v;
    }
    CHECK(nc_seal::glow_at(p, p.arc - nc_seal::tail)
          == Catch::Approx(nc_seal::weld_level).margin(1e-5));
    CHECK(nc_seal::glow_at(p, 0.0F) == Catch::Approx(nc_seal::weld_level).margin(1e-5));
    // Bearing 0 is where the bead starts, so it is lit from the first instant of a pass rather than
    // waiting a full revolution for the bead to come back to it.
    CHECK(
        nc_seal::glow_at({.arc = 0.0F, .struck = false, .hub = 0.0F}, 0.0F) == Catch::Approx(1.0F));
    CHECK(nc_seal::glow_at({.arc = 0.0F, .struck = false, .hub = 0.0F}, 0.3F) == 0.0F);

    // A node flares exactly when the bead reaches its bearing, never before.
    for (int i = 1; i < nc_seal::nodes; i++) {
        const float th = nc_seal::node_bearing(i);
        CHECK(nc_seal::node_glow({.arc = th - 0.05F, .struck = false, .hub = 0.0F}, i) == 0.0F);
        CHECK(nc_seal::node_glow({.arc = th, .struck = false, .hub = 0.0F}, i)
              == Catch::Approx(1.0F));
    }
}

TEST_CASE("nc_seal_the_strike_lights_the_whole_ring", "[newchar][seal]") {
    // The trap this case exists for: nc_apt::wrap_turn is HALF-OPEN, so a full turn wraps to 0. Had
    // the hold been expressed as `arc == 2pi` and fed through that arithmetic, every bearing would
    // report itself un-welded and the ring would go dark at the exact moment it closes — the one
    // frame of the cycle a seal must not lose.
    const nc_seal::phase p = nc_seal::at(nc_seal::weld_total + nc_seal::hold_secs * 0.5F);
    REQUIRE(p.struck);
    for (float theta = 0.0F; theta < turn; theta += 0.1F) {
        CHECK(nc_seal::glow_at(p, theta) == Catch::Approx(nc_seal::struck_level));
    }
    for (int i = 0; i < nc_seal::nodes; i++) {
        CHECK(nc_seal::node_glow(p, i) == Catch::Approx(nc_seal::struck_level));
    }
    CHECK(nc_seal::struck_level > nc_seal::weld_level);
}

TEST_CASE(
    "nc_seal_the_hub_flares_at_the_strike_and_the_cycle_closes_on_itself", "[newchar][seal]") {
    // Warming through the weld: monotone, so the emblem reads as charging rather than flickering.
    float prev = -1.0F;
    for (float s = 0.0F; s < nc_seal::weld_total; s += 0.02F) {
        const float h = nc_seal::at(s).hub;
        CHECK(h >= prev - 1e-5F);
        CHECK(h <= nc_seal::hub_warm + 1e-5F);
        prev = h;
    }

    // The strike is a STEP, and it is the only one in the cycle: the flare is the seal being
    // struck, so it is deliberately not eased.
    const float before = nc_seal::at(nc_seal::weld_total - 1e-3F).hub;
    const float after = nc_seal::at(nc_seal::weld_total + 1e-3F).hub;
    CHECK(before == Catch::Approx(nc_seal::hub_warm).margin(1e-2));
    CHECK(after > 0.98F);

    // And it decays to exactly zero by the end of the hold, which is where the next pass starts it.
    // Any other end value pops once per cycle at the wrap.
    CHECK(nc_seal::at(nc_seal::cycle_secs - 1e-4F).hub == Catch::Approx(0.0F).margin(1e-3));
    CHECK(nc_seal::at(0.0F).hub == Catch::Approx(0.0F).margin(1e-5));
}

TEST_CASE("nc_seal_node_alpha_never_reaches_zero", "[newchar][seal]") {
    // A node's glyph names a step the player has already been through. Fading it to nothing for two
    // thirds of every cycle reads as a rendering fault, so nodes keep a floor where thread does
    // not.
    CHECK(nc_seal::alpha_of(0.0F) == 0);
    CHECK(nc_seal::alpha_of(1.0F) == 255);
    CHECK(nc_seal::node_alpha_of(0.0F) == nc_seal::node_floor_alpha);
    CHECK(nc_seal::node_alpha_of(1.0F) == 255);
    // Clamped rather than trusted, the same reason nc_apt::alpha_of is: an easing curve
    // overshooting 1 would otherwise wrap the byte and the brightest socket would render
    // transparent.
    CHECK(nc_seal::node_alpha_of(1.7F) == 255);
    CHECK(nc_seal::node_alpha_of(-0.4F) == nc_seal::node_floor_alpha);
    CHECK(nc_seal::alpha_of(1.7F) == 255);
}
