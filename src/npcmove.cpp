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


namespace
{
const std::vector<bionic_id> power_cbms = {{
        bio_advreactor,
        bio_furnace,
        bio_reactor,
    }
};

const std::vector<bionic_id> health_cbms = {{bio_leukocyte, bio_plutfilter}};

const int avoidance_vehicles_radius = 5;

} // namespace

std::string npc_action_name( npc_action action );

void print_action( const char* prepend, npc_action action );

static bool compare_sound_alert( const dangerous_sound& sound_a, const dangerous_sound& sound_b )
{
    if( sound_a.type != sound_b.type ) { return sound_a.type < sound_b.type; }
    return sound_a.volume < sound_b.volume;
}

static bool clear_shot_reach(
    const tripoint_bub_ms& from, const tripoint_bub_ms& to, bool check_ally = true )
{
    ZoneScopedN( "clear_shot_reach" );
    std::vector<tripoint_bub_ms> path = line_to( from, to );
    tripoint_bub_ms target_point = path.back();
    path.pop_back();
    if( path.empty() ) { return true; }
    tripoint_bub_ms& last_point = path[0];
    for( const tripoint_bub_ms& p : path ) {
        Creature* inter = g->critter_at( p );
        if( check_ally && inter != nullptr ) {
            return false;
        } else if( get_map().impassable( p ) ) {
            return false;
        } else if( get_map().obstructed_by_vehicle_rotation( last_point, p ) ) {
            return false;
        }
        last_point = p;
    }

    return !get_map().obstructed_by_vehicle_rotation( last_point, target_point );
}

tripoint_bub_ms npc::good_escape_direction( bool include_pos )
{
    map &here = get_map();
    if( path.empty() ) {
        zone_type_id retreat_zone = zone_type_id( "NPC_RETREAT" );
        const zone_manager &mgr = zone_manager::get_manager();
        std::optional<tripoint_abs_ms> retreat_target = mgr.get_nearest( retreat_zone, abs_pos(), 60,
            fac_id );
        if( retreat_target && *retreat_target != abs_pos() ) {
            update_path( here.abs_to_bub( tripoint_abs_ms( *retreat_target ) ) );
            if( !path.empty() ) {
                return path[0];
            }
        }
    }

    std::vector<tripoint_bub_ms> candidates;

    const auto rate_pt = [&]( const tripoint_bub_ms & pt, const float threat_val ) {
        if( !can_move_to( pt, !rules.has_flag( ally_rule::allow_bash ) ) ) { return MAX_FLOAT; }
        float rating = threat_val;
        for( const auto& e : here.field_at( pt ) ) {
            if( is_dangerous_field( e.second ) ) {
                // TODO: Rate fire higher than smoke
                rating += e.second.get_field_intensity();
            }
        }
        return rating;
    };

    float best_rating = include_pos ? rate_pt( bub_pos(), 0.0f ) : FLT_MAX;
    candidates.emplace_back( bub_pos() );

    for( direction pt_dir : npc_threat_dir ) {
        const tripoint_bub_ms& pt = bub_pos() + displace_XY( pt_dir );
        float cur_rating = rate_pt( pt, ai_cache.threat_map[std::to_underlying( pt_dir )] );
        if( cur_rating == best_rating ) {
            candidates.emplace_back( bub_pos() + displace_XY( pt_dir ) );
        } else if( cur_rating < best_rating ) {
            candidates.clear();
            candidates.emplace_back( bub_pos() + displace_XY( pt_dir ) );
            best_rating = cur_rating;
        }
    }
    return random_entry( candidates );
}

bool npc::sees_dangerous_field( const tripoint_bub_ms& p ) const
{
    return is_dangerous_fields( get_map().field_at( p ) );
}

bool npc::could_move_onto( const tripoint_bub_ms& p ) const
{
    map& here = get_map();
    if( !here.passable( p ) ) { return false; }

    if( !sees_dangerous_field( p ) ) { return true; }

    const auto fields_here = here.field_at( bub_pos() );
    for( const auto& e : here.field_at( p ) ) {
        if( !is_dangerous_field( e.second ) ) { continue; }

        const auto* entry_here = fields_here.find_field( e.first );
        if( entry_here == nullptr
            || entry_here->get_field_intensity() < e.second.get_field_intensity() ) {
            return false;
        }
    }

    return true;
}

std::vector<sphere> npc::find_dangerous_explosives() const
{
    std::vector<sphere> result;

    const auto active_items = get_map().get_active_items_in_radius(
                                  bub_pos(), g_max_view_distance, special_item_type::explosive );

    for( const auto& elem : active_items ) {
        const auto use = elem->type->get_use( "explosion" );

        if( !use ) { continue; }

        if( !sees( tripoint_bub_ms( elem->position() ) ) ) {
            continue; // We can't worry about what we can't see.
        }

        const explosion_iuse* actor = dynamic_cast<const explosion_iuse *>( use->get_actor_ptr() );
        const int safe_range = actor->explosion.safe_range();

        if( rl_dist( bub_pos(), elem->position() ) >= safe_range ) {
            continue; // Far enough.
        }

        const int turns_to_evacuate = 2 * safe_range / speed_rating();

        if( elem->charges > turns_to_evacuate ) {
            continue; // Consider only imminent dangers.
        }

        result.emplace_back( elem->position().raw(), safe_range );
    }

    return result;
}


npc_action npc::address_needs() { return address_needs( ai_cache.danger ); }

static bool wants_to_reload( const npc& who, const item& it )
{
    if( !who.can_reload( it ) ) { return false; }

    const int required = it.ammo_required();
    // TODO: Add bandolier check here, once they can be reloaded
    if( required < 1 && !it.is_magazine() ) { return false; }

    const int remaining = it.ammo_remaining();
    return remaining < required || remaining < it.ammo_capacity();
}

static bool wants_to_reload_with( const item& weap, const item& ammo, bool danger )
{
    // Only reload loose ammo if gun has integral magazine or not in danger.
    bool combat_reload = !ammo.is_magazine() && ( danger || weap.magazine_integral() );
    // If in danger, only swap magazines if ammo is both greater and it's sufficient for a shot
    // To prevent them from constantly swapping magazines while entering range.
    // If not in danger, swap magazines if ammo is greater than current.
    bool want_swap =
        ammo.ammo_remaining() > weap.ammo_remaining()
        && ( !danger || ammo.ammo_remaining() > weap.ammo_required() );
    return combat_reload || want_swap;
}

void npc::check_or_reload_cbm()
{
    if( get_npc_ai_info_cache( npc_ai_info::reloadable_cbms ) >= 0.0 ) {
        add_msg( m_debug, "Cancelling cbm reload check as cache is not negative." );
        return;
    }

    std::vector<std::pair<bionic_id, item *>> checklist = find_reloadable_cbms( *this );

    if( !checklist.empty() ) {
        for( auto& [bid, itm] : checklist ) {
            bionic& bio = get_bionic_state( bid );
            const item* it_loc = character_funcs::select_ammo( *this, *itm ).ammo;
            if( it_loc && wants_to_reload_with( *itm, *it_loc, ai_cache.danger > 0 ) ) {
                do_reload( *itm );
                bio.ammo_loaded =
                    itm->ammo_data() != nullptr ? itm->ammo_data()->get_id() : itype_id::NULL_ID();
                bio.ammo_count = static_cast<unsigned int>( itm->ammo_remaining() );
                return;
            }
        }
    }

    set_npc_ai_info_cache( npc_ai_info::reloadable_cbms, 5.0 );
    return;
}

item &npc::find_reloadable()
{
    if( get_npc_ai_info_cache( npc_ai_info::reloadables ) >= 0.0 ) {
        add_msg( m_debug, "Cancelling reload check as cache is not negative." );
        return null_item_reference();
    }
    // Check wielded gun, non-wielded guns, mags and tools
    // TODO: Build a proper gun->mag->ammo DAG (Directed Acyclic Graph)
    // to avoid checking same properties over and over
    // TODO: Make this understand bandoliers, pouches etc.
    // TODO: Cache items checked for reloading to avoid re-checking same items every turn
    // TODO: Make it understand smaller and bigger magazines
    item* reloadable = nullptr;
    visit_items( [this, &reloadable]( item * node ) {
        if( !wants_to_reload( *this, *node ) ) { return VisitResponse::NEXT; }
        const auto it_loc = character_funcs::select_ammo( *this, *node ).ammo;
        if( it_loc && wants_to_reload_with( *node, *it_loc, ai_cache.danger > 0 ) ) {
            reloadable = node;
            return VisitResponse::ABORT;
        }

        return VisitResponse::NEXT;
    } );

    if( reloadable != nullptr ) { return *reloadable; }

    set_npc_ai_info_cache( npc_ai_info::reloadables, 5.0 );
    return null_item_reference();
}

const item &npc::find_reloadable() const
{
    return const_cast<const item &>( const_cast<npc*>( this )->find_reloadable() );
}

bool npc::can_reload_current()
{
    if( !primary_weapon().is_gun() || !wants_to_reload( *this, primary_weapon() ) ) { return false; }

    return static_cast<bool>( find_usable_ammo( primary_weapon() ) );
}

item *npc::find_usable_ammo( item& weap )
{
    if( !can_reload( weap ) ) { return nullptr; }

    auto loc = character_funcs::select_ammo( *this, weap ).ammo;
    if( !loc || !wants_to_reload_with( weap, *loc, ai_cache.danger > 0 ) ) { return nullptr; }

    return loc;
}

item *npc::find_usable_ammo( item& weap ) const
{
    return const_cast<npc *>( this )->find_usable_ammo( weap );
}

void npc::adjust_power_cbms()
{
    if( !is_player_ally() || wants_to_recharge_cbm() ) { return; }
    for( const bionic_id& cbm_id : power_cbms ) { deactivate_bionic_by_id( cbm_id ); }
}

void npc::activate_combat_cbms()
{
    for( bionic& bio : get_bionic_collection() ) {
        if( bio.info().has_flag( flag_COMBAT_NPC_USE ) ) { activate_bionic( bio ); }
    }
    if( can_use_offensive_cbm() ) { check_or_use_weapon_cbm(); }
}

void npc::deactivate_combat_cbms()
{
    for( bionic& bio : get_bionic_collection() ) {
        if( bio.info().has_flag( flag_COMBAT_NPC_USE ) ) { deactivate_bionic( bio ); }
    }
    deactivate_bionic_by_id( bio_hydraulics );
    deactivate_weapon_cbm( *this );
    cbm_active = bionic_id::NULL_ID();
    cbm_fake_active.release();
    cbm_toggled = bionic_id::NULL_ID();
    cbm_fake_toggled.release();
}

bool npc::activate_bionic_by_id( const bionic_id& cbm_id, bool eff_only )
{
    if( has_bionic( cbm_id ) ) {
        bionic& bio = get_bionic_state( cbm_id );
        if( !bio.powered ) { return activate_bionic( bio, eff_only ); }
    }
    return false;
}

bool npc::use_bionic_by_id( const bionic_id& cbm_id, bool eff_only )
{
    if( has_bionic( cbm_id ) ) {
        bionic& bio = get_bionic_state( cbm_id );
        if( bio.powered ) {
            return true;
        } else {
            return activate_bionic( bio, eff_only );
        }
    }
    return false;
}

bool npc::deactivate_bionic_by_id( const bionic_id& cbm_id, bool eff_only )
{
    if( has_bionic( cbm_id ) ) {
        bionic& bio = get_bionic_state( cbm_id );
        if( bio.powered ) { return deactivate_bionic( bio, eff_only ); }
    }
    return false;
}

bool npc::wants_to_recharge_cbm()
{
    const units::energy curr_power = get_power_level();
    const float allowed_ratio = static_cast<int>( rules.cbm_recharge ) / 100.0f;
    const units::energy max_pow_allowed = get_max_power_level() * allowed_ratio;

    if( curr_power < max_pow_allowed ) {
        for( const bionic_id& bid : get_fueled_bionics() ) {
            if( !has_active_bionic( bid ) ) { return true; }
        }
        return get_fueled_bionics().empty(); // NPC might have power CBM that doesn't use the json
        // fuel_opts entry
    }
    return false;
}

bool npc::can_use_offensive_cbm() const
{
    const float allowed_ratio = static_cast<int>( rules.cbm_reserve ) / 100.0f;
    return get_power_level() > get_max_power_level() * allowed_ratio;
}

bool npc::consume_cbm_items( const std::function<bool( const item & )> &filter )
{
    const_invslice slice = inv.const_slice();
    int index = -1;
    for( size_t i = 0; i < slice.size(); i++ ) {
        item* const& it = slice[i]->front();
        const item& real_item = it->is_container() ? it->contents.front() : *it;
        if( filter( real_item ) ) {
            index = i;
            break;
        }
    }
    if( index < 0 ) { return false; }
    int old_moves = moves;
    consume( i_at( index ) );
    // TODO: a more reliable check for whether item has been consumed
    return old_moves != moves;
}

bool npc::recharge_cbm()
{
    // non-allied NPCs don't consume resources to recharge
    if( !is_player_ally() ) {
        mod_power_level( get_max_power_level() );
        return true;
    }

    for( bionic_id& bid : get_fueled_bionics() ) {
        if( has_active_bionic( bid ) ) { continue; }

        if( !get_fuel_available( bid ).empty() ) {
            use_bionic_by_id( bid );
            return true;
        } else {
            const std::function<bool( const item & )> fuel_filter = [bid]( const item & it ) {
                for( const itype_id& fid : bid->fuel_opts ) {
                    return it.typeId() == fid
                           || ( !it.is_container_empty() && it.contents.front().typeId() == fid );
                }
                return false;
            };

            if( consume_cbm_items( fuel_filter ) ) {
                use_bionic_by_id( bid );
                return true;
            } else {
                const std::vector<itype_id> fuel_op = bid->fuel_opts;
                const bool need_alcohol =
                    std::find( fuel_op.begin(), fuel_op.end(), itype_chem_ethanol ) != fuel_op.end()
                    || std::find( fuel_op.begin(), fuel_op.end(), itype_chem_methanol )
                    != fuel_op.end()
                    || std::find( fuel_op.begin(), fuel_op.end(), itype_denat_alcohol )
                    != fuel_op.end();
                const bool need_environment =
                    std::find( fuel_op.begin(), fuel_op.end(), fuel_sunlight ) != fuel_op.end()
                    || std::find( fuel_op.begin(), fuel_op.end(), fuel_wind ) != fuel_op.end();

                if( std::find( fuel_op.begin(), fuel_op.end(), itype_battery ) != fuel_op.end() ) {
                    complain_about( "need_batteries", 3_hours, "<need_batteries>", false );
                } else if( need_alcohol ) {
                    complain_about( "need_booze", 3_hours, "<need_booze>", false );
                } else if( need_environment ) {
                    // No Need for NPCs to complain about the weather and time of day...
                    continue;
                } else {
                    complain_about( "need_fuel", 3_hours, "<need_fuel>", false );
                }
            }
        }
    }

    if( use_bionic_by_id( bio_furnace ) ) {
        const std::function<bool( const item & )> furnace_filter = []( const item & it ) {
            return it.typeId() == itype_id( "withered" ) || it.typeId() == itype_id( "file" )
                   || it.has_flag( flag_FIREWOOD );
        };
        if( consume_cbm_items( furnace_filter ) ) {
            return true;
        } else {
            complain_about( "need_junk", 3_hours, "<need_junk>", false );
        }
    }

    return false;
}

healing_options npc::patient_assessment( const Character& c )
{
    healing_options try_to_fix;
    try_to_fix.clear_all();

    for( const auto& part : c.get_all_body_parts( true ) ) {
        const auto& bp = c.get_part( part );
        if( c.has_effect( effect_bleed, part.id() ) ) { try_to_fix.bleed = true; }

        if( c.has_effect( effect_bite, part.id() ) ) { try_to_fix.bite = true; }

        if( c.has_effect( effect_infected, part.id() ) ) { try_to_fix.infect = true; }
        int part_threshold = 75;
        if( part == bodypart_str_id( "head" ) ) {
            part_threshold += 20;
        } else if( part == bodypart_str_id( "torso" ) ) {
            part_threshold += 10;
        }
        part_threshold = std::min( 80, part_threshold );
        part_threshold = part_threshold * bp.get_hp_max() / 100;

        if( bp.get_hp_cur() <= part_threshold ) {
            if( !c.has_effect( effect_bandaged, part.id() ) ) { try_to_fix.bandage = true; }
            if( !c.has_effect( effect_disinfected, part.id() ) ) { try_to_fix.disinfect = true; }
        }
    }
    return try_to_fix;
}

npc_action npc::address_needs( float danger )
{
    Character& player_character = get_player_character();
    // rng because NPCs are not meant to be hypervigilant hawks that notice everything
    // and swing into action with alarming alacrity.
    // no sometimes they are just looking the other way, sometimes they hestitate.
    // ( also we can get huge performance boosts )
    if( one_in( 3 ) ) {
        healing_options try_to_fix_me = patient_assessment( *this );
        if( try_to_fix_me.any_true() ) {
            if( !use_bionic_by_id( bio_nanobots ) ) {
                ai_cache.can_heal = has_healing_options( try_to_fix_me );
                if( ai_cache.can_heal.any_true() ) { return npc_heal; }
            }
        } else {
            deactivate_bionic_by_id( bio_nanobots );
        }
        if( get_skill_level( skill_firstaid ) > 0 ) {
            if( is_player_ally() ) {
                healing_options try_to_fix_other = patient_assessment( player_character );
                if( try_to_fix_other.any_true() ) {
                    ai_cache.can_heal = has_healing_options( try_to_fix_other );
                    if( ai_cache.can_heal.any_true() ) {
                        ai_cache.ally = g->shared_from( player_character );
                        return npc_heal_player;
                    }
                }
            }
            for( const npc& guy : g->all_npcs() ) {
                if( &guy == this || !guy.is_ally( *this ) || guy.bub_pos().z() != bub_pos().z()
                    || !sees( guy ) ) {
                    continue;
                }
                healing_options try_to_fix_other = patient_assessment( guy );
                if( try_to_fix_other.any_true() ) {
                    ai_cache.can_heal = has_healing_options( try_to_fix_other );
                    if( ai_cache.can_heal.any_true() ) {
                        ai_cache.ally = g->shared_from( guy );
                        return npc_heal_player;
                    }
                }
            }
        }
    }

    if( one_in( 3 ) ) {
        if( get_perceived_pain() >= 15 ) {
            if( !activate_bionic_by_id( bio_painkiller ) && has_painkiller() && !took_painkiller() ) {
                return npc_use_painkiller;
            }
        } else {
            deactivate_bionic_by_id( bio_painkiller );
        }
    }

    if( one_in( 3 ) && can_reload_current() ) { return npc_reload; }

    check_or_reload_cbm();

    item& reloadable = find_reloadable();
    if( !reloadable.is_null() ) {
        do_reload( reloadable );
        return npc_noop;
    }

    // Extreme thirst or hunger, bypass safety check.
    if( get_thirst() > thirst_levels::dehydrated
        || get_stored_kcal() + stomach.get_calories() < max_stored_kcal() * 0.75 ) {
        if( consume_food() ) { return npc_noop; }
    }
    // Does the hallucination needs to disappear ?
    if( is_hallucination() && player_character.sees( *this ) ) {
        if( !player_character.has_effect( effect_hallu ) ) { die( nullptr ); }
    }

    if( danger > NPC_DANGER_VERY_LOW ) { return npc_undecided; }

    if( one_in( 3 )
        && ( get_thirst() > thirst_levels::thirsty
             || get_stored_kcal() + stomach.get_calories() < max_stored_kcal() * 0.95 ) ) {
        if( consume_food() ) { return npc_noop; }
    }

    if( one_in( 3 ) && wants_to_recharge_cbm() && recharge_cbm() ) { return npc_noop; }

    if( one_in( 3 ) && find_corpse_to_pulp() ) {
        if( !do_pulp() ) { move_to_next(); }
        return npc_noop;
    }

    if( one_in( 3 ) && adjust_worn() ) { return npc_noop; }

    const auto could_sleep = [&]() {
        if( danger <= 0.01 ) {
            if( get_fatigue() >= fatigue_levels::tired ) {
                return true;
            } else if( is_walking_with() && player_character.in_sleep_state()
                       && get_fatigue() > ( fatigue_levels::tired / 2 ) ) {
                return true;
            }
        }
        return false;
    };
    // TODO: More risky attempts at sleep when exhausted
    if( one_in( 3 ) && could_sleep() ) {
        if( !is_player_ally() ) {
            // TODO: Make tired NPCs handle sleep offscreen
            set_fatigue( 0 );
            return npc_undecided;
        }

        if( rules.has_flag( ally_rule::allow_sleep ) || get_fatigue() > fatigue_levels::massive ) {
            return npc_sleep;
        } else if( player_character.in_sleep_state() ) {
            // TODO: "Guard me while I sleep" command
            return npc_sleep;
        }
    }
    // TODO: Mutation & trait related needs
    // e.g. finding glasses; getting out of sunlight if we're an albino; etc.

    return npc_undecided;
}

npc_action npc::address_player()
{
    Character& player_character = get_player_character();
    if( ( attitude == NPCATT_TALK ) && sees( player_character ) ) {
        if( player_character.in_sleep_state() ) {
            // Leave sleeping characters alone.
            return npc_undecided;
        }
        if( rl_dist( bub_pos(), player_character.bub_pos() ) <= 6 ) {
            return npc_talk_to_player; // Close enough to talk to you
        } else {
            if( one_in( 10 ) ) { say( "<lets_talk>" ); }
            return npc_follow_player;
        }
    }

    if( attitude == NPCATT_MUG && sees( player_character ) ) {
        if( one_in( 3 ) ) { say( _( "Don't move a <swear> muscle…" ) ); }
        return npc_mug_player;
    }

    if( attitude == NPCATT_WAIT_FOR_LEAVE ) {
        patience--;
        if( patience <= 0 ) {
            patience = 0;
            set_attitude( NPCATT_KILL );
            return npc_noop;
        }
        return npc_undecided;
    }

    if( attitude == NPCATT_FLEE_TEMP ) { return npc_flee; }

    if( attitude == NPCATT_LEAD ) {
        if( rl_dist( bub_pos(), player_character.bub_pos() ) >= 12 || !sees( player_character ) ) {
            int intense = get_effect_int( effect_catch_up );
            if( intense < 10 ) {
                say( "<keep_up>" );
                add_effect( effect_catch_up, 5_turns );
                return npc_pause;
            } else {
                say( "<im_leaving_you>" );
                set_attitude( NPCATT_NULL );
                return npc_pause;
            }
        } else if( has_omt_destination() ) {
            return npc_goto_destination;
        } else { // At goal. Now, waiting on nearby player
            return npc_pause;
        }
    }
    return npc_undecided;
}

npc_action npc::long_term_goal_action()
{
    add_msg( m_debug, "long_term_goal_action()" );

    if( mission == NPC_MISSION_SHOPKEEP || mission == NPC_MISSION_SHELTER
        || ( is_player_ally() && mission != NPC_MISSION_TRAVELLING ) ) {
        return npc_pause; // Shopkeepers just stay put.
    }

    if( !has_omt_destination() ) { set_omt_destination(); }

    if( has_omt_destination() ) {
        if( mission != NPC_MISSION_TRAVELLING ) {
            set_mission( NPC_MISSION_TRAVELLING );
            set_attitude( attitude );
        }
        return npc_goto_destination;
    }

    return npc_undecided;
}

double npc::confidence_mult() const
{
    if( !is_player_ally() || is_player() ) {
    return 1.0f;
}

switch( rules.aim ) {
    case aim_rule::WHEN_CONVENIENT:
        return emergency() ? 1.5f : 1.0f;
        case aim_rule::SPRAY:
            return 2.0f;
        case aim_rule::PRECISE:
            return emergency() ? 1.0f : 0.75f;
        case aim_rule::STRICTLY_PRECISE:
            return 0.5f;
    }

    return 1.0f;
}

int npc::confident_shoot_range( const item &it, int recoil ) const
{
    if( !it.is_gun() ) {
    return 0;
}
const auto gun_mode_cmp = []( const std::pair<gun_mode_id, gun_mode> &lhs,
const std::pair<gun_mode_id, gun_mode> &rhs ) {
    return lhs.second.qty < rhs.second.qty;
};
std::map<gun_mode_id, gun_mode> modes = it.gun_all_modes();
if( modes.empty() ) {
    debugmsg( "%s has no gun modes", it.tname() );
        return 0;
    }
    auto best = std::min_element( modes.begin(), modes.end(), gun_mode_cmp );
    return confident_gun_mode_range( ( *best ).second, recoil );
}

int npc::confident_gun_mode_range( const gun_mode &gun, int at_recoil ) const
{
    if( !gun || gun.melee() ) {
    return 0;
}

const std::optional<shape_factory> shaped = ranged::get_shape_factory( *gun.target );
if( shaped ) {
    return static_cast<int>( shaped->get_range() ) - 1;
    }

    // Doesn't use calculate_dispersion because that requires a map
    // TODO: Turn this into a common function.
    int gun_recoil = gun->gun_recoil();
    int eff_recoil = at_recoil + ( gun.qty > 1 ? ranged::burst_penalty( *this, *gun, gun_recoil ) : 0 );
    dispersion_sources mode_disp = ranged::get_weapon_dispersion( *this, *gun );
    mode_disp.add_range( eff_recoil );
    double max_dispersion = mode_disp.max();
    if( gun->ammo_current() ) {
    max_dispersion += gun->ammo_current()->ammo->dispersion;
    }
    double even_chance_range = range_with_even_chance_of_good_hit( max_dispersion );
    double confident_range = even_chance_range * confidence_mult();
    add_msg( m_debug, "%s confident_gun (%s<=%.2f) at %.1f", gun->tname(), gun.name(),
             confident_range, max_dispersion );
    return std::max<int>( confident_range, 1 );
}

int npc::confident_throw_range( const item& thrown, Creature* target ) const
{
    double average_dispersion = ranged::throwing_dispersion( *this, thrown, target, false ) / 2.0;
    double even_chance_range =
        ( target == nullptr ? 0.5 : target->ranged_target_size() ) / average_dispersion;
    double confident_range = even_chance_range * confidence_mult();
    add_msg( m_debug, "confident_throw_range == %d", static_cast<int>( confident_range ) );
    return static_cast<int>( confident_range );
}

auto item::ideal_ranged_dps( const Character &who, std::optional<gun_mode> &mode ) const -> double
{
    if( !is_gun() || is_gunmod() || !mode ) {
    return 0;
}
damage_instance gun_damage = this->gun_damage();
if( ammo_current() ) {
    itype_id ammo = ammo_current();
        gun_damage.add( ammo->ammo->damage );
    } else if( ammo_default() ) {
    itype_id ammo = ammo_default();
        gun_damage.add( ammo->ammo->damage );
    }
    int burst_size = mode->qty;
    if( burst_size <= 0 ) {
    debugmsg( "gun_mode for %s has burst size of 0", this->tname() );
        burst_size = 1;
    }
    float damage_factor = gun_damage.total_damage() * burst_size;

    int move_cost = ranged::time_to_attack( who, *this, nullptr );
    if( ammo_remaining() == 0 ) {
    int reload_cost = get_reload_time() + who.encumb( body_part_hand_l ) + who.encumb(
                              body_part_hand_r );
        // HACK: Doesn't check how much ammo they'll actually get from the reload. Because we don't know.
        // DPS is less impacted the larger the magazine being swapped.
        reload_cost /= magazine_integral() ? 1 : std::max( 1, ammo_capacity() / burst_size );
        move_cost += reload_cost;
    }
    std::vector<ranged::aim_type> aim_types = ranged::get_aim_types( who, *this );
    auto regular = std::find_if( aim_types.begin(),
    aim_types.end(), []( const ranged::aim_type & at ) {
        return at.action == std::string( "AIMED_SHOT" );
    } );
    if( regular == aim_types.end() ) {
    debugmsg( "Could not find REGULAR aim type for gun %s", tname() );
        return 0;
    }
    move_cost += ranged::gun_engagement_moves( who, *this, ( *regular ).threshold );

    double dps = damage_factor / ( move_cost / 100.0f );

    return dps;
}

// Index defaults to -1, i.e., wielded weapon
bool npc::wont_hit_friend( const tripoint_bub_ms& tar, const item& it, bool throwing ) const
{
    // TODO: Get actual dispersion instead of extracting it (badly) from confident range
    int confident = throwing ?
                    confident_throw_range( it, nullptr ) :
    confident_shoot_range( it, ranged::recoil_total( *this ) );
    // if there is no confidence at using weapon, it's not used at range
    // zero confidence leads to divide by zero otherwise
    if( confident < 1 ) {
    return true;
}

if( rl_dist( bub_pos(), tar ) == 1 ) {
    return true;    // If we're *really* sure that our aim is dead-on
}

units::angle target_angle = coord_to_angle( bub_pos(), tar );

// TODO: Base on dispersion
units::angle safe_angle = 30_degrees;

for( const auto &fr : ai_cache.friends ) {
    const shared_ptr_fast<Creature> ally_p = fr.lock();
        if( !ally_p ) {
            continue;
        }
        const Creature &ally = *ally_p;

        // TODO: Extract common functions with turret target selection
        units::angle safe_angle_ally = safe_angle;
        int ally_dist = rl_dist( bub_pos(), ally.bub_pos() );
        // Skip adjacent allies - ballistics code now protects them
        if( ally_dist <= 1 ) { continue; }
        if( ally_dist < 3 ) { safe_angle_ally += ( 3 - ally_dist ) * 30_degrees; }

        units::angle ally_angle = coord_to_angle( bub_pos(), ally.bub_pos() );
        units::angle angle_diff = units::fabs( ally_angle - target_angle );
        angle_diff = std::min( 360_degrees - angle_diff, angle_diff );
        if( angle_diff < safe_angle_ally ) {
            // TODO: Disable NPC whining is it's other NPC who prevents aiming
            return false;
        }
    }

    return true;
}

bool npc::enough_time_to_reload( const item& gun ) const
{
    int rltime =
        item_reload_cost( gun, *item::spawn_temporary( gun.ammo_default() ), gun.ammo_capacity() );
    const float turns_til_reloaded = static_cast<float>( rltime ) / get_speed();

    const Creature* target = current_target();
    if( target == nullptr ) {
    // No target, plenty of time to reload
    return true;
}

const auto distance = rl_dist( bub_pos(), target->bub_pos() );
const float target_speed = target->speed_rating();
const float turns_til_reached = distance / target_speed;
if( target->is_player() || target->is_npc() ) {
    auto& c = dynamic_cast<const Character &>( *target );
        if( sees( c ) && c.primary_weapon().is_gun() && rltime > 200
            && c.primary_weapon().gun_range( true ) > distance + turns_til_reloaded / target_speed ) {
            // Don't take longer than 2 turns if player has a gun
            return false;
        }
    }

    // TODO: Handle monsters with ranged attacks and players with CBMs
    return turns_til_reloaded < turns_til_reached;
}

bool npc::aim()
{
    gun_mode mode =
        cbm_active.is_null()
        ? primary_weapon().gun_current_mode()
        : cbm_fake_active->gun_current_mode();
    if( !mode ) {
        std::string error_weapon =
            cbm_active.is_null() ? primary_weapon().tname() : cbm_fake_active->tname();
        debugmsg( "NPC tried to aim %s without valid mode.", error_weapon );
    }

    bool did_aim = false;
    double aim_amount = ranged::aim_per_move( *this, *mode.target, recoil );
    while( aim_amount > 0 && recoil > 0 && moves > 0 ) {
        did_aim = true;
        moves--;
        recoil -= aim_amount;
        recoil = std::max( 0.0, recoil );
        aim_amount = ranged::aim_per_move( *this, *mode.target, recoil );
    }

    return did_aim;
}

bool npc::update_path( const tripoint_bub_ms& p, const bool no_bashing, bool force )
{
    if( p == bub_pos() ) {
        path.clear();
        return true;
    }

    while( !path.empty() && path[0] == bub_pos() ) { path.erase( path.begin() ); }

    if( !path.empty() ) {
        const auto& last = path[path.size() - 1];
        if( last == p && ( path[0].z() != bub_pos().z() || rl_dist( path[0], bub_pos() ) <= 1 ) ) {
            // Our path already leads to that point, no need to recalculate
            return true;
        }
    }

    auto new_path = get_map().route(
                        bub_pos(), p, get_legacy_pathfinding_settings( no_bashing ), get_legacy_path_avoid() );
    if( new_path.empty() ) {
        if( !ai_cache.sound_alerts.empty() ) {
            ai_cache.sound_alerts.erase( ai_cache.sound_alerts.begin() );
            add_msg( m_debug, "failed to path to sound alert %d,%d,%d->%d,%d,%d", bub_pos().x(),
                     bub_pos().y(), bub_pos().z(), p.x(), p.y(), p.z() );
        }
        add_msg( m_debug, "Failed to path %d,%d,%d->%d,%d,%d", bub_pos().x(), bub_pos().y(),
                 bub_pos().z(), p.x(), p.y(), p.z() );
    }

    while( !new_path.empty() && new_path[0] == bub_pos() ) { new_path.erase( new_path.begin() ); }

    if( !new_path.empty() || force ) {
        path = std::move( new_path );
        return true;
    }

    return false;
}

bool npc::can_open_door( const tripoint_bub_ms& p, const bool inside ) const
{
    return !rules.has_flag( ally_rule::avoid_doors ) && get_map().can_open_door( this, p, inside );
}

bool npc::can_move_to( const tripoint_bub_ms& p, bool no_bashing ) const
{
    map& here = get_map();
    // Allow moving into any bashable spots, but penalize them during pathing
    // Doors are not passable for hallucinations
    return (
               rl_dist( bub_pos(), p ) <= 1 && here.has_floor( p ) && !g->is_dangerous_tile( p )
               && ( here.passable( p )
                    || ( can_open_door( p, !here.is_outside( bub_pos() ) ) && !is_hallucination() )
                    || ( !no_bashing && here.bash_rating( smash_ability(), p ) > 0 ) ) );
}

void npc::move_to( const tripoint_bub_ms& pt, bool no_bashing, std::set<tripoint_bub_ms> *nomove )
{
    auto p = pt;

    const auto hook_results = cata::run_hooks( "on_npc_try_move", [ &, this]( sol::table & params ) {
        params["npc"] = this;
        params["from"] = cata::detail::lua_coords::to_lua( bub_pos() );
        params["to"] = cata::detail::lua_coords::to_lua( p );
        params["movement_mode"] = get_movement_mode();
        params["via_ramp"] = false;
        if( is_mounted() ) {
            params["mounted"] = true;
            params["mount"] = mounted_creature.get();
        } else {
            params["mounted"] = false;
        }
    } );

    const auto char_hook_results =
    cata::run_hooks( "on_character_try_move", [ &, this]( sol::table & params ) {
        params["char"] = static_cast<Character *>( this );
        params["from"] = cata::detail::lua_coords::to_lua( bub_pos() );
        params["to"] = cata::detail::lua_coords::to_lua( p );
        params["movement_mode"] = get_movement_mode();
        params["via_ramp"] = false;
        if( is_mounted() ) {
            params["mounted"] = true;
            params["mount"] = mounted_creature.get();
        } else {
            params["mounted"] = false;
        }
    } );

    if( !hook_results.get_or( "allowed", true ) || !char_hook_results.get_or( "allowed", true ) ) {
        return;
    }

    map& here = get_map();
    bool ceiling_blocking_climb =
        !here.has_floor_or_support( bub_pos() ) || here.has_floor_or_support( p + tripoint_above );
    if( sees_dangerous_field( p ) || ( nomove != nullptr && nomove->contains( p ) ) ) {
        // Move to a neighbor field instead, if possible.
        // Maybe this code already exists somewhere?
        auto other_points = here.get_dir_circle( bub_pos(), p );
        for( const tripoint_bub_ms& ot : other_points ) {
            if( could_move_onto( ot ) && ( nomove == nullptr || !nomove->contains( ot ) ) ) {

                p = ot;
                break;
            }
        }
    }

    recoil = MAX_RECOIL;

    if( has_effect( effect_stunned ) ) {
        p.x() = rng( bub_pos().x() - 1, bub_pos().x() + 1 );
        p.y() = rng( bub_pos().y() - 1, bub_pos().y() + 1 );
        p.z() = bub_pos().z();
    }

    // nomove is used to resolve recursive invocation, so reset destination no
    // matter it was changed by stunned effect or not.
    if( nomove != nullptr && nomove->contains( p ) ) { p = bub_pos(); }

    // "Long steps" are allowed when crossing z-levels
    // Stairs teleport the player too
    if( rl_dist( bub_pos(), p ) > 1 && p.z() == bub_pos().z() ) {
        // On the same level? Not so much. Something weird happened
        path.clear();
        move_pause();
    }

    if( here.obstructed_by_vehicle_rotation( bub_pos(), p ) ) {
        move_pause();
        return;
    }

    bool attacking = false;
    if( g->critter_at<monster>( p ) ) { attacking = true; }
    if( !move_effects( attacking ) ) {
        mod_moves( -100 );
        return;
    }

    Creature* critter = g->critter_at( p );
    if( critter != nullptr ) {
        if( critter == this ) { // We're just pausing!
            move_pause();
            return;
        }
        const auto att = attitude_to( *critter );
        if( att == Attitude::A_HOSTILE ) {
            if( !no_bashing ) {
                warn_about( "cant_flee", 5_turns + rng( 0, 5 ) * 1_turns );
                melee_attack( *critter, true );
            } else {
                move_pause();
            }

            return;
        }

        if( critter->is_avatar() ) { say( "<let_me_pass>" ); }

        // Let NPCs push each other when non-hostile
        // TODO: Have them attack each other when hostile
        npc* np = dynamic_cast<npc *>( critter );
        if( np != nullptr && !np->in_sleep_state() ) {
            std::unique_ptr<std::set<tripoint_bub_ms>> newnomove;
            std::set<tripoint_bub_ms> *realnomove;
            if( nomove != nullptr ) {
                realnomove = nomove;
            } else {
                // create the no-move list
                newnomove = std::make_unique<std::set<tripoint_bub_ms>>();
                realnomove = newnomove.get();
            }
            // other npcs should not try to move into this npc anymore,
            // so infinite loop can be avoided.
            realnomove->insert( bub_pos() );
            // Don't spam player with messages over followers blunder into each other.
            if( !np->is_following() ) { say( "<let_me_pass>" ); }
            np->move_away_from( bub_pos(), true, realnomove );
            // if we moved NPC, readjust their path, so NPCs don't jostle each other out of their
            // activity paths.
            if( np->attitude == NPCATT_ACTIVITY ) {
                std::vector<tripoint_bub_ms> activity_route = np->get_auto_move_route();
                if( !activity_route.empty() && !np->has_destination_activity() ) {
                    tripoint_bub_ms final_destination;
                    if( destination_point ) {
                        final_destination = here.abs_to_bub( *destination_point );
                    } else {
                        final_destination = activity_route.back();
                    }
                    np->update_path( final_destination );
                }
            }
        }

        if( critter->bub_pos() == p ) {
            move_pause();
            return;
        }
    }

    // Boarding moving vehicles is fine, unboarding isn't
    bool moved = false;
    if( const optional_vpart_position vp = here.veh_at( bub_pos() ) ) {
        const optional_vpart_position ovp = here.veh_at( p );
        if( vp->vehicle().is_moving()
            && ( veh_pointer_or_null( ovp ) != veh_pointer_or_null( vp )
                 || !ovp.part_with_feature( VPFLAG_BOARDABLE, true ) ) ) {
            move_pause();
            return;
        }
    }

    if( p.z() != bub_pos().z() ) {
        // Z-level move
        // For now just teleport to the destination
        // TODO: Make it properly find the tile to move to
        if( is_mounted() ) {
            move_pause();
            return;
        }
        moves -= 100;
        moved = true;
    } else if( here.passable( p ) && !here.has_flag( "DOOR", p ) ) {
        bool diag = trigdist && bub_pos().x() != p.x() && bub_pos().y() != p.y();
        if( is_mounted() ) {
            const double base_moves =
                run_cost( here.combined_movecost( bub_pos(), p ), diag ) * 100.0
                / mounted_creature->get_speed();
            const double encumb_moves = get_weight() / 4800.0_gram;
            moves -= static_cast<int>( std::ceil( base_moves + encumb_moves ) );
            if( mounted_creature->has_flag( MF_RIDEABLE_MECH ) ) {
                mounted_creature->use_mech_power( -1 );
            }
        } else {
            moves -= run_cost( here.combined_movecost( bub_pos(), p ), diag );
        }
        moved = true;
    } else if( here.can_open_door( this, p, !here.is_outside( bub_pos() ) ) ) {
        if( !is_hallucination() ) { // hallucinations don't open doors
            here.open_door( this, p, !here.is_outside( bub_pos() ) );
            moves -= 100;
        } else { // hallucinations teleport through doors
            moves -= 100;
            moved = true;
        }
    } else if( get_dex() > 1 && here.has_flag_ter_or_furn( "CLIMBABLE", p )
               && !ceiling_blocking_climb ) {
        ///\EFFECT_DEX_NPC increases chance to climb CLIMBABLE furniture or terrain
        int climb = get_dex();
        if( one_in( climb ) ) {
            add_msg_if_npc(
                m_neutral, _( "%1$s tries to climb the %2$s but slips." ), name, here.tername( p ) );
            moves -= 400;
        } else {
            add_msg_if_npc( m_neutral, _( "%1$s climbs over the %2$s." ), name, here.tername( p ) );
            moves -= ( 500 - ( rng( 0, climb ) * 20 ) );
            moved = true;
        }
    } else if( !no_bashing && smash_ability() > 0 && here.is_bashable( p )
               && here.bash_rating( smash_ability(), p ) > 0 ) {
        moves -= !is_armed() ? 80 : primary_weapon().attack_cost() * 0.8;
        here.bash( p, smash_ability() );
    } else {
        if( attitude == NPCATT_MUG || attitude == NPCATT_KILL
            || attitude == NPCATT_WAIT_FOR_LEAVE ) {
            set_attitude( NPCATT_FLEE_TEMP );
        }

        moves = 0;
    }

    if( moved ) {
        const auto old_pos = bub_pos();
        setpos( p );
        set_underwater( g->m.is_divable( p ) );
        if( old_pos.x() - p.x() < 0 ) {
            facing = FD_RIGHT;
        } else {
            facing = FD_LEFT;
        }
        if( is_mounted() ) {
            if( mounted_creature->bub_pos() != bub_pos() ) {
                mounted_creature->setpos( bub_pos() );
                mounted_creature->facing = facing;
                mounted_creature->process_triggers();
                here.creature_in_field( *mounted_creature );
                here.creature_on_trap( *mounted_creature );
            }
        }
        if( here.has_flag( "UNSTABLE", bub_pos() ) ) {
            add_effect( effect_bouldering, 1_turns, bodypart_str_id::NULL_ID() );
        } else if( has_effect( effect_bouldering ) ) {
            remove_effect( effect_bouldering );
        }

        if( here.has_flag_ter_or_furn( TFLAG_NO_SIGHT, bub_pos() ) ) {
            add_effect( effect_no_sight, 1_turns, bodypart_str_id::NULL_ID() );
        } else if( has_effect( effect_no_sight ) ) {
            remove_effect( effect_no_sight );
        }

        if( in_vehicle ) { here.unboard_vehicle( old_pos ); }

        // Close doors behind self (if you can)
        if( ( rules.has_flag( ally_rule::close_doors ) && is_player_ally() ) && !is_hallucination() ) {
            doors::close_door( here, *this, old_pos );
        }

        if( here.veh_at( p ).part_with_feature( VPFLAG_BOARDABLE, true ) ) {
            here.board_vehicle( p, this );
        }

        here.creature_on_trap( *this );
        here.creature_in_field( *this );
    }
}

void npc::move_to_next()
{
    while( !path.empty() && bub_pos() == path[0] ) { path.erase( path.begin() ); }

    if( path.empty() ) {
        add_msg( m_debug, "npc::move_to_next() called with an empty path or path "
                          "containing only current position" );
        move_pause();
        return;
    }

    move_to( path[0] );
    if( !path.empty() && bub_pos() == path[0] ) { // Move was successful
        path.erase( path.begin() );
    }
}

void npc::avoid_friendly_fire()
{
    // TODO: To parameter
    const tripoint_bub_ms& tar = current_target()->bub_pos();
    // Calculate center of weight of friends and move away from that
    tripoint_bub_ms center;
    for( const auto& fr : ai_cache.friends ) {
        if( shared_ptr_fast<Creature> fr_p = fr.lock() ) { center += fr_p->bub_pos().raw(); }
    }

    float friend_count = ai_cache.friends.size();
    center.x() = std::round( center.x() / friend_count );
    center.y() = std::round( center.y() / friend_count );
    center.z() = std::round( center.z() / friend_count );

    std::vector<tripoint_bub_ms> candidates = closest_points_first( bub_pos(), 1 );
    candidates.erase( candidates.begin() );
    std::sort(
        candidates.begin(), candidates.end(),
    [&tar, &center]( const tripoint_bub_ms & l, const tripoint_bub_ms & r ) {
        return ( rl_dist( l, tar ) - rl_dist( l, center ) ) < ( rl_dist( r, tar ) - rl_dist( r, center ) );
    } );

    for( const auto& pt : candidates ) {
        if( can_move_to( pt ) ) {
            move_to( pt );
            return;
        }
    }

    /* If we're still in the function at this point, maneuvering can't help us. So,
     * might as well address some needs.
     * We pass a <danger> value of NPC_DANGER_VERY_LOW + 1 so that we won't start
     * eating food (or, god help us, sleeping).
     */
    npc_action action = address_needs( NPC_DANGER_VERY_LOW + 1 );
    if( action == npc_undecided ) { move_pause(); }
    execute_action( resolve_cmd( action ) );
}

void npc::escape_explosion()
{
    if( ai_cache.dangerous_explosives.empty() ) { return; }

    warn_about( "explosion", 1_minutes );

    move_away_from( ai_cache.dangerous_explosives, true );
}

void npc::move_away_from(
    const tripoint_bub_ms& pt, bool no_bash_atk, std::set<tripoint_bub_ms> *nomove )
{
    auto best_pos = bub_pos();
    int best = -1;
    int chance = 2;
    map& here = get_map();
    for( const tripoint_bub_ms& p : here.points_in_radius( bub_pos(), 1 ) ) {
        if( nomove != nullptr && nomove->contains( p ) ) { continue; }

        if( p == bub_pos() ) { continue; }

        if( p == get_player_character().bub_pos() ) { continue; }

        const int cost = here.combined_movecost( bub_pos(), p );
        if( cost <= 0 ) { continue; }

        const int dst =
            std::abs( p.x() - pt.x() ) + std::abs( p.y() - pt.y() ) + std::abs( p.z() - pt.z() );
        const int val = dst * 1000 / cost;
        if( val > best && can_move_to( p, no_bash_atk ) ) {
            best_pos = p;
            best = val;
            chance = 2;
        } else if( ( val == best && one_in( chance ) ) && can_move_to( p, no_bash_atk ) ) {
            best_pos = p;
            best = val;
            chance++;
        }
    }

    move_to( best_pos, no_bash_atk, nomove );
}

void npc::move_pause()

{
    // make sure we're using the best weapon
    if( has_new_items ) { scan_new_items(); }
    if( calendar::once_every( 1_hours ) ) {
        deactivate_bionic_by_id( bio_soporific );
        for( const bionic_id& bio_id : health_cbms ) { activate_bionic_by_id( bio_id ); }
    }
    // NPCs currently always aim when using a gun, even with no target
    // This simulates them aiming at stuff just at the edge of their range
    if( !primary_weapon().is_gun() && cbm_active.is_null() ) {
        character_funcs::do_pause( *this );
        return;
    }

    // Stop, drop, and roll
    if( has_effect( effect_onfire ) ) {
        character_funcs::do_pause( *this );
    } else {
        aim();
        moves = std::min( moves, 0 );
    }
}

std::optional<tripoint_bub_ms> nearest_passable(
    const tripoint_bub_ms& p, const tripoint_bub_ms& closest_to )
{
    map& here = get_map();
    if( here.passable( p ) ) { return p; }

    // We need to path to adjacent tile, not the exact one
    // Let's pick the closest one to us that is passable
    std::vector<tripoint_bub_ms> candidates = closest_points_first( p, 1 );
    std::sort( candidates.begin(), candidates.end(),
    [closest_to]( const tripoint_bub_ms & l, const tripoint_bub_ms & r ) {
        return rl_dist( closest_to, l ) < rl_dist( closest_to, r );
    } );
    auto iter =
    std::find_if( candidates.begin(), candidates.end(), [&here, &p]( const tripoint_bub_ms & pt ) {
        return here.passable( pt ) && !here.obstructed_by_vehicle_rotation( p, pt );
    } );
    if( iter != candidates.end() ) { return *iter; }

    return std::nullopt;
}

void npc::move_away_from( const std::vector<sphere> &spheres, bool no_bashing )
{
    if( spheres.empty() ) { return; }

    auto minp = bub_pos();
    auto maxp = bub_pos();

    for( const auto& elem : spheres ) {
        minp.x() = std::min( minp.x(), elem.center.x - elem.radius );
        minp.y() = std::min( minp.y(), elem.center.y - elem.radius );
        maxp.x() = std::max( maxp.x(), elem.center.x + elem.radius );
        maxp.y() = std::max( maxp.y(), elem.center.y + elem.radius );
    }

    const tripoint_range<tripoint_bub_ms> range( minp, maxp );

    std::vector<tripoint_bub_ms> escape_points;

    map& here = get_map();
    std::copy_if( range.begin(), range.end(), std::back_inserter( escape_points ),
    [&here]( const tripoint_bub_ms & elem ) { return here.passable( elem ); } );

    cata::sort_by_rating(
    escape_points.begin(), escape_points.end(), [&]( const tripoint_bub_ms & elem ) {
        const int danger = std::
        accumulate( spheres.begin(), spheres.end(), 0, [&]( const int sum, const sphere & s ) {
            return sum + std::max( s.radius - rl_dist( elem.raw(), s.center ), 0 );
        } );

        const int distance = rl_dist( bub_pos(), elem );
        const int move_cost = here.move_cost( elem );

        return std::make_tuple( danger, distance, move_cost );
    } );

    for( const auto& elem : escape_points ) {
        update_path( elem, no_bashing );

        if( elem == bub_pos() || !path.empty() ) { break; }
    }

    if( !path.empty() ) {
        move_to_next();
    } else {
        move_pause();
    }
}

void npc::see_item_say_smth( const itype_id& object, const std::string& smth )
{
    map& here = get_map();
    for( const tripoint_bub_ms& p : closest_points_first( bub_pos(), 6 ) ) {
        if( here.sees_some_items( p, *this ) && sees( p ) ) {
            for( const item * const& it : here.i_at( p ) ) {
                if( one_in( 100 ) && ( it->typeId() == object ) ) { say( smth ); }
            }
        }
    }
}

bool npc::find_corpse_to_pulp()
{
    Character& player_character = get_player_character();
    if( ( is_player_ally()
          && ( !rules.has_flag( ally_rule::allow_pulp ) || player_character.in_vehicle ) )
        || is_hallucination() ) {
        return false;
    }

    map& here = get_map();
    // Pathing with overdraw can get expensive, limit it
    int path_counter = 4;
    const auto check_tile = [this, &path_counter, &here]( const tripoint_bub_ms & p ) -> const item* {
        if( !here.sees_some_items( p, *this ) || !sees( p ) ) { return nullptr; }

        const map_stack items = here.i_at( p );
        const item *found = nullptr;
        for( const item * const& it : items )
        {
            // Pulp only stuff that revives, but don't pulp acid stuff
            // That is, if you aren't protected from this stuff!
            if( it->can_revive() ) {
                // If the first encountered corpse bleeds something dangerous then
                // it is not safe to bash.
                if( is_dangerous_field( field_entry( it->get_mtype()->bloodType(), 1, 0_turns ) ) ) {
                    return nullptr;
                }

                found = it;
                break;
            }
        }

        if( found != nullptr )
        {
            path_counter--;
            // Only return corpses we can path to
            return update_path( p, false, false ) ? found : nullptr;
        }

        return nullptr;
    };

    const int range = 6;

    const item* corpse = nullptr;
    if( pulp_location && square_dist( bub_pos(), *pulp_location ) <= range ) {
        corpse = check_tile( *pulp_location );
    }

    // Find the old target to avoid spamming
    const item* old_target = corpse;

    if( corpse == nullptr ) {
        // If we're following the player, don't wander off to pulp corpses
        const auto &around = is_walking_with() ? player_character.bub_pos() : bub_pos();
        for( item * &location : here.get_active_items_in_radius( around, range,
                special_item_type::corpse ) ) {
            corpse = check_tile( location->position() );

            if( corpse != nullptr ) {
                pulp_location.emplace( location->position() );
                break;
            }

            if( path_counter <= 0 ) { break; }
        }
    }

    if( corpse != nullptr && corpse != old_target && is_walking_with() ) {
        say( _( "Hold on, I want to pulp that %s." ), corpse->tname() );
    }

    return corpse != nullptr;
}

bool npc::do_pulp()
{
    if( !pulp_location ) { return false; }

    if( rl_dist( *pulp_location, bub_pos() ) > 1 || pulp_location->z() != bub_pos().z() ) {
        return false;
    }
    // TODO: Don't recreate the activity every time
    int old_moves = moves;
    assign_activity( std::make_unique<player_activity>(
                         std::make_unique<pulp_activity_actor>( get_map().bub_to_abs( *pulp_location ) ) ) );
    activity->moves_left = calendar::INDEFINITELY_LONG;
    activity->do_turn( *this );
    return moves != old_moves;
}

bool npc::do_player_activity()
{
    int old_moves = moves;
    if( moves > 200 && activity
        && ( activity->is_multi_type() || activity->id() == activity_id( "ACT_TIDY_UP" ) ) ) {
        // a huge backlog of a multi-activity type can forever loop
        // instead; just scan the map ONCE for a task to do, and if it returns false
        // then stop scanning, abandon the activity, and kill the backlog of moves.
        if( !generic_multi_activity_handler( *activity, *this->as_player(), true ) ) {
            revert_after_activity();
            set_moves( 0 );
            return true;
        }
    }
    // the multi-activity types can sometimes cancel the activity, and return without using up any
    // moves. ( when they are setting a destination etc. ) normally this isn't a problem, but in the
    // main game loop, if the NPC has a huge backlog of moves; then each of these occurrences will
    // nudge the infinite loop counter up by one. ( even if other move-using things occur inbetween
    // ) so here - if no moves are used in a multi-type activity do_turn(), then subtract a nominal
    // amount to satisfy the infinite loop counter.
    const bool multi_type = activity ? activity->is_multi_type() : false;
    const int moves_before = moves;
    while( moves > 0 && activity && *activity ) {
        activity->do_turn( *this );
        if( !is_active() ) { return true; }
    }
    if( multi_type && moves == moves_before ) { moves -= 1; }
    /* if the activity is finished, grab any backlog or change the mission */
    if( !has_destination() && ( !activity || !*activity ) ) {
        if( !backlog.empty() ) {
            activity = std::move( backlog.front() );
            backlog.pop_front();
            current_activity_id = activity->id();
        } else {
            const std::string failure_msg = consume_activity_failure_message();
            const bool suppressed = consume_suppress_activity_complete_message();
            if( !failure_msg.empty() ) {
                add_msg( m_info, failure_msg );
            } else if( is_player_ally() && !suppressed ) {
                add_msg( m_info, string_format( "%s completed the assigned task.", disp_name() ) );
            }
            current_activity_id = activity_id::NULL_ID();
            revert_after_activity();
            // if we loaded after being out of the bubble for a while, we might have more
            // moves than we need, so clear them
            set_moves( 0 );
        }
    }
    return moves != old_moves;
}

void npc::heal_player( Character& patient )
{
    int dist = rl_dist( bub_pos(), patient.bub_pos() );

    if( dist > 1 ) {
        // We need to move to the player
        update_path( patient.bub_pos() );
        move_to_next();
        return;
    }

    Character& player_character = get_player_character();
    // Close enough to heal!
    bool u_see = player_character.sees( *this ) || player_character.sees( patient );
    if( u_see ) { add_msg( _( "%1$s heals %2$s." ), disp_name(), patient.disp_name() ); }

    item& used = get_healing_item( ai_cache.can_heal );
    if( used.is_null() ) {
        debugmsg( "%s tried to heal you but has no healing item", disp_name() );
        return;
    }
    if( !is_hallucination() ) {
        int charges_used = used.type->invoke( *this, used, patient.bub_pos(), "heal" );
        consume_charges( used, charges_used );
    } else {
        pretend_heal( patient, used );
    }
}

void npc::pretend_heal( Character& patient, item& used )
{
    if( get_player_character().sees( *this ) ) {
        add_msg( _( "%1$s heals %2$s." ), disp_name(),
                 patient.disp_name() ); // you can tell that it's not real by looking at your HP
        // though
    }
    consume_charges( used, 1 ); // empty hallucination's inventory to avoid spammming
    moves -= 100;             // consumes moves to avoid infinite loop
}

void npc::heal_self()
{
    if( has_effect( effect_asthma ) ) {
        item* treatment = &null_item_reference();
        std::string iusage = "OXYGEN_BOTTLE";
        if( has_charges( itype_inhaler, 1 ) ) {
            treatment = &inv.find_item( inv.position_by_type( itype_inhaler ) );
            iusage = "INHALER";
        } else if( has_charges( itype_oxygen_tank, 1 ) ) {
            treatment = &inv.find_item( inv.position_by_type( itype_oxygen_tank ) );
        } else if( has_charges( itype_smoxygen_tank, 1 ) ) {
            treatment = &inv.find_item( inv.position_by_type( itype_smoxygen_tank ) );
        }
        if( !treatment->is_null() ) {
            treatment->type->invoke( *this, *treatment, bub_pos(), iusage );
            consume_charges( *treatment, 1 );
            return;
        }
    }

    item& used = get_healing_item( ai_cache.can_heal );
    if( used.is_null() ) {
        debugmsg( "%s tried to heal self but has no healing item", disp_name() );
        return;
    }

    if( get_player_character().sees( *this ) ) {
        add_msg( _( "%s applies a %s" ), disp_name(), used.tname() );
    }
    warn_about( "heal_self", 1_turns );

    int charges_used = used.type->invoke( *this, used, bub_pos(), "heal" );
    if( used.is_medication() ) { consume_charges( used, charges_used ); }
}

void npc::use_painkiller()
{
    // First, find the best painkiller for our pain level
    item* it = inv.most_appropriate_painkiller( get_pain() );

    if( it->is_null() ) {
        debugmsg( "NPC tried to use painkillers, but has none!" );
        move_pause();
    } else {
        if( get_player_character().sees( *this ) ) {
            add_msg( _( "%1$s takes some %2$s." ), disp_name(), it->tname() );
        }
        consume( *it );
        moves = 0;
    }
}

// We want our food to:
// Provide enough nutrition and quench
// Not provide too much of either (don't waste food)
// Not be unhealthy
// Not have side effects
// Be eaten before it rots (favor soon-to-rot perishables)
static float rate_food( const item& it, int want_nutr, int want_quench )
{
    const auto& food = it.get_comestible();
    if( !food ) { return 0.0f; }

    if( food->parasites && !it.has_flag( flag_NO_PARASITES ) ) { return 0.0; }

    int nutr = food->get_default_nutr();
    int quench = food->quench;

    if( nutr <= 0 && quench <= 0 ) {
        // Not food - may be salt, drugs etc.
        return 0.0f;
    }

    if( !it.type->use_methods.empty() ) {
        // TODO: Get a good method of telling apart:
        // raw meat (parasites - don't eat unless mutant)
        // zed meat (poison - don't eat unless mutant)
        // alcohol (debuffs, health drop - supplement diet but don't bulk-consume)
        // caffeine (fine to consume, but expensive and prevents sleep)
        // hallucination mushrooms (NPCs don't hallucinate, so don't eat those)
        // honeycomb (harmless iuse)
        // royal jelly (way too expensive to eat as food)
        // mutagenic crap (don't eat, we want player to micromanage muties)
        // marloss (NPCs don't turn fungal)
        // weed brownies (small debuff)
        // seeds (too expensive)

        // For now skip all of those
        return 0.0f;
    }

    double relative_rot = it.get_relative_rot();
    if( relative_rot >= 1.0f ) {
        // TODO: Allow sapro mutants to eat it anyway and make them prefer it
        return 0.0f;
    }

    float weight = std::max( 1.0, 10.0 * relative_rot );
    if( it.get_comestible_fun() < 0 ) {
        // This helps to avoid eating stuff like flour
        weight /= ( -it.get_comestible_fun() ) + 1;
    }

    if( food->healthy < 0 ) { weight /= ( -food->healthy ) + 1; }

    // Avoid wasting quench values unless it's about to rot away
    if( relative_rot < 0.9f && quench > want_quench ) {
        weight -= ( 1.0f - relative_rot ) * ( quench - want_quench );
    }

    if( quench < 0 && want_quench > 0 && want_nutr < want_quench ) {
        // Avoid stuff that makes us thirsty when we're more thirsty than hungry
        weight = weight * want_nutr / want_quench;
    }

    if( nutr > want_nutr ) {
        // TODO: Allow overeating in some cases
        if( nutr >= 5 ) { return 0.0f; }

        if( relative_rot < 0.9f ) { weight /= nutr - want_nutr; }
    }

    if( it.poison > 0 ) { weight -= it.poison; }

    return weight;
}

bool npc::consume_food()
{
    float best_weight = 0.0f;
    int index = -1;
    int want_hunger = std::max<int>( 0, ( max_stored_kcal() - get_stored_kcal() ) / 10 );
    int want_quench = std::max( 0, get_thirst() );
    const_invslice slice = inv.const_slice();
    for( size_t i = 0; i < slice.size(); i++ ) {
        const item& it = *slice[i]->front();
        if( const item * food_item = it.get_food() ) {
            float cur_weight = rate_food( *food_item, want_hunger, want_quench );
            // Note: will_eat is expensive, avoid calling it if possible
            if( cur_weight > best_weight && will_eat( *food_item ).success() ) {
                best_weight = cur_weight;
                index = i;
            }
        }
    }

    if( index == -1 ) {
        if( !is_player_ally() ) {
            // TODO: Remove this and let player "exploit" hungry NPCs
            set_stored_kcal( max_stored_kcal() );
            set_thirst( 0 );
        }
        return false;
    }

    // consume doesn't return a meaningful answer, we need to compare moves
    // TODO: Make player::consume return false if it fails to consume
    int old_moves = moves;
    consume( i_at( index ) );
    // TODO: a more reliable check for whether item has been consumed
    bool consumed = old_moves != moves;
    if( !consumed ) { debugmsg( "%s failed to consume %s", name, i_at( index ).tname() ); }

    return consumed;
}

void npc::mug_player( Character& mark )
{
    if( mark.is_armed() ) { make_angry(); }

    if( rl_dist( bub_pos(), mark.bub_pos() ) > 1 ) { // We have to travel
        update_path( mark.bub_pos() );
        move_to_next();
        return;
    }

    Character& player_character = get_player_character();
    const bool u_see = player_character.sees( *this ) || player_character.sees( mark );
    if( mark.cash > 0 ) {
        if( !is_hallucination() ) { // hallucinations can't take items
            cash += mark.cash;
            mark.cash = 0;
        }
        moves = 0;
        // Describe the action
        if( mark.is_npc() ) {
            if( u_see ) { add_msg( _( "%1$s takes %2$s's money!" ), name, mark.name ); }
        } else {
            add_msg( m_bad, _( "%s takes your money!" ), name );
        }
        return;
    }

    // We already have their money; take some goodies!
    // value_mod affects at what point we "take the money and run"
    // A lower value means we'll take more stuff
    double value_mod =
        1 - ( ( 10 - personality.bravery ) * .05 ) - ( ( 10 - personality.aggression ) * .04 )
        - ( ( 10 - personality.collector ) * .06 );
    if( !mark.is_npc() ) {
        value_mod += ( op_of_u.fear * .08 );
        value_mod -= ( ( 8 - op_of_u.value ) * .07 );
    }
    double best_value = minimum_item_value() * value_mod;
    item* to_steal = nullptr;
    const_invslice slice = mark.inv_const_slice();
    for( const std::vector<item * > *stack : slice ) {
        item& front_stack = *stack->front();
        if( value( front_stack ) >= best_value && can_pick_volume( front_stack )
            && can_pick_weight( front_stack, true ) ) {
            best_value = value( front_stack );
            to_steal = &front_stack;
        }
    }
    if( to_steal == nullptr ) { // Didn't find anything worthwhile!
        set_attitude( NPCATT_FLEE_TEMP );
        if( !one_in( 3 ) ) { say( "<done_mugging>" ); }
        moves -= 100;
        return;
    }
    if( !is_hallucination() ) {
        i_add( to_steal->detach() );
        if( mark.is_npc() ) {
            if( u_see ) {
                add_msg( _( "%1$s takes %2$s's %3$s." ), name, mark.name, to_steal->tname() );
            }
        } else {
            add_msg( m_bad, _( "%1$s takes your %2$s." ), name, to_steal->tname() );
        }
    }
    moves -= 100;
    if( !mark.is_npc() ) {
        op_of_u.value -= rng( 0, 1 ); // Decrease the value of the player
    }
}

void npc::look_for_player( const Character& sought )
{
    complain_about( "look_for_player", 5_minutes, "<wait>", false );
    update_path( sought.bub_pos() );
    move_to_next();
    return;
    // The part below is not implemented properly
    /*
    if( sees( sought ) ) {
        move_pause();
        return;
    }

    if (!path.empty()) {
        const tripoint_bub_ms &dest = path[path.size() - 1];
        if( !sees( dest ) ) {
            move_to_next();
            return;
        }
        path.clear();
    }
    std::vector<point> possibilities;
    for (int x = 1; x < g_mapsize_x; x += 11) { // 1, 12, 23, 34
        for (int y = 1; y < g_mapsize_y; y += 11) {
            if( sees( x, y ) ) {
                possibilities.push_back(point(x, y));
            }
        }
    }
    if (possibilities.empty()) { // We see all the spots we'd like to check!
        say("<wait>");
        move_pause();
    } else {
        if (one_in(6)) {
            say("<wait>");
        }
        update_path( tripoint( random_entry( possibilities ), bub_pos().z() ) );
        move_to_next();
    }
    */
}

bool npc::saw_player_recently() const
{
    return last_player_seen_pos && get_map().inbounds( *last_player_seen_pos ) &&
    last_seen_player_turn > 0;
}

bool npc::has_omt_destination() const { return goal != no_goal_point; }

void npc::reach_omt_destination()
{
    if( !omt_path.empty() ) { omt_path.clear(); }
    map& here = get_map();
    if( is_travelling() ) {
        guard_pos = abs_pos();
        goal = no_goal_point;
        if( is_player_ally() ) {
            Character& player_character = get_player_character();
            talk_function::assign_guard( *this );
            if( rl_dist( player_character.bub_pos(), bub_pos() ) > SEEX * 2
                || !player_character.sees( bub_pos() ) ) {
                if( ( player_character.has_item_with_flag( flag_TWO_WAY_RADIO, true )
                      || player_character.has_bionic( bio_infolink ) )
                    && ( has_item_with_flag( flag_TWO_WAY_RADIO, true ) || has_bionic( bio_infolink ) ) ) {
                    add_msg( m_info,
                             _( "From your two-way radio you hear %s reporting in, "
                                "'I've arrived, boss!'" ),
                             disp_name() );
                }
            }
        } else {
            // for now - they just travel to a nearby place they want as a base
            // and chill there indefinitely, the plan is to add systems for them to build
            // up their base, then go out on looting missions,
            // then return to base afterwards.
            set_mission( NPC_MISSION_GUARD );
            if( !needs.empty() && needs[0] == need_safety ) {
                // we found our base.
                base_location = abs_omt_pos();
            }
        }
        return;
    }
    // Guarding NPCs want a specific point, not just an overmap tile
    // Rest stops having a goal after reaching it
    if( !( is_guarding() || is_patrolling() ) ) {
        goal = no_goal_point;
        return;
    }
    // If we are guarding, remember our position in case we get forcibly moved
    goal = abs_omt_pos();
    if( guard_pos == abs_pos() ) {
        // This is the specific point
        return;
    }

    if( path.size() > 1 ) {
        // No point recalculating the path to get home
        move_to_next();
    } else if( guard_pos != tripoint_abs_ms::min() ) {
        update_path( here.abs_to_bub( guard_pos ) );
        move_to_next();
    } else {
        guard_pos = abs_pos();
    }
}

void npc::set_omt_destination()
{
    /* TODO: Make NPCs' movement more intelligent.
     * Right now this function just makes them attempt to address their needs:
     *  if we need ammo, go to a gun store, if we need food, go to a grocery store,
     *  and if we don't have any needs, pick a random spot.
     * What it SHOULD do is that, if there's time; but also look at our mission and
     *  our faction to determine more meaningful actions, such as attacking a rival
     *  faction's base, or meeting up with someone friendly.  NPCs should also
     *  attempt to reach safety before nightfall, and possibly similar goals.
     * Also, NPCs should be able to assign themselves missions like "break into that
     *  lab" or "map that river bank."
     */
    if( is_stationary( true ) ) {
        guard_current_pos();
        return;
    }

    // all of the following luxuries are at ground level.
    // so please wallow in hunger & fear if below ground.
    if( bub_pos().z() != 0 && !get_map().has_zlevels() ) {
        goal = no_goal_point;
        return;
    }

    tripoint_abs_omt surface_omt_loc = abs_omt_pos();
    // We need that, otherwise find_closest won't work properly
    surface_omt_loc.z() = 0;

    // also, don't bother looking if the CITY_SIZE is 0, just go somewhere at random
    const int city_size = get_option<int>( "CITY_SIZE" );
    if( city_size == 0 ) {
        goal = surface_omt_loc + point( rng( -90, 90 ), rng( -90, 90 ) );
        return;
    }

    decide_needs();
    if( needs.empty() ) { // We don't need anything in particular.
        needs.push_back( need_none );
    }

    std::string dest_type;
    for( const auto& fulfill : needs ) {
        auto cache_iter = goal_cache.find( fulfill );
        if( cache_iter != goal_cache.end() && cache_iter->second.omt_loc == surface_omt_loc ) {
            goal = cache_iter->second.goal;
        } else {
            // look for the closest occurrence of any of that locations terrain types
            omt_find_params find_params;
            for( const oter_type_id& elem : get_location_for( fulfill )->get_all_terrains() ) {
                find_params.types.emplace_back( elem.id().str(), ot_match_type::type );
            }
            // note: no shuffle of `find_params.types` is needed, because `find_closest`
            // disregards `types` order anyway, and already returns random result among
            // those having equal minimal distance
            find_params.search_range = {0, 75};
            find_params.search_layers = omt_find_all_layers;

            auto& dim_ob = get_overmapbuffer( get_dimension() );
            goal = dim_ob.find_closest( surface_omt_loc, find_params );
            npc_need_goal_cache& cache = goal_cache[fulfill];
            cache.goal = goal;
            cache.omt_loc = surface_omt_loc;
        }
        omt_path.clear();
        if( goal != overmap::invalid_tripoint ) {
            omt_path =
                get_overmapbuffer( get_dimension() )
                .get_travel_path( surface_omt_loc, goal, overmap_path_params::for_npc() );
        }
        if( !omt_path.empty() ) { break; }
    }

    // couldn't find any places to go, so go somewhere.
    if( goal == overmap::invalid_tripoint || omt_path.empty() ) {
        auto& dim_ob = get_overmapbuffer( get_dimension() );
        goal = surface_omt_loc + point( rng( -90, 90 ), rng( -90, 90 ) );
        omt_path = dim_ob.get_travel_path( surface_omt_loc, goal, overmap_path_params::for_npc() );
        // try one more time
        if( omt_path.empty() ) {
            goal = surface_omt_loc + point( rng( -90, 90 ), rng( -90, 90 ) );
            omt_path =
                dim_ob.get_travel_path( surface_omt_loc, goal, overmap_path_params::for_npc() );
        }
        if( omt_path.empty() ) { goal = no_goal_point; }
        return;
    }

    DebugLog( DL::Info, DC::Main ) << "npc::set_omt_destination - new goal for NPC [" << get_name()
                                   << "] with [" << get_need_str_id( needs.front() ) << "] is ["
                                   << dest_type << "] in " << goal.to_string() << ".";
}

void npc::go_to_omt_destination()
{
    map& here = get_map();
    if( ai_cache.guard_pos ) {
        if( abs_pos() == *ai_cache.guard_pos ) {
            path.clear();
            ai_cache.guard_pos = std::nullopt;
            move_pause();
            return;
        }
    }
    if( goal == no_goal_point || omt_path.empty() ) {
        add_msg( m_debug, "npc::go_to_destination with no goal" );
        move_pause();
        reach_omt_destination();
        return;
    }
    const tripoint_abs_omt omt_pos = abs_omt_pos();
    if( goal == omt_pos ) {
        // We're at our desired map square!  Pause to keep the NPC infinite loop counter happy
        move_pause();
        reach_omt_destination();
        return;
    }
    if( !path.empty() ) {
        // we already have a path, just use that until we can't.
        move_to_next();
        return;
    }
    // get the next path point
    if( omt_path.back() == omt_pos ) {
        // this should be the square we are at.
        omt_path.pop_back();
    }
    if( !omt_path.empty() ) {
        point_rel_omt omt_diff = omt_path.back().xy() - omt_pos.xy();
        if( omt_diff.x() > 3 || omt_diff.x() < -3 || omt_diff.y() > 3 || omt_diff.y() < -3 ) {
            // we've gone wandering somehow, reset destination.
            if( !is_player_ally() ) {
                set_omt_destination();
            } else {
                talk_function::assign_guard( *this );
            }
            return;
        }
    }
    // TODO: fix point types
    auto sm_tri = here.abs_to_bub( project_to<coords::ms>( omt_path.back() ) );
    auto centre_sub = sm_tri + point( SEEX, SEEY );
    if( !here.passable( centre_sub ) ) {
        auto candidates = here.points_in_radius( centre_sub, 2 );
        for( const auto& elem : candidates ) {
            if( here.passable( elem ) ) {
                centre_sub = elem;
                break;
            }
        }
    }
    path = here.route(
               bub_pos(), centre_sub, get_legacy_pathfinding_settings(), get_legacy_path_avoid() );
    add_msg( m_debug, "%s going %s->%s", name, omt_pos.to_string(), goal.to_string() );

    if( !path.empty() ) {
        move_to_next();
        return;
    }
    move_pause();
}

void npc::guard_current_pos()
{
    goal = abs_omt_pos();
    guard_pos = get_map().bub_to_abs( bub_pos() );
}

std::string npc_action_name( npc_action action )
{
    switch( action ) {
        case npc_undecided:
            return "Undecided";
        case npc_pause:
            return "Pause";
        case npc_reload:
            return "Reload";
        case npc_investigate_sound:
            return "Investigate sound";
        case npc_return_to_guard_pos:
            return "Returning to guard position";
        case npc_sleep:
            return "Sleep";
        case npc_pickup:
            return "Pick up items";
        case npc_heal:
            return "Heal self";
        case npc_use_painkiller:
            return "Use painkillers";
        case npc_drop_items:
            return "Drop items";
        case npc_flee:
            return "Flee";
        case npc_melee:
            return "Melee";
        case npc_reach_attack:
            return "Reach attack";
        case npc_aim:
            return "Aim";
        case npc_shoot:
            return "Shoot";
        case npc_look_for_player:
            return "Look for player";
        case npc_heal_player:
            return "Heal player or ally";
        case npc_follow_player:
            return "Follow player";
        case npc_follow_embarked:
            return "Follow player (embarked)";
        case npc_talk_to_player:
            return "Talk to player";
        case npc_mug_player:
            return "Mug player";
        case npc_goto_to_this_pos:
            return "Go to position";
        case npc_goto_destination:
            return "Go to destination";
        case npc_avoid_friendly_fire:
            return "Avoid friendly fire";
        case npc_escape_explosion:
            return "Escape explosion";
        case npc_player_activity:
            return "Performing activity";
        case npc_move_to_next:
            return "Follow path (move to next)";
        default:
            return "Unnamed action";
    }
}

void print_action( const char* prepend, npc_action action )
{
    if( action != npc_undecided ) { add_msg( m_debug, prepend, npc_action_name( action ) ); }
}

const Creature *npc::current_target() const
{
    // TODO: Arguably we should return a shared_ptr to ensure that the returned
    // object stays alive while the caller uses it.  Not doing that for now.
    return ai_cache.target.lock().get();
}

Creature *npc::current_target()
{
    // TODO: As above.
    return ai_cache.target.lock().get();
}

const Creature *npc::current_ally() const
{
    // TODO: Arguably we should return a shared_ptr to ensure that the returned
    // object stays alive while the caller uses it.  Not doing that for now.
    return ai_cache.ally.lock().get();
}

Creature *npc::current_ally()
{
    // TODO: As above.
    return ai_cache.ally.lock().get();
}

// Maybe TODO: Move to Character method and use map methods
static bodypart_str_id bp_affected( npc& who, const efftype_id& effect_type )
{
    bodypart_str_id ret = bodypart_str_id::NULL_ID();
    int highest_intensity = INT_MIN;
    for( const body_part bp : all_body_parts ) {
        const auto& eff = who.get_effect( effect_type, convert_bp( bp ) );
        if( !eff.is_null() && eff.get_intensity() > highest_intensity ) {
            ret = convert_bp( bp );
            highest_intensity = eff.get_intensity();
        }
    }

    return ret;
}

static std::string distance_string( int range )
{
    if( range < 6 ) {
        return "<danger_close_distance>";
    } else if( range < 11 ) {
        return "<close_distance>";
    } else if( range < 26 ) {
        return "<medium_distance>";
    } else {
        return "<far_distance>";
    }
}

void npc::warn_about(
    const std::string& type, const time_duration& d, const std::string& name, int range,
    const tripoint_bub_ms& danger_pos )
{
    std::string snip;
    sounds::sound_t spriority = sounds::sound_t::alert;
    if( type == "monster" ) {
        snip = is_enemy() ? "<monster_warning_h>" : "<monster_warning>";
    } else if( type == "explosion" ) {
        snip = is_enemy() ? "<fire_in_the_hole_h>" : "<fire_in_the_hole>";
    } else if( type == "general_danger" ) {
        snip = is_enemy() ? "<general_danger_h>" : "<general_danger>";
        spriority = sounds::sound_t::speech;
    } else if( type == "relax" ) {
        snip = is_enemy() ? "<its_safe_h>" : "<its_safe>";
        spriority = sounds::sound_t::speech;
    } else if( type == "kill_npc" ) {
        snip = is_enemy() ? "<kill_npc_h>" : "<kill_npc>";
    } else if( type == "kill_player" ) {
        snip = is_enemy() ? "<kill_player_h>" : "";
    } else if( type == "run_away" ) {
        snip = "<run_away>";
    } else if( type == "cant_flee" ) {
        snip = "<cant_flee>";
    } else if( type == "fire_bad" ) {
        snip = "<fire_bad>";
    } else if( type == "speech_noise" ) {
        snip = "<speech_warning>";
        spriority = sounds::sound_t::speech;
    } else if( type == "combat_noise" ) {
        snip = "<combat_noise_warning>";
        spriority = sounds::sound_t::speech;
    } else if( type == "movement_noise" ) {
        snip = "<movement_noise_warning>";
        spriority = sounds::sound_t::speech;
    } else if( type == "heal_self" ) {
        snip = "<heal_self>";
        spriority = sounds::sound_t::speech;
    } else {
        return;
    }
    const std::string warning_name = "warning_" + type + name;
    if( name.empty() ) {
        complain_about( warning_name, d, snip, is_enemy(), spriority );
    } else {
        const std::string range_str = range < 1 ? "<punc>" :
                                      string_format( _( " %s, %s" ),
                                          direction_name( direction_from( bub_pos(), danger_pos ) ),
                                          distance_string( range ) );
        const std::string speech = string_format( _( "%s %s%s" ), snip, name, range_str );
        complain_about( warning_name, d, speech, is_enemy(), spriority );
    }
}

bool npc::complain_about(
    const std::string& issue, const time_duration& dur, const std::string& speech, const bool force,
    const sounds::sound_t priority )
{
    // Don't have a default constructor for time_point, so accessing it in the
    // complaints map is a bit difficult, those lambdas should cover it.
    const auto complain_since = [this]( const std::string & key, const time_duration & d ) {
        const auto iter = complaints.find( key );
        return iter == complaints.end() || iter->second < calendar::turn - d;
    };
    const auto set_complain_since = [this]( const std::string & key ) {
        const auto iter = complaints.find( key );
        if( iter == complaints.end() ) {
            complaints.emplace( key, calendar::turn );
        } else {
            iter->second = calendar::turn;
        }
    };

    // Don't wake player up with non-serious complaints
    // Stop complaining while asleep
    const bool do_complain =
        force
        || ( rules.has_flag( ally_rule::allow_complain ) && !get_player_character().in_sleep_state()
             && !in_sleep_state() );

    if( complain_since( issue, dur ) && do_complain ) {
        say( speech, priority );
        set_complain_since( issue );
        return true;
    }
    return false;
}

bool npc::complain()
{
    static const std::string infected_string = "infected";
    static const std::string fatigue_string = "fatigue";
    static const std::string bite_string = "bite";
    static const std::string bleed_string = "bleed";
    static const std::string radiation_string = "radiation";
    static const std::string hunger_string = "hunger";
    static const std::string thirst_string = "thirst";

    if( !is_player_ally() || !get_player_character().sees( *this ) ) { return false; }

    // When infected, complain every (4-intensity) hours
    // At intensity 3, ignore player wanting us to shut up
    if( has_effect( effect_infected ) ) {
        bodypart_str_id bp = bp_affected( *this, effect_infected );
        const auto& eff = get_effect( effect_infected, bp );
        int intensity = eff.get_intensity();
        const std::string speech = string_format( _( "My %s wound is infected…" ), body_part_name( bp ) );
        if( complain_about( infected_string, time_duration::from_hours( 4 - intensity ), speech,
                            intensity >= 3 ) ) {
            // Only one complaint per turn
            return true;
        }
    }

    // When bitten, complain every hour, but respect restrictions
    if( has_effect( effect_bite ) ) {
        bodypart_str_id bp = bp_affected( *this, effect_bite );
        const std::string speech =
            string_format( _( "The bite wound on my %s looks bad." ), body_part_name( bp ) );
        if( complain_about( bite_string, 1_hours, speech ) ) { return true; }
    }

    // When tired, complain every 30 minutes
    // If massively tired, ignore restrictions
    if( get_fatigue() > fatigue_levels::tired
        && complain_about( fatigue_string, 30_minutes, _( "<yawn>" ),
                           get_fatigue() > fatigue_levels::massive - 100 ) ) {
        return true;
    }

    // Radiation every 10 minutes
    if( get_rad() > 90 ) {
        activate_bionic_by_id( bio_radscrubber );
        std::string speech = _( "I'm suffering from radiation sickness…" );
        if( complain_about( radiation_string, 10_minutes, speech, get_rad() > 150 ) ) { return true; }
    } else if( !get_rad() ) {
        deactivate_bionic_by_id( bio_radscrubber );
    }

    // Hunger every 3-6 hours
    // Since NPCs can't starve to death, respect the rules
    if( get_kcal_percent() < 0.9
        && complain_about(
            hunger_string,
            std::max( 3_hours, time_duration::from_minutes( get_kcal_percent() * 60 * 7 ) ),
            _( "<hungry>" ) ) ) {
        return true;
    }

    // Thirst every 2 hours
    // Since NPCs can't dry to death, respect the rules
    if( get_thirst() > thirst_levels::very_thirsty
        && complain_about( thirst_string, 2_hours, _( "<thirsty>" ) ) ) {
        return true;
    }

    // Bleeding every 5 minutes
    if( has_effect( effect_bleed ) ) {
        bodypart_str_id bp = bp_affected( *this, effect_bleed );
        std::string speech = string_format( _( "My %s is bleeding!" ), body_part_name( bp ) );
        if( complain_about( bleed_string, 5_minutes, speech ) ) { return true; }
    }

    return false;
}

void npc::do_reload( item& it )
{
    item_reload_option reload_opt = character_funcs::select_ammo( *this, it );

    if( !reload_opt ) {
        debugmsg( "do_reload failed: no usable ammo for %s", it.tname() );
        return;
    }

    // Note: we may be reloading the magazine inside, not the gun itself
    // Maybe TODO: allow reload functions to understand such reloads instead of const casts
    item& target = ( *reload_opt.target );
    item* usable_ammo = reload_opt.ammo;

    // If in danger, don't spend multiple turns reloading a weapon to full one by one.
    // Get enough to shoot the enemy once, then unload it on them.
    int qty =
        ai_cache.danger > 0
        ? std::max( 1, std::min( usable_ammo->charges, it.ammo_required() - it.ammo_remaining() ) )
        : std::max( 1, std::min( usable_ammo->charges, it.ammo_capacity() - it.ammo_remaining() ) );
    int reload_time = item_reload_cost( it, *usable_ammo, qty );
    // TODO: Consider printing this info to player too
    const std::string ammo_name = usable_ammo->tname();
    if( !target.reload( *this, *usable_ammo, qty ) ) {
        debugmsg( "do_reload failed: item %s could not be reloaded with %ld charge(s) of %s",
                  it.tname(), qty, ammo_name );
        return;
    }

    moves -= reload_time;
    recoil = MAX_RECOIL;

    if( get_player_character().sees( *this ) ) {
        add_msg( _( "%1$s reloads their %2$s." ), name, it.tname() );
        sfx::play_variant_sound(
            "reload", it.typeId().str(), sfx::get_heard_volume( bub_pos() ),
            sfx::get_heard_angle( bub_pos() ), sfx::get_heard_distance( bub_pos() ) );
    }

    // Otherwise the NPC may not equip the weapon until they see danger
    has_new_items = true;
    // Reloading clears mode choice.
    clear_npc_ai_info_cache( npc_ai_info::range );
}

bool npc::adjust_worn()
{
    bool any_broken = false;
    for( const bodypart_id& bp : get_all_body_parts() ) {
        if( is_limb_broken( bp ) ) {
            any_broken = true;
            break;
        }
    }

    if( !any_broken ) { return false; }
    const auto covers_broken = [this]( const item & it, side s ) {
        const body_part_set covered = it.get_covered_body_parts( s );
        for( const bodypart_str_id& bp_id : covered ) {
            if( is_limb_broken( bp_id ) && covered.test( bp_id ) ) { return true; }
        }
        return false;
    };

    item* splint = nullptr;
    for( auto& elem : worn ) {
        if( !elem->has_flag( flag_SPLINT ) ) { continue; }

        if( !covers_broken( *elem, elem->get_side() ) ) {
            const bool needs_change = covers_broken( *elem, opposite_side( elem->get_side() ) );
            // Try to change side (if it makes sense), or take off.
            if( needs_change && change_side( *elem ) ) { return true; }

            if( can_takeoff( *elem ).success() ) {
                splint = elem;
                break;
            }
        }
    }
    if( splint ) {
        takeoff( *splint );
        return true;
    }

    return false;
}

void npc::set_movement_mode( character_movemode new_mode ) { move_mode = new_mode; }
