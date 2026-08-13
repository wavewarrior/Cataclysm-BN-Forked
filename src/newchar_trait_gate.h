#pragma once

/// Pure decision logic for the TRAITS creator step: may this trait be taken or dropped right now,
/// and if not, which refusal applies. Split out of newcharacter_ui.cpp so it can be exercised
/// without an avatar, a scenario, a profession or an RmlUi document.
///
/// The screen has THREE surfaces that must agree about a trait: the checkbox glyph (`[x]`/`[ ]`/
/// `[-]`), the Status field's reason line, and the popup CONFIRM raises when it refuses. Before
/// this they could not agree, because only the popup existed — every unavailable row rendered as
/// the same dark gray. They agree now because all three read one `state`, and the branchiness that
/// produces it lives here where it is tested: taken x locked x mandatory, conflicts x swaps x
/// has-a-holder, and a budget check at the cap on both signs.
namespace nc_trait_gate
{

/// What the caller looked up from game state. Deliberately plain data: every field is a question
/// already answered, so this header never needs a game, and a test can pose any combination —
/// including ones that are awkward to reach in play.
struct inputs {
    /// The avatar already has this trait.
    bool taken = false;
    /// Profession or scenario locks it, so it cannot be dropped.
    bool locked = false;
    /// One of its types declares `mandatory_one`, so the last one cannot be dropped.
    bool mandatory = false;
    /// Conflicts with something the avatar already has.
    bool conflicts = false;
    /// One of its types declares `swap_on_conflict`.
    bool swaps = false;
    /// The avatar actually holds a trait of that swappable type to give up.
    bool has_swap_holder = false;
    bool scen_forbids = false;
    bool prof_forbids = false;
    /// A starting bionic cancels it.
    bool bionic_blocks = false;
    /// Point cost: positive is an advantage, negative a disadvantage, zero free.
    int points = 0;
    /// Running totals, in the same signed form set_traits keeps them: `num_bad` is negative.
    int num_good = 0;
    int num_bad = 0;
    int max_points = 0;
    /// Freeform pools enforce no cap at all.
    bool freeform = false;
};

/// Would taking a trait of this cost breach the cap on ITS OWN side of the budget?
///
/// Keyed off the POINT SIGN. The screen used to key this off the cursor's column index
/// (`iCurWorkingPage == 0` meant "counts against advantages"), which stopped being true the moment
/// traits were grouped by anything other than point sign. A zero-point trait can never breach
/// either cap, and must not be reported as breaching the disadvantage cap just because it is not
/// an advantage.
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
/// applicable reason in words and a trait can carry several.
struct state {
    bool taken = false;
    bool locked = false;
    bool mandatory = false;
    bool conflicts = false;
    /// A conflict that resolves by replacement rather than a refusal.
    bool can_swap = false;
    bool scen_forbids = false;
    bool prof_forbids = false;
    bool bionic_blocks = false;
    bool over_budget = false;

    /// Can the player act on this trait at all — take it if unheld, drop it if held.
    ///
    /// The two directions have completely different gates, which is why this is not a single
    /// conjunction: a held trait cares only about whether it may be released, and nothing about
    /// conflicts or budget, since dropping it can only ever free points.
    constexpr auto toggleable() const -> bool {
        if( taken ) {
        return !locked && !mandatory;
    }
    return !bionic_blocks && !scen_forbids && !prof_forbids && !over_budget &&
           ( !conflicts || can_swap );
}
};

constexpr auto evaluate( const inputs &in ) -> state
{
    state st;
    st.taken = in.taken;
    if( in.taken ) {
        // A held trait is only ever asked whether it can be released. Evaluating conflicts or the
        // budget here would be nonsense: it is already paid for and already compatible.
        st.locked = in.locked;
        st.mandatory = in.mandatory;
        return st;
    }
    st.conflicts = in.conflicts;
    // A swap type replaces whatever of it you hold — but only when there IS something to give up.
    // `swaps` alone is not enough: with no holder the conflict is a plain refusal.
    st.can_swap = in.conflicts && in.swaps && in.has_swap_holder;
    st.scen_forbids = in.scen_forbids;
    st.prof_forbids = in.prof_forbids;
    st.bionic_blocks = in.bionic_blocks;
    st.over_budget = over_budget( in.points, in.num_good, in.num_bad, in.max_points, in.freeform );
    return st;
}

} // namespace nc_trait_gate
