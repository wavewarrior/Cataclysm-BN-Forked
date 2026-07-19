#include "monster.h"
#include "coop_mutation_log.h"

#include "avatar.h"
#include "bodypart.h"
#include "catalua_hooks.h"
#include "catalua_sol.h"
#include "character.h"
#include "combat_feedback.h"
#include "creature_tracker.h"
#include "cursesdef.h"
#include "debug.h"
#include "effect.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "explosion.h"
#include "field_type.h"
#include "flag.h"
#include "flat_set.h"
#include "game.h"
#include "game_constants.h"
#include "int_id.h"
#include "item.h"
#include "item_category.h"
#include "item_group.h"
#include "itype.h"
#include "line.h"
#include "locations.h"
#include "make_static.h"
#include "map.h"
#include "map_iterator.h"
#include "mapdata.h"
#include "mattack_common.h"
#include "melee.h"
#include "messages.h"
#include "mission.h"
#include "mod_manager.h"
#include "mondeath.h"
#include "mondefense.h"
#include "monfaction.h"
#include "mongroup.h"
#include "morale_types.h"
#include "mtype.h"
#include "mutation.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player.h"
#include "profile.h"
#include "projectile.h"
#include "rng.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_utils.h"
#include "submap.h"
#include "text_snippets.h"
#include "translations.h"
#include "trap.h"
#include "weather.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>



static const ammo_effect_str_id ammo_effect_WHIP( "WHIP" );

static const efftype_id effect_attention( "attention" );
static const efftype_id effect_badpoison( "badpoison" );
static const efftype_id effect_beartrap( "beartrap" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_blind( "blind" );
static const efftype_id effect_bouldering( "bouldering" );
static const efftype_id effect_command_buff( "command_buff" );
static const efftype_id effect_crushed( "crushed" );
static const efftype_id effect_corroding( "corroding" );
static const efftype_id effect_dazed( "dazed" );
static const efftype_id effect_deaf( "deaf" );
static const efftype_id effect_docile( "docile" );
static const efftype_id effect_downed( "downed" );
static const efftype_id effect_emp( "emp" );
static const efftype_id effect_feral_infighting_punishment( "feral_infighting_punishment" );
static const efftype_id effect_feral_killed_recently( "feral_killed_recently" );
static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_heavysnare( "heavysnare" );
static const efftype_id effect_hit_by_player( "hit_by_player" );
static const efftype_id effect_in_pit( "in_pit" );
static const efftype_id effect_lightsnare( "lightsnare" );
static const efftype_id effect_migo_atmosphere( "migo_atmosphere" );
static const efftype_id effect_monster_armor( "monster_armor" );
static const efftype_id effect_monster_disarmed( "monster_disarmed" );
static const efftype_id effect_no_sight( "no_sight" );
static const efftype_id effect_onfire( "onfire" );
static const efftype_id effect_pacified( "pacified" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_tpollen( "tpollen" );
static const efftype_id effect_paralyzepoison( "paralyzepoison" );
static const efftype_id effect_poison( "poison" );
static const efftype_id effect_ridden( "ridden" );
static const efftype_id effect_run( "run" );
static const efftype_id effect_smoke( "smoke" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_supercharged( "supercharged" );
static const efftype_id effect_teargas( "teargas" );
static const efftype_id effect_tied( "tied" );
static const efftype_id effect_webbed( "webbed" );
static const efftype_id effect_well_fed( "well_fed" );

static const itype_id itype_corpse( "corpse" );
static const itype_id itype_milk( "milk" );
static const itype_id itype_milk_raw( "milk_raw" );

static const species_id FISH( "FISH" );
static const species_id FUNGUS( "FUNGUS" );
static const species_id INSECT( "INSECT" );
static const species_id MAMMAL( "MAMMAL" );
static const species_id MOLLUSK( "MOLLUSK" );
static const species_id PLANT( "PLANT" );
static const species_id ROBOT( "ROBOT" );
static const species_id ZOMBIE( "ZOMBIE" );

static const trait_id trait_ANIMALDISCORD( "ANIMALDISCORD" );
static const trait_id trait_ANIMALDISCORD2( "ANIMALDISCORD2" );
static const trait_id trait_ANIMALEMPATH( "ANIMALEMPATH" );
static const trait_id trait_ANIMALEMPATH2( "ANIMALEMPATH2" );
static const trait_id trait_BEE( "BEE" );
static const trait_id trait_FLOWERS( "FLOWERS" );
static const trait_id trait_INATTENTIVE( "INATTENTIVE" );
static const trait_id trait_KILLER( "KILLER" );
static const trait_id trait_MYCUS_FRIEND( "MYCUS_FRIEND" );
static const trait_id trait_PACIFIST( "PACIFIST" );
static const trait_id trait_PHEROMONE_INSECT( "PHEROMONE_INSECT" );
static const trait_id trait_PHEROMONE_MAMMAL( "PHEROMONE_MAMMAL" );
static const trait_id trait_PROF_FERAL( "PROF_FERAL" );
static const trait_id trait_TERRIFYING( "TERRIFYING" );
static const trait_id trait_THRESH_MYCUS( "THRESH_MYCUS" );

// Defined in monster.cpp
auto find_targets_to_ungrab( const tripoint_bub_ms& pos ) -> std::vector<player *>;
auto get_item_category_spawn_rate( const item& itm ) -> float; // *NOPAD*

bool monster::is_immune_damage( const damage_type dt ) const
{
    switch( dt ) {
    case DT_NULL:
        return true;
    case DT_TRUE:
        return false;
    case DT_BIOLOGICAL:
        return has_flag( MF_BIOPROOF );
        case DT_BASH:
            return false;
        case DT_CUT:
            return false;
        case DT_ACID:
            return has_flag( MF_ACIDPROOF );
        case DT_STAB:
            return false;
        case DT_HEAT:
            return has_flag( MF_FIREPROOF );
        case DT_COLD:
            return has_flag( MF_COLDPROOF );
        case DT_DARK:
            return has_flag( MF_DARKPROOF );
        case DT_LIGHT:
            return has_flag( MF_LIGHTPROOF );
        case DT_PSI:
            return has_flag( MF_PSIPROOF );
        case DT_ELECTRIC:
            return type->sp_defense == &mdefense::zapback || has_flag( MF_ELECTRIC )
                   || has_flag( MF_ELECTRIC_FIELD );
        case DT_BULLET:
            return false;
        default:
            return true;
    }
}

bool monster::is_dead_state() const { return hp <= 0; }

bool monster::block_hit( Creature*, bodypart_id &, damage_instance & ) { return false; }

bool monster::block_ranged_hit( Creature*, bodypart_id &, damage_instance & ) { return false; }

void monster::absorb_hit( const bodypart_id &, damage_instance& dam )
{
    resistances res = resists();
    for( auto& elem : dam.damage_units ) {
        add_msg( m_debug, "Dam Type: %s :: Ar Pen: %.1f :: Armor Mult: %.1f", name_by_dt( elem.type ),
                 elem.res_pen, elem.res_mult );
        elem.amount -= std::min( res.get_effective_resist( elem ), elem.amount );
    }
}

void monster::melee_attack( Creature& target ) { melee_attack( target, get_hit() ); }

void monster::melee_attack( Creature& target, float accuracy )
{
    mod_moves( -type->attack_cost );
    if( type->melee_dice == 0 ) {
        // We don't attack, so just return
        return;
    }

    if( this == &target ) {
        // This happens sometimes
        return;
    }

    if( !can_squeeze_to( target.bub_pos() ) ) { return; }

    anim_on_attack( target.bub_pos(), false ); // sprite lunge toward the target
    int hitspread = target.deal_melee_attack( this, melee::melee_hit_range( accuracy ) );
    const bool attack_success = hitspread >= 0;

    if( target.is_player()
        || ( target.is_npc() && g->u.attitude_to( target ) == Attitude::A_FRIENDLY ) ) {
        // Make us a valid target
        add_effect( effect_hit_by_player, 10_minutes );
    }

    if( has_flag( MF_HIT_AND_RUN ) ) { add_effect( effect_run, 4_turns ); }

    const bool u_see_me = g->u.sees( *this );

    damage_instance damage = !is_hallucination() ? type->melee_damage : damage_instance();
    if( !is_hallucination() && type->melee_dice > 0 ) {
        damage.add_damage(
            DT_BASH,
            dice( type->melee_dice,
                  has_effect( effect_monster_disarmed ) ? type->melee_sides / 2 : type->melee_sides ) );
        damage.add_damage( DT_BASH, bash_bonus );
        damage.add_damage( DT_CUT, cut_bonus );
        if( has_effect( effect_monster_disarmed ) ) {
            for( damage_unit& elem : damage.damage_units ) {
                if( elem.amount > 0 && ( elem.type != DT_BASH ) ) {
                    elem.amount = 0;
                    continue;
                }
            }
        }
    }

    dealt_damage_instance dealt_dam;

    if( attack_success ) {
        // Compute monster crit chance based on hitspread margin.
        // Base 5% crit chance, scaling up with how comfortably the hit connects.
        // At hitspread >= 20, maxes out at ~15% crit chance.
        const double monster_crit_chance = std::min( 0.15, 0.05 + ( hitspread * 0.005 ) );
        const bool critical_hit = one_in( 1.0 / monster_crit_chance );

        target.deal_melee_hit( this, hitspread, critical_hit, false, damage, dealt_dam );
    }
    const bodypart_str_id bp_hit = dealt_dam.bp_hit;

    const int total_dealt = dealt_dam.total_damage();
    if( !attack_success ) {
        // Miss
        if( u_see_me && !target.in_sleep_state() ) {
            if( target.is_player() ) {
                add_msg( _( "You dodge %s." ), disp_name() );
            } else if( target.is_npc() ) {
                add_msg( _( "%1$s dodges %2$s attack." ), target.disp_name(), disp_name( true ) );
            } else {
                add_msg( _( "%1$s misses %2$s!" ), disp_name( false, true ), target.disp_name() );
            }
        } else if( target.is_player() ) {
            add_msg( _( "You dodge an attack from an unseen source." ) );
        }
    } else if( is_hallucination() || total_dealt > 0 ) {
        // Hallucinations always produce messages but never actually deal damage
        if( u_see_me ) {
            if( target.is_player() ) {
                sfx::play_variant_sound(
                    "melee_attack", "monster_melee_hit", sfx::get_heard_volume( target.bub_pos() ) );
                sfx::do_player_death_hurt( dynamic_cast<player &>( target ), false );
                //~ 1$s is attacker name, 2$s is bodypart name in accusative.
                add_msg( m_bad, _( "%1$s hits your %2$s." ), disp_name( false, true ),
                         bp_hit->accusative.translated() );
            } else if( target.is_npc() ) {
                if( has_effect( effect_ridden ) && has_flag( MF_RIDEABLE_MECH )
                    && bub_pos() == g->u.bub_pos() ) {
                    //~ %1$s: name of your mount, %2$s: target NPC name, %3$d: damage value
                    add_msg( m_good, _( "Your %1$s hits %2$s for %3$d damage!" ), name(),
                             target.disp_name(), total_dealt );
                } else {
                    //~ %1$s: attacker name, %2$s: target NPC name, %3$s: bodypart name in
                    //accusative
                    add_msg( _( "%1$s hits %2$s %3$s." ), disp_name( false, true ),
                             target.disp_name( true ), bp_hit->accusative.translated() );
                }
            } else {
                if( has_effect( effect_ridden ) && has_flag( MF_RIDEABLE_MECH )
                    && bub_pos() == g->u.bub_pos() ) {
                    //~ %1$s: name of your mount, %2$s: target creature name, %3$d: damage value
                    add_msg( m_good, _( "Your %1$s hits %2$s for %3$d damage!" ), get_name(),
                             target.disp_name(), total_dealt );
                } else {
                    //~ %1$s: attacker name, %2$s: target creature name
                    add_msg( _( "%1$s hits %2$s!" ), disp_name( false, true ), target.disp_name() );
                }
            }
        } else if( target.is_player() ) {
            //~ %s is bodypart name in accusative.
            add_msg( m_bad, _( "Something hits your %s." ), bp_hit->accusative.translated() );
        }
    } else {
        // No damage dealt
        if( u_see_me ) {
            if( target.is_player() ) {
                //~ 1$s is attacker name, 2$s is bodypart name in accusative, 3$s is armor name
                add_msg( _( "%1$s hits your %2$s, but your %3$s protects you." ),
                         disp_name( false, true ), bp_hit->accusative.translated(),
                         target.skin_name() );
            } else if( target.is_npc() ) {
                //~ $1s is monster name, %2$s is that monster target name,
                //~ $3s is target bodypart name in accusative, $4s is the monster target name,
                //~ 5$s is target armor name.
                add_msg(
                    _( "%1$s hits %2$s %3$s but is stopped by %4$s %5$s." ), disp_name( false, true ),
                    target.disp_name( true ), bp_hit->accusative.translated(), target.disp_name( true ),
                    target.skin_name() );
            } else {
                //~ $1s is monster name, %2$s is that monster target name,
                //~ $3s is target armor name.
                add_msg( _( "%1$s hits %2$s but is stopped by its %3$s." ), disp_name( false, true ),
                         target.disp_name(), target.skin_name() );
            }
        } else if( target.is_player() ) {
            //~ 1$s is bodypart name in accusative, 2$s is armor name.
            add_msg( _( "Something hits your %1$s, but your %2$s protects you." ),
                     bp_hit->accusative.translated(), target.skin_name() );
        }
    }

    target.check_dead_state();

    cata::run_hooks( "on_creature_melee_attacked", [ &, this]( auto & params ) {
        params["char"] = this;
        params["target"] = &target;
        params["success"] = attack_success;
    } );

    if( is_hallucination() ) {
        if( one_in( 7 ) ) { die( nullptr ); }
        return;
    }

    if( total_dealt <= 0 ) { return; }

    // Add any on damage effects
    for( const auto& eff : type->atk_effs ) {
        if( x_in_y( eff.chance, 100 ) ) {
            const bodypart_str_id& affected_bp = eff.affect_hit_bp ? bp_hit : convert_bp( eff.bp );
            target.add_effect( eff.id, time_duration::from_turns( eff.duration ), affected_bp );
            if( eff.permanent ) { target.get_effect( eff.id, affected_bp ).set_permanent(); }
        }
    }

    const int stab_cut = dealt_dam.type_damage( DT_CUT ) + dealt_dam.type_damage( DT_STAB );

    if( stab_cut > 0 && has_flag( MF_VENOM ) ) {
        target.add_msg_if_player( m_bad, _( "You're envenomed!" ) );
        target.add_effect( effect_poison, 3_minutes );
    }

    if( stab_cut > 0 && has_flag( MF_BADVENOM ) ) {
        target.add_msg_if_player( m_bad, _( "You feel venom flood your body, wracking you with "
                                            "pain…" ) );
        target.add_effect( effect_badpoison, 4_minutes );
    }

    if( stab_cut > 0 && has_flag( MF_PARALYZE ) ) {
        target.add_msg_if_player( m_bad, _( "You feel venom enter your body!" ) );
        target.add_effect( effect_paralyzepoison, 10_minutes );
    }

    if( total_dealt > 6 && stab_cut > 0 && has_flag( MF_BLEED ) ) {
        // Maybe should only be if DT_CUT > 6... Balance question
        target.add_effect( effect_bleed, 6_minutes, bp_hit );
    }
}

void monster::deal_projectile_attack(
    Creature* source, dealt_projectile_attack& attack, bool is_graze )
{
    deal_projectile_attack( source, nullptr, attack, is_graze );
}

void monster::deal_projectile_attack(
    Creature* source, item* source_weapon, dealt_projectile_attack& attack, bool is_graze )
{
    const auto& proj = attack.proj;
    double &missed_by = attack.missed_by;

    if( proj.has_effect( ammo_effect_WHIP ) && type->in_category( "WILDLIFE" ) && one_in( 3 ) ) {
        add_effect( effect_run, rng( 3_turns, 5_turns ) );
    }

    if( missed_by > 1.0 ) { return; }

    Creature::deal_projectile_attack( source, source_weapon, attack, is_graze );

    if( !is_hallucination() && attack.hit_critter == this ) {
        on_hit( source, bodypart_id( "torso" ), &attack, false );
    }
}

void monster::apply_damage(
    Creature* source, item* source_weapon, item* source_projectile, bodypart_id /*bp*/, int dam,
    const bool /*bypass_med*/ )
{
    if( is_dead_state() ) { return; }
    hp -= dam;
    if( hp < 1 ) {
        set_killer( source );
        if( source_weapon ) { source_weapon->add_monster_kill( type->id ); }
        if( source_projectile ) { source_projectile->add_monster_kill( type->id ); }
    } else if( dam > 0 ) {
        mfaction_id attacker_faction;
        if( source != nullptr ) {
            const monster* source_monster = source->as_monster();
            if( source_monster != nullptr ) {
                attacker_faction = source_monster->faction;
            } else if( ( source->is_player() || source->is_npc() ) && !source->is_fake() ) {
                // Only attribute to player faction if it's a real player/NPC, not a fake NPC from a
                // monster turret
                attacker_faction = mfaction_id( "player" );
            }
        }

        process_trigger( mon_trigger::HURT, 1 + ( dam / 3 ), attacker_faction );

        if( source != nullptr && !aggro_character && !source->is_monster() && !source->is_fake() ) {
            trigger_character_aggro( "hurt" );
        }
    }
}

void monster::apply_damage(
    Creature* source, item* source_weapon, bodypart_id bp, int dam, const bool bypass_med )
{
    apply_damage( source, source_weapon, nullptr, bp, dam, bypass_med );
}

void monster::apply_damage( Creature* source, bodypart_id bp, int dam, const bool bypass_med )
{
    apply_damage( source, nullptr, nullptr, bp, dam, bypass_med );
}

void monster::die_in_explosion( Creature* source )
{
    hp = -9999; // huge to trigger explosion and prevent corpse item
    die( source );
}

int monster::get_armor_bash( bodypart_id bp ) const
{
    ( void )bp;
    return static_cast<int>( type->armor_bash ) + armor_bash_bonus + get_worn_armor_val( DT_BASH );
}

float monster::get_hit_base() const
{
    float base = type->melee_skill;
    if( training_level > 0 && type->pet_training ) {
        base *= std::pow( type->pet_training->melee, training_level );
    }
    return base;
}

float monster::get_dodge_base() const
{
    float base = type->sk_dodge;
    if( training_level > 0 && type->pet_training ) {
        base *= std::pow( type->pet_training->dodge, training_level );
    }
    return base;
}

float monster::hit_roll() const
{
    float hit = get_hit();
    if( has_effect( effect_bouldering ) ) { hit /= 4; }

    return melee::melee_hit_range( hit );
}

float monster::get_dodge() const
{
    if( has_effect( effect_downed ) ) { return 0.0f; }

float ret = Creature::get_dodge();
if( has_effect( effect_lightsnare ) || has_effect( effect_heavysnare )
        || has_effect( effect_beartrap ) || has_effect( effect_tied ) ) {
    ret /= 2;
}

if( has_effect( effect_bouldering ) ) { ret /= 4; }

return ret;
}

float monster::get_melee() const
{
    float base = type->melee_skill;
    if( training_level > 0 && type->pet_training ) {
        base *= std::pow( type->pet_training->melee, training_level );
    }
    return base;
}

float monster::dodge_roll() { return get_dodge() * 5; }

float monster::fall_damage_mod() const
{
    if( flies() ) { return 0.0f; }

switch( type->size ) {
    case creature_size::tiny:
        return 0.2f;
    case creature_size::small:
        return 0.6f;
    case creature_size::medium:
        return 1.0f;
    case creature_size::large:
        return 1.4f;
    case creature_size::huge:
        return 2.0f;
    default:
        return 1.0f;
}

return 0.0f;
}

void monster::die( Creature* nkiller )
{
    if( dead ) {
        // We are already dead, don't die again, note that monster::dead is
        // *only* set to true in this function!
        return;
    }
    // We were carrying a creature, deposit the rider
    if( has_effect( effect_ridden ) && mounted_player ) { mounted_player->forced_dismount(); }
    g->set_critter_died();
    dead = true;
    if( auto * _log = coop_mutation_log::current() ) {
        _log->push( {coop_event_type::creature_died, abs_pos(), 0, 0} );
    }
    set_killer( nkiller );
    if( !death_drops ) { return; }
    if( !no_extra_death_drops ) {
        drop_items_on_death();
        drop_monster_weapon();
    }
    // TODO: should actually be class Character
    player* ch = dynamic_cast<player *>( get_killer() );
    if( !is_hallucination() && ch != nullptr ) {
        if( ( has_flag( MF_GUILT ) && ch->is_player() )
            || ( ch->has_trait( trait_PACIFIST ) && has_flag( MF_HUMAN ) ) ) {
            // has guilt flag or player is pacifist && monster is humanoid
            mdeath::guilt( *this );
        }
        g->events().send<event_type::character_kills_monster>( ch->getID(), type->id );
        if( ch->is_player() && ch->has_trait( trait_KILLER ) ) {
            if( one_in( 4 ) ) {
                const translation snip =
                    SNIPPET.random_from_category( "killer_on_kill" ).value_or( translation() );
                ch->add_msg_if_player( m_good, "%s", snip );
            }
            ch->add_morale( MORALE_KILLER_HAS_KILLED, 5, 10, 6_hours, 4_hours );
            ch->rem_morale( MORALE_KILLER_NEED_TO_KILL );
        }
        static const string_id<monfaction> faction_zombie( "zombie" );
        // Feral survivors are motivated to kill anything human
        if( ch->has_trait( trait_PROF_FERAL ) && has_flag( MF_HUMAN ) ) {
            if( !ch->has_effect( effect_feral_killed_recently ) ) {
                ch->add_msg_if_player( m_good, _( "The voices in your head quiet down a bit." ) );
            }
            if( faction != faction_zombie && !type->in_species( ZOMBIE ) ) {
                ch->add_effect( effect_feral_killed_recently, 3_days );
            } else {
                // Killing fellow ferals works but is less efficient, and comes with risk of
                // punishment.
                ch->add_effect( effect_feral_killed_recently, 6_hours );
                if( one_in( 3 ) ) {
                    ch->add_msg_if_player( m_bad, _( "The rush of blood seems to drive off the smell "
                                                     "of decay for a moment." ) );
                    ch->add_effect( effect_feral_infighting_punishment, 6_hours );
                }
            }
        }
    }
    // Drop items stored in optionals
    add_item( remove_tack_item() );
    add_item( remove_armor_item() );
    add_item( remove_storage_item() );
    add_item( remove_tied_item() );
    add_item( remove_battery_item() );

    if( has_effect( effect_lightsnare ) ) {
        add_item( item::spawn( "string_36", calendar::start_of_cataclysm ) );
        add_item( item::spawn( "snare_trigger", calendar::start_of_cataclysm ) );
    }
    if( has_effect( effect_heavysnare ) ) {
        add_item( item::spawn( "rope_6", calendar::start_of_cataclysm ) );
        add_item( item::spawn( "snare_trigger", calendar::start_of_cataclysm ) );
    }
    if( has_effect( effect_beartrap ) ) {
        add_item( item::spawn( "beartrap", calendar::start_of_cataclysm ) );
    }
    if( has_effect( effect_grabbing ) ) {
        remove_effect( effect_grabbing );
        for( player * p : find_targets_to_ungrab( bub_pos() ) ) {
            p->add_msg_player_or_npc(
                m_good, _( "The last enemy holding you collapses!" ),
                _( "The last enemy holding <npcname> collapses!" ) );
            p->remove_effect( effect_grabbed );
        }
    }
    if( !is_hallucination() ) {
        for( detached_ptr<item> &it : inv.clear() ) {
            g->m.add_item_or_charges( bub_pos(), std::move( it ) );
        }
    }

    // If we're a queen, make nearby groups of our type start to die out
    if( !is_hallucination() && has_flag( MF_QUEEN ) ) {
        // The submap coordinates of this monster, monster groups coordinates are
        // submap coordinates.
        const auto abssub = project_to<coords::sm>( g->m.bub_to_abs( bub_pos() ) );
        // Do it for overmap above/below too
        for( const auto& p : points_in_radius( abssub, g_half_mapsize, 1 ) ) {
            // TODO: fix point types
            for( auto& mgp : get_overmapbuffer( dimension_id_ ).groups_at( tripoint_abs_sm( p ) ) ) {
                if( MonsterGroupManager::IsMonsterInGroup( mgp->type, type->id ) ) {
                    mgp->dying = true;
                }
            }
        }
    }
    mission::on_creature_death( *this );
    // Also, perform our death function
    if( is_hallucination() || summon_time_limit ) {
        // Hallucinations always just disappear
        mdeath::disappear( *this );
        return;
    }

    // Not a hallucination, go process the death effects.
    for( const auto& deathfunction : type->dies ) { deathfunction( *this ); }
    // Process other on-death triggers (spawn monster(s), etc)
    for( const auto& deathfunction : type->on_death ) { deathfunction( *this ); }

    // Determine killer's faction
    mfaction_id killer_faction;
    if( nkiller != nullptr ) {
        const monster* killer_monster = nkiller->as_monster();
        if( killer_monster != nullptr ) {
            killer_faction = killer_monster->faction;
        } else if( ( nkiller->is_player() || nkiller->is_npc() ) && !nkiller->is_fake() ) {
            // Only attribute to player faction if it's a real player/NPC, not a fake NPC from a
            // monster turret
            killer_faction = mfaction_id( "player" );
        }
    }

    // If our species fears seeing one of our own die, process that
    int anger_adjust = 0;
    int morale_adjust = 0;
    if( type->has_anger_trigger( mon_trigger::FRIEND_DIED ) ) {
        anger_adjust += 15;
        if( nkiller != nullptr && !nkiller->is_monster() && !nkiller->is_fake() ) {
            // A character killed our friend
            trigger_character_aggro( "killing a friendly creature" );
        }
    }
    if( type->has_fear_trigger( mon_trigger::FRIEND_DIED ) ) { morale_adjust -= 15; }
    if( type->has_placate_trigger( mon_trigger::FRIEND_DIED ) ) { anger_adjust -= 15; }

    if( anger_adjust != 0 || morale_adjust != 0 ) {
        int light = g->light_level( bub_pos().z() );
        for( monster& critter : g->all_monsters() ) {
            if( critter.faction != this->faction ) { continue; }

            if( g->m.sees( critter.bub_pos(), bub_pos(), light ) ) {
                critter.morale += morale_adjust;

                if( critter.has_flag( MF_FACTION_MEMORY ) && killer_faction.is_valid() ) {
                    critter.add_faction_anger( killer_faction, anger_adjust );
                } else {
                    critter.anger += anger_adjust;
                }
            }
        }
    }
    cata::run_hooks( "on_mon_death", [ &, this]( auto & params ) {
        params["mon"] = this;
        params["killer"] = get_killer();
    } );
}

void monster::drop_items_on_death()
{
    if( is_hallucination() ) { return; }
    if( !type->death_drops ) { return; }

    auto items = item_group::items_from( type->death_drops, calendar::start_of_cataclysm );

    // Apply both global and category-specific spawn rates
    const auto global_spawn_rate = get_option<float>( "ITEM_SPAWNRATE" );

    // Filter items based on combined spawn rates using std::erase_if
    std::erase_if( items, [global_spawn_rate]( const auto & it ) {
        // Always keep mission items
        if( it->has_flag( flag_MISSION_ITEM ) ) {
            return false; // keep
        }

        // Calculate combined rate: global × category
        const auto category_rate = get_item_category_spawn_rate( *it );
        const auto final_rate = std::min( global_spawn_rate * category_rate, 1.0f );

        // Remove item based on final probability (erase_if removes when predicate is true)
        return rng_float( 0, 1 ) >= final_rate;
    } );

    // If there aren't any items left, there's nothing left to do
    if( items.empty() ) { return; }

    g->m.spawn_items( bub_pos(), std::move( items ) );
}

void monster::drop_monster_weapon()
{
    if( is_hallucination() ) { return; }
    if( !type->monster_weapon ) { return; }
    if( has_effect( effect_monster_disarmed ) ) { return; }

    auto items = item_group::items_from( type->monster_weapon, calendar::start_of_cataclysm );

    // Apply both global and category-specific spawn rates
    const auto global_spawn_rate = get_option<float>( "ITEM_SPAWNRATE" );

    // Filter items based on combined spawn rates using std::erase_if
    std::erase_if( items, [global_spawn_rate]( const auto & it ) {
        // Always keep mission items
        if( it->has_flag( flag_MISSION_ITEM ) ) {
            return false; // keep
        }

        // Calculate combined rate: global × category
        const auto category_rate = get_item_category_spawn_rate( *it );
        const auto final_rate = std::min( global_spawn_rate * category_rate, 1.0f );

        // Remove item based on final probability (erase_if removes when predicate is true)
        return rng_float( 0, 1 ) >= final_rate;
    } );

    // If there aren't any items left, there's nothing left to do
    if( items.empty() ) { return; }

    g->m.spawn_items( bub_pos(), std::move( items ) );
}

bool monster::is_dead() const { return dead || is_dead_state(); }

void monster::on_hit( Creature* source, bodypart_id, dealt_projectile_attack const* const proj )
{
    this->on_hit( source, bodypart_id( "torso" ), proj, false );
}

void monster::on_hit(
    Creature* source, bodypart_id, dealt_projectile_attack const* const proj,
    bool manual_retaliation )
{
    if( is_hallucination() ) { return; }

    if( rng( 0, 100 ) <= static_cast<int>( type->def_chance ) && !manual_retaliation ) {
        type->sp_defense( *this, source, proj );
    }

    // Determine attacker's faction
    mfaction_id attacker_faction;
    if( source != nullptr ) {
        const monster* source_monster = source->as_monster();
        if( source_monster != nullptr ) {
            attacker_faction = source_monster->faction;
        } else if( ( source->is_player() || source->is_npc() ) && !source->is_fake() ) {
            // Only attribute to player faction if it's a real player/NPC, not a fake NPC from a
            // monster turret
            attacker_faction = mfaction_id( "player" );
        }
    }

    // Adjust anger/morale of same-species monsters, if appropriate
    int anger_adjust = 0;
    int morale_adjust = 0;
    if( type->has_anger_trigger( mon_trigger::FRIEND_ATTACKED ) ) {
        anger_adjust += 15;
        if( source != nullptr && !aggro_character && !source->is_monster() && !source->is_fake() ) {
            // A character attacked our friend
            trigger_character_aggro( "killing a friendly creature" );
        }
    }
    if( type->has_fear_trigger( mon_trigger::FRIEND_ATTACKED ) ) { morale_adjust -= 15; }
    if( type->has_placate_trigger( mon_trigger::FRIEND_ATTACKED ) ) { anger_adjust -= 15; }

    if( anger_adjust != 0 || morale_adjust != 0 ) {
        int light = g->light_level( bub_pos().z() );
        for( monster& critter : g->all_monsters() ) {
            if( critter.faction != this->faction ) { continue; }

            if( g->m.sees( critter.bub_pos(), bub_pos(), light ) ) {
                critter.morale += morale_adjust;

                if( critter.has_flag( MF_FACTION_MEMORY ) && attacker_faction.is_valid() ) {
                    critter.add_faction_anger( attacker_faction, anger_adjust );
                } else {
                    critter.anger += anger_adjust;
                }
            }
        }
    }

    check_dead_state();
    // TODO: Faction relations
}

void monster::on_damage_of_type( int amt, damage_type dt, const bodypart_id& bp )
{
    Creature::on_damage_of_type( amt, dt, bp );
    int full_hp = get_hp_max();
    if( has_effect( effect_grabbing ) && ( dt == DT_BASH || dt == DT_CUT || dt == DT_STAB )
        && x_in_y( amt * 10, full_hp ) ) {
        remove_effect( effect_grabbing );
        for( player * p : find_targets_to_ungrab( bub_pos() ) ) {
            p->add_msg_player_or_npc(
                m_good, _( "%s flinches, letting you go!" ), _( "%s flinches, letting <npcname> go!" ),
                disp_name( false, true ) );
            p->remove_effect( effect_grabbed );
        }
    }
}

void monster::drop_items( const tripoint_bub_ms& p )
{
    for( detached_ptr<item> &it : inv.clear() ) { g->m.add_item_or_charges( p, std::move( it ) ); }
}

void monster::drop_items() { drop_items( bub_pos() ); }

void monster::add_corpse_component( detached_ptr<item>&& it )
{
    corpse_components.push_back( std::move( it ) );
}

detached_ptr<item> monster::remove_corpse_component( item& it )
{
    for( auto iter = corpse_components.begin(); iter != corpse_components.end(); iter++ ) {
        if( *iter == &it ) {
            detached_ptr<item> ret;
            corpse_components.erase( iter, &ret );
            return ret;
        }
    }
    return detached_ptr<item>();
}

std::vector<detached_ptr<item>> monster::remove_corpse_components()
{
    return corpse_components.clear();
}

