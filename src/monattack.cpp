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

bool mattack::none( monster * )
{
    return true;
}

bool mattack::eat_crop( monster *z )
{
    for( const auto &p : g->m.points_in_radius( z->bub_pos(), 1 ) ) {
        if( g->m.has_flag( "PLANT", p ) && one_in( 4 ) ) {
            g->m.furn_set( p, furn_str_id( g->m.furn( p )->plant->base ) );
            g->m.i_clear( p );
            return true;
        }
    }
    return true;
}

bool mattack::eat_food( monster *z )
{
    for( const auto &p : g->m.points_in_radius( z->bub_pos(), 1 ) ) {
        //Protect crop seeds from carnivores, give omnivores eat_crop special also
        if( g->m.has_flag( "PLANT", p ) ) {
            continue;
        }
        // Don't snap up food RIGHT under the player's nose.
        if( z->friendly && rl_dist( g->u.bub_pos(), p ) <= 2 ) {
            continue;
        }
        auto items = g->m.i_at( p );
        for( auto &item : items ) {
            //Fun limit prevents scavengers from eating feces
            if( !item->is_food() || item->get_comestible_fun() < -20 ) {
                continue;
            }
            //Don't eat own eggs
            if( z->type->baby_egg != item->type->get_id() ) {
                int consumed = 1;
                if( item->count_by_charges() ) {
                    g->m.use_charges( p, 0, item->type->get_id(), consumed );
                } else {
                    g->m.use_amount( p, 0, item->type->get_id(), consumed );
                }
                return true;
            }
        }
    }
    return true;
}

bool mattack::antqueen( monster *z )
{
    std::vector<tripoint_bub_ms> egg_points;
    std::vector<monster *> ants;
    // Count up all adjacent tiles the contain at least one egg.
    for( const auto &dest : g->m.points_in_radius( z->bub_pos(), 2 ) ) {
        if( g->m.impassable( dest ) ) {
            continue;
        }

        if( monster *const mon = g->critter_at<monster>( dest ) ) {
            if( mon->type->default_faction == mfaction_id( "ant" ) && mon->type->upgrades ) {
                ants.push_back( mon );
            }

            continue;
        }

        if( g->is_empty( dest ) && g->m.has_items( dest ) ) {
            for( auto &i : g->m.i_at( dest ) ) {
                if( i->typeId() == itype_ant_egg ) {
                    egg_points.push_back( dest );
                    // Done looking at this tile
                    break;
                }
            }
        }
    }

    if( !ants.empty() ) {
        // It takes a while
        z->moves -= 100;
        monster *ant = random_entry( ants );
        if( g->u.sees( *z ) && g->u.sees( *ant ) ) {
            add_msg( m_warning, _( "The %1$s feeds an %2$s and it grows!" ), z->name(),
                     ant->name() );
        }
        ant->poly( ant->type->upgrade_into );
    } else if( egg_points.empty() ) {
        // There's no eggs nearby--lay one.
        if( g->u.sees( *z ) ) {
            add_msg( _( "The %s lays an egg!" ), z->name() );
        }
        g->m.spawn_item( z->bub_pos(), "ant_egg", 1, 0, calendar::turn );
    } else {
        // There are eggs nearby.  Let's hatch some.
        // It takes a while
        z->moves -= 20 * egg_points.size();
        if( g->u.sees( *z ) ) {
            add_msg( m_warning, _( "The %s tends nearby eggs, and they hatch!" ), z->name() );
        }
        for( const tripoint_bub_ms &egg_pos : egg_points ) {
            map_stack items = g->m.i_at( egg_pos );
            for( map_stack::iterator it = items.begin(); it != items.end(); ) {
                if( ( *it )->typeId() != itype_ant_egg ) {
                    ++it;
                    continue;
                }
                const mtype_id &mt = z->type->id == mon_ant_acid_queen ? mon_ant_acid_larva : mon_ant_larva;
                // Max one hatch per tile
                if( monster *const mon = g->place_critter_at( mt, egg_pos ) ) {
                    mon->make_ally( *z );
                    it = items.erase( it );
                    break;
                }
            }
        }
    }

    return true;
}

bool mattack::shriek( monster *z )
{
    Creature *target = z->attack_target();
    if( target == nullptr ||
        rl_dist( z->bub_pos(), target->bub_pos() ) > 4 ||
        !z->sees( *target ) ) {
        return false;
    }

    // It takes a while
    z->moves -= 240;
    sound_event se;
    se.origin = z->bub_pos();
    se.volume = 120;
    se.category = sounds::sound_t::alert;
    se.description = _( "a terrible shriek!" );
    se.from_monster = true;
    se.monfaction = z->faction.id();
    se.id = "shout";
    se.variant = "shriek";
    sounds::sound( se );
    return true;
}

bool mattack::shriek_alert( monster *z )
{
    if( !z->can_act() || z->has_effect( effect_shrieking ) ) {
        return false;
    }

    Creature *target = z->attack_target();

    if( target == nullptr || rl_dist( z->bub_pos(), target->bub_pos() ) > 15 ||
        !z->sees( *target ) ) {
        return false;
    }

    if( g->u.sees( *z ) ) {
        add_msg( _( "The %s begins shrieking!" ), z->name() );
    }

    z->moves -= 150;
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
    z->add_effect( effect_shrieking, 1_minutes );

    return true;
}

bool mattack::shriek_stun( monster *z )
{
    if( !z->can_act() || !z->has_effect( effect_shrieking ) ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }

    int dist = rl_dist( z->bub_pos(), target->bub_pos() );
    // Currently the cone is 2D, so don't use it for 3D attacks
    if( dist > 7 ||
        z->bub_pos().z() != target->bub_pos().z() ||
        !z->sees( *target ) ) {
        return false;
    }

    units::angle target_angle = coord_to_angle( z->bub_pos(), target->bub_pos() );
    units::angle cone_angle = 20_degrees;
    map &here = get_map();
    for( const tripoint_bub_ms &cone : here.points_in_radius( z->bub_pos(), 4 ) ) {
        units::angle tile_angle = coord_to_angle( z->bub_pos(), cone );
        units::angle diff = units::fabs( target_angle - tile_angle );
        // Skip the target, because it's outside cone or it's the source
        if( diff + cone_angle > 360_degrees || diff > cone_angle || cone == z->bub_pos() ) {
            continue;
        }
        // Affect the target
        // Small bash to every square, silent to not flood message box
        here.bash( cone, 4, true );

        // If a monster is there, chance for stun
        Creature *target = g->critter_at( cone );
        if( target == nullptr ) {
            continue;
        }
        if( one_in( dist / 2 ) && !( target->is_immune_effect( effect_deaf ) ) ) {
            target->add_effect( effect_dazed, rng( 1_minutes, 2_minutes ), bodypart_str_id::NULL_ID(),
                                rng( 1, ( 15 - dist ) / 3 ) );
        }

    }

    return true;
}

bool mattack::howl( monster *z )
{
    Creature *target = z->attack_target();
    if( target == nullptr ||
        rl_dist( z->bub_pos(), target->bub_pos() ) > 4 ||
        !z->sees( *target ) ) {
        return false;
    }

    // It takes a while
    z->moves -= 200;
    sound_event se;
    se.origin = z->bub_pos();
    se.volume = 100;
    se.category = sounds::sound_t::alert;
    se.description = _( "an ear-piercing howl!" );
    se.from_monster = true;
    se.monfaction = z->faction.id();
    se.id = "shout";
    se.variant = "howl";
    sounds::sound( se );

    // TODO: Make this use mon's faction when those are in
    if( z->friendly != 0 ) {
        for( monster &other : g->all_monsters() ) {
            if( other.type != z->type ) {
                continue;
            }
            // Quote KA101: Chance of friendlying other howlers in the area, I'd imagine:
            // wolves use howls for communication and can convey that the ape is on Team Wolf.
            if( one_in( 4 ) ) {
                other.friendly = z->friendly;
                break;
            }
        }
    }

    return true;
}

bool mattack::rattle( monster *z )
{
    // TODO: Let it rattle at non-player friendlies
    const int min_dist = z->friendly != 0 ? 1 : 4;
    Creature *target = &g->u;
    // Can't use attack_target - the snake has no target
    if( rl_dist( z->bub_pos(), target->bub_pos() ) > min_dist ||
        !z->sees( *target ) ) {
        return false;
    }

    // It takes a very short while
    z->moves -= 20;
    sound_event se;
    se.origin = z->bub_pos();
    se.volume = 60;
    se.category = sounds::sound_t::alert;
    se.description = _( "a sibilant rattling sound!" );
    se.from_monster = true;
    se.monfaction = z->faction.id();
    se.id = "misc";
    se.variant = "rattling";
    sounds::sound( se );

    return true;
}

bool mattack::acid( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }

    // Can't see/reach target, no attack
    if( !z->sees( *target ) ||
        !g->m.clear_path( z->bub_pos(), target->bub_pos(), 10, 1, 100 ) ) {
        return false;
    }
    // It takes a while
    z->moves -= 300;
    sound_event se;
    se.origin = z->bub_pos();
    se.volume = 60;
    se.category = sounds::sound_t::combat;
    se.description = _( "a spitting noise." );
    se.from_monster = true;
    se.monfaction = z->faction.id();
    se.id = "misc";
    se.variant = "spitting";
    sounds::sound( se );

    projectile proj;
    proj.speed = 10;
    // Mostly just for momentum
    proj.impact.add_damage( DT_ACID, 5 );
    proj.range = 10;
    proj.add_effect( ammo_effect_NO_OVERSHOOT );
    auto dealt = projectile_attack( proj, z->bub_pos(), target->bub_pos(), dispersion_sources{ 5400 },
                                    z );
    const tripoint_bub_ms &hitp = dealt.end_point;
    const Creature *hit_critter = dealt.hit_critter;
    if( hit_critter == nullptr && g->m.hit_with_acid( hitp ) && g->u.sees( hitp ) ) {
        add_msg( _( "A glob of acid hits the %s!" ),
                 g->m.tername( hitp ) );
        if( g->m.impassable( hitp ) ) {
            // TODO: Allow it to spill on the side it hit from
            return true;
        }
    }

    for( int i = -3; i <= 3; i++ ) {
        for( int j = -3; j <= 3; j++ ) {
            auto dest = hitp + tripoint( i, j, 0 );
            if( g->m.passable( dest ) &&
                g->m.clear_path( dest, hitp, 6, 1, 100 ) &&
                ( ( one_in( std::abs( j ) ) && one_in( std::abs( i ) ) ) || ( i == 0 && j == 0 ) ) ) {
                g->m.add_field( dest, fd_acid, 2 );
            }
        }
    }

    return true;
}

bool mattack::acid_barf( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    // Let it be used on non-player creatures
    Creature *target = z->attack_target();
    if( target == nullptr || !is_adjacent( z, target, false ) ) {
        return false;
    }

    z->moves -= 80;
    // Make sure it happens before uncanny dodge
    g->m.add_field( target->bub_pos(), fd_acid, 1 );
    bool uncanny = target->uncanny_dodge();
    // Can we dodge the attack? Uses player dodge function % chance (melee.cpp)
    if( uncanny || dodge_check( z, target ) ) {
        auto msg_type = target == &g->u ? m_warning : m_info;
        target->add_msg_player_or_npc( msg_type,
                                       _( "The %s barfs acid at you, but you dodge!" ),
                                       _( "The %s barfs acid at <npcname>, but they dodge!" ),
                                       z->name() );
        if( !uncanny ) {
            target->on_dodge( z, z->type->melee_skill * 2 );
        }

        return true;
    }

    bodypart_str_id hit = target->get_random_body_part().id();
    int dam = rng( 5, 12 );
    dam = target->deal_damage( z, hit.id(), damage_instance( DT_ACID,
                               dam ) ).total_damage();
    target->add_env_effect( effect_corroding, hit, 5, time_duration::from_turns( dam / 2 + 5 ), hit );

    if( dam > 0 ) {
        auto msg_type = target == &g->u ? m_bad : m_info;
        target->add_msg_player_or_npc( msg_type,
                                       //~ 1$s is monster name, 2$s bodypart in accusative
                                       _( "The %1$s barfs acid on your %2$s for %3$d damage!" ),
                                       //~ 1$s is monster name, 2$s bodypart in accusative
                                       _( "The %1$s barfs acid on <npcname>'s %2$s for %3$d damage!" ),
                                       z->name(),
                                       body_part_name_accusative( hit ),
                                       dam );

        if( hit == body_part_eyes ) {
            target->add_env_effect( effect_blind, body_part_eyes, 3, 1_minutes );
        }
    } else {
        target->add_msg_player_or_npc(
            _( "The %1$s barfs acid on your %2$s, but it washes off the armor!" ),
            _( "The %1$s barfs acid on <npcname>'s %2$s, but it washes off the armor!" ),
            z->name(),
            body_part_name_accusative( hit ) );
    }

    target->on_hit( z, hit );

    return true;
}

bool mattack::acid_accurate( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }

    const int range = rl_dist( z->bub_pos(), target->bub_pos() );
    if( range > 10 || range < 2 || !z->sees( *target ) ) {
        return false;
    }

    z->moves -= 50;

    projectile proj;
    proj.speed = 10;
    proj.range = 10;
    proj.add_effect( ammo_effect_BLINDS_EYES );
    proj.add_effect( ammo_effect_NO_DAMAGE_SCALING );
    proj.impact.add_damage( DT_ACID, rng( 3, 5 ) );
    // Make it arbitrarily less accurate at close ranges
    projectile_attack( proj, z->bub_pos(), target->bub_pos(), dispersion_sources{ 8000.0 * range }, z );

    return true;
}

bool mattack::shockstorm( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }
    if( z->type->monster_weapon && z->has_effect( effect_monster_disarmed ) ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }

    bool seen = g->u.sees( *z );
    // Can't see/reach target, no attack
    if( !z->sees( *target ) ||
        !g->m.clear_path( z->bub_pos(), target->bub_pos(), 12, 1, 100 ) ) {
        return false;
    }

    // It takes a while
    z->moves -= 50;

    if( seen ) {
        auto msg_type = target == &g->u ? m_bad : m_neutral;
        add_msg( msg_type, _( "A bolt of electricity arcs towards %s!" ), target->disp_name() );
    }
    if( !g->u.is_deaf() ) {
        sfx::play_variant_sound( "fire_gun", "bio_lightning", sfx::get_heard_volume( z->bub_pos(), 95 ) );
    }
    tripoint_bub_ms tarp( target->bub_pos().x() + rng( -1, 1 ) + rng( -1, 1 ),
                          target->bub_pos().y() + rng( -1, 1 ) + rng( -1, 1 ),
                          target->bub_pos().z() );
    std::vector<tripoint_bub_ms> bolt = line_to( z->bub_pos(), tarp, 0, 0 );
    // Fill the LOS with electricity
    for( auto &i : bolt ) {
        if( !one_in( 4 ) ) {
            g->m.add_field( i, fd_electricity, rng( 1, 3 ) );
        }
    }
    // 5x5 cloud of electricity at the square hit
    for( const auto &dest : g->m.points_in_radius( tarp, 2 ) ) {
        if( !one_in( 4 ) ) {
            g->m.add_field( dest, fd_electricity, rng( 1, 3 ) );
        }
    }

    return true;
}

bool mattack::shocking_reveal( monster *z )
{
    shockstorm( z );
    return true;
}

bool mattack::pull_metal_weapon( monster *z )
{
    ////////////////////////////////////////////////////////////////////////////////////////////////
    // Constants and Configuration

    // max distance that "pull_metal_weapon" can be applied to the target.
    constexpr auto max_distance = 12;

    // attack movement costs
    constexpr int att_cost_pull = 150;

    // minimum str to resist "pull_metal_weapon"
    constexpr int min_str = 4;

    Creature *target = z->attack_target();
    if( target == nullptr ) {
        return false;
    }

    // Can't see/reach target, no attack
    if( !z->sees( *target ) || !g->m.clear_path( z->bub_pos(), target->bub_pos(),
            max_distance, 1, 100 ) ) {
        return false;
    }
    player *foe = dynamic_cast< player * >( target );
    if( foe != nullptr ) {
        const item &weapon = foe->primary_weapon();
        // Wielded steel or iron items except for built-in things like bionic claws or monomolecular blade
        if( !weapon.has_flag( flag_NO_UNWIELD ) &&
            ( weapon.made_of( material_id( "iron" ) ) ||
              weapon.made_of( material_id( "hardsteel" ) ) ||
              weapon.made_of( material_id( "steel" ) ) ||
              weapon.made_of( material_id( "budget_steel" ) ) ) ) {
            int wp_skill = foe->get_skill_level( skill_melee );
            // It takes a while
            z->moves -= att_cost_pull;
            int success = 100;
            ///\EFFECT_STR increases resistance to pull_metal_weapon special attack
            if( foe->str_cur > min_str ) {
                ///\EFFECT_MELEE increases resistance to pull_metal_weapon special attack
                success = std::max( 100 - ( 6 * ( foe->str_cur - 6 ) ) - ( 6 * wp_skill ), 0 );
            }
            auto m_type = foe == &g->u ? m_bad : m_neutral;
            if( rng( 1, 100 ) <= success ) {
                target->add_msg_player_or_npc( m_type, _( "%s is pulled away from your hands!" ),
                                               _( "%s is pulled away from <npcname>'s hands!" ), weapon.tname() );
                z->add_item( foe->remove_primary_weapon() );
                if( foe->has_activity( ACT_RELOAD ) ) {
                    foe->cancel_activity();
                }
                if( target->is_avatar() ) {
                    popup( _( "%s is pulled away from your hands!" ), weapon.tname() );
                } else if( target->is_npc() && target->as_npc()->is_following() ) {
                    popup( _( "%1$s is pulled away from %2$s's hands!" ),
                           weapon.tname(),
                           target->as_npc()->get_name() );
                }
            } else {
                target->add_msg_player_or_npc( m_type,
                                               _( "The %s unsuccessfully attempts to pull your weapon away." ),
                                               _( "The %s unsuccessfully attempts to pull <npcname>'s weapon away." ), z->name() );
            }
        }
    }

    return true;
}

bool mattack::boomer( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr || rl_dist( z->bub_pos(), target->bub_pos() ) > 3 || !z->sees( *target ) ) {
        return false;
    }

    map &here = get_map();

    std::vector<tripoint_bub_ms> line = here.find_clear_path( z->bub_pos(), target->bub_pos() );
    // It takes a while
    z->moves -= 250;
    bool u_see = g->u.sees( *z );
    if( u_see ) {
        add_msg( m_warning, _( "The %s spews bile!" ), z->name() );
    }
    auto prev_point = z->bub_pos();
    bool obstructed = false;
    for( auto &i : line ) {
        if( here.obstructed_by_vehicle_rotation( prev_point, i ) ) {
            if( one_in( 2 ) ) {
                i.x() = prev_point.x();
            } else {
                i.y() = prev_point.y();
            }
            obstructed = true;
        }

        here.add_field( i, fd_bile, 1 );

        // If bile hit a solid tile, return.
        if( obstructed || here.impassable( i ) ) {
            here.add_field( i, fd_bile, 3 );
            if( g->u.sees( i ) ) {
                add_msg( _( "Bile splatters on the %s!" ),
                         here.tername( i ) );
            }
            return true;
        }
        prev_point = i;
    }
    if( !target->uncanny_dodge() ) {
        ///\EFFECT_DODGE increases chance to avoid boomer effect
        if( rng( 0, 10 ) > target->get_dodge() || one_in( target->get_dodge() ) ) {
            target->add_env_effect( effect_boomered, body_part_eyes, 3, 12_turns );
        } else if( u_see ) {
            target->add_msg_player_or_npc( _( "You dodge it!" ),
                                           _( "<npcname> dodges it!" ) );
        }
        target->on_dodge( z, 5 );
    }

    return true;
}

bool mattack::boomer_glow( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr || rl_dist( z->bub_pos(), target->bub_pos() ) > 3 || !z->sees( *target ) ) {
        return false;
    }

    map &here = get_map();

    std::vector<tripoint_bub_ms> line = here.find_clear_path( z->bub_pos(), target->bub_pos() );
    // It takes a while
    z->moves -= 250;
    bool u_see = g->u.sees( *z );
    if( u_see ) {
        add_msg( m_warning, _( "The %s spews bile!" ), z->name() );
    }
    auto prev_point = z->bub_pos();
    bool obstructed = false;
    for( auto &i : line ) {
        if( here.obstructed_by_vehicle_rotation( prev_point, i ) ) {
            if( one_in( 2 ) ) {
                i.x() = prev_point.x();
            } else {
                i.y() = prev_point.y();
            }
            obstructed = true;
        }
        here.add_field( i, fd_bile, 1 );
        if( obstructed || here.impassable( i ) ) {
            here.add_field( i, fd_bile, 3 );
            if( g->u.sees( i ) ) {
                add_msg( _( "Bile splatters on the %s!" ), here.tername( i ) );
            }
            return true;
        }
        prev_point = i;
    }
    if( !target->uncanny_dodge() ) {
        ///\EFFECT_DODGE increases chance to avoid glowing boomer effect
        if( rng( 0, 10 ) > target->get_dodge() || one_in( target->get_dodge() ) ) {
            target->add_env_effect( effect_boomered, body_part_eyes, 5, 25_turns );
            target->on_dodge( z, 5 );
            for( int i = 0; i < rng( 2, 4 ); i++ ) {
                bodypart_str_id bp = random_body_part();
                target->add_env_effect( effect_glowing, bp, 4, 4_minutes );
                if( target->has_effect( effect_glowing ) ) {
                    break;
                }
            }
        } else {
            target->add_msg_player_or_npc( _( "You dodge it!" ),
                                           _( "<npcname> dodges it!" ) );
        }
    }

    return true;
}

bool mattack::resurrect( monster *z )
{
    // Chance to recover some of our missing speed (yes this will regain
    // loses from being revived ourselves as well).
    // Multiplying by (current base speed / max speed) means that the
    // rate of speed regaining is unaffected by what our current speed is, i.e.
    // we will regain the same amount per minute at speed 50 as speed 200.
    if( one_in( static_cast<int>( 15 * static_cast<double>( z->get_speed_base() ) / static_cast<double>
                                  ( z->type->speed ) ) ) ) {
        // Restore 10% of our current speed, capping at our type maximum
        z->set_speed_base( std::min( z->type->speed,
                                     static_cast<int>( z->get_speed_base() + .1 * z->type->speed ) ) );
    }

    int raising_level = 0;
    if( z->has_effect( effect_raising ) ) {
        raising_level = z->get_effect_int( effect_raising ) * 40;
    }

    bool sees_necromancer = g->u.sees( *z );
    std::vector<std::pair<tripoint_bub_ms, item *>> corpses;
    // Find all corpses that we can see within 10 tiles.
    int range = 10;
    bool found_eligible_corpse = false;
    int lowest_raise_score = INT_MAX;
    for( const tripoint_bub_ms &p : g->m.points_in_radius( z->bub_pos(), range ) ) {
        if( !g->is_empty( p ) || g->m.get_field_intensity( p, fd_fire ) > 1 ||
            !g->m.sees( z->bub_pos(), p, -1 ) ) {
            continue;
        }

        for( auto &i : g->m.i_at( p ) ) {
            const mtype *mt = i->get_mtype();
            if( !( i->is_corpse() && i->can_revive() && i->is_active() && mt->has_flag( MF_REVIVES ) &&
                   mt->in_species( ZOMBIE ) && !mt->has_flag( MF_NO_NECRO ) ) ) {
                continue;
            }

            found_eligible_corpse = true;
            if( raising_level == 0 ) {
                // Since we have a target, start charging to raise it.
                if( sees_necromancer ) {
                    add_msg( m_info, _( "The %s throws its arms wide." ), z->name() );
                }
                while( z->moves >= 0 ) {
                    z->add_effect( effect_raising, 1_minutes );
                    z->moves -= 100;
                }
                return false;
            }
            int raise_score = ( i->damage_level( 4 ) + 1 ) * mt->hp + i->burnt;
            lowest_raise_score = std::min( lowest_raise_score, raise_score );
            if( raise_score <= raising_level ) {
                corpses.emplace_back( p, i );
            }
        }
    }

    if( corpses.empty() ) { // No nearby corpses
        if( found_eligible_corpse ) {
            // There was a corpse, but we haven't charged enough.
            if( sees_necromancer && x_in_y( 1, std::sqrt( lowest_raise_score / 30.0 ) ) ) {
                add_msg( m_info, _( "The %s gesticulates wildly." ), z->name() );
            }
            while( z->moves >= 0 ) {
                z->add_effect( effect_raising, 1_minutes );
                z->moves -= 100;
                return false;
            }
        } else if( raising_level != 0 ) {
            z->remove_effect( effect_raising );
        }
        // Check to see if there are any nearby living zombies to see if we should get angry
        const bool allies = g->get_creature_if( [&]( const Creature & critter ) {
            const monster *const zed = dynamic_cast<const monster *>( &critter );
            return zed && zed != z && zed->type->has_flag( MF_REVIVES ) && zed->type->in_species( ZOMBIE ) &&
                   z->attitude_to( *zed ) == Attitude::A_FRIENDLY  &&
                   within_target_range( z, zed, 10 );
        } );
        if( !allies ) {
            // Nobody around who we could revive, get angry
            z->anger = 100;
        } else {
            // Someone is around who might die and we could revive,
            // calm down.
            z->anger = 5;
        }
        return false;
    } else {
        // We're reviving someone/could revive someone, calm down.
        z->anger = 5;
    }

    if( z->get_speed_base() <= z->type->speed / 2 ) {
        // We can only resurrect so many times in a time period
        // and we're currently out
        return false;
    }

    std::pair<tripoint_bub_ms, item *> raised = random_entry( corpses );
    // To appease static analysis
    assert( raised.second );
    // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
    float corpse_damage = raised.second->damage_level( 4 );
    // Did we successfully raise something?
    if( g->revive_corpse( raised.first, *raised.second ) ) {
        g->m.i_rem( raised.first, raised.second );
        if( sees_necromancer ) {
            add_msg( m_info, _( "The %s gestures at a nearby corpse." ), z->name() );
        }
        z->remove_effect( effect_raising );
        // Takes one turn
        z->moves -= z->type->speed;
        // Penalize speed by between 10% and 50% based on how damaged the corpse is.
        float speed_penalty = 0.1 + ( corpse_damage * 0.1 );
        z->set_speed_base( z->get_speed_base() - speed_penalty * z->type->speed );
        monster *const zed = g->critter_at<monster>( raised.first );
        if( !zed ) {
            debugmsg( "Misplaced or failed to revive a zombie corpse" );
            return true;
        }

        zed->make_ally( *z );
        if( g->u.sees( *zed ) ) {
            add_msg( m_warning, _( "A nearby %s rises from the dead!" ), zed->name() );
        } else if( sees_necromancer ) {
            // We saw the necromancer but not the revival
            add_msg( m_info, _( "But nothing seems to happen." ) );
        }
    }

    return true;
}

void mattack::smash_specific( monster *z, Creature *target )
{
    if( target == nullptr || !is_adjacent( z, target, false ) ) {
        return;
    }
    if( z->has_flag( MF_RIDEABLE_MECH ) ) {
        z->use_mech_power( -5 );
    }
    z->set_goal( target->bub_pos() );
    smash( z );
}

bool mattack::smash( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr || !is_adjacent( z, target, false ) ) {
        return false;
    }

    //Don't try to smash immobile targets
    if( target->has_flag( MF_IMMOBILE ) ) {
        return false;
    }

    // Costs lots of moves to give you a little bit of a chance to get away.
    z->moves -= 400;

    if( target->uncanny_dodge() ) {
        return true;
    }

    // Can we dodge the attack? Uses player dodge function % chance (melee.cpp)
    if( dodge_check( z, target ) ) {
        target->add_msg_player_or_npc( _( "The %s takes a powerful swing at you, but you dodge it!" ),
                                       _( "The %s takes a powerful swing at <npcname>, who dodges it!" ),
                                       z->name() );
        target->on_dodge( z, z->type->melee_skill * 2 );
        return true;
    }

    target->add_msg_player_or_npc( _( "A blow from %1$s sends %2$s flying!" ),
                                   _( "A blow from %s sends <npcname> flying!" ),
                                   z->disp_name(), target->disp_name() );
    // TODO: Make this parabolic
    g->fling_creature( target, coord_to_angle( z->bub_pos(), target->bub_pos() ),
                       z->type->melee_sides * z->type->melee_dice * 3 );

    return true;
}

//--------------------------------------------------------------------------------------------------
// TODO: move elsewhere
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/**
 * Find empty spaces around origin within a radius of N.
 *
 * @returns a pair with first  = array<tripoint, area>; area = (2*N + 1)^2.
 *                      second = the number of empty spaces found.
 */
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

//--------------------------------------------------------------------------------------------------
/**
 * Find empty spaces around a creature within a radius of N.
 *
 * @see find_empty_neighbors
 */
template <size_t N = 1>
std::pair < std::array < tripoint_bub_ms, ( 2 * N + 1 ) * ( 2 * N + 1 ) >, size_t >
find_empty_neighbors( const Creature &c )
{
    return find_empty_neighbors<N>( c.bub_pos() );
}

//--------------------------------------------------------------------------------------------------
/**
 * Get a size_t value in the closed interval [0, size]; a convenience to avoid messy casting.
  */
static size_t get_random_index( const size_t size )
{
    return static_cast<size_t>( rng( 0, static_cast<int>( size - 1 ) ) );
}

//--------------------------------------------------------------------------------------------------
/**
 * Get a size_t value in the closed interval [0, c.size() - 1]; a convenience to avoid messy casting.
 */
template <typename Container>
size_t get_random_index( const Container &c )
{
    return get_random_index( c.size() );
}

bool mattack::science( monster *const z ) // I said SCIENCE again!
{
    ////////////////////////////////////////////////////////////////////////////////////////////////
    // Constants and Configuration

    // attack types
    enum : int {
        att_shock,
        att_radiation,
        att_manhack,
        att_acid_pool,
        att_flavor,
        att_enum_size
    };

    // max distance that "science" can be applied to the target.
    constexpr auto max_distance = 5;

    // attack movement costs
    constexpr int att_cost_shock   = 0;
    constexpr int att_cost_rad     = 400;
    constexpr int att_cost_manhack = 200;
    constexpr int att_cost_acid    = 100;
    constexpr int att_cost_flavor  = 80;

    // radiation attack behavior
    // how hard it is to dodge
    constexpr int att_rad_dodge_diff    = 16;
    // (1/x) inverse chance to cause mutation.
    constexpr int att_rad_mutate_chance = 6;
    // min radiation
    constexpr int att_rad_dose_min      = 20;
    // max radiation
    constexpr int att_rad_dose_max      = 50;

    // acid attack behavior
    constexpr int att_acid_intensity = 3;

    if( !z->can_act() ) {
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // Look for a valid target...
    Creature *const target = z->attack_target();
    if( !target ) {
        return false;
    }

    // too far
    const int dist = rl_dist( z->bub_pos(), target->bub_pos() );
    if( dist > max_distance ) {
        return false;
    }

    // can't attack what you can't see
    if( !z->sees( *target ) ) {
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // okay, we have a valid target; populate valid attack options...
    std::array<int, att_enum_size> valid_attacks;
    size_t valid_attack_count = 0;

    // can only shock if adjacent
    if( dist == 1 ) {
        valid_attacks[valid_attack_count++] = att_shock;
    }

    // TODO: mutate() doesn't like non-players right now
    // It will mutate NPCs, but it will say it mutated the player
    player *const foe = dynamic_cast<player *>( target );
    if( ( foe == &g->u ) && dist <= 2 ) {
        valid_attacks[valid_attack_count++] = att_radiation;
    }

    // need an open space for these attacks
    const auto empty_neighbors = find_empty_neighbors( *z );
    const size_t empty_neighbor_count = empty_neighbors.second;

    if( empty_neighbor_count ) {
        if( z->ammo[itype_bot_manhack] > 0 ) {
            valid_attacks[valid_attack_count++] = att_manhack;
        }
        valid_attacks[valid_attack_count++] = att_acid_pool;
    }

    // flavor is always okay
    valid_attacks[valid_attack_count++] = att_flavor;

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // choose and do a valid attack
    const int attack_index = get_random_index( valid_attack_count );
    switch( valid_attacks[attack_index] ) {
        default:
            debugmsg( "Bad attack enum value %d", valid_attacks[attack_index] );
            break;
        case att_shock :
            z->moves -= att_cost_shock;

            // Just reuse the taze - it's a bit different (shocks torso vs all),
            // but let's go for consistency here
            taze( z, target );
            break;
        case att_radiation : {
            z->moves -= att_cost_rad;

            // if the player can see it
            if( g->u.sees( *z ) ) {
                // TODO: mutate() doesn't like non-players right now
                add_msg( m_bad, _( "%1$s fires a shimmering beam towards %2$s!" ),
                         z->disp_name( false, true ), target->disp_name() );
            }

            // (1) Give the target a chance at an uncanny_dodge.
            // (2) If that fails, always fail to dodge 1 in dodge_skill times.
            // (3) If okay, dodge if dodge_skill > att_rad_dodge_diff.
            // (4) Otherwise, fail 1 in (att_rad_dodge_diff - dodge_skill) times.
            if( foe->uncanny_dodge() ) {
                break;
            }

            const int  dodge_skill  = foe->get_dodge();
            const bool critial_fail = one_in( dodge_skill );
            const bool is_trivial   = dodge_skill > att_rad_dodge_diff;

            ///\EFFECT_DODGE increases chance to avoid science effect
            if( !critial_fail && ( is_trivial || dodge_skill > rng( 0, att_rad_dodge_diff ) ) ) {
                target->add_msg_player_or_npc( _( "You dodge the beam!" ),
                                               _( "<npcname> dodges the beam!" ) );
            } else {
                bool rad_proof = !foe->irradiate( rng( att_rad_dose_min, att_rad_dose_max ) );
                if( rad_proof ) {
                    target->add_msg_if_player( m_good, _( "Your armor protects you from the radiation!" ) );
                } else if( one_in( att_rad_mutate_chance ) ) {
                    foe->mutate();
                } else {
                    target->add_msg_if_player( m_bad, _( "You get pins and needles all over." ) );
                }
            }
        }
        break;
        case att_manhack : {
            z->moves -= att_cost_manhack;
            z->ammo[itype_bot_manhack]--;

            // if the player can see it
            if( g->u.sees( *z ) ) {
                add_msg( m_warning, _( "A manhack flies out of one of the holes on the %s!" ),
                         z->name() );
            }

            const auto where = empty_neighbors.first[get_random_index( empty_neighbor_count )];
            if( monster *const manhack = g->place_critter_at( mon_manhack, where ) ) {
                manhack->make_ally( *z );
            }
        }
        break;
        case att_acid_pool :
            z->moves -= att_cost_acid;

            // if the player can see it
            if( g->u.sees( *z ) ) {
                add_msg( m_warning,
                         _( "The %s shudders, and some sort of caustic fluid leaks from a its damaged shell!" ),
                         z->name() );
            }

            // fill empty tiles with acid
            for( size_t i = 0; i < empty_neighbor_count; ++i ) {
                const tripoint_bub_ms &p = empty_neighbors.first[i];
                g->m.add_field( p, fd_acid, att_acid_intensity );
            }

            break;
        case att_flavor : {
            // flavor messages
            static const std::array<std::string, 4> m_flavor = {{
                    translate_marker( "The %s shudders, letting out an eery metallic whining noise!" ),
                    translate_marker( "The %s scratches its long legs along the floor, shooting sparks." ),
                    translate_marker( "The %s bleeps inquiringly and focuses a red camera-eye on you." ),
                    translate_marker( "The %s's combat arms crackle with electricity." ),
                    //special case; leave the electricity last
                }
            };

            const size_t i = get_random_index( m_flavor );

            // the special case; see above
            if( i == m_flavor.size() - 1 ) {
                z->moves -= att_cost_flavor;
            }

            // if the player can see it, else forget about it
            if( g->u.sees( *z ) ) {
                add_msg( m_warning, _( m_flavor[i] ), z->name() );
            }
        }
        break;
    }

    return true;
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

bool mattack::growplants( monster *z )
{
    for( const auto &p : g->m.points_in_radius( z->bub_pos(), 3 ) ) {

        // Only affect natural, dirtlike terrain or trees.
        if( !( g->m.ter( p )->is_diggable() ||
               g->m.has_flag_ter( "TREE", p ) ||
               g->m.ter( p ) == t_tree_young ) ) {
            continue;
        }

        if( g->m.is_bashable( p ) && one_in( 3 ) ) {
            // Destroy everything
            g->m.destroy( p );
            // And then make the ground fertile
            g->m.ter_set( p, t_dirtmound );
            continue;
        }

        // 1 in 4 chance to grow a tree
        if( !one_in( 4 ) ) {
            if( one_in( 3 ) ) {
                // If no tree, perhaps underbrush
                g->m.ter_set( p, t_underbrush );
            }

            continue;
        }

        // Grow a tree and pierce stuff with it
        Creature *critter = g->critter_at( p );
        // Don't grow under friends (and self)
        if( critter != nullptr &&
            z->attitude_to( *critter ) == Attitude::A_FRIENDLY ) {
            continue;
        }

        g->m.ter_set( p, t_tree_young );
        if( critter == nullptr || critter->uncanny_dodge() ) {
            continue;
        }

        const bodypart_str_id hit = body_part_hit_by_plant();
        critter->add_msg_player_or_npc( m_bad,
                                        //~ %s is bodypart name in accusative.
                                        _( "A tree bursts forth from the earth and pierces your %s!" ),
                                        //~ %s is bodypart name in accusative.
                                        _( "A tree bursts forth from the earth and pierces <npcname>'s %s!" ),
                                        body_part_name_accusative( hit ) );
        critter->deal_damage( z, hit.id(), damage_instance( DT_STAB, rng( 10, 30 ) ) );
    }

    // 1 in 5 chance of making existing vegetation grow larger
    if( !one_in( 5 ) ) {
        return true;
    }
    for( const tripoint_bub_ms &p : g->m.points_in_radius( z->bub_pos(), 5 ) ) {
        const auto ter = g->m.ter( p );
        if( ter != t_tree_young && ter != t_underbrush ) {
            // Skip as soon as possible to avoid all the checks
            continue;
        }

        Creature *critter = g->critter_at( p );
        if( critter != nullptr && z->attitude_to( *critter ) == Attitude::A_FRIENDLY ) {
            // Don't buff terrain below friends (and self)
            continue;
        }

        if( ter == t_tree_young ) {
            // Young tree => tree
            // TODO: Make this deal damage too - young tree can be walked on, tree can't
            g->m.ter_set( p, t_tree );
        } else if( ter == t_underbrush ) {
            // Underbrush => young tree
            g->m.ter_set( p, t_tree_young );
            if( critter != nullptr && !critter->uncanny_dodge() ) {
                const bodypart_str_id hit = body_part_hit_by_plant();
                critter->add_msg_player_or_npc( m_bad,
                                                //~ %s is bodypart name in accusative.
                                                _( "The underbrush beneath your feet grows and pierces your %s!" ),
                                                //~ %s is bodypart name in accusative.
                                                _( "Underbrush grows into a tree, and it pierces <npcname>'s %s!" ),
                                                body_part_name_accusative( hit ) );
                critter->deal_damage( z, hit.id(), damage_instance( DT_STAB, rng( 10, 30 ) ) );
            }
        }
    }

    // added during refactor, previously had no cooldown reset
    return true;
}

bool mattack::grow_vine( monster *z )
{
    if( z->friendly ) {
        if( rl_dist( g->u.bub_pos(), z->bub_pos() ) <= 3 ) {
            // Friendly vines keep the area around you free, so you can move.
            return false;
        }
    }
    z->moves -= 100;
    // Attempt to fill up to 8 surrounding tiles.
    for( int i = 0; i < rng( 1, 8 ); ++i ) {
        if( monster *const vine = g->place_critter_around( mon_creeper_vine, z->bub_pos(), 1 ) ) {
            vine->make_ally( *z );
            // Store position of parent hub in vine goal point.
            vine->set_goal( z->bub_pos() );
        }
    }

    return true;
}

bool mattack::vine( monster *z )
{
    int vine_neighbors = 0;
    bool parent_out_of_range = !g->m.inbounds( z->move_target() );
    monster *parent = g->critter_at<monster>( z->move_target() );
    if( !parent_out_of_range && ( parent == nullptr || parent->type->id != mon_creeper_hub ) ) {
        // TODO: Should probably die instead.
        return true;
    }
    z->moves -= 100;
    for( const tripoint_bub_ms &dest : g->m.points_in_radius( z->bub_pos(), 1 ) ) {
        Creature *critter = g->critter_at( dest );
        if( critter != nullptr && z->attitude_to( *critter ) == Attitude::A_HOSTILE ) {
            if( critter->uncanny_dodge() ) {
                return true;
            }

            bodypart_id bphit = critter->get_random_body_part();
            critter->add_msg_player_or_npc( m_bad,
                                            //~ 1$s monster name(vine), 2$s bodypart in accusative
                                            _( "The %1$s lashes your %2$s!" ),
                                            _( "The %1$s lashes <npcname>'s %2$s!" ),
                                            z->name(),
                                            body_part_name_accusative( bphit->token ) );
            damage_instance d;
            d.add_damage( DT_CUT, 8 );
            d.add_damage( DT_BASH, 8 );
            critter->deal_damage( z, bphit, d );
            critter->check_dead_state();
            z->moves -= 100;
            return true;
        }

        if( monster *const neighbor = g->critter_at<monster>( dest ) ) {
            if( neighbor->type->id == mon_creeper_vine ) {
                vine_neighbors++;
            }
        }
    }
    // Calculate distance from nearest hub
    int dist_from_hub = rl_dist( z->bub_pos(), z->move_target() );
    if( dist_from_hub > 20 || vine_neighbors > 5 || one_in( 7 - vine_neighbors ) ||
        !one_in( dist_from_hub ) ) {
        return true;
    }
    if( monster *const vine = g->place_critter_around( mon_creeper_vine, z->bub_pos(), 1 ) ) {
        vine->make_ally( *z );
        vine->reset_special( "VINE" );
        // Store position of parent hub in vine goal point.
        vine->set_goal( z->move_target() );
    }

    return true;
}

bool mattack::spit_sap( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }

    Creature *target = z->attack_target();
    if( target == nullptr ||
        rl_dist( z->bub_pos(), target->bub_pos() ) > 12 ||
        !z->sees( *target ) ) {
        return false;
    }

    z->moves -= 150;

    projectile proj;
    proj.speed = 10;
    proj.range = 12;
    proj.add_effect( ammo_effect_APPLY_SAP );
    proj.impact.add_damage( DT_ACID, rng( 5, 10 ) );
    projectile_attack( proj, z->bub_pos(), target->bub_pos(), dispersion_sources{ 150 }, z );

    return true;
}

bool mattack::triffid_heartbeat( monster *z )
{
    sound_event se;
    se.origin = z->bub_pos();
    se.volume = 70;
    se.category = sounds::sound_t::movement;
    se.description = _( "thu-THUMP." );
    se.movement_noise = true;
    se.from_monster = true;
    se.monfaction = z->faction.id();
    se.id = "misc";
    se.variant = "heartbeat";
    sounds::sound( se );
    z->moves -= 300;
    if( z->friendly != 0 ) {
        return true;
        // TODO: when friendly: open a way to the stairs, don't spawn monsters
    }
    if( g->u.bub_pos().z() != z->bub_pos().z() ) {
        // Maybe remove this and allow spawning monsters above?
        return true;
    }

    static pathfinding_settings root_pathfind( 10, 20, 50, 0, false, false, false, false, false );
    if( rl_dist( z->bub_pos(), g->u.bub_pos() ) > 5 &&
        !g->m.route( g->u.bub_pos(), z->bub_pos(), root_pathfind ).empty() ) {
        add_msg( m_warning, _( "The root walls creak around you." ) );
        for( const tripoint_bub_ms &dest : g->m.points_in_radius( z->bub_pos(), 3 ) ) {
            if( g->is_empty( dest ) && one_in( 4 ) ) {
                g->m.ter_set( dest, t_root_wall );
            } else if( g->m.ter( dest ) == t_root_wall && one_in( 10 ) ) {
                g->m.ter_set( dest, t_dirt );
            }
        }
        // Open blank tiles as long as there's no possible route
        int tries = 0;
        while( g->m.route( g->u.bub_pos(), z->bub_pos(), root_pathfind ).empty() &&
               tries < 20 ) {
            auto p = point_bub_ms{ rng( g->u.bub_pos().x(), z->bub_pos().x() - 3 ), rng( g->u.bub_pos().y(),
                                   z->bub_pos().y() - 3 ) };
            tripoint_bub_ms dest( p, z->bub_pos().z() );
            tries++;
            g->m.ter_set( dest, t_dirt );
            if( rl_dist( dest, g->u.bub_pos() ) > 3 && g->num_creatures() < 30 &&
                !g->critter_at( dest ) && one_in( 20 ) ) { // Spawn an extra monster
                mtype_id montype = mon_triffid;
                if( one_in( 4 ) ) {
                    montype = mon_creeper_hub;
                } else if( one_in( 3 ) ) {
                    montype = mon_biollante;
                }
                if( monster *const plant = g->place_critter_at( montype, dest ) ) {
                    plant->make_ally( *z );
                }
            }
        }

    } else { // The player is close enough for a fight!

        // Spawn a monster in (about) every second surrounding tile.
        for( int i = 0; i < 4; ++i ) {
            if( monster *const  triffid = g->place_critter_around( mon_triffid, z->bub_pos(), 1 ) ) {
                triffid->make_ally( *z );
            }
        }
    }

    return true;
}


bool mattack::disappear( monster *z )
{
    // No death drops or corpse, just in case a vanishing monster is set to have that when killed normally
    z->no_corpse_quiet = true;
    z->no_extra_death_drops = true;
    z->set_hp( 0 );
    return true;
}

static void poly_keep_speed( monster &mon, const mtype_id &id )
{
    // Retain old speed after polymorph
    // This prevents blobs regenerating speed through polymorphs
    // and thus replicating indefinitely, covering entire map
    const int old_speed = mon.get_speed_base();
    mon.poly( id );
    mon.set_speed_base( old_speed );
}

static bool blobify( monster &blob, monster &target )
{
    if( g->u.sees( target ) ) {
        add_msg( m_warning, _( "%s is engulfed by %s!" ),
                 target.disp_name( false, true ), blob.disp_name() );
    }

    switch( target.get_size() ) {
        case creature_size::tiny:
            // Just consume it
            target.set_hp( 0 );
            blob.set_speed_base( blob.get_speed_base() + 5 );
            return false;
        case creature_size::small:
            target.poly( mon_blob_small );
            break;
        case creature_size::medium:
            target.poly( mon_blob );
            break;
        case creature_size::large:
            target.poly( mon_blob_large );
            break;
        case creature_size::huge:
            // No polymorphing huge stuff
            target.add_effect( effect_slimed, rng( 2_turns, 10_turns ) );
            break;
        default:
            debugmsg( "Tried to blobify %s with invalid size: %d",
                      target.disp_name(), static_cast<int>( target.get_size() ) );
            return false;
    }

    target.make_ally( blob );
    return true;
}

bool mattack::formblob( monster *z )
{
    if( z->friendly ) {
        // TODO: handle friendly monsters
        return false;
    }

    bool didit = false;
    std::vector<tripoint_bub_ms> pts = closest_points_first( z->bub_pos(), 1 );
    // Don't check own tile
    pts.erase( pts.begin() );
    for( const tripoint_bub_ms &dest : pts ) {
        Creature *critter = g->critter_at( dest );
        if( critter == nullptr ) {
            if( z->get_speed_base() > 85 && rng( 0, 250 ) < z->get_speed_base() ) {
                // If we're big enough, spawn a baby blob.
                didit = true;
                z->set_speed_base( z->get_speed_base() - 15 );
                if( monster *const blob = g->place_critter_at( mon_blob_small, dest ) ) {
                    blob->make_ally( *z );
                }

                break;
            }

            continue;
        }

        if( critter->is_player() || critter->is_npc() ) {
            // If we hit the player or some NPC, cover them with slime
            didit = true;
            // TODO: Add some sort of a resistance/dodge roll
            critter->add_effect( effect_slimed, rng( 0_turns, 1_turns * z->get_hp() ) );
            break;
        }

        monster &othermon = *( dynamic_cast<monster *>( critter ) );
        // Hit a monster.  If it's a blob, give it our speed.  Otherwise, blobify it?
        if( z->get_speed_base() > 40 && othermon.type->in_species( species_BLOB ) ) {
            if( othermon.type->id == mon_blob_brain ) {
                // Brain blobs don't get sped up, they heal at the cost of the other blob.
                // But only if they are hurt badly.
                if( othermon.get_hp() < othermon.get_hp_max() / 2 ) {
                    othermon.heal( z->get_speed_base(), true );
                    z->set_hp( 0 );
                    return true;
                }
                continue;
            }
            didit = true;
            othermon.set_speed_base( othermon.get_speed_base() + 5 );
            z->set_speed_base( z->get_speed_base() - 5 );
            if( othermon.type->id == mon_blob_small && othermon.get_speed_base() >= 60 ) {
                poly_keep_speed( othermon, mon_blob );
            } else if( othermon.type->id == mon_blob && othermon.get_speed_base() >= 80 ) {
                poly_keep_speed( othermon, mon_blob_large );
            }
        } else if( ( othermon.made_of( material_id( "flesh" ) ) ||
                     othermon.made_of( material_id( "veggy" ) ) ||
                     othermon.made_of( material_id( "iflesh" ) ) ) &&
                   rng( 0, z->get_hp() ) > rng( othermon.get_hp() / 2, othermon.get_hp() ) ) {
            didit = blobify( *z, othermon );
        }
    }

    if( didit ) { // We did SOMEthing.
        if( z->type->id == mon_blob && z->get_speed_base() <= 50 ) {
            // We shrank!
            poly_keep_speed( *z, mon_blob_small );
        } else if( z->type->id == mon_blob_large && z->get_speed_base() <= 70 ) {
            // We shrank!
            poly_keep_speed( *z, mon_blob );
        }

        z->moves = 0;
        return true;
    }

    return true; // consider returning false to try again immediately if nothing happened?
}

bool mattack::callblobs( monster *z )
{
    if( z->friendly ) {
        // TODO: handle friendly monsters
        return false;
    }
    // The huge brain blob interposes other blobs between it and any threat.
    // For the moment just target the player, this gets a bit more complicated
    // if we want to deal with NPCS and friendly monsters as well.
    // The strategy is to send about 1/3 of the available blobs after the player,
    // and keep the rest near the brain blob for protection.
    auto enemy = g->u.bub_pos();
    std::list<monster *> allies;
    std::vector<tripoint_bub_ms> nearby_points = closest_points_first( z->bub_pos(), 3 );
    for( monster &candidate : g->all_monsters() ) {
        if( candidate.type->in_species( species_BLOB ) && candidate.type->id != mon_blob_brain ) {
            // Just give the allies consistent assignments.
            // Don't worry about trying to make the orders optimal.
            allies.push_back( &candidate );
        }
    }
    // 1/3 of the available blobs, unless they would fill the entire area near the brain.
    const int num_guards = std::min( allies.size() / 3, nearby_points.size() );
    int guards = 0;
    for( std::list<monster *>::iterator ally = allies.begin();
         ally != allies.end(); ++ally, ++guards ) {
        auto post = enemy;
        if( guards < num_guards ) {
            // Each guard is assigned a spot in the nearby_points vector based on their order.
            int assigned_spot = ( nearby_points.size() * guards ) / num_guards;
            post = nearby_points[ assigned_spot ];
        }
        ( *ally )->set_dest( post );
        if( !( *ally )->has_effect( effect_ai_controlled ) ) {
            ( *ally )->add_effect( effect_ai_controlled, 1_turns );
        }
    }
    // This is telepathy, doesn't take any moves.

    return true;
}

bool mattack::jackson( monster *z )
{
    // Jackson draws nearby zombies into the dance.
    std::list<monster *> allies;
    std::vector<tripoint_bub_ms> nearby_points = closest_points_first( z->bub_pos(), 3 );
    for( monster &candidate : g->all_monsters() ) {
        if( candidate.type->in_species( ZOMBIE ) && candidate.type->id != mon_zombie_jackson ) {
            // Just give the allies consistent assignments.
            // Don't worry about trying to make the orders optimal.
            allies.push_back( &candidate );
        }
    }
    const int num_dancers = std::min( allies.size(), nearby_points.size() );
    int dancers = 0;
    bool converted = false;
    for( auto ally = allies.begin(); ally != allies.end(); ++ally, ++dancers ) {
        auto post = z->bub_pos();
        if( dancers < num_dancers ) {
            // Each dancer is assigned a spot in the nearby_points vector based on their order.
            int assigned_spot = ( nearby_points.size() * dancers ) / num_dancers;
            post = nearby_points[ assigned_spot ];
        }
        if( ( *ally )->type->id != mon_zombie_dancer ) {
            ( *ally )->poly( mon_zombie_dancer );
            converted = true;
        }
        ( *ally )->set_dest( post );
        if( !( *ally )->has_effect( effect_ai_controlled ) ) {
            ( *ally )->add_effect( effect_ai_controlled, 1_turns );
        }
    }
    // Did we convert anybody?
    if( converted ) {
        if( g->u.sees( *z ) ) {
            add_msg( m_warning, _( "The %s lets out a high-pitched cry!" ), z->name() );
        }
    }
    // This is telepathy, doesn't take any moves.
    return true;
}

bool mattack::dance( monster *z )
{
    if( g->u.sees( *z ) ) {
        switch( rng( 1, 10 ) ) {
            case 1:
                add_msg( m_neutral, _( "The %s swings its arms from side to side!" ), z->name() );
                break;
            case 2:
                add_msg( m_neutral, _( "The %s does some fancy footwork!" ), z->name() );
                break;
            case 3:
                add_msg( m_neutral, _( "The %s shrugs its shoulders!" ), z->name() );
                break;
            case 4:
                add_msg( m_neutral, _( "The %s spins in place!" ), z->name() );
                break;
            case 5:
                add_msg( m_neutral, _( "The %s crouches on the ground!" ), z->name() );
                break;
            case 6:
                add_msg( m_neutral, _( "The %s looks left and right!" ), z->name() );
                break;
            case 7:
                add_msg( m_neutral, _( "The %s jumps back and forth!" ), z->name() );
                break;
            case 8:
                add_msg( m_neutral, _( "The %s raises its arms in the air!" ), z->name() );
                break;
            case 9:
                add_msg( m_neutral, _( "The %s swings its hips!" ), z->name() );
                break;
            case 10:
                add_msg( m_neutral, _( "The %s claps!" ), z->name() );
                break;
        }
    }

    return true;
}

bool mattack::dogthing( monster *z )
{
    if( z == nullptr ) {
        // TODO: replace pointers with references
        return false;
    }

    if( !one_in( 3 ) || !g->u.sees( *z ) ) {
        return false;
    }

    add_msg( _( "The %s's head explodes in a mass of roiling tentacles!" ),
             z->name() );

    g->m.add_splash( z->bloodType(), z->bub_pos(), 2, 3 );

    z->friendly = 0;
    z->poly( mon_headless_dog_thing );

    return false;
}

bool mattack::tentacle( monster *z )
{
    if( z->friendly ) {
        // TODO: handle friendly monsters
        return false;
    }
    Creature *target = z->attack_target();
    if( target == nullptr || rl_dist( z->bub_pos(), target->bub_pos() ) > 3 || !z->sees( *target ) ) {
        return false;
    }
    game_message_type msg_type = target == &g->u ? m_bad : m_info;
    target->add_msg_player_or_npc( msg_type,
                                   _( "The %s lashes its tentacle at you!" ),
                                   _( "The %s lashes its tentacle at <npcname>!" ),
                                   z->name() );
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
    const body_part hit_token = hit->token;
    int dam = rng( 10, 20 );
    dam = target->deal_damage( z, hit, damage_instance( DT_BASH, dam ) ).total_damage();

    if( dam > 0 ) {
        target->add_msg_player_or_npc( msg_type,
                                       //~ 1$s is bodypart name, 2$d is damage value.
                                       _( "Your %1$s is hit for %2$d damage!" ),
                                       //~ 1$s is bodypart name, 2$d is damage value.
                                       _( "<npcname>'s %1$s is hit for %2$d damage!" ),
                                       body_part_name( hit_token ),
                                       dam );
    } else {
        target->add_msg_player_or_npc(
            _( "The %1$s lashes its tentacle at your %2$s, but glances off your armor!" ),
            _( "The %1$s lashes its tentacle at <npcname>'s %2$s, but glances off their armor!" ),
            z->name(),
            body_part_name_accusative( hit_token ) );
    }

    target->on_hit( z, hit );
    target->check_dead_state();

    return true;
}

bool mattack::ranged_pull( monster *z )
{
    Creature *target = z->attack_target();
    if( target == nullptr || rl_dist( z->bub_pos(), target->bub_pos() ) > 3 ||
        rl_dist( z->bub_pos(), target->bub_pos() ) <= 1 || !z->sees( *target ) ||
        z->has_effect( effect_grabbing ) ) {
        return false;
    }

    map &here = get_map();

    player *foe = dynamic_cast< player * >( target );
    std::vector<tripoint_bub_ms> line = here.find_clear_path( z->bub_pos(), target->bub_pos() );
    bool seen = g->u.sees( *z );
    auto prev_point = z->bub_pos();
    for( auto &i : line ) {
        // Player can't be pulled though bars, furniture, cars or creatures
        // TODO: Add bashing? Currently a window is enough to prevent grabbing
        if( ( !g->is_empty( i ) && i != z->bub_pos() && i != target->bub_pos() ) ||
            here.obstructed_by_vehicle_rotation( prev_point, i ) ) {
            return false;
        }
        prev_point = i;
    }

    z->moves -= 150;

    const bool uncanny = target->uncanny_dodge();
    if( uncanny || dodge_check( z, target ) ) {
        z->moves -= 200;
        auto msg_type = foe == &g->u ? m_warning : m_info;
        target->add_msg_player_or_npc( msg_type, _( "The %s's arms fly out at you, but you dodge!" ),
                                       _( "The %s's arms fly out at <npcname>, but they dodge!" ),
                                       z->name() );

        if( !uncanny ) {
            target->on_dodge( z, z->type->melee_skill * 2 );
        }

        return true;
    }

    // Limit the range in case some weird math thing would cause the target to fly past us
    int range = std::min( ( z->type->melee_sides * z->type->melee_dice ) / 10,
                          rl_dist( z->bub_pos(), target->bub_pos() ) + 1 );
    auto pt = target->bub_pos();
    while( range > 0 ) {
        // Recalculate the ray each step
        // We can't depend on either the target position being constant (obviously),
        // but neither on z pos staying constant, because we may want to shift the map mid-pull
        const units::angle dir = coord_to_angle( target->bub_pos(), z->bub_pos() );
        tileray tdir( dir );
        tdir.advance();
        pt.x() = target->bub_pos().x() + tdir.dx();
        pt.y() = target->bub_pos().y() + tdir.dy();
        if( !g->is_empty( pt ) ) { //Cancel the grab if the space is occupied by something
            break;
        }

        if( foe != nullptr ) {
            if( foe->in_vehicle ) {
                here.unboard_vehicle( foe->bub_pos() );
            }

            if( target->is_player() && ( pt.x() < g_half_mapsize_x || pt.y() < g_half_mapsize_y ||
                                         pt.x() >= g_half_mapsize_x + SEEX || pt.y() >= g_half_mapsize_y + SEEY ) ) {
                g->update_map( pt.x(), pt.y() );
            }
        }

        target->setpos( pt );
        range--;
        if( target->is_player() && seen ) {
            g->invalidate_main_ui_adaptor();
            inp_mngr.pump_events();
            ui_manager::redraw_invalidated();
            refresh_display();
        }
    }
    // The monster might drag a target that's not on it's z level
    // So if they leave them on open air, make them fall
    here.creature_on_trap( *target );
    if( seen ) {
        if( z->type->bodytype == "human" || z->type->bodytype == "angel" ) {
            add_msg( _( "%1$s's arms fly out and pull and grab %2$s!" ),
                     z->disp_name( false, true ), target->disp_name() );

            // Stop player from hauling when grabbed and pulled
            if( z->is_player() && z->as_character()->is_hauling() ) {
                z->as_character()->stop_hauling();
            }

        } else {
            add_msg( _( "%1$s reaches out and pulls %2$s!" ),
                     z->disp_name( false, true ), target->disp_name() );
        }
    }

    const int prev_effect = target->get_effect_int( effect_grabbed );
    //Duration needs to be at least 2, or grab will immediately be removed
    target->add_effect( effect_grabbed, 2_turns, body_part_torso, prev_effect + 4 );
    z->add_effect( effect_grabbing, 2_turns );
    return true;
}

bool mattack::grab( monster *z )
{
    if( !z->can_act() ) {
        return false;
    }
    // Grabbing non-Characters not supported yet
    player *target = dynamic_cast<player *>( z->attack_target() );
    player *pl = target;
    if( target == nullptr || !is_adjacent( z, target, false ) ) {
        return false;
    }

    const bool uncanny = target->uncanny_dodge();
    const auto msg_type = target == &g->u ? m_warning : m_info;
    if( uncanny || dodge_check( z, target ) ) {
        z->moves -= 40;
        target->add_msg_player_or_npc( msg_type, _( "The %s gropes at you, but you dodge!" ),
                                       _( "The %s gropes at <npcname>, but they dodge!" ),
                                       z->name() );

        if( !uncanny ) {
            target->on_dodge( z, z->type->melee_skill * 2 );
        }

        return true;
    }

    item &cur_weapon = pl->primary_weapon();
    ///\EFFECT_DEX increases chance to avoid being grabbed
    if( pl->can_use_grab_break_tec( cur_weapon ) &&
        rng( 0, pl->get_dex() ) > rng( 0, z->type->melee_sides + z->type->melee_dice ) ) {
        if( target->has_effect( effect_grabbed ) ) {
            target->add_msg_if_player( m_info, _( "The %s tries to grab you as well, but you bat it away!" ),
                                       z->name() );
        } else if( pl->is_throw_immune() && ( !pl->is_armed() ||
                                              pl->martial_arts_data->selected_has_weapon( pl->primary_weapon().typeId() ) ) ) {
            target->add_msg_if_player( m_info, _( "The %s tries to grab you…" ), z->name() );
            thrown_by_judo( z );
        } else if( pl->has_grab_break_tec() ) {
            ma_technique tech = pl->martial_arts_data->get_grab_break_tec( cur_weapon );
            target->add_msg_player_or_npc( m_info, _( tech.avatar_message ), _( tech.npc_message ), z->name() );
        } else {
            target->add_msg_player_or_npc( m_info, _( "The %s tries to grab you, but you break its grab!" ),
                                           _( "The %s tries to grab <npcname>, but they break its grab!" ),
                                           z->name() );
        }
        return true;
    }

    const int prev_effect = target->get_effect_int( effect_grabbed );
    z->add_effect( effect_grabbing, 2_turns );
    target->add_effect( effect_grabbed, 2_turns, body_part_torso,
                        prev_effect + z->get_grab_strength() );
    target->add_msg_player_or_npc( m_bad, _( "The %s grabs you!" ), _( "The %s grabs <npcname>!" ),
                                   z->name() );

    // Stop player from hauling since they have been grabbed
    if( pl->is_player() && pl->is_hauling() ) {
        pl->stop_hauling();
    }

    // A hit to use up our moves
    z->melee_attack( *target );
    // Set up a bite on the next turn
    if( z->type->special_attacks.contains( "BITE" ) ) {
        z->set_special( "BITE", 1 );
    }

    return true;
}

bool mattack::grab_drag( monster *z )
{
    if( !z || !z->can_act() ) {
        return false;
    }
    Creature *target = z->attack_target();
    if( target == nullptr || rl_dist( z->bub_pos(), target->bub_pos() ) > 1 ) {
        return false;
    }

    if( target->has_effect( effect_under_op ) ) {
        target->add_msg_player_or_npc( m_good,
                                       _( "The %s tries to drag you, but you're securely fastened in the autodoc." ),
                                       _( "The %s tries to drag <npcname>, but they're securely fastened in the autodoc." ), z->name() );
        return false;
    }

    // First, grab the target
    grab( z );

    if( !target->has_effect( effect_grabbed ) ) { //Can't drag if isn't grabbed, otherwise try and move
        return false;
    }
    const auto target_square = z->bub_pos() - ( target->bub_pos() - z->bub_pos() );
    if( z->can_move_to( target_square ) &&
        target->stability_roll() < dice( z->type->melee_sides, z->type->melee_dice ) ) {
        player *foe = dynamic_cast<player *>( target );
        monster *zz = dynamic_cast<monster *>( target );
        auto zpt = z->bub_pos();
        z->move_to( target_square );
        if( !g->is_empty( zpt ) ) { //Cancel the grab if the space is occupied by something
            return false;
        }
        if( target->is_player() && ( zpt.x() < g_half_mapsize_x ||
                                     zpt.y() < g_half_mapsize_y ||
                                     zpt.x() >= g_half_mapsize_x + SEEX || zpt.y() >= g_half_mapsize_y + SEEY ) ) {
            g->update_map( zpt.x(), zpt.y() );
        }
        if( foe != nullptr ) {
            if( foe->in_vehicle ) {
                g->m.unboard_vehicle( foe->bub_pos() );
            }
            foe->setpos( zpt );
        } else {
            zz->setpos( zpt );
        }
        target->add_msg_player_or_npc( m_bad, _( "You are dragged behind the %s!" ),
                                       _( "<npcname> gets dragged behind the %s!" ), z->name() );
    } else {
        target->add_msg_player_or_npc( m_good, _( "You resist the %s as it tries to drag you!" ),
                                       _( "<npcname> resist the %s as it tries to drag them!" ), z->name() );
    }
    int prev_effect = target->get_effect_int( effect_grabbed );
    z->add_effect( effect_grabbing, 2_turns );
    target->add_effect( effect_grabbed, 2_turns, body_part_torso, prev_effect + 3 );

    // cooldown was not reset prior to refactor here
    return true;
}

bool mattack::gene_sting( monster *z )
{
    const float range = 7.0f;
    Creature *target = sting_get_target( z, range );
    if( target == nullptr || !( target->is_player() || target->is_npc() ) ) {
        return false;
    }

    z->moves -= 150;

    damage_instance dam = damage_instance();
    dam.add_damage( DT_STAB, 6, 10, 0.6, 1 );
    bool hit = sting_shoot( z, target, dam, range );
    if( hit ) {
        //Add checks if previous NPC/player conditions are removed
        dynamic_cast<player *>( target )->irradiate( rng( 100, 300 ) );
    }

    return true;
}

bool mattack::para_sting( monster *z )
{
    const float range = 4.0f;
    Creature *target = sting_get_target( z, range );
    if( target == nullptr ) {
        return false;
    }

    z->moves -= 150;

    damage_instance dam = damage_instance();
    dam.add_damage( DT_STAB, 6, 8, 0.8, 1 );
    bool hit = sting_shoot( z, target, dam, range );
    if( hit ) {
        target->add_msg_if_player( m_bad, _( "You feel poison enter your body!" ) );
        target->add_effect( effect_paralyzepoison, 5_minutes );
    }

    return true;
}

bool mattack::triffid_growth( monster *z )
{
    // Young triffid growing into an adult
    if( g->u.sees( *z ) ) {
        add_msg( m_warning, _( "The %s young triffid grows into an adult!" ),
                 z->name() );
    }
    z->poly( mon_triffid );

    return false;
}

bool mattack::stare( monster *z )
{
    if( z->friendly ) {
        // TODO: handle friendly monsters
        return false;
    }
    z->moves -= 200;
    if( z->sees( g->u ) ) {
        //dimensional effects don't take against dimensionally anchored foes.
        if( g->u.worn_with_flag( flag_DIMENSIONAL_ANCHOR ) ||
            g->u.has_effect_with_flag( flag_DIMENSIONAL_ANCHOR ) ) {
            add_msg( m_warning, _( "You feel a strange reverberation across your body." ) );
            return true;
        }
        if( g->u.sees( *z ) ) {
            add_msg( m_bad, _( "The %s stares at you, and you shudder." ), z->name() );
        } else {
            add_msg( m_bad, _( "You feel like you're being watched, it makes you sick." ) );
        }
        g->u.add_effect( effect_attention, 80_minutes );
    }

    return true;
}

bool mattack::fear_paralyze( monster *z )
{
    if( z->friendly ) {
        // TODO: handle friendly monsters
        return false;
    }

    if( !within_visual_range( z, 10 ) ) {
        return false;
    }

    if( g->u.sees( *z ) && !g->u.has_effect( effect_fearparalyze ) ) {
        if( has_psy_protection( get_player_character(), 4 ) ) {
            add_msg( _( "The %s probes your mind, but is rebuffed!" ), z->name() );
            ///\EFFECT_INT decreases chance of being paralyzed by fear attack
        } else if( rng( 0, 20 ) > g->u.get_int() ) {
            add_msg( m_bad, _( "The terrifying visage of the %s paralyzes you." ), z->name() );
            g->u.add_effect( effect_fearparalyze, 5_turns );
            g->u.moves -= 4 * g->u.get_speed();
        } else {
            add_msg( _( "You manage to avoid staring at the horrendous %s." ), z->name() );
        }
    }

    return true;
}
bool mattack::nurse_check_up( monster *z )
{
    bool found_target = false;
    player *target = nullptr;
    tripoint_bub_ms tmp_pos( z->bub_pos() + point_rel_ms( 12, 12 ) );
    for( auto critter : g->m.get_creatures_in_radius( z->bub_pos(), 6 ) ) {
        player *tmp_player = dynamic_cast<player *>( critter );
        if( tmp_player != nullptr && z->sees( *tmp_player ) &&
            g->m.clear_path( z->bub_pos(), tmp_player->bub_pos(), 10, 0,
                             100 ) ) { // no need to scan players we can't reach
            if( rl_dist( z->bub_pos(), tmp_player->bub_pos() ) < rl_dist( z->bub_pos(), tmp_pos ) ) {
                tmp_pos = tmp_player->bub_pos();
                target = tmp_player;
                found_target = true;
            }
        }
    }
    if( found_target ) {

        // First we offer the check up then we wait to the player to come close
        sound_event se;
        se.origin = z->bub_pos();
        se.volume = 60;
        se.category = sounds::sound_t::electronic_speech;

        se.from_monster = true;
        se.monfaction = z->faction.id();
        if( !z->has_effect( effect_countdown ) ) {
            se.description = string_format(
                                 _( "a soft robotic voice say, \"Come here and stand still for a few minutes, I'll give you a check-up.\"" ) );
            sounds::sound( se );
            z->add_effect( effect_countdown, 30_minutes );
        } else if( rl_dist( target->bub_pos(), z->bub_pos() ) > 1 ) {
            // Giving them some encouragement
            se.description = string_format(
                                 _( "a soft robotic voice say, \"Come on.  I don't bite, I promise it won't hurt one bit.\"" ) );
            sounds::sound( se );
        } else {
            se.description = string_format(
                                 _( "a soft robotic voice say, \"Here we go.  Just hold still.\"" ) );
            sounds::sound( se );
            if( target == &g->u ) {
                add_msg( m_good, _( "You get a medical check-up." ) );
            }
            target->add_effect( effect_got_checked, 10_turns );
            z->remove_effect( effect_countdown );
        }
        return true;
    }
    return false;
}
bool mattack::nurse_assist( monster *z )
{

    const bool u_see = g->u.sees( *z );

    if( u_see && one_in( 100 ) ) {
        add_msg( m_info, _( "The %s is scanning its surroundings." ), z->name() );
    }

    bool found_target = false;
    player *target = nullptr;
    tripoint_bub_ms tmp_pos( z->bub_pos() + point_rel_ms( 12, 12 ) );
    for( auto critter : g->m.get_creatures_in_radius( z->bub_pos(), 6 ) ) {
        player *tmp_player = dynamic_cast<player *>( critter );
        // No need to scan players we can't reach
        if( tmp_player != nullptr && z->sees( *tmp_player ) &&
            g->m.clear_path( z->bub_pos(), tmp_player->bub_pos(), 10, 0, 100 ) ) {
            if( rl_dist( z->bub_pos(), tmp_player->bub_pos() ) < rl_dist( z->bub_pos(), tmp_pos ) ) {
                tmp_pos = tmp_player->bub_pos();
                target = tmp_player;
                found_target = true;
            }
        }
    }

    if( found_target ) {
        if( target->is_wearing( itype_badge_doctor ) ||
            z->attitude_to( *target ) == Attitude::A_FRIENDLY ) {
            sound_event se;
            se.origin = z->bub_pos();
            se.volume = 60;
            se.category = sounds::sound_t::electronic_speech;
            se.description = string_format(
                                 _( "a soft robotic voice say, \"Welcome doctor %s.  I'll be your assistant today.\"" ),
                                 Name::generate( target->male ) );
            se.from_monster = true;
            se.monfaction = z->faction.id();

            sounds::sound( se );
            target->add_effect( effect_assisted, 20_turns, bodypart_str_id::NULL_ID(), 12 );
            return true;
        }
    }
    return false;
}
bool mattack::nurse_operate( monster *z )
{
    const itype_id ammo_type( "anesthetic" );

    if( z->has_effect( effect_dragging ) || z->has_effect( effect_operating ) ) {
        return false;
    }
    const bool u_see = g->u.sees( *z );

    if( u_see && one_in( 100 ) ) {
        add_msg( m_info, _( "The %s is scanning its surroundings." ), z->name() );
    }

    if( ( ( g->u.is_wearing( itype_badge_doctor ) ||
            z->attitude_to( g->u ) == Attitude::A_FRIENDLY ) && u_see ) && one_in( 100 ) ) {

        add_msg( m_info, _( "The %s doesn't seem to register you as a doctor." ), z->name() );
    }

    if( z->ammo[ammo_type] == 0 && u_see ) {
        if( one_in( 100 ) ) {
            add_msg( m_info, _( "The %s looks at its empty anesthesia kit with a dejected look." ), z->name() );
        }
        return false;
    }

    bool found_target = false;
    player *target = nullptr;
    tripoint_bub_ms tmp_pos( z->bub_pos() + point_rel_ms( 12, 12 ) );
    for( auto critter : g->m.get_creatures_in_radius( z->bub_pos(), 6 ) ) {
        player *tmp_player = dynamic_cast< player *>( critter );
        // No need to scan players we can't reach
        if( tmp_player != nullptr && z->sees( *tmp_player ) &&
            g->m.clear_path( z->bub_pos(), tmp_player->bub_pos(), 10, 0, 100 ) ) {
            if( tmp_player->has_any_bionic() ) {
                if( rl_dist( z->bub_pos(), tmp_player->bub_pos() ) < rl_dist( z->bub_pos(), tmp_pos ) ) {
                    tmp_pos = tmp_player->bub_pos();
                    target = tmp_player;
                    found_target = true;
                }
            }
        }
    }
    if( found_target && z->attitude_to( g->u ) == Attitude::A_FRIENDLY ) {
        // 50% chance to not turn hostile again
        if( one_in( 2 ) ) {
            return false;
        }
    }
    if( found_target && u_see ) {
        add_msg( m_info, _( "The %1$s scans %2$s and seems to detect something." ), z->name(),
                 target->disp_name() );
    }

    if( found_target ) {

        z->friendly = 0;
        z->anger = 100;
        std::list<tripoint_bub_ms> couch_pos = g->m.find_furnitures_or_vparts_with_flag_in_radius(
                z->bub_pos(), 10,
                flag_AUTODOC_COUCH );

        if( couch_pos.empty() ) {
            add_msg( m_info, _( "The %s looks for something but doesn't seem to find it." ), z->name() );
            z->anger = 0;
            return false;
        }
        // Should designate target as the attack_target
        z->set_dest( target->bub_pos() );

        // Check if target is already grabbed by something else
        if( target->has_effect( effect_grabbed ) ) {
            for( auto critter : g->m.get_creatures_in_radius( target->bub_pos(), 1 ) ) {
                monster *mon = dynamic_cast<monster *>( critter );
                if( mon != nullptr && mon != z ) {
                    sound_event se;
                    se.origin = z->bub_pos();
                    se.volume = 60;
                    se.category = sounds::sound_t::electronic_speech;
                    se.from_monster = true;
                    se.monfaction = z->faction.id();
                    if( mon->type->id != mon_defective_robot_nurse ) {
                        se.description = string_format(
                                             _( "a soft robotic voice say, \"Unhand this patient immediately!  If you keep interfering with the procedure I'll be forced to call law enforcement.\"" ) );
                        sounds::sound( se );
                        // Try to push the perpetrator away
                        z->push_to( mon->bub_pos(), 6, 0 );
                    } else {
                        se.description = string_format(
                                             _( "a soft robotic voice say, \"Greetings kinbot.  Please take good care of this patient.\"" ) );
                        sounds::sound( se );
                        z->anger = 0;
                        // Situation is under control no need to intervene;
                        return false;
                    }
                }
            }
        } else {
            grab( z );
            // Check if we successfully grabbed the target
            if( target->has_effect( effect_grabbed ) ) {
                z->dragged_foe_id = target->getID();
                z->add_effect( effect_dragging, 1_turns );
                return true;
            }
        }
        return false;
    }
    z->anger = 0;
    return false;
}
bool mattack::check_money_left( monster *z )
{
    sound_event se;
    se.origin = z->bub_pos();
    se.category = sounds::sound_t::electronic_speech;
    se.from_monster = true;
    se.monfaction = z->faction.id();
    if( !z->has_effect( effect_paid ) ) {
        if( z->friendly == -1 &&
            z->has_effect( effect_pet ) ) { // if the pet effect runs out we're no longer friends
            z->friendly = 0;

            if( !z->get_items().empty() ) {
                z->drop_items();
                z->remove_effect( effect_has_bag );
                add_msg( m_info,
                         _( "The %s dumps the contents of its bag on the ground and drops the bag on top of it." ),
                         z->get_name() );
            }

            const SpeechBubble &speech_no_time = get_speech( "mon_grocerybot_friendship_done" );
            se.volume = speech_no_time.volume;
            se.description = speech_no_time.text.translated();
            sounds::sound( se );
            z->remove_effect( effect_pet );
            return true;
        }
    } else {
        const time_duration time_left = z->get_effect_dur( effect_paid );
        if( time_left < 1_minutes ) {
            if( calendar::once_every( 20_seconds ) ) {
                const SpeechBubble &speech_time_low = get_speech( "mon_grocerybot_running_out_of_friendship" );
                se.volume = speech_time_low.volume;
                se.description = speech_time_low.text.translated();
                sounds::sound( se );
            }
        }
    }
    if( z->friendly == -1 && !z->has_effect( effect_paid ) ) {
        if( calendar::once_every( 3_hours ) ) {
            const SpeechBubble &speech_override_start = get_speech( "mon_grocerybot_hacked" );
            se.volume = speech_override_start.volume;
            se.description = speech_override_start.text.translated();
            sounds::sound( se );
        }
    }
    return false;
}
bool mattack::photograph( monster *z )
{
    if( !within_visual_range( z, 6 ) ) {
        return false;
    }

    // Badges should NOT be swappable between roles.
    // Hence separate checking.
    // If you are in fact listed as a police officer
    if( g->u.has_trait( trait_PROF_POLICE ) ) {
        // And you're wearing your badge
        if( g->u.is_wearing( itype_badge_deputy ) ) {
            if( one_in( 3 ) ) {
                add_msg( m_info, _( "The %s flashes a LED and departs.  Human officer on scene." ),
                         z->name() );
                z->no_corpse_quiet = true;
                z->no_extra_death_drops = true;
                z->die( nullptr );
                return false;
            } else {
                add_msg( m_info,
                         _( "The %s acknowledges you as an officer responding, but hangs around to watch." ),
                         z->name() );
                add_msg( m_info, _( "Probably some now-obsolete Internal Affairs subroutine…" ) );
                return true;
            }
        }
    }

    if( g->u.has_trait( trait_PROF_PD_DET ) ) {
        // And you have your shield on
        if( g->u.is_wearing( itype_badge_detective ) ) {
            if( one_in( 4 ) ) {
                add_msg( m_info, _( "The %s flashes a LED and departs.  Human officer on scene." ),
                         z->name() );
                z->no_corpse_quiet = true;
                z->no_extra_death_drops = true;
                z->die( nullptr );
                return false;
            } else {
                add_msg( m_info,
                         _( "The %s acknowledges you as an officer responding, but hangs around to watch." ),
                         z->name() );
                add_msg( m_info, _( "Ops used to do that in case you needed backup…" ) );
                return true;
            }
        }
    } else if( g->u.has_trait( trait_PROF_SWAT ) ) {
        // And you're wearing your badge
        if( g->u.is_wearing( itype_badge_swat ) ) {
            if( one_in( 3 ) ) {
                add_msg( m_info, _( "The %s flashes a LED and departs.  SWAT's working the area." ),
                         z->name() );
                z->no_corpse_quiet = true;
                z->no_extra_death_drops = true;
                z->die( nullptr );
                return false;
            } else {
                add_msg( m_info, _( "The %s acknowledges you as SWAT onsite, but hangs around to watch." ),
                         z->name() );
                add_msg( m_info, _( "Probably some now-obsolete Internal Affairs subroutine…" ) );
                return true;
            }
        }
    } else if( g->u.has_trait( trait_PROF_CYBERCO ) ) {
        // And you're wearing your badge
        if( g->u.is_wearing( itype_badge_cybercop ) ) {
            if( one_in( 3 ) ) {
                add_msg( m_info, _( "The %s winks a LED and departs.  One machine to another?" ),
                         z->name() );
                z->no_corpse_quiet = true;
                z->no_extra_death_drops = true;
                z->die( nullptr );
                return false;
            } else {
                add_msg( m_info,
                         _( "The %s acknowledges you as an officer responding, but hangs around to watch." ),
                         z->name() );
                add_msg( m_info, _( "Apparently yours aren't the only systems kept alive post-apocalypse." ) );
                return true;
            }
        }
    }

    if( g->u.has_trait( trait_PROF_FED ) ) {
        // And you're wearing your badge
        if( g->u.is_wearing( itype_badge_marshal ) ) {
            add_msg( m_info, _( "The %s flashes a LED and departs.  The Feds got this." ), z->name() );
            z->no_corpse_quiet = true;
            z->no_extra_death_drops = true;
            z->die( nullptr );
            return false;
        }
    }

    if( z->friendly || g->u.primary_weapon().typeId() == itype_e_handcuffs ) {
        // Friendly (hacked?) bot ignore the player. Arrested suspect ignored too.
        // TODO: might need to be revisited when it can target npcs.
        return false;
    }
    z->moves -= 150;
    add_msg( m_warning, _( "The %s takes your picture!" ), z->name() );
    // TODO: Make the player known to the faction
    std::string cname = _( "…database connection lost!" );
    if( one_in( 6 ) ) {
        cname = Name::generate( g->u.male );
    } else if( one_in( 3 ) ) {
        cname = g->u.name;
    }
    sound_event se;
    se.origin = z->bub_pos();
    se.volume = 80;
    se.category = sounds::sound_t::alert;
    se.description = string_format( _( "a robotic voice boom, \"Citizen %s!\"" ), cname );
    se.from_monster = true;
    se.monfaction = z->faction.id();
    se.faction = faction_id( "no_faction" );
    se.id = "speech";
    se.variant = z->type->id.str();
    sounds::sound( se );

    if( g->u.primary_weapon().is_gun() ) {
        se.description = _( "\"Drop your gun!  Now!\"" );
        sounds::sound( se );
    } else if( g->u.is_armed() ) {
        se.description = _( "\"Drop your weapon!  Now!\"" );
        sounds::sound( se );
    }
    const SpeechBubble &speech = get_speech( z->type->id.str() );
    se.description = speech.text.translated();
    se.volume = speech.volume;
    sounds::sound( se );
    g->timed_events.add( TIMED_EVENT_ROBOT_ATTACK, calendar::turn + rng( 15_turns, 30_turns ), 0,
                         g->u.abs_sm_pos() );

    return true;
}


bool mattack::ratking( monster *z )
{
    if( z->friendly ) {
        // TODO: handle friendly monsters
        return false;
    }
    // Disable z-level ratting or it can get silly
    if( rl_dist( z->bub_pos(), g->u.bub_pos() ) > 50 || z->bub_pos().z() != g->u.bub_pos().z() ) {
        return false;
    }

    switch( rng( 1, 5 ) ) { // What do we say?
        case 1:
            add_msg( m_warning, _( "\"YOU… ARE FILTH…\"" ) );
            break;
        case 2:
            add_msg( m_warning, _( "\"VERMIN… YOU ARE VERMIN…\"" ) );
            break;
        case 3:
            add_msg( m_warning, _( "\"LEAVE NOW…\"" ) );
            break;
        case 4:
            add_msg( m_warning, _( "\"WE… WILL FEAST… UPON YOU…\"" ) );
            break;
        case 5:
            add_msg( m_warning, _( "\"FOUL INTERLOPER…\"" ) );
            break;
    }
    if( rl_dist( z->bub_pos(), g->u.bub_pos() ) <= 10 ) {
        g->u.add_effect( effect_rat, 3_minutes );
    }

    return true;
}

bool mattack::generator( monster *z )
{
    sound_event se;
    se.origin = z->bub_pos();
    se.volume = 90;
    se.category = sounds::sound_t::activity;
    se.description = "hmmmm";
    se.from_monster = true;
    se.monfaction = z->faction.id();
    se.faction = faction_id( "no_faction" );
    sounds::sound( se );
    if( calendar::once_every( 1_minutes ) && z->get_hp() < z->get_hp_max() ) {
        z->heal( 1 );
    }

    return true;
}

bool mattack::upgrade( monster *z )
{
    std::vector<monster *> targets;
    for( monster &zed : g->all_monsters() ) {
        // Check this first because it is a relatively cheap check
        if( zed.can_upgrade() ) {
            // Then do the more expensive ones
            if( z->attitude_to( zed ) != Attitude::A_HOSTILE &&
                within_target_range( z, &zed, 10 ) ) {
                targets.push_back( &zed );
            }
        }
    }
    if( targets.empty() ) {
        // Nobody to upgrade, get MAD!
        z->anger = 100;
        return false;
    } else {
        // We've got zombies to upgrade now, calm down again
        z->anger = 5;
    }

    // Takes one turn
    z->moves -= z->type->speed;

    monster *target = random_entry( targets );

    std::string old_name = target->name();
    const auto could_see = g->u.sees( *target );
    target->hasten_upgrade();
    target->try_upgrade( false );
    const auto can_see = g->u.sees( *target );
    if( g->u.sees( *z ) ) {
        if( could_see ) {
            //~ %1$s is the name of the zombie upgrading the other, %2$s is the zombie being upgraded.
            add_msg( m_warning, _( "A black mist floats from the %1$s around the %2$s." ),
                     z->name(), old_name );
        } else {
            add_msg( m_warning, _( "A black mist floats from the %s." ), z->name() );
        }
    }
    if( target->name() != old_name ) {
        if( could_see && can_see ) {
            //~ %1$s is the pre-upgrade monster, %2$s is the post-upgrade monster.
            add_msg( m_warning, _( "The %1$s becomes a %2$s!" ), old_name,
                     target->name() );
        } else if( could_see ) {
            add_msg( m_warning, _( "The %s vanishes!" ), old_name );
        } else if( can_see ) {
            add_msg( m_warning, _( "A %s appears!" ), target->name() );
        }
    }

    return true;
}

bool mattack::command_buff( monster *z )
{
    size_t aggroed = 0;
    const Creature *enemy = z->attack_target();
    if( enemy == nullptr ) {
        return false;
    }
    for( monster &ally : g->all_monsters() ) {
        if( rl_dist_fast( ally.bub_pos(), z->bub_pos() ) <= 30 &&
            ally.attitude_to( *z ) == Attitude::A_FRIENDLY ) {
            time_duration buff_dur = ally.get_effect_dur( effect_command_buff );
            time_duration half_max_dur = effect_command_buff->get_max_duration() / 2;
            if( buff_dur < half_max_dur ) {
                ally.add_effect( effect_command_buff, half_max_dur - buff_dur );
            }

            if( ally.move_target() != enemy->bub_pos() &&
                ally.attitude_to( *enemy ) == Attitude::A_HOSTILE ) {
                ally.set_dest( enemy->bub_pos() );
                aggroed++;
            }
        }
    }

    if( aggroed > 0 && enemy->is_avatar() ) {
        if( enemy->sees( *z ) ) {
            add_msg( m_warning, _( "%s points in your direction." ),  z->disp_name( false, true ) );
        }

        if( aggroed > 25 ) {
            add_msg( m_bad, _( "You feel intensely hated for a moment." ) );
        } else if( aggroed > 5 )        {
            add_msg( m_warning, _( "You feel an angry presence." ) );
        }
    }

    return true;
}

bool mattack::breathe( monster *z )
{
    // It takes a while
    z->moves -= 100;

    bool able = ( z->type->id == mon_breather_hub );
    if( !able ) {
        for( const tripoint_bub_ms &dest : g->m.points_in_radius( z->bub_pos(), 3 ) ) {
            monster *const mon = g->critter_at<monster>( dest );
            if( mon && mon->type->id == mon_breather_hub ) {
                able = true;
                break;
            }
        }
    }
    if( !able ) {
        return true;
    }

    if( monster *const spawned = g->place_critter_around( mon_breather, z->bub_pos(), 1 ) ) {
        spawned->reset_special( "BREATHE" );
        spawned->make_ally( *z );
    }

    return true;
}

