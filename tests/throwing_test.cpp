#include "catch/catch_amalgamated.hpp"
#include <algorithm>
#include <list>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "coordinates.h"
#include "damage.h"
#include "game.h"
#include "game_constants.h"
#include "inventory.h"
#include "item.h"
#include "line.h"
#include "map_helpers.h"
#include "material.h"
#include "monster.h"
#include "npc.h"
#include "player.h"
#include "player_helpers.h"
#include "projectile.h"
#include "ranged.h"
#include "state_helpers.h"
#include "test_statistics.h"
#include "type_id.h"

TEST_CASE( "throwing distance test", "[throwing], [balance]" )
{
    clear_all_state();
    const standard_npc thrower( "Thrower", tripoint_bub_ms( 60, 60, 0 ), {}, 4, 10, 10, 10, 10 );
    item &grenade = *item::spawn_temporary( "grenade" );
    CHECK( thrower.throw_range( grenade ) >= 30 );
    CHECK( thrower.throw_range( grenade ) <= 35 );
}

struct throw_test_data {
    statistics<bool> hits;
    statistics<double> dmg;

    throw_test_data() : dmg( Z95 ) {}
};

struct throw_test_pstats {
    int skill_lvl;
    int str;
    int dex;
    int per;
};

static std::ostream &operator<<( std::ostream &stream, const throw_test_pstats &pstats )
{
    return( stream << "STR: " << pstats.str << " DEX: " << pstats.dex <<
                      " PER: " << pstats.per << " SKL: " << pstats.skill_lvl );
}

static const skill_id skill_throw = skill_id( "throw" );

static void reset_player( player &p, const throw_test_pstats &pstats, const tripoint_bub_ms &pos )
{
    clear_character( p );
    CHECK( !p.in_vehicle );
    p.setpos( pos );
    p.str_max = pstats.str;
    p.dex_max = pstats.dex;
    p.per_max = pstats.per;
    p.set_str_bonus( 0 );
    p.set_per_bonus( 0 );
    p.set_dex_bonus( 0 );
    p.set_skill_level( skill_throw, pstats.skill_lvl );
}

// If tests are routinely failing you should:
//  1. Make sure some change hasn't caused some regression
//  2. Make sure test is accurate by testing with a large minimum iterations (min > 5000)
//  3. Increase bounds on thresholds
//  4. Increase max iterations which will make the CI smaller and more likely to
//     fit inside the threshold but also increase the average test length
// In that order.
constexpr int min_throw_test_iterations = 100;
constexpr int max_throw_test_iterations = 10000;

// tighter thresholds here will increase accuracy but also increase average test
// time since more samples are required to get a more accurate test
static void test_throwing_player_versus(
    player &p, const std::string &mon_id, const std::string &throw_id,
    const int range, const throw_test_pstats &pstats,
    const epsilon_threshold &hit_thresh, const epsilon_threshold &dmg_thresh )
{
    const tripoint_bub_ms monster_start = { 30 + range, 30, 0 };
    const tripoint_bub_ms player_start = { 30, 30, 0 };
    bool hit_thresh_met = false;
    bool dmg_thresh_met = false;
    throw_test_data data;


    do {
        reset_player( p, pstats, player_start );
        p.set_moves( 1000 );
        p.set_stamina( p.get_stamina_max() );
        detached_ptr<item> det = item::spawn( throw_id );
        monster &mon = spawn_test_monster( mon_id, monster_start );
        mon.set_moves( 0 );

        dealt_projectile_attack atk = ranged::throw_item( p, mon.bub_pos(), std::move( det ),
                                      std::nullopt );
        data.hits.add( atk.hit_critter != nullptr );
        data.dmg.add( atk.dealt_dam.total_damage() );

        if( data.hits.n() >= min_throw_test_iterations ) {
            // ideally we should actually still checking the threshold after we
            // meet it but we're busy people and don't have time for that
            if( !hit_thresh_met ) {
                hit_thresh_met = data.hits.test_threshold( hit_thresh );
            }
            // don't do an else here because it's possible we just made
            // hit_thresh_met true
            if( hit_thresh_met ) {
                // commenting this out is a super easy way to force all the
                // test to fail if you want to reset the baseline after
                // making balance changes or if many of the tests are failing
                dmg_thresh_met = data.dmg.test_threshold( dmg_thresh );
            }
        }
        g->remove_zombie( mon );
        // only need to check dmg_thresh_met because it can only be true if
        // hit_thresh_met first
    } while( !dmg_thresh_met && data.hits.n() < max_throw_test_iterations );

    INFO( "Monster: '" << mon_id << "' Item: '" << throw_id );
    INFO( "Range: " << range << " Pstats: " << pstats );
    INFO( "Total throws: " << data.hits.n() );
    INFO( "Ratio: " << data.hits.avg() * 100 << "%" );
    INFO( "Hit Lower: " << data.hits.lower() * 100 << "% Hit Upper: " << data.hits.upper() * 100 <<
          "%" );
    INFO( "Hit Thresh: " << ( hit_thresh.midpoint - hit_thresh.epsilon ) * 100 << "% - " <<
          ( hit_thresh.midpoint + hit_thresh.epsilon ) * 100 << "%" );
    INFO( "Adj Wald error: " << data.hits.margin_of_error() );
    INFO( "Avg total damage: " << data.dmg.avg() );
    INFO( "Dmg Lower: " << data.dmg.lower() << " Dmg Upper: " << data.dmg.upper() );
    INFO( "Dmg Thresh: " << dmg_thresh.midpoint - dmg_thresh.epsilon << " - " <<
          dmg_thresh.midpoint + dmg_thresh.epsilon );
    INFO( "Margin of error: " << data.hits.margin_of_error() );
    CHECK( dmg_thresh_met );
}

constexpr throw_test_pstats lo_skill_base_stats = { 0, 8, 8, 8 };
constexpr throw_test_pstats mid_skill_base_stats = { MAX_SKILL / 2, 8, 8, 8 };
constexpr throw_test_pstats hi_skill_base_stats = { MAX_SKILL, 8, 8, 8 };
constexpr throw_test_pstats hi_skill_athlete_stats = { MAX_SKILL, 12, 12, 12 };

TEST_CASE( "basic_throwing_sanity_tests", "[throwing],[balance]" )
{
    clear_all_state();
    player &p = g->u;

    // Float DDA + Box2D raycasting produces higher hit rates than the old
    // tile-based trajectory system.  Thresholds updated to match.
    SECTION( "test_player_vs_zombie_rock_basestats" ) {
        test_throwing_player_versus( p, "mon_zombie", "rock", 1, lo_skill_base_stats, { 1.00, 0.10 }, { 13, 5 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 5, lo_skill_base_stats, { 0.98, 0.10 }, { 13, 5 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 10, lo_skill_base_stats, { 0.76, 0.10 }, { 10, 5 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 15, lo_skill_base_stats, { 0.59, 0.10 }, { 7.7, 5 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 20, lo_skill_base_stats, { 0.42, 0.10 }, { 5.5, 5 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 25, lo_skill_base_stats, { 0.38, 0.10 }, { 5.0, 5 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 30, lo_skill_base_stats, { 0.33, 0.10 }, { 4.2, 5 } );
    }

    SECTION( "test_player_vs_zombie_javelin_iron_basestats" ) {
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 1, lo_skill_base_stats, { 1.00, 0.10 }, { 33, 8 } );
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 5, lo_skill_base_stats, { 0.95, 0.10 }, { 34, 10 } );
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 10, lo_skill_base_stats, { 0.65, 0.10 }, { 23, 8 } );
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 15, lo_skill_base_stats, { 0.53, 0.10 }, { 19, 8 } );
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 20, lo_skill_base_stats, { 0.37, 0.10 }, { 13, 6 } );
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 25, lo_skill_base_stats, { 0.31, 0.10 }, { 11, 5 } );
    }
    // Float DDA + Box2D raycasting significantly increases hit rates at medium-long
    // range compared to old tile-based trajectories.
    SECTION( "test_player_vs_zombie_rock_athlete" ) {
        test_throwing_player_versus( p, "mon_zombie", "rock", 1, hi_skill_athlete_stats, { 1.00, 0.10 }, { 16.5, 8 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 5, hi_skill_athlete_stats, { 1.00, 0.10 }, { 16.5, 8 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 10, hi_skill_athlete_stats, { 1.00, 0.10 }, { 16.27, 8 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 15, hi_skill_athlete_stats, { 1.00, 0.10 }, { 23, 8 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 20, hi_skill_athlete_stats, { 0.96, 0.10 }, { 23, 8 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 25, hi_skill_athlete_stats, { 0.92, 0.10 }, { 21, 8 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 30, hi_skill_athlete_stats, { 0.83, 0.10 }, { 19, 8 } );
    }

    SECTION( "test_player_vs_zombie_javelin_iron_athlete" ) {
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 1, hi_skill_athlete_stats, { 1.00, 0.10 }, { 59.00, 8 } );
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 5, hi_skill_athlete_stats, { 1.00, 0.10 }, { 50.55, 8 } );
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 10, hi_skill_athlete_stats, { 1.00, 0.10 }, { 58, 12 } );
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 15, hi_skill_athlete_stats, { 1.00, 0.10 }, { 58, 12 } );
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 20, hi_skill_athlete_stats, { 0.97, 0.10 }, { 57, 12 } );
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 25, hi_skill_athlete_stats, { 0.94, 0.10 }, { 55, 12 } );
        test_throwing_player_versus( p, "mon_zombie", "javelin_iron", 30, hi_skill_athlete_stats, { 0.85, 0.10 }, { 49, 12 } );
    }
}

TEST_CASE( "throwing_skill_impact_test", "[throwing],[balance]" )
{
    clear_all_state();
    player &p = g->u;
    // we already cover low stats in the sanity tests and we only cover a few
    // ranges here because what we're really trying to capture is the effect
    // the throwing skill has while the sanity tests are more explicit.
    SECTION( "mid_skill_basestats_rock" ) {
        test_throwing_player_versus( p, "mon_zombie", "rock", 5, mid_skill_base_stats, { 1.00, 0.10 }, { 19, 6 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 10, mid_skill_base_stats, { 1.00, 0.10 }, { 19, 6 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 15, mid_skill_base_stats, { 0.93, 0.10 }, { 18, 6 } );
    }

    SECTION( "hi_skill_basestats_rock" ) {
        test_throwing_player_versus( p, "mon_zombie", "rock", 5, hi_skill_base_stats, { 1.00, 0.10 }, { 24, 6 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 10, hi_skill_base_stats, { 1.00, 0.10 }, { 24, 6 } );
        test_throwing_player_versus( p, "mon_zombie", "rock", 15, hi_skill_base_stats, { 1.00, 0.10 }, { 24, 6 } );
    }
}

TEST_CASE( "time_to_throw_independent_of_number_of_projectiles", "[throwing],[balance]" )
{
    clear_all_state();
    player &p = g->u;

    detached_ptr<item> det = item::spawn( "throwing_stick", calendar::turn, 10 );
    item &thrown = *det;
    REQUIRE( thrown.charges > 1 );
    REQUIRE( thrown.count_by_charges() );
    p.wield( std::move( det ) );
    int initial_moves = -1;
    while( thrown.charges > 0 ) {
        const int cost = ranged::throw_cost( p, thrown );
        if( initial_moves < 0 ) {
            initial_moves = cost;
        } else {
            CHECK( initial_moves == cost );
        }
        thrown.charges--;
    }
}
