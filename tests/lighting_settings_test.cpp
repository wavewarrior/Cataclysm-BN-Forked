#include "catch/catch_amalgamated.hpp"
#include "lighting/lighting_settings.h"
#include "path_info.h"
#include "sdl_lighting_devui.h"

#include <array>
#include <algorithm>
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
    "vis_curve", "vis_radius", "ao_strength", "ramp_enable", "shadow_mask_str",
    "sun_scale", "sky_scale", "gi_strength", "cloud_strength", "nrm_amount",
    "gi_albedo", "gi_feedback", "rc_readback", "sun_arrow", "guard_amount",
    "portal_dirs", "portal_reach", "sky_sun_enable", "flicker_gain", "shaft_enable",
    "shaft_intensity", "shaft_length_scale", "shaft_width", "dust_enable",
    "dust_density", "dust_size", "dust_drift", "glow_enable", "glow_intensity",
    "glow_radius", "glow_saturation", "crt_world", "shadow_steps", "max_shadow_k",
    "gi_bilat", "light_eps", "force_rc_rebuild", "gi_enable", "force_world_redraw",
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

auto attr_of( std::string_view line, std::string_view attr ) -> std::string {
    const auto needle = std::string( attr ) + "=\"";
    const auto at = line.find( needle );
    if( at == std::string_view::npos ) {
        return {};
    }
    const auto begin = at + needle.size();
    const auto end = line.find( '"', begin );
    return std::string( line.substr( begin, end - begin ) );
}

auto markup_widgets() -> std::vector<std::pair<std::string, markup_widget>> {
    std::vector<std::pair<std::string, markup_widget>> out;
    std::ifstream f( PATH_INFO::datadir() + "gui/devui.rml" );
    REQUIRE( f.is_open() );
    std::string line;
    while( std::getline( f, line ) ) {
        const std::string_view sv( line );
        markup_widget w;
        auto dv = attr_of( sv, "data-value" );
        auto dc = attr_of( sv, "data-checked" );
        auto ec = attr_of( sv, "data-event-click" );
        if( dv.empty() && dc.empty() && ec.empty() ) {
            continue;
        }
        if( !dv.empty() ) {
            const auto mn = attr_of( sv, "min" );
            const auto mx = attr_of( sv, "max" );
            const auto st = attr_of( sv, "step" );
            if( !mn.empty() ) {
                w.ranged = true;
                std::istringstream( mn ) >> w.min;
                std::istringstream( mx ) >> w.max;
                std::istringstream( st ) >> w.step;
            }
            out.emplace_back( std::move( dv ), w );
        } else if( !dc.empty() ) {
            w.checkbox = true;
            out.emplace_back( std::move( dc ), w );
        } else {
            w.event_only = true;
            out.emplace_back( std::move( ec ), w );
        }
    }
    return out;
}

} // namespace

TEST_CASE( "knob table covers the old file channel exactly", "[lighting_settings]" ) {
    for( const std::string_view name : old_file_chain ) {
        const knob_entry *e = knob_find( name );
        CAPTURE( name );
        REQUIRE( e != nullptr );
        CHECK( ( e->channels & knob_file ) != 0u );
    }
    // No table entry pretends to be file-writable that the old chain rejected.
    for( const knob_entry &e : lighting_knob_table() ) {
        if( ( e.channels & knob_file ) == 0u ) {
            continue;
        }
        CAPTURE( e.name );
        CHECK( std::find( old_file_chain.begin(), old_file_chain.end(), e.name )
               != old_file_chain.end() );
    }
    // The key-only knobs the file chain never had.
    for( const std::string_view name : { "emitter_scale", "dither_amt", "dither_bands",
                                         "debug_mode" } ) {
        const knob_entry *e = knob_find( name );
        CAPTURE( name );
        REQUIRE( e != nullptr );
        CHECK( ( e->channels & knob_keys ) != 0u );
        CHECK( ( e->channels & knob_file ) == 0u );
    }
}


TEST_CASE( "knob table cannot drift from the panel markup", "[lighting_settings]" ) {
    const auto widgets = markup_widgets();
    int checked_against_markup = 0;
    for( const knob_entry &e : lighting_knob_table() ) {
        if( ( e.channels & knob_panel ) == 0u ) {
            continue;
        }
        CAPTURE( e.name );
        const std::string_view wname = e.widget.empty() ? e.name : e.widget;
        const auto it = std::find_if( widgets.begin(), widgets.end(),
        [&]( const auto &w ) {
            return w.first == wname;
        } );
        // Every panel knob has a widget; the markup is unchanged by this ticket.
        REQUIRE( it != widgets.end() );
        if( e.panel ) {
            // A ranged table entry must agree with the widget's attributes
            // exactly, or the panel would accept values the table forbids.
            REQUIRE( it->second.ranged );
            CHECK( it->second.min == e.panel->min );
            CHECK( it->second.max == e.panel->max );
            CHECK( it->second.step == e.panel->step );
            ++checked_against_markup;
        } else {
            // No range in the table: the widget is a checkbox, a button or the
            // debug-mode select, none of which carries min/max/step.
            CHECK_FALSE( it->second.ranged );
        }
    }
    // Guard the guard: the drift check actually compared the slider limits.
    CHECK( checked_against_markup >= 30 );
}

TEST_CASE( "file channel writes raw and honours the old chain's one domain guard",
           "[lighting_settings]" ) {
    lighting_settings s;
    // Raw: far outside the panel's limits, and the value lands verbatim.
    CHECK( knob_apply_file( s, "vis_radius", 999.0f ) );
    CHECK( s.debug.vis_radius == 999.0f );
    CHECK( knob_apply_file( s, "ao_strength", -3.0f ) );
    CHECK( s.debug.ao_strength == -3.0f );
    // Bool-ish lanes come from `value > 0.5`, as the old chain did.
    CHECK( knob_apply_file( s, "shaft_enable", 0.6f ) );
    CHECK( g_shaft_enable );
    CHECK( knob_apply_file( s, "shaft_enable", 0.4f ) );
    CHECK_FALSE( g_shaft_enable );
    // The uint lane's `max(1, kv)` domain guard survives, verbatim.
    CHECK( knob_apply_file( s, "shadow_steps", 0.0f ) );
    CHECK( s.debug.shadow_steps == 1u );
    CHECK( knob_apply_file( s, "shadow_steps", 40.0f ) );
    CHECK( s.debug.shadow_steps == 40u );
    // Unknown names are refused, which is what the old `(unknown)` log meant.
    CHECK_FALSE( knob_apply_file( s, "not_a_knob", 1.0f ) );
    // Key-only names are refused on the file channel.
    CHECK_FALSE( knob_apply_file( s, "emitter_scale", 1.0f ) );
}

TEST_CASE( "key clamps match the old lighting_dbg_range constants",
           "[lighting_settings]" ) {
    using namespace lighting_dbg_range;
    struct pair_expect {
        std::string_view name;
        float min;
        float max;
        float step;
    };
    // The F8/F9 handlers read these four ranges out of `lighting_dbg_range`; the
    // table must carry the same numbers so the two cannot drift.
    const std::array<pair_expect, 6> want{{
        { "emitter_scale", SCALE_MIN, SCALE_MAX, SCALE_STEP },
        { "sun_scale", SCALE_MIN, SCALE_MAX, SCALE_STEP },
        { "sky_scale", SCALE_MIN, SCALE_MAX, SCALE_STEP },
        { "gi_strength", GI_MIN, GI_MAX, GI_STEP },
        { "dither_amt", DAMT_MIN, DAMT_MAX, DAMT_STEP },
        { "dither_bands", DBND_MIN, DBND_MAX, DBND_STEP },
    }};
    for( const auto &w : want ) {
        CAPTURE( w.name );
        const knob_entry *e = knob_find( w.name );
        REQUIRE( e != nullptr );
        REQUIRE( e->keys.has_value() );
        CHECK( e->keys->min == w.min );
        CHECK( e->keys->max == w.max );
        CHECK( e->keys->step == w.step );
    }
    // Stepping clamps at the ends, exactly like `std::max(MIN, x - STEP)`.
    const knob_entry *gi = knob_find( "gi_strength" );
    REQUIRE( gi != nullptr );
    CHECK( knob_key_step( *gi->keys, 0.0f, -1.0f ) == 0.0f );
    CHECK( knob_key_step( *gi->keys, 0.1f, -1.0f ) == 0.05f );
    CHECK( knob_key_step( *gi->keys, 2.0f, 1.0f ) == 2.0f );
    CHECK( knob_key_step( *gi->keys, 1.9f, 1.0f ) == Catch::Approx( 1.95f ).margin( 1e-6f ) );
}