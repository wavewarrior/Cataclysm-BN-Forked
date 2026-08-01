#include "hud_phosphor_strips.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "action.h"
#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "character.h"
#include "color.h"
#include "coop_server.h"
#include "coop_session.h"
#include "game.h"
#include "input.h"
#include "item.h"
#include "lightmap.h"
#include "line.h"
#include "map.h"
#include "messages.h"
#include "omdata.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "panels_utility.h"
#include "point.h"
#include "point_float.h"
#include "profession.h"
#include "rml_util.h"
#include "string_utils.h"
#include "tileray.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "units_utility.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_type.h"

namespace
{

using hud_phosphor::ink;

// ── Ladder bands ────────────────────────────────────────────────────────────
//
// The thresholds below are lifted verbatim from the file-static helpers in
// `panels.cpp` — `temp_color`, `value_color`, and the
// `str_string`/`dex_string`/… family via `color_compare_base`. Their *hues*
// are deliberately left behind.
//
// That is the point of the exercise rather than a duplication smell. This
// register has exactly one hue, so a threshold ladder can only be expressed as
// luminance: a normal value reads at `ink::label`, a notable one at
// `ink::datum`, an alarming one at `ink::peak`. Reusing the originals would not
// help, because the mapping from `c_yellow` back to "notable" is not injective
// — the same colour means "buffed stat" three functions further up the same
// file. So we keep the bands, which are the actual design decision, and drop
// the encoding, which is not. `panels.cpp` keeps its originals untouched: the
// curses screens and the widget system still render in hue.

/// A stat against its unmodified base: below base is alarming, above is notable.
auto stat_rung( int base, int value ) -> ink
{
    if( value < base ) {
        return ink::peak;
    }
    return value > base ? ink::datum : ink::label;
}

/// Speed, from `value_color`'s 75/50/25 bands.
auto speed_rung( int speed ) -> ink
{
    if( speed >= 75 ) {
        return ink::label;
    }
    return speed >= 50 ? ink::datum : ink::peak;
}

/// Temperature, from `temp_color`'s Fahrenheit bands. Symmetric: both ends of
/// the ladder are alarming and both shoulders notable, which the hue version
/// could only express by spending four separate colours.
auto temp_rung( units::temperature t ) -> ink
{
    const auto f = units::to_fahrenheit( t );
    if( f < 32 || f >= 95 ) {
        return ink::peak;
    }
    return f < 50 || f >= 77 ? ink::datum : ink::label;
}

/// A remaining fraction where low is bad: fuel, lamp charge.
auto reserve_rung( int pct ) -> ink
{
    if( pct <= 20 ) {
        return ink::peak;
    }
    return pct <= 50 ? ink::datum : ink::label;
}

// ── Row assembly ────────────────────────────────────────────────────────────

/// `padded` escaped for RML, with every U+0020 turned into U+00A0.
///
/// RmlUi strips leading and trailing whitespace from an inline run at PARSE
/// time, and these producers' markup is injected through `data-rml`, which
/// parses with the element's computed style — so `white-space: pre` declared on
/// the element being created cannot preserve it. Every field becomes its own
/// `<span>`, so padding that lands at a span edge is trimmed off and a field
/// consisting only of spaces disappears entirely. In-game that welded every
/// label to its value across both status rows:
/// `DAY16Spring12:59:46mansionWXRain11°C`, `MOVEWALKINGSPD136FOC132`,
/// `STR13DEX22INT9PER20`.
///
/// Doing it here rather than at every call site is deliberate: in a fixed-cell
/// grid EVERY space is alignment, never word spacing that may be collapsed or
/// wrapped, so the conversion is unconditionally correct for anything a field
/// can hold. `hud_phosphor::pad` already fills with U+00A0 for the same reason;
/// this catches the rest.
///
/// It is a free function rather than a step buried inside one writer because
/// reverse video needs it just as much, and burying it is exactly how
/// `row::put_inverted` came to skip it: `.ph-inv` is an inline run like any
/// other, so a space inside an inverted field was trimmed at the span boundary
/// and the field rendered narrower than the cells the row had already counted
/// for it. `row::put_markup` is deliberately NOT routed through this — that
/// path carries already-rendered RML, and rewriting bytes inside markup would
/// corrupt the tags.
auto no_break( const std::string &padded ) -> std::string
{
    static constexpr std::string_view nbsp = "\u00a0";
    const auto escaped = rml_escape( padded );
    auto out = std::string();
    out.reserve( escaped.size() );
    for( const char c : escaped ) {
        if( c == ' ' ) {
            out += nbsp;
        } else {
            out += c;
        }
    }
    return out;
}

/// Accumulates one HUD row while tracking its exact width in display cells.
///
/// This exists because `hud_phosphor::pad` cannot be applied to a finished row:
/// once a field has been wrapped in `<span class="ph-i4">` the string's byte
/// length, its code-point count and its display width are three different
/// numbers, and only the last is the row's real size. So every field is padded
/// while it is still plain text, then escaped, then wrapped, and the width it
/// consumed is added to a running count that `line()` closes out against the
/// region width. A row can therefore never overrun its region, which is the
/// class of bug that put 1554 dp of text in a 1520 dp box.
class row
{
    public:
        explicit row( int cols ) : cols_( std::max( cols, 0 ) ) {}

        /// Cells still available before the row is full.
        auto left() const -> int {
            return cols_ - used_;
        }

        /// Append plain `text` at its natural width.
        auto put( ink i, std::string_view text ) -> row & // *NOPAD*
        {
            return put( i, text, hud_phosphor::display_width( text ) );
        }

        /// Append plain `text` in a field of exactly `cells`, padded or truncated.
        auto put( ink i, std::string_view text, int cells ) -> row & // *NOPAD*
        {
            return emit( i, hud_phosphor::pad( text, std::clamp( cells, 0, left() ) ) );
        }

        /// Append plain `text` right-aligned in a field of exactly `cells`.
        auto put_right( ink i, std::string_view text, int cells ) -> row & // *NOPAD*
        {
            return emit( i, hud_phosphor::pad_left( text, std::clamp( cells, 0, left() ) ) );
        }

        /// Append `text` as reverse video — the register's only shout.
        ///
        /// Padded, escaped and de-spaced exactly like every other field: an
        /// inverted run is still an inline run, so its alignment whitespace is
        /// trimmed at the span boundary unless it goes through `no_break`.
        auto put_inverted( std::string_view text, int cells ) -> row & // *NOPAD*
        {
            const auto w = std::clamp( cells, 0, left() );
            return w > 0
                   ? claim( hud_phosphor::invert( no_break( hud_phosphor::pad( text, w ) ) ), w )
                   : *this;
        }

        /// Append pre-built markup of known display width — a bar, or a nested row.
        ///
        /// The one writer that cannot measure its own argument, because the
        /// argument is markup: `cells` is the caller's promise about it. A promise
        /// that does not fit is refused rather than trusted — see `claim`.
        auto put_markup( std::string_view markup, int cells ) -> row & // *NOPAD*
        {
            return claim( markup, cells );
        }

        /// Append `cells` of blank ground. Blanks go through `pad` rather than
        /// through appended `' '` so that gap-making has exactly one owner: the
        /// padding character is `pad`'s decision (it is a NO-BREAK SPACE, which
        /// RmlUi will not trim at a span boundary), and a second literal-space
        /// path here would silently reintroduce the trimming it exists to avoid.
        /// Blanks stay outside any span: an empty tinted span says nothing.
        auto skip( int cells ) -> row & // *NOPAD*
        {
            const auto w = std::clamp( cells, 0, left() );
            return claim( hud_phosphor::pad( {}, w ), w );
        }

        /// Blank-fill forward to column `col`. Never rewinds, so a field that
        /// overran loses its trailing gap rather than corrupting the whole row.
        auto to( int col ) -> row & // *NOPAD*
        {
            return skip( col - used_ );
        }

        /// The row's cells, closed out to exactly the region width.
        auto line() const -> std::string {
            return out_ + hud_phosphor::pad( {}, std::max( cols_ - used_, 0 ) );
        }

        /// The row wrapped in its own element. The producer owns the row element
        /// because only the producer knows its own row count; the bound elements
        /// in `sidebar_hud.rml` are deliberately class-less.
        auto div( std::string_view classes = "ph-row", std::string_view id = {} ) const
        -> std::string {
            auto out = std::format( "<div class=\"{}\"", classes );
            if( !id.empty() ) {
                out += std::format( " id=\"{}\"", id );
            }
            return out + ">" + line() + "</div>";
        }

    private:
        /// The single point where `out_` grows and `used_` advances.
        ///
        /// Funnelling every writer through here is what makes non-overlap
        /// STRUCTURAL rather than a property to be re-audited whenever a `put_*`
        /// is added. A write can only ever begin at `used_`, and it can only ever
        /// claim cells the row still has, so no two writes can address the same
        /// cell — there is no code path that could place one. Before this existed
        /// the invariant was hand-rolled at four sites; one of them had already
        /// drifted, emitting markup that had not been through `no_break`.
        ///
        /// A write that does not fit is DROPPED WHOLE rather than clamped into the
        /// cells behind it. Both are safe, and the choice is deliberate: dropping
        /// is what `put_segments` already does one level up, and for the same
        /// reason — a field clipped to fit reads as a different, wrong value (`12`
        /// where the datum is `128`), while a field that is absent is visibly
        /// absent. The padded writers clamp their FIELD WIDTH before they get here
        /// so they can never reach the drop; only `put_markup`, which cannot be
        /// re-measured, can. Either way `line()` closes the row out to exactly
        /// `cols_`, so a drop costs content and never geometry.
        auto claim( std::string_view markup, int cells ) -> row & // *NOPAD*
        {
            if( cells > 0 && cells <= left() ) {
                out_ += markup;
                used_ += cells;
            }
            return *this;
        }

        /// Tint `padded` and claim the cells it measures.
        auto emit( ink i, const std::string &padded ) -> row & // *NOPAD*
        {
            const auto w = hud_phosphor::display_width( padded );
            return w > 0 ? claim( hud_phosphor::tint( i, no_break( padded ) ), w ) : *this;
        }

        std::string out_;
        int cols_ = 0;
        int used_ = 0;
};

/// One `LABEL value` pair on a status row. Both halves are upper-case: the
/// register has exactly one case, and the mockup's rows read `AUTUMN`,
/// `CHESWICK — RESIDENTIAL`, `LIGHT DRIZZLE`. The fold is the caller's, through
/// `to_upper_case`, which is UTF-8 aware — a byte-wise `std::toupper` would
/// mangle every name outside ASCII, which is the hazard that made an earlier
/// draft leave game data verbatim and the register only half-applied.
///
/// A value that is free text — an overmap tile name, a weather description, a
/// partner's character name — is capped by its *caller* with `capped()` below,
/// not here. Which values are unbounded is knowledge the call site has and this
/// struct does not, and doing it there means the cap is applied once instead of
/// once per width query.
struct segment {
    std::string label;
    std::string value;
    ink value_rung = ink::datum;
};

/// Free text, fitted to `cells` with an ellipsis if it did not fit.
///
/// The ellipsis is the point: hard truncation would leave the player reading
/// `Cheswick — residen` as though that were the name of the place. Without any
/// cap at all a thirty-cell tile name silently evicts the two telemetry fields
/// behind it off the end of the row, which is worse than either.
auto capped( const std::string &text, int cells ) -> std::string
{
    return trunc_ellipse( text, static_cast<unsigned int>( std::max( cells, 1 ) ) );
}

/// Cells a segment occupies, label and its separating space included.
auto segment_width( const segment &s ) -> int
{
    const auto label_w = s.label.empty() ? 0 : hud_phosphor::display_width( s.label ) + 1;
    return label_w + hud_phosphor::display_width( s.value );
}

/// Lay segments out left to right, dropping any that will not fit whole.
///
/// Dropping whole segments rather than truncating one mid-field is what stops a
/// long value from shifting every column to its right — the failure this grid
/// exists to make impossible. A later, shorter segment may still land after an
/// earlier one was dropped; the run stays in priority order, it just has gaps.
///
/// The separator is two cells in the mockup, whose middle field is 123 cells
/// wide. The shipping grid's is 90, and at two cells the ninth field falls off
/// the end — so the whole run is measured first and re-laid at one cell if that
/// is what lets every field in. Losing a field costs the player more than a
/// tightened gap costs legibility, and the tightening is uniform, so the row
/// still reads as a row of fields rather than as a run-on.
void put_segments( row &r, const std::vector<segment> &segs )
{
    const auto total = [&segs]( int gap ) {
        auto width = 0;
        auto first = true;
        for( const auto &s : segs ) {
            if( s.value.empty() ) {
                continue;
            }
            width += ( first ? 0 : gap ) + segment_width( s );
            first = false;
        }
        return width;
    };
    const auto gap = total( 2 ) <= r.left() ? 2 : 1;

    auto first = true;
    for( const auto &s : segs ) {
        if( s.value.empty() ) {
            continue;
        }
        const auto width = segment_width( s );
        const auto lead = first ? 0 : gap;
        if( lead + width > r.left() ) {
            continue;
        }
        r.skip( lead );
        if( !s.label.empty() ) {
            r.put( ink::label, s.label ).skip( 1 );
            r.put( s.value_rung, s.value,
                   width - hud_phosphor::display_width( s.label ) - 1 );
        } else {
            r.put( s.value_rung, s.value, width );
        }
        first = false;
    }
}

// ── Shared geometry ─────────────────────────────────────────────────────────

/// The message log's internal column grid, from the mockup:
/// `1 mark + 5 time + 1 gap + 1 gutter + 1 gap + 2 glyph + 1 gap + N text + 1 border`.
/// Everything but the text field is fixed, so the text field absorbs the whole
/// of any width difference and the gutter never moves — which matters because
/// a *different* row, `hud_keys_rule`, puts a junction glyph on that column.
constexpr int log_time_cells = 5;
constexpr int log_gutter_col = 1 + log_time_cells + 1;
constexpr int log_glyph_cells = 2;
constexpr int log_text_col = log_gutter_col + 1 + 1 + log_glyph_cells + 1;

/// The function-key grid: `1 pad + 9 x (3 bracket + 1 key + 1 gap + L label) + 2`.
constexpr int keys_lead_cells = 1;
constexpr int keys_tail_cells = 2;
constexpr int keys_slot_count = 9;
constexpr int keys_fixed_cells = 3 + 1 + 1;

/// A title's `┤` sits two cells right of the junction preceding it, so the rule
/// reads `──┤ TITLE ├──` instead of butting the bracket against a corner. At a
/// region's own left edge there is no junction, so there it is one cell.
constexpr int title_offset = 2;

// ── Status helpers ──────────────────────────────────────────────────────────

/// The clock as `HH:MM`, honouring the `24_HOUR` option exactly as
/// `to_string_time_of_day` does, minus the seconds.
///
/// Seconds are the one field on this row that changes every frame and never
/// changes a decision, and the mockup prints `21:47`. The log's five-cell
/// timestamp is NOT a formatter worth reusing for this: it calls
/// `to_string_time_of_day` too and merely truncates, which happens to read as
/// `HH:MM` on a 24-hour clock and mangles `9:59:46AM` into `9:59:` on a
/// 12-hour one. The format strings stay translated, because that is how the
/// original expresses a locale that separates or orders time differently.
auto clock_hm( const time_point &p ) -> std::string
{
    const auto hour = hour_of_day<int>( p );
    const auto minute = minute_of_hour<int>( p );
    const auto format_type = get_option<std::string>( "24_HOUR" );
    if( format_type == "military" ) {
        return string_format( "%02d%02d", hour, minute );
    }
    if( format_type == "24h" ) {
        //~ hour:minute (24hr time display)
        return string_format( _( "%02d:%02d" ), hour, minute );
    }
    const auto hour12 = hour % 12 == 0 ? 12 : hour % 12;
    if( hour < 12 ) {
        //~ hour:minute AM (12hr time display)
        return string_format( _( "%d:%02dAM" ), hour12, minute );
    }
    //~ hour:minute PM (12hr time display)
    return string_format( _( "%d:%02dPM" ), hour12, minute );
}

/// Coarse clock for a character with no watch. Copied from `panels.cpp`'s
/// file-static `time_approx` for the same reason as the threshold helpers: the
/// bands are the design, and this TU cannot reach the original.
auto approx_time_of_day() -> std::string
{
    const auto hour = hour_of_day<int>( calendar::turn );
    if( hour >= 23 || hour <= 1 ) {
        return _( "Around midnight" );
    } else if( hour <= 4 ) {
        return _( "Dead of night" );
    } else if( hour <= 6 ) {
        return _( "Around dawn" );
    } else if( hour <= 8 ) {
        return _( "Early morning" );
    } else if( hour <= 10 ) {
        return _( "Morning" );
    } else if( hour <= 13 ) {
        return _( "Around noon" );
    } else if( hour <= 16 ) {
        return _( "Afternoon" );
    } else if( hour <= 18 ) {
        return _( "Early evening" );
    } else if( hour <= 20 ) {
        return _( "Around dusk" );
    }
    return _( "Night" );
}

/// Full move-mode word. `panels.cpp`'s `move_mode_string` returns one letter,
/// for a curses sidebar that had one cell to spend; this strip has room for the
/// word, and a word needs no legend.
auto move_mode_word( const avatar &u ) -> std::string
{
    if( u.movement_mode_is( CMM_RUN ) ) {
        return _( "RUNNING" );
    } else if( u.movement_mode_is( CMM_STEALTH ) ) {
        return _( "STEALTH" );
    } else if( u.movement_mode_is( CMM_CROUCH ) ) {
        return _( "CROUCHING" );
    }
    return _( "WALKING" );
}

/// The brightest light the avatar is actually carrying, and how much is left of
/// it. Only *active* emitters are found: an unlit flashlight is a separate itype
/// whose `light_emission` is zero, which is the same test `Character::active_light`
/// relies on.
struct lamp_state {
    bool lit = false;
    std::optional<int> pct;   ///< absent for an emitter with no magazine, e.g. a lit candle
};

auto brightest_lamp( const avatar &u ) -> lamp_state
{
    auto best_lum = 0;
    auto state = lamp_state{};
    u.has_item_with( [&]( const item & it ) {
        const auto lum = it.getlight_emit();
        if( lum <= best_lum ) {
            return false;
        }
        best_lum = lum;
        state.lit = true;
        // A glowstick or a lit candle has no magazine at all; `getlight_emit`
        // itself guards on this before dividing, and so must we.
        const auto capacity = it.ammo_capacity();
        state.pct = capacity > 0
                    ? std::optional<int>( std::clamp( it.ammo_remaining() * 100 / capacity, 0, 100 ) )
                    : std::nullopt;
        return false; // visit everything: this is a maximum, not a search
    } );
    return state;
}

/// Wind as a compass point plus a speed in the player's chosen units. Reuses the
/// game's own angle table so this strip and the weather screen can never
/// disagree about which way the wind is blowing.
auto wind_text() -> std::string
{
    const auto &w = get_weather();
    if( w.windspeed <= 0 ) {
        return _( "CALM" );
    }
    const auto vec = convert_wind_to_coord( w.winddirection );
    const auto dir = direction_from( point( static_cast<int>( vec.x ),
                                            static_cast<int>( vec.y ) ) );
    // Upper-cased whole, so the unit reads `KM/H` with the compass point rather
    // than dropping into lower case halfway through one field.
    return to_upper_case( std::format( "{} {:.0f}{}", direction_name_short( dir ),
                                       convert_velocity( w.windspeed, VU_WIND ),
                                       velocity_units( VU_WIND ) ) );
}

/// How lit the avatar's own tile is, as the game's own six-band description.
///
/// The mockup prints `LUX 12%`, but the engine has no percentage to print —
/// `ambient_light_at` is an unbounded float — so inventing a denominator would
/// be inventing data. The band word is the honest datum and is already
/// translated; the rung carries the same severity the percentage would have.
auto light_text( const avatar &u ) -> std::pair<std::string, ink>
{
    const auto ambient = get_map().ambient_light_at( u.bub_pos() );
    const auto shade = std::max( 1.0f, LIGHT_AMBIENT_LIT - ambient + 1.0f );
    // `get_light_level`'s array runs bright -> very dark, so a larger shade is
    // a darker tile, and dark is what changes what you can safely do next.
    const auto rung = shade >= 5.0f ? ink::peak : shade >= 3.0f ? ink::datum : ink::label;
    return { get_light_level( shade ).first, rung };
}

/// Whole-body warmth as a word and a rung.
///
/// The engine has no warmth *description* to borrow — `warmth::bodytemp_color`
/// is a colour and nothing else — so the words are set here against that
/// function's own `BODYTEMP_*` bands, in the vocabulary the game already prints
/// when a part crosses one ("getting chilly", "getting very cold"). Torso
/// temperature is the whole-body reading: it is the part the shivering and
/// hypothermia rules consult, so it is the one that means "you".
auto warmth_text( const avatar &u ) -> std::pair<std::string, ink>
{
    const auto t = u.get_part_temp_cur( body_part_torso.id() );
    if( t > BODYTEMP_SCORCHING ) {
        return { _( "SCORCHING" ), ink::peak };
    } else if( t > BODYTEMP_VERY_HOT ) {
        return { _( "VERY HOT" ), ink::peak };
    } else if( t > BODYTEMP_HOT ) {
        return { _( "HOT" ), ink::datum };
    } else if( t > BODYTEMP_COLD ) {
        return { _( "NORMAL" ), ink::label };
    } else if( t > BODYTEMP_VERY_COLD ) {
        return { _( "CHILLY" ), ink::datum };
    } else if( t > BODYTEMP_FREEZING ) {
        return { _( "VERY COLD" ), ink::peak };
    }
    return { _( "FREEZING" ), ink::peak };
}

/// A need as a status segment: its own description, or the register's word for
/// "nothing to report".
///
/// `get_thirst_description` and `get_fatigue_description` return an EMPTY
/// string inside their comfortable band, and the strip used to drop the whole
/// field when they did. Three of the four need columns were therefore missing
/// whenever the avatar was fine, and the ones that were left shifted sideways
/// every time another came back — in a fixed-cell grid that is the one failure
/// mode the grid exists to prevent. All four are unconditional here, and a need
/// with nothing to report says so one rung down, which is also the only reading
/// that lets the player tell "not thirsty" from "the row ran out of room".
auto need_field( std::string label, const std::pair<std::string, nc_color> &desc,
                 int cells ) -> segment
{
    if( desc.first.empty() ) {
        return { .label = std::move( label ), .value = _( "NORMAL" ), .value_rung = ink::label };
    }
    return { .label = std::move( label ),
             .value = capped( to_upper_case( desc.first ), cells ),
             .value_rung = ink::datum };
}

/// The avatar's profession, preferring one they wrote for themselves.
auto profession_text( const avatar &u ) -> std::string
{
    if( !u.custom_profession.empty() ) {
        return u.custom_profession;
    }
    return u.prof ? u.prof->gender_appropriate_name( u.male ) : std::string();
}

/// Partner state for the co-op segment; empty when there is no session.
///
/// Not in the mockup, whose fixture is single-player. Carried anyway, because
/// the shipping status row carried it and a partner's HP falling is exactly the
/// class of thing a status strip exists to report.
auto coop_text() -> std::string
{
    const auto &sess = coop_session::get();
    if( !sess.is_coop() ) {
        return std::string();
    }
    const auto has_partner = !sess.is_host() ||
                             ( g->coop_server_ != nullptr && g->coop_server_->has_client() );
    if( !has_partner ) {
        return _( "WAITING" );
    }
    auto out = std::format( "{} {}%", to_upper_case( sess.partner_name ), sess.partner_hp_pct );
    if( const auto ping = sess.partner_ping_ms.load(); ping > 0 ) {
        out += std::format( " {}ms", ping );
    }
    if( sess.is_host() && g->coop_server_ != nullptr && g->coop_server_->awaiting_reconnect() ) {
        out += " " + std::string( _( "RECONNECTING" ) );
    }
    return out;
}

/// The populated throw quick-slots as `1x4 2x12`. Also absent from the mockup's
/// fixture, also carried, for the same reason as the co-op segment.
auto throw_text( const avatar &u ) -> std::string
{
    auto out = std::string();
    for( const auto i : std::views::iota( 0, avatar::MAX_THROW_SLOTS ) ) {
        if( u.is_throw_slot_empty( i ) ) {
            continue;
        }
        if( !out.empty() ) {
            out += " ";
        }
        out += std::format( "{}x{}", i + 1, u.count_throwable( i ) );
    }
    return out;
}

/// Both status text rows share this frame: a left field ending on SOMA's right
/// border, a middle field, and a right field starting at DOCK's left border.
/// The two verticals drawn here are crossed by `hud_status_rule` one row down,
/// and they land on the same columns because all three read one layout.
struct status_frame {
    int cols = 0;
    int left_edge = 0;   ///< column of the vertical over SOMA's right border
    int right_edge = 0;  ///< column of the vertical over DOCK's left border
};

/// The right column's stop. RADAR is the topmost right-hand region and DOCK sits
/// under it in the same columns, so either answers — but on a short grid the one
/// that gets clamped to an empty rect reports col 0, which would drag the status
/// strip's vertical to the screen edge. Prefer whichever is actually present.
auto right_column( const hud_phosphor::layout &l ) -> hud_phosphor::cell_rect
{
    return l.radar.cols > 0 ? l.radar : l.dock;
}

auto frame_of( const hud_phosphor::layout &l ) -> status_frame
{
    const auto cols = std::max( l.status.cols, 1 );
    const auto left_edge = std::clamp( l.soma.col + l.soma.cols - 1 - l.status.col, 0, cols - 1 );
    const auto right_edge = std::clamp( right_column( l ).col - l.status.col, left_edge + 1,
                                        cols - 1 );
    return { .cols = cols, .left_edge = left_edge, .right_edge = right_edge };
}

/// One of the four attribute cells on status row 2. Carries the unmodified base
/// alongside the current value, because in this register a stat's *deviation*
/// from its base is the whole message and the number alone cannot carry it.
struct stat_field {
    std::string label;
    int base = 0;
    int value = 0;
};

// ── Function keys ───────────────────────────────────────────────────────────

/// A key strip slot: what it does, what it is called, and whether pressing it
/// right now would achieve anything.
struct key_slot {
    action_id act = ACTION_NULL;
    std::string label;
    std::string reason;      ///< why it is unavailable; empty when it is available
    bool available = true;
};

/// Why the FIRE slot would do nothing, if it would do nothing.
auto fire_block_reason( const avatar &u ) -> std::string
{
    const item &weapon = u.primary_weapon();
    if( !weapon.is_gun() ) {
        return _( "NO GUN" );
    }
    return weapon.ammo_sufficient() ? std::string() : _( "NO AMMO" );
}

/// Why the RELOAD slot would do nothing, if it would do nothing.
auto reload_block_reason( const avatar &u ) -> std::string
{
    return u.primary_weapon().is_reloadable() ? std::string() : _( "NO MAG" );
}

auto key_slots( const avatar &u ) -> std::array<key_slot, keys_slot_count>
{
    // Availability is only decided where the avatar actually answers the
    // question. A slot whose precondition is not cheaply knowable stays enabled:
    // rendering a usable action dead is the worse lie of the two, because the
    // player can discover an unusable live slot just by pressing it.
    auto fire = fire_block_reason( u );
    auto reload = reload_block_reason( u );
    const auto fire_ok = fire.empty();
    const auto reload_ok = reload.empty();
    return {{
            { .act = ACTION_FIRE, .label = _( "FIRE" ), .reason = std::move( fire ), .available = fire_ok },
            {   .act = ACTION_RELOAD_WIELDED, .label = _( "RELOAD" ), .reason = std::move( reload ),
                .available = reload_ok
            },
            { .act = ACTION_TOGGLE_RUN, .label = _( "RUN" ) },
            { .act = ACTION_EXAMINE, .label = _( "EXAMINE" ) },
            { .act = ACTION_PICKUP, .label = _( "PICK UP" ) },
            { .act = ACTION_CRAFT, .label = _( "CRAFT" ) },
            { .act = ACTION_INVENTORY, .label = _( "INVENTORY" ) },
            { .act = ACTION_THROW, .label = _( "THROW" ) },
            // WAIT is `pause`, not `wait`. `wait` opens the wait-for-N-minutes
            // menu and its default binding is `|` (`keybindings.json`), which in
            // this register is a box-drawing stroke: on screen `[5]|` read as a
            // stray rule fragment rather than as a key. The mockup's WAIT slot
            // shows `5`, the numpad key only `pause` carries, so the action the
            // design asks for is the roguelike wait-a-turn — which is also the
            // one worth a permanent place on a nine-slot strip.
            { .act = ACTION_PAUSE, .label = _( "WAIT" ) },
        }};
}

/// The key bound to `act`, or nothing when it is unbound.
///
/// `input_context::get_desc` has no "leave it blank" mode: with no binding at
/// all it returns the literal sentence `Unbound globally!`, and with every
/// binding filtered out it returns `Disabled` (`input.cpp:769` and `:785`). The
/// shipping hotbar printed those straight into the strip — three of nine slots
/// on a default keymap, 1554 dp of text in a 1520 dp box, which is bug 5 in the
/// redesign contract.
///
/// `game.cpp:1800` hits the same wall for its context-menu hints and answers it
/// with `keys_bound_to( ident ).empty()`. That is deliberately not what happens
/// here, because `keys_bound_to` only reports single *printable keyboard* keys:
/// an action bound solely to F5, to a gamepad button or to a multi-key sequence
/// would be blanked as unbound when it is nothing of the sort. Matching the
/// sentinels against the very literals `get_desc` builds them from is the exact
/// test, and it also catches `Disabled`, which a boundness check cannot see.
auto bound_key( const input_context &ctxt, action_id act ) -> std::optional<std::string>
{
    const auto desc = ctxt.get_desc( action_ident( act ), 1 );
    if( desc.empty() || desc == _( "Unbound globally!" ) || desc == _( "Unbound locally!" )
        || desc == pgettext( "keybinding", "Disabled" ) ) {
        return std::nullopt;
    }
    return desc;
}

// ── Vehicle ─────────────────────────────────────────────────────────────────

/// Compass point for a vehicle's facing, in `tileray::dir8`'s ordering.
auto heading_word( int dir8 ) -> std::string
{
    static const std::array<const char *, 8> compass = {
        translate_marker( "E" ), translate_marker( "SE" ), translate_marker( "S" ),
        translate_marker( "SW" ), translate_marker( "W" ), translate_marker( "NW" ),
        translate_marker( "N" ), translate_marker( "NE" ),
    };
    return dir8 >= 0 && dir8 < 8 ? _( compass[dir8] ) : std::string( "?" );
}

} // namespace

// ── Status strip ────────────────────────────────────────────────────────────

auto hud_status_row1( avatar &u, const hud_phosphor::layout &l ) -> std::string
{
    const auto f = frame_of( l );
    row r( f.cols );

    // Left field: who you are. The name is peak because it anchors the eye at
    // column zero; the profession recedes to chrome because it never changes.
    //
    // The pair is laid out as the mockup lays it out — name at its natural
    // width, profession two cells behind it — rather than in a fixed name
    // field. The fixed field is what rendered `GUY MCCLENDON` as
    // `GUY MCCLENDO`: its width was a ratio of `left_edge`, so the only names
    // that fit were the ones the mockup's own fixture happened to have.
    // Nesting the pair in a row of its own is what makes that safe: a long
    // name can only cost the profession its room, never the column the
    // vertical stands on.
    r.skip( 1 );
    const auto ident_cells = std::max( f.left_edge - 1, 0 );
    row ident( ident_cells );
    ident.put( ink::peak, capped( to_upper_case( u.get_name() ), ident_cells ) );
    ident.skip( 2 );
    ident.put( ink::label, to_upper_case( profession_text( u ) ), ident.left() );
    r.put_markup( ident.line(), ident_cells );
    r.to( f.left_edge );
    r.put( ink::rule, "\u2502" );

    // Middle field: where and when you are.
    const auto &weather = get_weather();
    const auto temp = weather.get_temperature( u.abs_pos() );
    const auto lamp = brightest_lamp( u );
    const auto [light_word, light_rung] = light_text( u );

    // Every unbounded string in this field competes for the same cells, so one
    // cap serves them all and it is a share of the field rather than a constant
    // tuned to one grid width. At the design's 192 columns that is 24 cells,
    // which is what the mockup's `CHESWICK — RESIDENTIAL` needs; on a narrower
    // grid it tightens rather than pushing the telemetry fields off the end.
    const auto middle_cols = std::max( f.right_edge - f.left_edge - 2, 0 );
    const auto free_cells = std::clamp( middle_cols / 5, 6, 28 );

    // Ordered by how often the field changes a decision, because that is also the
    // order they get dropped in when the region is too narrow to hold them all.
    // The place name, the weather description and a partner's character name are
    // the only unbounded strings here, so each is capped.
    auto segs = std::vector<segment>{};
    if( auto coop = coop_text(); !coop.empty() ) {
        segs.push_back( { .label = _( "CO-OP" ), .value = capped( coop, free_cells ),
                          .value_rung = ink::peak } );
    }
    segs.push_back( { .label = _( "DAY" ),
                      .value = std::to_string( day_of_season<int>( calendar::turn ) + 1 ) } );
    const auto season = calendar::name_season( season_of_year( calendar::turn ) );
    segs.push_back( { .label = {}, .value = to_upper_case( season ) } );
    // Only the clock is peak in this row's middle field, per the register: it is
    // the one number here that changes what you should be doing.
    segs.push_back( { .label = {},
                      .value = u.has_watch() ? clock_hm( calendar::turn )
                      : g->get_levz() >= 0 ? to_upper_case( approx_time_of_day() )
                      : std::string( "???" ),
                      .value_rung = ink::peak } );
    const auto place = ACTIVE_OVERMAP_BUFFER.ter( u.abs_omt_pos() )->get_name();
    segs.push_back( { .label = {}, .value = capped( to_upper_case( place ), free_cells ) } );
    if( g->get_levz() < 0 ) {
        segs.push_back( { .label = {}, .value = std::format( "-{}Z", -g->get_levz() ),
                          .value_rung = ink::label } );
    }
    const auto wx = weather.weather_id->name.translated();
    segs.push_back( { .label = _( "WX" ), .value = capped( to_upper_case( wx ), free_cells ) } );
    segs.push_back( { .label = {}, .value = print_temperature( temp ),
                      .value_rung = temp_rung( temp ) } );
    segs.push_back( { .label = {}, .value = wind_text() } );
    segs.push_back( { .label = _( "LUX" ), .value = to_upper_case( light_word ),
                      .value_rung = light_rung } );
    if( lamp.lit ) {
        segs.push_back( { .label = _( "LAMP" ),
                          .value = lamp.pct ? std::format( "{}%", *lamp.pct ) : std::string( _( "ON" ) ),
                          .value_rung = lamp.pct ? reserve_rung( *lamp.pct ) : ink::datum } );
    }

    row middle( middle_cols );
    put_segments( middle, segs );
    r.skip( 1 );
    r.put_markup( middle.line(), middle_cols );
    r.to( f.right_edge );
    r.put( ink::rule, "\u2502" );

    // Right field: safe mode. On is peak; off is the register's one shout, because
    // "safe mode is off" is the single piece of status that gets people killed,
    // and an inverted cell survives greyscale, colourblindness and a dim panel.
    r.skip( 1 );
    r.put( ink::label, _( "SAFE MODE" ) );
    const auto safe_on = g->safe_mode != SAFE_MODE_OFF;
    const auto field = std::max( r.left() - 1, 0 );
    if( safe_on ) {
        r.put_right( ink::peak, _( "ON" ), field );
    } else {
        const std::string off = _( "OFF" );
        const auto off_w = std::min( hud_phosphor::display_width( off ), field );
        r.skip( field - off_w );
        r.put_inverted( off, off_w );
    }

    return r.div();
}

auto hud_status_row2( avatar &u, const hud_phosphor::layout &l ) -> std::string
{
    const auto f = frame_of( l );
    row r( f.cols );

    // Left field: the four stats, each in a fixed-width cell so none of them can
    // shuffle sideways when a neighbour gains a digit.
    //
    // The cell width is derived from the field rather than floored at a
    // comfortable minimum. A floor looks harmless and is not: on a narrow grid
    // four floored cells outrun `left_edge`, `to()` refuses to rewind, and this
    // row's vertical lands a cell or two right of row 1's and of the rule's
    // crossing — three glyphs that are supposed to be one stroke. Below six
    // cells the labels are dropped instead, because a right-aligned bare number
    // still reads in column order and a truncated `ST` does not.
    r.skip( 1 );
    const auto stat_cells = std::max( ( f.left_edge - 1 ) / 4, 1 );
    const auto labelled = stat_cells >= 6;
    const auto stats = std::array<stat_field, 4>{{
            { .label = _( "STR" ), .base = u.get_str_base(), .value = u.get_str() },
            { .label = _( "DEX" ), .base = u.get_dex_base(), .value = u.get_dex() },
            { .label = _( "INT" ), .base = u.get_int_base(), .value = u.get_int() },
            { .label = _( "PER" ), .base = u.get_per_base(), .value = u.get_per() },
        }};
    for( const stat_field &s : stats ) {
        const auto cells = std::min( stat_cells, r.left() );
        row cell( cells );
        if( labelled ) {
            cell.put( ink::label, s.label ).skip( 1 );
        }
        // Left-aligned, so the cell's slack falls between stats as a gap rather
        // than butting one stat's digits against the next stat's label.
        cell.put( stat_rung( s.base, s.value ), std::to_string( s.value ) );
        r.put_markup( cell.line(), cells );
    }
    r.to( f.left_edge );
    r.put( ink::rule, "\u2502" );

    // Middle field: how you are moving and how you feel. Ordered by how often a
    // field changes a decision, because that is also the order they get dropped
    // in when the region is too narrow to hold all of them.
    //
    // FOCUS, PAIN and MORALE are deliberately absent. Focus and morale are rows
    // of the SOMA panel's POOLS section and pain is an EFFECTS entry, so
    // carrying them here said everything twice and cost this row the four need
    // fields it exists for.
    const auto enc_torso = u.encumb( body_part_torso );
    const auto enc_arms = std::max( u.encumb( body_part_arm_l ), u.encumb( body_part_arm_r ) );
    const auto [warmth_word, warmth_rung] = warmth_text( u );

    const auto middle_cols = std::max( f.right_edge - f.left_edge - 2, 0 );
    // Four need descriptions compete for this field at once, so their cap is a
    // tenth of it rather than the whole-field share row 1 gives its one place
    // name. At 192 columns that is 12 cells, which holds `VERY HUNGRY` and
    // `DEAD TIRED` — the longest the engine emits — without an ellipsis.
    const auto need_cells = std::clamp( middle_cols / 10, 6, 14 );

    auto segs = std::vector<segment>{
        { .label = _( "MOVE" ), .value = move_mode_word( u ) },
        {   .label = _( "SPD" ), .value = std::to_string( u.get_speed() ),
            .value_rung = speed_rung( u.get_speed() )
        },
        { .label = _( "NOISE" ), .value = std::to_string( u.volume ) },
        // Two segments rather than one `ENC 42/14`. A shared solidus reads as a
        // fraction, and these are two independent loads on two different sets
        // of limbs; split, a narrow row drops ARMS and still says what the
        // torso is carrying.
        { .label = _( "ENC TORSO" ), .value = std::to_string( enc_torso ) },
        { .label = _( "ARMS" ), .value = std::to_string( enc_arms ) },
        need_field( _( "HUNGER" ), u.get_hunger_description(), need_cells ),
        need_field( _( "THIRST" ), u.get_thirst_description(), need_cells ),
        need_field( _( "FATIGUE" ), u.get_fatigue_description(), need_cells ),
        { .label = _( "WARMTH" ), .value = warmth_word, .value_rung = warmth_rung },
    };
    if( u.weight_carried() > u.weight_capacity() ) {
        segs.push_back( { .label = {}, .value = _( "OVERBURDENED" ), .value_rung = ink::peak } );
    }
    if( auto thrown = throw_text( u ); !thrown.empty() ) {
        segs.push_back( { .label = _( "THROW" ), .value = std::move( thrown ) } );
    }

    row middle( middle_cols );
    put_segments( middle, segs );
    r.skip( 1 );
    r.put_markup( middle.line(), middle_cols );
    r.to( f.right_edge );
    r.put( ink::rule, "\u2502" );

    // Right field: what is looking at you. VIS is every hostile you can see;
    // TRACK is the subset inside the combat bubble, i.e. the ones close enough
    // to be acting on you this turn. Each count carries its own word: a bare
    // `3 · 1` needs a legend the row has no room to print, and this one does
    // not.
    const auto &seen = u.get_mon_visible();
    const auto visible = seen.nearby_hostile_count;
    const auto tracking = seen.combat_hostile_count;
    r.skip( 1 );
    r.put( ink::label, _( "HOSTILE" ) );
    const auto summary = visible > 0
                         ? std::format( "{} {} \u00b7 {} {}", visible, _( "VIS" ),
                                        tracking, _( "TRACK" ) )
                         : std::string( _( "NONE" ) );
    r.put_right( visible > 0 ? ink::peak : ink::label, summary, std::max( r.left() - 1, 0 ) );

    return r.div();
}

auto hud_status_rule( const hud_phosphor::layout &l ) -> std::string
{
    // This one rule carries BOTH panel titles. SOMA and DOCK draw no top rule of
    // their own, so the status strip is literally their header: the crossings
    // land on the same columns as the verticals in the two rows above and as the
    // panel borders in the rows below, and they do so because all three read the
    // same layout object rather than three copies of the same ratio.
    const auto f = frame_of( l );
    const auto soma_title = std::clamp( l.soma.col - l.status.col + 1, 0, f.cols - 1 );
    const auto dock_title = std::clamp( right_column( l ).col - l.status.col + title_offset, 0,
                                        f.cols - 1 );

    return "<div class=\"ph-row\">" + hud_phosphor::rule( {
        .cols = f.cols,
        .titles = { { .col = soma_title, .text = _( "SOMA" ) },
            { .col = dock_title, .text = _( "RADAR" ) }
        },
        .crossings = { f.left_edge, f.right_edge },
        .crossing_glyph = "\u253c",
    } ) + "</div>";
}

// ── Message log ─────────────────────────────────────────────────────────────

auto hud_log_rows( const std::vector<Messages::rich_message> &msgs,
                   const hud_phosphor::layout &l ) -> std::string
{
    const auto cols = l.log.cols;
    // A region with no rows gets no rule either: `layout_for` shrinks the log
    // first on a short viewport, and a lone rule in a zero-row box would be one
    // more row than the region was budgeted for.
    if( cols <= log_text_col + 1 || l.log.rows <= 0 ) {
        return std::string();
    }
    const auto text_cells = cols - log_text_col - 1;
    const auto border_col = cols - 1;

    // Row one is the log's own titled top rule: a `┬` opening the timestamp
    // gutter and a `┐` closing the box. `hud_keys_rule` supplies the matching
    // pair of `┴` from below, which is why both of them read the log's geometry.
    auto out = "<div class=\"ph-row\">" + hud_phosphor::rule( {
        .cols = cols,
        .titles = { { .col = log_gutter_col + title_offset, .text = _( "MESSAGE LOG" ) } },
        .crossings = { log_gutter_col },
        .right = "\u2510",
    } ) + "</div>";

    // The age ramp IS the phosphor-persistence curve, and it is four ladder
    // rungs rather than a pre-multiplied inline opacity. Two reasons: an inline
    // opacity fights `hud_anim`'s entry tween, which owns this element's inline
    // opacity, and it would not survive the ladder being re-tuned live from the
    // F4 Theme tab, because the fade would have been baked in C++.
    static constexpr std::array<ink, 4> ramp = { ink::peak, ink::datum, ink::label, ink::rule };

    // The well is sized to its line count rather than to the sidebar's height,
    // which is where this design's whole occlusion saving comes from: the
    // shipping log was 752 dp of trough holding 267 dp of message.
    //
    // `sidebar_hud_sync` has already trimmed `msgs` to `l.log.rows - 1`, dropping
    // the oldest, so that the animation keys, the rebuild guard and these rows
    // all describe the same set. The clamp below is therefore expected never to
    // fire; it stays because a producer that silently overruns its region is the
    // exact failure this grid was built to make impossible, and one `min` is a
    // cheap price for that being structurally true rather than true by protocol.
    const auto capacity = static_cast<std::size_t>( std::max( l.log.rows - 1, 0 ) );
    const auto skipped = msgs.size() - std::min( msgs.size(), capacity );

    // Indexed rather than `views::enumerate`d: the ramp is chosen by distance
    // from the newest row, so the index is the datum, not bookkeeping.
    for( const auto i : std::views::iota( skipped, msgs.size() ) ) {
        const Messages::rich_message &m = msgs[i];
        const auto from_newest = msgs.size() - 1 - i;
        const auto rung = ramp[std::min( from_newest, ramp.size() - 1 )];
        const auto newest = from_newest == 0;

        // Glyph tier is severity, luminance is recency. The two encodings never
        // contend because they are on different channels.
        const auto *glyph = m.type == m_good ? "+"
                            : m.type == m_bad ? "!"
                            : m.type == m_warning ? "^"
                            : "\u00b7";

        row r( cols );
        r.put( ink::peak, newest ? ">" : " " );
        r.put( rung, m.time, log_time_cells );
        r.skip( 1 );
        r.put( ink::rule, "\u2502" );
        r.skip( 1 );
        r.put( rung, glyph, log_glyph_cells );
        r.skip( 1 );

        // Messages arrive carrying embedded `<color_*>` tags. They are stripped
        // rather than rendered: a second hue is precisely what this register does
        // not have, and the message's severity is already on its glyph.
        auto body = remove_color_tags( m.text );
        if( newest && hud_phosphor::display_width( body ) + 2 <= text_cells ) {
            body += " \u2588"; // the cursor block marks where the log is writing
        }
        r.put( rung, body, text_cells );
        r.to( border_col );
        r.put( ink::rule, "\u2502" );

        // `hud-log-entry` and `log-<seq>` are load-bearing: `hud_anim` feeds and
        // forgets rows by that exact id, so an index-based id or a dropped class
        // silently kills the entry animation while leaving the layout perfect.
        const auto *classes = newest ? "ph-row hud-log-entry hud-log-fresh"
                              : "ph-row hud-log-entry";
        out += r.div( classes, std::format( "log-{}", m.seq ) );
    }
    return out;
}

// ── Function keys ───────────────────────────────────────────────────────────

auto hud_keys_rule( const hud_phosphor::layout &l ) -> std::string
{
    // One row doing two jobs. It titles the function-key strip, and its two `┴`
    // close the message log's box from below — they are the counterparts of the
    // `┬` and the `┐` that `hud_log_rows` puts on the log's own top rule. That is
    // the whole reason the log's geometry has to be visible from here.
    const auto cols = std::max( l.keys.cols, 1 );
    const auto gutter = std::clamp( l.log.col + log_gutter_col - l.keys.col, 0, cols - 1 );
    const auto log_edge = std::clamp( l.log.col + l.log.cols - 1 - l.keys.col, gutter + 1, cols - 1 );

    return "<div class=\"ph-row\">" + hud_phosphor::rule( {
        .cols = cols,
        .titles = { { .col = log_edge + title_offset, .text = _( "FUNCTION KEYS" ) } },
        .crossings = { gutter, log_edge },
        .crossing_glyph = "\u2534",
    } ) + "</div>";
}

auto hud_keys( avatar &u, const hud_phosphor::layout &l ) -> std::string
{
    const auto cols = l.keys.cols;
    row r( cols );
    r.skip( keys_lead_cells );

    const auto slot_cells = std::max( ( cols - keys_lead_cells - keys_tail_cells ) / keys_slot_count,
                                      keys_fixed_cells + 2 );
    const auto label_cells = slot_cells - keys_fixed_cells;

    const input_context ctxt = get_default_mode_input_context();

    const auto slots = key_slots( u );
    for( const auto i : std::views::iota( std::size_t{ 0 }, slots.size() ) ) {
        const key_slot &slot = slots[i];
        if( r.left() < slot_cells ) {
            break;
        }
        const auto key = bound_key( ctxt, slot.act );
        // Three states, three redundant markers, every one of them luminance:
        // live (bracket at rule, index at label, key at peak, label at datum),
        // unavailable, and unbound. The last two both collapse to `ink::dead`
        // because the player's next move is the same either way — this key is
        // not going to help right now.
        const auto live = key.has_value() && slot.available;
        const auto bracket = live ? ink::rule : ink::dead;

        // The key cell says one of exactly three things: the key itself; a `-`
        // for a key that is bound but would achieve nothing right now; and
        // blank ground for an action with no binding at all. Nothing else can
        // reach it, because `get_desc`'s only non-key returns are its three
        // sentence-shaped sentinels and `bound_key` catches all three — so
        // neither `[Unbound globally!]` nor a placeholder glyph standing in for
        // a key that does not exist can come back.
        const auto key_glyph = !key.has_value() ? std::string( " " )
                               : slot.available ? *key
                               : std::string( "-" );

        row cell( slot_cells );
        cell.put( bracket, "[" );
        cell.put( live ? ink::label : ink::dead, std::to_string( i + 1 ) );
        cell.put( bracket, "]" );
        cell.put( live ? ink::peak : ink::dead, key_glyph, 1 );
        cell.skip( 1 );
        // An unavailable slot spends its label field saying why, right next to
        // the action it is about, rather than leaving the player to guess. The
        // separator is the mockup's two cells whenever the pair still fits and
        // one when it does not — MEASURED against the field rather than
        // inferred from its width, which is what the `>= 16` test it replaces
        // got wrong: on a 12-cell field it picked the one-cell form and then
        // overran it anyway, putting `RELOAD NO MA` on screen butted against
        // the next slot's bracket. The widest text any slot can emit is
        // `RELOAD  NO MAG` at 14 cells; the design's 21-cell slot leaves 16.
        const auto text = [&]() -> std::string {
            if( slot.reason.empty() ) {
                return slot.label;
            }
            const auto spaced = std::format( "{}  {}", slot.label, slot.reason );
            return hud_phosphor::display_width( spaced ) <= label_cells
                   ? spaced
                   : std::format( "{} {}", slot.label, slot.reason );
        }();
        cell.put( live ? ink::datum : ink::dead, text, label_cells );
        r.put_markup( cell.line(), slot_cells );
    }

    return r.div();
}

// ── Vehicle ─────────────────────────────────────────────────────────────────

auto hud_veh_panel( avatar &u, const hud_phosphor::layout &l ) -> std::string
{
    if( !u.controlling_vehicle || l.vehicle.cols <= 2 || l.vehicle.rows <= 0 ) {
        return std::string();
    }
    const vehicle *veh = veh_pointer_or_null( get_map().veh_at( u.bub_pos() ) );
    if( veh == nullptr ) {
        return std::string();
    }

    const auto cols = l.vehicle.cols;
    const auto content_cols = cols - 2;
    auto rows = std::vector<std::string>{};

    // The panel hangs under the DOCK column, so like the DOCK it draws only the
    // edge facing the play area — its left — and opens on an interrupting rule.
    rows.push_back( hud_phosphor::rule( {
        .cols = cols,
        .titles = { { .col = title_offset, .text = _( "VEHICLE" ) } },
        .left = "\u250c",
    } ) );

    // Every content row reserves the closing rule's own row. `layout_for` clamps
    // `l.vehicle.rows` first on a short grid, so the panel has to truncate from
    // the bottom rather than spill into the region beneath it.
    const auto content_row = [&]( auto && fill ) {
        if( static_cast<int>( rows.size() ) + 1 >= l.vehicle.rows ) {
            return;
        }
        row r( cols );
        r.put( ink::rule, "\u2502" );
        r.skip( 1 );
        row body( content_cols );
        fill( body );
        r.put_markup( body.line(), content_cols );
        rows.push_back( r.line() );
    };

    // Name and heading.
    content_row( [&]( row & body ) {
        const auto heading = heading_word( veh->face.dir8() );
        const auto heading_w = hud_phosphor::display_width( heading );
        const auto name_cells = std::max( content_cols - heading_w - 1, 0 );
        body.put( ink::datum, capped( veh->name, name_cells ), name_cells );
        body.put_right( ink::peak, heading, body.left() );
    } );

    // Speed against the safe and maximum envelope.
    const auto abs_vel = std::abs( veh->velocity );
    const auto safe_vel = veh->safe_velocity();
    const auto max_vel = veh->max_velocity();
    const auto to_display = []( int v ) -> int {
        return static_cast<int>( convert_velocity( std::abs( v ), VU_VEHICLE ) );
    };
    const auto speed_rung_veh = abs_vel == 0 ? ink::rule
                                : abs_vel <= safe_vel ? ink::label
                                : abs_vel <= max_vel ? ink::datum
                                : ink::peak;
    content_row( [&]( row & body ) {
        auto segs = std::vector<segment>{
            {   .label = _( "SPD" ),
                .value = std::format( "{}/{} {}", to_display( veh->velocity ), to_display( max_vel ),
                                      velocity_units( VU_VEHICLE ) ),
                .value_rung = speed_rung_veh
            },
        };
        if( veh->cruise_on && veh->cruise_velocity != 0 ) {
            segs.push_back( { .label = _( "CRU" ),
                              .value = std::to_string( to_display( veh->cruise_velocity ) ) } );
        }
        put_segments( body, segs );
    } );

    // Engine and the status flags, each present only while it is true.
    content_row( [&]( row & body ) {
        auto segs = std::vector<segment>{
            {   .label = _( "ENG" ), .value = veh->engine_on ? _( "ON" ) : _( "OFF" ),
                .value_rung = veh->engine_on ? ink::datum : ink::rule
            },
        };
        if( abs_vel > safe_vel ) {
            segs.push_back( { .label = {}, .value = _( "UNSAFE" ), .value_rung = ink::peak } );
        }
        if( veh->is_alarm_on ) {
            segs.push_back( { .label = {}, .value = _( "ALARM" ), .value_rung = ink::peak } );
        }
        if( veh->cruise_on ) {
            segs.push_back( { .label = {}, .value = _( "CRUISE" ) } );
        }
        if( veh->autopilot_on ) {
            segs.push_back( { .label = {}, .value = _( "AUTO" ) } );
        }
        if( veh->camera_on ) {
            segs.push_back( { .label = {}, .value = _( "CAM" ) } );
        }
        put_segments( body, segs );
    } );

    // Fuel gauges, as glyph bars rather than nested divs.
    //
    // Worth recording why that is not merely a restyle. `.veh-fuel-fill` was the
    // one construction in the old stylesheet that correctly carried
    // `display: block`; its sibling `.tbar-fill` — same inline-span trick, same
    // file, same author — did not, and so the target HP bar rendered as a
    // permanently empty trough at every health value. One of the pair being
    // right is what proves the other was an oversight rather than a choice. A
    // bar made of block-element glyphs inside a text run cannot have that bug,
    // because there is no box whose display mode could be wrong.
    const auto bar_cells = std::clamp( content_cols / 2, 4, 15 );
    const auto fuel_label_cells = std::max( content_cols - bar_cells - 6, 0 );
    for( const auto &[fuel_id, amount] : veh->fuels_left() ) {
        // Leave room for the closing rule; the region is sized in whole cells and
        // a panel that outgrows it would overlap the region below.
        if( static_cast<int>( rows.size() ) + 1 >= l.vehicle.rows ) {
            break;
        }
        const auto capacity = veh->fuel_capacity( fuel_id );
        if( capacity <= 0 ) {
            continue;
        }
        const auto pct = std::clamp( amount * 100 / capacity, 0, 100 );
        content_row( [&]( row & body ) {
            body.put( ink::label, capped( item::nname( fuel_id, 1 ), fuel_label_cells ),
                      fuel_label_cells );
            body.skip( 1 );
            // `intact_recedes` is exactly right for a tank: a full one recedes to
            // chrome and a draining one advances, which is the same severity rule
            // the body-part bars use, applied to the same shape of quantity.
            body.put_markup( hud_phosphor::bar( { .cur = amount, .max = capacity,
                                                  .cells = bar_cells, .intact_recedes = true } ),
                             bar_cells );
            body.skip( 1 );
            body.put_right( reserve_rung( pct ), std::format( "{}%", pct ), body.left() );
        } );
    }

    rows.push_back( hud_phosphor::rule( { .cols = cols, .left = "\u2514" } ) );

    // One `.ph-row` element per row, concatenated with no separator — the shape
    // `hud_log_rows` and `hud_keys` already use, and the only one that works.
    // A single `.ph-block` with its rows joined by `\n` does NOT break lines in
    // game: `data-rml` parses this markup against the element's *current*
    // computed style, so `.ph-block`'s `white-space: pre` is not in effect yet
    // when the newlines are collapsed away.
    auto out = std::string();
    for( const std::string &line : rows ) {
        out += "<div class=\"ph-row\">" + line + "</div>";
    }
    return out;
}
