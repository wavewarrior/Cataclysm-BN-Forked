#include "catch/catch_amalgamated.hpp"

#include "newchar_finish_gate.h"

// The finish gate decides what the OVERVIEW step says about readiness and what NEXT_TAB does about
// it. Its whole content is PRECEDENCE, and precedence is behaviour: which sub-pool a popup blames
// when a character is over on two of them, and whether "no name" can hide behind "spare points".
// Both are things a refactor silently reorders, so both are pinned here.

TEST_CASE( "nc_finish_a_valid_named_character_with_no_spare_is_ready", "[newchar][finish_gate]" )
{
    const nc_finish_gate::inputs in = { .valid = true,
                                        .stat_left = 0,
                                        .trait_left = 0,
                                        .skill_left = 0,
                                        .spare = false,
                                        .name_empty = false
                                      };
    CHECK( nc_finish_gate::evaluate( in ) == nc_finish_gate::verdict::ready );
    CHECK_FALSE( nc_finish_gate::is_refusal( nc_finish_gate::verdict::ready ) );
}

TEST_CASE( "nc_finish_an_invalid_pool_blames_skills_then_traits_then_stats",
           "[newchar][finish_gate]" )
{
    // The order the old NEXT_TAB chain used, preserved case for case. A character can be over on
    // several pools at once, and which one the popup names is what the player acts on.
    nc_finish_gate::inputs in = { .valid = false,
                                  .stat_left = -2,
                                  .trait_left = -3,
                                  .skill_left = -1,
                                  .spare = false,
                                  .name_empty = false
                                };
    CHECK( nc_finish_gate::evaluate( in ) == nc_finish_gate::verdict::over_skill );
    in.skill_left = 0;
    CHECK( nc_finish_gate::evaluate( in ) == nc_finish_gate::verdict::over_trait );
    in.trait_left = 0;
    CHECK( nc_finish_gate::evaluate( in ) == nc_finish_gate::verdict::over_stat );

    // Invalid with every sub-pool in credit is not a contradiction: that is what a ONE_POOL total
    // that does not fit looks like, and it must still refuse rather than fall through to ready.
    in.stat_left = 3;
    in.trait_left = 1;
    in.skill_left = 4;
    CHECK( nc_finish_gate::evaluate( in ) == nc_finish_gate::verdict::over_pool );

    CHECK( nc_finish_gate::is_refusal( nc_finish_gate::verdict::over_skill ) );
    CHECK( nc_finish_gate::is_refusal( nc_finish_gate::verdict::over_trait ) );
    CHECK( nc_finish_gate::is_refusal( nc_finish_gate::verdict::over_stat ) );
    CHECK( nc_finish_gate::is_refusal( nc_finish_gate::verdict::over_pool ) );
}

TEST_CASE( "nc_finish_an_overspend_outranks_spare_points_and_a_missing_name",
           "[newchar][finish_gate]" )
{
    // An overspend is a refusal; the other two are questions. Reporting "you have points left" to a
    // character who is over budget would be both wrong and unactionable.
    const nc_finish_gate::inputs in = { .valid = false,
                                        .stat_left = -1,
                                        .trait_left = 0,
                                        .skill_left = 0,
                                        .spare = true,
                                        .name_empty = true
                                      };
    CHECK( nc_finish_gate::evaluate( in ) == nc_finish_gate::verdict::over_stat );
}

TEST_CASE( "nc_finish_spare_points_outrank_a_missing_name", "[newchar][finish_gate]" )
{
    // Both are confirmable, so the only question is which is asked first — and it is the one that
    // costs something. The name is generated for you; the points are simply discarded.
    nc_finish_gate::inputs in = { .valid = true,
                                  .stat_left = 2,
                                  .trait_left = 0,
                                  .skill_left = 0,
                                  .spare = true,
                                  .name_empty = true
                                };
    CHECK( nc_finish_gate::evaluate( in ) == nc_finish_gate::verdict::spare_points );
    in.spare = false;
    CHECK( nc_finish_gate::evaluate( in ) == nc_finish_gate::verdict::needs_name );
    CHECK_FALSE( nc_finish_gate::is_refusal( nc_finish_gate::verdict::spare_points ) );
    CHECK_FALSE( nc_finish_gate::is_refusal( nc_finish_gate::verdict::needs_name ) );
}

TEST_CASE( "nc_finish_unspent_points_in_a_freeform_pool_are_not_a_question",
           "[newchar][finish_gate]" )
{
    // FREEFORM and TRANSFER report valid with no spare however the counters read, so a character
    // built with them reaches ready without being asked about points that were never a budget.
    const nc_finish_gate::inputs in = { .valid = true,
                                        .stat_left = 12,
                                        .trait_left = -7,
                                        .skill_left = 30,
                                        .spare = false,
                                        .name_empty = false
                                      };
    CHECK( nc_finish_gate::evaluate( in ) == nc_finish_gate::verdict::ready );
}
