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


float npc::evaluate_enemy( const Creature &target ) const
{
    ZoneScopedN( "evaluate_enemy" );
    if( target.is_monster() ) {
    const monster &mon = dynamic_cast<const monster &>( target );
        float diff = static_cast<float>( mon.type->difficulty );
        return std::min( diff, NPC_DANGER_MAX );
    } else if( target.is_npc() || target.is_player() ) {
    return std::min( character_danger( dynamic_cast<const player &>( target ) ),
                     NPC_DANGER_MAX );
    } else {
        return 0.0f;
    }
}

static bool too_close(
    const tripoint_bub_ms& critter_pos, const tripoint_bub_ms& ally_pos, const int def_radius )
{
    return rl_dist( critter_pos, ally_pos ) <= def_radius;
}

// Per-turn Creature::sees() cache wrapper — mirrors turn_cached_sees() in monmove.cpp.
// Key is directional; map::sees() handles symmetric LOS reuse below this layer.
static auto npc_turn_cached_sees( const Creature& seer, const Creature& target ) -> bool
{
    const auto key = std::make_pair( &seer, &target );
    {
        std::shared_lock<std::shared_mutex> lock( g->turn_sight_cache_mutex_ );
        const auto it = g->turn_sight_cache_.find( key );
        if( it != g->turn_sight_cache_.end() ) { return it->second; }
    }
    const bool result = seer.sees( target );
    {
        std::unique_lock<std::shared_mutex> lock( g->turn_sight_cache_mutex_ );
        g->turn_sight_cache_.emplace( key, result );
    }
    return result;
}

void npc::assess_danger()
{
    ZoneScoped;
    auto has_mutation_attitude_rules = false;
    for( const trait_id& mut : get_mutations() ) {
        const auto& mutation = mut.obj();
        if( !mutation.anger_relations.empty() || !mutation.ignored_by.empty() ) {
            has_mutation_attitude_rules = true;
            break;
        }
    }
    // True if this NPC has traits/effects that cause monster::attitude() to return a
    // result different from what a generic NPC would get.  When false, we can use each
    // monster's per-npcmove-pass cached attitude instead of recomputing it.
    const auto has_special_attitude_traits = guaranteed_hostile() || is_hallucination() ||
        has_mutation_attitude_rules ||
        has_trait( trait_ANIMALEMPATH ) || has_trait( trait_ANIMALEMPATH2 ) ||
        has_trait( trait_ANIMALDISCORD ) || has_trait( trait_ANIMALDISCORD2 ) ||
        has_trait( trait_PROF_FERAL ) || has_trait( trait_BEE ) ||
        has_trait( trait_FLOWERS ) || has_trait( trait_THRESH_MYCUS ) ||
        has_trait( trait_MYCUS_FRIEND ) || has_trait( trait_PHEROMONE_MAMMAL ) ||
        has_trait( trait_PHEROMONE_INSECT ) || has_trait( trait_TERRIFYING ) ||
        has_effect( effect_attention ) || has_effect( effect_feral_infighting_punishment );

    float assessment = 0.0f;
    float highest_priority = 1.0f;
    int def_radius = rules.has_flag( ally_rule::follow_close ) ? follow_distance() : 6;

    // Radius we can attack without moving
    int max_range;
    if( confident_range_cache ) {
        max_range = *confident_range_cache;
    } else {
        invalidate_range_cache();
        max_range = *confident_range_cache;
    }

    Character& player_character = get_player_character();
    // NPCs will hold back from charging if they get in trouble.
    const bool self_defense_only =
        rules.engagement == combat_engagement::NO_MOVE
        || rules.engagement == combat_engagement::NONE || emergency();
    const bool no_fighting = rules.has_flag( ally_rule::forbid_engage );
    // Companion NPCs will additionally break off to return to the player when in trouble if not
    // already ordered to.
    const bool must_retreat =
        is_walking_with() && ( emergency() || rules.has_flag( ally_rule::follow_close ) )
        && !too_close( bub_pos(), player_character.bub_pos(), follow_distance() );

    // Set this for non-companion NPCs too so all NPCs will switch to self-defense if low on stamina
    // or wounded.
    if( rules.engagement == combat_engagement::FREE_FIRE ) {
        def_radius = std::max( 6, max_range );
    } else if( self_defense_only ) {
        def_radius = max_range;
    } else if( no_fighting ) {
        def_radius = 1;
    }

    const auto ok_by_rules =
        [max_range, def_radius, this,
    &player_character]( const Creature & c, int dist, int scaled_dist ) {
        // If we're forbidden to attack, no need to check engagement rules
        if( rules.has_flag( ally_rule::forbid_engage ) ) { return false; }
        switch( rules.engagement ) {
            case combat_engagement::NONE:
                return false;
            case combat_engagement::CLOSE:
                // Either close to player or close enough that we can reach it and close to us
                return ( dist <= max_range && scaled_dist <= def_radius * 0.5 )
                       || too_close( c.bub_pos(), player_character.bub_pos(), def_radius );
            case combat_engagement::WEAK:
                return c.get_hp() <= average_damage_dealt();
            case combat_engagement::HIT:
                return c.has_effect( effect_hit_by_player );
            case combat_engagement::NO_MOVE:
                return dist <= max_range;
            case combat_engagement::FREE_FIRE:
                return dist <= max_range;
            case combat_engagement::ALL:
                return true;
        }

        return true;
    };
    std::array<float, 27> cur_threat_map{};
    // start with a decayed version of last turn's map
    for( direction threat_dir : npc_threat_dir ) {
        cur_threat_map[std::to_underlying( threat_dir )] = 0.25f * ai_cache.threat_map[std::to_underlying(
                threat_dir )];
    }
    map& here = get_map();
    // cache string_id -> int_id conversion before hot loop
    const field_type_id fd_fire = ::fd_fire;
    // first, check if we're about to be consumed by fire
    // `map::get_field` checks field_count first, so in general case (no fire) it provides an early
    // exit
    for( const tripoint_bub_ms& pt : here.points_in_radius( bub_pos(), 6 ) ) {
        if( pt == bub_pos() || !here.get_field( pt, fd_fire )
            || here.has_flag( TFLAG_FIRE_CONTAINER, pt ) ) {
            continue;
        }
        const int dist = rl_dist( bub_pos(), pt );
        cur_threat_map[std::to_underlying( direction_from( bub_pos(), pt ) )] +=
            2.0f * ( NPC_DANGER_MAX - dist );
        if( dist < 3 && !has_effect( effect_npc_fire_bad ) ) {
            warn_about( "fire_bad", 1_minutes );
            add_effect( effect_npc_fire_bad, 5_turns );
            path.clear();
        }
    }

    // Find our Character friends and enemies.
    // NPC friends are cached across turns; only rebuild when faction membership or active NPC
    // list changes (tracked via g_npc_friends_dirty_version).
    std::vector<weak_ptr_fast<Creature>> hostile_guys;
    auto friend_positions = std::vector<tripoint_bub_ms> {};
    const auto remember_friend_position = [&]( const weak_ptr_fast<Creature> &guy ) {
        if( auto ally = guy.lock() ) { friend_positions.push_back( ally->bub_pos() ); }
    };
    {
        ZoneScopedN( "npc_friend_enemy_scan" );
        const uint32_t current_version = g_npc_friends_dirty_version.load(
                                             std::memory_order_relaxed );
        const bool friends_dirty = ( ai_cache.npc_friends_version != current_version );
        if( friends_dirty ) { ai_cache.cached_npc_friends.clear(); }
        for( const shared_ptr_fast<npc> &npc_ptr : g->raw_npcs() ) {
            const npc& guy = *npc_ptr;
            if( &guy == this || guy.is_dead() ) { continue; }
            if( has_faction_relationship( guy, npc_factions::watch_your_back ) ) {
                if( friends_dirty ) {
                    ai_cache.cached_npc_friends.emplace_back( g->shared_from( guy ) );
                }
            } else if( attitude_to( guy ) != Attitude::A_NEUTRAL && sees( guy.bub_pos() ) ) {
                hostile_guys.emplace_back( g->shared_from( guy ) );
            }
        }
        if( friends_dirty ) { ai_cache.npc_friends_version = current_version; }
        for( const weak_ptr_fast<Creature> &guy : ai_cache.cached_npc_friends ) {
            ai_cache.friends.emplace_back( guy );
            remember_friend_position( guy );
        }
    }
    if( sees( player_character.bub_pos() ) ) {
        if( is_enemy() ) {
            hostile_guys.emplace_back( g->shared_from( player_character ) );
        } else if( is_friendly( player_character ) ) {
            auto player_ref = g->shared_from( player_character );
            friend_positions.push_back( player_character.bub_pos() );
            ai_cache.friends.emplace_back( player_ref );
        }
    }

    // Stride the monster scan for Tier-1 NPCs — skip on non-scan turns.
    const bool skip_monster_scan =
        npc_lod_tier == 1 && !calendar::stride_due( npc_coarse_danger_interval );
    if( !skip_monster_scan ) {
        ZoneScopedN( "assess_all_monsters" );
        for( const shared_ptr_fast<monster> &mon_ptr : g->critter_tracker->get_monsters_list() ) {
            if( mon_ptr->is_dead() ) { continue; }
            monster& critter = *mon_ptr;
            const auto dist = rl_dist_fast( bub_pos(), critter.bub_pos() );
            if( dist > default_daylight_level() ) { continue; }
            Attitude att;
            if( !has_special_attitude_traits
                && critter.cached_npc_attitude_epoch == g_npcmove_attitude_epoch ) {
                ZoneScopedN( "npc_monster_attitude_cache_hit" );
                att = critter.cached_npc_attitude;
            } else {
                ZoneScopedN( "npc_monster_attitude_cache_miss" );
                att = has_special_attitude_traits
                      ? critter.attitude_to( *this )
                      : critter.generic_npc_attitude_to();
                if( !has_special_attitude_traits ) {
                    critter.cached_npc_attitude_epoch = g_npcmove_attitude_epoch;
                    critter.cached_npc_attitude = att;
                }
            }
            if( att == Attitude::A_FRIENDLY ) {
                ai_cache.friends.emplace_back( g->shared_from( critter ) );
                friend_positions.push_back( critter.bub_pos() );
                continue;
            }
            // Skip non-hostile monsters entirely — includes MATT_IGNORE, MATT_FLEE, and
            // MATT_FOLLOW (tracking but not yet attacking; take neutral attitude at face value).
            if( att != Attitude::A_HOSTILE ) { continue; }
            if( !npc_turn_cached_sees( *this, critter ) ) { continue; }
            float critter_threat = evaluate_enemy( critter );
            // warn and consider the odds for distant enemies
            if( ( is_enemy() || !critter.friendly ) ) {
                assessment += critter_threat;
                if( critter_threat > ( 8.0f + personality.bravery + rng( 0, 5 ) ) ) {
                    warn_about( "monster", 10_minutes, critter.type->nname(), dist,
                                critter.bub_pos() );
                }
            }
            if( must_retreat || no_fighting ) { continue; }
            // ignore targets behind glass even if we can see them
            if( !clear_shot_reach( bub_pos(), critter.bub_pos(), false ) ) { continue; }

            float scaled_distance = std::max( 1.0f, dist / critter.speed_rating() );
            float hp_percent = 1.0f - static_cast<float>( critter.get_hp() ) / critter.get_hp_max();
            float critter_danger =
                std::max( critter_threat * ( hp_percent * 0.5f + 0.5f ), NPC_DANGER_VERY_LOW );
            ai_cache.total_danger += critter_danger / scaled_distance;

            // don't ignore monsters that are too close or too close to an ally if we can move
            bool is_too_close = dist <= def_radius;
            for( const tripoint_bub_ms& ally_pos : friend_positions ) {
                if( is_too_close || self_defense_only ) { break; }
                is_too_close |= too_close( critter.bub_pos(), ally_pos, def_radius );
            }
            // ignore distant monsters that our rules prevent us from attacking
            if( !is_too_close && is_player_ally() && !ok_by_rules( critter, dist, scaled_distance ) ) {
                continue;
            }
            // prioritize the biggest, nearest threats, or the biggest threats that are threatening
            // us or an ally
            // critter danger is always at least NPC_DANGER_VERY_LOW
            float priority = std::
                             max( critter_danger - 2.0f * ( scaled_distance - 1.0f ),
                                  is_too_close ? critter_danger : 0.0f );
            cur_threat_map[std::to_underlying( direction_from( bub_pos(), critter.bub_pos() ) )] +=
                priority;
            if( priority > highest_priority ) {
                highest_priority = priority;
                ai_cache.target = g->shared_from( critter );
                ai_cache.danger = critter_danger;
            }
        }
    } // assess_all_monsters

    if( assessment == 0.0 && hostile_guys.empty() ) {
        // When the monster scan was strided out this turn, preserve the danger
        // assessment and threat_map cached from the last scan instead of zeroing
        // them — otherwise a Tier-1 NPC fighting a monster-only horde would
        // oscillate between alarmed (scan turn) and oblivious (skip turns).
        if( !skip_monster_scan ) { ai_cache.danger_assessment = assessment; }
        return;
    }
    const auto handle_hostile =
        [&]( const Character & foe, float foe_threat, const std::string & bogey,
    const std::string & warning ) {
        int dist = rl_dist( bub_pos(), foe.bub_pos() );
        if( foe_threat > ( 8.0f + personality.bravery + rng( 0, 5 ) ) ) {
            warn_about( "monster", 10_minutes, bogey, dist, foe.bub_pos() );
        }

        int scaled_distance = std::max( 1, ( 100 * dist ) / foe.get_speed() );
        ai_cache.total_danger += foe_threat / scaled_distance;
        if( must_retreat || no_fighting ) { return 0.0f; }
        // ignore targets behind glass even if we can see them
        if( !clear_shot_reach( bub_pos(), foe.bub_pos(), false ) ) { return 0.0f; }
        bool is_too_close = dist <= def_radius;
        for( const weak_ptr_fast<Creature> &guy : ai_cache.friends ) {
            if( self_defense_only ) { break; }
            if( auto ally = guy.lock() ) {
                is_too_close |= too_close( foe.bub_pos(), ally->bub_pos(), def_radius );
                if( is_too_close ) { break; }
            }
        }

        if( !is_player_ally() || is_too_close || ok_by_rules( foe, dist, scaled_distance ) ) {
            float priority = std::
                             max( foe_threat - 2.0f * ( scaled_distance - 1 ),
                                  is_too_close ? std::max( foe_threat, NPC_DANGER_VERY_LOW ) : 0.0f );
            cur_threat_map[std::to_underlying( direction_from( bub_pos(), foe.bub_pos() ) )] +=
                priority;
            if( priority > highest_priority ) {
                warn_about( warning, 1_minutes );
                highest_priority = priority;
                ai_cache.danger = foe_threat;
                ai_cache.target = g->shared_from( foe );
            }
        }
        return foe_threat;
    };

    for( const weak_ptr_fast<Creature> &guy : hostile_guys ) {
        player* foe = dynamic_cast<player *>( guy.lock().get() );
        if( foe && foe->is_npc() ) {
            assessment += handle_hostile( *foe, evaluate_enemy( *foe ), "bandit", "kill_npc" );
        }
    }

    for( const weak_ptr_fast<Creature> &guy : ai_cache.friends ) {
        player* ally = dynamic_cast<player *>( guy.lock().get() );
        if( !( ally && ally->is_npc() ) ) { continue; }
        float guy_threat = evaluate_enemy( *ally );
        float min_danger = assessment >= NPC_DANGER_VERY_LOW ? NPC_DANGER_VERY_LOW : -10.0f;
        assessment = std::max( min_danger, assessment - guy_threat * 0.5f );
    }

    if( sees( player_character.bub_pos() ) ) {
        // Mod for the player
        // cap player difficulty at 150
        float player_diff = evaluate_enemy( player_character );
        if( is_enemy() ) {
            assessment += handle_hostile( player_character, player_diff, "maniac", "kill_player" );
        } else if( is_friendly( player_character ) ) {
            float min_danger = assessment >= NPC_DANGER_VERY_LOW ? NPC_DANGER_VERY_LOW : -10.0f;
            assessment = std::max( min_danger, assessment - player_diff * 0.5f );
            ai_cache.friends.emplace_back( g->shared_from( player_character ) );
        }
    }
    assessment *= 0.1f;
    if( !has_effect( effect_npc_run_away ) && !has_effect( effect_npc_fire_bad ) ) {
        float my_diff = evaluate_enemy( *this );
        if( ( my_diff * 0.5f + personality.bravery + rng( 0, 10 ) ) < assessment ) {
            time_duration run_away_for = 5_turns + 1_turns * rng( 0, 5 );
            warn_about( "run_away", run_away_for );
            add_effect( effect_npc_run_away, run_away_for );
            path.clear();
        }
    }
    // update the threat cache
    for( size_t i = 0; i < 8; i++ ) {
        direction threat_dir = npc_threat_dir[i];
        direction dir_right = npc_threat_dir[( i + 1 ) % 8];
        direction dir_left = npc_threat_dir[( i + 7 ) % 8 ];
        ai_cache.threat_map[std::to_underlying( threat_dir )] =
            cur_threat_map[std::to_underlying( threat_dir )] + 0.1f *
            ( cur_threat_map[std::to_underlying( dir_right )] + cur_threat_map[std::to_underlying(
                    dir_left )] );
    }
    if( assessment <= 2.0f ) {
        assessment = -10.0f + 5.0f * assessment; // Low danger if no monsters around
    }
    ai_cache.danger_assessment = assessment;
}

float npc::character_danger( const Character& u ) const
{
    float ret = 0.0;
    bool u_gun = u.primary_weapon().is_gun();
    bool my_gun = primary_weapon().is_gun();
    double u_weap_val = npc_ai::wielded_value( u );
    const double &my_weap_val = ai_cache.my_weapon_value;
    if( u_gun && !my_gun ) { u_weap_val *= 1.5f; }
    ret += u_weap_val;

    static const bodypart_id torso_id( "torso" );
    ret += hp_percentage() * get_hp_max( torso_id ) / 100.0 / my_weap_val;

    ret += my_gun ? u.get_dodge() / 2 : u.get_dodge();

    ret *= std::max( 0.5, u.get_speed() / 100.0 );

    add_msg( m_debug, "%s danger: %1f", u.disp_name(), ret );
    return ret;
}

void npc::regen_ai_cache()
{
    ZoneScoped;
    map& here = get_map();
    auto i = std::begin( ai_cache.sound_alerts );
    while( i != std::end( ai_cache.sound_alerts ) ) {
        if( sees( here.abs_to_bub( i->abs_pos ) ) ) {
            i = ai_cache.sound_alerts.erase( i );
            if( ai_cache.sound_alerts.size() == 1 ) { path.clear(); }
        } else {
            ++i;
        }
    }
    float old_assessment = ai_cache.danger_assessment;
    ai_cache.friends.clear();
    ai_cache.target = shared_ptr_fast<Creature>();
    ai_cache.ally = shared_ptr_fast<Creature>();
    ai_cache.can_heal.clear_all();
    ai_cache.danger = 0.0f;
    ai_cache.total_danger = 0.0f;
    ai_cache.my_weapon_value = npc_ai::wielded_value( *this );
    ai_cache.dangerous_explosives = find_dangerous_explosives();

    assess_danger();
    if( old_assessment > NPC_DANGER_VERY_LOW && ai_cache.danger_assessment <= 0 ) {
        warn_about( "relax", 30_minutes );
    } else if( old_assessment <= 0.0f && ai_cache.danger_assessment > NPC_DANGER_VERY_LOW ) {
        warn_about( "general_danger" );
    }
    // Non-allied NPCs with a completed mission should move to the player
    if( !is_player_ally() && !is_stationary( true ) ) {
        Character& player_character = get_player_character();
        for( auto& miss : chatbin.missions_assigned ) {
            if( miss->is_complete( getID() ) ) {
                // unless the player found an item and already told the NPC he wanted to keep it
                const mission_goal& mgoal = miss->get_type().goal;
                if( ( mgoal == MGOAL_FIND_ITEM || mgoal == MGOAL_FIND_ANY_ITEM
                      || mgoal == MGOAL_FIND_ITEM_GROUP )
                    && has_effect( effect_npc_player_looking ) ) {
                    continue;
                }
                if( abs_omt_pos() != player_character.abs_omt_pos() ) {
                    goal = player_character.abs_omt_pos();
                }
                set_attitude( NPCATT_TALK );
                break;
            }
        }
    }
}

void npc::move()
{
    ZoneScoped;
    // don't just return from this function without doing something
    // that will eventually subtract moves, or change the NPC to a different type of action.
    // because this will result in an infinite loop
    if( attitude == NPCATT_FLEE ) {
        set_attitude( NPCATT_FLEE_TEMP ); // Only run for so many hours
    } else if( attitude == NPCATT_FLEE_TEMP && !has_effect( effect_npc_flee_player ) ) {
        set_attitude( NPCATT_NULL );
    }
    // Co-op proxy NPC: the game loop already granted moves via process_turn();
    // we must consume them here so the while(moves>0) caller doesn't spin to
    // 10 iterations and call reboot() (which would teleport the proxy).  Drain
    // happens in coop_world_tick() AFTER post_action_world_step() returns — the
    // drain sets its own budget with set_moves(get_speed()) just before each
    // execute_client_action() call, so zeroing here doesn't interfere.
    if( is_coop_remote ) {
        set_moves( 0 );
        return;
    }
    // Tier 2 macro step: distant NPCs skip full AI on non-step turns.
    if( npc_lod_tier == 2 && !calendar::stride_due( npc_macro_interval ) ) { return; }
    regen_ai_cache();
    adjust_power_cbms();
    {
        ZoneScopedN( "npc_execute_action" );
        execute_action( decide_action() );
    }
}

auto npc::decide_action() -> npc_cmd_t
{
    // NPCs under operation should just stay still
    if( activity->id() == activity_id( "ACT_OPERATION" ) ) {
        return npc_cmd_t{.kind = npc_player_activity};
    }

    npc_action action = npc_undecided;

    static const std::string no_target_str = "none";
    const Creature* target = current_target();
    const std::string& target_name = target != nullptr ? target->disp_name() : no_target_str;
    add_msg(
        m_debug, "NPC %s: target = %s, danger = %.1f, range = %d", name, target_name,
        ai_cache.danger,
        primary_weapon().is_gun()
        ? confident_shoot_range( primary_weapon(), ranged::recoil_total( *this ) )
        : primary_weapon().reach_range( *this ) );

    Character& player_character = get_player_character();
    // faction opinion determines if it should consider you hostile
    if( !is_enemy() && guaranteed_hostile() && sees( player_character ) ) {
        if( is_player_ally() ) { mutiny(); }
        add_msg( m_debug, "NPC %s turning hostile because is guaranteed_hostile()", name );
        if( op_of_u.fear > 10 + personality.aggression + personality.bravery ) {
            set_attitude( NPCATT_FLEE_TEMP ); // We don't want to take u on!
        } else {
            set_attitude( NPCATT_KILL ); // Yeah, we think we could take you!
        }
    }

    /* This bypasses the logic to determine the npc action, but this all needs to be rewritten
     * anyway.
     * NPC won't avoid dangerous terrain while accompanying the player inside a vehicle to keep
     * them from inadvertently getting themselves run over and/or cause vehicle related errors.
     * NPCs flee from uncontained fires within 3 tiles
     */
    if( !in_vehicle && ( sees_dangerous_field( bub_pos() ) || has_effect( effect_npc_fire_bad ) ) ) {
        const auto danger_at_pos = sees_dangerous_field( bub_pos() );
        // danger_at_pos: clear path before querying so good_escape_direction() takes the
        // retreat-zone branch (up to 60 tiles). Without this clear it only rates adjacents.
        if( danger_at_pos ) { path.clear(); }
        const auto escape_dir = good_escape_direction( danger_at_pos );
        if( escape_dir != bub_pos() ) {
            // Unconditional clear so execute_action(npc_flee) takes move_to(tar) not
            // move_to_next() — matches the original direct move_to(escape_dir) call.
            path.clear();
            return npc_cmd_t{.kind = npc_flee, .dest = escape_dir};
        }
        // else: no valid escape direction — fall through to normal decision tree
    }

    // TODO: Place player-aiding actions here, with a weight

    /* NPCs are fairly suicidal so at this point we will do a quick check to see if
     * something nasty is going to happen.
     */

    if( is_enemy() && vehicle_danger( avoidance_vehicles_radius ) > 0 ) {
        // TODO: Think about how this actually needs to work, for now assume flee from player
        ai_cache.target = g->shared_from( player_character );
    }

    map& here = get_map();
    if( !ai_cache.dangerous_explosives.empty() ) {
        action = npc_escape_explosion;
    } else if( target == &player_character && attitude == NPCATT_FLEE_TEMP ) {
        action = method_of_fleeing();
    } else if( has_effect( effect_npc_run_away ) ) {
        action = method_of_fleeing();
    } else if( has_effect( effect_asthma )
               && ( has_charges( itype_inhaler, 1 ) || has_charges( itype_oxygen_tank, 1 )
                    || has_charges( itype_smoxygen_tank, 1 ) ) ) {
        action = npc_heal;
    } else if( target != nullptr && ai_cache.danger > 0 ) {
        action = method_of_attack();
    } else if( !ai_cache.sound_alerts.empty() && !is_walking_with() ) {
        auto cur_s_abs_pos = ai_cache.s_abs_pos;
        if( !ai_cache.guard_pos ) { ai_cache.guard_pos = abs_pos(); }
        if( ai_cache.sound_alerts.size() > 1 ) {
            std::sort( ai_cache.sound_alerts.begin(), ai_cache.sound_alerts.end(),
                       compare_sound_alert );
            if( ai_cache.sound_alerts.size() > 10 ) { ai_cache.sound_alerts.resize( 10 ); }
        }
        action = npc_investigate_sound;
        if( ai_cache.sound_alerts.front().abs_pos != cur_s_abs_pos ) {
            ai_cache.stuck = 0;
            ai_cache.s_abs_pos = ai_cache.sound_alerts.front().abs_pos;
        } else if( ai_cache.stuck > 10 ) {
            ai_cache.stuck = 0;
            if( ai_cache.sound_alerts.size() == 1 ) {
                ai_cache.sound_alerts.clear();
                action = npc_return_to_guard_pos;
            } else {
                ai_cache.s_abs_pos = ai_cache.sound_alerts.at( 1 ).abs_pos;
            }
        }
        if( action == npc_investigate_sound ) {
            add_msg( m_debug, "NPC %s: investigating sound at x(%d) y(%d)", name,
                     ai_cache.s_abs_pos.x(), ai_cache.s_abs_pos.y() );
        }
    } else {
        // No present danger
        deactivate_combat_cbms();

        // Deactivate Armor & Weapons
        for( auto& elem : worn ) {
            // The is_active() part was taken from is_wearing_active_power_armor
            if( elem->has_flag( flag_COMBAT_NPC_USE ) && elem->has_flag( flag_COMBAT_NPC_ON ) ) {
                if( elem->get_use( "transform" ) ) {
                    invoke_item( elem, "transform" );
                    recalculate_enchantment_cache();
                } else if( elem->get_use( "set_transform" ) ) {
                    invoke_item( elem, "set_transform" );
                    recalculate_enchantment_cache();
                }
            }
        }
        item& weapon = primary_weapon();
        if( !weapon.is_null() && weapon.has_flag( flag_COMBAT_NPC_USE )
            && weapon.has_flag( flag_COMBAT_NPC_ON ) ) {
            if( weapon.get_use( "transform" ) ) {
                invoke_item( &weapon, "transform" );
                recalculate_enchantment_cache();
            } else if( weapon.get_use( "fireweapon_on" ) ) {
                invoke_item( &weapon, "fireweapon_on" );
                recalculate_enchantment_cache();
            }
        }

        action = address_needs();
        print_action( "address_needs %s", action );

        if( action == npc_undecided ) {
            action = address_player();
            print_action( "address_player %s", action );
        }
        if( ai_cache.sound_alerts.empty() && ai_cache.guard_pos ) {
            auto return_guard_pos = *ai_cache.guard_pos;
            add_msg( m_debug, "NPC %s: returning to guard spot at x(%d) y(%d)", name,
                     return_guard_pos.x(), return_guard_pos.y() );
            action = npc_return_to_guard_pos;
        }
    }

    if( action == npc_undecided && is_walking_with() && goto_to_this_pos ) {
        action = npc_goto_to_this_pos;
    }

    // check if in vehicle before doing any other follow activities
    if( action == npc_undecided && is_walking_with() && player_character.in_vehicle
        && !in_vehicle ) {
        action = npc_follow_embarked;
    }

    if( action == npc_undecided && is_walking_with() && rules.has_flag( ally_rule::follow_close )
        && rl_dist( bub_pos(), player_character.bub_pos() ) > follow_distance() ) {
        action = npc_follow_player;
    }

    if( action == npc_undecided && attitude == NPCATT_ACTIVITY ) {
        if( has_stashed_activity() ) {
            if( !check_outbounds_activity( get_stashed_activity() ) ) {
                assign_stashed_activity();
                return npc_cmd_t{.kind = npc_player_activity};
            } else {
                // wait a turn, because next turn, the object of our activity
                // may have been loaded in. set_moves(0) only — npc_pause would run
                // move_pause() side effects (bionics, aim) that the original didn't.
                set_moves( 0 );
                return npc_cmd_t{.kind = npc_noop};
            }
        }
        std::vector<tripoint_bub_ms> activity_route = get_auto_move_route();
        if( !activity_route.empty() && !has_destination_activity() ) {
            tripoint_bub_ms final_destination;
            if( destination_point ) {
                final_destination = here.abs_to_bub( *destination_point );
            } else {
                final_destination = activity_route.back();
            }
            update_path( final_destination );
            if( !path.empty() ) { return npc_cmd_t{.kind = npc_move_to_next}; }
        }
        if( has_destination_activity() ) {
            start_destination_activity();
            action = npc_player_activity;
        } else if( has_player_activity() ) {
            action = npc_player_activity;
        }
    }
    if( action == npc_undecided ) {
        // an interrupted activity can cause this situation. stops allied NPCs zooming off
        // like random NPCs
        if( attitude == NPCATT_ACTIVITY && !activity ) {
            revert_after_activity();
            if( is_ally( player_character ) ) {
                attitude = NPCATT_FOLLOW;
                mission = NPC_MISSION_NULL;
            }
        }
        if( is_stationary( true ) ) {
            // if we're in a vehicle, stay in the vehicle
            if( in_vehicle ) {
                action = npc_pause;
                goal = abs_omt_pos();
            } else {
                action = goal == abs_omt_pos() ? npc_pause : npc_goto_destination;
            }
        } else if( has_new_items ) {
            scan_new_items();
            return npc_cmd_t{.kind = npc_noop};
        } else if( !fetching_item ) {
            find_item();
            print_action( "find_item %s", action );
        }

        // check if in vehicle before rushing off to fetch things
        if( is_walking_with() && player_character.in_vehicle ) {
            action = npc_follow_embarked;
        } else if( fetching_item ) {
            // Set to true if find_item() found something
            action = npc_pickup;
        } else if( is_following() ) {
            // No items, so follow the player?
            action = npc_follow_player;
        }
        // Friendly NPCs who are followers/ doing tasks for the player should never get here.
        // This will revert them to a dynamic NPC state.
        if( action == npc_undecided ) {
            // Do our long-term action
            action = long_term_goal_action();
            print_action( "long_term_goal_action %s", action );
        }
    }

    /* Sometimes we'll be following the player at this point, but close enough that
     * "following" means standing still.  If that's the case, if there are any
     * monsters around, we should attack them after all!
     *
     * If we are following a embarked player and we are in a vehicle then shoot anyway
     * as we are most likely riding shotgun
     */
    if( ai_cache.danger > 0 && target != nullptr
        && ( ( action == npc_follow_embarked && in_vehicle )
             || ( action == npc_follow_player
                  && ( rl_dist( bub_pos(), player_character.bub_pos() ) <= follow_distance()
                       || bub_pos().z() != player_character.bub_pos().z() ) ) ) ) {
        action = method_of_attack();
    }

    add_msg( m_debug, "%s chose action %s.", name, npc_action_name( action ) );
    return resolve_cmd( action );
}

auto npc::resolve_cmd( npc_action action ) -> npc_cmd_t
{
    // good_escape_direction() mutates path and draws RNG — guard it to npc_flee only.
    Creature* const cmd_target = current_target();
    const auto cmd_dest =
        action == npc_flee
        ? good_escape_direction( false )
        : ( cmd_target != nullptr ? cmd_target->bub_pos() : bub_pos() );
    return npc_cmd_t{.kind = action, .target = cmd_target, .dest = cmd_dest};
}

void npc::execute_action( const npc_cmd_t &cmd )
{
    const auto action = cmd.kind;
    int oldmoves = moves;
    const auto tar = cmd.dest;
    auto* const cur = cmd.target;
    /*
      debugmsg("%s ran execute_action() with target = %d! Action %s",
               name, target, npc_action_name(action));
    */

    Character& player_character = get_player_character();
    map& here = get_map();
    switch( action ) {
        case npc_pause:
            move_pause();
            break;
        case npc_reload: {
            do_reload( primary_weapon() );
        }
        break;

        case npc_investigate_sound: {
            auto cur_pos = bub_pos();
            update_path( here.abs_to_bub( ai_cache.s_abs_pos ) );
            move_to_next();
            if( bub_pos() == cur_pos ) { ai_cache.stuck += 1; }
        }
        break;

        case npc_return_to_guard_pos: {
            const auto local_guard_pos = here.abs_to_bub( *ai_cache.guard_pos );
            update_path( local_guard_pos );
            if( bub_pos() == local_guard_pos || path.empty() ) {
                move_pause();
                ai_cache.guard_pos = std::nullopt;
                path.clear();
            } else {
                move_to_next();
            }
        }
        break;

        case npc_sleep: {
            // TODO: Allow stims when not too tired
            // Find a nice spot to sleep
            int best_sleepy = character_funcs::rate_sleep_spot( *this, bub_pos() );
            auto best_spot = bub_pos();
            for( const auto& p : closest_points_first( bub_pos(), 6 ) ) {
                if( !could_move_onto( p ) || !g->is_empty( p ) ) { continue; }

                // TODO: Blankets when it's cold
                const int sleepy = character_funcs::rate_sleep_spot( *this, p );
                if( sleepy > best_sleepy ) {
                    best_sleepy = sleepy;
                    best_spot = p;
                }
            }
            if( is_walking_with() ) { complain_about( "napping", 30_minutes, _( "<warn_sleep>" ) ); }
            update_path( best_spot );
            // TODO: Handle empty path better
            if( best_spot == bub_pos() || path.empty() ) {
                move_pause();
                if( !has_effect( effect_lying_down ) ) {
                    activate_bionic_by_id( bio_soporific );
                    add_effect( effect_lying_down, 30_minutes, bodypart_str_id::NULL_ID(), 1 );
                    if( player_character.sees( *this ) && !player_character.in_sleep_state() ) {
                        add_msg( _( "%s lies down to sleep." ), name );
                    }
                }
            } else {
                move_to_next();
            }
        }
        break;

        case npc_pickup:
            pick_up_item();
            break;

        case npc_heal:
            heal_self();
            break;

        case npc_use_painkiller:
            use_painkiller();
            break;

        case npc_drop_items:
            /* NPCs can't choose this action anymore, but at least it works */
            drop_invalid_inventory();
            /* drop_items is still broken
             * drop_items( weight_carried() - weight_capacity(),
             *             volume_carried() - volume_capacity() );
             */
            move_pause();
            break;

        case npc_flee:
            if( path.empty() ) {
                move_to( tar );
            } else {
                move_to_next();
            }
            break;

        case npc_reach_attack:
            if( can_use_offensive_cbm() ) { activate_bionic_by_id( bio_hydraulics ); }
            reach_attack( tar );
            break;
        case npc_melee:
            update_path( tar );
            if( path.size() > 1 ) {
                move_to_next();
            } else if( path.size() == 1 ) {
                if( cur != nullptr ) {
                    if( can_use_offensive_cbm() ) { activate_bionic_by_id( bio_hydraulics ); }
                    melee_attack( *cur, true );
                }
            } else {
                look_for_player( player_character );
            }
            break;

        case npc_aim: {
            gun_mode mode =
                cbm_active.is_null()
                ? primary_weapon().gun_current_mode()
                : cbm_fake_active->gun_current_mode();
            if( !mode ) {
                std::string error_weapon =
                    cbm_active.is_null() ? primary_weapon().tname() : cbm_fake_active->tname();
                debugmsg( "NPC tried to aim %s without valid mode.", error_weapon );
            }

            bool did_aim = aim();
            if( !did_aim ) {
                debugmsg( "%s is trying to aim, but failing repeatedly", disp_name().c_str() );
                set_moves( 0 );
            }
        }
        break;

        case npc_shoot: {
            gun_mode mode =
                cbm_active.is_null()
                ? primary_weapon().gun_current_mode()
                : cbm_fake_active->gun_current_mode();
            if( !mode ) {
                std::string error_weapon =
                    cbm_active.is_null() ? primary_weapon().tname() : cbm_fake_active->tname();
                debugmsg( "NPC tried to shoot %s without valid mode.", error_weapon );
            }

            aim();
            if( is_hallucination() ) {
                pretend_fire( this, mode.qty, *mode );
            } else {
                add_msg( m_debug, "%s recoil on firing: %s", name, recoil );
                ranged::fire_gun( *this, tar, mode.qty, *mode, nullptr );
                // Clear the ranged cbm entry and item so next turn a new comparison is made.
                if( !cbm_active.is_null() ) { discharge_cbm_weapon(); }
            }
            // Important, once they've fired their gun, wield calculations have to be redone
            // else they'll fail to realize when they run out of ammo.
            clear_npc_ai_info_cache( npc_ai_info::range );
            break;
        }

        case npc_look_for_player:
            if( saw_player_recently() && last_player_seen_pos && sees( *last_player_seen_pos ) ) {
                update_path( *last_player_seen_pos );
                move_to_next();
            } else {
                look_for_player( player_character );
            }
            break;

        case npc_heal_player: {
            player* patient = dynamic_cast<player *>( current_ally() );
            if( patient ) {
                update_path( patient->bub_pos() );
                if( path.size() == 1 ) { // We're adjacent to u, and thus can heal u
                    heal_player( *patient );
                } else if( !path.empty() ) {
                    say( _( "Hold still %s, I'm coming to help you." ), patient->disp_name() );
                    move_to_next();
                } else {
                    move_pause();
                }
            }
            break;
        }
        case npc_follow_player:
            update_path( player_character.bub_pos() );
            move_mode =
                rules.has_flag( ally_rule::move_own_pace )
                ? ( ( static_cast<int>( path.size() ) > follow_distance() * 4 ) ? CMM_RUN : CMM_WALK )
                : player_character.get_movement_mode();
            if( static_cast<int>( path.size() ) <= follow_distance()
                && player_character.bub_pos().z() == bub_pos().z() ) { // We're close enough to u.
                move_pause();
            } else if( !path.empty() ) {
                move_to_next();
            } else {
                move_pause();
            }
            // TODO: Make it only happen when it's safe
            complain();
            break;

        case npc_follow_embarked: {
            move_mode =
                rules.has_flag( ally_rule::move_own_pace )
                ? ( ( static_cast<int>( path.size() ) > follow_distance() * 4 ) ? CMM_RUN : CMM_WALK )
                : player_character.get_movement_mode();
            const optional_vpart_position vp = here.veh_at( player_character.bub_pos() );

            if( !vp ) {
                debugmsg( "Following an embarked player with no vehicle at their location?" );
                // TODO: change to wait? - for now pause
                move_pause();
                break;
            }
            vehicle* const veh = &vp->vehicle();

            // Try to find the last destination
            // This is mount point, not actual position
            point last_dest( INT_MIN, INT_MIN );
            if( !path.empty() && veh_pointer_or_null( here.veh_at( path[path.size() - 1] ) ) == veh ) {
                last_dest = vp->mount().xy().raw();
            }

            // Prioritize last found path, then seats
            // Don't change spots if ours is nice
            int my_spot = -1;
            std::vector<std::pair<int, int>> seats;
            for( const vpart_reference& vp : veh->get_avail_parts( VPFLAG_BOARDABLE ) ) {
                const player* passenger = veh->get_passenger( vp.part_index() );
                if( passenger != this && passenger != nullptr ) { continue; }

                // a seat is available if either unassigned or assigned to us
                auto available_seat = [&]( const vehicle_part & pt ) {
                    if( !pt.is_seat() ) { return false; }
                    const npc* who = pt.crew();
                    return !who || who->getID() == getID();
                };

                const vehicle_part& pt = vp.part();

                int priority = 0;

                if( vp.mount().xy().raw() == last_dest ) {
                    // Shares mount point with last known path
                    // We probably wanted to go there in the last turn
                    priority = 4;

                } else if( available_seat( pt ) ) {
                    // Assuming player "owns" a sensible vehicle seats should be in good spots to
                    // occupy Prefer our assigned seat if we have one
                    const npc* who = pt.crew();
                    priority = who && who->getID() == getID() ? 3 : 2;

                } else if( vp.is_inside() ) {
                    priority = 1;
                }

                if( passenger == this ) { my_spot = priority; }

                seats.emplace_back( priority, static_cast<int>( vp.part_index() ) );
            }

            if( my_spot >= 3 ) {
                // We won't get any better, so don't try
                move_pause();
                break;
            }

            std::sort( seats.begin(), seats.end(),
            []( const std::pair<int, int> &l, const std::pair<int, int> &r ) {
                return l.first > r.first;
            } );

            if( seats.empty() ) {
                // TODO: be angry at player, switch to wait or leave - for now pause
                move_pause();
                break;
            }

            // Only check few best seats - pathfinding can get expensive
            const size_t try_max = std::min<size_t>( 4, seats.size() );
            for( size_t i = 0; i < try_max; i++ ) {
                if( seats[i].first <= my_spot ) {
                    // We have a nicer spot than this
                    // Note: this will make NPCs steal player's seat...
                    break;
                }

                const int cur_part = seats[i].second;

                tripoint_bub_ms pp = veh->bub_part_location( cur_part );
                update_path( pp, true );
                if( !path.empty() ) {
                    // All is fine
                    move_to_next();
                    break;
                }
            }

            // TODO: Check the rest
            move_pause();
        }

        break;
        case npc_talk_to_player:
            talk_to_u();
            moves = 0;
            break;

        case npc_mug_player:
            mug_player( player_character );
            break;

        case npc_goto_to_this_pos: {
            if( !goto_to_this_pos.has_value() ) {
                debugmsg( "npc_goto_to_this_pos set to true, but no target set" );
                break;
            }
            update_path( get_map().abs_to_bub( goto_to_this_pos.value() ) );
            move_to_next();

            if( abs_pos() == goto_to_this_pos.value() ) {
                add_msg( m_debug, "%s reached target", disp_name() );
                goto_to_this_pos = std::nullopt;
            }
            break;
        }

        case npc_goto_destination:
            go_to_omt_destination();
            break;

        case npc_avoid_friendly_fire:
            avoid_friendly_fire();
            break;

        case npc_escape_explosion:
            escape_explosion();
            break;

        case npc_player_activity:
            do_player_activity();
            break;

        case npc_move_to_next:
            move_to_next();
            break;

        case npc_undecided:
            complain();
            move_pause();
            break;

        case npc_noop:
            add_msg( m_debug, "%s skips turn (noop)", disp_name() );
            return;

        default:
            debugmsg( "Unknown NPC action (%d)", action );
    }

    if( oldmoves == moves ) {
        add_msg( m_debug, "NPC didn't use its moves.  Action %s (%d).", npc_action_name( action ),
                 action );
    }
}

npc_action npc::method_of_fleeing()
{
    if( in_vehicle ) { return npc_undecided; }
    return npc_flee;
}

void npc::activate_combat_gear()
{
    activate_combat_cbms();

    // Activate Armor & Weapons
    for( auto &elem : worn ) {
        // The is_active() part was taken from is_wearing_active_power_armor
        if( elem->has_flag( flag_COMBAT_NPC_USE ) && !elem->has_flag( flag_COMBAT_NPC_ON ) ) {
            if( elem->get_use( "transform" ) ) {
                invoke_item( elem, "transform" );
            } else if( elem->get_use( "set_transform" ) ) {
                invoke_item( elem, "set_transform" );
            }
        }
    }
    item &weapon = primary_weapon();
    if( !weapon.is_null() && weapon.has_flag( flag_COMBAT_NPC_USE ) &&
        !weapon.has_flag( flag_COMBAT_NPC_ON ) ) {

        if( weapon.get_use( "transform" ) ) {
            invoke_item( &weapon, "transform" );
        } else if( weapon.get_use( "fireweapon_off" ) ) {
            invoke_item( &weapon, "fireweapon_off" );
        }
    }
}

gun_mode npc::resolve_gun_mode( bool can_use_gun, int dist, bool use_silent ) const
{
    gun_mode g_mode = cbm_active.is_null() ? primary_weapon().gun_current_mode() :
                      cbm_fake_active->gun_current_mode();
    if( !can_use_gun || dist == 0 ||
        ( g_mode && ( ( use_silent && !g_mode->is_silent() ) ||
                      ( item_funcs::shots_remaining( *this, *g_mode ) < g_mode.qty ) ) ) ) {
        g_mode = gun_mode();
    }
    return g_mode;
}

npc_action npc::method_of_attack()
{
    Character &player_character = get_player_character();
    Creature *critter = current_target();
    if( critter == nullptr ) {
        // This function shouldn't be called...
        debugmsg( "Ran npc::method_of_attack without a target!" );
        return npc_pause;
    }

    auto tar = critter->bub_pos();
    int dist = rl_dist( bub_pos(), tar );
    const bool has_los = clear_shot_reach( bub_pos(), tar, false );
    const bool same_z = tar.z() == bub_pos().z();
    const int cur_recoil = ranged::recoil_total( *this );

    // TODO: Change the in_vehicle check to actual "are we driving" check
    const bool dont_move =
        in_vehicle || rules.engagement == combat_engagement::NO_MOVE
        || rules.engagement == combat_engagement::FREE_FIRE;
    // NPCs engage in free fire can move to avoid allies, but not if they're in a vehicle
    const bool dont_move_ff = in_vehicle || rules.engagement == combat_engagement::NO_MOVE;
    bool can_use_gun =
        ( ( !is_player_ally() || rules.has_flag( ally_rule::use_guns ) )
          && ( ai_cache.danger >= 3 || emergency() || dist < 0 ) );
    bool use_silent = ( is_player_ally() && rules.has_flag( ally_rule::use_silent ) );
    const bool not_engaged_yet =
        !critter->has_effect( effect_hit_by_player ) && rules.engagement == combat_engagement::HIT;

    activate_combat_gear();

    if( emergency() && alt_attack() ) {
        add_msg( m_debug, "%s is trying an alternate attack", disp_name() );
        return npc_noop;
    }

    // TODO: Add a time check now that wielding takes a lot of time
    if( wield_better_weapon() ) {
        add_msg( m_debug, "%s is changing weapons", disp_name() );
        return npc_noop;
    }

    gun_mode g_mode = resolve_gun_mode( can_use_gun, dist, use_silent );

    // reach attacks are silent and consume no ammo so prefer these if available
    int reach_range = primary_weapon().reach_range( *this );
    if( reach_range > 1 && reach_range >= dist && clear_shot_reach( bub_pos(), tar ) ) {
        add_msg( m_debug, "%s is trying a reach attack", disp_name() );
        return npc_reach_attack;
    }

    // if the best mode is within the confident range try for a shot
    if( g_mode && sees( *critter ) && has_los && g_mode->gun_range( true ) >= dist
        && confident_gun_mode_range( g_mode, cur_recoil ) >= dist ) {
        if( wont_hit_friend( tar, *g_mode, false ) ) {
            add_msg( m_debug, "%s is trying to shoot someone", disp_name() );
            return npc_shoot;

        } else {
            if( !dont_move_ff ) {
                add_msg( m_debug, "%s is trying to avoid friendly fire", disp_name() );
                return npc_avoid_friendly_fire;
            }
        }
    }

    if( !primary_weapon().ammo_sufficient() && can_reload_current() ) {
        add_msg( m_debug, "%s is reloading", disp_name() );
        return npc_reload;
    }

    if( dist == 1 && same_z ) {
        add_msg( m_debug, "%s is trying a melee attack", disp_name() );
        return npc_melee;
    }

    // TODO: Needs a check for transparent but non-passable tiles on the way
    int effective_range =
        g_mode ? confident_gun_mode_range( g_mode, ranged::get_most_accurate_sight( *this, *g_mode ) )
        : 0;
    if( g_mode && sees( *critter ) && ranged::aim_per_move( *this, *g_mode, recoil ) > 0
        && effective_range >= dist ) {
        add_msg( m_debug, "%s is aiming", disp_name() );
        if( critter->is_player() && player_character.sees( *this ) ) {
            add_msg( m_bad, _( "%s takes aim at you!" ), disp_name() );
        }
        return npc_aim;
    }
    add_msg( m_debug, "%s can't figure out what to do", disp_name() );
    return ( dont_move || !same_z || not_engaged_yet ) ? npc_undecided : npc_melee;
}
