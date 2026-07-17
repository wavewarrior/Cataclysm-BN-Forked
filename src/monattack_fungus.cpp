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


static bodypart_str_id body_part_hit_by_plant()
{
    bodypart_str_id hit;
    if( one_in( 2 ) ) {
        hit = body_part_leg_l;
    } else {
        hit = body_part_leg_r;
    }
    if( one_in( 4 ) ) {
        hit = body_part_torso;
    } else if( one_in( 2 ) ) {
        if( one_in( 2 ) ) {
            hit = body_part_foot_l;
        } else {
            hit = body_part_foot_r;
        }
    }
    return hit;
}

bool mattack::fungus( monster *z )
{
    // TODO: Infect NPCs?
    // It takes a while
    z->moves -= 200;

    //~ the sound of a fungus releasing spores
    sounds::sound( z->bub_pos(), 10, sounds::sound_t::combat, _( "Pouf!" ), false, "misc", "puff" );
    if( g->u.sees( *z ) ) {
        add_msg( m_warning, _( "Spores are released from the %s!" ), z->name() );
    }

    bool on_fungus = g->m.has_flag_ter( "FUNGUS", z->bub_pos() );
    int radius = one_in( 4 ) ? 2 : 1;
    double spore_chance = ( on_fungus ? 0.5f : 0.2f ) / ( ( radius + 1 ) * ( radius + 1 ) );
    fungal_effects fe( *g, g->m );
    for( const tripoint_bub_ms &sporep : g->m.points_in_radius( z->bub_pos(), radius ) ) {
        if( sporep == z->bub_pos() ) {
            continue;
        }
        const int dist = rl_dist( z->bub_pos(), sporep );
        if( !one_in( dist ) ||
            g->m.impassable( sporep ) ||
            ( dist > 1 && !g->m.clear_path( z->bub_pos(), sporep, 2, 1, 10 ) ) ) {
            continue;
        }

        fe.fungalize( sporep, z, spore_chance );
    }

    return true;
}

bool mattack::fungus_advanced( monster *z )
{
    // TODO: Infect NPCs?
    // It takes a while
    z->moves -= 200;

    //~ the sound of a fungus releasing spores
    sounds::sound( z->bub_pos(), 10, sounds::sound_t::combat, _( "Pouf!" ), false, "misc", "puff" );
    if( g->u.sees( *z ) ) {
        add_msg( m_warning, _( "Spores are released from the %s!" ), z->name() );
    }

    // Calculating spore spawn chance
    int radius = one_in( 4 ) ? 2 : 1;
    double spore_chance = fungal_opt.spore_chance;
    double creatures_threshold = static_cast<float>( fungal_opt.advanced_creatures_threshold );
    int creatures_nearby = static_cast<int>( g->num_creatures() );
    if( creatures_nearby > fungal_opt.advanced_creatures_threshold ) {
        // Number of creatures in the bubble and the resulting average number of spores per "Pouf!"
        // (assuming that spore_chance is 0.25 and creatures_threshold is 25:
        // 0-25: 2
        // 50  : 0.5
        // 75  : 0.22
        // 100 : 0.125
        // Assuming all creatures in the bubble were fungaloids (unlikely), the average number of spores per generation:
        // 25  : 50
        // 50  : 25
        // 75  : 17
        // 100 : 13
        spore_chance *= ( creatures_threshold / creatures_nearby ) *
                        ( creatures_threshold /
                          creatures_nearby );
    }
    if( radius == 2 ) {
        const double old_area = ( ( 2 * radius + 1 ) * ( 2 * radius + 1 ) ) - 1;
        radius++;
        const double new_area = ( ( 2 * radius + 1 ) * ( 2 * radius + 1 ) ) - 1;
        spore_chance *= old_area / new_area;
    }

    // Applying spore launch in certain radius
    fungal_effects fe( *g, g->m );
    for( const tripoint_bub_ms &sporep : g->m.points_in_radius( z->bub_pos(), radius ) ) {
        if( sporep == z->bub_pos() ) {
            continue;
        }
        const int dist = rl_dist( z->bub_pos(), sporep );
        if( !one_in( dist ) ||
            g->m.impassable( sporep ) ||
            ( dist > 1 && !g->m.clear_path( z->bub_pos(), sporep, 2, 1, 10 ) ) ) {
            continue;
        }

        fe.fungalize( sporep, z, spore_chance );
    }

    return true;
}

bool mattack::fungus_corporate( monster *z )
{
    return fungus( z );
}

bool mattack::fungus_haze( monster *z )
{
    //~ That spore sound again
    sounds::sound( z->bub_pos(), 10, sounds::sound_t::combat, _( "Pouf!" ), true, "misc", "puff" );
    if( g->u.sees( *z ) ) {
        add_msg( m_info, _( "The %s pulses, and fresh fungal material bursts forth." ), z->name() );
    }
    z->moves -= 150;
    for( const tripoint_bub_ms &dest : g->m.points_in_radius( z->bub_pos(), 3 ) ) {
        g->m.add_field( dest, fd_fungal_haze, rng( 1, 2 ) );
    }

    return true;
}

bool mattack::fungus_big_blossom( monster *z )
{
    bool firealarm = false;
    const auto u_see = g->u.sees( *z );
    // Fungal fire-suppressor! >:D
    for( const tripoint_bub_ms &dest : g->m.points_in_radius( z->bub_pos(), 6 ) ) {
        if( g->m.get_field_intensity( dest, fd_fire ) != 0 ) {
            firealarm = true;
        }
        if( firealarm ) {
            g->m.remove_field( dest, fd_fire );
            g->m.remove_field( dest, fd_smoke );
            g->m.add_field( dest, fd_fungal_haze, 3 );
        }
    }
    // Special effects handled outside the loop
    if( firealarm ) {
        if( u_see ) {
            // Sucks up all the smoke
            add_msg( m_warning, _( "The %s suddenly inhales!" ), z->name() );
        }
        //~Sound of a giant fungal blossom inhaling
        sounds::sound( z->bub_pos(), 20, sounds::sound_t::combat, _( "WOOOSH!" ), true, "misc", "inhale" );
        if( u_see ) {
            add_msg( m_bad, _( "The %s discharges an immense flow of spores, smothering the flames!" ),
                     z->name() );
        }
        //~Sound of a giant fungal blossom blowing out the dangerous fire!
        sounds::sound( z->bub_pos(), 20, sounds::sound_t::combat, _( "POUFF!" ), true, "misc", "exhale" );
        return true;
    } else {
        // No fire detected, routine haze-emission
        //~ That spore sound, much louder
        sounds::sound( z->bub_pos(), 15, sounds::sound_t::combat, _( "POUF." ), true, "misc", "puff" );
        if( u_see ) {
            add_msg( m_info, _( "The %s pulses, and fresh fungal material bursts forth!" ), z->name() );
        }
        z->moves -= 150;
        for( const tripoint_bub_ms &dest : g->m.points_in_radius( z->bub_pos(), 12 ) ) {
            g->m.add_field( dest, fd_fungal_haze, rng( 1, 2 ) );
        }
    }

    return true;
}

bool mattack::fungus_inject( monster *z )
{
    // For faster copy+paste
    Creature *target = &g->u;
    if( rl_dist( z->bub_pos(), g->u.bub_pos() ) > 1 ) {
        return false;
    }

    if( g->u.has_trait( trait_THRESH_MARLOSS ) || g->u.has_trait( trait_THRESH_MYCUS ) ) {
        z->friendly = 1;
        return true;
    }
    if( ( g->u.has_trait( trait_MARLOSS ) ) && ( g->u.has_trait( trait_MARLOSS_BLUE ) ) &&
        !g->u.crossed_threshold() ) {
        add_msg( m_info, _( "The %s seems to wave you toward the tower…" ), z->name() );
        z->anger = 0;
        return true;
    }
    if( z->friendly ) {
        // TODO: attack other creatures, not just g->u, for now just skip the code below as it
        // only attacks g->u but the monster is friendly.
        return true;
    }
    add_msg( m_warning, _( "The %s jabs at you with a needlelike point!" ), z->name() );
    z->moves -= 150;

    if( g->u.uncanny_dodge() ) {
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
    int dam = rng( 5, 11 );
    dam = g->u.deal_damage( z, hit, damage_instance( DT_CUT, dam ) ).total_damage();

    if( dam > 0 ) {
        //~ 1$s is monster name, 2$s bodypart in accusative
        add_msg( m_bad, _( "The %1$s sinks its point into your %2$s!" ), z->name(),
                 body_part_name_accusative( hit->token ) );

        if( one_in( 10 - dam ) ) {
            g->u.add_effect( effect_fungus, 10_minutes, bodypart_str_id::NULL_ID() );
            add_msg( m_warning, _( "You feel thousands of live spores pumping into you…" ) );
        }
    } else {
        //~ 1$s is monster name, 2$s bodypart in accusative
        add_msg( _( "The %1$s strikes your %2$s, but your armor protects you." ), z->name(),
                 body_part_name_accusative( hit->token ) );
    }

    target->on_hit( z, hit );
    g->u.check_dead_state();

    return true;
}

bool mattack::fungus_bristle( monster *z )
{
    if( g->u.has_trait( trait_THRESH_MARLOSS ) || g->u.has_trait( trait_THRESH_MYCUS ) ) {
        z->friendly = 1;
    }
    if( ( g->u.has_trait( trait_MARLOSS ) ) && ( g->u.has_trait( trait_MARLOSS_BLUE ) ) &&
        !g->u.crossed_threshold() && rl_dist( z->bub_pos(), g->u.bub_pos() ) < 6 ) {
        add_msg( m_info, _( "The %s recedes, as if anticipating your arrival…" ), z->name() );
        z->no_corpse_quiet = true;
        z->no_extra_death_drops = true;
        z->die( nullptr );
        return true;
    }
    Creature *target = z->attack_target();
    if( target == nullptr ||
        !is_adjacent( z, target, true ) ||
        !z->sees( *target ) ) {
        return false;
    }

    auto msg_type = target == &g->u ? m_warning : m_neutral;

    add_msg( msg_type, _( "%1$s swipes at %2$s with a barbed tendril!" ),
             z->disp_name( false, true ), target->disp_name() );
    z->moves -= 150;

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
    int dam = rng( 7, 16 );
    dam = target->deal_damage( z, hit, damage_instance( DT_CUT, dam ) ).total_damage();

    if( dam > 0 ) {
        //~ 1$s is monster name, 2$s bodypart in accusative
        target->add_msg_if_player( m_bad, _( "The %1$s sinks several needlelike barbs into your %2$s!" ),
                                   z->name(),
                                   body_part_name_accusative( hit->token ) );

        if( one_in( 15 - dam ) ) {
            target->add_effect( effect_fungus, 20_minutes, bodypart_str_id::NULL_ID() );
            target->add_msg_if_player( m_warning,
                                       _( "You feel thousands of live spores pumping into you…" ) );
        }
    } else {
        //~ 1$s is monster name, 2$s bodypart in accusative
        target->add_msg_if_player( _( "The %1$s slashes your %2$s, but your armor protects you." ),
                                   z->name(),
                                   body_part_name_accusative( hit->token ) );
    }

    target->on_hit( z, hit );

    return true;
}

bool mattack::fungus_growth( monster *z )
{
    // Young fungaloid growing into an adult
    if( g->u.sees( *z ) ) {
        add_msg( m_warning, _( "The %s grows into an adult!" ),
                 z->name() );
    }

    z->poly( mon_fungaloid );

    return false;
}

bool mattack::fungus_sprout( monster *z )
{
    Character &player_character = get_player_character();
    // To avoid map shift weirdness
    bool push_player = false;
    for( const tripoint_bub_ms &dest : g->m.points_in_radius( z->bub_pos(), 1 ) ) {
        if( player_character.bub_pos() == dest ) {
            push_player = true;
        }
        if( monster *const wall = g->place_critter_at( mon_fungal_wall, dest ) ) {
            wall->make_ally( *z );
        }
    }

    if( push_player ) {
        const units::angle angle = coord_to_angle( z->bub_pos(), player_character.bub_pos() );
        add_msg( m_bad, _( "You're shoved away as a fungal wall grows!" ) );
        g->fling_creature( &player_character, angle, rng( 10, 50 ) );
    }

    return true;
}

bool mattack::fungus_fortify( monster *z )
{
    if( z->friendly ) {
        // TODO: handle friendly monsters
        return false;
    }
    Creature *target = &g->u;
    bool mycus = false;
    bool peaceful = true;
    //No nifty support effects.  Yet.  This lets it rebuild hedges.
    if( g->u.has_trait( trait_THRESH_MARLOSS ) || g->u.has_trait( trait_THRESH_MYCUS ) ) {
        mycus = true;
    }
    if( ( g->u.has_trait( trait_MARLOSS ) ) && ( g->u.has_trait( trait_MARLOSS_BLUE ) ) &&
        !g->u.crossed_threshold() && !mycus ) {
        // You have the other two.  Is it really necessary for us to fight?
        add_msg( m_info, _( "The %s spreads its tendrils.  It seems as though it's expecting you…" ),
                 z->name() );
        if( rl_dist( z->bub_pos(), g->u.bub_pos() ) < 3 ) {
            if( query_yn( _( "The tower extends and aims several tendrils from its depths.  Hold still?" ) ) ) {
                add_msg( m_warning,
                         _( "The %s works several tendrils into your arms, legs, torso, and even neck…" ),
                         z->name() );
                g->u.hurtall( 1, z );
                add_msg( m_warning,
                         _( "You see a clear golden liquid pump through the tendrils--and then lose consciousness." ) );
                g->u.unset_mutation( trait_MARLOSS );
                g->u.unset_mutation( trait_MARLOSS_BLUE );
                g->u.set_mutation( trait_THRESH_MARLOSS );
                g->m.ter_set( g->u.bub_pos(),
                              t_marloss ); // We only show you the door.  You walk through it on your own.
                g->memorial().add(
                    pgettext( "memorial_male", "Was shown to the Marloss Gateway." ),
                    pgettext( "memorial_female", "Was shown to the Marloss Gateway." ) );
                g->u.add_msg_if_player( m_good,
                                        _( "You wake up in a marloss bush.  Almost *cradled* in it, actually, as though it grew there for you." ) );
                g->u.add_msg_if_player( m_good,
                                        //~ Beginning to hear the Mycus while conscious: this is it speaking
                                        _( "assistance, on an arduous quest.  unity.  together we have reached the door.  now to pass through…" ) );
                return true;
            } else {
                peaceful = false; // You declined the offer.  Fight!
            }
        }
    } else {
        peaceful = false; // You weren't eligible.  Fight!
    }

    bool fortified = false;
    bool push_player = false; // To avoid map shift weirdness
    for( const tripoint_bub_ms &dest : g->m.points_in_radius( z->bub_pos(), 1 ) ) {
        if( g->u.bub_pos() == dest ) {
            push_player = true;
        }
        if( monster *const wall = g->place_critter_at( mon_fungal_hedgerow, dest ) ) {
            wall->make_ally( *z );
            fortified = true;
        }
    }
    if( push_player ) {
        add_msg( m_bad, _( "You're shoved away as a fungal hedgerow grows!" ) );
        g->fling_creature( &g->u, coord_to_angle( z->bub_pos(), g->u.bub_pos() ), rng( 10, 50 ) );
    }
    if( fortified || mycus || peaceful ) {
        return true;
    }

    // TODO: De-playerize the whole block
    const int dist = rl_dist( z->bub_pos(), g->u.bub_pos() );
    if( dist >= 12 ) {
        return false;
    }


    if( dist > 3 ) {
        // Oops, can't reach. ):
        // How's about we spawn more tendrils? :)
        // Aimed at the player, too?  Sure!
        const tripoint_bub_ms hit_pos = target->bub_pos() + point_rel_ms( rng( -1, 1 ), rng( -1, 1 ) );
        if( hit_pos == target->bub_pos() && !target->uncanny_dodge() ) {
            const bodypart_str_id hit = body_part_hit_by_plant();
            //~ %s is bodypart name in accusative.
            add_msg( m_bad, _( "A fungal tendril bursts forth from the earth and pierces your %s!" ),
                     body_part_name_accusative( hit ) );
            g->u.deal_damage( z, hit.id(), damage_instance( DT_CUT, rng( 5, 11 ) ) );
            g->u.check_dead_state();
            // Probably doesn't have spores available *just* yet.  Let's be nice.
        } else if( monster *const tendril = g->place_critter_at( mon_fungal_tendril, hit_pos ) ) {
            add_msg( m_bad, _( "A fungal tendril bursts forth from the earth!" ) );
            tendril->make_ally( *z );
        }
        return true;
    }

    add_msg( m_warning, _( "The %s takes aim, and spears at you with a massive tendril!" ),
             z->name() );
    z->moves -= 150;

    if( g->u.uncanny_dodge() ) {
        return true;
    }
    // Can we dodge the attack? Uses player dodge function % chance (melee.cpp)
    if( dodge_check( z, target ) ) {
        target->add_msg_player_or_npc( _( "You dodge it!" ),
                                       _( "<npcname> dodges it!" ) );
        target->on_dodge( z, z->type->melee_skill * 2 );
        return true;
    }

    // TODO: 21 damage with no chance to critical isn't scary
    const bodypart_id hit = target->get_random_body_part();
    int dam = rng( 15, 21 );
    dam = g->u.deal_damage( z, hit, damage_instance( DT_STAB, dam ) ).total_damage();

    if( dam > 0 ) {
        //~ 1$s is monster name, 2$s bodypart in accusative
        add_msg( m_bad, _( "The %1$s sinks its point into your %2$s!" ), z->name(),
                 body_part_name_accusative( hit->token ) );
        g->u.add_effect( effect_fungus, 40_minutes, bodypart_str_id::NULL_ID() );
        add_msg( m_warning, _( "You feel millions of live spores pumping into you…" ) );
    } else {
        //~ 1$s is monster name, 2$s bodypart in accusative
        add_msg( _( "The %1$s strikes your %2$s, but your armor protects you." ), z->name(),
                 body_part_name_accusative( hit->token ) );
    }

    target->on_hit( z, hit );
    g->u.check_dead_state();
    return true;
}

bool mattack::impale( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }
    Creature *target = z->attack_target();
    if( target == nullptr || !is_adjacent( z, target, false ) ) {
        return false;
    }

    z->moves -= 80;
    bool uncanny = target->uncanny_dodge();
    if( uncanny || dodge_check( z, target ) ) {
        auto msg_type = target == &g->u ? m_warning : m_info;
        target->add_msg_player_or_npc( msg_type, _( "The %s lunges at you, but you dodge!" ),
                                       _( "The %s lunges at <npcname>, but they dodge!" ),
                                       z->name() );
        if( !uncanny ) {
            target->on_dodge( z, z->type->melee_skill * 2 );
        }

        return true;
    }

    int dam = target->deal_damage( z, bodypart_id( "torso" ), damage_instance( DT_STAB, rng( 10, 20 ),
                                   rng( 5, 15 ),
                                   .5 ) ).total_damage();
    if( dam > 0 ) {
        auto msg_type = target == &g->u ? m_bad : m_info;
        target->add_msg_player_or_npc( msg_type,
                                       //~ 1$s is monster name, 2$s bodypart in accusative
                                       _( "The %1$s impales your torso!" ),
                                       //~ 1$s is monster name, 2$s bodypart in accusative
                                       _( "The %1$s impales <npcname>'s torso!" ),
                                       z->name() );

        target->on_hit( z, bodypart_id( "torso" ) );
        if( one_in( 60 / ( dam + 20 ) ) ) {
            target->add_effect( effect_bleed, rng( 75_turns, 125_turns ), body_part_torso );

        }

        if( rng( 0, 200 + dam ) > 100 ) {
            target->add_effect( effect_downed, 3_turns );
        }
        z->moves -= 80; //Takes extra time for the creature to pull out the protrusion
    } else {
        target->add_msg_player_or_npc(
            _( "The %1$s tries to impale your torso, but fails to penetrate your armor!" ),
            _( "The %1$s tries to impale <npcname>'s torso, but fails to penetrate their armor!" ),
            z->name() );
    }

    target->check_dead_state();

    return true;
}

bool mattack::dermatik( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ||
        !is_adjacent( z, target, true ) ||
        !z->sees( *target ) ) {
        return false;
    }

    if( target->uncanny_dodge() ) {
        return true;
    }
    player *foe = dynamic_cast< player * >( target );
    if( foe == nullptr ) {
        return true; // No implanting monsters for now
    }
    // Can we dodge the attack? Uses player dodge function % chance (melee.cpp)
    if( dodge_check( z, target ) ) {
        if( target == &g->u ) {
            add_msg( _( "The %s tries to land on you, but you dodge." ), z->name() );
        }
        z->stumble();
        target->on_dodge( z, z->type->melee_skill * 2 );
        return true;
    }

    // Can we swat the bug away?
    int dodge_roll = z->dodge_roll();
    ///\EFFECT_MELEE increases chance to deflect dermatik attack

    ///\EFFECT_UNARMED increases chance to deflect dermatik attack
    int swat_skill = ( foe->get_skill_level( skill_melee ) + foe->get_skill_level(
                           skill_unarmed ) * 2 ) / 3;
    int player_swat = dice( swat_skill, 10 );
    if( foe->has_trait( trait_TAIL_CATTLE ) ) {
        target->add_msg_if_player( _( "You swat at the %s with your tail!" ), z->name() );
        ///\EFFECT_DEX increases chance of deflecting dermatik attack with TAIL_CATTLE

        ///\EFFECT_UNARMED increases chance of deflecting dermatik attack with TAIL_CATTLE
        player_swat += ( ( foe->dex_cur + foe->get_skill_level( skill_unarmed ) ) / 2 );
    }
    if( player_swat > dodge_roll ) {
        target->add_msg_if_player( _( "The %s lands on you, but you swat it off." ), z->name() );
        if( z->get_hp() >= z->get_hp_max() / 2 ) {
            z->apply_damage( &g->u, bodypart_id( "torso" ), 1 );
            z->check_dead_state();
        }
        if( player_swat > dodge_roll * 1.5 ) {
            z->stumble();
        }
        return true;
    }

    // Can the bug penetrate our armor?
    const bodypart_id targeted = target->get_random_body_part();
    if( 4 < g->u.get_armor_cut( targeted ) / 3 ) {
        //~ 1$s monster name(dermatik), 2$s bodypart name in accusative.
        target->add_msg_if_player( _( "The %1$s lands on your %2$s, but can't penetrate your armor." ),
                                   z->name(), body_part_name_accusative( targeted->token ) );
        z->moves -= 150; // Attempted laying takes a while
        return true;
    }

    // Success!
    z->moves -= 500; // Successful laying takes a long time
    //~ 1$s monster name(dermatik), 2$s bodypart name in accusative.
    target->add_msg_if_player( m_bad, _( "The %1$s sinks its ovipositor into your %2$s!" ),
                               z->name(),
                               body_part_name_accusative( targeted->token ) );
    if( !foe->has_trait( trait_PARAIMMUNE ) || !foe->has_trait( trait_ACIDBLOOD ) ) {
        foe->add_effect( effect_dermatik, 1_turns, targeted.id() );
        g->events().send<event_type::dermatik_eggs_injected>( foe->getID() );
    }

    return true;
}

bool mattack::dermatik_growth( monster *z )
{
    // Dermatik larva growing into an adult
    if( g->u.sees( *z ) ) {
        add_msg( m_warning, _( "The %s dermatik larva grows into an adult!" ),
                 z->name() );
    }
    z->poly( mon_dermatik );

    return false;
}

bool mattack::fungal_trail( monster *z )
{
    fungal_effects fe( *g, g->m );
    fe.spread_fungus( z->bub_pos() );
    return false;
}

bool mattack::plant( monster *z )
{
    fungal_effects fe( *g, g->m );

    // If terrain already infested there is chance for spore to become fungal stalk
    const bool is_fungi = g->m.has_flag_ter( ter_bitflags::TFLAG_FUNGUS, z->bub_pos() );
    if( fungal_opt.young_allowed && is_fungi ) {
        const int base_chance = fungal_opt.young_spawn_base_rate;
        const int divider = fungal_opt.young_spawn_bubble_creatures_divider;
        if( one_in( base_chance + static_cast<int>( g->num_creatures() / divider ) ) ) {
            add_msg( _( "The %s takes seed and becomes a young fungaloid!" ),
                     z->name() );
            z->poly( mon_fungaloid_young );
            z->mod_moves( -to_moves<int>( 10_seconds ) ); // It takes a while
            return false;
        }
    }

    // Spore blows up and infest some terrain
    if( g->u.sees( *z ) ) {
        add_msg( _( "The %s suddenly splits and bursts!" ),
                 z->name() );
    }
    z->set_hp( 0 );
    fe.spread_fungus( z->bub_pos() );
    for( const tripoint_bub_ms &p : closest_points_first( z->bub_pos(), 1 ) ) {
        if( !one_in( 3 ) ) {
            fe.fungalize( p, z );
        }
    }

    return true;
}
