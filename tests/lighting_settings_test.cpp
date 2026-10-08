#include "catch/catch_amalgamated.hpp"
#include "lighting/lighting_settings.h"
#include "path_info.h"
#include "sdl_lighting_devui.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The knob-table seam (issue 116): the table is the single source for what the
// /tmp/cata_knob file channel accepts, what F8/F9 clamp against, and what the F4
// panel's widgets allow. Asserted without a GPU, a window or a world.

using namespace lighting;

namespace {

/// The 39 names the old `/tmp/cata_knob` if-chain accepted, in the old chain's
/// order (`git show HEAD~:src/sdl_input.cpp`, the `cata_knob` block). The test
/// below asserts the table resolves every one of them through the file channel
/// and nothing more: the chain is gone, the table is now the list.
constexpr std::array<std::string_view, 39> old_file_chain{{
    "vis_curve",        "vis_radius",         "ao_strength",        "ramp_enable",
    "shadow_mask_str",  "sun_scale",          "sky_scale",          "gi_strength",
    "cloud_strength",   "nrm_amount",         "gi_albedo",          "gi_feedback",
    "rc_readback",      "sun_arrow",          "guard_amount",       "portal_dirs",
    "portal_reach",     "sky_sun_enable",     "flicker_gain",       "shaft_enable",
    "shaft_intensity",  "shaft_length_scale", "shaft_width",        "dust_enable",
    "dust_density",     "dust_size",          "dust_drift",         "glow_enable",
    "glow_intensity",   "glow_radius",        "glow_saturation",    "crt_world",
    "shadow_steps",     "max_shadow_k",       "gi_bilat",           "light_eps",
    "force_rc_rebuild", "gi_enable",          "force_world_redraw",
}};

/// Reads `data/gui/devui.rml` and returns the `min`/`max`/`step` of the widget
/// bound to `name` via `data-value` or `data-checked`, or nullopt when the name
/// has no widget. A cheap line scanner: the markup is hand-written, one widget
/// per attribute occurrence.
struct markup_widget {
    bool ranged = false;
    float min = 0.0f;
    float max = 0.0f;
    float step = 0.0f;
    bool checkbox = false;
    bool event_only = false;
};

auto attr_of(std::string_view line, std::string_view attr) -> std::string {
    const auto needle = std::string(attr) + "=\"";
    const auto at = line.find(needle);
    if (at == std::string_view::npos) { return {}; }
    const auto begin = at + needle.size();
    const auto end = line.find('"', begin);
    return std::string(line.substr(begin, end - begin));
}

auto markup_widgets() -> std::vector<std::pair<std::string, markup_widget>> {
    std::vector<std::pair<std::string, markup_widget>> out;
    std::ifstream f(PATH_INFO::datadir() + "gui/devui.rml");
    REQUIRE(f.is_open());
    std::string line;
    while (std::getline(f, line)) {
        const std::string_view sv(line);
        markup_widget w;
        auto dv = attr_of(sv, "data-value");
        auto dc = attr_of(sv, "data-checked");
        auto ec = attr_of(sv, "data-event-click");
        if (dv.empty() && dc.empty() && ec.empty()) { continue; }
        if (!dv.empty()) {
            const auto mn = attr_of(sv, "min");
            const auto mx = attr_of(sv, "max");
            const auto st = attr_of(sv, "step");
            if (!mn.empty()) {
                w.ranged = true;
                std::istringstream(mn) >> w.min;
                std::istringstream(mx) >> w.max;
                std::istringstream(st) >> w.step;
            }
            out.emplace_back(std::move(dv), w);
        } else if (!dc.empty()) {
            w.checkbox = true;
            out.emplace_back(std::move(dc), w);
        } else {
            w.event_only = true;
            out.emplace_back(std::move(ec), w);
        }
    }
    return out;
}

} // namespace

TEST_CASE("knob table covers the old file channel exactly", "[lighting_settings]") {
    for (const std::string_view name : old_file_chain) {
        const knob_entry* e = knob_find(name);
        CAPTURE(name);
        REQUIRE(e != nullptr);
        CHECK((e->channels & knob_file) != 0u);
    }
    // No table entry pretends to be file-writable that the old chain rejected.
    for (const knob_entry& e : lighting_knob_table()) {
        if ((e.channels & knob_file) == 0u) { continue; }
        CAPTURE(e.name);
        CHECK(std::find(old_file_chain.begin(), old_file_chain.end(), e.name)
              != old_file_chain.end());
    }
    // The key-only knobs the file chain never had.
    for (const std::string_view name :
         {"emitter_scale", "dither_amt", "dither_bands", "debug_mode"}) {
        const knob_entry* e = knob_find(name);
        CAPTURE(name);
        REQUIRE(e != nullptr);
        CHECK((e->channels & knob_keys) != 0u);
        CHECK((e->channels & knob_file) == 0u);
    }
}


TEST_CASE("knob table cannot drift from the panel markup", "[lighting_settings]") {
    const auto widgets = markup_widgets();
    int checked_against_markup = 0;
    for (const knob_entry& e : lighting_knob_table()) {
        if ((e.channels & knob_panel) == 0u) { continue; }
        CAPTURE(e.name);
        const std::string_view wname = e.widget.empty() ? e.name : e.widget;
        const auto it = std::find_if(widgets.begin(), widgets.end(), [&](const auto& w) {
            return w.first == wname;
        });
        // Every panel knob has a widget; the markup is unchanged by this ticket.
        REQUIRE(it != widgets.end());
        if (e.panel) {
            // A ranged table entry must agree with the widget's attributes
            // exactly, or the panel would accept values the table forbids.
            REQUIRE(it->second.ranged);
            CHECK(it->second.min == e.panel->min);
            CHECK(it->second.max == e.panel->max);
            CHECK(it->second.step == e.panel->step);
            ++checked_against_markup;
        } else {
            // No range in the table: the widget is a checkbox, a button or the
            // debug-mode select, none of which carries min/max/step.
            CHECK_FALSE(it->second.ranged);
        }
    }
    // Guard the guard: the drift check actually compared the slider limits.
    CHECK(checked_against_markup >= 30);
}

TEST_CASE(
    "file channel writes raw and honours the old chain's one domain guard", "[lighting_settings]") {
    lighting_settings s;
    // Raw: far outside the panel's limits, and the value lands verbatim.
    CHECK(knob_apply_file(s, "vis_radius", 999.0f));
    CHECK(s.debug.vis_radius == 999.0f);
    CHECK(knob_apply_file(s, "ao_strength", -3.0f));
    CHECK(s.debug.ao_strength == -3.0f);
    // Bool-ish lanes come from `value > 0.5`, as the old chain did.
    CHECK(knob_apply_file(s, "shaft_enable", 0.6f));
    CHECK(g_shaft_enable);
    CHECK(knob_apply_file(s, "shaft_enable", 0.4f));
    CHECK_FALSE(g_shaft_enable);
    // The uint lane's `max(1, kv)` domain guard survives, verbatim.
    CHECK(knob_apply_file(s, "shadow_steps", 0.0f));
    CHECK(s.debug.shadow_steps == 1u);
    CHECK(knob_apply_file(s, "shadow_steps", 40.0f));
    CHECK(s.debug.shadow_steps == 40u);
    // Unknown names are refused, which is what the old `(unknown)` log meant.
    CHECK_FALSE(knob_apply_file(s, "not_a_knob", 1.0f));
    // Key-only names are refused on the file channel.
    CHECK_FALSE(knob_apply_file(s, "emitter_scale", 1.0f));
}

TEST_CASE("key clamps keep the old F8/F9 ranges", "[lighting_settings]") {
    struct pair_expect {
        std::string_view name;
        float min;
        float max;
        float step;
    };
    // The numbers the F8/F9 handlers always used (the former `lighting_dbg_range`
    // constants); the table is now their only home, so this pins them.
    const std::array<pair_expect, 6> want{{
        {"emitter_scale", 0.0f, 10.0f, 0.1f},
        {"sun_scale", 0.0f, 10.0f, 0.1f},
        {"sky_scale", 0.0f, 10.0f, 0.1f},
        {"gi_strength", 0.0f, 2.0f, 0.05f},
        {"dither_amt", 0.0f, 1.0f, 0.1f},
        {"dither_bands", 1.0f, 16.0f, 1.0f},
    }};
    for (const auto& w : want) {
        CAPTURE(w.name);
        const knob_entry* e = knob_find(w.name);
        REQUIRE(e != nullptr);
        REQUIRE(e->keys.has_value());
        CHECK(e->keys->min == w.min);
        CHECK(e->keys->max == w.max);
        CHECK(e->keys->step == w.step);
    }
    // Stepping clamps at the ends, exactly like `std::max(MIN, x - STEP)`.
    const knob_entry* gi = knob_find("gi_strength");
    REQUIRE(gi != nullptr);
    CHECK(knob_key_step(*gi->keys, 0.0f, -1.0f) == 0.0f);
    CHECK(knob_key_step(*gi->keys, 0.1f, -1.0f) == 0.05f);
    CHECK(knob_key_step(*gi->keys, 2.0f, 1.0f) == 2.0f);
    CHECK(knob_key_step(*gi->keys, 1.9f, 1.0f) == Catch::Approx(1.95f).margin(1e-6f));
}

TEST_CASE("force-once is consumed exactly once and every-frame is not", "[lighting_settings]") {
    lighting_settings s;
    CHECK_FALSE(s.pulses.take_force_once());

    // `force_rc_rebuild 2`: one rebuild, then back to none.
    CHECK(knob_apply_file(s, "force_rc_rebuild", 2.0f));
    CHECK(s.pulses.force_rebuild == force_rebuild_mode::once);
    CHECK(s.pulses.take_force_once());
    CHECK_FALSE(s.pulses.take_force_once());
    CHECK(s.pulses.force_rebuild == force_rebuild_mode::none);

    // `force_rc_rebuild 1`: continuous forcing is never consumed.
    CHECK(knob_apply_file(s, "force_rc_rebuild", 1.0f));
    CHECK_FALSE(s.pulses.take_force_once());
    CHECK(s.pulses.force_rebuild == force_rebuild_mode::every_frame);
    CHECK(knob_apply_file(s, "force_rc_rebuild", 0.0f));
    CHECK(s.pulses.force_rebuild == force_rebuild_mode::none);

    // The readback pulse is take-once too.
    CHECK(knob_apply_file(s, "rc_readback", 1.0f));
    CHECK(s.pulses.take_rc_readback());
    CHECK_FALSE(s.pulses.take_rc_readback());
}

TEST_CASE("the debug mode has one home", "[lighting_settings]") {
    lighting_settings s;
    CHECK(s.debug_mode() == 0u);
    CHECK_FALSE(s.diagnostic_view_active());
    // Whoever writes it (file, F7, panel), the cbuffer member and the predicate
    // read the same value.
    s.set_debug_mode(5u);
    CHECK(s.debug.debug_mode == 5u);
    CHECK_FALSE(s.diagnostic_view_active());
    s.set_debug_mode(6u);
    CHECK(s.debug.debug_mode == 6u);
    CHECK(s.diagnostic_view_active());
    // F7 advances and wraps after the last mode.
    s.set_debug_mode(debug_mode_count - 1u);
    s.cycle_debug_mode();
    CHECK(s.debug_mode() == 0u);
    s.cycle_debug_mode();
    CHECK(s.debug_mode() == 1u);
    s.set_debug_mode(6u);
    // The file channel does not write it: `/tmp/cata_dbg_mode` and `CATA_DBG_MODE` do.
    CHECK_FALSE(knob_apply_file(s, "debug_mode", 9.0f));
    CHECK(s.debug_mode() == 6u);
}

TEST_CASE(
    "int proxy reconciliation lets a file write survive the open panel", "[lighting_settings]") {
    // Storage 40 (a file write), proxy still at the last synced 16: storage wins.
    const auto file_won = knob_reconcile_int_proxy({.proxy = 16, .last = 16}, 40, 16);
    CHECK_FALSE(file_won.panel_won);
    CHECK(file_won.store == 40);
    CHECK(file_won.last == 40);

    // The panel moved its proxy to 20: the panel wins, and `last` follows.
    const auto panel_won = knob_reconcile_int_proxy({.proxy = 20, .last = 16}, 40, 20);
    CHECK(panel_won.panel_won);
    CHECK(panel_won.store == 20);
    CHECK(panel_won.last == 20);

    // The next tick with an untouched panel and a new file write: the file wins
    // again. This is the `shadow_steps` fix; a `last` that was never assigned
    // made the proxy overwrite the write every frame.
    const auto next = knob_reconcile_int_proxy(
        {.proxy = panel_won.store, .last = panel_won.last}, 33, panel_won.store);
    CHECK_FALSE(next.panel_won);
    CHECK(next.store == 33);

    // The panel's clamp is the caller's `store_clamped`: shadow_steps floors at 1.
    const auto floor = knob_reconcile_int_proxy({.proxy = 0, .last = 5}, 5, 1);
    CHECK(floor.panel_won);
    CHECK(floor.store == 1);
}
