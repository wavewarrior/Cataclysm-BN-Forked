#include "hud_runic.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include "rml_util.h"
#include "ui_theme.h"

/// This translation unit is pure geometry, markup and palette. It includes no
/// game header and reads no game state, which is what lets the producer slices
/// share it without ordering constraints, and what makes every function in it
/// exercisable from a bare harness.

namespace
{

// ── Palette tables ──────────────────────────────────────────────────────────

/// Theme token per ladder rung. Order must match `hud_runic::ink`.
constexpr std::array<std::string_view, 6> ink_tokens = {
    "bg-hard", "bg2", "bg4", "fg4", "fg", "fg0"
};

/// The ladder baked in. `ui_theme::substitute_tokens` turns an unknown token
/// into `#ff00ffff` and a warning; a HUD whose entire encoding is luminance
/// cannot degrade to magenta, so a truncated `theme.json` falls back to the
/// correct colours here instead. Values are gruvbox-dark's, the same ones the
/// character creator reads.
constexpr std::array<std::string_view, 6> ink_fallback = {
    "#1d2021ff", "#504945ff", "#7c6f64ff", "#a89984ff", "#ebdbb2ff", "#fbf1c7ff"
};

/// RCSS class per rung, declared in `data/gui/sidebar_hud.rcss`.
constexpr std::array<std::string_view, 6> ink_classes = {
    "hud-i0", "hud-i1", "hud-i2", "hud-i3", "hud-i4", "hud-i5"
};

/// Table index for `i`, saturating at `ink::datum` so a value cast in from
/// outside the enum cannot index out of bounds.
auto rung( hud_runic::ink i ) -> std::size_t
{
    const auto idx = static_cast<std::size_t>( i );
    return idx < ink_tokens.size() ? idx : static_cast<std::size_t>( hud_runic::ink::datum );
}

/// 0..1 float channel to an 8-bit value, rounded rather than truncated so a
/// literal survives the round trip through `ui_theme`'s float storage.
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

/// `rml_escape` over a view, since every primitive takes views and the escaper
/// takes a string.
auto esc( std::string_view s ) -> std::string
{
    return rml_escape( std::string( s ) );
}

/// Shorthand for the public `hud_runic::ink_class`, used by every primitive
/// below.
auto cls( hud_runic::ink i ) -> std::string_view
{
    return hud_runic::ink_class( i );
}

// ── Layout constants, with their derivations ────────────────────────────────
//
// `status_h` and `keys_h` are literals, not formulas: they are the measured sum
// of the RCSS box each strip actually contains, and deriving them from `row_h`
// would silently disagree with the stylesheet the first time a padding value
// moves. If either strip's RCSS padding changes, change the literal here in the
// same commit — `sidebar_hud_top_rows()` carves the terrain viewport out of
// exactly this number, so a stale value leaves a strip painting over live map.

/// 4 pad + 20 meta row + 11 `.nc-rule` block + 20 meta row + 4 pad + 1 slack.
constexpr float status_h = 60.0f;

/// 4 pad + 20 legend row + 4 pad.
constexpr float keys_h = 28.0f;

/// SOMA's two worst cases, in dp of content, rounded up to whole rows.
///
/// Expanded — `max( 6 limb rows 120 + 6 chip rows 108, figure 78 )` = 228, plus
/// POOLS (rule 11 + subhead 20 + 3 rows 60 = 91) and EFFECTS (rule 11 + subhead
/// 20 + 8 tally rows at 16 dp = 159) = 478, which fits `panel_h( 26 )`'s 520 dp
/// body. **The figure sits BESIDE the limb rows precisely so this number does not
/// move**: stacked it would cost their sum and overflow.
///
/// Collapsed — 3 summary rows = 60, plus the same 91 and 159 = 310, fitting
/// `panel_h( 16 )`'s 320. POOLS and EFFECTS are NOT part of the card and render
/// in both states; only the limb section collapses.
///
/// Overflowing either is not a graceful degradation. `.hud-body`'s scrollbar is
/// decorative here: with only passive documents open, `rmlui_layer.cpp:702-705`
/// hands the wheel to RmlUi and then returns `any_interactive_open()` — false —
/// so the event falls through to the map zoom and the player cannot scroll a HUD
/// panel at all. Content past the bottom is not deferred, it is gone.
constexpr float soma_max_expanded = hud_runic::panel_h( 26 );
constexpr float soma_max_collapsed = hud_runic::panel_h( 16 );

/// A fixed 300 dp dot field under the head. Letterboxed horizontally on a wide
/// column rather than square, because a square field at `col_w` 460 would push
/// DOCK off a 720p-class viewport.
constexpr float radar_max = hud_runic::head_h + 300.0f + hud_runic::chrome_h;

/// DOCK: mission fact + TARGET subhead + name/sub/HP/status + ARMS subhead +
/// wield and its sub + sidearm and its sub = 12 rows.
constexpr float dock_max = hud_runic::panel_h( 12 );

/// VEHICLE: name + heading sub + speed + cruise sub + flag chips + 3 fuel
/// gauges = 8 rows. A vehicle with more tanks scrolls; the panel does not grow
/// into the play area.
constexpr float veh_max = hud_runic::panel_h( 8 );

/// Below one message a log well is a head and nothing else, so the columns
/// yield instead (see `layout_for`).
constexpr float log_min = hud_runic::panel_h( 1 );

// ── Rect helpers ────────────────────────────────────────────────────────────

auto bottom( const hud_runic::rect &r ) -> float
{
    return r.y + r.h;
}

/// Do `a` and `b` share a column of the screen? An empty rect shares nothing.
auto spans_overlap( const hud_runic::rect &a, const hud_runic::rect &b ) -> bool
{
    return a.w > 0.0f && b.w > 0.0f && a.x < b.x + b.w && b.x < a.x + a.w;
}

/// The tallest whole-row panel that fits in `avail` dp without exceeding `cap`.
///
/// Flooring to whole rows is what keeps a panel from showing a half-clipped last
/// row: `.hud-row` is a fixed 20 dp and `.hud-body` clips, so a region 12 dp
/// taller than a whole number of rows spends those 12 dp drawing the top of a
/// row the player cannot read. Zero when even a head-only panel does not fit.
auto floor_rows( float avail, float cap ) -> float
{
    const auto h = std::min( avail, cap );
    if( h < hud_runic::panel_h( 0 ) ) {
        return 0.0f;
    }
    const auto rows = std::floor( ( h - hud_runic::head_h - hud_runic::chrome_h ) /
                                  hud_runic::row_h );
    return hud_runic::panel_h( static_cast<int>( rows ) );
}

/// Reflect `r` about the context's vertical centre. Exact and involutive, so
/// mirroring twice is the identity and no pixel is gained or lost at the edge.
void mirror( hud_runic::rect &r, float ctx_w )
{
    if( r.w > 0.0f ) {
        r.x = ctx_w - r.x - r.w;
    }
}

} // namespace

// ── Palette ─────────────────────────────────────────────────────────────────

auto hud_runic::hex( ink i ) -> std::string
{
    const auto r = rung( i );
    float rgba[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if( !ui_theme::get_rcss_rgba( std::string( ink_tokens[r] ), rgba ) ) {
        return std::string( ink_fallback[r] );
    }
    return std::format( "#{:02x}{:02x}{:02x}{:02x}", to_byte( rgba[0] ), to_byte( rgba[1] ),
                        to_byte( rgba[2] ), to_byte( rgba[3] ) );
}

auto hud_runic::rgba( ink i ) -> std::array<float, 4>
{
    const auto r = rung( i );
    auto out = std::array<float, 4> {};
    if( ui_theme::get_rcss_rgba( std::string( ink_tokens[r] ), out.data() ) ) {
        return out;
    }
    return parse_hex8( ink_fallback[r] );
}

auto hud_runic::ink_class( ink i ) -> std::string_view
{
    return ink_classes[rung( i )];
}

/// `content` MUST already be RML-escaped: this wraps, it never escapes.
/// Producers escape game-supplied text where they read it, because that is the
/// only place that knows whether a `<` came from the player's item name or from
/// a span this file emitted.
auto hud_runic::tint( ink i, std::string_view content ) -> std::string
{
    return std::format( R"(<span class="{}">{}</span>)", cls( i ), content );
}

// ── Geometry ────────────────────────────────────────────────────────────────

auto hud_runic::layout_for( const layout_options &o ) -> layout
{
    layout l;
    l.ctx_w = std::max( 0.0f, o.ctx_w_dp );
    l.ctx_h = std::max( 0.0f, o.ctx_h_dp );
    const auto ctx_w = l.ctx_w;
    const auto ctx_h = l.ctx_h;

    // The two opaque strips claim their band first: they are the frame the rest
    // of the HUD hangs off, and neither is elastic. `keys` is anchored to
    // `ctx_h` itself rather than to a row count, which is what removes the
    // residual sliver the cell grid had to round away — and what lets the
    // terrain carve be exact.
    l.status = { .x = 0.0f, .y = 0.0f, .w = ctx_w, .h = std::min( status_h, ctx_h ) };
    const auto keys_have = std::clamp( ctx_h - l.status.h, 0.0f, keys_h );
    l.keys = { .x = 0.0f, .y = ctx_h - keys_have, .w = ctx_w, .h = keys_have };

    const auto body_top = l.status.h;
    const auto body_bot = l.keys.y;
    const auto body_h = std::max( 0.0f, body_bot - body_top );

    // Column width: a fifth of the viewport, bounded so a 4K screen does not
    // hand a body-part list half the map and a 720p one does not squeeze the
    // value column out. Halved defensively so the two columns can never meet on
    // a context narrower than the bounds assume.
    const auto col_w = std::min( std::clamp( ctx_w * 0.20f, 300.0f, 460.0f ), ctx_w * 0.5f );

    // Built in the right-sidebar orientation and then mirrored as one
    // operation, so there is exactly one set of geometry to reason about.
    const auto soma_max = o.soma_expanded ? soma_max_expanded : soma_max_collapsed;
    l.soma = { .x = 0.0f, .y = body_top, .w = col_w, .h = floor_rows( body_h, soma_max ) };
    // Right column, top to bottom: RADAR, DOCK, VEHICLE.
    l.radar = { .x = ctx_w - col_w, .y = body_top, .w = col_w,
                .h = floor_rows( body_h, radar_max )
              };
    const auto dock_top = bottom( l.radar );
    l.dock = { .x = ctx_w - col_w, .y = dock_top, .w = col_w,
               .h = floor_rows( body_bot - dock_top, dock_max )
             };
    if( o.show_vehicle ) {
        const auto veh_top = bottom( l.dock );
        l.vehicle = { .x = ctx_w - col_w, .y = veh_top, .w = col_w,
                      .h = floor_rows( body_bot - veh_top, veh_max )
                    };
    }
    if( !o.sidebar_right ) {
        mirror( l.soma, ctx_w );
        mirror( l.radar, ctx_w );
        mirror( l.dock, ctx_w );
        mirror( l.vehicle, ctx_w );
    }

    // The log is bottom-left, sized to its line count. That sizing is where this
    // design's occlusion saving comes from: the well it replaced was 752 dp
    // holding 267 dp of message. Zero messages is no region at all — a well
    // showing a head and nothing else is pure occlusion.
    const auto log_w = std::min( std::clamp( ctx_w * 0.48f, 380.0f, 900.0f ), ctx_w );
    const auto log_want = o.log_lines > 0
                          ? std::min( panel_h( o.log_lines ), body_h )
                          : 0.0f;
    rect log = { .x = 0.0f, .y = body_top, .w = log_w, .h = 0.0f };

    // Disjointness, resolved in a fixed order so the same context always
    // produces the same layout.
    //
    // Stage 1 — the log yields to every region it sits beneath. It goes first
    // because it is the only elastic region: three fewer visible messages is a
    // graceful degradation, half a body-part panel is not.
    auto log_top = body_bot - log_want;
    for( const rect *r : { &l.soma, &l.radar, &l.dock, &l.vehicle } ) {
        if( spans_overlap( log, *r ) ) {
            log_top = std::max( log_top, bottom( *r ) );
        }
    }
    log.h = std::max( 0.0f, body_bot - log_top );

    // Stage 2 — below one message a log is not a log, so now the columns yield:
    // the vehicle panel first (supplementary), then DOCK, then RADAR, then SOMA
    // last. Each is re-floored to whole rows so a yielding column does not end
    // up showing a sliced final row.
    if( log_want >= log_min && log.h < log_min && body_h >= log_min ) {
        log.h = log_min;
        log_top = body_bot - log.h;
        for( rect *r : { &l.vehicle, &l.dock, &l.radar, &l.soma } ) {
            if( spans_overlap( log, *r ) && bottom( *r ) > log_top ) {
                r->h = floor_rows( log_top - r->y, r->h );
            }
        }
    }
    log.y = body_bot - log.h;
    l.log = log;

    // Anything clamped out of existence is reported as a zero rect, so a caller
    // tests one field instead of two and never positions an empty region.
    for( rect *r : { &l.status, &l.soma, &l.radar, &l.dock, &l.log, &l.keys, &l.vehicle } ) {
        if( r->w <= 0.0f || r->h <= 0.0f ) {
            *r = rect{};
        }
    }
    return l;
}

// ── Markup primitives ───────────────────────────────────────────────────────

auto hud_runic::row( std::string_view classes, std::string_view id, std::string_view inner )
-> std::string
{
    if( id.empty() ) {
    return std::format( R"(<div class="{}">{}</div>)", classes, inner );
    }
    return std::format( R"(<div class="{}" id="{}">{}</div>)", classes, id, inner );
}

auto hud_runic::rule_div() -> std::string
{
    return R"(<div class="nc-rule"></div>)";
}

auto hud_runic::subhead( std::string_view title ) -> std::string
{
    return rule_div() +
    row( "hud-row hud-subhead", {},
    std::format( R"(<span class="hud-text">{}</span>)", esc( title ) ) );
}

auto hud_runic::pips( const pip_options &o ) -> std::string
{
    const auto count = std::max( 0, o.count );
    auto on = 0;
    if( o.max > 0 && o.cur > 0 ) {
        const auto exact = static_cast<double>( count ) * static_cast<double>( o.cur ) /
                           static_cast<double>( o.max );
        on = std::clamp( static_cast<int>( std::lround( exact ) ), 0, count );
    }
    std::string out;
    out.reserve( static_cast<std::size_t>( count ) * 32 );
    for( auto k = 0; k < count; ++k ) {
        out.append( k < on ? R"(<div class="nc-pip on"></div>)" : R"(<div class="nc-pip"></div>)" );
    }
    return out;
}

/// An empty `.label` omits its element rather than emitting an empty one, for
/// the same reason an empty `.sub` does: `.nc-fact-label` is a block, so an
/// empty one still spends 11 dp of a fixed-height panel on nothing. That is how
/// a fact carries a bare value — a target's name, a vehicle's name — without
/// inventing a caption for it.
auto hud_runic::fact( const fact_options &o ) -> std::string
{
    std::string out = R"(<div class="nc-fact">)";
    if( !o.label.empty() ) {
        out += std::format( R"(<div class="nc-fact-label">{}</div>)", esc( o.label ) );
    }
    out += std::format( R"(<div class="nc-fact-value {}">{}</div>)", cls( o.value_ink ),
                        esc( o.value ) );
    if( !o.sub.empty() ) {
        out += std::format( R"(<div class="nc-fact-sub">{}</div>)", esc( o.sub ) );
    }
    out += "</div>";
    return out;
}

auto hud_runic::legend_item( const legend_options &o ) -> std::string
{
    std::string out = R"(<div class="nc-legend-item">)";
    if( !o.label.empty() ) {
        const auto label_cls = o.label_class.empty()
                               ? std::string( "nc-legend-label" )
                               : std::format( "nc-legend-label {}", o.label_class );
        out += std::format( R"(<span class="{} {}">{}</span>)", label_cls, cls( o.label_ink ),
                            esc( o.label ) );
        out += R"(<span class="nc-legend-sep">::</span>)";
    }
    const auto value_cls = o.alarm ? std::string( "hud-alarm" ) : std::string( cls( o.value_ink ) );
    out += o.value_class.empty()
           ? std::format( R"(<span class="{}">{}</span>)", value_cls, esc( o.value ) )
           : std::format( R"(<span class="{} {}">{}</span>)", o.value_class, value_cls,
                          esc( o.value ) );
    out += "</div>";
    return out;
}

auto hud_runic::chip( std::string_view text, ink i ) -> std::string
{
    return std::format( R"(<span class="nc-chip"><span class="nc-chip-label {}">{}</span></span>)",
                        cls( i ), esc( text ) );
}

auto hud_runic::tally_row( std::string_view name, std::string_view val, ink name_ink,
                           ink val_ink ) -> std::string
{
    std::string out = R"(<div class="nc-tally-row">)";
    out += std::format( R"(<span class="nc-tally-name {}">{}</span>)", cls( name_ink ),
                        esc( name ) );
    if( !val.empty() ) {
        out += std::format( R"(<span class="nc-tally-val {}">{}</span>)", cls( val_ink ),
                            esc( val ) );
    }
    out += "</div>";
    return out;
}

// ── Severity ────────────────────────────────────────────────────────────────

auto hud_runic::is_critical( const crit_options &o ) -> bool
{
    // Acute first: an actively bleeding or bitten limb is critical at ANY
    // health, including a broken one, because those are the states that tick
    // damage between turns.
    if( o.bleeding || o.bitten ) {
    return true;
}
// A broken limb is NOT critical on its own. This has to be an explicit early
// return rather than a comment saying `o.broken` is "deliberately not
// consulted": `is_limb_broken` means hp == 0, so a broken limb ALWAYS trips
// the ratio test below, and leaving it to fall through made every broken
// limb permanently shout — which spends the register's loudest signal on a
// long-term condition the player cannot act on this turn, and makes the
// ordinary "BROKE / SPLINT" row in hud_soma unreachable.
if( o.broken ) {
    return false;
}
return o.max > 0 && static_cast<float>( o.cur ) / static_cast<float>( o.max ) < 1.0f / 3.0f;
}
