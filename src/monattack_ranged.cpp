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


bool mattack::tazer( monster *z )
{
    if( z->type->monster_weapon && z->has_effect( effect_monster_disarmed ) ) {
        return false;
    }
    Creature *target = z->attack_target();
    if( target == nullptr || !is_adjacent( z, target, false ) ) {
        return false;
    }

    taze( z, target );
    return true;
}

void mattack::taze( monster *z, Creature *target )
{
    // It takes a while
    z->moves -= 200;
    // Uncanny dodge prints its own message when it triggers, to return here instead of below.
    if( target == nullptr || target->uncanny_dodge() ) {
        return;
    }
    if( dodge_check( z, target ) ) {
        target->add_msg_player_or_npc( _( "The %s tries to shock you, but you dodge." ),
                                       _( "The %s tries to shock <npcname>, but they dodge." ),
                                       z->name() );
        return;
    }

    int dam = target->deal_damage( z, target->get_random_body_part(), damage_instance( DT_ELECTRIC,
                                   rng( 1,
                                        5 ) ) ).total_damage();
    if( dam == 0 ) {
        target->add_msg_player_or_npc( _( "The %s unsuccessfully attempts to shock you." ),
                                       _( "The %s unsuccessfully attempts to shock <npcname>." ),
                                       z->name() );
        return;
    }

    auto m_type = target->attitude_to( g->u ) == Attitude::A_FRIENDLY ? m_bad : m_neutral;
    target->add_msg_player_or_npc( m_type,
                                   _( "The %s shocks you!" ),
                                   _( "The %s shocks <npcname>!" ),
                                   z->name() );
    target->check_dead_state();
}

void mattack::rifle( monster *z, Creature *target )
{
    if( z->type->monster_weapon && z->has_effect( effect_monster_disarmed ) ) {
        return;
    }
    const itype_id ammo_type( "556" );
    // Make sure our ammo isn't weird.
    if( z->ammo[ammo_type] > 3000 ) {
        debugmsg( "Generated too much ammo (%d) for %s in mattack::rifle", z->ammo[ammo_type],
                  z->name() );
        z->ammo[ammo_type] = 3000;
    }

    //TODO!: check alllla this shit
    std::unique_ptr<npc> tmp = make_fake_npc( z, 16, 10, 8, 12 );
    tmp->set_skill_level( skill_rifle, 8 );
    tmp->set_skill_level( skill_gun, 6 );
    // No need to aim
    tmp->recoil = 0;
    sound_event se;
    se.origin = z->bub_pos();
    se.from_monster = true;
    se.monfaction = z->faction.id();
    if( target == &g->u ) {
        if( !z->has_effect( effect_targeted ) ) {
            se.volume = 60;
            se.description = _( "beep-beep." );
            se.id = "misc";
            se.variant = "beep";
            se.category = sounds::sound_t::alarm;
            sounds::sound( se );
            z->add_effect( effect_targeted, 8_turns );
            z->moves -= 100;
            return;
        }
    }
    // It takes a while
    z->moves -= 150;

    if( z->ammo[ammo_type] <= 0 ) {
        if( one_in( 3 ) ) {
            se.volume = 50;
            se.description = _( "a chk!" );
            se.id = "fire_gun";
            se.variant = "empty";
            se.category = sounds::sound_t::combat;
            sounds::sound( se );
        } else if( one_in( 4 ) ) {
            se.volume = 60;
            se.description = _( "boop!" );
            se.id = "fire_gun";
            se.variant = "empty";
            se.category = sounds::sound_t::combat;
            sounds::sound( se );
        }
        return;
    }
    if( g->u.sees( *z ) ) {
        add_msg( m_warning, _( "The %s opens up with its rifle!" ), z->name() );
    }

    detached_ptr<item> gun = item::spawn( "m4a1" );
    gun->ammo_set( ammo_type, z->ammo[ ammo_type ] );
    tmp->set_primary_weapon( std::move( gun ) );
    int burst = std::max( tmp->primary_weapon().gun_get_mode( gun_mode_id( "AUTO" ) ).qty, 1 );
    z->ammo[ ammo_type ] -= ranged::fire_gun( *tmp, target->bub_pos(),
                            burst ) * tmp->primary_weapon().ammo_required();

    if( target == &g->u ) {
        z->add_effect( effect_targeted, 3_turns );
    }
}

void mattack::frag( monster *z, Creature *target ) // This is for the bots, not a standalone turret
{
    if( z->type->monster_weapon && z->has_effect( effect_monster_disarmed ) ) {
        return;
    }
    const itype_id ammo_type( "40x46mm_m433" );
    // Make sure our ammo isn't weird.
    if( z->ammo[ammo_type] > 200 ) {
        debugmsg( "Generated too much ammo (%d) for %s in mattack::frag", z->ammo[ammo_type],
                  z->name() );
        z->ammo[ammo_type] = 200;
    }
    sound_event se;
    se.origin = z->bub_pos();
    se.from_monster = true;
    se.monfaction = z->faction.id();
    se.faction = faction_id( "no_faction" );

    if( target == &g->u ) {
        if( !z->has_effect( effect_targeted ) ) {
            if( g->u.has_trait( trait_PROF_CHURL ) ) {
                //~ Potential grenading detected.
                add_msg( m_warning, _( "Thee eye o dat divil be upon me!" ) );
            } else {
                //~ Potential grenading detected.
                add_msg( m_warning, _( "Those laser dots don't seem very friendly…" ) );
            }
            // Effect removed in game.cpp, duration doesn't much matter
            g->u.add_effect( effect_laserlocked, 3_turns );
            se.volume = 60;
            se.category = sounds::sound_t::electronic_speech;
            se.description = _( "Targeting." );
            se.id = "speech";
            se.variant = z->type->id.str();
            sounds::sound( se );
            z->add_effect( effect_targeted, 5_turns );
            z->moves -= 150;
            // Should give some ability to get behind cover,
            // even though it's patently unrealistic.
            return;
        }
    }
    std::unique_ptr<npc> tmp = make_fake_npc( z, 16, 10, 8, 12 );
    tmp->set_skill_level( skill_launcher, 8 );
    tmp->set_skill_level( skill_gun, 6 );
    // No need to aim
    tmp->recoil = 0;
    // It takes a while
    z->moves -= 150;

    if( z->ammo[ammo_type] <= 0 ) {
        se.id = "fire_gun";
        se.variant = "empty";
        se.category = sounds::sound_t::combat;
        se.volume = 60;
        if( one_in( 3 ) ) {
            se.description = _( "a chk!" );
            sounds::sound( se );
        } else if( one_in( 4 ) ) {
            se.description = _( "boop!" );
            sounds::sound( se );
        }
        return;
    }
    if( g->u.sees( *z ) ) {
        add_msg( m_warning, _( "The %s's grenade launcher fires!" ), z->name() );
    }

    detached_ptr<item> tweap = item::spawn( "mgl" );
    tweap->ammo_set( ammo_type, z->ammo[ ammo_type ] );
    tmp->set_primary_weapon( std::move( tweap ) );
    int burst = std::max( tmp->primary_weapon().gun_get_mode( gun_mode_id( "AUTO" ) ).qty, 1 );

    z->ammo[ ammo_type ] -= ranged::fire_gun( *tmp, target->bub_pos(),
                            burst ) * tmp->primary_weapon().ammo_required();

    if( target == &g->u ) {
        z->add_effect( effect_targeted, 3_turns );
    }
}

void mattack::tankgun( monster *z, Creature *target )
{
    if( z->type->monster_weapon && z->has_effect( effect_monster_disarmed ) ) {
        return;
    }
    const itype_id ammo_type( "120mm_HEAT" );
    // Make sure our ammo isn't weird.
    if( z->ammo[ammo_type] > 40 ) {
        debugmsg( "Generated too much ammo (%d) for %s in mattack::tankgun", z->ammo[ammo_type],
                  z->name() );
        z->ammo[ammo_type] = 40;
    }

    int dist = rl_dist( z->bub_pos(), target->bub_pos() );
    if( dist > 50 ) {
        return;
    }
    sound_event se;
    se.origin = z->bub_pos();

    se.from_monster = true;
    se.monfaction = z->faction.id();
    se.faction = faction_id( "no_faction" );
    se.category = sounds::sound_t::combat;
    if( !z->has_effect( effect_targeted ) ) {
        //~ There will be a 120mm HEAT shell sent at high speed to your location next turn.
        target->add_msg_if_player( m_warning, _( "You're not sure why you've got a laser dot on you…" ) );
        //~ Sound of a tank turret swiveling into place
        se.volume = 65;
        se.description = _( "whirrrrrclick." );
        se.id = "misc";
        se.variant = "servomotor";

        sounds::sound( se );
        z->add_effect( effect_targeted, 1_minutes );
        target->add_effect( effect_laserlocked, 1_minutes );
        z->moves -= 200;
        // Should give some ability to get behind cover,
        // even though it's patently unrealistic.
        return;
    }
    // kevingranade KA101: yes, but make it really inaccurate
    // Sure thing.
    std::unique_ptr<npc> tmp = make_fake_npc( z, 12, 8, 8, 8 );
    tmp->set_skill_level( skill_launcher, 1 );
    tmp->set_skill_level( skill_gun, 1 );
    // No need to aim
    tmp->recoil = 0;
    // It takes a while
    z->moves -= 150;

    if( z->ammo[ammo_type] <= 0 ) {
        se.id = "fire_gun";
        se.variant = "empty";
        se.volume = 60;
        if( one_in( 3 ) ) {
            se.description = _( "a chk!" );
            sounds::sound( se );
        } else if( one_in( 4 ) ) {
            se.description = ( "clank!" );
            sounds::sound( se );
        }
        return;
    }
    if( g->u.sees( *z ) ) {
        add_msg( m_warning, _( "The %s's 120mm cannon fires!" ), z->name() );
    }

    detached_ptr<item> gun = item::spawn( "TANK" );
    gun->ammo_set( ammo_type, z->ammo[ ammo_type ] );
    tmp->set_primary_weapon( std::move( gun ) );
    int burst = std::max( tmp->primary_weapon().gun_get_mode( gun_mode_id( "AUTO" ) ).qty, 1 );

    z->ammo[ ammo_type ] -= ranged::fire_gun( *tmp, target->bub_pos(),
                            burst ) * tmp->primary_weapon().ammo_required();
}

void mattack::atgm( monster *z, Creature *target )
{
    if( z->type->monster_weapon && z->has_effect( effect_monster_disarmed ) ) {
        return;
    }
    const itype_id ammo_type( "atgm_heat" );
    // Make sure our ammo isn't weird.
    if( z->ammo[ammo_type] > 4 ) {
        debugmsg( "Generated too much ammo (%d) for %s in mattack::atgm", z->ammo[ammo_type],
                  z->name() );
        z->ammo[ammo_type] = 4;
    }

    int dist = rl_dist( z->bub_pos(), target->bub_pos() );
    if( dist > 50 ) {
        return;
    }
    sound_event se;
    se.origin = z->bub_pos();
    se.from_monster = true;
    se.monfaction = z->faction->id;
    se.category = sounds::sound_t::combat;
    if( !z->has_effect( effect_targeted ) ) {
        //~ There will be a ATGM HEAT sent at high speed to your location next turn.
        target->add_msg_if_player( m_warning, _( "You're not sure why you've got a laser dot on you…" ) );
        //~ Sound of a atgm tube uncovering swiveling into place
        se.description = _( "whirrrrrclick." );
        se.id = "misc";
        se.variant = "servomotor";
        se.volume = 75;
        sounds::sound( se );
        z->add_effect( effect_targeted, 1_minutes );
        target->add_effect( effect_laserlocked, 1_minutes );
        z->moves -= 200;
        // Should give some ability to get behind cover,
        // even though it's patently unrealistic.
        return;
    }
    std::unique_ptr<npc> tmp = make_fake_npc( z, 12, 8, 8, 8 );
    tmp->set_skill_level( skill_launcher, 1 );
    tmp->set_skill_level( skill_gun, 1 );
    // No need to aim
    tmp->recoil = 0;
    // It takes a while
    z->moves -= 150;

    if( z->ammo[ammo_type] <= 0 ) {
        se.id = "fire_gun";
        se.variant = "empty";
        if( one_in( 3 ) ) {
            se.description = _( "a chk!" );
            se.volume = 50;
            sounds::sound( se );

        } else if( one_in( 4 ) ) {
            se.description = _( "clank!" );
            se.volume = 60;
            sounds::sound( se );
        }
        return;
    }
    if( g->u.sees( *z ) ) {
        add_msg( m_warning, _( "The %s's ATGM tube fires!" ), z->name() );
    }

    detached_ptr<item> gun = item::spawn( "atgm_launcher" );
    gun->ammo_set( ammo_type, z->ammo[ ammo_type ] );
    tmp->set_primary_weapon( std::move( gun ) );
    int burst = std::max( tmp->primary_weapon().gun_get_mode( gun_mode_id( "AUTO" ) ).qty, 1 );

    z->ammo[ ammo_type ] -= ranged::fire_gun( *tmp, target->bub_pos(),
                            burst ) * tmp->primary_weapon().ammo_required();
}

bool mattack::searchlight( monster *z )
{

    int max_lamp_count = 3;
    if( z->get_hp() < z->get_hp_max() ) {
        max_lamp_count--;
    }
    if( z->get_hp() < z->get_hp_max() / 3 ) {
        max_lamp_count--;
    }

    const int zposx = z->bub_pos().x();
    const int zposy = z->bub_pos().y();

    //this searchlight is not initialized
    if( z->get_items().empty() ) {

        for( int i = 0; i < max_lamp_count; i++ ) {

            detached_ptr<item> settings = item::spawn( "processor", calendar::start_of_cataclysm );

            settings->set_var( "SL_PREFER_UP", "TRUE" );
            settings->set_var( "SL_PREFER_DOWN", "TRUE" );
            settings->set_var( "SL_PREFER_RIGHT", "TRUE" );
            settings->set_var( "SL_PREFER_LEFT", "TRUE" );

            for( const tripoint_bub_ms &dest : g->m.points_in_radius( z->bub_pos(), 24 ) ) {
                const monster *const mon = g->critter_at<monster>( dest );
                if( mon && mon->type->id == mon_turret_searchlight ) {
                    if( dest.x() < zposx ) {
                        settings->set_var( "SL_PREFER_LEFT", "FALSE" );
                    }
                    if( dest.x() > zposx ) {
                        settings->set_var( "SL_PREFER_RIGHT", "FALSE" );
                    }
                    if( dest.y() < zposy ) {
                        settings->set_var( "SL_PREFER_UP", "FALSE" );
                    }
                    if( dest.y() > zposy ) {
                        settings->set_var( "SL_PREFER_DOWN", "FALSE" );
                    }
                }
            }

            settings->set_var( "SL_SPOT_X", 0 );
            settings->set_var( "SL_SPOT_Y", 0 );

            z->add_item( std::move( settings ) );
        }
    }

    //battery charge from the generator is enough for some time of work
    if( calendar::once_every( 10_minutes ) ) {

        bool generator_ok = false;

        for( int x = zposx - 24; x < zposx + 24; x++ ) {
            for( int y = zposy - 24; y < zposy + 24; y++ ) {
                tripoint_bub_ms dest( x, y, z->bub_pos().z() );
                if( g->m.ter( dest ) == ter_str_id( "t_plut_generator" ) ) {
                    generator_ok = true;
                }
            }
        }

        if( !generator_ok ) {
            for( auto &settings : z->get_items() ) {
                settings->set_var( "SL_POWER", "OFF" );
            }

            return true;
        }
    }

    for( int i = 0; i < max_lamp_count; i++ ) {

        item &settings = *z->get_items()[i];

        if( settings.get_var( "SL_POWER" )  == "OFF" ) {
            return true;
        }

        const int rng_dir = rng( 0, 7 );

        if( one_in( 5 ) ) {

            if( !one_in( 5 ) ) {
                settings.set_var( "SL_DIR", rng_dir );
            } else {
                const int rng_pref = rng( 0, 3 ) * 2;
                if( rng_pref == 0 && settings.get_var( "SL_PREFER_UP" ) == "TRUE" ) {
                    settings.set_var( "SL_DIR", rng_pref );
                } else            if( rng_pref == 2 && settings.get_var( "SL_PREFER_RIGHT" ) == "TRUE" ) {
                    settings.set_var( "SL_DIR", rng_pref );
                } else            if( rng_pref == 4 && settings.get_var( "SL_PREFER_DOWN" ) == "TRUE" ) {
                    settings.set_var( "SL_DIR", rng_pref );
                } else            if( rng_pref == 6 && settings.get_var( "SL_PREFER_LEFT" ) == "TRUE" ) {
                    settings.set_var( "SL_DIR", rng_pref );
                }
            }
        }

        int x = zposx + settings.get_var( "SL_SPOT_X", 0 );
        int y = zposy + settings.get_var( "SL_SPOT_Y", 0 );
        int shift = 0;

        for( int i = 0; i < rng( 1, 2 ); i++ ) {

            if( !z->sees( g->u ) ) {
                shift = settings.get_var( "SL_DIR", shift );

                switch( shift ) {
                    case 0:
                        y--;
                        break;
                    case 1:
                        y--;
                        x++;
                        break;
                    case 2:
                        x++;
                        break;
                    case 3:
                        x++;
                        y++;
                        break;
                    case 4:
                        y++;
                        break;
                    case 5:
                        y++;
                        x--;
                        break;
                    case 6:
                        x--;
                        break;
                    case 7:
                        x--;
                        y--;
                        break;

                    default:
                        break;
                }

            } else {
                if( x < g->u.bub_pos().x() ) {
                    x++;
                }
                if( x > g->u.bub_pos().x() ) {
                    x--;
                }
                if( y < g->u.bub_pos().y() ) {
                    y++;
                }
                if( y > g->u.bub_pos().y() ) {
                    y--;
                }
            }

            if( rl_dist( point( x, y ), point( zposx, zposy ) ) > 50 ) {
                if( x > zposx ) {
                    x--;
                }
                if( x < zposx ) {
                    x++;
                }
                if( y > zposy ) {
                    y--;
                }
                if( y < zposy ) {
                    y++;
                }
            }
        }

        settings.set_var( "SL_SPOT_X", x - zposx );
        settings.set_var( "SL_SPOT_Y", y - zposy );

        g->m.add_field( tripoint_bub_ms( x, y, z->bub_pos().z() ), fd_spotlight, 1 );

    }

    return true;
}

bool mattack::flamethrower( monster *z )
{
    if( z->friendly ) {
        // TODO: handle friendly monsters
        return false;
    }
    if( z->type->monster_weapon && z->has_effect( effect_monster_disarmed ) ) {
        return false;
    }
    // TODO: that is always false!
    if( z->friendly != 0 ) {
        // Attacking monsters, not the player!
        // Flamethrower leaves dangerous trail
        auto res = creature_functions::auto_find_hostile_target( *z, { .range = 5, .trail = true, .area = 0 } );
        // Couldn't find any targets!
        if( !res ) {
            // Because that stupid oaf was in the way!
            const int boo_hoo = res.error();
            if( boo_hoo > 0 && g->u.sees( *z ) ) {
                add_msg( m_warning, vgettext( "Pointed in your direction, the %s emits an IFF warning beep.",
                                              "Pointed in your direction, the %s emits %d annoyed sounding beeps.",
                                              boo_hoo ),
                         z->name(), boo_hoo );
            }
            // Did reset before refactor, changed to match other turret behaviors
            return false;
        }
        flame( z, &res.value().get() );
        return true;
    }

    if( !within_visual_range( z, 5 ) ) {
        return false;
    }

    flame( z, &g->u );

    return true;
}

void mattack::flame( monster *z, Creature *target )
{
    int dist = rl_dist( z->bub_pos(), target->bub_pos() );

    map &here = get_map();
    if( target != &g->u ) {
        // friendly
        // It takes a while
        z->moves -= 500;
        if( !here.sees( z->bub_pos(), target->bub_pos(), dist ) ) {
            // shouldn't happen
            debugmsg( "mattack::flame invoked on invisible target" );
        }
        std::vector<tripoint_bub_ms> traj = here.find_clear_path( z->bub_pos(), target->bub_pos() );
        auto prev_point = z->bub_pos();
        for( auto &i : traj ) {
            if( here.obstructed_by_vehicle_rotation( prev_point, i ) ) {
                if( one_in( 2 ) ) {
                    i.x() = prev_point.x();
                } else {
                    i.y() = prev_point.y();
                }
            }

            // break out of attack if flame hits a wall
            // TODO: Z
            if( here.hit_with_fire( tripoint_bub_ms( i.xy(), z->bub_pos().z() ) ) ) {
                if( g->u.sees( i ) ) {
                    add_msg( _( "The tongue of flame hits the %s!" ),
                             here.tername( i.xy() ) );
                }
                return;
            }
            here.add_field( i, fd_fire, 1 );
            prev_point = i;
        }
        target->add_effect( effect_onfire, 8_turns, body_part_torso );

        return;
    }

    // It takes a while
    z->moves -= 500;
    if( !here.sees( z->bub_pos(), target->bub_pos(), dist + 1 ) ) {
        // shouldn't happen
        debugmsg( "mattack::flame invoked on invisible target" );
    }
    std::vector<tripoint_bub_ms> traj = here.find_clear_path( z->bub_pos(), target->bub_pos() );
    auto prev_point = z->bub_pos();
    for( auto &i : traj ) {
        if( here.obstructed_by_vehicle_rotation( prev_point, i ) ) {
            auto intervening = i;
            if( one_in( 2 ) ) {
                intervening.x() = prev_point.x();
            } else {
                intervening.y() = prev_point.y();
            }
            if( here.hit_with_fire( tripoint_bub_ms( intervening.xy(), z->bub_pos().z() ) ) ) {
                if( g->u.sees( i ) ) {
                    add_msg( _( "The tongue of flame hits the %s!" ),
                             here.tername( intervening.xy() ) );
                }
                return;
            }
        }
        // break out of attack if flame hits a wall
        if( here.hit_with_fire( tripoint_bub_ms( i.xy(), z->bub_pos().z() ) ) ) {
            if( g->u.sees( i ) ) {
                add_msg( _( "The tongue of flame hits the %s!" ),
                         here.tername( i.xy() ) );
            }
            return;
        }
        here.add_field( i, fd_fire, 1 );
        prev_point = i;
    }
    if( !target->uncanny_dodge() ) {
        target->add_effect( effect_onfire, 8_turns, body_part_torso );
    }
}

bool mattack::copbot( monster *z )
{
    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }

    // TODO: Make it recognize zeds as human, but ignore animals
    player *foe = dynamic_cast<player *>( target );
    bool sees_u = foe != nullptr && z->sees( *foe );
    bool cuffed = foe != nullptr && foe->primary_weapon().typeId() == itype_e_handcuffs;
    // Taze first, then ask questions (simplifies later checks for non-humans)
    if( !cuffed && is_adjacent( z, target, true ) ) {
        taze( z, target );
        return true;
    }

    if( rl_dist( z->bub_pos(), target->bub_pos() ) > 2 || foe == nullptr || !z->sees( *target ) ) {
        sound_event se;
        se.origin = z->bub_pos();
        se.category = sounds::sound_t::alert;
        se.from_monster = true;
        se.monfaction = z->faction.id();
        se.faction = faction_id( "no_faction" );
        if( one_in( 3 ) ) {
            se.id = "speech";
            se.variant = z->type->id.str();
            se.volume = 80;
            if( sees_u ) {
                if( foe->unarmed_attack() ) {
                    se.description = _( "a robotic voice boom, \"Citizen, Halt!\"" );
                    sounds::sound( se );
                } else if( !cuffed ) {
                    se.description = _( "a robotic voice boom, \"Please put down your weapon.\"" );
                    sounds::sound( se );
                }
            } else {
                se.description = _( "a robotic voice boom, \"Come out with your hands up!\"" );
                sounds::sound( se );
            }
        } else {
            se.id = "environment";
            se.variant = "police_siren";
            se.description = _( "a police siren, whoop WHOOP" );
            se.volume = 100;
            sounds::sound( se );
        }
        return true;
    }

    // If cuffed don't attack the player, unless the bot is damaged
    // presumably because of the player's actions
    if( z->get_hp() == z->get_hp_max() ) {
        z->anger = 1;
    } else {
        z->anger = z->type->agro;
    }

    return true;
}

bool mattack::chickenbot( monster *z )
{
    if( z->type->monster_weapon && z->has_effect( effect_monster_disarmed ) ) {
        return false;
    }
    int mode = 0;
    Creature *target;
    if( z->friendly == 0 ) {
        target = z->attack_target();
        if( target == nullptr ) {
            return false;
        }
    } else {
        // Chickenbot uses M4 and 40mm grenades - bullets don't leave trail
        auto res = creature_functions::auto_find_hostile_target( *z, { .range = 38, .trail = false, .area = 0 } );
        if( !res ) {
            const int boo_hoo = res.error();
            if( boo_hoo > 0 && g->u.sees( *z ) ) { // because that stupid oaf was in the way!
                add_msg( m_warning, vgettext( "Pointed in your direction, the %s emits an IFF warning beep.",
                                              "Pointed in your direction, the %s emits %d annoyed sounding beeps.",
                                              boo_hoo ),
                         z->name(), boo_hoo );
            }
            return false;
        }
        target = &res.value().get();
    }

    int cap = target->power_rating() - 1;
    monster *mon = dynamic_cast< monster * >( target );
    // Their attitude to us and not ours to them, so that bobcats won't get gunned down
    // Only monster-types for now - assuming humans are smart enough not to make it obvious
    // Unless damaged - then everything is hostile
    if( z->get_hp() <= z->get_hp_max() ||
        ( mon != nullptr && mon->attitude_to( *z ) == Attitude::A_HOSTILE ) ) {
        cap += 2;
    }

    int dist = rl_dist( z->bub_pos(), target->bub_pos() );
    int player_dist = rl_dist( target->bub_pos(), g->u.bub_pos() );
    if( dist == 1 && one_in( 2 ) ) {
        // Use tazer at point-blank range, and even then, not continuously.
        mode = 1;
    } else if( ( z->friendly == 0 || player_dist >= 6 ) &&
               // Avoid shooting near player if we're friendly.
               ( dist >= 12 || ( g->u.in_vehicle && dist >= 6 ) ) ) {
        // Only use at long range, unless player is in a vehicle, then tolerate closer targeting.
        mode = 3;
    } else if( dist >= 4 ) {
        // Don't use machine gun at very close range, under the assumption that targets at that range can dodge?
        mode = 2;
    }

    // No attacks were valid!
    if( mode == 0 ) {
        return false;
    }

    if( mode > cap ) {
        mode = cap;
    }
    switch( mode ) {
        case 0:
        case 1:
            // If we downgraded to taze, but are out of range, don't act.
            if( dist <= 1 ) {
                taze( z, target );
            }
            break;
        case 2:
            if( dist <= 20 ) {
                rifle( z, target );
            }
            break;
        case 3:
            if( dist <= 38 ) {
                frag( z, target );
            }
            break;
        default:
            // Weak stuff, shouldn't bother with
            return false;
    }

    return true;
}

bool mattack::multi_robot( monster *z )
{
    if( z->type->monster_weapon && z->has_effect( effect_monster_disarmed ) ) {
        return false;
    }
    int mode = 0;
    Creature *target;
    if( z->friendly == 0 ) {
        target = z->attack_target();
        if( target == nullptr ) {
            return false;
        }
    } else {
        auto res = creature_functions::auto_find_hostile_target( *z, { .range = 48, .trail = true, .area = 0 } );
        if( !res ) {
            const int boo_hoo = res.error();
            if( boo_hoo > 0 && g->u.sees( *z ) ) { // because that stupid oaf was in the way!
                add_msg( m_warning, vgettext( "Pointed in your direction, the %s emits an IFF warning beep.",
                                              "Pointed in your direction, the %s emits %d annoyed sounding beeps.",
                                              boo_hoo ),
                         z->name(), boo_hoo );
            }
            return false;
        }
        target = &res.value().get();
    }

    int cap = target->power_rating();
    monster *mon = dynamic_cast< monster * >( target );
    // Their attitude to us and not ours to them, so that bobcats won't get gunned down
    // Only monster-types for now - assuming humans are smart enough not to make it obvious
    // Unless damaged - then everything is hostile
    if( z->get_hp() <= z->get_hp_max() ||
        ( mon != nullptr && mon->attitude_to( *z ) == Attitude::A_HOSTILE ) ) {
        cap += 2;
    }

    int dist = rl_dist( z->bub_pos(), target->bub_pos() );
    if( dist <= 15 ) {
        mode = 1;
    } else if( dist <= 30 ) {
        mode = 2;
    } else if( ( target == &g->u && g->u.in_vehicle ) ||
               z->friendly != 0 ||
               cap > 4 ) {
        // Primary only kicks in if you're in a vehicle or are big enough to be mistaken for one.
        // Or if you've hacked it so the turret's on your side.  ;-)
        if( dist < 50 ) {
            // Enforced max-range of 50.
            mode = 5;
            cap = 5;
        }
    }

    // No attacks were valid!
    if( mode == 0 ) {
        return false;
    }

    if( mode > cap ) {
        mode = cap;
    }
    switch( mode ) {
        case 1:
            if( dist <= 15 ) {
                rifle( z, target );
            }
            break;
        case 2:
            if( dist <= 30 ) {
                frag( z, target );
            }
            break;
        case 5:
            if( dist <= 50 ) {
                atgm( z, target );
            }
            break;
        default:
            // Weak stuff, shouldn't bother with
            return false;
    }

    return true;
}
