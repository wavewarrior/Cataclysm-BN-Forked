#include "catch/catch_amalgamated.hpp"
#include "hud_runic.h"

#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

// Contract tests for the sidebar HUD's primitives. This TU is pure geometry,
// markup and palette — it deliberately touches no game state — so the invariants
// the whole HUD rests on can be pinned cheaply here rather than discovered by
// looking at a screenshot.
//
// Two of these lock bugs that actually shipped in the HUD this replaces.

namespace {

/// How many times `needle` occurs in `hay`.
auto count_of(std::string_view hay, std::string_view needle) -> int {
    if (needle.empty()) { return 0; }
    auto n = 0;
    for (std::size_t at = hay.find(needle); at != std::string_view::npos;
         at = hay.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

/// Relative luma of an `#rrggbbaa` string on the 8-bit channels. The ladder's
/// whole promise is that this is strictly increasing, so the test computes it
/// rather than trusting the table.
auto luma(std::string_view hex) -> double {
    REQUIRE(hex.size() == 9);
    REQUIRE(hex[0] == '#');
    const auto chan = [hex](std::size_t k) -> double {
        return static_cast<double>(std::stoi(std::string(hex.substr(1 + 2 * k, 2)), nullptr, 16));
    };
    return 0.2126 * chan(0) + 0.7152 * chan(1) + 0.0722 * chan(2);
}

/// Do two non-empty rects share any area?
auto intersects(const hud_runic::rect& a, const hud_runic::rect& b) -> bool {
    if (a.w <= 0.0f || a.h <= 0.0f || b.w <= 0.0f || b.h <= 0.0f) { return false; }
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

} // namespace

TEST_CASE("runic_crit_predicate_fires_on_the_shipped_bug", "[hud_runic]") {
    using hud_runic::is_critical;

    // THE regression. The shipping predicate was integer
    // `o.cur * 100 / o.max < 25` (panels.cpp:795-796): 8 * 100 / 30 truncates
    // to 26, which is not < 25, so a left arm at 26.7% health that was BOTH
    // bleeding and bitten never once rendered as critical. hud_anim's
    // is_critical missed identically at 0.267 vs 0.25.
    CHECK(is_critical({.cur = 8, .max = 30, .bleeding = true, .bitten = true}));

    // It must fire on the health ratio alone, with no effects at all — this is
    // the part the integer truncation was eating.
    CHECK(is_critical({.cur = 8, .max = 30}));

    // An actively bleeding or bitten limb is critical at ANY health, because
    // those are the states that kill you between turns.
    CHECK(is_critical({.cur = 30, .max = 30, .bleeding = true}));
    CHECK(is_critical({.cur = 30, .max = 30, .bitten = true}));

    // A healthy, unafflicted limb is not.
    CHECK_FALSE(is_critical({.cur = 30, .max = 30}));

    // Boundary: the threshold is a third, exclusive. Exactly one third is not
    // critical; a hair under it is.
    CHECK_FALSE(is_critical({.cur = 10, .max = 30}));
    CHECK(is_critical({.cur = 9, .max = 30}));

    // Broken is a different state with its own SPLINT/BROKE reading, so it does
    // not claim the crit row on its own — but it does when it is also bleeding.
    CHECK_FALSE(is_critical({.cur = 0, .max = 30, .broken = true}));
    CHECK(is_critical({.cur = 0, .max = 30, .bleeding = true, .broken = true}));

    // Degenerate max must not divide by zero or report critical.
    CHECK_FALSE(is_critical({.cur = 0, .max = 0}));
}

TEST_CASE("runic_layout_regions_never_overlap", "[hud_runic]") {
    // The property the whole chassis rests on: seven absolutely-positioned
    // regions that tile the viewport without ever painting over each other, at
    // every viewport the game can be run at and in every configuration that
    // moves a region. The cell grid this replaced could only round toward the
    // bottom anchor; the dp layout hits it exactly, which is what makes the
    // terrain carve in sidebar_hud_top_rows/_bottom_rows honest.
    struct ctx_size {
        float w;
        float h;
    };
    constexpr std::array<ctx_size, 5> sizes = {{
        {.w = 640.0f, .h = 480.0f},
        {.w = 1280.0f, .h = 720.0f},
        {.w = 1920.0f, .h = 1080.0f},
        {.w = 2560.0f, .h = 1440.0f},
        {.w = 800.0f, .h = 600.0f},
    }};
    constexpr std::array<int, 4> log_lines = {0, 3, 6, 12};

    for (const auto& sz : sizes) {
        for (const bool right : {true, false}) {
            for (const int lines : log_lines) {
                for (const bool veh : {true, false}) {
                    CAPTURE(sz.w, sz.h, right, lines, veh);
                    const auto l = hud_runic::layout_for(
                        {.ctx_w_dp = sz.w,
                         .ctx_h_dp = sz.h,
                         .sidebar_right = right,
                         .log_lines = lines,
                         .show_vehicle = veh});

                    const std::array<const hud_runic::rect*, 7> all =
                        {&l.status, &l.soma, &l.radar, &l.dock, &l.log, &l.keys, &l.vehicle};
                    for (std::size_t a = 0; a < all.size(); ++a) {
                        const auto& ra = *all[a];
                        if (ra.w <= 0.0f || ra.h <= 0.0f) { continue; }
                        // (b) every region lies inside the viewport.
                        CHECK(ra.x >= 0.0f);
                        CHECK(ra.y >= 0.0f);
                        CHECK(ra.x + ra.w <= sz.w);
                        CHECK(ra.y + ra.h <= sz.h);
                        // (a) no two regions intersect.
                        for (std::size_t b = a + 1; b < all.size(); ++b) {
                            CHECK_FALSE(intersects(ra, *all[b]));
                        }
                    }

                    // (c) the two carved strips are flush with the viewport edges,
                    // EXACTLY. `sidebar_hud_top_rows`/`_bottom_rows` carve the
                    // terrain viewport out of these two numbers, so a strip that
                    // merely approaches an edge leaves a sliver of live map under
                    // an opaque box.
                    CHECK(l.status.y == 0.0f);
                    CHECK(l.keys.y + l.keys.h == sz.h);
                }
            }
        }
    }
}

TEST_CASE("runic_ink_ladder_never_collapses", "[hud_runic]") {
    using hud_runic::ink;
    constexpr std::array<ink, 6> ladder =
        {ink::ground, ink::dead, ink::rule, ink::label, ink::datum, ink::peak};

    std::vector<double> lumas;
    for (const ink i : ladder) { lumas.push_back(luma(hud_runic::hex(i))); }

    // Strictly increasing, ground to peak: hierarchy in this HUD is luminance,
    // so two rungs that cross have swapped meanings on screen.
    for (std::size_t k = 1; k < lumas.size(); ++k) {
        CAPTURE(k, lumas[k - 1], lumas[k]);
        CHECK(lumas[k] > lumas[k - 1]);
    }

    // ground → datum is the load-bearing span, and every adjacent gap in it is
    // at least 35 8-bit levels — three times the ~10-level just-noticeable
    // difference, so no pair can collapse in greyscale or for any form of
    // colour blindness.
    for (std::size_t k = 1; k + 1 < lumas.size(); ++k) {
        CAPTURE(k);
        CHECK(lumas[k] - lumas[k - 1] >= 35.0);
    }

    // peak/datum is DELIBERATELY not held to 35 — it is 20.7, which is the
    // character creator's own fg/fg0 step. Nothing critical is encoded by peak
    // alone, so the design does not claim that gap; this assertion exists so
    // nobody "fixes" the ladder to satisfy a threshold it never promised.
    CHECK(lumas.back() > lumas[lumas.size() - 2]);

    // Six members and no gold rung. Gold is the stylesheet's, spent on marks
    // that are redundant with a shape; a seventh rung would let a producer
    // encode a reading in hue alone.
    CHECK(static_cast<int>(ink::peak) == 5);
}

TEST_CASE("runic_tint_emits_theme_classes_not_inline_colour", "[hud_runic]") {
    const auto out = hud_runic::tint(hud_runic::ink::datum, "ok");
    CHECK(out.find(R"(class="hud-i4")") != std::string::npos);
    // A producer that writes a colour bypasses the ladder, and the F4 Theme tab
    // then edits a palette half the HUD ignores.
    CHECK(out.find("style=") == std::string::npos);
    CHECK(out.find('#') == std::string::npos);

    // The class table and the public accessor must agree — every producer builds
    // its rows off the accessor, not off `tint`.
    CHECK(hud_runic::ink_class(hud_runic::ink::datum) == "hud-i4");
    CHECK(hud_runic::ink_class(hud_runic::ink::ground) == "hud-i0");
    CHECK(hud_runic::ink_class(hud_runic::ink::peak) == "hud-i5");
}

TEST_CASE("runic_pips_quantise_and_never_exceed_count", "[hud_runic]") {
    const auto lit = [](int cur, int max) -> int {
        const auto s = hud_runic::pips({.cur = cur, .max = max, .count = 12});
        // The meter is a fixed-width row of boxes: the TOTAL must never move, or
        // the value column beside it shifts as the bar fills.
        CHECK(count_of(s, R"(<div class="nc-pip)") == 12);
        return count_of(s, R"(class="nc-pip on")");
    };

    CHECK(lit(0, 100) == 0);
    CHECK(lit(100, 100) == 12);
    CHECK(lit(50, 100) == 6);
    // Rounded, not truncated: 12 * 5 / 100 = 0.6, and a limb with any health
    // left must not read as an empty meter.
    CHECK(lit(5, 100) == 1);
    CHECK(lit(2, 100) == 0);

    // A degenerate maximum yields an empty meter rather than dividing by zero.
    CHECK(lit(7, 0) == 0);
    CHECK(lit(7, -3) == 0);

    // Over-full clamps to the meter instead of overflowing the row — morale and
    // stamina both exceed their nominal span in play.
    CHECK(lit(250, 100) == 12);
}

TEST_CASE("runic_fact_omits_an_empty_sub", "[hud_runic]") {
    const auto with = hud_runic::fact({.label = "WIELD", .value = "crowbar", .sub = "9 bash"});
    CHECK(with.find("nc-fact-sub") != std::string::npos);

    // An empty sub-line is not free: `.nc-fact-sub` is a block, so an empty one
    // still spends 11dp of a fixed-height panel on nothing.
    const auto without = hud_runic::fact({.label = "WIELD", .value = "fists"});
    CHECK(without.find("nc-fact-sub") == std::string::npos);
    CHECK(without.find("nc-fact-label") != std::string::npos);

    // Same reasoning for a bare value — a target's name, a vehicle's name — which
    // has no caption to carry.
    const auto bare = hud_runic::fact({.value = "zombie"});
    CHECK(bare.find("nc-fact-label") == std::string::npos);
    CHECK(bare.find("nc-fact-value") != std::string::npos);
}

TEST_CASE("runic_helpers_escape_caller_text", "[hud_runic]") {
    // Every one of these renders game-supplied text — item names, effect names,
    // monster names, place names. `data-rml` parses its input as markup, so an
    // unescaped `<` in a player's custom item name is markup injection into the
    // HUD, not a cosmetic glitch.
    const std::string nasty = R"(a<b&c">d)";
    const std::vector<std::string> outs = {
        hud_runic::fact({.label = nasty, .value = nasty, .sub = nasty}),
        hud_runic::legend_item({.label = nasty, .value = nasty}),
        hud_runic::chip(nasty, hud_runic::ink::datum),
        hud_runic::tally_row(nasty, nasty, hud_runic::ink::datum, hud_runic::ink::label),
        hud_runic::subhead(nasty),
    };
    for (const std::string& s : outs) {
        CAPTURE(s);
        CHECK(s.find("&lt;") != std::string::npos);
        CHECK(s.find("&amp;") != std::string::npos);
        CHECK(s.find("&gt;") != std::string::npos);
        CHECK(s.find("a<b") == std::string::npos);
        // `rml_escape` deliberately leaves `"` alone, and these helpers must keep
        // it that way: caller text only ever lands in ELEMENT CONTENT here, never
        // inside an attribute value, so a quote cannot close one. A helper that
        // grew an attribute taking caller text would need its own escaper — this
        // assertion is here to make that a deliberate change rather than a
        // silent one.
        CHECK(s.find(R"(c"&gt;d)") != std::string::npos);
    }
}

TEST_CASE("runic_legend_item_drops_the_separator_on_a_bare_value", "[hud_runic]") {
    // A bare value — the season, the clock, the place name — joins a STATUS
    // group without inventing a caption for it, and a dangling `::` in front of
    // one reads as a missing label rather than as a deliberate absence.
    const auto bare = hud_runic::legend_item({.value = "Spring"});
    CHECK(bare.find("nc-legend-sep") == std::string::npos);
    CHECK(bare.find("nc-legend-label") == std::string::npos);

    const auto pair = hud_runic::legend_item({.label = "DAY", .value = "16"});
    CHECK(pair.find("nc-legend-sep") != std::string::npos);
    CHECK(pair.find("hud-lab") != std::string::npos);

    // The alarm chip replaces the rung class outright: it is a filled shape, and
    // a rung colour on top of the fill would make it unreadable.
    const auto alarm = hud_runic::legend_item(
        {.label = "SAFE MODE", .value = "OFF", .alarm = true});
    CHECK(alarm.find("hud-alarm") != std::string::npos);
    CHECK(alarm.find(R"(hud-text hud-i4)") == std::string::npos);

    // Every value span carries a class that declares `display`. RmlUi does NOT
    // blockify flex children, so an unclassed span would silently ignore its
    // width and alignment — which is exactly how the old target-HP bar became a
    // permanently empty trough.
    CHECK(bare.find(R"(class="hud-text )") != std::string::npos);
}

TEST_CASE("runic_panel_h_is_whole_rows", "[hud_runic]") {
    // `panels.cpp` inverts this to get the log's message budget:
    // floor( ( l.log.h - head_h - chrome_h ) / row_h ). If the two ever disagree
    // the log either sizes itself for a message it then clips, or reserves a row
    // it never fills.
    for (int n = 0; n <= 30; ++n) {
        CAPTURE(n);
        CHECK(hud_runic::panel_h(n) - hud_runic::head_h - hud_runic::chrome_h
              == static_cast<float>(n) * hud_runic::row_h);
    }
}

TEST_CASE("runic_layout_soma_collapses", "[hud_runic]") {
    // Collapsing has to reach the LAYOUT, not just the producer: a card that
    // still reserves its full column while showing three rows has given the
    // player back nothing, which is the whole point of the control.
    struct ctx_size {
        float w;
        float h;
    };
    constexpr std::array<ctx_size, 4> sizes = {{
        {.w = 1280.0f, .h = 720.0f},
        {.w = 1920.0f, .h = 1080.0f},
        {.w = 2560.0f, .h = 1440.0f},
        {.w = 800.0f, .h = 600.0f},
    }};

    for (const auto& sz : sizes) {
        for (const bool right : {true, false}) {
            CAPTURE(sz.w, sz.h, right);
            const auto opts = hud_runic::layout_options{
                .ctx_w_dp = sz.w,
                .ctx_h_dp = sz.h,
                .sidebar_right = right,
                .log_lines = 6,
                .show_vehicle = false};
            auto open = opts;
            open.soma_expanded = true;
            const auto expanded = hud_runic::layout_for(open);
            const auto collapsed = hud_runic::layout_for(opts);

            // Strictly smaller, and still a whole number of rows — a region that
            // ends mid-row spends those pixels drawing the top of a row nobody
            // can read.
            CHECK(collapsed.soma.h < expanded.soma.h);
            const auto rows =
                (collapsed.soma.h - hud_runic::head_h - hud_runic::chrome_h) / hud_runic::row_h;
            CHECK(rows == std::floor(rows));

            // Collapsing must not disturb anything else, and must not open a gap
            // for another region to overlap into.
            CHECK(collapsed.status == expanded.status);
            CHECK(collapsed.keys == expanded.keys);
            const std::array<const hud_runic::rect*, 7> all =
                {&collapsed.status, &collapsed.soma, &collapsed.radar,  &collapsed.dock,
                 &collapsed.log,    &collapsed.keys, &collapsed.vehicle};
            for (std::size_t a = 0; a < all.size(); ++a) {
                if (all[a]->w <= 0.0f || all[a]->h <= 0.0f) { continue; }
                for (std::size_t b = a + 1; b < all.size(); ++b) {
                    CHECK_FALSE(intersects(*all[a], *all[b]));
                }
            }
        }
    }

    // The collapsed card must still fit its content: three summary rows, the
    // POOLS block and the EFFECTS block, which do NOT collapse with it.
    const auto l = hud_runic::layout_for({.ctx_w_dp = 1920.0f, .ctx_h_dp = 1080.0f});
    CHECK(l.soma.h >= hud_runic::panel_h(16));
}
