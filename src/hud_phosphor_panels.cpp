#include "hud_phosphor_panels.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <iterator>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "catacharset.h"
#include "character_display.h"
#include "coordinates.h"
#include "creature.h"
#include "damage.h"
#include "effect.h"
#include "item.h"
#include "itype.h"
#include "line.h"
#include "overmap.h"
#include "panels.h"
#include "point.h"
#include "rml_util.h"
#include "string_formatter.h"
#include "string_utils.h"
#include "translations.h"
#include "type_id.h"

namespace
{

using hud_phosphor::ink;

const efftype_id effect_bite( "bite" );
const efftype_id effect_bleed( "bleed" );
const efftype_id effect_infected( "infected" );

const flag_id json_flag_SPLINT( "SPLINT" );

// ── Box glyphs ──────────────────────────────────────────────────────────────
//
// Every frame stroke in this register is a real Unicode box-drawing character
// inside a text run; there is not one CSS `border` in the phosphor stylesheet.
// SOMA frames its RIGHT edge and DOCK its LEFT, because each panel only draws
// the side that faces the play area — the screen edge is the fourth wall.

constexpr std::string_view glyph_vert = "\u2502";      // │
constexpr std::string_view glyph_corner_bl = "\u2514"; // └
constexpr std::string_view glyph_corner_br = "\u2518"; // ┘
constexpr std::string_view glyph_tee_left = "\u251c";  // ├
constexpr std::string_view glyph_tee_right = "\u2524"; // ┤
constexpr std::string_view glyph_trough = "\u2591";    // ░
constexpr std::string_view glyph_block = "\u2588";     // █
constexpr std::string_view glyph_half_left = "\u258c"; // ▌
constexpr std::string_view glyph_half_right = "\u2590"; // ▐

/// `pad | label | gap` before a DOCK field's value.
constexpr int dock_label_cells = 6;

auto repeat( std::string_view glyph, int n ) -> std::string
{
    auto out = std::string();
    out.reserve( glyph.size() * std::max( n, 0 ) );
    for( auto k = 0; k < n; ++k ) {
        out += glyph;
    }
    return out;
}

/// Plain text from an RML fragment: drops tag runs and resolves the three
/// entities `rml_escape` emits.
///
/// This exists for one job: `fit` has to measure a `raw` segment, whose text is
/// already RML — a bar built from block glyphs, an inverted cell — and
/// `display_width` cannot see through markup. Laundering it back to glyphs keeps
/// the measurement honest without every producer having to track its own widths.
auto strip_rml( std::string_view rml ) -> std::string
{
    auto out = std::string();
    out.reserve( rml.size() );
    for( auto i = std::size_t{ 0 }; i < rml.size(); ) {
        if( rml[i] == '<' ) {
            const auto close = rml.find( '>', i );
            i = close == std::string_view::npos ? rml.size() : close + 1;
            continue;
        }
        if( rml[i] == '&' ) {
            const auto rest = rml.substr( i );
            if( rest.starts_with( "&amp;" ) ) {
                out += '&';
                i += 5;
                continue;
            }
            if( rest.starts_with( "&lt;" ) ) {
                out += '<';
                i += 4;
                continue;
            }
            if( rest.starts_with( "&gt;" ) ) {
                out += '>';
                i += 4;
                continue;
            }
        }
        out += rml[i++];
    }
    return out;
}

// ── Rows as runs ────────────────────────────────────────────────────────────

/// One run inside a row.
///
/// Rows are assembled as runs rather than as one string so that a critical row
/// can be inverted wholesale: reverse video is the register's only shout and it
/// owns the entire row. Nesting is safe — `.ph-inv .ph-i*` collapses every rung
/// inside an inverted row to `ph-k`, which is what `ph-k` is for — so a run
/// that arrives already rendered (a `hud_phosphor::bar`, a per-cell inversion
/// in the overmap) passes through untouched as `raw`.
struct seg {
    ink i = ink::label;
    std::string text;  ///< plain UTF-8, unless `raw`
    bool raw = false;  ///< already-rendered RML: emitted verbatim
    int cells = 0;     ///< display width; consulted only when `raw`

    auto width() const -> int
    {
        return raw ? cells : hud_phosphor::display_width( text );
    }
};

auto seg_width( const std::vector<seg> &segs ) -> int
{
    auto total = 0;
    for( const auto &s : segs ) {
        total += s.width();
    }
    return total;
}

/// Force `segs` to occupy exactly `field` cells: pad short, truncate long.
///
/// The truncation branch is the point of the whole exercise. A producer that
/// hand-counts spaces silently overruns its region the first time a translated
/// string grows, which is how `[Unbound globally!]` came to render 34 dp past
/// the edge of its box. Here overrun is impossible by construction.
auto fit( std::vector<seg> segs, int field ) -> std::vector<seg>
{
    auto out = std::vector<seg>();
    out.reserve( segs.size() + 1 );
    auto used = 0;
    for( auto &s : segs ) {
        const auto w = s.width();
        if( used + w <= field ) {
            used += w;
            out.push_back( std::move( s ) );
            continue;
        }
        if( used < field ) {
            const auto plain = s.raw ? strip_rml( s.text ) : s.text;
            out.push_back( { .i = s.i, .text = hud_phosphor::pad( plain, field - used ) } );
            used = field;
        }
        break;
    }
    if( used < field ) {
        out.push_back( { .i = ink::rule, .text = std::string( field - used, ' ' ) } );
    }
    return out;
}

/// Push `tail` flush against the right edge of a `field`-cell row.
auto justify( std::vector<seg> head, std::vector<seg> tail, int field ) -> std::vector<seg>
{
    const auto used = seg_width( head ) + seg_width( tail );
    if( used < field ) {
        head.push_back( { .i = ink::rule, .text = std::string( field - used, ' ' ) } );
    }
    head.insert( head.end(), std::make_move_iterator( tail.begin() ),
                 std::make_move_iterator( tail.end() ) );
    return head;
}

/// Every U+0020 in a segment becomes U+00A0 on the way out, and that is the one
/// place this conversion belongs.
///
/// RmlUi strips leading and trailing whitespace from an inline run at PARSE time,
/// and these producers' markup is injected through `data-rml`, which parses with
/// the element's computed style — so `white-space: pre` declared on the element
/// being created cannot preserve it. Every segment here becomes its own `<span>`,
/// so a gap segment is a run consisting solely of a space and is trimmed to
/// nothing, while padding at a span edge is trimmed off the end. In-game that
/// welded columns together (`MISSION MARKERNONE`) and collapsed the SOMA grid.
///
/// Doing it here rather than at ~20 call sites is deliberate: in a fixed-cell grid
/// EVERY space is alignment, never word spacing that may be collapsed or wrapped,
/// so the conversion is unconditionally correct for anything a segment can hold —
/// including the interior spaces of a phrase like `MISSION MARKER`, which must not
/// wrap either. `hud_phosphor::pad` already fills with U+00A0 for the same reason;
/// this catches the literals and the `std::string( n, ' ' )` runs that do not go
/// through it.
auto render_runs( const std::vector<seg> &segs ) -> std::string
{
    static constexpr std::string_view nbsp = "\u00a0";
    const auto no_break = []( std::string_view text ) -> std::string {
        auto out = std::string();
        out.reserve( text.size() );
        for( const char c : text ) {
            if( c == ' ' ) {
                out += nbsp;
            } else {
                out += c;
            }
        }
        return out;
    };
    auto out = std::string();
    for( const auto &s : segs ) {
        // `raw` segments are already-rendered RML (a bar's block glyphs, a nested
        // tint) and carry no alignment spaces, so they pass through untouched —
        // rewriting bytes inside markup would corrupt the tags.
        out += s.raw ? s.text : hud_phosphor::tint( s.i, no_break( rml_escape( s.text ) ) );
    }
    return out;
}

struct row_options {
    std::vector<seg> segs;
    int cols = 0;
    std::string_view border;   ///< the box glyph closing the play-area-facing edge
    bool border_leads = false; ///< DOCK frames its left edge, SOMA its right
    bool inverted = false;
};

/// Assemble one row of exactly `cols` display cells.
auto compose( const row_options &o ) -> std::string
{
    const auto edge = o.border.empty() ? 0 : 1;
    const auto body = render_runs( fit( o.segs, std::max( o.cols - edge, 0 ) ) );
    const auto inked = o.inverted ? hud_phosphor::invert( body ) : body;
    if( edge == 0 ) {
        return inked;
    }
    const auto frame = hud_phosphor::tint( ink::rule, o.border );
    return o.border_leads ? frame + inked : inked + frame;
}

/// Trim to the region's height, always keeping `closer` as the final row.
///
/// Sections are appended in priority order, so the rows that fall off a short
/// grid are the ones at the bottom — SOMA sheds effects before pools, DOCK
/// sheds arms before its target. The closing rule survives regardless, because
/// a frame that does not close reads as a rendering fault rather than as a
/// truncated list.
auto clamp_rows( std::vector<std::string> rows, int limit,
                 std::string closer ) -> std::vector<std::string>
{
    const auto room = std::max( limit - 1, 0 );
    if( std::ssize( rows ) > room ) {
        rows.resize( room );
    }
    rows.push_back( std::move( closer ) );
    return rows;
}

/// Wrap each row in its own one-cell-tall `.ph-row` block.
///
/// One element per row, never one box with the rows joined by `\n`. `data-rml`
/// parses a producer's markup against the bound element's *current* computed
/// style, so a `white-space: pre` declared on that very element is not yet in
/// effect when those newlines are read: RmlUi drops them and the rows run
/// together and wrap at the panel edge, which is how SOMA came to render
/// `HEAD ███ 96/96 TOR` with the torso row continuing on the head row's line.
/// A block per row cannot collapse, and it is the shape the message log and
/// the function-key row already emit correctly in-game.
///
/// Rows arrive already padded to their region width, and an inverted row
/// arrives already wrapped in its span — inversion is an inline run and the
/// row is a block, so the span stays *inside* the div.
auto wrap_rows( const std::vector<std::string> &rows ) -> std::string
{
    constexpr std::string_view open = "<div class=\"ph-row\">";
    constexpr std::string_view close = "</div>";

    auto bytes = rows.size() * ( open.size() + close.size() );
    for( const auto &r : rows ) {
        bytes += r.size();
    }
    auto out = std::string();
    out.reserve( bytes );
    for( const auto &r : rows ) {
        out += open;
        out += r;
        out += close;
    }
    return out;
}

// ── Internal column grids ───────────────────────────────────────────────────

/// SOMA's measurement grid, from the mockup's nominal 34 cells:
/// `1 pad | 6 label | 1 gap | 15 bar | 1 gap | 9 value | 1 border`.
///
/// Only the bar flexes. Everything else is a field whose content has a natural
/// width, so spending the panel's spare cells on bar resolution is the one
/// choice that degrades gracefully in both directions; below the point where
/// the bar would stop reading, the value column gives up cells instead.
struct soma_grid {
    int label = 6;
    int bar = 15;
    int value = 9;
};

auto soma_grid_for( int cols ) -> soma_grid
{
    constexpr auto chrome = 4; // 1 pad + 2 gaps + 1 border
    constexpr auto min_bar = 4;
    const auto slack = cols - chrome - 6 - 9;
    if( slack >= min_bar ) {
        return { .label = 6, .bar = slack, .value = 9 };
    }
    const auto value = std::clamp( cols - chrome - 6 - min_bar, 4, 9 );
    const auto label = std::clamp( cols - chrome - min_bar - value, 3, 6 );
    return { .label = label, .bar = std::max( cols - chrome - label - value, 1 ), .value = value };
}

/// SOMA's roster grid: `1 pad | 2 glyph | 1 gap | 20 name | 9 site | 1 border`.
struct roster_grid {
    int name = 20;
    int site = 9;
};

auto roster_grid_for( int cols ) -> roster_grid
{
    constexpr auto chrome = 5; // 1 pad + 2 glyph + 1 gap + 1 border
    const auto site = std::clamp( cols - chrome - 8, 0, 9 );
    return { .name = std::max( cols - chrome - site, 1 ), .site = site };
}

// ── Ladder bands ────────────────────────────────────────────────────────────
//
// The threshold helpers in `panels.cpp` return `nc_color`, which is exactly the
// part this register cannot use. What is worth copying is their *bands*: an
// ordinary value recedes to `label`, a notable one advances to `datum`, an
// alarming one peaks. Other screens still use the originals, so these are a
// translation rather than a duplicate.

auto pool_ink( int cur, int max ) -> ink
{
    if( max <= 0 ) {
        return ink::dead;
    }
    const auto pct = 100.0f * static_cast<float>( cur ) / static_cast<float>( max );
    if( pct <= 25.0f ) {
        return ink::peak;
    }
    return pct <= 50.0f ? ink::datum : ink::label;
}

auto morale_ink( int level ) -> ink
{
    if( level <= -10 ) {
        return ink::peak;
    }
    return level >= 10 ? ink::datum : ink::label;
}

// ── Bars ────────────────────────────────────────────────────────────────────

/// Morale as a deviation scale, not a fill bar.
///
/// The axis sits at the middle cell; negative morale grows left and positive
/// grows right, so the sign is carried by direction — a channel this register
/// still has — instead of by the red/green pair it has spent. A magnitude under
/// one cell shows as the axis cell's half-block on the correct side, which is
/// the smallest mark the font can make and still keeps zero distinguishable
/// from nearly-zero.
struct morale_scale_options {
    int level = 0;
    int cells = 15;
    int span = 100; ///< magnitude that fills one arm of the scale end to end
};

auto morale_scale( const morale_scale_options &o ) -> std::string
{
    // The arm is the room on the side the value actually points, so an even
    // cell count (which has no true centre) can never overrun its short side.
    const auto axis = std::clamp( o.cells / 2, 0, std::max( o.cells - 1, 0 ) );
    const auto arm = std::max( o.level < 0 ? axis : o.cells - 1 - axis, 0 );
    const auto mag = arm == 0 ? 0
                     : std::min( std::abs( o.level ) * arm / std::max( o.span, 1 ), arm );

    auto lead = axis;
    auto mark = std::string();
    if( o.level == 0 ) {
        lead = o.cells;
    } else if( mag == 0 ) {
        mark = o.level < 0 ? glyph_half_left : glyph_half_right;
    } else if( o.level < 0 ) {
        lead = axis - mag;
        mark = repeat( glyph_block, mag );
    } else {
        lead = axis + 1;
        mark = repeat( glyph_block, mag );
    }

    const auto marked = hud_phosphor::display_width( mark );
    return hud_phosphor::tint( ink::rule, repeat( glyph_trough, lead ) )
           + hud_phosphor::tint( morale_ink( o.level ), mark )
           + hud_phosphor::tint( ink::rule, repeat( glyph_trough, o.cells - lead - marked ) );
}

// ── Rules ───────────────────────────────────────────────────────────────────

/// `───┤ POOLS ├────────┤` — a SOMA section head, terminating in the panel's
/// right border. Costs zero rows, which is what makes section heads affordable.
auto soma_sub_rule( const std::string &title, int cols ) -> std::string
{
    return hud_phosphor::rule( { .cols = cols,
                                 .titles = { { .col = 3, .text = title } },
                                 .right = std::string( glyph_tee_right ) } );
}

/// `├─┤ TARGET ├────────` — a DOCK section head, opening from its left border.
auto dock_sub_rule( const std::string &title, int cols ) -> std::string
{
    return hud_phosphor::rule( { .cols = cols,
                                 .titles = { { .col = 2, .text = title } },
                                 .left = std::string( glyph_tee_left ) } );
}

// ── SOMA rows ───────────────────────────────────────────────────────────────

/// `pad | label | gap | bar | gap | value | border`.
struct gauge_row_options {
    std::string label;
    std::string bar;   ///< already-rendered RML, `grid.bar` cells wide
    std::string value;
    ink label_ink = ink::label;
    ink value_ink = ink::datum;
    bool inverted = false;
    soma_grid grid = {};
    int cols = 0;
};

auto gauge_row( const gauge_row_options &o ) -> std::string
{
    return compose( {
        .segs = {
            { .i = ink::rule, .text = " " },
            { .i = o.label_ink, .text = hud_phosphor::pad( o.label, o.grid.label ) },
            { .i = ink::rule, .text = " " },
            { .text = o.bar, .raw = true, .cells = o.grid.bar },
            { .i = ink::rule, .text = " " },
            { .i = o.value_ink, .text = hud_phosphor::pad_left( o.value, o.grid.value ) },
        },
        .cols = o.cols,
        .border = glyph_vert,
        .inverted = o.inverted,
    } );
}

struct limb_state {
    bool bleeding = false;
    bool bitten = false;
    bool infected = false;
    bool broken = false;
    bool splinted = false;
    bool critical = false;
};

/// The words on a limb's continuation row.
///
/// This is the fix for the shipping panel's silent drop. `hud_vitals` resolved
/// `u.limb_color( bp, true, true, true )` into a hex string at `panels.cpp:872`
/// and then never used it, so a bleeding, bitten arm said so only in the bottom
/// bar — 891 dp from the limb it described, and nowhere at all in the body
/// panel. `bodygraph_bp_color` (`panels.cpp:2011-2053`) drew the same
/// bleed/bite/infected/splint distinction and was never once consulted by the
/// HUD path. Here the states are words, on the row under the part they belong
/// to, which is a channel that survives greyscale as well.
auto limb_notes( const limb_state &s ) -> std::vector<std::string>
{
    auto notes = std::vector<std::string>();
    if( s.critical ) {
        notes.emplace_back( _( "CRITICAL" ) );
    }
    if( s.bleeding ) {
        notes.emplace_back( _( "BLEEDING" ) );
    }
    if( s.bitten ) {
        notes.emplace_back( _( "BITTEN" ) );
    }
    if( s.infected ) {
        notes.emplace_back( _( "INFECTED" ) );
    }
    if( s.broken ) {
        // Splinted versus not, as a word and (via the mend value's rung) as a
        // luminance step — the shipping panel said it in blue versus grey.
        notes.emplace_back( s.splinted ? _( "MENDING" ) : _( "BROKEN" ) );
    }
    return notes;
}

auto note_row( const std::vector<std::string> &notes, bool critical, int cols ) -> std::string
{
    return compose( {
        .segs = {
            { .i = ink::rule, .text = " " },
            { .i = critical ? ink::peak : ink::datum,
              .text = hud_phosphor::pad( critical ? "!!" : "!", 2 ) },
            { .i = ink::rule, .text = " " },
            { .i = critical ? ink::peak : ink::datum, .text = join( notes, " - " ) },
        },
        .cols = cols,
        .border = glyph_vert,
        .inverted = critical,
    } );
}

// ── SOMA effect roster ──────────────────────────────────────────────────────

/// Severity tiers, quietest first, and the rung each is drawn at.
///
/// The glyphs are the register's own ladder — `!!` › `!` › `^` › `·` — and the
/// bottom rung is U+00B7 MIDDLE DOT, never an ASCII full stop. That is not
/// pedantry. A period sits on the baseline, so it reads as punctuation trailing
/// the row above rather than as a mark in the glyph column, and `.` is already
/// spent one panel over on the overmap's open ground. The middle dot is centred
/// in its cell and stacks under the `!` and `!!` above it, which is what makes
/// the column scannable as a ladder rather than as five unrelated marks.
constexpr std::array<std::string_view, 4> tier_glyphs = { "\u00b7", "^", "!", "!!" };
constexpr std::array<ink, 4> tier_inks = { ink::label, ink::datum, ink::datum, ink::peak };

/// Severity tier for every live effect, keyed by the display name
/// `character_display::effect_name_and_text` will report for it.
///
/// Derived from the effect's own JSON rating and whether it is localised, never
/// from a hard-coded name list: a bad effect pinned to a body part is the class
/// this panel exists to surface (bleed, bite, infection), a bad effect on the
/// whole character is one step below it, a mixed effect is a caution, and
/// everything else is a note. Rows the roster synthesises rather than reads
/// from the effects map — pain, body mass, withdrawal — are absent here and
/// fall to the bottom tier, because the status strip already reports them at
/// their own severity and repeating that shout would flatten this one.
auto effect_tiers( const avatar &u ) -> std::map<std::string, int>
{
    const auto live = u.get_all_effects();
    auto tiers = std::map<std::string, int>();
    for( const auto &by_part : live | std::views::values ) {
        for( const auto &eff : by_part | std::views::values ) {
            auto name = eff.disp_name();
            if( name.empty() ) {
                continue;
            }
            const auto rating = eff.get_effect_type()->get_rating();
            const auto tier = rating == e_bad ? ( eff.get_bp().is_empty() ? 2 : 3 )
                              : rating == e_mixed ? 1 : 0;
            tiers.insert_or_assign( std::move( name ), tier );
        }
    }
    return tiers;
}

struct named_site {
    std::string name;
    std::string site;
};

/// Split the roster's site column off an effect's display name.
///
/// `effect::disp_name` already appends `" (l. arm)"` for a limb-local effect
/// (`effect.cpp:607-609`), so the site is there for the taking; re-deriving the
/// body part from the effects map would also lose the synthesised rows, which
/// have no body part and must simply leave the column blank.
auto split_site( const std::string &full ) -> named_site
{
    const auto open = full.rfind( " (" );
    if( !full.ends_with( ')' ) || open == std::string::npos ) {
        return { .name = full };
    }
    return { .name = full.substr( 0, open ),
             .site = full.substr( open + 2, full.size() - open - 3 ) };
}

struct roster_row_options {
    named_site entry = {};
    int tier = 0;
    roster_grid grid = {};
    int cols = 0;
};

auto roster_row( const roster_row_options &o ) -> std::string
{
    const auto rung = tier_inks[o.tier];
    return compose( {
        .segs = {
            { .i = ink::rule, .text = " " },
            { .i = rung, .text = hud_phosphor::pad( tier_glyphs[o.tier], 2 ) },
            { .i = ink::rule, .text = " " },
            { .i = rung, .text = hud_phosphor::pad( o.entry.name, o.grid.name ) },
            { .i = rung, .text = hud_phosphor::pad_left( o.entry.site, o.grid.site ) },
        },
        .cols = o.cols,
        .border = glyph_vert,
    } );
}

// ── DOCK helpers ────────────────────────────────────────────────────────────

/// `border | pad | label | gap | value` — DOCK's plain two-field row. An empty
/// label indents the value under the field above it, which is how the arms
/// block hangs damage numbers and ammo types off their weapon.
struct field_row_options {
    std::string label;
    std::string value;
    ink value_ink = ink::datum;
    int cols = 0;
};

auto field_row( const field_row_options &o ) -> std::string
{
    return compose( {
        .segs = {
            { .i = ink::rule, .text = " " },
            { .i = ink::label, .text = hud_phosphor::pad( o.label, dock_label_cells ) },
            { .i = ink::rule, .text = " " },
            { .i = o.value_ink, .text = o.value },
        },
        .cols = o.cols,
        .border = glyph_vert,
        .border_leads = true,
    } );
}

/// The first stowed firearm, or null.
///
/// `items_with` walks contents, so a pistol inside a holster inside a coat is
/// still found; the wielded item is excluded by identity rather than by name
/// because a character may well be carrying two of the same gun.
auto stowed_sidearm( const avatar &u ) -> const item * // *NOPAD*
{
    const item *held = u.is_armed() ? &u.primary_weapon() : nullptr;
    const auto guns = u.items_with( []( const item & it ) {
        return it.is_gun() && !it.is_gunmod();
    } );
    const auto found = std::ranges::find_if( guns, [held]( const item * g ) {
        return g != held;
    } );
    return found == guns.end() ? nullptr : *found;
}

} // namespace

auto hud_soma( avatar &u, const hud_phosphor::layout &l ) -> std::string
{
    const auto cols = l.soma.cols;
    if( cols <= 0 || l.soma.rows <= 0 ) {
        return {};
    }
    const auto grid = soma_grid_for( cols );
    auto rows = std::vector<std::string>();

    // ── Body parts ──────────────────────────────────────────────────────────
    for( const bodypart_id &bp : u.get_all_body_parts( true ) ) {
        const auto cur = u.get_part_hp_cur( bp );
        const auto max = u.get_part_hp_max( bp );
        const auto bleeding = u.has_effect( effect_bleed, bp.id() );
        const auto bitten = u.has_effect( effect_bite, bp.id() );
        const auto infected = u.has_effect( effect_infected, bp.id() );
        const auto broken = u.is_limb_broken( bp ) && !bp->essential;
        const auto splinted = u.worn_with_flag( json_flag_SPLINT, bp ) ||
                              u.mutation_value( "mending_modifier" ) >= 1.0f;
        // The shipping gate was integer `cur * 100 / max < 25`, so 8/30
        // truncated to 26 and a limb at 26.7% that was both bleeding and bitten
        // never once rendered critical. This predicate is float, thresholds at
        // a third, and treats bleeding or bitten as critical at any health.
        const auto critical = hud_phosphor::is_critical( { .cur = cur, .max = max,
                              .bleeding = bleeding, .bitten = bitten, .broken = broken } );

        // A broken limb shows its state as a WORD, not a percentage. There is no
        // mend-progress datum in this fork to show: the `mending` effect is
        // obsolete (`data/json/obsoletion/effects.json:66`), and
        // `bodypart::healed_total` is a per-turn heal accumulator that is reset
        // every cycle (`character_needs.cpp:694`), not progress toward un-breaking.
        // An earlier draft printed `100 * cur / max`, which for a broken limb —
        // `is_limb_broken` means hp == 0 — is a gauge permanently reading 0%. A
        // number that never moves is worse than no number: it invites the player to
        // wait for it. The splinted-versus-not distinction the shipping panel drew
        // in blue-versus-grey is carried here by the word (MENDING / BROKEN, see
        // `limb_notes`) and by the rung, which survives greyscale as colour did not.
        const auto value_ink = broken ? ( splinted ? ink::datum : ink::dead )
                               : ( cur >= max ? ink::label : ink::datum );

        rows.push_back( gauge_row( {
            .label = body_part_hp_bar_ui_text( bp ),
            .bar = hud_phosphor::bar( { .cur = cur, .max = max, .cells = grid.bar } ),
            .value = broken ? std::string{ splinted ? _( "SPLINT" ) : _( "BROKE" ) }
            : std::format( "{}/{}", cur, max ),
            .label_ink = cur >= max ? ink::label : ink::datum,
            .value_ink = value_ink,
            .inverted = critical,
            .grid = grid,
            .cols = cols,
        } ) );

        const auto notes = limb_notes( { .bleeding = bleeding, .bitten = bitten,
                                         .infected = infected, .broken = broken,
                                         .splinted = splinted, .critical = critical } );
        if( !notes.empty() ) {
            rows.push_back( note_row( notes, critical, cols ) );
        }
    }

    // ── Pools ───────────────────────────────────────────────────────────────
    rows.push_back( soma_sub_rule( _( "POOLS" ), cols ) );

    const auto stamina = u.get_stamina();
    const auto stamina_max = u.get_stamina_max();
    rows.push_back( gauge_row( {
        .label = _( "STAM" ),
        .bar = hud_phosphor::bar( { .cur = stamina, .max = stamina_max, .cells = grid.bar } ),
        .value = std::format( "{}/{}", stamina, stamina_max ),
        .value_ink = pool_ink( stamina, stamina_max ),
        .grid = grid,
        .cols = cols,
    } ) );

    constexpr auto focus_span = 100;
    rows.push_back( gauge_row( {
        .label = _( "FOCUS" ),
        .bar = hud_phosphor::bar( { .cur = u.focus_pool, .max = focus_span, .cells = grid.bar } ),
        .value = std::format( "{}", u.focus_pool ),
        .value_ink = pool_ink( u.focus_pool, focus_span ),
        .grid = grid,
        .cols = cols,
    } ) );

    const auto morale = u.get_morale_level();
    rows.push_back( gauge_row( {
        .label = _( "MORALE" ),
        .bar = morale_scale( { .level = morale, .cells = grid.bar } ),
        .value = morale == 0 ? std::string( "0" ) : std::format( "{:+}", morale ),
        .value_ink = morale_ink( morale ),
        .grid = grid,
        .cols = cols,
    } ) );

    // ── Effects ─────────────────────────────────────────────────────────────
    rows.push_back( soma_sub_rule( _( "EFFECTS" ), cols ) );

    const auto roster = character_display::effect_name_and_text( u );
    if( roster.empty() ) {
        rows.push_back( compose( { .segs = { { .i = ink::rule, .text = " " },
                                             { .i = ink::dead, .text = _( "NONE" ) } },
                                   .cols = cols, .border = glyph_vert } ) );
    } else {
        const auto tiers = effect_tiers( u );
        const auto grid_r = roster_grid_for( cols );
        const auto budget = std::max( l.soma.rows - std::ssize( rows ) - 1, std::ptrdiff_t{ 0 } );
        for( const auto &entry : roster | std::views::take( budget ) ) {
            // Shouted here and not in `split_site`, because the tier lookup
            // keys on the name `effect_tiers` recorded — which is the one this
            // roster arrived with, in its own case.
            const auto found = tiers.find( entry.first );
            rows.push_back( roster_row( {
                .entry = split_site( to_upper_case( entry.first ) ),
                .tier = found == tiers.end() ? 0 : found->second,
                .grid = grid_r,
                .cols = cols,
            } ) );
        }
    }

    const auto closer = hud_phosphor::rule( { .cols = cols,
                                              .right = std::string( glyph_corner_br ) } );
    return wrap_rows( clamp_rows( std::move( rows ), l.soma.rows, closer ) );
}

/// The RADAR region's RmlUi content: nothing but the play-area-facing vertical,
/// one row per viewport row.
///
/// The dots themselves are GPU quads on the UI layer, which composites BELOW the
/// RmlUi document — so this element must stay transparent (no `.ph-veil` in the
/// stylesheet) and must emit no ground of its own. `hud_radar::draw` paints the
/// interior's ground itself, from the same `ink::ground` rung, for the reason
/// given in `hud_phosphor.h`: a luminance-only encoding needs a controlled
/// background, and an uncontrolled one would eat the two dimmest rungs.
auto hud_radar_frame( const hud_phosphor::layout &l ) -> std::string
{
    const auto cols = l.radar.cols;
    if( cols <= 0 || l.radar.rows <= 0 ) {
        return {};
    }
    auto rows = std::vector<std::string>();
    rows.reserve( l.radar.rows );
    for( auto k = 0; k < l.radar.rows; ++k ) {
        rows.push_back( compose( { .segs = {}, .cols = cols, .border = glyph_vert,
                                   .border_leads = true } ) );
    }
    return wrap_rows( rows );
}

auto hud_dock( avatar &u, const hud_phosphor::layout &l ) -> std::string
{
    const auto cols = l.dock.cols;
    if( cols <= 0 || l.dock.rows <= 0 ) {
        return {};
    }
    auto rows = std::vector<std::string>();

    // The mission marker's bearing, read by the caption row below. The overmap
    // chunk that used to head this panel is now the RADAR region above it.
    const auto here = u.abs_omt_pos();
    const auto custom = u.get_custom_mission_target();
    const auto active = u.get_active_mission_target();
    const auto target_omt = custom != overmap::invalid_tripoint ? custom : active;
    const auto has_mission = target_omt != overmap::invalid_tripoint;

    const auto bearing = has_mission
                         ? std::format( "{} {}", rl_dist( here, target_omt ),
                                        direction_name_short( direction_from( here.raw().xy(),
                                                target_omt.raw().xy() ) ) )
                         : std::string( _( "NONE" ) );
    rows.push_back( compose( {
        .segs = justify( { { .i = ink::rule, .text = " " },
            { .i = has_mission ? ink::peak : ink::dead, .text = "^" },
            { .i = ink::rule, .text = " " },
            { .i = ink::label, .text = _( "MISSION MARKER" ) } },
        { { .i = has_mission ? ink::peak : ink::dead, .text = bearing },
            { .i = ink::rule, .text = " " } },
        cols - 1 ),
        .cols = cols, .border = glyph_vert, .border_leads = true,
    } ) );

    // ── Target ──────────────────────────────────────────────────────────────
    rows.push_back( dock_sub_rule( _( "TARGET" ), cols ) );

    const auto locked = u.last_target.lock();
    if( const Creature *t = locked.get() ) {
        const auto hp = t->get_hp();
        const auto hp_max = std::max( t->get_hp_max(), 1 );
        const auto pct = std::clamp( 100 * hp / hp_max, 0, 100 );
        const auto self = u.abs_pos();
        const auto foe = t->abs_pos();
        const auto range = rl_dist( self, foe );
        const auto heading = direction_name_short(
                                 direction_from( self.raw().xy(), foe.raw().xy() ) );

        rows.push_back( compose( {
            .segs = justify( { { .i = ink::rule, .text = " " },
                { .i = ink::peak, .text = to_upper_case( t->disp_name() ) } },
            { { .i = ink::datum, .text = string_format( _( "%d TILES %s" ), range, heading ) },
                { .i = ink::rule, .text = " " } },
            cols - 1 ),
            .cols = cols, .border = glyph_vert, .border_leads = true,
        } ) );

        // The shipping target bar was a permanently empty trough: `.tbar-fill`
        // was an inline <span> with no `display`, so its width never applied.
        // A glyph bar cannot fail that way — its fill is characters.
        const auto bar_cells = std::clamp( cols - 12, 4, 15 );
        rows.push_back( compose( {
            .segs = justify( { { .i = ink::rule, .text = " " },
                { .i = ink::label, .text = _( "HP" ) },
                { .i = ink::rule, .text = " " },
                { .text = hud_phosphor::bar( { .cur = hp, .max = hp_max, .cells = bar_cells,
                                               .intact_recedes = false } ),
                    .raw = true, .cells = bar_cells } },
            { { .i = ink::datum, .text = std::format( "{}%", pct ) },
                { .i = ink::rule, .text = " " } },
            cols - 1 ),
            .cols = cols, .border = glyph_vert, .border_leads = true,
        } ) );

        const auto &attitude = Creature::get_attitude_ui_data( t->attitude_to( u ) );
        rows.push_back( field_row( { .label = _( "STATUS" ),
                                     .value = to_upper_case( attitude.first.translated() ),
                                     .cols = cols } ) );
    } else {
        // No target collapses to one line rather than framing an empty box.
        rows.push_back( compose( { .segs = { { .i = ink::rule, .text = " " },
                                             { .i = ink::dead, .text = _( "NO TARGET" ) } },
                                   .cols = cols, .border = glyph_vert, .border_leads = true } ) );
    }

    // ── Arms ────────────────────────────────────────────────────────────────
    rows.push_back( dock_sub_rule( _( "ARMS" ), cols ) );

    const auto armed = u.is_armed();
    // Shouted at the point of render, never in the catalogue: `fists`, an item
    // `tname` and an ammo `nname` are all translated prose belonging to somebody
    // else, so this register re-cases them rather than demanding every locale
    // ship a second, upper-case copy. `to_upper_case` goes through the locale's
    // ctype facet — a byte loop over `std::toupper` would shred every
    // multi-byte glyph in a translated name.
    rows.push_back( field_row( {
        .label = _( "WIELD" ),
        .value = to_upper_case( armed ? u.primary_weapon().tname()
                                : std::string( _( "fists" ) ) ),
        .value_ink = armed ? ink::peak : ink::dead,
        .cols = cols,
    } ) );
    if( armed ) {
        const item &held = u.primary_weapon();
        rows.push_back( compose( {
            .segs = {
                { .i = ink::rule, .text = std::string( dock_label_cells + 2, ' ' ) },
                { .i = ink::datum, .text = std::format( "{}", held.damage_melee( DT_BASH ) ) },
                { .i = ink::rule, .text = " " },
                { .i = ink::label, .text = _( "BASH" ) },
                { .i = ink::rule, .text = "  " },
                { .i = ink::datum, .text = std::format( "{}", held.damage_melee( DT_CUT ) ) },
                { .i = ink::rule, .text = " " },
                { .i = ink::label, .text = _( "CUT" ) },
                { .i = ink::rule, .text = "  " },
                { .i = ink::label, .text = _( "HIT" ) },
                { .i = ink::rule, .text = " " },
                { .i = ink::datum, .text = std::format( "{:+}", held.type->m_to_hit ) },
            },
            .cols = cols, .border = glyph_vert, .border_leads = true,
        } ) );
    }

    if( const item *sidearm = stowed_sidearm( u ) ) {
        const auto loaded = sidearm->ammo_remaining();
        const auto capacity = sidearm->ammo_capacity();
        rows.push_back( compose( {
            .segs = justify( { { .i = ink::rule, .text = " " },
                { .i = ink::label, .text = hud_phosphor::pad( _( "ALT" ), dock_label_cells ) },
                { .i = ink::rule, .text = " " },
                { .i = ink::datum, .text = to_upper_case( sidearm->tname() ) } },
            { { .i = loaded > 0 ? ink::datum : ink::dead,
                    .text = std::format( "{}/{}", loaded, capacity ) },
                { .i = ink::rule, .text = " " } },
            cols - 1 ),
            .cols = cols, .border = glyph_vert, .border_leads = true,
        } ) );

        const auto *ammo = sidearm->ammo_data();
        rows.push_back( field_row( { .label = {},
                                     .value = ammo != nullptr ? to_upper_case( ammo->nname( 1 ) )
                                     : _( "EMPTY" ),
                                     .value_ink = ammo != nullptr ? ink::label : ink::dead,
                                     .cols = cols } ) );
    }

    const auto closer = hud_phosphor::rule( { .cols = cols,
                                              .left = std::string( glyph_corner_bl ) } );
    return wrap_rows( clamp_rows( std::move( rows ), l.dock.rows, closer ) );
}
