#include "hud_phosphor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "string_utils.h"
#include "ui_theme.h"

/// This translation unit is pure geometry, text and palette. It includes no
/// game header and reads no game state, which is what lets the five producer
/// slices share it without ordering constraints, and what makes every function
/// in it exercisable from a bare harness.

namespace
{

// ── Palette tables ──────────────────────────────────────────────────────────

/// Theme token per ladder rung. Order must match `hud_phosphor::ink`.
constexpr std::array<std::string_view, 7> ink_tokens = {
    "ph-0", "ph-1", "ph-2", "ph-3", "ph-4", "ph-5", "ph-k"
};

/// The ladder baked in. `ui_theme::substitute_tokens` turns an unknown token
/// into `#ff00ffff` and a warning; a HUD whose entire encoding is luminance
/// cannot degrade to magenta, so a truncated `theme.json` falls back to the
/// correct colours here instead. Values are the ones in `data/gui/theme.json`
/// and in `mockups/hud/04-terminal-phosphor.md` §Palette.
constexpr std::array<std::string_view, 7> ink_fallback = {
    "#0c0800ff", "#3a2800ff", "#7a5400ff", "#b87f00ff", "#ffb000ff", "#ffebbfff", "#120c00ff"
};

/// RCSS class per rung. `ink::inverse` is `ph-ik` rather than `ph-i6` because it
/// is not a sixth rung of the ladder — it is the glyph colour that only ever
/// appears inside a `.ph-inv` cell.
constexpr std::array<std::string_view, 7> ink_classes = {
    "ph-i0", "ph-i1", "ph-i2", "ph-i3", "ph-i4", "ph-i5", "ph-ik"
};

/// Table index for `i`, saturating at `ink::datum` so a value cast in from
/// outside the enum cannot index out of bounds.
auto rung( hud_phosphor::ink i ) -> std::size_t
{
    const auto idx = static_cast<std::size_t>( i );
    return idx < ink_tokens.size() ? idx : static_cast<std::size_t>( hud_phosphor::ink::datum );
}

/// 0..1 float channel to an 8-bit value, rounded rather than truncated so
/// `#ffb000` survives the round trip through `ui_theme`'s float storage.
auto to_byte( float v ) -> int
{
    return static_cast<int>( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) );
}

/// `#rrggbbaa` → straight RGBA 0..1. Only ever fed the `ink_fallback` literals,
/// which are always 9 bytes; anything else yields opaque black rather than
/// throwing, so a malformed literal degrades instead of crashing the HUD.
auto parse_hex8( std::string_view hex ) -> std::array<float, 4>
{
    auto out = std::array<float, 4> { 0.0f, 0.0f, 0.0f, 1.0f };
    if( hex.size() != 9 || hex[0] != '#' ) {
        return out;
    }
    const auto nib = []( char c ) -> int {
        if( c >= '0' && c <= '9' ) { return c - '0'; }
        if( c >= 'a' && c <= 'f' ) { return c - 'a' + 10; }
        if( c >= 'A' && c <= 'F' ) { return c - 'A' + 10; }
        return 0;
    };
    for( auto k = 0; k < 4; ++k ) {
        out[k] = static_cast<float>( nib( hex[1 + 2 * k] ) * 16 + nib( hex[2 + 2 * k] ) ) / 255.0f;
    }
    return out;
}

// ── Glyphs ──────────────────────────────────────────────────────────────────

constexpr std::string_view glyph_rule = "\u2500";     ///< `─` BOX DRAWINGS LIGHT HORIZONTAL
constexpr std::string_view glyph_title_l = "\u2524";  ///< `┤` LIGHT VERTICAL AND LEFT
constexpr std::string_view glyph_title_r = "\u251c";  ///< `├` LIGHT VERTICAL AND RIGHT

/// Bar fill. LOWER HALF BLOCK, **not** FULL BLOCK, and that choice is the whole
/// reason a stack of bars reads as bars at all: at 50% cell ink the mark is
/// ~10.5 px tall in a 20 px cell, so consecutive rows leave a dark gap and
/// adjacent bars never merge vertically. Filling with `█` welded the six SOMA
/// body-part bars into one solid amber slab in-game — the same data, rendered
/// as a single object. `mockups/hud/04-terminal-phosphor.md` §Register/Bars.
constexpr std::string_view glyph_fill = "\u2584";     ///< `▄` LOWER HALF BLOCK

/// Bar trough. LIGHT SHADE at `ink::rule`: 25% ink, so the unlit remainder of a
/// bar stays visibly dotted rather than going to ground and letting the bar's
/// extent read as the region's edge.
constexpr std::string_view glyph_trough = "\u2591";   ///< `░` LIGHT SHADE

/// The fill `pad` and `pad_left` add. NO-BREAK SPACE, deliberately — see `pad`.
constexpr std::string_view glyph_pad = "\u00a0";      ///< ` ` NO-BREAK SPACE

/// `glyph` repeated `n` times.
auto repeat( std::string_view glyph, std::size_t n ) -> std::string
{
    std::string out;
    out.reserve( glyph.size() * n );
    for( std::size_t i = 0; i < n; ++i ) {
        out.append( glyph );
    }
    return out;
}

// ── UTF-8 ───────────────────────────────────────────────────────────────────

/// One decoded code point. `bytes` is how far to advance: zero only at end of
/// input, otherwise always at least one, so no decode loop can stall on
/// malformed input.
struct code_point {
    char32_t cp = 0;
    std::size_t bytes = 0;
};

auto decode( std::string_view s ) -> code_point
{
    if( s.empty() ) {
        return {};
    }
    const auto b0 = static_cast<unsigned char>( s[0] );
    const auto trailing = [s]( std::size_t n ) -> bool {
        if( s.size() <= n ) {
            return false;
        }
        return std::ranges::all_of( std::views::iota( std::size_t{ 1 }, n + 1 ),
        [s]( std::size_t k ) {
            return ( static_cast<unsigned char>( s[k] ) & 0xc0 ) == 0x80;
        } );
    };
    const auto cont = [s]( std::size_t k ) -> char32_t {
        return static_cast<char32_t>( static_cast<unsigned char>( s[k] ) & 0x3f );
    };
    if( b0 < 0x80 ) {
        return { .cp = static_cast<char32_t>( b0 ), .bytes = 1 };
    }
    if( ( b0 & 0xe0 ) == 0xc0 && trailing( 1 ) ) {
        return { .cp = ( static_cast<char32_t>( b0 & 0x1f ) << 6 ) | cont( 1 ), .bytes = 2 };
    }
    if( ( b0 & 0xf0 ) == 0xe0 && trailing( 2 ) ) {
        return { .cp = ( static_cast<char32_t>( b0 & 0x0f ) << 12 ) | ( cont( 1 ) << 6 ) | cont( 2 ),
                 .bytes = 3 };
    }
    if( ( b0 & 0xf8 ) == 0xf0 && trailing( 3 ) ) {
        return { .cp = ( static_cast<char32_t>( b0 & 0x07 ) << 18 ) | ( cont( 1 ) << 12 ) |
                       ( cont( 2 ) << 6 ) | cont( 3 ),
                 .bytes = 4 };
    }
    // Malformed lead or truncated sequence: consume exactly one byte and count
    // it as one cell. Dropping it would let a field silently shrink; consuming
    // more would risk eating a valid lead byte that follows it.
    return { .cp = 0xfffd, .bytes = 1 };
}

/// Cells occupied by one code point.
///
/// Deliberately not a general `wcwidth`. The HUD's alphabet is ASCII, Latin-1,
/// box drawing U+2500-U+257F and block elements U+2580-U+259F, and every one of
/// those is exactly one cell in Source Code Pro. Anything else is also counted
/// as one cell: guessing East Asian width would only move the disagreement
/// between `pad` and the renderer, and a field padded consistently is
/// recoverable where a frame that no longer joins is not. NUL is zero cells, so
/// an embedded terminator cannot inflate a width.
///
/// U+00A0 NO-BREAK SPACE lands on one cell here like the rest of Latin-1, and
/// that is load-bearing rather than incidental: it is the fill `pad` adds, and
/// `pad` asks this function how wide its input already is in order to decide how
/// much fill to append. Counting it as zero or two cells would put every padded
/// field out by the number of pad characters in it.
auto cell_width( char32_t cp ) -> int
{
    return cp == 0 ? 0 : 1;
}

/// Where to cut `utf8` at `cells` display cells.
struct cut {
    std::size_t bytes = 0;  ///< byte offset of the boundary; always a code-point boundary
    int cells = 0;          ///< cells consumed, fewer than asked when the input is shorter
};

auto cut_at( std::string_view utf8, int cells ) -> cut
{
    cut c;
    while( c.bytes < utf8.size() && c.cells < cells ) {
        const auto d = decode( utf8.substr( c.bytes ) );
        if( d.bytes == 0 ) {
            break;
        }
        const auto w = cell_width( d.cp );
        if( c.cells + w > cells ) {
            break;
        }
        c.bytes += d.bytes;
        c.cells += w;
    }
    return c;
}

/// `utf8` split into one string per display cell. Zero-width code points are
/// dropped, so `split_cells( s ).size() == display_width( s )` always holds —
/// which is what lets `rule` treat its working buffer as one string per cell.
auto split_cells( std::string_view utf8 ) -> std::vector<std::string>
{
    std::vector<std::string> out;
    for( std::size_t at = 0; at < utf8.size(); ) {
        const auto d = decode( utf8.substr( at ) );
        if( d.bytes == 0 ) {
            break;
        }
        if( cell_width( d.cp ) > 0 ) {
            out.emplace_back( utf8.substr( at, d.bytes ) );
        }
        at += d.bytes;
    }
    return out;
}

// ── Layout constants, with their derivations ────────────────────────────────

/// 2 text rows of world/character state + 1 rule row. The rule is shared: it
/// carries SOMA's and DOCK's titles and the two `┼` junctions above their
/// verticals, which is why the status strip is a header for the columns rather
/// than a separate object, and why neither column draws a top rule of its own.
constexpr int status_row_count = 3;

/// 1 rule row (which also closes the log well from below) + 1 function-key row.
constexpr int keys_row_count = 2;

/// SOMA's internal column grid, from `mockups/hud/04-terminal-phosphor.md`
/// §Layout: 1 pad + 6 label + 1 gap + 15 bar + 1 gap + 9 value + 1 border = 34.
/// Below this either the bar or the value has to shrink, and the bar is the
/// design.
constexpr int soma_min_cols = 34;

/// SOMA's worst-case height:
///     6 body-part rows
///   + 6 inverted continuation rows  (one per part, when every part is critical)
///   + 1 `┤ POOLS ├` rule row
///   + 3 pool rows                   (stamina, focus, morale)
///   + 1 `┤ EFFECTS ├` rule row
///   + 5 effect rows
///   + 1 closing rule row
///   = 23
/// The mockup renders 18 because only one limb is critical there; 23 is the
/// height at which the panel never has to reflow.
constexpr int soma_max_rows = 23;

/// DOCK's internal column grid, §Layout:
/// 1 border + 6 pad + 11 overmap tiles x 2 cells + 6 pad = 35. The 2-cell tile
/// is what makes the overmap square at a 1:2 cell aspect.
constexpr int dock_min_cols = 35;

/// The radar viewport's height. 34 interior cells wide at a 1:2 cell aspect makes
/// 17 rows the square, which is what keeps the dot pitch equal on both axes at the
/// authored 192-column grid.
constexpr int radar_max_rows = 17;

/// DOCK's height, now that the 11-row text overmap chunk it used to carry has been
/// replaced by the radar region above it:
///    1 mission-marker caption row
///   + 1 `┤ TARGET ├` rule row
///   + 3 target rows                 (name and range, HP bar, status)
///   + 1 `┤ ARMS ├` rule row
///   + 4 arms rows                   (wielded, damage, sidearm, ammo)
///   + 1 closing rule row
///   = 11
constexpr int dock_max_rows = 11;

/// Vehicle panel height, sized to what the vehicle producer emits:
/// 1 `┤ VEHICLE ├` rule row + 1 name and heading + 1 speed and cruise
/// + 1 engine flags + 3 fuel gauges + 1 closing rule row = 8. Three gauges is
/// the practical cap; a vehicle with more tanks shows its three largest rather
/// than growing the panel into the play area.
constexpr int veh_max_rows = 8;

/// The log's internal column grid, §Layout:
/// 1 mark + 5 time + 1 gap + 1 gutter + 1 gap + 2 glyph + 1 gap + 79 text
/// + 1 border = 92. The 79-cell text field is a VT220 line minus its border,
/// and it is the reason no message wraps.
constexpr int log_min_cols = 92;

/// One message plus its own titled rule row. Below this the well shows a frame
/// and no information, so the columns yield instead (see `layout_for`).
constexpr int log_min_rows = 2;

/// One KEYS slot: 3 bracket + 1 key + 1 gap + 16 label = 21 cells, §Layout.
constexpr int keys_slot_cols = 3 + 1 + 1 + 16;

/// The DOS F-key line carries nine of them.
constexpr int keys_slot_count = 9;

/// The KEYS row's internal column grid, §Layout:
/// 1 pad + 9 slots x 21 + 2 = 192. This is the only internal grid that spans
/// the whole screen, so it — not the viewport — is what fixes the grid width.
constexpr int keys_row_cols = 1 + keys_slot_count * keys_slot_cols + 2;

/// The narrowest grid `layout_for` can populate without the chrome eating the
/// play area: `soma_min_cols + dock_min_cols` = 69 cells of column, plus a
/// 51-cell clear span so a centred player still has 25 cells of visible map to
/// either side before a panel edge. 34 + 35 + 51 = 120.
constexpr int grid_min_cols = soma_min_cols + dock_min_cols + 51;

/// The column count `metrics_for` aims for.
///
/// **The target is a column COUNT, not a cell size, because the design is
/// authored in cells.** Every internal grid in §Layout — SOMA's 34, DOCK's 35,
/// the log's 92, KEYS' 192 — is quoted against a 192-column screen, and those
/// are the numbers the producers emit. Pin the cell size instead and the grid
/// width drifts with the window: a 12 dp cell on a 1920 dp context yields only
/// 160 columns, and at 160 the identity field lost its last character
/// (`Guy McClendo`) while a KEYS slot overran into its neighbour
/// (`RELOAD NO MA`). Pin the count and the cell falls out of it — 1920 / 192 is
/// exactly the mockup's 10 dp cell — while a wider context takes the next
/// larger cell that still clears 192 instead of merely rendering the same
/// layout smaller.
constexpr int grid_target_cols = keys_row_cols;

// The target has to be wide enough for everything laid out inside it; these
// hold by construction and are asserted so a change to one grid cannot quietly
// outgrow the screen it is quoted against.
static_assert( grid_target_cols == 192, "the mockup's grid is 192 columns" );
static_assert( grid_target_cols >= grid_min_cols );
static_assert( grid_target_cols >= log_min_cols );
static_assert( grid_target_cols >= soma_min_cols + dock_min_cols );

/// The shortest grid in which the left stack still closes:
/// 3 status + 23 SOMA + 2 log floor + 2 keys = 30. The right stack wants 35 when
/// the vehicle panel is up; that case is resolved by clamping in `layout_for`
/// rather than by raising this floor, because a driving player must not lose
/// their body panel to a smaller font.
constexpr int grid_min_rows = status_row_count + soma_max_rows + log_min_rows + keys_row_count;

/// A candidate cell. `cell_w == 0.6 * font_size` exactly — Source Code Pro's
/// advance is 0.6em — which is why a full row of this grid has zero horizontal
/// drift where a Consolas mock-up drifts 0.19 dp per 192 cells.
struct cell_candidate {
    float font_size = 0.0f;
    float cell_w = 0.0f;
};

/// Legibility-ordered, largest first. `cell_h` is always `2 * cell_w`: the 1:2
/// aspect is the mockup's 10 x 20 cell, and it is what makes `│` stems abut
/// between rows so `┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼` close into a frame. Any other aspect
/// leaves a visible gap in every vertical.
constexpr std::array<cell_candidate, 4> cell_candidates = { {
        { .font_size = 20.0f, .cell_w = 12.0f },         // 0.6 * 20      = 12
        { .font_size = 55.0f / 3.0f, .cell_w = 11.0f },  // 0.6 * 18.3333 = 11
        { .font_size = 50.0f / 3.0f, .cell_w = 10.0f },  // 0.6 * 16.6667 = 10, the mockup's cell
        { .font_size = 15.0f, .cell_w = 9.0f },          // 0.6 * 15      =  9
    }
};

/// The whole-cell grid `c` yields in a context of `w` x `h` dp.
auto grid_of( const cell_candidate &c, float w, float h ) -> hud_phosphor::metrics
{
    const auto cell_h = c.cell_w * 2.0f;
    return hud_phosphor::metrics{
        .cell_w = c.cell_w,
        .cell_h = cell_h,
        .font_size = c.font_size,
        .cols = std::max( 0, static_cast<int>( std::floor( w / c.cell_w ) ) ),
        .rows = std::max( 0, static_cast<int>( std::floor( h / cell_h ) ) ),
    };
}

// ── Rect helpers ────────────────────────────────────────────────────────────

/// One past the last row of `r`.
auto bottom( const hud_phosphor::cell_rect &r ) -> int
{
    return r.row + r.rows;
}

/// Do `a` and `b` share a column? An empty rect shares nothing.
auto spans_overlap( const hud_phosphor::cell_rect &a, const hud_phosphor::cell_rect &b ) -> bool
{
    return a.cols > 0 && b.cols > 0 && a.col < b.col + b.cols && b.col < a.col + a.cols;
}

/// Reflect `r` about the grid's vertical centre. Half-open spans make this
/// exact and involutive: `[col, col + cols)` maps to
/// `[total - col - cols, total - col)`, so mirroring twice is the identity and
/// no cell is gained or lost at the edge.
void mirror( hud_phosphor::cell_rect &r, int total_cols )
{
    if( r.cols > 0 ) {
        r.col = total_cols - r.col - r.cols;
    }
}

} // namespace

// ── Palette ─────────────────────────────────────────────────────────────────

auto hud_phosphor::hex( ink i ) -> std::string
{
    const auto r = rung( i );
    float rgba[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if( !ui_theme::get_rcss_rgba( std::string( ink_tokens[r] ), rgba ) ) {
        return std::string( ink_fallback[r] );
    }
    return std::format( "#{:02x}{:02x}{:02x}{:02x}", to_byte( rgba[0] ), to_byte( rgba[1] ),
                        to_byte( rgba[2] ), to_byte( rgba[3] ) );
}

auto hud_phosphor::rgba( ink i ) -> std::array<float, 4>
{
    const auto r = rung( i );
    auto out = std::array<float, 4> {};
    if( ui_theme::get_rcss_rgba( std::string( ink_tokens[r] ), out.data() ) ) {
        return out;
    }
    return parse_hex8( ink_fallback[r] );
}

// ── Geometry ────────────────────────────────────────────────────────────────

auto hud_phosphor::metrics_for( float ctx_w_dp, float ctx_h_dp ) -> metrics
{
    const auto reaches = [ctx_w_dp, ctx_h_dp]( int want_cols ) {
        return [ctx_w_dp, ctx_h_dp, want_cols]( const cell_candidate & c ) {
            const auto g = grid_of( c, ctx_w_dp, ctx_h_dp );
            return g.cols >= want_cols && g.rows >= grid_min_rows;
        };
    };
    // Candidates are largest-first, so the first match is the largest cell that
    // still reaches the target. Aiming at the COUNT rather than at the biggest
    // legible cell is the whole point: at 1920 dp a 12 dp cell gives 160
    // columns and an 11 dp cell 174, so only 10 dp reproduces the mockup, while
    // at 2560 dp the 12 dp cell clears 192 on its own and is taken instead.
    auto found = std::ranges::find_if( cell_candidates, reaches( grid_target_cols ) );
    if( found == cell_candidates.end() ) {
        // Too small for the authored grid — a window this narrow cannot show
        // the design as drawn, so fall back to the widest layout that fits and
        // let the producers truncate rather than showing nothing.
        found = std::ranges::find_if( cell_candidates, reaches( grid_min_cols ) );
    }
    // Clamping to the smallest candidate rather than returning a zero cell is
    // what lets callers lay out unconditionally: `layout_for` then resolves the
    // resulting overlaps deterministically instead of dividing by zero.
    const auto &chosen = found != cell_candidates.end() ? *found : cell_candidates.back();
    return grid_of( chosen, ctx_w_dp, ctx_h_dp );
}

auto hud_phosphor::layout_for( const layout_options &o ) -> layout
{
    layout l;
    l.m = o.m;
    const auto cols = std::max( 0, o.m.cols );
    const auto rows = std::max( 0, o.m.rows );

    // The two opaque strips claim their rows first: they are the frame the rest
    // of the HUD hangs off, and neither is elastic.
    const auto status_rows = std::min( status_row_count, rows );
    l.status = { .col = 0, .row = 0, .cols = cols, .rows = status_rows };
    const auto keys_rows = std::clamp( rows - status_rows, 0, keys_row_count );
    l.keys = { .col = 0, .row = rows - keys_rows, .cols = cols, .rows = keys_rows };

    const auto body_top = status_rows;
    const auto body_bot = rows - keys_rows;
    const auto body_rows = std::max( 0, body_bot - body_top );

    // Column widths: about 18% of the grid, floored at the internal grids the
    // producers draw into. Flooring the percentage rather than rounding it
    // reproduces the mockup exactly at 192 cells (192 * 18 / 100 = 34).
    const auto pct18 = cols * 18 / 100;
    // Defensive only: on a grid narrower than `metrics_for` will ever return,
    // the two columns would meet. SOMA wins — the body panel is the one you
    // cannot play without — and DOCK takes whatever is left beside it.
    const auto soma_cols = std::min( std::max( soma_min_cols, pct18 ), cols );
    const auto dock_cols = std::clamp( std::max( dock_min_cols, pct18 ), 0, cols - soma_cols );

    // Built in the right-sidebar orientation and then mirrored as one operation,
    // so there is exactly one set of geometry to reason about.
    l.soma = { .col = 0, .row = body_top, .cols = soma_cols,
               .rows = std::min( soma_max_rows, body_rows ) };
    // Right column, top to bottom: RADAR, DOCK, VEHICLE. The radar is first
    // because it is what the shared status rule above titles.
    l.radar = { .col = cols - dock_cols, .row = body_top, .cols = dock_cols,
                .rows = std::min( radar_max_rows, body_rows ) };
    const auto dock_top = bottom( l.radar );
    l.dock = { .col = cols - dock_cols, .row = dock_top, .cols = dock_cols,
               .rows = std::clamp( body_bot - dock_top, 0, dock_max_rows ) };
    if( o.show_vehicle ) {
        const auto veh_top = bottom( l.dock );
        l.vehicle = { .col = l.dock.col, .row = veh_top, .cols = dock_cols,
                      .rows = std::clamp( body_bot - veh_top, 0, veh_max_rows ) };
    }
    if( !o.sidebar_right ) {
        mirror( l.soma, cols );
        mirror( l.radar, cols );
        mirror( l.dock, cols );
        mirror( l.vehicle, cols );
    }

    // The log is bottom-left, sized to its line count plus its own titled rule
    // row. That sizing is where this design's occlusion saving comes from: the
    // shipping well was 752 dp holding 267 dp of message. `log_lines` is the
    // MESSAGE count; the rule row is added here, so a caller passes what it has
    // and no region has to know the producer's row structure. Zero messages is
    // no region at all: a well showing a title and nothing else is pure
    // occlusion.
    const auto log_cols = std::min( cols, std::max( log_min_cols, cols * 48 / 100 ) );
    const auto log_want = std::min( o.log_lines > 0 ? o.log_lines + 1 : 0, body_rows );
    cell_rect log = { .col = 0, .row = body_top, .cols = log_cols, .rows = 0 };

    // Disjointness, resolved in a fixed order so the same grid always produces
    // the same layout.
    //
    // Stage 1 — the log yields rows to every region it sits beneath. It goes
    // first because it is the only elastic region: three fewer visible messages
    // is a graceful degradation, half a body-part panel is not.
    auto log_top = body_bot - log_want;
    for( const cell_rect *r : { &l.soma, &l.radar, &l.dock, &l.vehicle } ) {
        if( spans_overlap( log, *r ) ) {
            log_top = std::max( log_top, bottom( *r ) );
        }
    }
    log.rows = std::max( 0, body_bot - log_top );

    // Stage 2 — below one message plus its rule a log is not a log, so now the
    // columns yield: the vehicle panel first (supplementary), then DOCK, then
    // SOMA last. Only reachable on a grid shorter than `grid_min_rows`, or on a
    // short grid while driving, where the right stack wants 3 + 22 + 8 + 2 = 35.
    // A log with no messages wants nothing and takes nothing.
    if( log_want >= log_min_rows && log.rows < log_min_rows && body_rows >= log_min_rows ) {
        log.rows = log_min_rows;
        log_top = body_bot - log.rows;
        for( cell_rect *r : { &l.vehicle, &l.dock, &l.radar, &l.soma } ) {
            if( spans_overlap( log, *r ) && bottom( *r ) > log_top ) {
                r->rows = std::max( 0, log_top - r->row );
            }
        }
    }
    log.row = body_bot - log.rows;
    l.log = log;

    // Anything clamped out of existence is reported as a zero rect, so a caller
    // tests one field instead of two and never positions an empty region.
    for( cell_rect *r : { &l.status, &l.soma, &l.radar, &l.dock, &l.log, &l.keys, &l.vehicle } ) {
        if( r->rows <= 0 || r->cols <= 0 ) {
            *r = cell_rect{};
        }
    }
    return l;
}

auto hud_phosphor::to_dp( const metrics &m, const cell_rect &c ) -> rect
{
    // Exact multiplication, no rounding: the cell size was chosen so the grid
    // tiles the context, so snapping here could only reintroduce the straddle
    // this whole scheme exists to remove.
    return rect{
        .x = m.cell_w * static_cast<float>( c.col ),
        .y = m.cell_h * static_cast<float>( c.row ),
        .w = m.cell_w * static_cast<float>( c.cols ),
        .h = m.cell_h * static_cast<float>( c.rows ),
    };
}

// ── Cell-grid text ──────────────────────────────────────────────────────────

auto hud_phosphor::display_width( std::string_view utf8 ) -> int
{
    int w = 0;
    for( std::size_t at = 0; at < utf8.size(); ) {
        const auto d = decode( utf8.substr( at ) );
        if( d.bytes == 0 ) {
            break;
        }
        at += d.bytes;
        w += cell_width( d.cp );
    }
    return w;
}

/// Pads with U+00A0 NO-BREAK SPACE rather than with a plain space, and that is
/// NOT a mistake to be tidied away.
///
/// Producers build a row by concatenating `tint` spans, and that markup reaches
/// the document through `data-rml`, which parses it against the element's
/// CURRENT computed style. RmlUi strips leading and trailing whitespace from an
/// inline run at PARSE time, so a `white-space: pre` declared on the very
/// element being created cannot save the padding — it is already gone by the
/// time the property applies. Padding that landed on a `<span>` boundary was
/// therefore deleted outright, which welded every label to its value in-game:
/// `DAY16Spring12:59:46`, `STR13DEX22INT9PER20`, `MISSION MARKERNONE`. U+00A0 is
/// not whitespace for that purpose and survives unconditionally, which is the
/// whole reason padding goes through one choke point instead of being written
/// out at each call site. Restoring a plain space here silently un-aligns the
/// entire grid again, and it will look correct in the source while doing it.
///
/// Source Code Pro carries U+00A0, and `display_width` counts it as one cell.
auto hud_phosphor::pad( std::string_view utf8, int cols ) -> std::string
{
    if( cols <= 0 ) {
        return {};
    }
    const auto c = cut_at( utf8, cols );
    std::string out( utf8.substr( 0, c.bytes ) );
    out.append( repeat( glyph_pad, static_cast<std::size_t>( cols - c.cells ) ) );
    return out;
}

/// Right-align within `cols`, truncating from the left if needed. Pads with the
/// same non-breaking fill as `pad`, and for the same reason.
auto hud_phosphor::pad_left( std::string_view utf8, int cols ) -> std::string
{
    if( cols <= 0 ) {
        return {};
    }
    const auto w = display_width( utf8 );
    if( w > cols ) {
        // Truncate from the left: in a right-aligned field the tail carries the
        // value. The cut still lands on a code-point boundary, because `cut_at`
        // only ever advances by whole code points.
        return std::string( utf8.substr( cut_at( utf8, w - cols ).bytes ) );
    }
    std::string out = repeat( glyph_pad, static_cast<std::size_t>( cols - w ) );
    out.append( utf8 );
    return out;
}

// ── Frames ──────────────────────────────────────────────────────────────────

auto hud_phosphor::rule( const rule_options &o ) -> std::string
{
    if( o.cols <= 0 ) {
        return {};
    }
    const auto width = static_cast<std::size_t>( o.cols );
    std::vector<std::string> cells( width, std::string( glyph_rule ) );

    // Every overlay is normalised to exactly one cell before it lands, so the
    // finished rule is `o.cols` display cells by construction — a caller that
    // passes a two-glyph corner cannot shift everything to its right.
    const auto put = [&cells]( std::size_t at, std::string_view s ) {
        cells[at] = pad( s, 1 );
    };
    if( !o.left.empty() ) {
        put( 0, o.left );
    }
    if( !o.right.empty() ) {
        put( width - 1, o.right );
    }
    for( const int c : o.crossings ) {
        // Out of range is dropped, not clamped: a junction glyph at the wrong
        // column claims a vertical that is not underneath it.
        if( c >= 0 && c < o.cols ) {
            put( static_cast<std::size_t>( c ), o.crossing_glyph );
        }
    }
    for( const rule_title &t : o.titles ) {
        // `┤ TEXT ├` — a DOS-style interrupting title, which is why a section
        // header in this register costs zero rows.
        const auto parts = split_cells( std::format( "{} {} {}", glyph_title_l,
                                        to_upper_case( t.text ), glyph_title_r ) );
        const auto title_cols = static_cast<int>( parts.size() );
        if( t.col < 0 || t.col + title_cols > o.cols ) {
            // Dropped rather than clamped: a title slid left to make it fit
            // would name the wrong section, and a rule with no title is a rule.
            continue;
        }
        std::ranges::copy( parts, cells.begin() + t.col );
    }

    std::string out;
    out.reserve( width * glyph_rule.size() );
    for( const std::string &c : cells ) {
        out.append( c );
    }
    return out;
}

// ── Bars ────────────────────────────────────────────────────────────────────

auto hud_phosphor::bar( const bar_options &o ) -> std::string
{
    if( o.cells <= 0 ) {
        return {};
    }
    const auto cells = static_cast<std::size_t>( o.cells );
    // A non-positive maximum means "no scale", which draws as an empty trough
    // rather than dividing by zero or reading as zero-and-dying.
    const auto frac = o.max > 0
                      ? std::clamp( static_cast<float>( o.cur ) / static_cast<float>( o.max ), 0.0f, 1.0f )
                      : 0.0f;
    // Whole cells only. `▄` divides the cell VERTICALLY, and there is no
    // horizontal eighth-block equivalent of a half-height glyph, so the
    // sub-cell remainder the full-block fill used to render is rounded away.
    // That is the correct trade and the spec makes it: quantisation is +-1/2
    // cell, and the exact figure is printed beside every bar, so the number
    // carries the truth while the bar carries the gestalt — and the gestalt is
    // only legible because half-height marks leave a gap between rows.
    const auto rounded = static_cast<int>( std::lround( frac * static_cast<float>( o.cells ) ) );
    const auto lit = std::min( static_cast<std::size_t>( std::max( 0, rounded ) ), cells );

    const std::string fill = repeat( glyph_fill, lit );
    const std::string trough = repeat( glyph_trough, cells - lit );

    // The severity rule: a part at full health recedes to `ink::label`, so the
    // eye is drawn to damage rather than to a wall of identical healthy bars.
    const bool intact = o.intact_recedes && o.cur >= o.max;
    std::string out;
    if( !fill.empty() ) {
        out += tint( intact ? ink::label : ink::datum, fill );
    }
    if( !trough.empty() ) {
        out += tint( ink::rule, trough );
    }
    return out;
}

// ── Emphasis ────────────────────────────────────────────────────────────────

/// `content` MUST already be RML-escaped: these two wrap, they never escape.
/// Producers escape game-supplied text (item names, log messages) where they
/// read it, because that is the only place that knows whether a `<` came from
/// the player's item name or from a span this file emitted. Spans nest safely —
/// a tinted run inside `invert` takes `ph-k` from the stylesheet's
/// `.ph-inv .ph-iN` rule, so a bar can sit inside an inverted row unchanged.
auto hud_phosphor::tint( ink i, std::string_view content ) -> std::string
{
    return std::format( R"(<span class="{}">{}</span>)", ink_classes[rung( i )], content );
}

auto hud_phosphor::invert( std::string_view content ) -> std::string
{
    return std::format( R"(<span class="ph-inv">{}</span>)", content );
}

// ── Severity ────────────────────────────────────────────────────────────────

/// Replaces the integer predicate at `panels.cpp:795-796`, which was
/// `pct = o.cur * 100 / o.max; crit = pct < 25`. At 8/30 that truncates to 26,
/// so a limb at 26.7% health that was both bleeding and bitten never once
/// rendered as critical — and `hud_anim::is_critical` missed identically, at
/// 0.267 against 0.25. Here the ratio is float, the threshold is a third, and
/// bleeding or bitten is critical at any health, because those are the states
/// that kill you between turns.
auto hud_phosphor::is_critical( const crit_options &o ) -> bool
{
    // Acute first: an actively bleeding or bitten limb is critical at ANY health,
    // including a broken one, because those are the states that tick damage
    // between turns.
    if( o.bleeding || o.bitten ) {
        return true;
    }
    // A broken limb is NOT critical on its own. This has to be an explicit early
    // return rather than a comment saying `o.broken` is "deliberately not
    // consulted": `is_limb_broken` means hp == 0, so a broken limb ALWAYS trips
    // the ratio test below, and leaving it to fall through made every broken limb
    // permanently inverted — which spends the register's single loudest signal on
    // a long-term condition the player cannot act on this turn, and makes the
    // non-inverted "! BROKEN / MENDING" row in hud_soma unreachable.
    if( o.broken ) {
        return false;
    }
    return o.max > 0 && static_cast<float>( o.cur ) / static_cast<float>( o.max ) < 1.0f / 3.0f;
}
