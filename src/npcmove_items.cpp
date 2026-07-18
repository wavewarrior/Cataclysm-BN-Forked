#include "active_item_cache.h"
#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "bionics.h"
#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "cata_algo.h"
#include "catalua_coord.h"
#include "catalua_hooks.h"
#include "catalua_sol.h"
#include "character.h"
#include "character_functions.h"
#include "character_id.h"
#include "character_turn.h"
#include "clzones.h"
#include "creature_tracker.h"
#include "damage.h"
#include "debug.h"
#include "dispersion.h"
#include "effect.h"
#include "enums.h"
#include "explosion.h"
#include "field.h"
#include "field_type.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "gates.h"
#include "gun_mode.h"
#include "item.h"
#include "item_contents.h"
#include "item_functions.h"
#include "item_reload_option.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "line.h"
#include "map.h"
#include "map_iterator.h"
#include "mapdata.h"
#include "messages.h"
#include "mission.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h" // IWYU pragma: associated
#include "npc_action.h"
#include "npctalk.h"
#include "options.h"
#include "overmap.h"
#include "overmap_location.h"
#include "overmapbuffer.h"
#include "overmapbuffer_registry.h"
#include "player_activity.h"
#include "pldata.h"
#include "profile.h"
#include "projectile.h"
#include "ranged.h"
#include "ret_val.h"
#include "rng.h"
#include "sounds.h"
#include "stomach.h"
#include "translations.h"
#include "units.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "visitable.h"
#include "vpart_position.h"
#include "vpart_range.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <numeric>
#include <ostream>
#include <tuple>


static const activity_id ACT_PULP( "ACT_PULP" );

static const skill_id skill_firstaid( "firstaid" );

static const bionic_id bio_ads( "bio_ads" );
static const bionic_id bio_advreactor( "bio_advreactor" );
static const bionic_id bio_faraday( "bio_faraday" );
static const bionic_id bio_furnace( "bio_furnace" );
static const bionic_id bio_heat_absorb( "bio_heat_absorb" );
static const bionic_id bio_heatsink( "bio_heatsink" );
static const bionic_id bio_hydraulics( "bio_hydraulics" );
static const bionic_id bio_infolink( "bio_infolink" );
static const bionic_id bio_leukocyte( "bio_leukocyte" );
static const bionic_id bio_nanobots( "bio_nanobots" );
static const bionic_id bio_ods( "bio_ods" );
static const bionic_id bio_painkiller( "bio_painkiller" );
static const bionic_id bio_plutfilter( "bio_plutfilter" );
static const bionic_id bio_radscrubber( "bio_radscrubber" );
static const bionic_id bio_reactor( "bio_reactor" );
static const bionic_id bio_shock( "bio_shock" );
static const bionic_id bio_soporific( "bio_soporific" );

static const efftype_id effect_asthma( "asthma" );
static const efftype_id effect_bandaged( "bandaged" );
static const efftype_id effect_bite( "bite" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_bouldering( "bouldering" );
static const efftype_id effect_catch_up( "catch_up" );
static const efftype_id effect_disinfected( "disinfected" );
static const efftype_id effect_hallu( "hallu" );
static const efftype_id effect_hit_by_player( "hit_by_player" );
static const efftype_id effect_infected( "infected" );
static const efftype_id effect_lying_down( "lying_down" );
static const efftype_id effect_no_sight( "no_sight" );
static const efftype_id effect_npc_fire_bad( "npc_fire_bad" );
static const efftype_id effect_npc_flee_player( "npc_flee_player" );
static const efftype_id effect_npc_player_looking( "npc_player_still_looking" );
static const efftype_id effect_npc_run_away( "npc_run_away" );
static const efftype_id effect_onfire( "onfire" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_attention( "attention" );
static const efftype_id effect_feral_infighting_punishment( "feral_infighting_punishment" );

static const trait_id trait_ANIMALDISCORD( "ANIMALDISCORD" );
static const trait_id trait_ANIMALDISCORD2( "ANIMALDISCORD2" );
static const trait_id trait_ANIMALEMPATH( "ANIMALEMPATH" );
static const trait_id trait_ANIMALEMPATH2( "ANIMALEMPATH2" );
static const trait_id trait_BEE( "BEE" );
static const trait_id trait_FLOWERS( "FLOWERS" );
static const trait_id trait_MYCUS_FRIEND( "MYCUS_FRIEND" );
static const trait_id trait_PHEROMONE_INSECT( "PHEROMONE_INSECT" );
static const trait_id trait_PHEROMONE_MAMMAL( "PHEROMONE_MAMMAL" );
static const trait_id trait_PROF_FERAL( "PROF_FERAL" );
static const trait_id trait_TERRIFYING( "TERRIFYING" );
static const trait_id trait_THRESH_MYCUS( "THRESH_MYCUS" );

static const itype_id itype_battery( "battery" );
static const itype_id itype_chem_ethanol( "chem_ethanol" );
static const itype_id itype_chem_methanol( "chem_methanol" );
static const itype_id itype_denat_alcohol( "denat_alcohol" );
static const itype_id itype_inhaler( "inhaler" );
static const itype_id itype_lsd( "lsd" );
static const itype_id itype_smoxygen_tank( "smoxygen_tank" );
static const itype_id itype_thorazine( "thorazine" );
static const itype_id itype_oxygen_tank( "oxygen_tank" );

static const itype_id fuel_wind( "wind" );
static const itype_id fuel_sunlight( "sunlight" );

static constexpr float NPC_DANGER_VERY_LOW = 5.0f;
static constexpr float NPC_DANGER_MAX = 150.0f;
static constexpr float MAX_FLOAT = 5000000000.0f;

// Defined in npcmove.cpp
auto nearest_passable( const tripoint_bub_ms& p, const tripoint_bub_ms& closest_to )
-> std::optional<tripoint_bub_ms>;


void npc::find_item()
{
    if( is_hallucination() ) {
        see_item_say_smth( itype_thorazine, "<no_to_thorazine>" );
        see_item_say_smth( itype_lsd, "<yes_to_lsd>" );
        return;
    }

    if( is_player_ally() && !rules.has_flag( ally_rule::allow_pick_up ) ) {
        // Grabbing stuff not allowed by our "owner"
        return;
    }

    fetching_item = false;
    int best_value = minimum_item_value();
    // Not perfect, but has to mirror pickup code
    units::volume volume_allowed = volume_capacity() - volume_carried();
    units::mass weight_allowed = weight_capacity() - weight_carried();
    // For some reason range limiting by vision doesn't work properly
    const int range = 6;
    // int range = sight_range( g->light_level( bub_pos().z() ) );
    // range = std::max( 1, std::min( 12, range ) );

    static const zone_type_id zone_type_no_npc_pickup( "NO_NPC_PICKUP" );

    const item* wanted = nullptr;

    const bool whitelisting = has_item_whitelist();

    if( volume_allowed <= 0_ml || weight_allowed <= 0_gram ) { return; }

    const auto consider_item =
        [&wanted, &best_value, whitelisting, volume_allowed, weight_allowed,
    this]( const item & it, const tripoint_bub_ms & p ) {
        if( it.made_of( LIQUID ) ) {
            // Don't even consider liquids.
            return;
        }
        std::vector<npc *> followers;
        for( auto& elem : g->get_follower_list() ) {
            shared_ptr_fast<npc> npc_to_get = get_overmapbuffer( get_dimension() ).find_npc( elem );
            if( !npc_to_get ) { continue; }
            npc* npc_to_add = npc_to_get.get();
            followers.push_back( npc_to_add );
        }
        Character& player_character = get_player_character();
        for( auto& elem : followers ) {
            if( !it.is_owned_by( *this, true )
                && ( player_character.sees( bub_pos() ) || player_character.sees( wanted_item_pos )
                     || elem->sees( bub_pos() ) || elem->sees( wanted_item_pos ) ) ) {
                return;
            }
        }
        if( whitelisting && !item_whitelisted( it ) ) { return; }

        // When using a whitelist, skip the value check
        // TODO: Whitelist hierarchy?
        int itval = whitelisting ? 1000 : value( it );

        if( itval > best_value
            && ( it.volume() <= volume_allowed && it.weight() <= weight_allowed ) ) {
            wanted_item_pos = p;
            wanted = &( it );
            best_value = itval;
        }
    };

    map& here = get_map();
    // Harvest item doesn't exist, so we'll be checking by its name
    std::string wanted_name;
    const auto consider_terrain =
        [this, whitelisting, volume_allowed, &wanted, &wanted_name,
    &here]( const tripoint_bub_ms & p ) {
        // We only want to pick plants when there are no items to pick
        if( !whitelisting || wanted != nullptr || !wanted_name.empty()
            || volume_allowed < 250_ml ) {
            return;
        }

        const auto& harvest = here.get_harvest_names( p );
        for( const auto& entry : harvest ) {
            if( item_name_whitelisted( entry ) ) {
                wanted_name = entry;
                wanted_item_pos = p;
                break;
            }
        }
    };

    for( const tripoint_abs_ms& p : closest_points_first( abs_pos(), range ) ) {
        // TODO: Make this sight check not overdraw nearby tiles
        // TODO: Optimize that zone check
        const auto eq_bub_pos = p - abs_pos() + bub_pos();
        if( is_player_ally() && g->check_zone( zone_type_no_npc_pickup, eq_bub_pos ) ) { continue; }
        const int prev_num_items = ai_cache.searched_tiles.get( p, -1 );
        // Prefetch the number of items present so we can bail out if we already checked here.
        const map_stack m_stack = here.i_at( eq_bub_pos );
        int num_items = m_stack.size();
        const optional_vpart_position vp = here.veh_at( p );
        if( vp ) {
            const std::optional<vpart_reference> cargo = vp.part_with_feature( VPFLAG_CARGO, true );
            if( cargo ) {
                vehicle_stack v_stack = cargo->vehicle().get_items( cargo->part_index() );
                num_items += v_stack.size();
            }
        }
        if( prev_num_items == num_items ) { continue; }
        auto cache_tile = [this, &p, num_items, &wanted]() {
            if( wanted == nullptr ) { ai_cache.searched_tiles.insert( 1000, p, num_items ); }
        };
        bool can_see = false;
        if( here.sees_some_items( eq_bub_pos, *this ) && sees( eq_bub_pos ) ) {
            can_see = true;
            for( const item * const& it : m_stack ) { consider_item( *it, eq_bub_pos ); }
        }

        // Not cached because it gets checked once and isn't expected to change.
        if( can_see || sees( eq_bub_pos ) ) {
            can_see = true;
            consider_terrain( eq_bub_pos );
        }

        if( !vp || vp->vehicle().is_moving() || !( can_see || sees( eq_bub_pos ) ) ) {
            cache_tile();
            continue;
        }
        const std::optional<vpart_reference> cargo = vp.part_with_feature( VPFLAG_CARGO, true );
        static const std::string locked_string( "LOCKED" );
        // TODO: Let player know what parts are safe from NPC thieves
        if( !cargo || cargo->has_feature( locked_string ) ) {
            cache_tile();
            continue;
        }

        static const std::string cargo_locking_string( "CARGO_LOCKING" );
        if( vp.part_with_feature( cargo_locking_string, true ) ) {
            cache_tile();
            continue;
        }

        for( const item * const& it : cargo->vehicle().get_items( cargo->part_index() ) ) {
            consider_item( *it, eq_bub_pos );
        }
        cache_tile();
    }

    if( wanted != nullptr ) { wanted_name = wanted->tname(); }

    if( wanted_name.empty() ) { return; }

    fetching_item = true;

    // TODO: Move that check above, make it multi-target pathing and use it
    // to limit tiles available for choice of items
    const int dist_to_item = rl_dist( wanted_item_pos, bub_pos() );
    if( const std::optional<tripoint_bub_ms> dest = nearest_passable( wanted_item_pos, bub_pos() ) ) {
        update_path( *dest );
    }

    if( path.empty() && dist_to_item > 1 ) {
        // Item not reachable, let's just totally give up for now
        fetching_item = false;
    }

    if( fetching_item && rl_dist( wanted_item_pos, bub_pos() ) > 1 && is_walking_with() ) {
        say( _( "Hold on, I want to pick up that %s." ), wanted_name );
    }
}

void npc::pick_up_item()
{
    if( is_hallucination() ) { return; }

    if( !rules.has_flag( ally_rule::allow_pick_up ) && is_player_ally() ) {
        add_msg( m_debug, "%s::pick_up_item(); Canceling on player's request", name );
        fetching_item = false;
        moves -= 1;
        return;
    }

    map &here = get_map();
    const std::optional<vpart_reference> vp = here.veh_at( wanted_item_pos ).part_with_feature(
            VPFLAG_CARGO, false );
    const bool has_cargo = vp && !vp->has_feature( "LOCKED" );

    if( ( !here.has_items( wanted_item_pos ) && !has_cargo && !here.is_harvestable( wanted_item_pos )
          && sees( wanted_item_pos ) )
        || ( is_player_ally() && g->check_zone( zone_type_id( "NO_NPC_PICKUP" ), wanted_item_pos ) ) ) {
        // Items we wanted no longer exist and we can see it
        // Or player who is leading us doesn't want us to pick it up
        fetching_item = false;
        move_pause();
        add_msg( m_debug, "Canceling pickup - no items or new zone" );
        return;
    }

    add_msg( m_debug, "%s::pick_up_item(); [%d, %d, %d] => [%d, %d, %d]", name, bub_pos().x(),
             bub_pos().y(), bub_pos().z(), wanted_item_pos.x(), wanted_item_pos.y(),
             wanted_item_pos.z() );
    if( const std::optional<tripoint_bub_ms> dest = nearest_passable( wanted_item_pos, bub_pos() ) ) {
        update_path( *dest );
    }

    const int dist_to_pickup = rl_dist( bub_pos(), wanted_item_pos );

    bool cant_reach =
        dist_to_pickup > 1 || get_map().obstructed_by_vehicle_rotation( bub_pos(), wanted_item_pos );
    if( cant_reach && !path.empty() ) {
        add_msg( m_debug, "Moving; [%d, %d, %d] => [%d, %d, %d]", bub_pos().x(), bub_pos().y(),
                 bub_pos().z(), path[0].x(), path[0].y(), path[0].z() );

        move_to_next();
        return;
    } else if( cant_reach && path.empty() ) {
        add_msg( m_debug, "Can't find path" );
        // This can happen, always do something
        fetching_item = false;
        move_pause();
        return;
    }

    // We're adjacent to the item; grab it!

    auto picked_up = pick_up_item_map( wanted_item_pos );
    if( picked_up.empty() && has_cargo ) {
        picked_up = pick_up_item_vehicle( vp->vehicle(), vp->part_index() );
    }

    if( picked_up.empty() ) {
        // Last chance: plant harvest
        if( here.is_harvestable( wanted_item_pos ) ) {
            here.examine( *this, wanted_item_pos );
            // Note: we didn't actually pick up anything, just spawned items
            // but we want the item picker to find new items
            fetching_item = false;
            return;
        }
    }
    Character& player_character = get_player_character();
    // Describe the pickup to the player
    bool u_see = player_character.sees( *this ) || player_character.sees( wanted_item_pos );
    if( u_see ) {
        if( picked_up.size() == 1 ) {
            add_msg( _( "%1$s picks up a %2$s." ), name, picked_up.front()->tname() );
        } else if( picked_up.size() == 2 ) {
            add_msg( _( "%1$s picks up a %2$s and a %3$s." ), name, picked_up.front()->tname(),
                     picked_up.back()->tname() );
        } else if( picked_up.size() > 2 ) {
            add_msg( _( "%s picks up several items." ), name );
        } else {
            add_msg( _( "%s looks around nervously, as if searching for something." ), name );
        }
    }

    for( auto& it : picked_up ) {
        int itval = value( *it );
        if( itval < worst_item_value ) { worst_item_value = itval; }
        i_add( it->detach() );
    }

    moves -= 100;
    fetching_item = false;
    has_new_items = true;
}

template <typename T> std::vector<item *> npc_pickup_from_stack( npc& who, T& items )
{
    const bool whitelisting = who.has_item_whitelist();
    auto volume_allowed = who.volume_capacity() - who.volume_carried();
    auto weight_allowed = who.weight_capacity() - who.weight_carried();
    auto min_value = whitelisting ? 0 : who.minimum_item_value();
    std::vector<item *> picked_up;

    for( auto& iter : items ) {
        item& it = *iter;
        if( it.made_of( LIQUID ) ) { continue; }

        if( whitelisting && !who.item_whitelisted( it ) ) { continue; }

        auto volume = it.volume();
        if( volume > volume_allowed ) { continue; }

        auto weight = it.weight();
        if( weight > weight_allowed ) { continue; }

        int itval = whitelisting ? 1000 : who.value( it );
        if( itval < min_value ) { continue; }

        volume_allowed -= volume;
        weight_allowed -= weight;
        picked_up.push_back( &it );
    }

    return picked_up;
}

std::vector<item *> npc::pick_up_item_map( const tripoint_bub_ms& where )
{
    map_stack stack = get_map().i_at( where );
    return npc_pickup_from_stack( *this, stack );
}

std::vector<item *> npc::pick_up_item_vehicle( vehicle& veh, int part_index )
{
    auto stack = veh.get_items( part_index );
    return npc_pickup_from_stack( *this, stack );
}

// Used in npc::drop_items()
struct ratio_index {
    double ratio;
    int index;
    ratio_index( double R, int I ): ratio( R ), index( I ) {}
};

/* As of October 2019, this is buggy, do not use!! */
void npc::drop_items( units::mass drop_weight, units::volume drop_volume, int min_val )
{
    /* Remove this when someone debugs it back to functionality */
    return;

    add_msg( m_debug, "%s is dropping items-%3.2f kg, %3.2f L (%d items, wgt %3.2f/%3.2f kg, "
                      "vol %3.2f/%3.2f L)",
             name, units::to_kilogram( drop_weight ), units::to_liter( drop_volume ), inv.size(),
             units::to_kilogram( weight_carried() ), units::to_kilogram( weight_capacity() ),
             units::to_liter( volume_carried() ), units::to_liter( volume_capacity() ) );

    units::mass weight_dropped = units::from_gram( 0 );
    units::volume volume_dropped = units::from_liter( 0 );
    std::vector<ratio_index> rWgt, rVol; // Weight/Volume to value ratios

    // First fill our ratio vectors, so we know which things to drop first
    const_invslice slice = inv.const_slice();
    for( size_t i = 0; i < slice.size(); i++ ) {
        item& it = *slice[i]->front();
        double wgt_ratio = 0.0;
        double vol_ratio = 0.0;
        if( value( it ) == 0 || value( it ) <= min_val ) {
            wgt_ratio = 99999;
            vol_ratio = 99999;
        } else {
            wgt_ratio = units::to_gram<double>( it.weight() ) / value( it );
            vol_ratio = units::to_liter( it.volume() / value( it ) );
        }
        bool added_wgt = false;
        bool added_vol = false;
        for( size_t j = 0; j < rWgt.size() && !added_wgt; j++ ) {
            if( wgt_ratio > rWgt[j].ratio ) {
                added_wgt = true;
                rWgt.insert( rWgt.begin() + j, ratio_index( wgt_ratio, i ) );
            }
        }
        if( !added_wgt ) { rWgt.emplace_back( ratio_index( wgt_ratio, i ) ); }
        for( size_t j = 0; j < rVol.size() && !added_vol; j++ ) {
            if( vol_ratio > rVol[j].ratio ) {
                added_vol = true;
                rVol.insert( rVol.begin() + j, ratio_index( vol_ratio, i ) );
            }
        }
        if( !added_vol ) { rVol.emplace_back( ratio_index( vol_ratio, i ) ); }
    }

    map& here = get_map();
    std::string item_name;     // For description below
    int num_items_dropped = 0; // For description below
    // Now, drop items, starting from the top of each list
    while( weight_dropped < drop_weight || volume_dropped < drop_volume ) {
        // weight and volume may be passed as 0 or a negative value, to indicate that
        // decreasing that variable is not important.
        int dWeight =
            units::to_gram<int>( drop_weight ) <= 0
            ? -1
            : units::to_gram<int>( drop_weight - weight_dropped ) / 250;
        int dVolume =
            units::to_milliliter<int>( drop_volume ) <= 0
            ? -1
            : units::to_milliliter<int>( drop_volume - volume_dropped ) / 250;
        int index;
        // Which is more important, weight or volume?
        if( dWeight > dVolume ) {
            index = rWgt[0].index;
            rWgt.erase( rWgt.begin() );
            // Fix the rest of those indices.
            for( auto& elem : rWgt ) {
                if( elem.index > index ) { elem.index--; }
            }
        } else {
            index = rVol[0].index;
            rVol.erase( rVol.begin() );
            // Fix the rest of those indices.
            for( size_t i = 0; i < rVol.size(); i++ ) {
                if( i > rVol.size() ) {
                    debugmsg( "npc::drop_items() - looping through rVol - Size is %d, i is %d",
                              rVol.size(), i );
                }
                if( rVol[i].index > index ) { rVol[i].index--; }
            }
        }
        weight_dropped += slice[index]->front()->weight();
        volume_dropped += slice[index]->front()->volume();
        detached_ptr<item> dropped = i_rem( index );
        num_items_dropped++;
        if( num_items_dropped == 1 ) {
            item_name += dropped->tname();
        } else if( num_items_dropped == 2 ) {
            item_name += _( " and " ) + dropped->tname();
        }
        if( !is_hallucination() ) { // hallucinations can't drop real items
            here.add_item_or_charges( bub_pos(), std::move( dropped ) );
        }
    }
    // Finally, describe the action if u can see it
    if( get_player_character().sees( *this ) ) {
        if( num_items_dropped >= 3 ) {
            add_msg( vgettext( "%s drops %d item.", "%s drops %d items.", num_items_dropped ), name,
                     num_items_dropped );
        } else {
            add_msg( _( "%1$s drops a %2$s." ), name, item_name );
        }
    }
    update_worst_item_value();
}

bool npc::wield_better_weapon()
{
    const Creature* critter = current_target();
    const int dist = critter ? rl_dist( bub_pos(), critter->bub_pos() ) : -1;

    if( get_npc_ai_info_cache( npc_ai_info::range ) == dist ) {
        add_msg( m_debug, "Distance unchanged from last check, cancelling." );
        return false;
    }
    if( primary_weapon().has_flag( flag_NO_UNWIELD ) && cbm_toggled.is_null() ) {
        add_msg( m_debug, "Cannot unwield %s, not switching.",
                 primary_weapon().type->get_id().str() );
        return false;
    }

    // TODO: Allow wielding weaker weapons against weaker targets
    bool can_use_gun =
        ( ( !is_player_ally() || rules.has_flag( ally_rule::use_guns ) )
          && ( ai_cache.danger >= 3 || emergency() || dist < 0 ) );
    bool use_silent = ( is_player_ally() && rules.has_flag( ally_rule::use_silent ) );

    // Check if there's something better to wield
    item* best = &primary_weapon();
    double best_dps = -1;
    std::map<itype_id, gun_mode_id> mode_pairs;

    const auto compare_weapon =
    [this, &best, &best_dps, can_use_gun, use_silent, dist, &mode_pairs]( const item & it ) {
        bool gun_usable = can_use_gun && dist != 0 && ( !use_silent || it.is_silent() );
        double dps = 0.0f;
        auto [mode_id, mode_] = npc_ai::best_mode_for_range( *this, it, dist );

        if( mode_ && gun_usable ) {
            dps = it.ideal_ranged_dps( *this, mode_ );
            mode_pairs[it.typeId()] = mode_id;

            if( dps > best_dps ) {
                best = const_cast<item *>( &it );
                best_dps = dps;
            }
        } else {
            if( dist > 0 && dist > it.reach_range( *this ) ) { return; }
            dps = npc_ai::melee_value( *this, it );

            if( dps > best_dps ) {
                if( it.is_gun() ) { mode_pairs[it.typeId()] = it.gun_get_mode_id(); }
                best = const_cast<item *>( &it );
                best_dps = dps;
            }
        }
        add_msg( m_debug, "Evaluated %s at %.1f for distance %d", it.tname(), dps, dist );
    };

    compare_weapon( primary_weapon() );

    // Fists aren't checked below
    compare_weapon( null_item_reference() );

    // TOD: Once NPCs respect wielding costs more, find an efficient way
    // to have NPCs wield weapons with shorter ranges than dist in preparation
    // if they don't have a weapon with appropriate range/ammo.
    visit_items( [&compare_weapon, this]( item * node ) {
        // For worn items, only compare if they have a weapon category defined.
        if( is_worn( *node ) && node->type->weapon_category.empty() ) { return VisitResponse::SKIP; }
        // Otherwise, compare any melee usable item, guns or holstered items
        if( node->is_melee() || node->is_gun() ) {
            compare_weapon( *node );
        } else if( node->get_use( "holster" ) && !node->contents.empty()
                   && node != &primary_weapon() ) {
            // TODO: special case for "wield from wielded holster"
            const item& holstered = node->get_contained();
            if( holstered.is_melee() || holstered.is_gun() ) { compare_weapon( holstered ); }
        }
        return VisitResponse::SKIP;
    } );

    std::map<item *, bionic_id> toggled_list = check_toggle_cbm();
    for( const auto& [it, _] : toggled_list ) { compare_weapon( *it ); }

    set_npc_ai_info_cache( npc_ai_info::range, dist );

    // TODO: Reimplement switching to empty guns
    // Needs to check reload speed, RELOAD_ONE etc.
    // Until then, the NPCs should reload the guns as a last resort

    if( best == &primary_weapon() ) {
        add_msg( m_debug, "Wielded %s is best at %.1f, not switching", best->type->get_id().str(),
                 best_dps );
        if( best_dps >= 0 && primary_weapon().is_gun()
            && !primary_weapon().gun_set_mode( mode_pairs[primary_weapon().typeId()] ) ) {
            debugmsg( "Failed to set mode %s for %s", mode_pairs[primary_weapon().typeId()].c_str(),
                      primary_weapon().tname() );
        }
        return false;
    }

    add_msg( m_debug, "Wielding %s at value %.1f", best->type->get_id().str(), best_dps );

    if( toggled_list[best].is_valid() ) {
        cbm_toggled = toggled_list[best];
        cbm_fake_toggled = item::spawn( *best );
        if( is_armed() ) { stow_weapon(); }
        activate_bionic_by_id( cbm_toggled );
        if( primary_weapon().is_gun()
            && !primary_weapon().gun_set_mode( mode_pairs[primary_weapon().typeId()] ) ) {
            debugmsg( "Failed to set mode for %s", primary_weapon().tname() );
        }
        if( get_player_character().sees( bub_pos() ) ) {
            add_msg( m_info, _( "%s activates their %s." ), disp_name(), cbm_toggled->name );
        }

        if( !cbm_fake_active->is_null() && best->is_gun() ) {
            // They'll need time to swap weapons anyway, consolidates the comparisons into
            // check_or_use_bionics.
            cbm_fake_active.release();
            cbm_active = bionic_id::NULL_ID();
        }
        moves -= 15;
        return true;
    } else if( primary_weapon().typeId() == cbm_fake_toggled->typeId() ) {
        deactivate_bionic_by_id( cbm_toggled );
        cbm_toggled = bionic_id::NULL_ID();
        cbm_fake_toggled.release();
    }

    wield( *best );
    if( primary_weapon().is_gun()
        && !primary_weapon().gun_set_mode( mode_pairs[primary_weapon().typeId()] ) ) {
        debugmsg( "Failed to set mode for %s", primary_weapon().tname() );
    }
    return true;
}

void npc::scan_new_items()
{
    add_msg( m_debug, "%s scanning new items", name );
    wield_better_weapon();
    has_new_items = false;
    return;
    // TODO: Armor?
}

static void npc_throw( npc& np, item& it, const tripoint_bub_ms& pos )
{
    if( get_player_character().sees( np ) ) { add_msg( _( "%1$s throws a %2$s." ), np.name, it.tname() ); }

    detached_ptr<item> det = it.count_by_charges() ? it.split( 1 ) : it.detach();

    if( !np.is_hallucination() ) { // hallucinations only pretend to throw
        ranged::throw_item( np, pos, std::move( det ), std::nullopt );
    }
    np.clear_npc_ai_info_cache( npc_ai_info::range );
}

bool npc::alt_attack()
{
    if( ( is_player_ally() && !rules.has_flag( ally_rule::use_grenades ) ) || is_hallucination() ) {
        return false;
    }

    Creature* critter = current_target();
    if( critter == nullptr ) {
        // This function shouldn't be called...
        debugmsg( "npc::alt_attack() called with no target" );
        move_pause();
        return false;
    }

    auto tar = critter->bub_pos();

    const int dist = rl_dist( bub_pos(), tar );
    item* used = nullptr;
    // Remember if we have an item that is dangerous to hold
    bool used_dangerous = false;

    // TODO: The active bomb with shortest fuse should be thrown first
    const auto check_alt_item = [&used, &used_dangerous, dist, this]( item & it ) {
        const bool dangerous = it.has_flag( flag_NPC_THROW_NOW );
        if( !dangerous && used_dangerous ) { return; }

        // Guns with bayonets inherit the thrown flag, prevent NPCs from throwing it.
        if( it.is_gun() ) { return; }

        // Not alt attack
        if( !dangerous && !it.has_flag( flag_NPC_ALT_ATTACK ) ) { return; }

        // TODO: Non-thrown alt items
        if( !dangerous && throw_range( it ) < dist ) { return; }

        // Low priority items
        if( !dangerous && used != nullptr ) { return; }

        used = &it;
        used_dangerous = used_dangerous || dangerous;
    };

    check_alt_item( primary_weapon() );
    for( auto& sl : inv.const_slice() ) {
        // TODO: Cached values - an itype slot maybe?
        check_alt_item( *sl->front() );
    }

    if( used == nullptr ) { return false; }

    int weapon_index = get_item_position( used );
    if( weapon_index == INT_MIN ) {
        debugmsg( "npc::alt_attack() couldn't find expected item %s", used->tname() );
        return false;
    }

    // Are we going to throw this item?
    if( !used->is_active() && used->has_flag( flag_NPC_ACTIVATE ) ) {
        activate_item( weapon_index );
        // Note: intentional lack of return here
        // We want to ignore player-centric rules to avoid carrying live explosives
        // TODO: Non-grenades
    }

    // We are throwing it!
    int conf = confident_throw_range( *used, critter );
    const bool wont_hit = wont_hit_friend( tar, *used, true );
    if( dist <= conf && wont_hit ) {
        npc_throw( *this, *used, tar );
        return true;
    }

    if( wont_hit ) {
        // Within this block, our chosen target is outside of our range
        update_path( tar );
        move_to_next(); // Move towards the target
    }

    // Danger of friendly fire
    if( !wont_hit && !used_dangerous ) {
        // Safe to hold on to, for now
        // Maneuver around player
        avoid_friendly_fire();
        return true;
    }

    map& here = get_map();
    // We need to throw this live (grenade, etc) NOW! Pick another target?
    for( int dist = 2; dist <= conf; dist++ ) {
        for( const tripoint_bub_ms& pt : here.points_in_radius( bub_pos(), dist ) ) {
            const monster* const target_ptr = g->critter_at<monster>( pt );
            int newdist = rl_dist( bub_pos(), pt );
            // TODO: Change "newdist >= 2" to "newdist >= safe_distance(used)"
            if( newdist <= conf && newdist >= 2 && target_ptr && wont_hit_friend( pt, *used, true ) ) {
                // Friendlyfire-safe!
                ai_cache.target = g->shared_from( *target_ptr );
                if( !one_in( 100 ) ) {
                    // Just to prevent infinite loops...
                    if( alt_attack() ) { return true; }
                }
                return false;
            }
        }
    }
    /* If we have reached THIS point, there's no acceptable monster to throw our
     * grenade or whatever at.  Since it's about to go off in our hands, better to
     * just chuck it as far away as possible--while being friendly-safe.
     */
    int best_dist = 0;
    for( int dist = 2; dist <= conf; dist++ ) {
        for( const tripoint_bub_ms& pt : here.points_in_radius( bub_pos(), dist ) ) {
            int new_dist = rl_dist( bub_pos(), pt );
            if( new_dist > best_dist && wont_hit_friend( pt, *used, true ) ) {
                best_dist = new_dist;
                tar = pt;
            }
        }
    }
    /* Even if tar.x/tar.y didn't get set by the above loop, throw it anyway.  They
     * should be equal to the original location of our target, and risking friendly
     * fire is better than holding on to a live grenade / whatever.
     */
    npc_throw( *this, *used, tar );
    return true;
}

void npc::activate_item( int item_index )
{
    const int oldmoves = moves;
    item& it = i_at( item_index );
    if( it.is_tool() || it.is_food() ) { it.type->invoke( *this, it, bub_pos() ); }

    if( moves == oldmoves ) {
        // HACK: A hack to prevent debugmsgs when NPCs activate 0 move items
        // while not removing the debugmsgs for other 0 move actions
        moves--;
    }
}

