#include "om_direction.h" // IWYU pragma: associated
#include "cube_direction.h" // IWYU pragma: associated
#include "enum_conversions.h"
#include "omdata.h" // IWYU pragma: associated
#include "overmap_special.h" // IWYU pragma: associated
#include "overmap.h" // IWYU pragma: associated

#include <algorithm>
#include <cassert>
#include <cmath>
#include <coordinates.h>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <numeric>
#include <optional>
#include <ostream>
#include <point.h>
#include <set>
#include <submap.h>
#include <tuple>
#include <unordered_set>
#include <vector>
#include <vehicle.h>

#include "all_enum_values.h"
#include "assign.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "catalua.h"
#include "catalua_impl.h"
#include "catalua_sol.h"
#include "character_id.h"
#include "debug.h"
#include "init.h"
#include "distribution.h"
#include "flood_fill.h"
#include "fstream_utils.h"
#include "game.h"
#include "generic_factory.h"
#include "json.h"
#include "line.h"
#include "map.h"
#include "map_iterator.h"
#include "mapbuffer.h"
#include "mapgen.h"
#include "mapgen_functions.h"
#include "math_defines.h"
#include "messages.h"
#include "mongroup.h"
#include "monster.h"
#include "mtype.h"
#include "name.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmap_connection.h"
#include "overmap_location.h"
#include "overmap_label.h"
#include "overmap_noise.h"
#include "overmap_types.h"
#include "overmapbuffer.h"
#include "overmapbuffer_registry.h"
#include "fluid_grid.h"
#include "regional_settings.h"
#include "rng.h"
#include "rotatable_symbols.h"
#include "sets_intersect.h"
#include "simple_pathfinding.h"
#include "string_formatter.h"
#include "string_utils.h"
#include "text_snippets.h"
#include "translations.h"
#include "type_id.h"
#include "weighted_list.h"
#include "world.h"
#include "world_type.h"

static const efftype_id effect_pet( "pet" );

static const species_id ZOMBIE( "ZOMBIE" );

static const mongroup_id GROUP_DIMENSIONAL_SURFACE( "GROUP_DIMENSIONAL_SURFACE" );
static const mongroup_id GROUP_WORM( "GROUP_WORM" );
static const mongroup_id GROUP_ZOMBIE( "GROUP_ZOMBIE" );
static const mongroup_id GROUP_NEMESIS( "GROUP_NEMESIS" );

static const oter_type_str_id oter_type_bridge( "bridge" );


#define dbg(x) DebugLogFL((x),DC::MapGen)


////////////////




//const regional_settings default_region_settings;










/** @relates string_id */

/** @relates int_id */

/** @relates string_id */

/** @relates int_id */


/** @relates string_id */

/** @relates string_id */

/** @relates string_id */

/** @relates string_id */

/** @relates int_id */

/** @relates int_id */

/** @relates int_id */

/** @relates int_id */





























/*
 * load mapgen functions from an overmap_terrain json entry
 * suffix is for roads/subways/etc which have "_straight", "_curved", "_tee", "_four_way" function mappings
 */






















































// When building a mutable overmap special we maintain a collection of
// unresolved joins.  We need to be able to index that collection in
// various ways, so it gets its own struct to maintain the relevant invariants.



























// *** BEGIN overmap FUNCTIONS ***


extern oter_id ot_forest;
extern oter_id ot_forest_thick;
extern oter_id ot_forest_water;
extern oter_id ot_river_center;


void overmap::ter_set( const tripoint_om_omt &p, const oter_id &id )
{
    if( !inbounds( p ) ) {
        /// TODO: Add a debug message reporting this, but currently there are way too many place that would trigger it.
        return;
    }

    layer[p.z() + OVERMAP_DEPTH].terrain[p.x()][p.y()] = id;
}

bool overmap::seen( const tripoint_om_omt &p ) const
{
    if( !inbounds( p ) ) {
    return false;
}
return layer[p.z() + OVERMAP_DEPTH].visible[p.x()][p.y()];
}

bool overmap::is_explored( const tripoint_om_omt &p ) const
{
    if( !inbounds( p ) ) {
    return false;
}
return layer[p.z() + OVERMAP_DEPTH].explored[p.x()][p.y()];
}

bool overmap::has_note( const tripoint_om_omt &p ) const
{
    if( p.z() < -OVERMAP_DEPTH || p.z() > OVERMAP_HEIGHT ) {
    return false;
}

for( const om_note &i : layer[p.z() + OVERMAP_DEPTH].notes ) {
    if( i.p == p.xy() ) {
            return true;
        }
    }
    return false;
}

bool overmap::is_marked_dangerous( const tripoint_om_omt &p ) const
{
for( const om_note &i : layer[p.z() + OVERMAP_DEPTH].notes ) {
        if( !i.dangerous ) {
            continue;
        } else if( p.xy() == i.p ) {
            return true;
        }
        const int radius = i.danger_radius;
        if( i.danger_radius == 0 && i.p != p.xy() ) {
            continue;
        }
        for( int x = -radius; x <= radius; x++ ) {
            for( int y = -radius; y <= radius; y++ ) {
                const tripoint_om_omt rad_point = tripoint_om_omt( i.p, p.z() ) + point( x, y );
                if( p.xy() == rad_point.xy() ) {
                    return true;
                }
            }
        }
    }
    return false;
}

void overmap::add_note( const tripoint_om_omt &p, std::string message )
{
    if( p.z() < -OVERMAP_DEPTH || p.z() > OVERMAP_HEIGHT ) {
        debugmsg( "Attempting to add not to overmap for blank layer %d", p.z() );
        return;
    }

    auto &notes = layer[p.z() + OVERMAP_DEPTH].notes;
    const auto it = std::find_if( begin( notes ), end( notes ), [&]( const om_note & n ) {
        return n.p == p.xy();
    } );

    if( it == std::end( notes ) ) {
        notes.emplace_back( om_note{ std::move( message ), p.xy() } );
    } else if( !message.empty() ) {
        it->text = std::move( message );
    } else {
        notes.erase( it );
    }
}

void overmap::mark_note_dangerous( const tripoint_om_omt &p, int radius, bool is_dangerous )
{
    for( auto &i : layer[p.z() + OVERMAP_DEPTH].notes ) {
        if( p.xy() == i.p ) {
            i.dangerous = is_dangerous;
            i.danger_radius = radius;
            return;
        }
    }
}

void overmap::delete_note( const tripoint_om_omt &p )
{
    add_note( p, std::string{} );
}

void overmap::move_hordes()
{
    // Prevent hordes to be moved twice by putting them in here after moving.
    decltype( zg ) tmpzg;
    //MOVE ZOMBIE GROUPS
    for( auto it = zg.begin(); it != zg.end(); ) {
        mongroup &mg = it->second;
        if( !mg.horde || mg.horde_behaviour == "nemesis" ) {
            // Nemesis hordes have their own move logic.
            ++it;
            continue;
        }

        if( mg.horde_behaviour.empty() ) {
            mg.horde_behaviour = one_in( 2 ) ? "city" : "roam";
        }

        // Gradually decrease interest.
        mg.dec_interest( 1 );

        if( ( mg.abs_pos.xy() == mg.target.xy() ) || mg.interest <= 15 ) {
            auto used_hook_target = false;

            if( auto *state = DynamicDataLoader::get_instance().lua.get() ) {
                auto &lua = state->lua;
                auto game = lua.globals()["game"];
                auto behaviours_obj = game["horde_behaviours"].get<sol::object>();
                if( behaviours_obj.is<sol::table>() ) {
                    auto behaviours = behaviours_obj.as<sol::table>();
                    const auto fn_obj = behaviours.get_or<sol::object>( mg.horde_behaviour, sol::lua_nil );
                    if( fn_obj.is<sol::protected_function>() || fn_obj.is<sol::function>() ) {
                        auto func = fn_obj.as<sol::protected_function>();
                        auto params = lua.create_table();
                        auto results = lua.create_table();
                        params["results"] = results;
                        params["group"] = &mg;
                        params["pos_abs_sm"] = mg.abs_pos;
                        params["target_abs_sm"] = mg.target;
                        params["behaviour"] = mg.horde_behaviour;

                        auto res = func( params );
                        check_func_result( res );

                        const auto hook_target = results.get<sol::optional<tripoint_abs_sm>>( "target" );
                        const auto hook_interest = results.get<sol::optional<int>>( "interest" );
                        if( hook_target.has_value() ) {
                            mg.set_target( *hook_target );
                            used_hook_target = true;
                        }
                        if( hook_interest.has_value() ) {
                            mg.set_interest( *hook_interest );
                        }
                    }
                }
            }

            if( !used_hook_target ) {
                mg.wander( *this );
            }
        }

        // Decrease movement chance according to the terrain we're currently on.
        auto local_pos = project_remain<coords::om>( mg.abs_pos ).remainder_tripoint;
        const oter_id &walked_into = ter( project_to<coords::omt>( local_pos ) );
        int movement_chance = 1;
        if( walked_into == ot_forest || walked_into == ot_forest_water ) {
            movement_chance = 3;
        } else if( walked_into == ot_forest_thick ) {
            movement_chance = 6;
        } else if( walked_into == ot_river_center ) {
            movement_chance = 10;
        }

        // If the average horde speed is 50% that of normal, then the chance to
        // move should be 1/2 what it would be if the speed was 100%.
        // Since the max speed for a horde is one map space per 2.5 minutes,
        // choose that to be the speed of the fastest horde monster, which is
        // roughly 200 at the time of writing. So a horde with average speed
        // 200 or over will move at max speed, and slower hordes will move less
        // frequently. The average horde speed for regular Z's is around 100,
        // or one space per 5 minutes.
        if( one_in( movement_chance ) && rng( 0, 100 ) < mg.interest && rng( 0, 200 ) < mg.avg_speed() ) {
            if( mg.abs_pos.x() > mg.target.x() ) {
                mg.abs_pos.x()--;
            }
            if( mg.abs_pos.x() < mg.target.x() ) {
                mg.abs_pos.x()++;
            }
            if( mg.abs_pos.y() > mg.target.y() ) {
                mg.abs_pos.y()--;
            }
            if( mg.abs_pos.y() < mg.target.y() ) {
                mg.abs_pos.y()++;
            }

            // Erase the group at it's old location, add the group with the new location
            const auto new_local_pos = project_remain<coords::om>( mg.abs_pos ).remainder_tripoint;
            tmpzg.insert( std::pair<tripoint_om_sm, mongroup>( new_local_pos, mg ) );
            zg.erase( it++ );
        } else {
            ++it;
        }
    }
    // and now back into the monster group map.
    zg.insert( tmpzg.begin(), tmpzg.end() );

    if( get_option<bool>( "WANDER_SPAWNS" ) ) {

        // Re-absorb zombies into hordes.
        // Scan over monsters outside the player's view and place them back into hordes.
        auto monster_map_it = monster_map->begin();
        while( monster_map_it != monster_map->end() ) {
            const auto &p = monster_map_it->first;
            auto &this_monster = monster_map_it->second;

            // Only zombies on z-level 0 may join hordes.
            if( p.z() != 0 ) {
                monster_map_it++;
                continue;
            }

            // Check if the monster is a zombie.
            auto &type = *( this_monster.type );
            if(
                !type.species.contains( ZOMBIE ) || // Only add zombies to hordes.
                type.id == mtype_id( "mon_jabberwock" ) || // Jabberwockies are an exception.
                this_monster.get_speed() <= 30 || // So are very slow zombies, like crawling zombies.
                this_monster.has_flag( MF_IMMOBILE ) || // Also exempt anything stationary.
                this_monster.has_flag( MF_STATIONARY ) || // Also exempt anything stationary.
                this_monster.has_effect( effect_pet ) || // "Zombie pet" zlaves are, too.
                !this_monster.will_join_horde( INT_MAX ) || // So are zombies who won't join a horde of any size.
                this_monster.mission_id != -1 // We mustn't delete monsters that are related to missions.
            ) {
                // Don't delete the monster, just increment the iterator.
                monster_map_it++;
                continue;
            }

            // Scan for compatible hordes in this area, selecting the largest.
            mongroup *add_to_group = nullptr;
            auto group_bucket = zg.equal_range( p );
            std::vector<monster>::size_type add_to_horde_size = 0;
            std::for_each( group_bucket.first, group_bucket.second,
            [&]( std::pair<const tripoint_om_sm, mongroup> &horde_entry ) {
                mongroup &horde = horde_entry.second;

                // We only absorb zombies into GROUP_ZOMBIE hordes
                if( horde.horde && !horde.monsters.empty() && horde.type == GROUP_ZOMBIE &&
                    horde.monsters.size() > add_to_horde_size ) {
                    add_to_group = &horde;
                    add_to_horde_size = horde.monsters.size();
                }
            } );

            // Check again if the zombie will join the largest horde, now that we know the accurate size.
            if( this_monster.will_join_horde( add_to_horde_size ) ) {
                // If there is no horde to add the monster to, create one.
                if( add_to_group == nullptr ) {
                    mongroup m( GROUP_ZOMBIE, project_combine( pos(), p ), 1, 0 );
                    m.horde = true;
                    m.monsters.push_back( this_monster );
                    m.interest = 0; // Ensures that we will select a new target.
                    add_mon_group( m );
                } else {
                    add_to_group->monsters.push_back( this_monster );
                }
            } else { // Bad luck--the zombie would have joined a larger horde, but not this one.  Skip.
                // Don't delete the monster, just increment the iterator.
                monster_map_it++;
                continue;
            }

            // Delete the monster, continue iterating.
            monster_map_it = monster_map->erase( monster_map_it );
        }
    }
}

void overmap::move_nemesis()
{
    // Prevent hordes to be moved twice by putting them in here after moving.
    decltype( zg ) tmpzg;
    for( std::multimap<tripoint_om_sm, mongroup>::iterator it = zg.begin(); it != zg.end(); ) {
        mongroup &mg = it->second;
        if( !mg.horde || mg.horde_behaviour != "nemesis" ) {
            ++it;
            continue;
        }

        // Decrease movement chance according to the terrain we're currently on.
        auto local_pos = project_remain<coords::om>( mg.abs_pos ).remainder_tripoint;
        const oter_id &walked_into = ter( project_to<coords::omt>( local_pos ) );
        int movement_chance = 1;
        if( walked_into == ot_forest || walked_into == ot_forest_water ) {
            movement_chance = 3;
        } else if( walked_into == ot_forest_thick ) {
            movement_chance = 6;
        } else if( walked_into == ot_river_center ) {
            movement_chance = 10;
        }

        if( one_in( movement_chance ) && rng( 0, 200 ) < mg.avg_speed() ) {
            if( mg.abs_pos.x() > mg.nemesis_target.x() ) {
                mg.abs_pos.x()--;
            }
            if( mg.abs_pos.x() < mg.nemesis_target.x() ) {
                mg.abs_pos.x()++;
            }
            if( mg.abs_pos.y() > mg.nemesis_target.y() ) {
                mg.abs_pos.y()--;
            }
            if( mg.abs_pos.y() < mg.nemesis_target.y() ) {
                mg.abs_pos.y()++;
            }

            if( project_to<coords::om>( mg.abs_pos ) == project_to<coords::om>( mg.nemesis_target ) ) {
                point_abs_om omp;
                tripoint_om_sm local_sm;
                std::tie( omp, local_sm ) = project_remain<coords::om>( mg.abs_pos );

                local_pos.y() = local_sm.y();
                local_pos.x() = local_sm.x();

                // Erase the group at its old location, add the group with the new location
                tmpzg.insert( std::pair<tripoint_om_sm, mongroup>( local_pos, mg ) );
                zg.erase( it++ );
                break;
            }
        } else {
            break;
        }
        break;
    }
    // and now back into the monster group map.
    zg.insert( tmpzg.begin(), tmpzg.end() );
}

void overmap::signal_hordes( const tripoint_abs_sm &p, const int sig_power )
{
    for( auto &elem : zg ) {
        mongroup &mg = elem.second;
        if( !mg.horde ) {
            continue;
        }
        if( mg.horde_behaviour == "nemesis" ) {
            // Nemesis hordes are signaled to the player by their own function.
            continue;
        }
        const int dist = rl_dist( p, mg.abs_pos );
        if( sig_power < dist ) {
            continue;
        }
        // TODO: base this in monster attributes, foremost GOODHEARING.
        const int inter_per_sig_power = 15; //Interest per signal value
        const int min_initial_inter = 30; //Min initial interest for horde
        const int calculated_inter = ( sig_power + 1 - dist ) * inter_per_sig_power; // Calculated interest
        const int roll = rng( 0, mg.interest );
        // Minimum capped calculated interest. Used to give horde enough interest to really investigate the target at start.
        const int min_capped_inter = std::max( min_initial_inter, calculated_inter );
        if( roll < min_capped_inter ) { //Rolling if horde interested in new signal
            // TODO: Z-coordinate for mongroup targets
            const int targ_dist = rl_dist( p, mg.target );
            // TODO: Base this on targ_dist:dist ratio.
            if( targ_dist < 5 ) {  // If signal source already pursued by horde
                auto new_target = midpoint( mg.target, p );
                mg.set_target( new_target );
                const int min_inc_inter = 3; // Min interest increase to already targeted source
                const int inc_roll = rng( min_inc_inter, calculated_inter );
                mg.inc_interest( inc_roll );
                add_msg( m_debug, "horde inc interest %d dist %d", inc_roll, dist );
            } else { // New signal source
                mg.set_target( p );
                mg.set_interest( min_capped_inter );
                add_msg( m_debug, "horde set interest %d dist %d", min_capped_inter, dist );
            }
        }
    }
}

void overmap::place_mongroups()
{
    // Cities are full of zombies
    for( const city &elem : cities ) {
        if( get_option<bool>( "WANDER_SPAWNS" ) ) {
            if( !one_in( 16 ) || elem.size > 5 ) {
                mongroup m( GROUP_ZOMBIE,
                            project_combine( pos(), project_to<coords::sm>( tripoint_om_omt( elem.pos, 0 ) ) ),
                            static_cast<int>( elem.size * 2.5 ),
                            elem.size * 80 );
                //                m.set_target( zg.back().posx, zg.back().posy );
                m.horde = true;
                m.wander( *this );
                add_mon_group( m );
            }
        }
    }

    if( pos() == point_abs_om() ) {
        // Figure out where the dimensional lab is, and flood area with nether critters
        for( int x = 0; x < OMAPX; x++ ) {
            for( int y = 0; y < OMAPY; y++ ) {
                if( ter( tripoint_om_omt( x, y, 0 ) ) == "central_lab_entrance" ) {
                    add_mon_group( mongroup( GROUP_DIMENSIONAL_SURFACE, project_to<coords::sm>( tripoint_abs_omt( x, y,
                                             0 ) ), 5, 30 ) );
                }
            }
        }
    }

    // Place the "put me anywhere" groups
    int numgroups = rng( 0, 3 );
    for( int i = 0; i < numgroups; i++ ) {
        auto offset = tripoint_om_sm( rng( 0, OMAPX * 2 - 1 ), rng( 0, OMAPY * 2 - 1 ), 0 );
        add_mon_group( mongroup( GROUP_WORM, project_combine( pos(), offset ),
                                 rng( 20, 40 ), rng( 30, 50 ) ) );
    }
}

void overmap::place_radios()
{
    auto strength = []() {
        return rng( RADIO_MIN_STRENGTH, RADIO_MAX_STRENGTH );
    };
    std::string message;
    for( int i = 0; i < OMAPX; i++ ) {
        for( int j = 0; j < OMAPY; j++ ) {
            tripoint_om_omt pos_omt( i, j, 0 );
            point_om_sm pos_sm = project_to<coords::sm>( pos_omt.xy() );

            // Since location have id such as "radio_tower_1_north", we must check the beginning of the id
            if( is_ot_match( "radio_tower", ter( pos_omt ), ot_match_type::prefix ) ) {
                if( one_in( 3 ) ) {
                    radios.emplace_back( pos_sm, strength(), "", radio_type::WEATHER_RADIO );
                } else {
                    message = SNIPPET.expand( SNIPPET.random_from_category( "radio_archive" ).value_or(
                                                  translation() ).translated() );
                    radios.emplace_back( pos_sm, strength(), message );
                }
            } else if( is_ot_match( "lmoe", ter( pos_omt ), ot_match_type::prefix ) ) {
                message = string_format( _( "This is automated emergency shelter beacon %d%d."
                                            "  Supplies, amenities and shelter are stocked." ), i, j );
                radios.emplace_back( pos_sm, strength() / 2, message );
            } else if( is_ot_match( "fema_entrance", ter( pos_omt ), ot_match_type::prefix ) ) {
                message = string_format( _( "This is FEMA camp %d%d."
                                            "  Supplies are limited, please bring supplemental food, water, and bedding."
                                            "  This is FEMA camp %d%d.  A designated long-term emergency shelter." ), i, j, i, j );
                radios.emplace_back( pos_sm, strength(), message );
            } else if( ter( pos_omt ) == "central_lab_entrance" && pos() == point_abs_om() ) {
                std::string message =
                    _( "If you can hear this message, the probe to 021XC is functioning correctly." );
                // Repeat the message on different frequencies
                for( int i = 0; i < 10; i++ ) {
                    radios.emplace_back( pos_sm, RADIO_MAX_STRENGTH, message );
                }
            }
        }
    }
}

void overmap::open( const std::string &dim_id,
                    overmap_special_batch &enabled_specials )
{
    const auto ter_reader = [&]( std::istream & fin ) {
        overmap::unserialize( fin, string_format( "overmap terrain %d.%d", loc.x(), loc.y() ) );
    };

    if( g->get_active_world()->read_overmap( dim_id, loc, ter_reader ) ) {
        const auto plr_reader = [&]( std::istream & fin ) {
            overmap::unserialize_view( fin, string_format( "overmap visibility %d.%d", loc.x(), loc.y() ) );
        };
        g->get_active_world()->read_overmap_player_visibility( dim_id, loc, plr_reader );
    } else { // No map exists!  Prepare neighbors, and generate one.
        auto &owning_omb = get_overmapbuffer( dim_id );
        std::vector<const overmap *> pointers;
        // Fetch south and north
        for( int i = -1; i <= 1; i += 2 ) {
            pointers.push_back( owning_omb.get_existing( loc + point( 0, i ) ) );
        }
        // Fetch east and west
        for( int i = -1; i <= 1; i += 2 ) {
            pointers.push_back( owning_omb.get_existing( loc + point( i, 0 ) ) );
        }

        // pointers looks like (north, south, west, east)
        generate( pointers[0], pointers[3], pointers[1], pointers[2], enabled_specials );
    }
}

void overmap::add_mon_group( const mongroup &group )
{
    // Monster groups: the old system had large groups (radius > 1),
    // the new system transforms them into groups of radius 1, this also
    // makes the diffuse setting obsolete (as it only controls how the radius
    // is interpreted) - it's only used when adding monster groups with function.
    if( group.radius == 1 ) {
        zg.insert( std::pair<tripoint_om_sm, mongroup>( project_remain<coords::om>
                   ( group.abs_pos ).remainder_tripoint, group ) );
        return;
    }
    // diffuse groups use a circular area, non-diffuse groups use a rectangular area
    const int rad = std::max<int>( 0, group.radius );
    const double total_area = group.diffuse ? std::pow( rad + 1, 2 ) : ( rad * rad * M_PI + 1 );
    const double pop = std::max<int>( 0, group.population );
    for( int x = -rad; x <= rad; x++ ) {
        for( int y = -rad; y <= rad; y++ ) {
            const int dist = group.diffuse ? square_dist( point( x, y ), point_zero ) : trig_dist( point( x,
                             y ), point_zero );
            if( dist > rad ) {
                continue;
            }
            // Population on a single submap, *not* a integer
            double pop_here;
            if( rad == 0 ) {
                pop_here = pop;
            } else if( group.diffuse ) {
                pop_here = pop / total_area;
            } else {
                // non-diffuse groups are more dense towards the center.
                // This computation is delicate, be careful and see
                // https://github.com/CleverRaven/Cataclysm-DDA/issues/26941
                pop_here = ( static_cast<double>( rad - dist ) / rad ) * pop / total_area;
            }
            if( pop_here > pop || pop_here < 0 ) {
                dbg( DL::Error ) << "overmap::add_mon_group: " << group.type.str()
                                 << " - invalid population here: " << pop_here;
            }
            int p = std::max( 0, static_cast<int>( std::floor( pop_here ) ) );
            if( pop_here - p != 0 ) {
                // in case the population is something like 0.2, randomly add a
                // single population unit, this *should* on average give the correct
                // total population.
                const int mod = static_cast<int>( 10000.0 * ( pop_here - p ) );
                if( x_in_y( mod, 10000 ) ) {
                    p++;
                }
            }
            if( p == 0 ) {
                continue;
            }
            // Exact copy to keep all important values, only change what's needed
            // for a single-submap group.
            mongroup tmp( group );
            tmp.radius = 1;
            tmp.abs_pos += point( x, y );
            tmp.population = p;
            // This *can* create groups outside of the area of this overmap.
            // As this function is called during generating the overmap, the
            // neighboring overmaps might not have been generated and one can't access
            // them through the overmapbuffer as this would trigger generating them.
            // This would in turn to lead to a call to this function again.
            // To avoid this, the overmapbuffer checks the monster groups when loading
            // an overmap and moves groups with out-of-bounds position to another overmap.
            add_mon_group( tmp );
        }
    }
}

void overmap::for_each_npc( const std::function<void( npc & )> &callback )
{
    for( auto &guy : npcs ) {
        callback( *guy );
    }
}

void overmap::for_each_npc( const std::function<void( const npc & )> &callback ) const
{
for( auto &guy : npcs ) {
    callback( *guy );
    }
}

shared_ptr_fast<npc> overmap::find_npc( const character_id &id ) const
{
for( const auto &guy : npcs ) {
    if( guy->getID() == id ) {
            return guy;
        }
    }
    return nullptr;
}

bool overmap::is_omt_generated( const tripoint_om_omt &loc ) const
{
    if( !inbounds( loc ) ) {
    return false;
}

// Location is local to this overmap, but we need global submap coordinates
// for the mapbuffer lookup.
tripoint_abs_sm global_sm_loc =
    project_to<coords::sm>( project_combine( pos(), loc ) );

    // TODO: fix point types
    const bool is_generated =
        MAPBUFFER_REGISTRY.get( dimension_id_ ).lookup_submap( global_sm_loc ) != nullptr;

    return is_generated;
}

