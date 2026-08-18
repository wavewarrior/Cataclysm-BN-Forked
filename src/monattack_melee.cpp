#include "monattack.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <list>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "ballistics.h"
#include "bionics.h"
#include "bodypart.h"
#include "calendar.h"
#include "character.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "creature.h"
#include "creature_functions.h"
#include "damage.h"
#include "debug.h"
#include "dispersion.h"
#include "effect.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "explosion.h"
#include "field_type.h"
#include "flag.h"
#include "flat_set.h"
#include "fungal_effects.h"
#include "game.h"
#include "game_constants.h"
#include "gun_mode.h"
#include "int_id.h"
#include "item.h"
#include "item_stack.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "line.h"
#include "map.h"
#include "map_iterator.h"
#include "mapdata.h"
#include "martialarts.h"
#include "material.h"
#include "memorial_logger.h"
#include "messages.h"
#include "mondefense.h"
#include "monfaction.h"
#include "monster.h"
#include "morale_types.h"
#include "mtype.h"
#include "name.h"
#include "npc.h"
#include "output.h"
#include "legacy_pathfinding.h"
#include "player.h"
#include "point.h"
#include "projectile.h"
#include "ranged.h"
#include "rng.h"
#include "sounds.h"
#include "speech.h"
#include "string_formatter.h"
#include "tileray.h"
#include "timed_event.h"
#include "translations.h"
#include "type_id.h"
#include "ui.h"
#include "ui_manager.h"
#include "units.h"
#include "value_ptr.h"
#include "weighted_list.h"

static const activity_id ACT_RELOAD( "ACT_RELOAD" );

static const ammo_effect_str_id ammo_effect_NO_OVERSHOOT( "NO_OVERSHOOT" );
static const ammo_effect_str_id ammo_effect_BLINDS_EYES( "BLINDS_EYES" );
static const ammo_effect_str_id ammo_effect_NO_DAMAGE_SCALING( "NO_DAMAGE_SCALING" );
static const ammo_effect_str_id ammo_effect_APPLY_SAP( "APPLY_SAP" );

static const efftype_id effect_ai_controlled( "ai_controlled" );
static const efftype_id effect_assisted( "assisted" );
static const efftype_id effect_attention( "attention" );
static const efftype_id effect_bite( "bite" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_blind( "blind" );
static const efftype_id effect_boomered( "boomered" );
static const efftype_id effect_command_buff( "command_buff" );
static const efftype_id effect_corroding( "corroding" );
static const efftype_id effect_countdown( "countdown" );
static const efftype_id effect_darkness( "darkness" );
static const efftype_id effect_dazed( "dazed" );
static const efftype_id effect_deaf( "deaf" );
static const efftype_id effect_dermatik( "dermatik" );
static const efftype_id effect_downed( "downed" );
static const efftype_id effect_dragging( "dragging" );
static const efftype_id effect_fearparalyze( "fearparalyze" );
static const efftype_id effect_fungus( "fungus" );
static const efftype_id effect_glowing( "glowing" );
static const efftype_id effect_got_checked( "got_checked" );
static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_grown_of_fuse( "grown_of_fuse" );
static const efftype_id effect_has_bag( "has_bag" );
static const efftype_id effect_infected( "infected" );
static const efftype_id effect_laserlocked( "laserlocked" );
static const efftype_id effect_monster_disarmed( "monster_disarmed" );
static const efftype_id effect_onfire( "onfire" );
static const efftype_id effect_operating( "operating" );
static const efftype_id effect_paid( "paid" );
static const efftype_id effect_paralyzepoison( "paralyzepoison" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_raising( "raising" );
static const efftype_id effect_rat( "rat" );
static const efftype_id effect_shrieking( "shrieking" );
static const efftype_id effect_slimed( "slimed" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_targeted( "targeted" );
static const efftype_id effect_under_op( "under_operation" );

static const itype_id itype_ant_egg( "ant_egg" );
static const itype_id itype_badge_cybercop( "badge_cybercop" );
static const itype_id itype_badge_deputy( "badge_deputy" );
static const itype_id itype_badge_detective( "badge_detective" );
static const itype_id itype_badge_doctor( "badge_doctor" );
static const itype_id itype_badge_marshal( "badge_marshal" );
static const itype_id itype_badge_swat( "badge_swat" );
static const itype_id itype_bot_c4_hack( "bot_c4_hack" );
static const itype_id itype_bot_flashbang_hack( "bot_flashbang_hack" );
static const itype_id itype_bot_gasbomb_hack( "bot_gasbomb_hack" );
static const itype_id itype_bot_grenade_hack( "bot_grenade_hack" );
static const itype_id itype_bot_manhack( "bot_manhack" );
static const itype_id itype_bot_mininuke_hack( "bot_mininuke_hack" );
static const itype_id itype_bot_pacification_hack( "bot_pacification_hack" );
static const itype_id itype_c4( "c4" );
static const itype_id itype_c4armed( "c4armed" );
static const itype_id itype_e_handcuffs( "e_handcuffs" );
static const itype_id itype_mininuke( "mininuke" );
static const itype_id itype_mininuke_act( "mininuke_act" );

static const skill_id skill_gun( "gun" );
static const skill_id skill_launcher( "launcher" );
static const skill_id skill_melee( "melee" );
static const skill_id skill_rifle( "rifle" );
static const skill_id skill_unarmed( "unarmed" );

static const species_id species_BLOB( "BLOB" );
static const species_id LEECH_PLANT( "LEECH_PLANT" );
static const species_id ZOMBIE( "ZOMBIE" );

static const std::string flag_AUTODOC_COUCH( "AUTODOC_COUCH" );

static const trait_id trait_ACIDBLOOD( "ACIDBLOOD" );
static const trait_id trait_MARLOSS( "MARLOSS" );
static const trait_id trait_MARLOSS_BLUE( "MARLOSS_BLUE" );
static const trait_id trait_PARAIMMUNE( "PARAIMMUNE" );
static const trait_id trait_PROF_CHURL( "PROF_CHURL" );
static const trait_id trait_PROF_CYBERCO( "PROF_CYBERCO" );
static const trait_id trait_PROF_FED( "PROF_FED" );
static const trait_id trait_PROF_PD_DET( "PROF_PD_DET" );
static const trait_id trait_PROF_POLICE( "PROF_POLICE" );
static const trait_id trait_PROF_SWAT( "PROF_SWAT" );
static const trait_id trait_TAIL_CATTLE( "TAIL_CATTLE" );
static const trait_id trait_THRESH_MARLOSS( "THRESH_MARLOSS" );
static const trait_id trait_THRESH_MYCUS( "THRESH_MYCUS" );

static const mtype_id mon_ant_acid_larva( "mon_ant_acid_larva" );
static const mtype_id mon_ant_acid_queen( "mon_ant_acid_queen" );
static const mtype_id mon_ant_larva( "mon_ant_larva" );
static const mtype_id mon_biollante( "mon_biollante" );
static const mtype_id mon_blob( "mon_blob" );
static const mtype_id mon_blob_brain( "mon_blob_brain" );
static const mtype_id mon_blob_large( "mon_blob_large" );
static const mtype_id mon_blob_small( "mon_blob_small" );
static const mtype_id mon_breather( "mon_breather" );
static const mtype_id mon_breather_hub( "mon_breather_hub" );
static const mtype_id mon_creeper_hub( "mon_creeper_hub" );
static const mtype_id mon_creeper_vine( "mon_creeper_vine" );
static const mtype_id mon_defective_robot_nurse( "mon_nursebot_defective" );
static const mtype_id mon_dermatik( "mon_dermatik" );
static const mtype_id mon_fungal_hedgerow( "mon_fungal_hedgerow" );
static const mtype_id mon_fungal_tendril( "mon_fungal_tendril" );
static const mtype_id mon_fungal_wall( "mon_fungal_wall" );
static const mtype_id mon_fungaloid( "mon_fungaloid" );
static const mtype_id mon_fungaloid_young( "mon_fungaloid_young" );
static const mtype_id mon_headless_dog_thing( "mon_headless_dog_thing" );
static const mtype_id mon_hound_tindalos_afterimage( "mon_hound_tindalos_afterimage" );
static const mtype_id mon_leech_blossom( "mon_leech_blossom" );
static const mtype_id mon_leech_root_drone( "mon_leech_root_drone" );
static const mtype_id mon_leech_root_runner( "mon_leech_root_runner" );
static const mtype_id mon_leech_stalk( "mon_leech_stalk" );
static const mtype_id mon_manhack( "mon_manhack" );
static const mtype_id mon_shadow( "mon_shadow" );
static const mtype_id mon_triffid( "mon_triffid" );
static const mtype_id mon_turret_searchlight( "mon_turret_searchlight" );
static const mtype_id mon_zombie_dancer( "mon_zombie_dancer" );
static const mtype_id mon_zombie_gasbag_crawler( "mon_zombie_gasbag_crawler" );
static const mtype_id mon_zombie_gasbag_impaler( "mon_zombie_gasbag_impaler" );
static const mtype_id mon_zombie_jackson( "mon_zombie_jackson" );
static const mtype_id mon_zombie_skeltal_minion( "mon_zombie_skeltal_minion" );

static const bionic_id bio_uncanny_dodge( "bio_uncanny_dodge" );

// shared utility functions
static bool within_visual_range( monster *z, int max_range )
{
    return !( rl_dist( z->bub_pos(), g->u.bub_pos() ) > max_range || !z->sees( g->u ) );
}

static bool within_target_range( const monster *const z, const Creature *const target, int range )
{
    return target != nullptr &&
           rl_dist( z->bub_pos(), target->bub_pos() ) <= range &&
           z->sees( *target );
}

static Creature *sting_get_target( monster *z, float range = 5.0f )
{
    Creature *target = z->attack_target();

    if( target == nullptr ) {
        return nullptr;
    }

    // Can't see/reach target, no attack
    if( !z->sees( *target ) ||
        !g->m.clear_path( z->bub_pos(), target->bub_pos(), range, 1, 100 ) ) {
        return nullptr;
    }

    return rl_dist( z->bub_pos(), target->bub_pos() ) <= range ? target : nullptr;
}

static bool sting_shoot( monster *z, Creature *target, damage_instance &dam, float range )
{
    if( target->uncanny_dodge() ) {
        target->add_msg_if_player( m_bad, _( "The %s shoots a dart but you dodge it." ),
                                   z->name() );
        return false;
    }

    projectile proj;
    proj.speed = 10;
    proj.range = range;
    proj.impact.add( dam );
    proj.add_effect( ammo_effect_NO_OVERSHOOT );

    dealt_projectile_attack atk = projectile_attack( proj, z->bub_pos(), target->bub_pos(),
                                  dispersion_sources{ 500 }, z );
    if( atk.dealt_dam.total_damage() > 0 ) {
        target->add_msg_if_player( m_bad, _( "The %s shoots a dart into you!" ), z->name() );
        return true;
    } else {
        if( atk.missed_by == 1 ) {
            target->add_msg_if_player( m_good,
                                       _( "The %s shoots a dart at you, but misses!" ),
                                       z->name() );
        } else {
            target->add_msg_if_player( m_good,
                                       _( "The %s shoots a dart but it bounces off your armor." ),
                                       z->name() );
        }
        return false;
    }
}

// Distance == 1 and on the same z-level or with a clear shot up/down.
// If allow_zlev is false, don't allow attacking up/down at all.
// If allow_zlev is true, also allow distance == 1 and on different z-level
// as long as floor/ceiling doesn't exist.
static bool is_adjacent( const monster *z, const Creature *target, const bool allow_zlev )
{
    if( target == nullptr ) {
        return false;
    }

    if( rl_dist( z->bub_pos(), target->bub_pos() ) != 1 ) {
        return false;
    }

    if( !z->can_squeeze_to( target->bub_pos() ) ) {
        return false;
    }

    if( z->bub_pos().z() == target->bub_pos().z() ) {
        return true;
    }

    if( !allow_zlev ) {
        return false;
    }

    // The square above must have no floor (currently only open air).
    // The square below must have no ceiling (i.e. be outside).
    const bool target_above = target->bub_pos().z() > z->bub_pos().z();
    const auto &up   = target_above ? target->bub_pos() : z->bub_pos();
    const auto &down = target_above ? z->bub_pos() : target->bub_pos();
    return g->m.ter( up ) == t_open_air && g->m.is_outside( down );
}

static std::unique_ptr<npc> make_fake_npc( monster *z, int str, int dex, int inte, int per )
{
    std::unique_ptr<npc> tmp = std::make_unique<npc>();
    tmp->name = _( "The " ) + z->name();
    tmp->set_fake( true );
    tmp->recoil = 0;
    tmp->setpos( z->bub_pos() );
    tmp->str_cur = str;
    tmp->dex_cur = dex;
    tmp->int_cur = inte;
    tmp->per_cur = per;
    if( z->friendly != 0 ) {
        tmp->set_attitude( NPCATT_FOLLOW );
        tmp->set_fac( faction_id( "your_followers" ) );
    } else {
        tmp->set_attitude( NPCATT_KILL );
    }
    return tmp;
}


template <size_t N = 1>
std::pair < std::array < tripoint_bub_ms, ( 2 * N + 1 ) * ( 2 * N + 1 ) >, size_t >
find_empty_neighbors( const tripoint_bub_ms &origin )
{
    constexpr auto r = static_cast<int>( N );
    std::pair < std::array < tripoint_bub_ms, ( 2 * N + 1 )*( 2 * N + 1 ) >, size_t > result;
    for( const tripoint_bub_ms &tmp : g->m.points_in_radius( origin, r ) ) {
        if( g->is_empty( tmp ) ) {
            result.first[result.second++] = tmp;
        }
    }
    return result;
}

template <size_t N = 1>
std::pair < std::array < tripoint_bub_ms, ( 2 * N + 1 ) * ( 2 * N + 1 ) >, size_t >
find_empty_neighbors( const Creature &c )
{
    return find_empty_neighbors<N>( c.bub_pos() );
}

static size_t get_random_index( const size_t size )
{
    return static_cast<size_t>( rng( 0, static_cast<int>( size - 1 ) ) );
}

template <typename Container>
size_t get_random_index( const Container &c )
{
    return get_random_index( c.size() );
}

bool mattack::stretch_bite( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    // Let it be used on non-player creatures
    // can be used at close range too!
    Creature *target = z->attack_target();
    if( target == nullptr || rl_dist( z->bub_pos(), target->bub_pos() ) > 3 || !z->sees( *target ) ) {
        return false;
    }

    z->moves -= 150;

    auto prev_point = z->bub_pos();
    bool obstructed = false;
    for( auto &pnt : g->m.find_clear_path( z->bub_pos(), target->bub_pos() ) ) {

        if( get_map().obstructed_by_vehicle_rotation( prev_point, pnt ) ) {
            if( one_in( 2 ) ) {
                pnt.x() = prev_point.x();
            } else {
                pnt.y() = prev_point.y();
            }
            obstructed = true;
        }

        if( obstructed || g->m.impassable( pnt ) ) {
            z->add_effect( effect_stunned, 6_turns );
            target->add_msg_player_or_npc( _( "The %1$s stretches its head at you, but bounces off the %2$s" ),
                                           _( "The %1$s stretches its head at <npcname>, but bounces off the %2$s" ),
                                           z->name(), g->m.obstacle_name( pnt ) );
            return true;
        }
        prev_point = pnt;
    }
    bool uncanny = target->uncanny_dodge();
    // Can we dodge the attack? Uses player dodge function % chance (melee.cpp)
    if( uncanny || dodge_check( z, target ) ) {
        z->moves -= 150;
        z->add_effect( effect_stunned, 3_turns );
        auto msg_type = target == &g->u ? m_warning : m_info;
        target->add_msg_player_or_npc( msg_type,
                                       _( "The %s's head extends to bite you, but you dodge and the head sails past!" ),
                                       _( "The %s's head extends to bite <npcname>, but they dodge and the head sails past!" ),
                                       z->name() );
        if( !uncanny ) {
            target->on_dodge( z, z->type->melee_skill * 2 );
        }
        return true;
    }

    const bodypart_id hit = target->get_random_body_part();
    const body_part hit_token = hit->token;
    // More damage due to the speed of the moving head
    int dam = rng( 5, 15 );
    dam = target->deal_damage( z, hit, damage_instance( DT_STAB, dam ) ).total_damage();

    if( dam > 0 ) {
        auto msg_type = target == &g->u ? m_bad : m_info;
        target->add_msg_player_or_npc( msg_type,
                                       //~ 1$s is monster name, 2$s bodypart in accusative
                                       _( "The %1$s's teeth sink into your %2$s!" ),
                                       //~ 1$s is monster name, 2$s bodypart in accusative
                                       _( "The %1$s's teeth sink into <npcname>'s %2$s!" ),
                                       z->name(),
                                       body_part_name_accusative( hit_token ) );

        if( one_in( 16 - dam ) ) {
            if( target->has_effect( effect_bite, hit.id() ) ) {
                target->add_effect( effect_bite, 40_minutes, hit.id() );
            } else if( target->has_effect( effect_infected, hit.id() ) ) {
                target->add_effect( effect_infected, 25_minutes, hit.id() );
            } else {
                target->add_effect( effect_bite, 1_turns, hit.id() );
            }
        }
    } else {
        target->add_msg_player_or_npc( _( "The %1$s's head hits your %2$s, but glances off your armor!" ),
                                       _( "The %1$s's head hits <npcname>'s %2$s, but glances off armor!" ),
                                       z->name(),
                                       body_part_name_accusative( hit_token ) );
    }

    target->on_hit( z, hit );

    return true;
}

bool mattack::brandish( monster *z )
{
    if( z->type->monster_weapon && z->has_effect( effect_monster_disarmed ) ) {
        return false;
    }
    if( z->friendly ) {
        // TODO: handle friendly monsters
        return false;
    }
    // Only brandish if we can see you!
    if( !z->sees( g->u ) ) {
        return false;
    }
    add_msg( m_warning, _( "He's brandishing a knife!" ) );
    add_msg( _( "Quiet, quiet" ) );

    return true;
}

bool mattack::flesh_golem( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }

    int dist = rl_dist( z->bub_pos(), target->bub_pos() );
    if( dist > 20 ||
        !z->sees( *target ) ) {
        return false;
    }

    if( dist > 1 ) {
        if( one_in( 12 ) ) {
            z->moves -= 200;
            // It doesn't "nearly deafen you" when it roars from the other side of bubble
            sound_event se;
            se.origin = z->bub_pos();
            se.volume = 120;
            se.category = sounds::sound_t::alert;
            se.description = _( "a terrifying roar!" );
            se.from_monster = true;
            se.monfaction = z->faction.id();
            se.id = "shout";
            se.variant = "roar";
            sounds::sound( se );
            return true;
        }
        return false;
    }
    if( !is_adjacent( z, target, true ) ) {
        // No attacking through floor, even if we can see the target somehow
        return false;
    }
    if( g->u.sees( *z ) ) {
        add_msg( _( "%1$s swings a massive claw at %2$s!" ),
                 z->disp_name( false, true ), target->disp_name() );
    }
    z->moves -= 100;

    if( target->uncanny_dodge() ) {
        return true;
    }

    // Can we dodge the attack? Uses player dodge function % chance (melee.cpp)
    if( dodge_check( z, target ) ) {
        target->add_msg_player_or_npc( _( "You dodge it!" ),
                                       _( "<npcname> dodges it!" ) );
        target->on_dodge( z, z->type->melee_skill * 2 );
        return true;
    }
    const bodypart_id hit = target->get_random_body_part();
    // TODO: 10 bashing damage doesn't sound like a "massive claw" but a mediocre punch
    int dam = rng( 5, 10 );
    target->deal_damage( z, hit, damage_instance( DT_BASH, dam ) );
    if( one_in( 6 ) ) {
        target->add_effect( effect_downed, 3_minutes );
    }

    //~ 1$s is bodypart name, 2$d is damage value.
    target->add_msg_if_player( m_bad, _( "Your %1$s is battered for %2$d damage!" ),
                               body_part_name( hit->token ), dam );
    target->on_hit( z, hit );

    return true;
}

bool mattack::absorb_meat( monster *z )
{
    //Absorb no more than 1/10th monster's volume, times the volume of a meat chunk
    const int monster_volume = units::to_liter( z->get_volume() );
    const float average_meat_chunk_volume = 0.5;
    // TODO: dynamically get volume of meat
    const int max_meat_absorbed = monster_volume / 10.0 * average_meat_chunk_volume;
    //For every milliliter of meat absorbed, heal this many HP
    const float meat_absorption_factor = 0.01;
    //Search surrounding tiles for meat
    for( const auto &p : g->m.points_in_radius( z->bub_pos(), 1 ) ) {
        auto items = g->m.i_at( p );
        for( auto &current_item : items ) {
            const material_id current_item_material = current_item->get_base_material().ident();
            if( current_item_material == material_id( "flesh" ) ||
                current_item_material == material_id( "hflesh" ) ) {
                //We have something meaty! Calculate how much it will heal the monster
                const int ml_of_meat = units::to_milliliter<int>( current_item->volume() );
                const int total_charges = current_item->count();
                const int ml_per_charge = ml_of_meat / total_charges;
                //We have a max size of meat here to avoid absorbing whole corpses.
                if( ml_per_charge > max_meat_absorbed * 1000 ) {
                    add_msg( m_info, _( "The %1$s quivers hungrily in the direction of the %2$s." ), z->name(),
                             current_item->tname() );
                    return false;
                }
                if( current_item->count_by_charges() ) {
                    //Choose a random amount of meat charges to absorb
                    int meat_absorbed = std::min( max_meat_absorbed, rng( 1, total_charges ) );
                    const int hp_to_heal = meat_absorbed * ml_per_charge * meat_absorption_factor;
                    z->heal( hp_to_heal, true );
                    g->m.use_charges( p, 0, current_item->type->get_id(), meat_absorbed );
                } else {
                    //Only absorb one meaty item
                    int meat_absorbed = 1;
                    const int hp_to_heal = meat_absorbed * ml_per_charge * meat_absorption_factor;
                    z->heal( hp_to_heal, true );
                    g->m.use_amount( p, 0, current_item->type->get_id(), meat_absorbed );
                }
                if( g->u.sees( *z ) ) {
                    add_msg( m_warning, _( "The %1$s absorbs the %2$s, growing larger." ), z->name(),
                             current_item->tname() );
                    add_msg( m_debug, "The %1$s now has %2$s out of %3$s hp", z->name(), z->get_hp(),
                             z->get_hp_max() );
                }
                return true;
            }
        }
    }
    return false;
}

bool mattack::lunge( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }

    int dist = rl_dist( z->bub_pos(), target->bub_pos() );
    if( dist > 20 ||
        !z->sees( *target ) ) {
        return false;
    }

    bool seen = g->u.sees( *z );
    if( dist > 1 ) {
        if( one_in( 5 ) ) {
            // Out of range
            if( dist > 4 || !z->sees( *target ) ) {
                return false;
            }
            z->moves += 200;
            if( seen ) {
                add_msg( _( "%1$s lunges for %2$s!" ), z->disp_name( false, true ), target->disp_name() );
            }
            return true;
        }
        return false;
    }

    if( !is_adjacent( z, target, false ) ) {
        // No attacking up or down - lunging requires contact
        // There could be a lunge down attack, though
        return false;
    }

    z->moves -= 100;

    if( target->uncanny_dodge() ) {
        return true;
    }

    // Can we dodge the attack? Uses player dodge function % chance (melee.cpp)
    if( dodge_check( z, target ) ) {
        target->add_msg_player_or_npc( _( "The %1$s lunges at you, but you sidestep it!" ),
                                       _( "The %1$s lunges at <npcname>, but they sidestep it!" ), z->name() );
        target->on_dodge( z, z->type->melee_skill * 2 );
        return true;
    }
    const bodypart_id hit = target->get_random_body_part();
    int dam = rng( 3, 7 );
    dam = target->deal_damage( z, hit, damage_instance( DT_BASH, dam ) ).total_damage();
    if( dam > 0 ) {
        auto msg_type = target == &g->u ? m_bad : m_warning;
        target->add_msg_player_or_npc( msg_type,
                                       _( "The %1$s lunges at your %2$s, battering it for %3$d damage!" ),
                                       _( "The %1$s lunges at <npcname>'s %2$s, battering it for %3$d damage!" ),
                                       z->name(), body_part_name( hit->token ), dam );
    } else {
        target->add_msg_player_or_npc( _( "The %1$s lunges at your %2$s, but your armor prevents injury!" ),
                                       _( "The %1$s lunges at <npcname>'s %2$s, but their armor prevents injury!" ),
                                       z->name(),
                                       body_part_name_accusative( hit->token ) );
    }
    if( one_in( 6 ) ) {
        target->add_effect( effect_downed, 3_turns );
    }
    target->on_hit( z, hit );
    target->check_dead_state();
    return true;
}

bool mattack::longswipe( monster *z )
{
    if( z->friendly ) {
        // TODO: handle friendly monsters
        return false;
    }
    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }
    // Out of range
    if( rl_dist( z->bub_pos(), target->bub_pos() ) > 3 || !z->sees( *target ) ) {
        return false;
    }
    map &here = get_map();
    //Is there something impassable blocking the claw?
    auto prev_point = z->bub_pos();
    bool obstructed = false;
    for( tripoint_bub_ms &pnt : g->m.find_clear_path( z->bub_pos(), target->bub_pos() ) ) {

        if( here.obstructed_by_vehicle_rotation( prev_point, pnt ) ) {
            if( one_in( 2 ) ) {
                pnt.x() = prev_point.x();
            } else {
                pnt.y() = prev_point.y();
            }
            obstructed = true;
        }

        if( obstructed || here.impassable( pnt ) ) {
            //If we're here, it's an nonadjacent attack, which is only attempted 1/5 of the time.
            if( !one_in( 5 ) ) {
                return false;
            }
            target->add_msg_player_or_npc( _( "The %1$s thrusts a claw at you, but it bounces off the %2$s!" ),
                                           _( "The %1$s thrusts a claw at <npcname>, but it bounces off the %2$s!" ),
                                           z->name(), here.obstacle_name( pnt ) );
            z->mod_moves( -150 );
            return true;
        }
        prev_point = pnt;
    }

    if( !is_adjacent( z, target, true ) ) {
        if( one_in( 5 ) ) {

            z->moves -= 150;

            if( target->uncanny_dodge() ) {
                return true;
            }
            // Can we dodge the attack? Uses player dodge function % chance (melee.cpp)
            if( dodge_check( z, target ) ) {
                target->add_msg_player_or_npc( _( "The %s thrusts a claw at you, but you evade it!" ),
                                               _( "The %s thrusts a claw at <npcname>, but they evade it!" ),
                                               z->name() );
                target->on_dodge( z, z->type->melee_skill * 2 );
                return true;
            }
            const bodypart_id hit = target->get_random_body_part();
            int dam = rng( 3, 7 );
            dam = target->deal_damage( z, hit, damage_instance( DT_CUT, dam ) ).total_damage();
            if( dam > 0 ) {
                auto msg_type = target == &g->u ? m_bad : m_warning;
                target->add_msg_player_or_npc( msg_type,
                                               //~ 1$s is bodypart name, 2$d is damage value.
                                               _( "The %1$s thrusts a claw at your %2$s, slashing it for %3$d damage!" ),
                                               //~ 1$s is bodypart name, 2$d is damage value.
                                               _( "The %1$s thrusts a claw at <npcname>'s %2$s, slashing it for %3$d damage!" ),
                                               z->name(), body_part_name( hit->token ), dam );
            } else {
                target->add_msg_player_or_npc(
                    _( "The %1$s thrusts a claw at your %2$s, but glances off your armor!" ),
                    _( "The %1$s thrusts a claw at <npcname>'s %2$s, but glances off armor!" ),
                    z->name(),
                    body_part_name_accusative( hit->token ) );
            }
            target->on_hit( z, hit );
            return true;
        }
        return false;
    }
    z->moves -= 100;

    if( target->uncanny_dodge() ) {
        return true;
    }

    // Can we dodge the attack? Uses player dodge function % chance (melee.cpp)
    if( dodge_check( z, target ) ) {
        target->add_msg_player_or_npc( _( "The %s slashes at your neck!  You duck!" ),
                                       _( "The %s slashes at <npcname>'s neck!  They duck!" ), z->name() );
        target->on_dodge( z, z->type->melee_skill * 2 );
        return true;
    }

    int dam = rng( 6, 10 );
    dam = target->deal_damage( z, bodypart_id( "head" ), damage_instance( DT_CUT,
                               dam ) ).total_damage();
    if( dam > 0 ) {
        auto msg_type = target == &g->u ? m_bad : m_warning;
        target->add_msg_player_or_npc( msg_type,
                                       _( "The %1$s slashes at your neck, cutting your throat for %2$d damage!" ),
                                       _( "The %1$s slashes at <npcname>'s neck, cutting their throat for %2$d damage!" ),
                                       z->name(), dam );
        target->add_effect( effect_bleed, 10_minutes, body_part_head );
    } else {
        target->add_msg_player_or_npc( _( "The %1$s slashes at your %2$s, but glances off your armor!" ),
                                       _( "The %1$s slashes at <npcname>'s %2$s, but glances off armor!" ),
                                       z->name(),
                                       body_part_name_accusative( bp_head ) );
    }
    target->on_hit( z, bodypart_id( "head" ) );
    target->check_dead_state();

    return true;
}

static void parrot_common( monster *parrot )
{
    const SpeechBubble &speech = get_speech( parrot->type->id.str() );
    sound_event se;
    se.origin = parrot->bub_pos();
    se.volume = speech.volume;
    se.category = sounds::sound_t::speech;
    se.description = speech.text.translated();
    se.from_monster = true;
    se.monfaction = parrot->faction.id();
    se.id = "speech";
    se.variant = parrot->type->id.str();
    sounds::sound( se );
}

bool mattack::parrot( monster *z )
{
    if( z->has_effect( effect_shrieking ) ) {
        sound_event se;
        se.origin = z->bub_pos();
        se.volume = 120;
        se.category = sounds::sound_t::alert;
        se.description = _( "a piercing wail!" );
        se.from_monster = true;
        se.monfaction = z->faction.id();
        se.id = "shout";
        se.variant = "wail";
        sounds::sound( se );
        z->moves -= 40;
        return false;
    } else if( one_in( 20 ) ) {
        parrot_common( z );
        return true;
    }

    return false;
}

bool mattack::parrot_at_danger( monster *parrot )
{

    Creature *target = parrot->attack_target();
    if( one_in( 20 ) && target != nullptr && parrot->sees( *target ) ) {
        parrot_common( parrot );
        return true;
    }

    return false;
}

bool mattack::darkman( monster *z )
{
    if( z->friendly ) {
        // TODO: handle friendly monsters
        return false;
    }
    // Wont do stuff unless it can see you and is in range
    if( rl_dist( z->bub_pos(), g->u.bub_pos() ) > 40 ) {
        return false;
    }
    if( !z->sees( g->u ) ) {
        return true;
    }
    if( monster *const shadow = g->place_critter_around( mon_shadow, z->bub_pos(), 1 ) ) {
        z->moves -= 10;
        shadow->make_ally( *z );
        if( g->u.sees( *z ) ) {
            add_msg( m_warning, _( "A shadow splits from the %s!" ),
                     z->name() );
        }
    }
    // What do we say?
    switch( rng( 1, 7 ) ) {
        case 1:
            add_msg( _( "\"Stop it please\"" ) );
            break;
        case 2:
            add_msg( _( "\"Let us help you\"" ) );
            break;
        case 3:
            add_msg( _( "\"We wish you no harm\"" ) );
            break;
        case 4:
            add_msg( _( "\"Do not fear\"" ) );
            break;
        case 5:
            add_msg( _( "\"We can help you\"" ) );
            break;
        case 6:
            add_msg( _( "\"We are friendly\"" ) );
            break;
        case 7:
            add_msg( _( "\"Please dont\"" ) );
            break;
    }
    g->u.add_effect( effect_darkness, 1_turns, bodypart_str_id::NULL_ID() );

    return true;
}

bool mattack::slimespring( monster *z )
{
    if( rl_dist( z->bub_pos(), g->u.bub_pos() ) > 30 ) {
        return false;
    }

    // This morale buff effect could get spammy
    if( g->u.get_morale_level() <= 1 ) {
        switch( rng( 1, 3 ) ) {
            case 1:
                //~ Your slimes try to cheer you up!
                //~ Lowercase is intended: they're small voices.
                add_msg( m_good, _( "\"hey, it's gonna be all right!\"" ) );
                g->u.add_morale( MORALE_SUPPORT, 10, 50 );
                break;
            case 2:
                //~ Your slimes try to cheer you up!
                //~ Lowercase is intended: they're small voices.
                add_msg( m_good, _( "\"we'll get through this!\"" ) );
                g->u.add_morale( MORALE_SUPPORT, 10, 50 );
                break;
            case 3:
                //~ Your slimes try to cheer you up!
                //~ Lowercase is intended: they're small voices.
                add_msg( m_good, _( "\"i'm here for you!\"" ) );
                g->u.add_morale( MORALE_SUPPORT, 10, 50 );
                break;
        }
    }
    if( rl_dist( z->bub_pos(), g->u.bub_pos() ) <= 3 && z->sees( g->u ) ) {
        if( ( g->u.has_effect( effect_bleed ) ) || ( g->u.has_effect( effect_bite ) ) ) {
            //~ Lowercase is intended: they're small voices.
            add_msg( _( "\"let me help!\"" ) );
            // Yes, your slimespring(s) handle/don't all Bad Damage at the same time.
            if( g->u.has_effect( effect_bite ) ) {
                if( one_in( 3 ) ) {
                    g->u.remove_effect( effect_bite );
                    add_msg( m_good, _( "The slime cleans you out!" ) );
                } else {
                    add_msg( _( "The slime flows over you, but your gouges still ache." ) );
                }
            }
            if( g->u.has_effect( effect_bleed ) ) {
                if( one_in( 2 ) ) {
                    g->u.remove_effect( effect_bleed );
                    add_msg( m_good, _( "The slime seals up your leaks!" ) );
                } else {
                    add_msg( _( "The slime flows over you, but your fluids are still leaking." ) );
                }
            }
        }
    }

    return true;
}

bool mattack::thrown_by_judo( monster *z )
{
    Creature *target = z->attack_target();
    if( target == nullptr ||
        !is_adjacent( z, target, false ) ||
        !z->sees( *target ) ) {
        return false;
    }

    player *foe = dynamic_cast< player * >( target );
    if( foe == nullptr ) {
        // No mons for now
        return false;
    }
    // "Wimpy" Judo is about to pay off... :D
    if( foe->is_throw_immune() ) {
        // DX + Unarmed
        ///\EFFECT_DEX increases chance judo-throwing a monster

        ///\EFFECT_UNARMED increases chance of judo-throwing monster, vs their melee skill
        if( ( ( foe->dex_cur + foe->get_skill_level( skill_unarmed ) ) > ( z->type->melee_skill + rng( 0,
                3 ) ) ) ) {
            target->add_msg_if_player( m_good, _( "but you grab its arm and flip it to the ground!" ) );

            // most of the time, when not isolated
            if( !one_in( 4 ) && !target->is_elec_immune() && z->type->sp_defense == &mdefense::zapback ) {
                // If it all pans out, we're zap the player's arm as he flips the monster.
                target->add_msg_if_player( _( "The flip does shock you…" ) );
                // Discounted electric damage for quick flip
                damage_instance shock;
                shock.add_damage( DT_ELECTRIC, rng( 1, 3 ) );
                foe->deal_damage( z, bodypart_id( "arm_l" ), shock );
                foe->deal_damage( z, bodypart_id( "arm_r" ), shock );
                foe->check_dead_state();
            }
            // Monster is down,
            z->add_effect( effect_downed, 5_turns );
            const int min_damage = 10 + foe->get_skill_level( skill_unarmed );
            const int max_damage = 20 + foe->get_skill_level( skill_unarmed );
            // Deal moderate damage
            const auto damage = rng( min_damage, max_damage );
            z->apply_damage( foe, bodypart_id( "torso" ), damage );
            z->check_dead_state();
        } else {
            // Still avoids the major hit!
            target->add_msg_if_player( _( "but you deftly spin out of its grasp!" ) );
        }
        return true;
    } else {
        return false;
    }
}

bool mattack::riotbot( monster *z )
{
    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }

    map &here = get_map();

    player *foe = dynamic_cast<player *>( target );

    if( calendar::once_every( 1_minutes ) ) {
        for( const tripoint_bub_ms &dest : here.points_in_radius( z->bub_pos(), 4 ) ) {
            if( here.passable( dest ) &&
                here.clear_path( z->bub_pos(), dest, 3, 1, 100 ) ) {
                here.add_field( dest, fd_relax_gas, rng( 1, 3 ) );
            }
        }
    }
    sound_event se;
    se.origin = z->bub_pos();
    se.from_monster = true;
    se.monfaction = z->faction.id();
    //already arrested?
    //and yes, if the player has no hands, we are not going to arrest him.
    if( foe != nullptr &&
        ( foe->primary_weapon().typeId() == itype_e_handcuffs || !foe->has_two_arms() ) ) {
        z->anger = 0;

        if( calendar::once_every( 25_turns ) ) {
            se.volume = 70;
            se.category = sounds::sound_t::electronic_speech;
            se.description = _( "Halt and submit to arrest, citizen!  The police will be here any moment." );
            se.id = "speech";
            se.variant = z->type->id.str();

            sounds::sound( se );
        }

        return true;
    }

    if( z->anger < z->type->agro ) {
        z->anger += z->type->agro / 20;
        return true;
    }

    const int dist = rl_dist( z->bub_pos(), target->bub_pos() );

    //we need empty hands to arrest
    if( foe == &g->u && !foe->is_armed() ) {

        se.volume = 70;
        se.category = sounds::sound_t::electronic_speech;
        se.description = _( "Please stay in place, citizen, do not make any movements!" );
        se.id = "speech";
        se.variant = z->type->id.str();
        sounds::sound( se );

        //we need to come closer and arrest
        if( !is_adjacent( z, foe, false ) ) {
            return true;
        }

        //Strain the atmosphere, forcing the player to wait. Let him feel the power of law!
        if( !one_in( 10 ) ) {
            foe->add_msg_player_or_npc( _( "The robot carefully scans you." ),
                                        _( "The robot carefully scans <npcname>." ) );
            return true;
        }

        enum {ur_arrest, ur_resist, ur_trick};

        //arrest!
        uilist amenu;
        amenu.allow_cancel = false;
        amenu.text = _( "The riotbot orders you to present your hands and be cuffed." );

        amenu.addentry( ur_arrest, true, 'a', _( "Allow yourself to be arrested." ) );
        amenu.addentry( ur_resist, true, 'r', _( "Resist arrest!" ) );
        ///\EFFECT_INT >10 allows and increases chance whether you can feign death to avoid riot bot arrest
        if( foe->int_cur > 12 || ( foe->int_cur > 10 && !one_in( foe->int_cur - 8 ) ) ) {
            amenu.addentry( ur_trick, true, 't', _( "Feign death." ) );
        }

        amenu.query();
        const int choice = amenu.ret;

        if( choice == ur_arrest ) {
            z->anger = 0;

            detached_ptr<item> handcuffs = item::spawn( "e_handcuffs", calendar::start_of_cataclysm );
            handcuffs->charges = handcuffs->type->maximum_charges();
            handcuffs->activate();
            handcuffs->set_var( "HANDCUFFS_X", foe->bub_pos().x() );
            handcuffs->set_var( "HANDCUFFS_Y", foe->bub_pos().y() );

            const bool is_uncanny = foe->has_active_bionic( bio_uncanny_dodge ) &&
                                    foe->get_power_level() > bio_uncanny_dodge.obj().power_trigger &&
                                    !one_in( 3 );
            ///\EFFECT_DEX >13 allows and increases chance to slip out of riot bot handcuffs
            const bool is_dex = foe->dex_cur > 13 && !one_in( foe->dex_cur - 11 );

            if( is_uncanny || is_dex ) {

                if( is_uncanny ) {
                    foe->mod_power_level( -bio_uncanny_dodge->power_trigger );
                }

                add_msg( m_good,
                         _( "You deftly slip out of the handcuffs just as the robot closes them.  The robot didn't seem to notice!" ) );
                foe->i_add( std::move( handcuffs ) );
            } else {
                handcuffs->set_flag( flag_NO_UNWIELD );
                item &as_obj = *handcuffs;
                foe->i_add( std::move( handcuffs ) );
                foe->wield( as_obj );
                foe->moves -= 300;
                add_msg( _( "The robot puts handcuffs on you." ) );
            }

            se.volume = 60;
            se.category = sounds::sound_t::electronic_speech;
            // Casting out a a whole bunch of sounds in sequence is less desireable than just one sound with a long description.
            se.description =
                _( "You are under arrest, citizen.  You have the right to remain silent.  If you do not remain silent, anything you say may be used against you in a court of law. You have the right to an attorney.  If you cannot afford an attorney, one will be provided at no cost to you.  You may have your attorney present during any questioning. If you do not understand these rights, an officer will explain them in greater detail when taking you into custody. Do not attempt to flee or to remove the handcuffs, citizen.  That can be dangerous to your health." );

            se.id = "speech";
            se.variant = z->type->id.str();
            sounds::sound( se );

            z->moves -= 300;

            return true;
        }

        bool bad_trick = false;

        if( choice == ur_trick ) {

            ///\EFFECT_INT >10 allows and increases chance of successful feign death against riot bot
            if( !one_in( foe->int_cur - 10 ) ) {

                add_msg( m_good,
                         _( "You fall to the ground and feign a sudden convulsive attack.  Though you're obviously still alive, the riotbot cannot tell the difference between your 'attack' and a potentially fatal medical condition.  It backs off, signaling for medical help." ) );

                z->moves -= 300;
                z->anger = -rng( 0, 50 );
                return true;
            } else {
                add_msg( m_bad, _( "Your awkward movements do not fool the riotbot." ) );
                foe->moves -= 100;
                bad_trick = true;
            }
        }

        if( ( choice == ur_resist ) || bad_trick ) {

            add_msg( m_bad, _( "The robot sprays tear gas!" ) );
            z->moves -= 200;

            for( const tripoint_bub_ms &dest : here.points_in_radius( z->bub_pos(), 2 ) ) {
                if( here.passable( dest ) &&
                    here.clear_path( z->bub_pos(), dest, 3, 1, 100 ) ) {
                    here.add_field( dest, fd_tear_gas, rng( 1, 3 ) );
                }
            }

            return true;
        }

        return true;
    }

    if( calendar::once_every( 5_turns ) ) {
        se.volume = 80;
        se.category = sounds::sound_t::electronic_speech;
        se.description = _( "Empty your hands and hold your position, citizen!" );
        se.id = "speech";
        se.variant = z->type->id.str();
        sounds::sound( se );
    }

    if( dist > 5 && dist < 18 && one_in( 10 ) ) {

        z->moves -= 50;

        // Precautionary shot
        int delta = dist / 4 + 1;
        // Precision shot
        if( z->get_hp() < z->get_hp_max() ) {
            delta = 1;
        }

        auto dest = tripoint_bub_ms{ target->bub_pos().x() + rng( 0, delta ) - rng( 0, delta ),
                                     target->bub_pos().y() + rng( 0, delta ) - rng( 0, delta ),
                                     target->bub_pos().z() };

        //~ Sound of a riotbot using its blinding flash
        se.volume = 50;
        se.category = sounds::sound_t::combat;
        se.description = _( "fzzzzzt" );
        se.id = "misc";
        se.variant = "flash";
        sounds::sound( se );

        std::vector<tripoint_bub_ms> traj = line_to( z->bub_pos(), dest, 0, 0 );
        auto prev_point = z->bub_pos();
        for( auto &elem : traj ) {
            if( !here.is_transparent( elem ) || here.obscured_by_vehicle_rotation( prev_point, elem ) ) {
                break;
            }
            here.add_field( elem, fd_dazzling, 1 );
            prev_point = elem;
        }
        return true;

    }

    return true;
}

bool mattack::evolve_kill_strike( monster *z )
{
    Creature *target = z->attack_target();
    if( target == nullptr ||
        !is_adjacent( z, target, false ) ||
        !z->sees( *target ) ) {
        return false;
    }
    if( !z->can_act() ) {
        return false;
    }

    z->moves -= 100;
    const bool uncanny = target->uncanny_dodge();
    if( uncanny || dodge_check( z, target ) ) {
        auto msg_type = target == &g->u ? m_warning : m_info;
        target->add_msg_player_or_npc( msg_type, _( "The %s lunges at you, but you dodge!" ),
                                       _( "The %s lunges at <npcname>, but they dodge!" ),
                                       z->name() );
        if( !uncanny ) {
            target->on_dodge( z, z->type->melee_skill * 2 );
            target->add_msg_player_or_npc( msg_type, _( "The %s lunges at you, but you dodge!" ),
                                           _( "The %s lunges at <npcname>, but they dodge!" ),
                                           z->name() );
        }
        return true;
    }
    const auto target_pos = target->bub_pos();
    const std::string target_name = target->disp_name();
    damage_instance damage( z->type->melee_damage );
    damage.mult_damage( 1.33f );
    damage.add( damage_instance( DT_STAB, dice( z->type->melee_dice, z->type->melee_sides ), rng( 5,
                                 15 ), 1.0, 0.5 ) );
    int damage_dealt = target->deal_damage( z, bodypart_id( "torso" ), damage ).total_damage();
    if( damage_dealt > 0 ) {
        auto msg_type = target == &g->u ? m_bad : m_warning;
        target->add_msg_player_or_npc( msg_type,
                                       _( "The %1$s impales yor chest for %2$d damage!" ),
                                       _( "The %1$s impales <npcname>'s chest for %2$d damage!" ),
                                       z->name(), damage_dealt );
    } else {
        target->add_msg_player_or_npc(
            _( "The %1$s attempts to burrow itself into you, but is stopped by your armor!" ),
            _( "The %1$s slashes at <npcname>'s torso, but is stopped by their armor!" ),
            z->name() );
        return true;
    }
    if( target->is_dead_state() && g->is_empty( target_pos ) &&
        target->made_of_any( Creature::cmat_flesh ) ) {
        const std::string old_name = z->name();
        const bool could_see_z = g->u.sees( *z );
        z->allow_upgrade();
        z->try_upgrade( false );
        z->setpos( target_pos );
        const std::string upgrade_name = z->name();
        const bool can_see_z_upgrade = g->u.sees( *z );
        if( could_see_z && can_see_z_upgrade ) {
            add_msg( m_warning, _( "The %1$s burrows within %2$s corpse and a %3$s emerges from the remains!" ),
                     old_name,
                     target_name, upgrade_name );
        } else if( could_see_z ) {
            add_msg( m_warning, _( "The %1$s burrows within %2$s corpse!" ), old_name, target_name );
        } else if( can_see_z_upgrade ) {
            add_msg( m_warning, _( "A %1$s emerges from %2$s corpse!" ), upgrade_name, target_name );
        }
    }
    return true;
}

bool mattack::leech_spawner( monster *z )
{
    Creature *target = z->attack_target();
    const bool u_see = g->u.sees( *z );
    std::list<monster *> allies;
    for( monster &candidate : g->all_monsters() ) {
        if( candidate.in_species( LEECH_PLANT ) && !candidate.has_flag( MF_IMMOBILE ) ) {
            allies.push_back( &candidate );
        }
    }
    // Only propagate if you see a target or are the current queen
    if( !z->has_flag( MF_QUEEN ) && target == nullptr ) {
        return false;
    }
    if( allies.size() > 30 ) {
        return false;
    }
    const int monsters_spawned = rng( 1, 4 );
    const mtype_id monster_type = one_in( 3 ) ? mon_leech_root_runner : mon_leech_root_drone;
    for( int i = 0; i < monsters_spawned; i++ ) {
        if( monster *const new_mon = g->place_critter_around( monster_type, z->bub_pos(), 1 ) ) {
            if( u_see ) {
                add_msg( m_warning,
                         _( "An egg pod ruptures and a %s crawls out from the remains!" ), new_mon->name() );
            }
            if( one_in( 10 ) && z->has_flag( MF_QUEEN ) ) {
                z->poly( mon_leech_stalk );
                if( u_see ) {
                    add_msg( m_warning,
                             _( "Resplendent fronds emerge from the still intact pods!" ) );
                }
                return true;
            }
        }
    }
    // If egg pod or other non-queen source of this attack, die off from doing this
    if( !z->has_flag( MF_QUEEN ) ) {
        z->set_hp( 0 );
    }
    return true;
}

bool mattack::mon_leech_evolution( monster *z )
{
    const bool u_see = g->u.sees( *z );
    const bool is_queen = z->has_flag( MF_QUEEN );
    std::list<monster *> queens;
    for( monster &candidate : g->all_monsters() ) {
        if( candidate.in_species( LEECH_PLANT ) && candidate.has_flag( MF_QUEEN ) &&
            rl_dist( z->bub_pos(), candidate.bub_pos() ) < 45 ) {
            queens.push_back( &candidate );
        }
    }
    if( !is_queen ) {
        if( queens.empty() ) {
            z->poly( mon_leech_blossom );
            z->set_hp( z->get_hp_max() );
            if( u_see ) {
                add_msg( m_warning,
                         _( "The %s blooms into flowers!" ), z->name() );
            }
        }
    }
    return true;
}

bool mattack::tindalos_teleport( monster *z )
{
    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }
    if( one_in( 7 ) ) {
        if( monster *const afterimage = g->place_critter_around( mon_hound_tindalos_afterimage,
                                        z->bub_pos(),
                                        1 ) ) {
            z->moves -= 140;
            afterimage->make_ally( *z );
            if( g->u.sees( *z ) ) {
                add_msg( m_warning,
                         _( "The hound's movements chaotically rewind as a living afterimage splits from it!" ) );
            }
        }
    }
    const int distance_to_target = rl_dist( z->bub_pos(), target->bub_pos() );
    const tripoint_bub_ms oldpos = z->bub_pos();
    if( distance_to_target > 5 ) {
        for( const tripoint_bub_ms &dest : g->m.points_in_radius( target->bub_pos(), 4 ) ) {
            if( g->m.is_cornerfloor( dest ) ) {
                if( g->is_empty( dest ) ) {
                    z->setpos( dest );
                    // Not teleporting if it means losing sight of our current target
                    if( z->sees( *target ) ) {
                        g->m.add_field( oldpos, fd_tindalos_rift, 2 );
                        g->m.add_field( dest, fd_tindalos_rift, 2 );
                        if( g->u.sees( *z ) ) {
                            add_msg( m_bad, _( "The %s dissipates and reforms close by." ), z->name() );
                        }
                        return true;
                    }
                }
            }
        }
        // couldn't teleport without losing sight of target
        z->setpos( oldpos );
        return true;
    }
    return true;
}

bool mattack::flesh_tendril( monster *z )
{
    Creature *target = z->attack_target();
    sound_event se;
    se.origin = z->bub_pos();
    se.category = sounds::sound_t::alert;
    se.from_monster = true;
    se.monfaction = z->faction.id();
    se.faction = faction_id( "no_faction" );
    if( target == nullptr || !z->sees( *target ) ) {
        if( one_in( 70 ) ) {
            add_msg( _( "The floor trembles underneath your feet." ) );
            z->moves -= 200;
            se.volume = 120;
            se.description = _( "a deafening roar!" );
            se.id = "shout";
            se.variant = "roar";
            sounds::sound( se );
        }
        return false;
    }

    const int distance_to_target = rl_dist( z->bub_pos(), target->bub_pos() );

    // the monster summons stuff to fight you
    if( distance_to_target > 3 && one_in( 12 ) ) {
        mtype_id spawned = mon_zombie_gasbag_crawler;
        if( one_in( 2 ) ) {
            spawned = mon_zombie_gasbag_impaler;
        }
        if( monster *const summoned = g->place_critter_around( spawned, z->bub_pos(), 1 ) ) {
            z->moves -= 100;
            summoned->make_ally( *z );
            g->m.propagate_field( z->bub_pos(), fd_gibs_flesh, 75, 1 );
            if( g->u.sees( *z ) ) {
                add_msg( m_warning, _( "A %s struggles to pull itself free from the %s!" ), summoned->name(),
                         z->name() );
            }
        }
        return true;
    }

    if( ( distance_to_target == 2 || distance_to_target == 3 ) && one_in( 4 ) ) {
        //it pulls you towards itself and then knocks you away
        bool pulled = ranged_pull( z );
        if( pulled && one_in( 4 ) ) {
            se.volume = 120;
            se.description = _( "a deafening roar!" );
            se.id = "shout";
            se.variant = "roar";
            sounds::sound( se );
        }
        return pulled;
    }

    if( distance_to_target <= 1 ) {
        if( one_in( 8 ) ) {
            g->fling_creature( target, coord_to_angle( z->bub_pos(), target->bub_pos() ),
                               z->type->melee_sides * z->type->melee_dice * 3 );
        } else {
            grab( z );
        }
    }

    return false;
}

bool mattack::bio_op_random_biojutsu( monster *z )
{
    int choice;
    int redo;

    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ||
        !is_adjacent( z, target, false ) ||
        !z->sees( *target ) ) {
        return false;
    }

    player *foe = dynamic_cast< player * >( target );

    do {
        choice = rng( 1, 3 );
        redo = false;

        // ignore disarm if the target isn't a "player" or isn't armed
        if( choice == 3 && foe != nullptr && !foe->is_armed() ) {
            redo = true;
        }

    } while( redo );

    switch( choice ) {
        case 1:
            bio_op_takedown( z );
            break;
        case 2:
            bio_op_impale( z );
            break;
        case 3:
            bio_op_disarm( z );
            break;
    }

    return true;
}

bool mattack::bio_op_takedown( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    // TODO: Allow drop-takedown form above
    if( target == nullptr ||
        !is_adjacent( z, target, false ) ||
        !z->sees( *target ) ) {
        return false;
    }

    bool seen = g->u.sees( *z );
    player *foe = dynamic_cast< player * >( target );
    if( seen ) {
        add_msg( _( "%1$s mechanically grabs at %2$s!" ),
                 z->disp_name( false, true ), target->disp_name() );
    }
    z->moves -= 100;

    if( target->uncanny_dodge() ) {
        return true;
    }

    // Can we dodge the attack? Uses player dodge function % chance (melee.cpp)
    if( dodge_check( z, target ) ) {
        target->add_msg_player_or_npc( _( "You dodge it!" ),
                                       _( "<npcname> dodges it!" ) );
        target->on_dodge( z, z->type->melee_skill * 2 );
        return true;
    }
    int dam = rng( 3, 9 );
    if( foe == nullptr ) {
        // Handle mons earlier - less to check for
        dam = rng( 6, 18 );
        // Always aim for the torso
        target->deal_damage( z, bodypart_id( "torso" ), damage_instance( DT_BASH, dam ) );
        // Two hits - "leg" and torso
        target->deal_damage( z, bodypart_id( "torso" ), damage_instance( DT_BASH, dam ) );
        target->add_effect( effect_downed, 3_turns );
        if( seen ) {
            add_msg( _( "%1$s slams %2$s to the ground!" ), z->disp_name( false, true ), target->disp_name() );
        }
        target->check_dead_state();
        return true;
    }
    // Yes, it has the CQC bionic.
    bodypart_str_id hit;
    if( one_in( 2 ) ) {
        hit = body_part_leg_l;
    } else {
        hit = body_part_leg_r;
    }
    // Weak kick to start with, knocks you off your footing

    //~ 1$s is the attacker, 2$s is bodypart name in accusative, 3$d is damage value.
    target->add_msg_if_player( m_bad, _( "The %1$s kicks your %2$s for %3$d damage…" ),
                               z->name(), body_part_name_accusative( hit->token ), dam );
    foe->deal_damage( z,  hit, damage_instance( DT_BASH, dam ) );
    // At this point, Judo or Tentacle Bracing can make this much less painful
    if( !foe->is_throw_immune() ) {
        if( !target->is_immune_effect( effect_downed ) ) {
            if( one_in( 4 ) ) {
                hit = body_part_head;
                // 50% damage buff for the headshot.
                dam = rng( 9, 21 );
                target->add_msg_if_player( m_bad, _( "and slams you, face first, to the ground for %d damage!" ),
                                           dam );
                foe->deal_damage( z, body_part_head.id(), damage_instance( DT_BASH, dam ) );
            } else {
                hit = body_part_torso;
                dam = rng( 6, 18 );
                target->add_msg_if_player( m_bad, _( "and slams you to the ground for %d damage!" ), dam );
                foe->deal_damage( z, body_part_torso.id(), damage_instance( DT_BASH, dam ) );
            }
            foe->add_effect( effect_downed, 3_turns );
        }
    } else if( ( !foe->is_armed() ||
                 foe->martial_arts_data->selected_has_weapon( foe->primary_weapon().typeId() ) ) &&
               !thrown_by_judo( z ) ) {
        // Saved by the tentacle-bracing! :)
        hit = body_part_torso;
        dam = rng( 3, 9 );
        target->add_msg_if_player( m_bad, _( "and slams you for %d damage!" ), dam );
        foe->deal_damage( z, bodypart_id( "torso" ), damage_instance( DT_BASH, dam ) );
    }
    target->on_hit( z, hit.id() );
    foe->check_dead_state();

    return true;
}

bool mattack::bio_op_impale( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ||
        !is_adjacent( z, target, false ) ||
        !z->sees( *target ) ) {
        return false;
    }

    const bool seen = g->u.sees( *z );
    player *foe = dynamic_cast< player * >( target );
    if( seen ) {
        add_msg( _( "%1$s mechanically lunges at %2$s!" ),
                 z->disp_name( false, true ), target->disp_name() );
    }
    z->moves -= 100;

    if( target->uncanny_dodge() ) {
        return true;
    }

    // Can we dodge the attack? Uses player dodge function % chance (melee.cpp)
    if( dodge_check( z, target ) ) {
        target->add_msg_player_or_npc( _( "You dodge it!" ),
                                       _( "<npcname> dodges it!" ) );
        target->on_dodge( z, z->type->melee_skill * 2 );
        return true;
    }

    // Yes, it has the CQC bionic.
    int dam = rng( 8, 24 );
    bool do_bleed = false;
    int t_dam;

    if( one_in( 4 ) ) {
        dam = rng( 12, 36 ); // 50% damage buff for the crit.
        do_bleed = true;
    }

    if( foe == nullptr ) {
        // Handle mons earlier - less to check for
        target->deal_damage( z, bodypart_id( "torso" ), damage_instance( DT_STAB, dam ) );
        if( do_bleed ) {
            target->add_effect( effect_bleed, rng( 75_turns, 125_turns ), body_part_torso );
        }
        if( seen ) {
            add_msg( _( "%1$s impales %2$s!" ), z->disp_name( false, true ), target->disp_name() );
        }
        target->check_dead_state();
        return true;
    }

    const bodypart_id hit = target->get_random_body_part();

    t_dam = foe->deal_damage( z, hit, damage_instance( DT_STAB, dam ) ).total_damage();

    target->add_msg_player_or_npc( _( "The %1$s tries to impale your %s…" ),
                                   _( "The %1$s tries to impale <npcname>'s %s…" ),
                                   z->name(), body_part_name_accusative( hit->token ) );

    if( t_dam > 0 ) {
        target->add_msg_if_player( m_bad, _( "and deals %d damage!" ), t_dam );

        if( do_bleed ) {
            target->add_effect( effect_bleed, rng( 75_turns, 125_turns ), body_part_torso );
        }
    } else {
        target->add_msg_player_or_npc( _( "but fails to penetrate your armor!" ),
                                       _( "but fails to penetrate <npcname>'s armor!" ) );
    }

    target->on_hit( z, hit );
    foe->check_dead_state();

    return true;
}

bool mattack::bio_op_disarm( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ||
        !is_adjacent( z, target, false ) ||
        !z->sees( *target ) ) {
        return false;
    }

    const bool seen = g->u.sees( *z );
    player *foe = dynamic_cast< player * >( target );

    // disarm doesn't work on creatures or unarmed targets
    if( foe == nullptr || ( foe != nullptr && !foe->is_armed() ) ) {
        return false;
    }

    if( seen ) {
        add_msg( _( "%1$s mechanically reaches for %2$s!" ),
                 z->disp_name( false, true ), target->disp_name() );
    }
    z->moves -= 100;

    if( target->uncanny_dodge() ) {
        return true;
    }

    // Can we dodge the attack? Uses player dodge function % chance (melee.cpp)
    if( dodge_check( z, target ) ) {
        target->add_msg_player_or_npc( _( "You dodge it!" ),
                                       _( "<npcname> dodges it!" ) );
        target->on_dodge( z, z->type->melee_skill * 2 );
        return true;
    }

    int mon_stat = z->type->melee_dice * z->type->melee_sides;
    int my_roll = dice( 3, 2 * mon_stat );
    my_roll += dice( 3, z->type->melee_skill );

    /** @EFFECT_STR increases chance to avoid disarm, primary stat */
    /** @EFFECT_DEX increases chance to avoid disarm, secondary stat */
    /** @EFFECT_PER increases chance to avoid disarm, secondary stat */
    /** @EFFECT_MELEE increases chance to avoid disarm */
    int their_roll = dice( 3, 2 * foe->get_str() + foe->get_dex() );
    their_roll += dice( 3, foe->get_per() );
    their_roll += dice( 3, foe->get_skill_level( skill_melee ) );

    item &it = foe->primary_weapon();

    target->add_msg_if_player( m_bad, _( "The zombie grabs your %s…" ), it.tname() );

    if( my_roll >= their_roll && !it.has_flag( flag_NO_UNWIELD ) ) {
        target->add_msg_if_player( m_bad, _( "and throws it to the ground!" ) );
        const tripoint_bub_ms tp = foe->bub_pos() + tripoint_rel_ms( rng( -1, 1 ), rng( -1, 1 ), 0 );
        g->m.add_item_or_charges( tp, it.detach( ) );
    } else {
        target->add_msg_if_player( m_good, _( "but you break its grip!" ) );
    }

    return true;
}

bool mattack::suicide( monster *z )
{
    Creature *target = z->attack_target();
    if( !within_target_range( z, target, 2 ) ) {
        return false;
    }
    z->die( z );

    return false;
}

bool mattack::kamikaze( monster *z )
{
    if( z->ammo.empty() ) {
        // We somehow lost our ammo! Toggle this special off so we stop processing
        add_msg( m_debug, "Missing ammo in kamikaze special for %s.", z->name() );
        z->disable_special( "KAMIKAZE" );
        return true;
    }

    // Get the bomb type and it's data
    const itype *bomb_type = &*z->ammo.begin()->first;
    itype_id act_bomb_type;
    int charges;
    // Hardcoded data for charge variant items
    if( z->ammo.begin()->first == itype_mininuke ) {
        act_bomb_type = itype_mininuke_act;
        charges = 20;
    } else if( z->ammo.begin()->first == itype_c4 ) {
        act_bomb_type = itype_c4armed;
        charges = 10;
    } else {
        auto usage = bomb_type->get_use( "transform" );
        if( usage == nullptr ) {
            // Invalid item usage, Toggle this special off so we stop processing
            add_msg( m_debug, "Invalid bomb transform use in kamikaze special for %s.", z->name() );
            z->disable_special( "KAMIKAZE" );
            return true;
        }
        const iuse_transform *actor = dynamic_cast<const iuse_transform *>( usage->get_actor_ptr() );
        if( actor == nullptr ) {
            // Invalid bomb item, Toggle this special off so we stop processing
            add_msg( m_debug, "Invalid bomb type in kamikaze special for %s.", z->name() );
            z->disable_special( "KAMIKAZE" );
            return true;
        }
        act_bomb_type = actor->target;
        charges = actor->ammo_qty;
    }

    // HACK: HORRIBLE HACK ALERT! Remove the following code completely once we have working monster inventory processing
    if( z->has_effect( effect_countdown ) ) {
        if( z->get_effect( effect_countdown ).get_duration() == 1_turns ) {
            z->die( nullptr );
            // Timer is out, detonate
            detached_ptr<item> i_explodes = item::spawn( act_bomb_type, calendar::turn, 0 );
            i_explodes->activate();
            item::process( std::move( i_explodes ), nullptr, z->bub_pos(), false );
            return false;
        }
        return false;
    }
    // END HORRIBLE HACK

    auto use = act_bomb_type->get_use( "explosion" );
    if( use == nullptr ) {
        // Invalid active bomb item usage, Toggle this special off so we stop processing
        add_msg( m_debug, "Invalid active bomb explosion use in kamikaze special for %s.",
                 z->name() );
        z->disable_special( "KAMIKAZE" );
        return true;
    }
    const explosion_iuse *exp_actor = dynamic_cast<const explosion_iuse *>( use->get_actor_ptr() );
    if( exp_actor == nullptr ) {
        // Invalid active bomb item, Toggle this special off so we stop processing
        add_msg( m_debug, "Invalid active bomb type in kamikaze special for %s.", z->name() );
        z->disable_special( "KAMIKAZE" );
        return true;
    }

    // Get our blast radius
    int radius = -1;
    if( exp_actor->fields_radius > radius ) {
        radius = exp_actor->fields_radius;
    }
    if( exp_actor->emp_blast_radius > radius ) {
        radius = exp_actor->emp_blast_radius;
    }
    // Extra check here to avoid sqrt if not needed
    if( exp_actor->explosion ) {
        int tmp = ( exp_actor->explosion.safe_range() / 2 );
        if( tmp > radius ) {
            radius = tmp;
        }
    }
    // Flashbangs have a max range of 8
    if( exp_actor->do_flashbang && radius < 8 ) {
        radius = 8;
    }
    if( radius <= -1 ) {
        // Not a valid explosion size, toggle this special off to stop processing
        z->disable_special( "KAMIKAZE" );
        return true;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }
    // Range is (radius + distance they expect to gain on you during the countdown)
    // We double target speed because if the player is walking and then start to run their effective speed doubles
    // .65 factor was determined experimentally to be about the factor required for players to be able to *just barely*
    // outrun the explosion if they drop everything and run.
    float factor = static_cast<float>( z->get_speed() ) / static_cast<float>( target->get_speed() * 2 );
    int range = std::max( 1, static_cast<int>( .65 * ( radius + 1 + factor * charges ) ) );

    // Check if we are in range to begin the countdown
    if( !within_target_range( z, target, range ) ) {
        return false;
    }

    // HACK: HORRIBLE HACK ALERT! Currently uses the amount of ammo as a pseudo-timer.
    // Once we have proper monster inventory item processing replace the following
    // line with the code below.
    z->add_effect( effect_countdown, 1_turns * charges + 1_turns );
    /* Replacement code here for once we have working monster inventories

    item i_explodes(act_bomb_type->id, 0);
    i_explodes.charges = charges;
    z->add_item(i_explodes);
    z->disable_special("KAMIKAZE");
    */
    // END HORRIBLE HACK

    if( g->u.sees( z->bub_pos() ) ) {
        add_msg( m_bad, _( "The %s lights up menacingly." ), z->name() );
    }

    return true;
}

struct grenade_helper_struct {
    std::string message;
    int chance = 1;
    float ammo_percentage = 1;
};

// Returns 0 if this should be retired, 1 if it was successful, and -1 if something went horribly wrong
static int grenade_helper( monster *const z, Creature *const target, const int dist,
                           const int moves, std::map<itype_id, grenade_helper_struct> data )
{
    // Can't do anything if we can't act
    if( !z->can_act() ) {
        return 0;
    }
    // Too far or we can't target them
    if( !within_target_range( z, target, dist ) ) {
        return 0;
    }
    // We need an open space for these attacks
    const auto empty_neighbors = find_empty_neighbors( *z );
    const size_t empty_neighbor_count = empty_neighbors.second;
    if( !empty_neighbor_count ) {
        return 0;
    }

    int total_ammo = 0;
    // Sum up the ammo entries to get a ratio.
    for( const auto &ammo_entry : z->type->starting_ammo ) {
        total_ammo += ammo_entry.second;
    }
    if( total_ammo == 0 ) {
        // Should never happen, but protect us from a div/0 if it does.
        return -1;
    }

    // Find how much ammo we currently have to get the total ratio
    int curr_ammo = 0;
    for( const auto &amm : z->ammo ) {
        curr_ammo += amm.second;
    }
    float rat = curr_ammo / static_cast<float>( total_ammo );

    if( curr_ammo == 0 ) {
        // We've run out of ammo, get angry and toggle the special off.
        z->anger = 100;
        return -1;
    }

    // Hey look! another weighted list!
    // Grab all attacks that pass their chance check and we've spent enough ammo for
    weighted_float_list<itype_id> possible_attacks;
    for( const auto &amm : z->ammo ) {
        if( amm.second > 0 && data[amm.first].ammo_percentage >= rat ) {
            possible_attacks.add( amm.first, 1.0 / data[amm.first].chance );
        }
    }
    itype_id att = *possible_attacks.pick();

    z->moves -= moves;
    z->ammo[att]--;

    // if the player can see it
    if( g->u.sees( *z ) ) {
        if( data[att].message.empty() ) {
            add_msg( m_debug, "Invalid ammo message in grenadier special." );
        } else {
            add_msg( m_bad, data[att].message, z->name() );
        }
    }

    // Get our monster type
    const use_function *usage = att->get_use( "place_monster" );
    if( usage == nullptr ) {
        // Invalid bomb item usage, Toggle this special off so we stop processing
        add_msg( m_debug, "Invalid bomb item usage in grenadier special for %s.", z->name() );
        return -1;
    }
    auto *actor = dynamic_cast<const place_monster_iuse *>( usage->get_actor_ptr() );
    if( actor == nullptr ) {
        // Invalid bomb item, Toggle this special off so we stop processing
        add_msg( m_debug, "Invalid bomb type in grenadier special for %s.", z->name() );
        return -1;
    }

    const auto where = empty_neighbors.first[get_random_index( empty_neighbor_count )];

    if( monster *const hack = g->place_critter_at( actor->mtypeid, where ) ) {
        hack->make_ally( *z );
    }
    return 1;
}

bool mattack::grenadier( monster *const z )
{
    // Build our grenade map
    std::map<itype_id, grenade_helper_struct> grenades;
    // Grenades
    grenades[itype_bot_pacification_hack].message =
        _( "The %s deploys a pacification hack!" );
    // Flashbangs
    grenades[itype_bot_flashbang_hack].message =
        _( "The %s deploys a flashbang hack!" );
    // Gasbombs
    grenades[itype_bot_gasbomb_hack].message =
        _( "The %s deploys a tear gas hack!" );
    // C-4
    grenades[itype_bot_c4_hack].message = _( "The %s buzzes and deploys a C-4 hack!" );
    grenades[itype_bot_c4_hack].chance = 8;

    // Only can actively target the player right now. Once we have the ability to grab targets that we aren't
    // actively attacking change this to use that instead.
    Creature *const target = static_cast<Creature *>( &g->u );
    if( z->attitude_to( *target ) == Attitude::A_FRIENDLY ) {
        return false;
    }
    int ret = grenade_helper( z, target, 30, 60, grenades );
    if( ret == -1 ) {
        // Something broke badly, disable our special
        z->disable_special( "GRENADIER" );
    }
    return true;
}

bool mattack::grenadier_elite( monster *const z )
{
    // Build our grenade map
    std::map<itype_id, grenade_helper_struct> grenades;
    // Grenades
    grenades[itype_bot_grenade_hack].message = _( "The %s deploys a grenade hack!" );
    // Flashbangs
    grenades[itype_bot_flashbang_hack].message =
        _( "The %s deploys a flashbang hack!" );
    // Gasbombs
    grenades[itype_bot_gasbomb_hack].message = _( "The %s deploys a tear gas hack!" );
    // C-4
    grenades[itype_bot_c4_hack].message = _( "The %s buzzes and deploys a C-4 hack!" );
    grenades[itype_bot_c4_hack].chance = 8;
    grenades[itype_bot_c4_hack].ammo_percentage = .75;
    // Mininuke
    grenades[itype_bot_mininuke_hack].message =
        _( "A klaxon blares from %s as it deploys a mininuke hack!" );
    grenades[itype_bot_mininuke_hack].chance = 50;
    grenades[itype_bot_mininuke_hack].ammo_percentage = .75;

    // Only can actively target the player right now. Once we have the ability to grab targets that we aren't
    // actively attacking change this to use that instead.
    Creature *const target = static_cast<Creature *>( &g->u );
    if( z->attitude_to( *target ) == Attitude::A_FRIENDLY ) {
        return false;
    }
    int ret = grenade_helper( z, target, 30, 60, grenades );
    if( ret == -1 ) {
        // Something broke badly, disable our special
        z->disable_special( "GRENADIER_ELITE" );
    }

    return true;
}

bool mattack::stretch_attack( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }

    int distance = rl_dist( z->bub_pos(), target->bub_pos() );
    if( distance < 2 || distance > 3 || !z->sees( *target ) ) {
        return false;
    }

    int dam = rng( 5, 10 );
    z->moves -= 100;
    auto prev_point = z->bub_pos();
    bool bounce = false;
    for( auto &pnt : g->m.find_clear_path( z->bub_pos(), target->bub_pos() ) ) {
        if( g->m.obstructed_by_vehicle_rotation( prev_point, pnt ) ) {
            bounce = true;
            //50% chance of bouncing off each intervening tile
            if( one_in( 2 ) ) {
                pnt.x() = prev_point.x();
            } else {
                pnt.y() = prev_point.y();
            }
        }
        if( bounce || g->m.impassable( pnt ) ) {
            target->add_msg_player_or_npc( _( "The %1$s thrusts its arm at you, but bounces off the %2$s." ),
                                           _( "The %1$s thrusts its arm at <npcname>, but bounces off the %2$s." ),
                                           z->name(), g->m.obstacle_name( pnt ) );
            return true;
        }

        prev_point = pnt;

    }

    auto msg_type = target == &g->u ? m_warning : m_info;
    target->add_msg_player_or_npc( msg_type,
                                   _( "The %s thrusts its arm at you, stretching to reach you from afar." ),
                                   _( "The %s thrusts its arm at <npcname>." ),
                                   z->name() );
    if( dodge_check( z, target ) || g->u.uncanny_dodge() ) {
        target->add_msg_player_or_npc( msg_type, _( "You evade the stretched arm and it sails past you!" ),
                                       _( "<npcname> evades the stretched arm!" ) );
        target->on_dodge( z, z->type->melee_skill * 2 );
        //takes some time to retract the arm
        z->moves -= 150;
        return true;
    }

    const bodypart_id hit = target->get_random_body_part();
    dam = target->deal_damage( z, hit, damage_instance( DT_STAB, dam ) ).total_damage();

    if( dam > 0 ) {
        auto msg_type = target == &g->u ? m_bad : m_info;
        target->add_msg_player_or_npc( msg_type,
                                       //~ 1$s is monster name, 2$s bodypart in accusative
                                       _( "The %1$s's arm pierces your %2$s!" ),
                                       //~ 1$s is monster name, 2$s bodypart in accusative
                                       _( "The %1$s arm pierces <npcname>'s %2$s!" ),
                                       z->name(),
                                       body_part_name_accusative( hit->token ) );

        target->check_dead_state();
    } else {
        target->add_msg_player_or_npc( _( "The %1$s arm hits your %2$s, but glances off your armor!" ),
                                       _( "The %1$s hits <npcname>'s %2$s, but glances off armor!" ),
                                       z->name(),
                                       body_part_name_accusative( hit->token ) );
    }

    target->on_hit( z, hit );

    return true;
}

bool mattack::zombie_fuse( monster *z )
{
    // Don't max out fusion while just vibing away from targets.
    Creature *target = z->attack_target();
    if( target == nullptr || !z->sees( *target ) ) {
        return false;
    }
    monster *critter = nullptr;
    for( const tripoint_bub_ms &p : g->m.points_in_radius( z->bub_pos(), 1 ) ) {
        critter = g->critter_at<monster>( p );
        if( critter != nullptr && critter->faction == z->faction
            && critter != z && critter->get_size() <= z->get_size() ) {
            break;
        }
    }

    if( critter == nullptr || ( z->has_effect( effect_grown_of_fuse ) &&
                                ( z->get_hp() + critter->get_hp() > z->get_hp_max() + z->get_effect(
                                      effect_grown_of_fuse ).get_max_intensity() ) ) ) {
        return false;
    }
    if( g->u.sees( *z ) ) {
        add_msg( _( "The %1$s fuses with the %2$s." ),
                 critter->name(),
                 z->name() );
    }
    z->moves -= 200;
    z->add_effect( effect_grown_of_fuse, 10_days, bodypart_str_id::NULL_ID(),
                   critter->get_hp_max() + z->get_effect( effect_grown_of_fuse ).get_intensity() );
    z->heal( critter->get_hp(), true );
    critter->death_drops = false;
    critter->die( z );
    return true;
}

bool mattack::doot( monster *z )
{
    if( z->type->monster_weapon && z->has_effect( effect_monster_disarmed ) ) {
        return false;
    }
    z->moves -= 300;
    if( g->u.sees( *z ) ) {
        add_msg( _( "The %s doots its trumpet!" ), z->name() );
    }
    int spooks = 0;
    for( const tripoint_bub_ms &spookyscary : g->m.points_in_radius( z->bub_pos(), 2 ) ) {
        if( !g->is_empty( spookyscary ) ) {
            continue;
        }
        const int dist = rl_dist( z->bub_pos(), spookyscary );
        if( ( one_in( dist + 3 ) || spooks == 0 ) && spooks < 5 ) {
            if( g->u.sees( *z ) ) {
                add_msg( _( "A spooky skeleton rises from the ground!" ) );
            }
            g->place_critter_at( mon_zombie_skeltal_minion, spookyscary );
            spooks++;
            continue;
        }
    }
    sound_event se;
    se.origin = z->bub_pos();
    se.volume = 140;
    se.category = sounds::sound_t::music;
    se.description = _( "DOOT." );
    se.from_monster = true;
    se.monfaction = z->faction.id();
    se.faction = faction_id( "no_faction" );
    se.id = "music_instrument";
    se.variant = "trumpet";
    sounds::sound( se );
    return true;
}

bool mattack::dodge_check( monster *z, Creature *target )
{
    ///\EFFECT_DODGE increases chance of dodging, vs their melee skill
    float dodge = std::max( target->get_dodge() - rng( 0, z->get_hit() ), 0.0f );
    return rng( 0, 10000 ) < 10000 / ( 1 + 99 * std::exp( -.6 * dodge ) );
}
