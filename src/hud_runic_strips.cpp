#include "hud_runic_strips.h"

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
#include "point.h"
#include "point_float.h"
#include "profession.h"
#include "rml_util.h"
#include "translations.h"
#include "type_id.h"
#include "uistate.h"
#include "units.h"
#include "units_utility.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_type.h"

namespace
{

using hud_runic::ink;

// ── Ladder bands ────────────────────────────────────────────────────────────
//
// The thresholds below are lifted verbatim from the file-static helpers in
// `panels.cpp` — `temp_color`, `value_color`, and the
// `str_string`/`dex_string`/… family via `color_compare_base`. Their *hues* are
// deliberately left behind.
//
// That is the point of the exercise rather than a duplication smell. Hierarchy
// in this register is luminance, so a threshold ladder can only be expressed as
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

// ── Field groups ────────────────────────────────────────────────────────────

/// One `.hud-meta-group` div holding one `legend_item` per field, skipping any
/// whose value is absent.
///
/// This is all that survives of the old `put_segments`, and the skip is the only
/// part of it that was ever about the data rather than about the grid: a field
/// whose source has nothing to say contributes nothing, instead of an orphaned
/// caption with an empty value beside it. Everything else that function did —
/// measuring each field, tightening the inter-field gap, dropping the fields
/// that no longer fit — was cell arithmetic answering a question the stylesheet
/// now answers: `.hud-meta-mid` wraps and then clips.
auto group( std::string_view classes, const std::vector<hud_runic::legend_options> &fields )
-> std::string
{
    std::string inner;
    for( const hud_runic::legend_options &f : fields ) {
        if( f.value.empty() ) {
            continue;
        }
        inner += hud_runic::legend_item( f );
    }
    return hud_runic::row( classes, {}, inner );
}

// ── Status helpers ──────────────────────────────────────────────────────────

/// The clock as `HH:MM`, honouring the `24_HOUR` option exactly as
/// `to_string_time_of_day` does, minus the seconds.
///
/// Seconds are the one field on this row that changes every frame and never
/// changes a decision. The log's timestamp is NOT a formatter worth reusing for
/// this: it calls `to_string_time_of_day` too and merely truncates, which
/// happens to read as `HH:MM` on a 24-hour clock and mangles `9:59:46AM` into
/// `9:59:` on a 12-hour one. The format strings stay translated, because that
/// is how the original expresses a locale that separates or orders time
/// differently.
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
/// whose `light_emission` is zero, which is the same test
/// `Character::active_light` relies on.
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
    return std::format( "{} {:.0f}{}", direction_name_short( dir ),
                        convert_velocity( w.windspeed, VU_WIND ), velocity_units( VU_WIND ) );
}

/// How lit the avatar's own tile is, as the game's own six-band description.
///
/// There is no percentage to print — `ambient_light_at` is an unbounded float —
/// so inventing a denominator would be inventing data. The band word is the
/// honest datum and is already translated; the rung carries the same severity a
/// percentage would have.
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

/// A need as a status field: its own description, or the register's word for
/// "nothing to report".
///
/// `get_thirst_description` and `get_fatigue_description` return an EMPTY string
/// inside their comfortable band, and `group()` above drops a field with an
/// empty value — so without this, three of the four need fields would vanish
/// whenever the avatar was fine, and the player would have no way to tell "not
/// thirsty" from "the strip ran out of room". All four are unconditional, and a
/// need with nothing to report says so one rung down.
auto need_field( std::string label, const std::pair<std::string, nc_color> &desc )
-> hud_runic::legend_options
{
    if( desc.first.empty() ) {
    return { .label = std::move( label ), .value = _( "NORMAL" ), .value_ink = ink::label };
}
return { .label = std::move( label ), .value = desc.first, .value_ink = ink::datum };
}

/// The avatar's profession, preferring one they wrote for themselves.
auto profession_text( const avatar &u ) -> std::string
{
    if( !u.custom_profession.empty() ) {
    return u.custom_profession;
}
return u.prof ? u.prof->gender_appropriate_name( u.male ) : std::string();
}

/// Partner state for the co-op field; empty when there is no session.
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
    auto out = std::format( "{} {}%", sess.partner_name, sess.partner_hp_pct );
    if( const auto ping = sess.partner_ping_ms.load(); ping > 0 ) {
        out += std::format( " {}ms", ping );
    }
    if( sess.is_host() && g->coop_server_ != nullptr && g->coop_server_->awaiting_reconnect() ) {
        out += " " + std::string( _( "RECONNECTING" ) );
    }
    return out;
}

/// The populated throw quick-slots as `1x4 2x12`.
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

// ── Function keys ───────────────────────────────────────────────────────────

constexpr int keys_slot_count = 10;

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
            {
                .act = ACTION_RELOAD_WIELDED, .label = _( "RELOAD" ), .reason = std::move( reload ),
                .available = reload_ok
            },
            { .act = ACTION_TOGGLE_RUN, .label = _( "RUN" ) },
            { .act = ACTION_EXAMINE, .label = _( "EXAMINE" ) },
            { .act = ACTION_PICKUP, .label = _( "PICK UP" ) },
            { .act = ACTION_CRAFT, .label = _( "CRAFT" ) },
            { .act = ACTION_INVENTORY, .label = _( "INVENTORY" ) },
            { .act = ACTION_THROW, .label = _( "THROW" ) },
            // WAIT is `pause`, not `wait`. `wait` opens the wait-for-N-minutes
            // menu and its default binding is `|` (`keybindings.json`), which
            // reads as a stray rule fragment rather than as a key. The action the
            // design asks for is the roguelike wait-a-turn — which is also the
            // one worth a permanent place on the strip.
            { .act = ACTION_PAUSE, .label = _( "WAIT" ) },
            // The limb card's toggle. On the strip because a keybinding nobody
            // can see is a feature nobody finds: the HUD takes no mouse input, so
            // this key is the ONLY way to open the card. The label states what
            // the press will do, not what the card currently is.
            {
                .act = ACTION_TOGGLE_SOMA_DETAIL,
                .label = uistate.hud_soma_expanded ? _( "HIDE LIMBS" ) : _( "LIMBS" )
            },
        }};
}

/// The key bound to `act`, or nothing when it is unbound.
///
/// `input_context::get_desc` has no "leave it blank" mode: with no binding at
/// all it returns the literal sentence `Unbound globally!`, and with every
/// binding filtered out it returns `Disabled` (`input.cpp:769` and `:785`). The
/// shipping hotbar printed those straight into the strip — three of nine slots
/// on a default keymap.
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

/// One slot as an `.nc-legend-item`: `KEY :: ACTION`, plus the reason it is
/// unusable when there is one.
///
/// Three states, and the player's next move only differs between two of them, so
/// live spends `peak` on the key and `datum` on the action while both unavailable
/// and unbound collapse to `dead` — this key is not going to help right now
/// either way. The key cell says one of exactly three things: the key itself; a
/// `-` for a key that is bound but would achieve nothing now; and nothing at all
/// for an action with no binding. Nothing else can reach it, because `get_desc`'s
/// only non-key returns are its three sentence-shaped sentinels and `bound_key`
/// catches all three.
///
/// Two shapes the helper cannot produce, both here rather than as extra fields on
/// it. An unbound slot has no key glyph, and `hud_runic::legend_item` drops the
/// `::` separator along with an empty label — right for a bare STATUS value, and
/// wrong here, where the action names must stay in one column behind one
/// separator, so that case is emitted whole with the key span present but empty.
/// And a blocked slot carries a third span saying why, which the helper has no
/// slot for; giving it one for this single caller would cost every other caller
/// a field it will never set.
auto key_item( const key_slot &slot, const std::optional<std::string> &key ) -> std::string
{
    const auto live = key.has_value() && slot.available;
    const auto key_ink = live ? ink::peak : ink::dead;
    const auto label_ink = live ? ink::datum : ink::dead;

    auto out = key.has_value()
    ? hud_runic::legend_item( {
        .label = live ? *key : std::string( "-" ),
        .value = slot.label,
        .label_ink = key_ink,
        .value_ink = label_ink,
        .label_class = "hud-key",
        .value_class = "nc-legend-label",
    } )
        : std::format( R"(<div class="nc-legend-item">)"
                       R"(<span class="nc-legend-label hud-key {}"></span>)"
                       R"(<span class="nc-legend-sep">::</span>)"
                       R"(<span class="nc-legend-label {}">{}</span></div>)",
                       hud_runic::ink_class( key_ink ), hud_runic::ink_class( label_ink ),
                       rml_escape( slot.label ) );

    if( !slot.reason.empty() ) {
        // Spliced in front of the item's own closing tag rather than appended
        // after it: the reason belongs to this slot, and outside the item div
        // `.nc-legend`'s flex row would let it wrap away from the action it
        // explains. Every path above ends in exactly this tag.
        static constexpr std::string_view close = "</div>";
        out.insert( out.size() - close.size(),
                    std::format( R"(<span class="hud-keyreason {}">{}</span>)",
                                 hud_runic::ink_class( ink::dead ), rml_escape( slot.reason ) ) );
    }
    return out;
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

/// One `.hud-row` gauge: a name, a pip meter, and a right-aligned percentage.
/// The same three columns the SOMA panel's body-part rows use, because a fuel
/// tank and a limb are the same shape of quantity read the same way.
auto gauge_row( const std::string &name, int cur, int max, int pct ) -> std::string
{
    return hud_runic::row( "hud-row", {},
           std::format( R"(<span class="hud-cell-name {}">{}</span>)",
                        hud_runic::ink_class( ink::label ), rml_escape( name ) ) +
           std::format( R"(<div class="hud-cell-meter">{}</div>)",
                        hud_runic::pips( { .cur = cur, .max = max } ) ) +
           std::format( R"(<span class="hud-cell-val {}">{}%</span>)",
                        hud_runic::ink_class( reserve_rung( pct ) ), pct ) );
}

} // namespace

// ── Status strip ────────────────────────────────────────────────────────────

auto hud_status_row1( avatar &u, const hud_runic::layout & ) -> std::string
{
    // Left group: who you are. The name is peak because it anchors the eye at
    // the strip's left edge; the profession recedes to chrome because it never
    // changes. Both are bare values — a caption reading NAME over a name is a
    // caption the player never needs to read twice.
    const auto left = std::vector<hud_runic::legend_options> {
        { .label = {}, .value = u.get_name(), .value_ink = ink::peak },
        { .label = {}, .value = profession_text( u ), .value_ink = ink::label },
    };

    // Middle group: where and when you are. Ordered by how often a field changes
    // a decision, which on a narrow viewport is also the order `.hud-meta-mid`
    // clips them away in.
    //
    // Only the fields whose value would be ambiguous alone carry a caption. A
    // season, a clock, a place name and a depth read as themselves; `12` does
    // not read as a day of the season without `DAY` in front of it.
    const auto &weather = get_weather();
    const auto temp = weather.get_temperature( u.abs_pos() );
    const auto lamp = brightest_lamp( u );
    const auto [light_word, light_rung] = light_text( u );

    auto middle = std::vector<hud_runic::legend_options> {};
    if( auto coop = coop_text(); !coop.empty() ) {
        middle.push_back( { .label = _( "CO-OP" ), .value = std::move( coop ),
                            .value_ink = ink::peak } );
    }
    middle.push_back( { .label = _( "DAY" ),
                        .value = std::to_string( day_of_season<int>( calendar::turn ) + 1 ) } );
    middle.push_back( { .label = {},
                        .value = calendar::name_season( season_of_year( calendar::turn ) ) } );
    // The clock is the one value in this group at peak: it is the only number
    // here that changes what you should be doing next.
    middle.push_back( { .label = {},
                        .value = u.has_watch() ? clock_hm( calendar::turn )
                                 : g->get_levz() >= 0 ? approx_time_of_day()
                                 : std::string( "???" ),
                        .value_ink = ink::peak } );
    middle.push_back( { .label = {},
                        .value = ACTIVE_OVERMAP_BUFFER.ter( u.abs_omt_pos() )->get_name() } );
    if( g->get_levz() < 0 ) {
        middle.push_back( { .label = {}, .value = std::format( "-{}Z", -g->get_levz() ),
                            .value_ink = ink::label } );
    }
    middle.push_back( { .label = _( "WX" ), .value = weather.weather_id->name.translated() } );
    middle.push_back( { .label = {}, .value = print_temperature( temp ),
                        .value_ink = temp_rung( temp ) } );
    middle.push_back( { .label = {}, .value = wind_text() } );
    middle.push_back( { .label = _( "LUX" ), .value = light_word, .value_ink = light_rung } );
    if( lamp.lit ) {
        middle.push_back( { .label = _( "LAMP" ),
                            .value = lamp.pct ? std::format( "{}%", *lamp.pct )
                                     : std::string( _( "ON" ) ),
                            .value_ink = lamp.pct ? reserve_rung( *lamp.pct ) : ink::datum } );
    }

    // Right group: safe mode. On is peak; off is the register's one shout,
    // because "safe mode is off" is the single piece of status that gets people
    // killed. `.hud-alarm` carries it as a gold-filled chip — a fill and a weight
    // step, both of which survive greyscale and a dim panel.
    const auto safe_on = g->safe_mode != SAFE_MODE_OFF;
    const auto right = std::vector<hud_runic::legend_options> {
        safe_on
        ? hud_runic::legend_options{
            .label = _( "SAFE MODE" ), .value = _( "ON" ),
            .value_ink = ink::peak }
:
        hud_runic::legend_options{ .label = _( "SAFE MODE" ), .value = _( "OFF" ), .alarm = true },
    };

    return group( "hud-meta-group", left )
           + group( "hud-meta-group hud-meta-mid", middle )
           + group( "hud-meta-group", right );
}

auto hud_status_row2( avatar &u, const hud_runic::layout & ) -> std::string
{
    // Left group: the four attributes. Each carries its deviation from its own
    // unmodified base on the rung, because in this register that deviation is
    // the whole message and the number alone cannot carry it.
    const auto left = std::vector<hud_runic::legend_options> {
        {
            .label = _( "STR" ), .value = std::to_string( u.get_str() ),
            .value_ink = stat_rung( u.get_str_base(), u.get_str() )
        },
        {
            .label = _( "DEX" ), .value = std::to_string( u.get_dex() ),
            .value_ink = stat_rung( u.get_dex_base(), u.get_dex() )
        },
        {
            .label = _( "INT" ), .value = std::to_string( u.get_int() ),
            .value_ink = stat_rung( u.get_int_base(), u.get_int() )
        },
        {
            .label = _( "PER" ), .value = std::to_string( u.get_per() ),
            .value_ink = stat_rung( u.get_per_base(), u.get_per() )
        },
    };

    // Middle group: how you are moving and how you feel.
    //
    // FOCUS, PAIN and MORALE are deliberately absent. Focus and morale are rows
    // of the SOMA panel's POOLS section and pain is an EFFECTS entry, so carrying
    // them here would say everything twice.
    const auto enc_torso = u.encumb( body_part_torso );
    const auto enc_arms = std::max( u.encumb( body_part_arm_l ), u.encumb( body_part_arm_r ) );
    const auto [warmth_word, warmth_rung] = warmth_text( u );

    auto middle = std::vector<hud_runic::legend_options> {
        { .label = _( "MOVE" ), .value = move_mode_word( u ) },
        {
            .label = _( "SPD" ), .value = std::to_string( u.get_speed() ),
            .value_ink = speed_rung( u.get_speed() )
        },
        { .label = _( "NOISE" ), .value = std::to_string( u.volume ) },
        // Two fields rather than one `ENC 42/14`. A shared solidus reads as a
        // fraction, and these are two independent loads on two different sets of
        // limbs; split, a narrow strip clips ARMS and still says what the torso
        // is carrying.
        { .label = _( "ENC TORSO" ), .value = std::to_string( enc_torso ) },
        { .label = _( "ARMS" ), .value = std::to_string( enc_arms ) },
        need_field( _( "HUNGER" ), u.get_hunger_description() ),
        need_field( _( "THIRST" ), u.get_thirst_description() ),
        need_field( _( "FATIGUE" ), u.get_fatigue_description() ),
        { .label = _( "WARMTH" ), .value = warmth_word, .value_ink = warmth_rung },
    };
    if( u.weight_carried() > u.weight_capacity() ) {
        middle.push_back( { .label = {}, .value = _( "OVERBURDENED" ), .alarm = true } );
    }
    if( auto thrown = throw_text( u ); !thrown.empty() ) {
        middle.push_back( { .label = _( "THROW" ), .value = std::move( thrown ) } );
    }

    // Right group: what is looking at you. VIS is every hostile you can see;
    // TRACK is the subset inside the combat bubble, i.e. the ones close enough to
    // be acting on you this turn. Each count carries its own word, because a bare
    // `3 · 1` needs a legend the strip has no room to print.
    const auto &seen = u.get_mon_visible();
    const auto visible = seen.nearby_hostile_count;
    const auto tracking = seen.combat_hostile_count;
    const auto right = std::vector<hud_runic::legend_options> {
        {
            .label = _( "HOSTILE" ),
            .value = visible > 0
            ? std::format( "{} {} \u00b7 {} {}", visible, _( "VIS" ), tracking, _( "TRACK" ) )
            : std::string( _( "None" ) ),
            .value_ink = visible > 0 ? ink::peak : ink::label
        },
    };

    return group( "hud-meta-group", left )
           + group( "hud-meta-group hud-meta-mid", middle )
           + group( "hud-meta-group", right );
}

// ── Message log ─────────────────────────────────────────────────────────────

auto hud_log_rows( const std::vector<Messages::rich_message> &msgs,
                   const hud_runic::layout & ) -> std::string
{
    // The recency ramp is four ladder rungs rather than a pre-multiplied inline
    // opacity, for two reasons. An inline opacity fights `hud_anim`'s entry
    // tween, which owns this element's inline opacity; and a baked fade would not
    // survive the ladder being re-tuned live from the F4 Theme tab.
    static constexpr std::array<ink, 4> ramp = { ink::peak, ink::datum, ink::label, ink::rule };

    // Indexed rather than `views::enumerate`d: the ramp is chosen by distance
    // from the newest row, so the index is the datum, not bookkeeping.
    auto out = std::string();
    for( const auto i : std::views::iota( std::size_t{ 0 }, msgs.size() ) ) {
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

        // Messages arrive carrying embedded `<color_*>` tags. They are stripped
        // rather than rendered: a second data hue is precisely what this register
        // does not have, and the message's severity is already on its glyph.
        auto body = remove_color_tags( m.text );
        if( newest ) {
            body += " \u2588"; // the cursor block marks where the log is writing
        }

        const auto inner =
            std::format( R"(<span class="hud-log-time {}">{}</span>)",
                         hud_runic::ink_class( rung ), rml_escape( m.time ) ) +
            std::format( R"(<span class="hud-log-glyph {}">{}</span>)",
                         hud_runic::ink_class( rung ), glyph ) +
            std::format( R"(<span class="hud-log-text {}">{}</span>)",
                         hud_runic::ink_class( rung ), rml_escape( body ) );

        // `hud-log-entry` and `log-<seq>` are load-bearing: `hud_anim` feeds and
        // forgets rows by that exact id, so an index-based id or a dropped class
        // silently kills the entry animation while leaving the layout perfect.
        // The newest row's marker is `.hud-log-fresh`'s gold left edge — the
        // creator's cursor device — rather than a `>` in the text.
        out += hud_runic::row( newest ? "hud-row hud-log-entry hud-log-fresh"
                               : "hud-row hud-log-entry",
                               std::format( "log-{}", m.seq ), inner );
    }
    return out;
}

// ── Function keys ───────────────────────────────────────────────────────────

auto hud_keys( avatar &u, const hud_runic::layout & ) -> std::string
{
    const input_context ctxt = get_default_mode_input_context();
    const auto slots = key_slots( u );

    auto out = std::string();
    for( const key_slot &slot : slots ) {
        out += key_item( slot, bound_key( ctxt, slot.act ) );
    }
    return out;
}

// ── Vehicle ─────────────────────────────────────────────────────────────────

auto hud_veh_panel( avatar &u, const hud_runic::layout & ) -> std::string
{
    if( !u.controlling_vehicle ) {
    return std::string();
    }
    const vehicle *veh = veh_pointer_or_null( get_map().veh_at( u.bub_pos() ) );
    if( veh == nullptr ) {
    return std::string();
    }

    // Name over heading: the heading changes every turn you steer and the name
    // never changes, so the heading is the subordinate line under the identity
    // rather than a field competing with it.
    auto out = hud_runic::fact( { .label = {}, .value = veh->name,
                                  .sub = heading_word( veh->face.dir8() ),
                                  .value_ink = ink::datum } );

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
    out += hud_runic::fact( {
        .label = _( "SPD" ),
        .value = std::format( "{}/{} {}", to_display( veh->velocity ), to_display( max_vel ),
                              velocity_units( VU_VEHICLE ) ),
        .sub = veh->cruise_on && veh->cruise_velocity != 0
        ? std::format( "{} {}", _( "CRU" ), to_display( veh->cruise_velocity ) )
        : std::string(),
        .value_ink = speed_rung_veh,
    } );

    // Engine state plus one chip per flag that is currently true. Absence is the
    // reading for a flag that is false: a row of OFF chips would spend the
    // player's attention on the six things that are not happening.
    auto chips = hud_runic::chip( std::format( "{} {}", _( "ENG" ),
                                  veh->engine_on ? _( "ON" ) : _( "OFF" ) ),
                                  veh->engine_on ? ink::datum : ink::rule );
    if( abs_vel > safe_vel ) {
    chips += hud_runic::chip( _( "UNSAFE" ), ink::peak );
    }
    if( veh->is_alarm_on ) {
    chips += hud_runic::chip( _( "ALARM" ), ink::peak );
    }
    if( veh->cruise_on ) {
    chips += hud_runic::chip( _( "CRUISE" ), ink::datum );
    }
    if( veh->autopilot_on ) {
    chips += hud_runic::chip( _( "AUTO" ), ink::datum );
    }
    if( veh->camera_on ) {
    chips += hud_runic::chip( _( "CAM" ), ink::datum );
    }
    out += hud_runic::row( "hud-row hud-chiprow", {}, chips );
    out += hud_runic::rule_div();

    // Fuel gauges. `reserve_rung` is exactly right for a tank: a full one recedes
    // to chrome and a draining one advances, which is the same severity rule the
    // body-part meters use, applied to the same shape of quantity.
for( const auto &[fuel_id, amount] : veh->fuels_left() ) {
        const auto capacity = veh->fuel_capacity( fuel_id );
        if( capacity <= 0 ) {
            continue;
        }
        const auto pct = std::clamp( amount * 100 / capacity, 0, 100 );
        out += gauge_row( item::nname( fuel_id, 1 ), amount, capacity, pct );
    }
    return out;
}
