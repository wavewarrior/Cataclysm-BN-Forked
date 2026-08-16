#include "hud_runic_panels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <functional>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "character_display.h"
#include "coordinates.h"
#include "creature.h"
#include "damage.h"
#include "effect.h"
#include "hud_anim.h"
#include "hud_soma_figure.h"
#include "item.h"
#include "itype.h"
#include "line.h"
#include "overmap.h"
#include "point.h"
#include "rml_util.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"
#include "uistate.h"

namespace
{

using hud_runic::ink;

const efftype_id effect_bite( "bite" );
const efftype_id effect_bleed( "bleed" );
const efftype_id effect_infected( "infected" );

const flag_id json_flag_SPLINT( "SPLINT" );

// --- The gauge row ---------------------------------------------------------

/// `name | pip meter | value` — the shape a limb, a pool and the target's
/// health all take, and the only row in these panels that is not one of the
/// creator's own devices.
///
/// The three children are spans and a div carrying a layout class *and* a rung
/// class on the same element. `hud_runic::tint` cannot serve here: it would
/// nest the colour on an inner box, and a flex parent sizes its direct children
/// only — the colour would land on a box the row never measured.
struct gauge_row_options {
    std::string name;
    int cur = 0;
    int max = 0;
    std::string value;
    ink name_ink = ink::label;
    ink value_ink = ink::datum;
    bool critical = false;
};

auto gauge_row( const gauge_row_options &o ) -> std::string
{
    return hud_runic::row( o.critical ? "hud-row crit" : "hud-row", {},
           std::format( R"(<span class="hud-cell-name {}">{}</span>)"
                        R"(<div class="hud-cell-meter">{}</div>)"
                        R"(<span class="hud-cell-val {}">{}</span>)",
                        hud_runic::ink_class( o.name_ink ), rml_escape( o.name ),
                        hud_runic::pips( { .cur = o.cur, .max = o.max } ),
                        hud_runic::ink_class( o.value_ink ),
                        rml_escape( o.value ) ) );
}

// --- The body figure -------------------------------------------------------

/// The six-box body silhouette: head over arm|torso|arm over leg|leg.
///
/// Six flex boxes in pure flow, one per limb. Not a raster of cells, for two
/// reasons that both bite hard: `position: absolute` resolves against the wrong
/// ancestor in these documents, so nothing can be overlaid at a shared centre;
/// and `hud_anim` addresses elements by id, so a limb has to BE an element for
/// its damage flash to have anywhere to land. A raster's cells cannot be grouped
/// per limb.
///
/// Geometry, orientation and the fill ladder live in `hud_soma_figure.h`, which
/// is unit-tested. All this does is stamp the markup.
auto body_figure( const hud_figure::body_state &b ) -> std::string
{
    const auto box = [&b]( hud_figure::limb l ) {
        const auto &state = b.limbs[static_cast<std::size_t>( l )];
        return std::format( R"(<div id="{}" class="{} {}{}"></div>)",
                            hud_figure::element_id( l ), hud_figure::shape_class( l ),
                            hud_figure::fill_class( state ),
                            state.critical ? " hud-fig-crit" : "" );
    };
    using hud_figure::limb;
    return std::format(
               R"(<div class="hud-fig">)"
               R"(<div class="hud-fig-row">{}</div>)"
               R"(<div class="hud-fig-row">{}{}{}</div>)"
               R"(<div class="hud-fig-row">{}{}</div>)"
               R"(</div>)",
               box( limb::head ),
               box( limb::arm_l ), box( limb::torso ), box( limb::arm_r ),
               box( limb::leg_l ), box( limb::leg_r ) );
}

/// A limb's short display name, the same `hp_bar_ui_text` the detail rows use so
/// the collapsed reading and the expanded one cannot disagree about what a part
/// is called. Lives here rather than in `hud_soma_figure.h` because that header
/// is deliberately game-free.
auto worst_name( hud_figure::limb l ) -> std::string
{
    static const std::array<std::string_view, hud_figure::limb_count> ids = {
        "head", "torso", "arm_l", "arm_r", "leg_l", "leg_r"
    };
    const auto k = static_cast<std::size_t>( l );
    return body_part_hp_bar_ui_text( bodypart_id( std::string(
                                         ids[k < hud_figure::limb_count ? k : 0] ) ) );
}

/// The collapsible limb card.
///
/// Collapsed: an aggregate bar, the worst limb by name, and — only when one is
/// live — a row of condition chips. The bar alone would be dishonest: limb
/// maxima differ, so a max-weighted total hides one crippled limb inside five
/// healthy ones, and bleeding or bitten ticks damage between turns and must not
/// require opening a card to see.
///
/// Expanded: the figure BESIDE the detail rows, not above them. That is a height
/// decision — side by side the pair costs `max( rows, figure )`, stacked it costs
/// their sum and pushes POOLS and EFFECTS out of the region, which cannot scroll
/// back into view (the HUD is passive, so the wheel reaches the map zoom instead
/// of a scrollbar).
struct soma_card_options {
    hud_figure::body_state body;
    std::string detail;   ///< the six gauge rows and their chip rows, prebuilt
    bool expanded = false;
};

auto soma_card( const soma_card_options &o ) -> std::string
{
    auto inner = std::string();
    if( o.expanded ) {
        inner = std::format( R"(<div class="hud-fig-split">{}<div class="hud-fig-rows">{}</div></div>)",
                             body_figure( o.body ), o.detail );
    } else {
        const auto ratio = hud_figure::overall_ratio( o.body );
        const auto pct = static_cast<int>( std::lround( ratio * 100.0f ) );
        const auto worst = hud_figure::worst_limb( o.body );
        const auto &worst_state = o.body.limbs[static_cast<std::size_t>( worst )];

        inner += hud_runic::row( "hud-row", {},
                                 std::format( R"(<span class="hud-cell-name {}">{}</span>)"
                                              R"(<div class="hud-cell-meter" id="hud-soma-bar">{}</div>)"
                                              R"(<span class="hud-cell-val {}">{}%</span>)",
                                              hud_runic::ink_class( ink::label ),
                                              rml_escape( _( "VITALS" ) ),
                                              hud_runic::pips( { .cur = pct, .max = 100 } ),
                                              hud_runic::ink_class( pct >= 100 ? ink::label : ink::datum ),
                                              pct ) );

        // Named even on an undamaged body, so the row keeps a fixed shape rather
        // than a field that appears the first time the player is hurt.
        inner += hud_runic::row( worst_state.critical ? "hud-row crit" : "hud-row", {},
                                 std::format( R"(<span class="hud-cell-name {}">{}</span>)"
                                              R"(<span class="hud-fig-worst {}">{}</span>)",
                                              hud_runic::ink_class( ink::label ),
                                              rml_escape( _( "WORST" ) ),
                                              hud_runic::ink_class( worst_state.cur >= worst_state.max
                                                      ? ink::label : ink::datum ),
                                              rml_escape( worst_name( worst ) ) ) );

        const auto cond = hud_figure::conditions_of( o.body );
        if( cond.any() ) {
            auto chips = std::string();
            if( cond.bleeding ) { chips += hud_runic::chip( _( "BLEEDING" ), ink::peak ); }
            if( cond.bitten ) { chips += hud_runic::chip( _( "BITTEN" ), ink::peak ); }
            if( cond.infected ) { chips += hud_runic::chip( _( "INFECTED" ), ink::datum ); }
            if( cond.broken ) { chips += hud_runic::chip( _( "BROKEN" ), ink::datum ); }
            inner += hud_runic::row( "hud-row hud-chiprow", {}, chips );
        }
    }
    return hud_runic::row( "hud-card", "hud-soma-card", inner );
}

/// Feed this frame's card state to the animation system.
///
/// Every spec these keys resolve to has FINITE repeats (`gfx/widgets/icons.json`,
/// `hud_limb` / `hud_soma_bar` / `hud_soma_card`). That is not a style
/// preference: `sidebar_requires_animation()` is true while any tween is
/// unsettled, and `handle_action.cpp:263,314-321` responds by dropping the input
/// timeout to 33 ms AND calling `invalidate_main_ui_adaptor()` every iteration —
/// a full-screen redraw. An ambient spec here would cost the whole game 30 fps
/// while the player stands still.
///
/// `forget` on the elements that are NOT in the DOM this frame is load-bearing
/// for the same reason: a fed element that has left the document keeps its
/// registry key alive, which holds `any_active()` true forever — the framerate
/// pin arriving by the back door.
auto feed_card_motion( const hud_figure::body_state &body, bool expanded ) -> void
{
    for( std::size_t k = 0; k < hud_figure::limb_count; ++k ) {
        const auto l = static_cast<hud_figure::limb>( k );
        const auto id = std::string( hud_figure::element_id( l ) );
        if( !expanded ) {
            hud_anim::forget( id );
            continue;
        }
        const auto &state = body.limbs[k];
        hud_anim::feed( { .element_id = id, .spec_icon = "hud_limb",
                          .value = hud_figure::ratio_of( state ),
                          .is_critical = state.critical } );
    }
    if( expanded ) {
        hud_anim::forget( "hud-soma-bar" );
    } else {
        hud_anim::feed( { .element_id = "hud-soma-bar", .spec_icon = "hud_soma_bar",
                          .value = hud_figure::overall_ratio( body ) } );
    }
    // The card itself: `value` is the state, so flipping it is a change and the
    // `on_change` fade/slide fires exactly once per toggle.
    hud_anim::feed( { .element_id = "hud-soma-card", .spec_icon = "hud_soma_card",
                      .value = expanded ? 1.0 : 0.0 } );
}

// --- Ladder bands ----------------------------------------------------------
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

// --- SOMA limbs ------------------------------------------------------------

struct limb_state {
    bool bleeding = false;
    bool bitten = false;
    bool infected = false;
    bool broken = false;
    bool splinted = false;
    bool critical = false;
};

/// The words on a limb's chip row.
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
        // Splinted versus not, as a word and (via the value's rung) as a
        // luminance step — the shipping panel said it in blue versus grey.
        notes.emplace_back( s.splinted ? _( "MENDING" ) : _( "BROKEN" ) );
    }
    return notes;
}

// --- SOMA effect roster ----------------------------------------------------

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

struct roster_entry {
    named_site entry;
    int tier = 0;
};

/// The live effects worth a row, worst first, at most `roster_cap` of them.
///
/// The cap is a literal here and not a function of the region's height on
/// purpose. Every other overflow in this HUD is answered by `.hud-body`'s
/// scrolling, but the roster is the last section in the panel: an unbounded one
/// pushes the pools the player reads every turn below the fold, behind a
/// scrollbar the HUD gives them no way to drive. Eight rows is the point past
/// which the list stops being scannable anyway.
///
/// The sort is stable, so within a tier the entries keep the order the `@`
/// screen shows them in — this is a severity re-ordering, not a new one.
auto roster_for( const avatar &u ) -> std::vector<roster_entry>
{
    constexpr auto roster_cap = std::size_t{ 8 };

    const auto tiers = effect_tiers( u );
    auto out = std::vector<roster_entry>();
    // Bound to a named local: `views::keys` over the returned vector directly
    // would range over a temporary whose lifetime the pipeline does not extend.
    const auto named = character_display::effect_name_and_text( u );
    for( const auto &name : named | std::views::keys ) {
        const auto found = tiers.find( name );
        out.push_back( { .entry = split_site( name ),
                         .tier = found == tiers.end() ? 0 : found->second } );
    }
    std::ranges::stable_sort( out, std::ranges::greater{}, &roster_entry::tier );
    if( out.size() > roster_cap ) {
        out.resize( roster_cap );
    }
    return out;
}

// --- DOCK helpers ----------------------------------------------------------

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

auto hud_soma( avatar &u, const hud_runic::layout &l ) -> std::string
{
    if( l.soma.w <= 0.0f || l.soma.h <= 0.0f ) {
        return {};
    }
    auto out = std::string();

    // --- The limb card -----------------------------------------------------
    //
    // ONLY the limbs collapse. POOLS and EFFECTS below are unconditional: they
    // are not part of what the player asked to fold away, and a default HUD
    // missing stamina, focus, morale and the whole effects roster would be a
    // content regression wearing a feature's clothes.
    const auto expanded = uistate.hud_soma_expanded;

    hud_figure::body_state body;
    auto detail = std::string();
    auto slot = static_cast<std::size_t>( 0 );
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
        const auto critical = hud_runic::is_critical( { .cur = cur, .max = max,
                              .bleeding = bleeding, .bitten = bitten, .broken = broken } );

        // `get_all_body_parts( true )` yields main parts in `sort_order`, which
        // is the order `hud_figure::limb` is declared in, so position IS the
        // limb. A mod adding a seventh main part would overrun the figure, so
        // the extra parts keep their detail row and simply have no box.
        if( slot < hud_figure::limb_count ) {
            body.limbs[slot] = { .cur = cur, .max = max, .bleeding = bleeding,
                                 .bitten = bitten, .infected = infected, .broken = broken,
                                 .splinted = splinted, .critical = critical
                               };
        }
        ++slot;

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
        detail += gauge_row( {
            .name = body_part_hp_bar_ui_text( bp ),
            .cur = cur,
            .max = max,
            .value = broken ? std::string{ splinted ? _( "SPLINT" ) : _( "BROKE" ) }
: std::format( "{}/{}", cur, max ),
            .name_ink = cur >= max ? ink::label : ink::datum,
            .value_ink = broken ? ( splinted ? ink::datum : ink::dead )
            : ( cur >= max ? ink::label : ink::datum ),
            .critical = critical,
        } );

        const auto notes = limb_notes( { .bleeding = bleeding, .bitten = bitten,
                                         .infected = infected, .broken = broken,
                                         .splinted = splinted, .critical = critical } );
        if( !notes.empty() ) {
            auto chips = std::string();
            for( const auto &note : notes ) {
                chips += hud_runic::chip( note, critical ? ink::peak : ink::datum );
            }
            detail += hud_runic::row( "hud-row hud-chiprow", {}, chips );
        }
    }

    out += soma_card( { .body = body, .detail = detail, .expanded = expanded } );
    feed_card_motion( body, expanded );

    // --- Pools -------------------------------------------------------------
    out += hud_runic::subhead( _( "POOLS" ) );

    const auto stamina = u.get_stamina();
    const auto stamina_max = u.get_stamina_max();
    out += gauge_row( {
        .name = _( "STAM" ),
        .cur = stamina,
        .max = stamina_max,
        .value = std::format( "{}/{}", stamina, stamina_max ),
        .value_ink = pool_ink( stamina, stamina_max ),
    } );

    constexpr auto focus_span = 100;
    out += gauge_row( {
        .name = _( "FOCUS" ),
        .cur = u.focus_pool,
        .max = focus_span,
        .value = std::format( "{}", u.focus_pool ),
        .value_ink = pool_ink( u.focus_pool, focus_span ),
    } );

    // The meter is magnitude only. Morale's sign used to be a deviation scale
    // growing left or right of a centre cell, which the pip device cannot draw;
    // the explicit `+`/`-` on the value carries the direction instead, and
    // `morale_ink` carries the severity as it always did.
    const auto morale = u.get_morale_level();
    constexpr auto morale_span = 100;
    out += gauge_row( {
        .name = _( "MORALE" ),
        .cur = std::abs( morale ),
        .max = morale_span,
        .value = std::format( "{:+}", morale ),
        .value_ink = morale_ink( morale ),
    } );

    // --- Effects -----------------------------------------------------------
    out += hud_runic::subhead( _( "EFFECTS" ) );

    const auto roster = roster_for( u );
    auto tally = std::string();
    if( roster.empty() ) {
        tally = hud_runic::tally_row( _( "None" ), {}, ink::dead, ink::dead );
    } else {
        for( const auto &e : roster ) {
            // Tier 3 — a bad effect pinned to a body part — is the only one that
            // gets a rung of its own; the glyph ladder it used to carry has no
            // column to live in now, and spending a second rung on the middle
            // tiers would make an ordinary roster look like an emergency.
            tally += hud_runic::tally_row( e.entry.name, e.entry.site,
                                           e.tier >= 3 ? ink::peak : ink::datum, ink::label );
        }
    }
    out += hud_runic::row( "nc-tally", {}, tally );

    return out;
}

auto hud_dock( avatar &u, const hud_runic::layout &l ) -> std::string
{
    if( l.dock.w <= 0.0f || l.dock.h <= 0.0f ) {
        return {};
    }

    // The mission marker's bearing. The overmap chunk that used to head this
    // panel is the RADAR region above it.
    const auto here = u.abs_omt_pos();
    const auto custom = u.get_custom_mission_target();
    const auto active = u.get_active_mission_target();
    const auto target_omt = custom != overmap::invalid_tripoint ? custom : active;
    const auto has_mission = target_omt != overmap::invalid_tripoint;

    auto out = hud_runic::fact( {
        .label = _( "MISSION" ),
        .value = has_mission
        ? std::format( "{} {}", rl_dist( here, target_omt ),
                       direction_name_short( direction_from( here.raw().xy(),
                                             target_omt.raw().xy() ) ) )
        : std::string( _( "None" ) ),
        .value_ink = has_mission ? ink::datum : ink::dead,
    } );

    // --- Target ------------------------------------------------------------
    out += hud_runic::rule_div();
    out += hud_runic::subhead( _( "TARGET" ) );

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

        out += hud_runic::fact( {
            .value = t->disp_name(),
            .sub = string_format( _( "%d tiles %s" ), range, heading ),
            .value_ink = ink::peak,
        } );
        // The shipping target bar was a permanently empty trough: `.tbar-fill`
        // was an inline <span> with no `display`, so its width never applied.
        // The pip device cannot fail that way — its fill is elements, not a
        // percentage of a box that collapsed.
        out += gauge_row( {
            .name = _( "HP" ),
            .cur = hp,
            .max = hp_max,
            .value = std::format( "{}%", pct ),
        } );

        const auto &attitude = Creature::get_attitude_ui_data( t->attitude_to( u ) );
        out += hud_runic::fact( { .label = _( "STATUS" ),
                                  .value = attitude.first.translated() } );
    } else {
        out += hud_runic::fact( { .value = _( "No target" ), .value_ink = ink::dead } );
    }

    // --- Arms --------------------------------------------------------------
    out += hud_runic::rule_div();
    out += hud_runic::subhead( _( "ARMS" ) );

    const auto armed = u.is_armed();
    out += hud_runic::fact( {
        .label = _( "WIELD" ),
        .value = armed ? u.primary_weapon().tname() : std::string( _( "fists" ) ),
        // One translatable sentence rather than three shouted words in a hand-
        // aligned row: the separators and the order are the translator's to
        // move, and there is no column left for them to break.
        .sub = armed ? string_format( _( "%d bash · %d cut · %+d hit" ),
                                      u.primary_weapon().damage_melee( DT_BASH ),
                                      u.primary_weapon().damage_melee( DT_CUT ),
                                      u.primary_weapon().type->m_to_hit )
        : std::string(),
        .value_ink = armed ? ink::peak : ink::dead,
    } );

    if( const item *sidearm = stowed_sidearm( u ) ) {
        const auto loaded = sidearm->ammo_remaining();
        const auto capacity = sidearm->ammo_capacity();
        const auto *ammo = sidearm->ammo_data();
        out += hud_runic::fact( {
            .label = _( "ALT" ),
            .value = sidearm->tname(),
            .sub = std::format( "{}/{} {}", loaded, capacity,
                                ammo != nullptr ? ammo->nname( 1 ) : std::string( _( "empty" ) ) ),
            .value_ink = loaded > 0 ? ink::datum : ink::dead,
        } );
    }

    return out;
}
