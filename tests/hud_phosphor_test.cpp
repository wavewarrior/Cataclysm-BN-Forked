#include "catch/catch_amalgamated.hpp"

#include <string>
#include <string_view>

#include "hud_phosphor.h"

// Contract tests for the terminal-phosphor HUD's primitives. This TU is pure
// geometry, text and palette — it deliberately touches no game state — so the
// invariants the whole HUD's alignment rests on can be pinned cheaply here
// rather than discovered by looking at a screenshot.
//
// Two of these lock bugs that actually shipped in the HUD this replaces.

namespace
{

// Count display cells the same way the HUD does, so a test failure means the
// contract broke rather than that the test counted bytes.
auto cells( std::string_view s ) -> int
{
    return hud_phosphor::display_width( s );
}

} // namespace

TEST_CASE( "phosphor_crit_predicate_fires_on_the_shipped_bug", "[hud_phosphor]" )
{
    using hud_phosphor::is_critical;

    // THE regression. The shipping predicate was integer
    // `o.cur * 100 / o.max < 25` (panels.cpp:795-796): 8 * 100 / 30 truncates
    // to 26, which is not < 25, so a left arm at 26.7% health that was BOTH
    // bleeding and bitten never once rendered as critical. hud_anim's
    // is_critical missed identically at 0.267 vs 0.25.
    CHECK( is_critical( { .cur = 8, .max = 30, .bleeding = true, .bitten = true } ) );

    // It must fire on the health ratio alone, with no effects at all — this is
    // the part the integer truncation was eating.
    CHECK( is_critical( { .cur = 8, .max = 30 } ) );

    // An actively bleeding or bitten limb is critical at ANY health, because
    // those are the states that kill you between turns.
    CHECK( is_critical( { .cur = 30, .max = 30, .bleeding = true } ) );
    CHECK( is_critical( { .cur = 30, .max = 30, .bitten = true } ) );

    // A healthy, unafflicted limb is not.
    CHECK_FALSE( is_critical( { .cur = 30, .max = 30 } ) );

    // Boundary: the threshold is a third, exclusive. Exactly one third is not
    // critical; a hair under it is.
    CHECK_FALSE( is_critical( { .cur = 10, .max = 30 } ) );
    CHECK( is_critical( { .cur = 9, .max = 30 } ) );

    // Broken is a different state with its own mend treatment, so it does not
    // claim the inverted row on its own — but it does when it is also bleeding.
    CHECK_FALSE( is_critical( { .cur = 0, .max = 30, .broken = true } ) );
    CHECK( is_critical( { .cur = 0, .max = 30, .bleeding = true, .broken = true } ) );

    // Degenerate max must not divide by zero or report critical.
    CHECK_FALSE( is_critical( { .cur = 0, .max = 0 } ) );
}

TEST_CASE( "phosphor_pad_is_cell_exact_for_every_glyph_class", "[hud_phosphor]" )
{
    using hud_phosphor::pad;
    using hud_phosphor::pad_left;

    // `pad` is the guard that stops a producer overrunning its region. The HUD
    // this replaces had no such guard and put 1554 dp of text in a 1520 dp box,
    // because three of nine hotbar actions rendered the literal string
    // "[Unbound globally!]".
    const std::string ascii = "L ARM";
    const std::string box = "\u2500\u2524\u251c\u252c\u2534\u253c"; // ─┤├┬┴┼
    const std::string block = "\u2588\u2584\u2591\u258f";           // █▄░▏
    const std::string latin1 = "8 \u00b0C";                        // 8 °C

    for( const std::string &s : {
             ascii, box, block, latin1
         } ) {
        // Padding short and truncating long must BOTH land exactly on the width.
        CHECK( cells( pad( s, 20 ) ) == 20 );
        CHECK( cells( pad( s, 2 ) ) == 2 );
        CHECK( cells( pad_left( s, 20 ) ) == 20 );
        CHECK( cells( pad_left( s, 2 ) ) == 2 );
        // Zero and negative widths are a clamped-out region, not a crash.
        CHECK( cells( pad( s, 0 ) ) == 0 );
    }

    // Multi-byte glyphs must be counted, not measured in bytes: a box-drawing
    // glyph is three UTF-8 bytes and exactly one cell.
    CHECK( cells( box ) == 6 );
    CHECK( box.size() == 18 );

    // Truncation happens on code-point boundaries, so a partial sequence can
    // never reach the renderer. Cutting the six box glyphs to four cells must
    // yield four whole glyphs, i.e. twelve bytes.
    CHECK( pad( box, 4 ).size() == 12 );

    // Padding is U+00A0 NO-BREAK SPACE, not U+0020, and that is load-bearing
    // rather than incidental. RmlUi strips leading and trailing whitespace from
    // inline runs at PARSE time, and the producers' markup is injected through
    // `data-rml`, which parses with the element's computed style — so
    // `white-space: pre` declared on the element being created cannot preserve it.
    // Padding that landed at a `<span>` boundary was therefore deleted outright,
    // which in-game welded every label to its value: `DAY16Spring12:59:46`,
    // `STR13DEX22INT9PER20`, `MISSION MARKERNONE`. U+00A0 is not whitespace for
    // that purpose and survives unconditionally. Anyone "cleaning this up" back to
    // a plain space reintroduces the bug, so it is pinned here.
    const std::string nbsp = "\u00a0";
    CHECK( pad_left( "8/30", 9 ) == nbsp + nbsp + nbsp + nbsp + nbsp + "8/30" );
    CHECK( pad( "8/30", 9 ) == "8/30" + nbsp + nbsp + nbsp + nbsp + nbsp );
    // And the width logic must agree with the fill it emits, or every row is off
    // by the number of pad characters.
    CHECK( cells( nbsp ) == 1 );
    CHECK( cells( pad( "8/30", 9 ) ) == 9 );
}

TEST_CASE( "phosphor_rule_is_cols_exact_and_places_its_junctions", "[hud_phosphor]" )
{
    using hud_phosphor::rule;
    using hud_phosphor::rule_title;

    // A rule that is not exactly its region's width is a visible seam, and the
    // status rule's crossings have to line up with verticals emitted by two
    // other producers — so width exactness is the whole contract.
    for( const int cols : {
             1, 2, 8, 34, 35, 92, 160, 192
         } ) {
        CHECK( cells( rule( { .cols = cols } ) ) == cols );
    }

    // A title interrupts the rule, DOS-style, and therefore costs zero rows.
    const std::string titled = rule( {
        .cols = 34,
        .titles = { rule_title{ .col = 3, .text = "pools" } },
        .right = "\u2524",
    } );
    CHECK( cells( titled ) == 34 );
    CHECK( titled.find( "POOLS" ) != std::string::npos ); // upper-cased

    // Crossings land where an internal vertical meets the rule.
    const std::string crossed = rule( {
        .cols = 160,
        .crossings = { 33, 125 },
        .crossing_glyph = "\u253c",
    } );
    CHECK( cells( crossed ) == 160 );

    // Out-of-range titles and crossings are DROPPED, not clamped into a wrong
    // position — a junction glyph in the wrong column is worse than an absent
    // one, because it implies a vertical that is not there.
    const std::string oob = rule( {
        .cols = 10,
        .titles = { rule_title{ .col = 40, .text = "nope" } },
        .crossings = { 99, -5 },
    } );
    CHECK( cells( oob ) == 10 );
    CHECK( oob.find( "NOPE" ) == std::string::npos );

    // End glyphs occupy the first and last cell without widening the rule.
    const std::string ends = rule( { .cols = 12, .left = "\u251c", .right = "\u2524" } );
    CHECK( cells( ends ) == 12 );
}

TEST_CASE( "phosphor_bar_is_cell_exact_across_its_whole_range", "[hud_phosphor]" )
{
    using hud_phosphor::bar;

    // `bar` returns RML with tint spans around the fill and the trough, so the
    // glyph run has to be recovered before it can be measured.
    const auto glyphs = []( std::string_view rml ) -> std::string {
        std::string out;
        bool in_tag = false;
        for( const char c : rml )
        {
            if( c == '<' ) {
                in_tag = true;
            } else if( c == '>' ) {
                in_tag = false;
            } else if( !in_tag ) {
                out += c;
            }
        }
        return out;
    };

    // Bars are glyph runs, which is what makes the old empty-trough failure
    // impossible to repeat: `.tbar-fill` was an inline <span> with height:100%
    // and no display, so RmlUi collapsed it and the target HP bar was
    // permanently empty. A glyph bar has no box to collapse — but it does have
    // to be exactly as wide as it claims, at every value including out-of-range
    // ones, or it shifts the numeric column beside it.
    for( const int width : {
             1, 9, 13, 15, 20
         } ) {
        for( int max = 1; max <= 40; ++max ) {
            for( int cur = -3; cur <= max + 3; ++cur ) {
                const std::string b = bar( { .cur = cur, .max = max, .cells = width } );
                INFO( "cur=" << cur << " max=" << max << " cells=" << width );
                REQUIRE( cells( glyphs( b ) ) == width );
            }
        }
    }

    // Full and empty differ in the fill/trough split but not in width, and the
    // severity rule means an intact bar recedes to a lower rung than a damaged
    // one — so the two must not be the same markup either.
    const std::string full = bar( { .cur = 30, .max = 30, .cells = 15 } );
    const std::string empty = bar( { .cur = 0, .max = 30, .cells = 15 } );
    CHECK( cells( glyphs( full ) ) == 15 );
    CHECK( cells( glyphs( empty ) ) == 15 );
    CHECK( full != empty );

    // Degenerate max must not divide by zero, and must still be cell-exact.
    const std::string degenerate = bar( { .cur = 5, .max = 0, .cells = 15 } );
    CHECK( cells( glyphs( degenerate ) ) == 15 );
}

TEST_CASE( "phosphor_grid_matches_the_mockup_at_1080p", "[hud_phosphor]" )
{
    using hud_phosphor::metrics_for;

    // `mockups/hud/04-terminal-phosphor.md` states the design's grid outright:
    // 192 columns x 54 rows of 10 x 20 px at 1920x1080. That column count is not
    // decorative — every internal column grid in the spec is quoted against it
    // (SOMA 34 cells, DOCK 35, LOG 92, KEYS 192), so resolving to fewer columns
    // silently truncates fields. It shipped at 160x45 of 12x24 once, and the
    // visible result was an identity field reading `Guy McClendo` and a reload
    // slot reading `NO MA` where it should have said `NO MAG`.
    const auto m = metrics_for( 1920.0f, 1080.0f );
    CHECK( m.cell_w == Catch::Approx( 10.0f ) );
    CHECK( m.cell_h == Catch::Approx( 20.0f ) );
    CHECK( m.cols == 192 );
    CHECK( m.rows == 54 );
    // Source Code Pro's advance is exactly 0.6em, which is what makes the grid
    // drift-free across a full row; at a 10dp cell that pins the font size.
    CHECK( m.font_size == Catch::Approx( 10.0f / 0.6f ) );

    // The 192-column floor must hold at larger viewports too, since the internal
    // grids do not shrink.
    for( const float w : {
             1920.0f, 2560.0f, 3840.0f
         } ) {
        INFO( "width " << w );
        CHECK( metrics_for( w, w * 9.0f / 16.0f ).cols >= 192 );
    }
}

TEST_CASE( "phosphor_bar_uses_half_block_so_bars_do_not_merge", "[hud_phosphor]" )
{
    using hud_phosphor::bar;

    // The spec picks U+2584 LOWER HALF BLOCK for the fill specifically so that
    // "adjacent bars never merge vertically" — at 50% cell ink a bar is about
    // 10.5px of a 20px cell, leaving a dark gap between rows. Filling with U+2588
    // FULL BLOCK instead made the six SOMA body-part bars render as one solid
    // amber slab in-game, which is the single most visible way to get this
    // register wrong. The trough is U+2591 LIGHT SHADE and must stay visibly
    // dotted rather than empty.
    const std::string half = "\u2584";
    const std::string shade = "\u2591";
    const std::string full = "\u2588";

    const std::string partial = bar( { .cur = 7, .max = 30, .cells = 15 } );
    CHECK( partial.find( half ) != std::string::npos );
    CHECK( partial.find( shade ) != std::string::npos );
    CHECK( partial.find( full ) == std::string::npos );

    // A full bar is all fill and no trough; an empty bar the reverse.
    const std::string filled = bar( { .cur = 30, .max = 30, .cells = 15 } );
    CHECK( filled.find( half ) != std::string::npos );
    CHECK( filled.find( shade ) == std::string::npos );

    const std::string empty = bar( { .cur = 0, .max = 30, .cells = 15 } );
    CHECK( empty.find( shade ) != std::string::npos );
    CHECK( empty.find( half ) == std::string::npos );
}

TEST_CASE( "phosphor_layout_regions_never_overlap", "[hud_phosphor]" )
{
    using hud_phosphor::layout_for;
    using hud_phosphor::metrics_for;

    // The shipping HUD mixed percentage and cell geometry, so its two bottom
    // strips overlapped each other by 6.36 dp and the hotbar ran 6.34 dp off
    // the bottom of the screen. Snapping every region to the cell grid is what
    // makes that unrepresentable — but only if the regions are actually
    // disjoint at every size the metrics can return.
    const auto overlaps = []( const hud_phosphor::cell_rect & a,
    const hud_phosphor::cell_rect & b ) -> bool {
        if( a.cols <= 0 || a.rows <= 0 || b.cols <= 0 || b.rows <= 0 )
        {
            return false; // a clamped-out region cannot overlap anything
        }
        return a.col < b.col + b.cols && b.col < a.col + a.cols &&
        a.row < b.row + b.rows && b.row < a.row + a.rows;
    };

    for( const float w : {
             640.f, 1280.f, 1920.f, 2560.f, 3840.f
         } ) {
        for( const float h : {
                 400.f, 720.f, 1080.f, 1440.f, 2160.f
             } ) {
            for( const bool right : {
                     true, false
                 } ) {
                for( const bool veh : {
                         true, false
                     } ) {
                    const auto m = metrics_for( w, h );
                    REQUIRE( m.cell_w > 0.0f );
                    REQUIRE( m.cell_h > 0.0f );
                    // Source Code Pro's advance is exactly 0.6em; the grid's
                    // zero horizontal drift depends on that identity holding.
                    REQUIRE( m.cell_w == Catch::Approx( 0.6f * m.font_size ) );

                    const auto l = layout_for( {
                        .m = m, .sidebar_right = right, .log_lines = 6, .show_vehicle = veh,
                    } );

                    const hud_phosphor::cell_rect regions[] = {
                        l.status, l.soma, l.radar, l.dock, l.log, l.keys, l.vehicle,
                    };
                    for( std::size_t i = 0; i < std::size( regions ); ++i ) {
                        // Nothing may sit outside the grid.
                        if( regions[i].cols > 0 && regions[i].rows > 0 ) {
                            INFO( "w=" << w << " h=" << h << " region=" << i );
                            CHECK( regions[i].col >= 0 );
                            CHECK( regions[i].row >= 0 );
                            CHECK( regions[i].col + regions[i].cols <= m.cols );
                            CHECK( regions[i].row + regions[i].rows <= m.rows );
                        }
                        for( std::size_t j = i + 1; j < std::size( regions ); ++j ) {
                            INFO( "w=" << w << " h=" << h
                                  << " regions " << i << " vs " << j );
                            CHECK_FALSE( overlaps( regions[i], regions[j] ) );
                        }
                    }

                    // The vehicle region exists only while driving.
                    if( !veh ) {
                        CHECK( l.vehicle.cols * l.vehicle.rows == 0 );
                    }
                }
            }
        }
    }
}

TEST_CASE( "phosphor_ink_spans_one_hue_and_never_collapses", "[hud_phosphor]" )
{
    using hud_phosphor::hex;
    using hud_phosphor::ink;

    // The register's accessibility claim is that the encoding is recoverable in
    // greyscale, which holds only if no two rungs share a luminance. Parse the
    // ladder and check the ordering directly rather than trusting the theme.
    const auto luma = []( const std::string & h ) -> float {
        const auto byte = [&]( std::size_t i ) -> float {
            const auto nib = []( char c ) -> int {
                if( c >= '0' && c <= '9' )
                {
                    return c - '0';
                }
                const char l = static_cast<char>( c | 0x20 );
                return l >= 'a' && l <= 'f' ? 10 + l - 'a' : 0;
            };
            return static_cast<float>( nib( h[i] ) * 16 + nib( h[i + 1] ) ) / 255.0f;
        };
        REQUIRE( h.size() >= 7 );
        REQUIRE( h[0] == '#' );
        // Rec. 709 relative luminance is enough to order the rungs.
        return 0.2126f * byte( 1 ) + 0.7152f * byte( 3 ) + 0.0722f * byte( 5 );
    };

    const ink ladder[] = { ink::ground, ink::dead, ink::rule, ink::label, ink::datum, ink::peak };
    float prev = -1.0f;
    for( const ink i : ladder ) {
        const float y = luma( hex( i ) );
        INFO( "rung " << static_cast<int>( i ) << " = " << hex( i ) );
        CHECK( y > prev ); // strictly ascending: no two rungs can collapse
        prev = y;
    }

    // The inverted-cell ink must be dark enough to read against the top rung.
    CHECK( luma( hex( ink::inverse ) ) < luma( hex( ink::peak ) ) );
}

TEST_CASE( "phosphor_tint_and_invert_emit_theme_classes_not_inline_colour",
           "[hud_phosphor]" )
{
    using hud_phosphor::ink;
    using hud_phosphor::invert;
    using hud_phosphor::tint;

    // Producers must emit classes so the stylesheet keeps ownership of the
    // palette and the F4 Theme tab keeps working. An inline colour here would
    // silently take the palette out of the theme's hands.
    const std::string t = tint( ink::datum, "45/60" );
    CHECK( t.find( "ph-i4" ) != std::string::npos );
    CHECK( t.find( "45/60" ) != std::string::npos );
    CHECK( t.find( "color:" ) == std::string::npos );
    CHECK( t.find( '#' ) == std::string::npos );

    const std::string v = invert( "!! CRITICAL" );
    CHECK( v.find( "ph-inv" ) != std::string::npos );
    CHECK( v.find( "color:" ) == std::string::npos );
    CHECK( v.find( '#' ) == std::string::npos );
}
