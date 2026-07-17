#include "npc.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>

#include "auto_pickup.h"
#include "activity_actor_definitions.h"
#include "avatar.h"
#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "character.h"
#include "character_id.h"
#include "character_functions.h"
#include "character_martial_arts.h"
#include "catalua_hooks.h"
#include "catalua_sol.h"
#include "clzones.h"
#include "damage.h"
#include "debug.h"
#include "detached_ptr.h"
#include "effect.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "faction.h"
#include "flag.h"
#include "flat_set.h"
#include "game.h"
#include "game_constants.h"
#include "game_inventory.h"
#include "int_id.h"
#include "item.h"
#include "item_contents.h"
#include "item_group.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "json.h"
#include "locations.h"
#include "magic.h"
#include "map.h"
#include "map_iterator.h"
#include "map_selector.h"
#include "mapdata.h"
#include "math_defines.h"
#include "messages.h"
#include "mission.h"
#include "monster.h"
#include "morale_types.h"
#include "mtype.h"
#include "mutation.h"
#include "npc_class.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "overmapbuffer_registry.h"
#include "legacy_pathfinding.h"
#include "player_activity.h"
#include "pldata.h"
#include "ranged.h"
#include "ret_val.h"
#include "rng.h"
#include "skill.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_utils.h"
#include "text_snippets.h"
#include "tileray.h"
#include "trait_group.h"
#include "translations.h"
#include "tts_synthesizer.h"
#include "tts_voice_registry.h"
#include "units.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "visitable.h"
#include "vpart_position.h"
#include "vpart_range.h"

static const efftype_id effect_drunk( "drunk" );
static const efftype_id effect_feral_killed_recently( "feral_killed_recently" );
static const efftype_id effect_npc_flee_player( "npc_flee_player" );
static const efftype_id effect_pkill_l( "pkill_l" );
static const efftype_id effect_pkill1( "pkill1" );
static const efftype_id effect_pkill2( "pkill2" );
static const efftype_id effect_pkill3( "pkill3" );
static const efftype_id effect_ridden( "ridden" );
static const trait_id trait_BEE( "BEE" );
static const trait_id trait_CANNIBAL( "CANNIBAL" );
static const trait_id trait_KILLER( "KILLER" );
static const trait_id trait_PROF_FERAL( "PROF_FERAL" );
static const trait_id trait_PSYCHOPATH( "PSYCHOPATH" );
static const trait_id trait_SAPIOVORE( "SAPIOVORE" );
static const trait_id trait_TERRIFYING( "TERRIFYING" );

void npc::randomize_from_faction( faction *fac )
{
    // Personality = aggression, bravery, altruism, collector
    set_fac( fac->id() );
    randomize( npc_class_id::NULL_ID() );
}

npc_attitude npc::get_previous_attitude()
{
    return previous_attitude;
}

skill_id npc::best_skill() const
{
    int highest_level = std::numeric_limits<int>::min();
    skill_id highest_skill( skill_id::NULL_ID() );

    for( const auto &p : *_skills ) {
        if( p.first.obj().is_weapon_skill() ) {
            const int level = p.second.level();
            if( level > highest_level ) {
                highest_level = level;
                highest_skill = p.first;
            }
        }
    }

    return highest_skill;
}

int npc::best_skill_level() const
{
    int highest_level = std::numeric_limits<int>::min();

    for( const auto &p : *_skills ) {
        if( p.first.obj().is_combat_skill() ) {
            const int level = p.second.level();
            if( level > highest_level ) {
                highest_level = level;
            }
        }
    }

    return highest_level;
}

void npc::form_opinion( const Character &u )
{
    // FEAR
    if( u.primary_weapon().is_gun() ) {
        // TODO: Make bows not guns
        if( primary_weapon().is_gun() ) {
            op_of_u.fear += 2;
        } else {
            op_of_u.fear += 6;
        }
    } else if( npc_ai::wielded_value( u ) > 20 ) {
        op_of_u.fear += 2;
    } else if( !u.is_armed() ) {
        // Unarmed, but actually unarmed ("unarmed weapons" are not unarmed)
        op_of_u.fear -= 3;
    }

    ///\EFFECT_STR increases NPC fear of the player
    if( u.str_max >= 16 ) {
        op_of_u.fear += 2;
    } else if( u.str_max >= 12 ) {
        op_of_u.fear += 1;
    } else if( u.str_max <= 3 ) {
        op_of_u.fear -= 3;
    } else if( u.str_max <= 5 ) {
        op_of_u.fear -= 1;
    }

    // is your health low
    for( const std::pair<const bodypart_str_id, bodypart> &elem : get_player_character().get_body() ) {
        const int hp_max = elem.second.get_hp_max();
        const int hp_cur = elem.second.get_hp_cur();
        if( hp_cur <= hp_max / 2 ) {
            op_of_u.fear--;
        }
    }

    // is my health low
    for( const std::pair<const bodypart_str_id, bodypart> &elem : get_body() ) {
        const int hp_max = elem.second.get_hp_max();
        const int hp_cur = elem.second.get_hp_cur();
        if( hp_cur <= hp_max / 2 ) {
            op_of_u.fear++;
        }
    }

    if( u.has_trait( trait_SAPIOVORE ) ) {
        op_of_u.fear += 10; // Sapiovores = Scary
    }
    if( u.has_trait( trait_TERRIFYING ) ) {
        op_of_u.fear += 6;
    }

    int u_ugly = 0;
    for( trait_id &mut : u.get_mutations() ) {
        u_ugly += mut.obj().ugliness;
    }
    op_of_u.fear += u_ugly / 2;
    op_of_u.trust -= u_ugly / 3;

    if( u.get_stim() > 20 ) {
        op_of_u.fear++;
    }

    if( u.has_effect( effect_drunk ) ) {
        op_of_u.fear -= 2;
    }

    // TRUST
    if( op_of_u.fear > 0 ) {
        op_of_u.trust -= 3;
    } else {
        op_of_u.trust += 1;
    }

    if( u.primary_weapon().is_gun() ) {
        op_of_u.trust -= 2;
    } else if( !u.is_armed() ) {
        op_of_u.trust += 2;
    }

    // TODO: More effects
    if( u.has_effect( effect_drunk ) ) {
        op_of_u.trust -= 2;
    }
    if( u.get_stim() > 20 || u.get_stim() < -20 ) {
        op_of_u.trust -= 1;
    }
    if( u.get_painkiller() > 30 ) {
        op_of_u.trust -= 1;
    }

    if( op_of_u.trust > 0 ) {
        // Trust is worth a lot right now
        op_of_u.trust /= 2;
    }

    // VALUE
    op_of_u.value = 0;
    for( const std::pair<const bodypart_str_id, bodypart> &elem : get_body() ) {
        if( elem.second.get_hp_cur() < elem.second.get_hp_max() * 0.8f ) {
            op_of_u.value++;
        }
    }
    decide_needs();
    for( const npc_need &i : needs ) {
        if( i == need_food || i == need_drink ) {
            op_of_u.value += 2;
        }
    }

    if( op_of_u.fear < personality.bravery + 10 &&
        op_of_u.fear - personality.aggression > -10 && op_of_u.trust > -8 ) {
        set_attitude( NPCATT_TALK );
    } else if( op_of_u.fear - 2 * personality.aggression - personality.bravery < -30 ) {
        set_attitude( NPCATT_KILL );
    } else if( my_fac && my_fac->likes_u() < -10 ) {
        if( is_player_ally() ) {
            mutiny();
        }
        set_attitude( NPCATT_KILL );
    } else {
        set_attitude( NPCATT_FLEE_TEMP );
    }

    add_msg( m_debug, "%s formed an opinion of u: %s", name, npc_attitude_id( attitude ) );
}

float npc::vehicle_danger( int radius ) const
{
    const tripoint_bub_sm from( bub_pos().x() - radius, bub_pos().y() - radius, bub_pos().z() );
    const tripoint_bub_sm to( bub_pos().x() + radius, bub_pos().y() + radius, bub_pos().z() );
    VehicleList vehicles = g->m.get_vehicles( from, to );

    int danger = 0;

    // TODO: check for most dangerous vehicle?
    for( size_t i = 0; i < vehicles.size(); ++i ) {
    const wrapped_vehicle &wrapped_veh = vehicles[i];
        if( wrapped_veh.v->is_moving() ) {
            // FIXME: this can't be the right way to do this
            units::angle facing = wrapped_veh.v->face.dir();

            point a( wrapped_veh.v->bub_ms_location().xy().raw() );
            point b( static_cast<int>( a.x + units::cos( facing ) * radius ),
                     static_cast<int>( a.y + units::sin( facing ) * radius ) );

            // fake size
            /* This will almost certainly give the wrong size/location on customized
             * vehicles. This should just count frames instead. Or actually find the
             * size. */
            vehicle_part *last_part = &wrapped_veh.v->part( 0 );
            // vehicle_part_range is a forward only iterator, see comment in vpart_range.h
            for( const vpart_reference &vpr : wrapped_veh.v->get_all_parts() ) {
                last_part = &vpr.part();
            }
            int size = std::max( last_part->mount.x(), last_part->mount.y() );

            double normal = std::sqrt( static_cast<float>( ( b.x - a.x ) * ( b.x - a.x ) + ( b.y - a.y ) *
                                       ( b.y - a.y ) ) );
            int closest = static_cast<int>( std::abs( ( bub_pos().x() - a.x ) * ( b.y - a.y ) -
                                            ( bub_pos().y() - a.y ) *
                                            ( b.x - a.x ) ) / normal );

            if( size > closest ) {
                danger = i;
            }
        }
    }
    return danger;
}

bool npc::turned_hostile() const
{
    return ( op_of_u.anger >= hostile_anger_level() );
}

int npc::hostile_anger_level() const
{
    return ( 20 + op_of_u.fear - personality.aggression );
}

int npc::assigned_missions_value()
{
    int ret = 0;
    for( auto &m : chatbin.missions_assigned ) {
        ret += m->get_value();
    }
    return ret;
}

std::vector<skill_id> npc::skills_offered_to( const Character &p ) const
{
    std::vector<skill_id> ret;
    for( const auto &pair : *_skills ) {
        const skill_id &id = pair.first;
        if( p.get_skill_level( id ) < pair.second.level() ) {
            ret.push_back( id );
        }
    }
    return ret;
}

int npc::minimum_item_value() const
{
    // TODO: Base on inventory
    int ret = 20;
    ret -= personality.collector;
    return ret;
}

void npc::update_worst_item_value()
{
    worst_item_value = 99999;
    // TODO: Cache this
    int inv_val = inv.worst_item_value( this );
    if( inv_val < worst_item_value ) {
        worst_item_value = inv_val;
    }
}

int npc::value( const item &it ) const
{
    int market_price = it.price( true );
    return value( it, market_price );
}

int npc::value( const item &it, int market_price ) const
{
    if( it.is_dangerous() || ( it.has_flag( flag_BOMB ) && it.is_active() ) || it.made_of( LIQUID ) ) {
    // NPCs won't be interested in buying active explosives or spilled liquids
    return -1000;
}

// faction currency trades at market price
if( my_fac && my_fac->currency() == it.typeId() ) {
    return market_price;
}

int ret = 0;
double weapon_val = npc_ai::weapon_value( *this, it, it.ammo_capacity() )
                    - npc_ai::wielded_value( *this );
if( weapon_val > 0 ) {
    ret += weapon_val;
}

if( it.is_food() ) {
    int comestval = 0;
    if( nutrition_for( it ) > 0 || it.get_comestible()->quench > 0 ) {
            comestval++;
        }
        if( max_stored_kcal() - get_stored_kcal() > 500 ) {
            comestval += ( nutrition_for( it ) +
                           ( max_stored_kcal() - get_stored_kcal() - 500 ) / 10 ) / 6;
        }
        if( get_thirst() > thirst_levels::thirsty ) {
            comestval += ( it.get_comestible()->quench + get_thirst() - thirst_levels::thirsty ) / 4;
        }
        if( comestval > 0 && will_eat( it ).success() ) {
            ret += comestval;
        }
    }

    if( it.is_ammo() ) {
    const ammotype &at = it.ammo_type();
        if( primary_weapon().is_gun() && primary_weapon().ammo_types().contains( at ) ) {
            // TODO: magazines - don't count ammo as usable if the weapon isn't.
            ret += 14;
        }

        bool has_gun_for_ammo = has_item_with( [at]( const item & itm ) {
            // item::ammo_type considers the active gunmod.
            return itm.is_gun() && itm.ammo_types().contains( at );
        } );

        if( has_gun_for_ammo ) {
            // TODO: consider making this cumulative (once was)
            ret += 14;
        }
    }

    if( it.is_book() ) {
    auto &book = *it.type->book;
    ret += book.fun;
    if( book.skill && get_skill_level( book.skill ) < book.level &&
            get_skill_level( book.skill ) >= book.req ) {
            ret += book.level * 3;
        }
    }

    // Practical item value is more important than price
    ret *= 50;

    // TODO: Sometimes we want more than one tool?  Also we don't want EVERY tool.
    if( it.is_tool() && !has_amount( it.typeId(), 1 ) ) {
    ret += market_price * 0.2; // 20% premium for fresh tools
}
ret += market_price;
return ret;
}

bool npc::has_painkiller()
{
    return inv.has_enough_painkiller( get_pain() );
}

bool npc::took_painkiller() const
{
    return ( has_effect( effect_pkill1 ) || has_effect( effect_pkill2 ) ||
    has_effect( effect_pkill3 ) || has_effect( effect_pkill_l ) );
}

int npc::get_faction_ver() const
{
    return faction_api_version;
}

void npc::set_faction_ver( int new_version )
{
    faction_api_version = new_version;
}

bool npc::has_faction_relationship( const Character &p,
                                    const npc_factions::relationship flag ) const
{
    faction *p_fac = p.get_faction();
    if( !my_fac || !p_fac ) {
        return false;
    }

    return my_fac->has_relationship( p_fac->id(), flag );
}

bool npc::is_friendly( const Character &p ) const
{
    return is_ally( p ) || ( p.is_player() && ( is_walking_with() || is_player_ally() ) );
}

bool npc::guaranteed_hostile() const
{
    return is_enemy() || ( my_fac && my_fac->likes_u() < -10 ) || g->u.has_trait( trait_PROF_FERAL );
}

Attitude npc::attitude_to( const Creature &other ) const
{
    if( other.is_npc() || other.is_player() ) {
    const player &guy = dynamic_cast<const player &>( other );
        // check faction relationships first
        const auto *guy_fac = guy.get_faction();
        if( my_fac != nullptr && guy_fac != nullptr ) {
            const auto *rel_data = my_fac->relationship_flags_with( guy_fac->id() );
            if( rel_data != nullptr ) {
                if( rel_data->test( npc_factions::kill_on_sight ) ) {
                    return Attitude::A_HOSTILE;
                } else if( rel_data->test( npc_factions::watch_your_back ) ) {
                    return Attitude::A_FRIENDLY;
                }
            }
        }
    }

    if( is_player_ally() ) {
    // Friendly NPCs share player's alliances
    return g->u.attitude_to( other );
    }

    if( other.is_npc() ) {
    // Hostile NPCs are also hostile towards player's allies
    if( is_enemy() && other.attitude_to( g->u ) == Attitude::A_FRIENDLY ) {
            return Attitude::A_HOSTILE;
        }

        return Attitude::A_NEUTRAL;
    } else if( other.is_player() ) {
    // For now, make it symmetric.
    return other.attitude_to( *this );
    }

    // TODO: Get rid of the ugly cast without duplicating checks
    const monster &m = dynamic_cast<const monster &>( other );
    switch( m.attitude( this ) ) {
    case MATT_FOLLOW:
    case MATT_FPASSIVE:
    case MATT_IGNORE:
    case MATT_FLEE:
        return Attitude::A_NEUTRAL;
    case MATT_FRIEND:
    case MATT_ZLAVE:
        return Attitude::A_FRIENDLY;
    case MATT_ATTACK:
        return Attitude::A_HOSTILE;
    case MATT_NULL:
    case MATT_UNKNOWN:
    case NUM_MONSTER_ATTITUDES:
        break;
}

return Attitude::A_NEUTRAL;
}

float npc::danger_assessment()
{
    return ai_cache.danger_assessment;
}

std::string npc::opinion_text() const
{
    std::string ret;
    if( op_of_u.trust <= -10 ) {
        ret += _( "Completely untrusting" );
    } else if( op_of_u.trust <= -6 ) {
        ret += _( "Very untrusting" );
    } else if( op_of_u.trust <= -3 ) {
        ret += _( "Untrusting" );
    } else if( op_of_u.trust <= 2 ) {
        ret += _( "Uneasy" );
    } else if( op_of_u.trust <= 4 ) {
        ret += _( "Trusting" );
    } else if( op_of_u.trust < 10 ) {
        ret += _( "Very trusting" );
    } else {
        ret += _( "Completely trusting" );
    }

    ret += string_format( _( " (Trust: %d); " ), op_of_u.trust );

    if( op_of_u.fear <= -10 ) {
        ret += _( "Thinks you're laughably harmless" );
    } else if( op_of_u.fear <= -6 ) {
        ret += _( "Thinks you're harmless" );
    } else if( op_of_u.fear <= -3 ) {
        ret += _( "Unafraid" );
    } else if( op_of_u.fear <= 2 ) {
        ret += _( "Wary" );
    } else if( op_of_u.fear <= 5 ) {
        ret += _( "Afraid" );
    } else if( op_of_u.fear < 10 ) {
        ret += _( "Very afraid" );
    } else {
        ret += _( "Terrified" );
    }

    ret += string_format( _( " (Fear: %d); " ), op_of_u.fear );

    if( op_of_u.value <= -10 ) {
        ret += _( "Considers you a major liability" );
    } else if( op_of_u.value <= -6 ) {
        ret += _( "Considers you a burden" );
    } else if( op_of_u.value <= -3 ) {
        ret += _( "Considers you an annoyance" );
    } else if( op_of_u.value <= 2 ) {
        ret += _( "Doesn't care about you" );
    } else if( op_of_u.value <= 5 ) {
        ret += _( "Values your presence" );
    } else if( op_of_u.value < 10 ) {
        ret += _( "Treasures you" );
    } else {
        ret += _( "Best Friends Forever!" );
    }

    ret += string_format( _( " (Value: %d); " ), op_of_u.value );

    if( op_of_u.anger <= -10 ) {
        ret += _( "You can do no wrong!" );
    } else if( op_of_u.anger <= -6 ) {
        ret += _( "You're good people" );
    } else if( op_of_u.anger <= -3 ) {
        ret += _( "Thinks well of you" );
    } else if( op_of_u.anger <= 2 ) {
        ret += _( "Ambivalent" );
    } else if( op_of_u.anger <= 5 ) {
        ret += _( "Pissed off" );
    } else if( op_of_u.anger < 10 ) {
        ret += _( "Angry" );
    } else {
        ret += _( "About to kill you" );
    }

    ret += string_format( _( " (Anger: %d)" ), op_of_u.anger );

    return ret;
}

void npc::die( Creature *nkiller )
{
    if( dead ) {
        // We are already dead, don't die again, note that npc::dead is
        // *only* set to true in this function!
        return;
    }
    // Need to unboard from vehicle before dying, otherwise
    // the vehicle code cannot find us
    if( in_vehicle ) {
        g->m.unboard_vehicle( bub_pos(), true );
    }
    if( is_mounted() ) {
        monster *critter = mounted_creature.get();
        critter->remove_effect( effect_ridden );
        critter->mounted_player = nullptr;
        critter->mounted_player_id = character_id();
    }
    // if this NPC was the only member of a micro-faction, clean it up.
    if( my_fac ) {
        if( !is_fake() && !is_hallucination() ) {
            if( my_fac->members.size() == 1 ) {
                for( auto elem : inv_dump() ) {
                    elem->remove_owner();
                    elem->remove_old_owner();
                }
            }
            my_fac->remove_member( getID() );
        }
    }
    dead = true;
    Character::die( nkiller );

    if( is_hallucination() ) {
        if( g->u.sees( *this ) ) {
            add_msg( _( "%s disappears." ), name.c_str() );
        }
        return;
    }

    if( g->u.sees( *this ) ) {
        add_msg( _( "%s dies!" ), name );
    }

    if( Character *ch = dynamic_cast<Character *>( get_killer() ) ) {
        g->events().send<event_type::character_kills_character>( ch->getID(), getID(), get_name() );
    }

    if( get_killer() == &g->u && ( !guaranteed_hostile() || hit_by_player ) ) {
        bool cannibal = g->u.has_trait( trait_CANNIBAL );
        bool psycho = g->u.has_trait( trait_PSYCHOPATH ) || g->u.has_trait( trait_KILLER );
        if( g->u.has_trait( trait_SAPIOVORE ) || psycho ) {
            // No morale penalty
        } else if( cannibal ) {
            g->u.add_morale( MORALE_KILLED_INNOCENT, -5, 0, 2_days, 3_hours );
        } else {
            g->u.add_morale( MORALE_KILLED_INNOCENT, -100, 0, 2_days, 3_hours );
        }
    }

    if( get_killer() == &g->u && g->u.has_trait( trait_KILLER ) ) {
        const translation snip = SNIPPET.random_from_category( "killer_on_kill" ).value_or( translation() );
        g->u.add_msg_if_player( m_good, "%s", snip );
        g->u.add_morale( MORALE_KILLER_HAS_KILLED, 5, 10, 6_hours, 4_hours );
        g->u.rem_morale( MORALE_KILLER_NEED_TO_KILL );
    }

    if( get_killer() == &g->u && g->u.has_trait( trait_PROF_FERAL ) ) {
        if( !g->u.has_effect( effect_feral_killed_recently ) ) {
            g->u.add_msg_if_player( m_good, _( "The voices in your head quiet down a bit." ) );
        }
        g->u.add_effect( effect_feral_killed_recently, 7_days );
    }
    place_corpse();
}

mfaction_id npc::get_monster_faction() const
{
    if( my_fac && my_fac->mon_faction().is_valid() ) {
    return my_fac->mon_faction();
    }

    // legacy checks
    // Those can't be static int_ids, because mods add factions
    static const string_id<monfaction> human_fac( "human" );
    static const string_id<monfaction> player_fac( "player" );
    static const string_id<monfaction> bee_fac( "bee" );

    if( is_player_ally() ) {
    return player_fac.id();
    }

    if( has_trait( trait_BEE ) ) {
    return bee_fac.id();
    }

    return human_fac.id();
}

void npc::set_companion_mission( npc &p, const std::string &mission_id )
{
    const tripoint_abs_omt omt_pos = p.abs_omt_pos();
    set_companion_mission( omt_pos, p.companion_mission_role_id, mission_id );
}

void npc::set_companion_mission( const tripoint_abs_omt &omt_pos, const std::string &role_id,
                                 const std::string &mission_id )
{
    comp_mission.position = omt_pos;
    comp_mission.mission_id = mission_id;
    comp_mission.role_id = role_id;
}

void npc::set_companion_mission( const tripoint_abs_omt &omt_pos, const std::string &role_id,
                                 const std::string &mission_id, const tripoint_abs_omt &destination )
{
    comp_mission.position = omt_pos;
    comp_mission.mission_id = mission_id;
    comp_mission.role_id = role_id;
    comp_mission.destination = destination;
}

void npc::reset_companion_mission()
{
    comp_mission.position = tripoint_abs_omt( -999, -999, -999 );
    comp_mission.mission_id.clear();
    comp_mission.role_id.clear();
    if( comp_mission.destination ) {
        comp_mission.destination = std::nullopt;
    }
}

bool npc::has_companion_mission() const
{
    return !comp_mission.mission_id.empty();
}

npc_companion_mission npc::get_companion_mission() const
{
    return comp_mission;
}

attitude_group npc::get_attitude_group( npc_attitude att ) const
{
    switch( att ) {
    case NPCATT_MUG:
    case NPCATT_WAIT_FOR_LEAVE:
    case NPCATT_KILL:
        return attitude_group::hostile;
    case NPCATT_FLEE:
    case NPCATT_FLEE_TEMP:
        return attitude_group::fearful;
    case NPCATT_FOLLOW:
    case NPCATT_ACTIVITY:
    case NPCATT_LEAD:
        return attitude_group::friendly;
    default:
        break;
}
return attitude_group::neutral;
}

npc_attitude npc::get_attitude() const
{
    return attitude;
}

void npc::set_attitude( npc_attitude new_attitude )
{
    if( new_attitude == attitude ) {
        return;
    }
    previous_attitude = attitude;
    if( new_attitude == NPCATT_FLEE ) {
        new_attitude = NPCATT_FLEE_TEMP;
    }
    if( new_attitude == NPCATT_FLEE_TEMP && !has_effect( effect_npc_flee_player ) ) {
        add_effect( effect_npc_flee_player, 24_hours, bodypart_str_id::NULL_ID() );
    }

    add_msg( m_debug, "%s changes attitude from %s to %s",
             name, npc_attitude_id( attitude ), npc_attitude_id( new_attitude ) );
    attitude_group new_group = get_attitude_group( new_attitude );
    attitude_group old_group = get_attitude_group( attitude );
    if( new_group != old_group && !is_fake() && g->u.sees( *this ) ) {
        switch( new_group ) {
            case attitude_group::hostile:
                add_msg_if_npc( m_bad, _( "<npcname> gets angry!" ) );
                break;
            case attitude_group::fearful:
                add_msg_if_npc( m_warning, _( "<npcname> gets scared!" ) );
                break;
            default:
                if( old_group == attitude_group::hostile ) {
                    add_msg_if_npc( m_good, _( "<npcname> calms down." ) );
                } else if( old_group == attitude_group::fearful ) {
                    add_msg_if_npc( _( "<npcname> is no longer afraid." ) );
                }
                break;
        }
    }
    attitude = new_attitude;
}

