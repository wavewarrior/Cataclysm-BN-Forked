#pragma once

/// Pure decision logic for the finish line of character creation: may this character be started, and
/// if not, which refusal applies. Split out of newcharacter_ui.cpp so it can be exercised without an
/// avatar, a points pool or an RmlUi document.
///
/// The OVERVIEW step has THREE surfaces that must agree about readiness: the meta bar's readiness
/// word, the STATUS fact in the detail panel, and the popup chain `NEXT_TAB` raises. Before this only
/// the popup existed, so the screen said nothing at all about whether it would accept the keypress
/// until it had already refused it. They agree now because all three read one `verdict`, and the
/// precedence that produces it lives here where it is tested.
///
/// The ORDER is the old `NEXT_TAB` block's, case for case: an invalid pool first (skills, then
/// traits, then stats, then a pool with no sub-pool to blame), then spare points, then a missing
/// name. Only the plumbing changed; no refusal reaches the player differently, and the popup wording
/// is unchanged. Words stay in the producer — this header knows nothing about translation.
namespace nc_finish_gate
{

/// What the caller looked up from the points pool and the avatar. Plain data: every field is a
/// question already answered, so a test can pose combinations that are awkward to reach in play —
/// including an invalid pool with every sub-pool in credit, which is what a ONE_POOL overspend looks
/// like.
struct inputs {
    /// `points_left::is_valid()`. Not derived from the three counters below: ONE_POOL and MULTI_POOL
    /// answer this differently, and FREEFORM/TRANSFER are always valid however the counters read.
    bool valid = true;
    /// `points_left::stat_points_left()` and friends. Negative means that sub-pool is overspent.
    int stat_left = 0;
    int trait_left = 0;
    int skill_left = 0;
    /// `points_left::has_spare()` — points that would be thrown away by starting now.
    bool spare = false;
    /// The avatar has no name, so starting rolls one.
    bool name_empty = false;
};

/// Why the finish line is not simply open. `ready` is the only verdict that needs no words.
enum struct verdict {
    ready,
    /// Overspent, with the sub-pool to blame.
    over_skill,
    over_trait,
    over_stat,
    /// Overspent, but no single sub-pool is in deficit — a ONE_POOL total that does not fit.
    over_pool,
    /// Points left on the table; starting discards them.
    spare_points,
    /// No name entered; starting generates one.
    needs_name,
};

constexpr auto evaluate( const inputs &in ) -> verdict
{
    if( !in.valid ) {
    // Deficits are reported in the order the player is most likely to be able to fix: skills
    // first, then traits, then stats. Preserved from the old chain rather than re-derived — a
    // character can be over on two pools at once, and which one the popup names is behaviour.
    if( in.skill_left < 0 ) {
            return verdict::over_skill;
        }
        if( in.trait_left < 0 ) {
            return verdict::over_trait;
        }
        if( in.stat_left < 0 ) {
            return verdict::over_stat;
        }
        return verdict::over_pool;
    }
    if( in.spare ) {
    return verdict::spare_points;
}
if( in.name_empty ) {
    return verdict::needs_name;
}
return verdict::ready;
}

/// Whether the verdict REFUSES rather than asks. A refusal shows a popup and returns to the screen;
/// everything else is a confirmable question, including `ready` (creation has always asked "are you
/// SURE you're finished?").
///
/// This distinction is why the verdict is not just a bool: the meta bar needs to colour a refusal
/// differently from a question, and the popup chain needs to know whether an affirmative answer may
/// start the game.
constexpr auto is_refusal( verdict v ) -> bool
{
    switch( v ) {
    case verdict::over_skill:
    case verdict::over_trait:
    case verdict::over_stat:
    case verdict::over_pool:
        return true;
    case verdict::ready:
    case verdict::spare_points:
    case verdict::needs_name:
        return false;
}
return false;
}

} // namespace nc_finish_gate
