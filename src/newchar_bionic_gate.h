#pragma once

/// Pure decision logic for the BIONICS creator step: may this bionic be installed or removed right
/// now, and if not, which refusal applies. Split out of newcharacter_ui.cpp so it can be exercised
/// without an avatar, a scenario, a profession or an RmlUi document — the shape
/// `nc_trait_gate` established on the TRAITS step, for the same reason.
///
/// The screen has THREE surfaces that must agree about a bionic: the checkbox glyph
/// (`[x]`/`[ ]`/`[-]`), the Status field's reason line, and the popup CONFIRM raises when it
/// refuses. Before this rework only the popup existed, so every unavailable row rendered as the
/// same dark gray and the reason arrived only AFTER a keypress. They agree now because all three
/// read one `state`.
namespace nc_bionic_gate
{

/// What the caller looked up from game state. Deliberately plain data: every field is a question
/// already answered, so this header never needs a game, and a test can pose combinations that are
/// awkward to reach in play.
struct inputs {
    /// The avatar has this bionic installed right now (`Character::has_bionic`).
    bool taken = false;
    /// The chosen profession grants it, but it is not installed yet — profession CBMs are added by
    /// `avatar::add_profession_items` AFTER the wizard finishes. The row has always rendered these
    /// as held; they cannot be toggled, because there is nothing installed to remove and the
    /// profession would re-grant it anyway.
    bool granted = false;
    /// Another installed bionic lists this one in its `required_bionics`, so removing it would
    /// leave that one dangling.
    bool has_dependents = false;
    /// The scenario or profession forbids bionics WHOLESALE.
    bool scen_forbids_all = false;
    bool prof_forbids_all = false;
    /// ...or forbids this one specifically.
    bool scen_forbids = false;
    bool prof_forbids = false;
    /// ...or pins this one, so it may not be removed.
    bool scen_locked = false;
    bool prof_locked = false;
    /// A trait the avatar holds is cancelled by this bionic.
    bool trait_conflicts = false;
    /// `Character::bionic_installation_issues` is non-empty: not enough free slots. Always false
    /// when CBM_SLOTS_ENABLED is off, which is the default.
    bool no_space = false;
    /// One of its `required_bionics` is missing.
    bool missing_prereq = false;
    /// The avatar holds the bionic this one upgrades, or one of its own upgrades.
    bool has_downgrade = false;
    bool has_upgrade = false;
    /// Point cost: positive is an advantage, negative a disadvantage, zero free.
    int points = 0;
    /// Running totals, in the same signed form set_bionics keeps them: `num_bad` is negative.
    int num_good = 0;
    int num_bad = 0;
    int max_points = 0;
    /// Freeform pools enforce no cap at all.
    bool freeform = false;
};

/// Would installing a bionic of this cost breach the cap on ITS OWN side of the budget?
///
/// Keyed off the POINT SIGN. The screen used to key this off the cursor's column index
/// (`iCurWorkingPage == 0` meant "counts against advantages"), which stops being true the moment
/// the columns are grouped by anything else — and reported a zero-point bionic as breaching the
/// DISADVANTAGE cap purely because it was not an advantage. Identical to the TRAITS rule, and kept
/// separate rather than shared because the two steps are free to diverge on budgeting.
constexpr auto over_budget( int points, int num_good, int num_bad, int max_points,
                            bool freeform ) -> bool
{
    if( freeform || points == 0 ) {
        return false;
    }
    return points > 0 ? num_good + points > max_points
           : num_bad + points < -max_points;
}

/// The resolved answer. Every field is independently meaningful, because the UI reports the FIRST
/// applicable reason in words and a bionic can carry several.
struct state {
    bool taken = false;
    bool granted = false;
    bool has_dependents = false;
    bool locked = false;            //< scenario or profession pins it
    bool forbidden = false;         //< scenario or profession forbids it, wholesale or by name
    bool trait_conflicts = false;
    bool over_budget = false;
    bool no_space = false;
    bool missing_prereq = false;
    bool has_downgrade = false;
    bool has_upgrade = false;

    /// Shown as held: installed, or guaranteed by the profession.
    constexpr auto held() const -> bool {
        return taken || granted;
    }

    /// Can the player act on this bionic at all — install it if absent, remove it if present.
    ///
    /// The two directions have completely different gates, which is why this is not one
    /// conjunction: an installed bionic cares only about whether it may be removed, and nothing
    /// about conflicts, space or budget, since removing it can only ever free all three.
    constexpr auto toggleable() const -> bool {
        if( taken ) {
        return !has_dependents && !locked;
    }
    // `granted` and `locked` are the same fact for a profession — `profession::is_locked_bionic`
    // IS "in _starting_CBMs" — and both mean the same thing here: this step does not get to
    // decide. A scenario's `_forced_bionics` reach `locked` the same way.
    return !granted && !locked && !forbidden && !trait_conflicts && !over_budget && !no_space &&
           !missing_prereq && !has_downgrade && !has_upgrade;
}
};

constexpr auto evaluate( const inputs &in ) -> state
{
    state st;
    st.taken = in.taken;
    st.granted = in.granted;
    if( in.taken ) {
        // An installed bionic is only ever asked whether it can come out. Evaluating conflicts,
        // space or the budget here would be nonsense: it is already paid for and already fits.
        st.has_dependents = in.has_dependents;
        st.locked = in.scen_locked || in.prof_locked;
        return st;
    }
    st.locked = in.scen_locked || in.prof_locked;
    st.forbidden = in.scen_forbids_all || in.prof_forbids_all || in.scen_forbids || in.prof_forbids;
    st.trait_conflicts = in.trait_conflicts;
    st.over_budget = over_budget( in.points, in.num_good, in.num_bad, in.max_points, in.freeform );
    st.no_space = in.no_space;
    st.missing_prereq = in.missing_prereq;
    st.has_downgrade = in.has_downgrade;
    st.has_upgrade = in.has_upgrade;
    return st;
}

} // namespace nc_bionic_gate
