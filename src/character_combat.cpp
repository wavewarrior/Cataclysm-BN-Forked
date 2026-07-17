#include "character.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "catalua_hooks.h"
#include "catalua_sol.h"
#include "character_martial_arts.h"
#include "color.h"
#include "combat_feedback.h"
#include "creature.h"
#include "damage.h"
#include "activity_handlers.h"
#include "debug.h"
#include "effect.h"
#include "enums.h"
#include "event.h"
#include "event_bus.h"
#include "flag.h"
#include "game.h"
#include "item.h"
#include "magic_enchantment.h"
#include "map.h"
#include "material.h"
#include "memorial_logger.h"
#include "messages.h"
#include "morale.h"
#include "monster.h"
#include "mutation.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "rng.h"
#include "sounds.h"
#include "translations.h"
#include "type_id.h"

static const bionic_id bio_ads( "bio_ads" );
static const bionic_id bio_ods( "bio_ods" );
static const efftype_id effect_adrenaline( "adrenaline" );
static const efftype_id effect_bandaged( "bandaged" );
static const efftype_id effect_blind( "blind" );
static const efftype_id effect_disabled( "disabled" );
static const efftype_id effect_disinfected( "disinfected" );
static const efftype_id effect_downed( "downed" );
static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_grabbing( "grabbing" );
static const efftype_id effect_narcosis( "narcosis" );
static const efftype_id effect_onfire( "onfire" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_stunned( "stunned" );
static const mtype_id mon_player_blob( "mon_player_blob" );
static const mtype_id mon_shadow_snake( "mon_shadow_snake" );
static const skill_id skill_dodge( "dodge" );
static const trait_id trait_ACIDBLOOD( "ACIDBLOOD" );
static const trait_id trait_ADRENALINE( "ADRENALINE" );
static const trait_id trait_CF_HAIR( "CF_HAIR" );
static const trait_id trait_DEBUG_NODMG( "DEBUG_NODMG" );
static const trait_id trait_DEFT( "DEFT" );
static const trait_id trait_HOLLOW_BONES( "HOLLOW_BONES" );
static const trait_id trait_LIGHT_BONES( "LIGHT_BONES" );
static const trait_id trait_PROF_SKATER( "PROF_SKATER" );
static const trait_id trait_QUILLS( "QUILLS" );
static const trait_id trait_SLIMESPAWNER( "SLIMESPAWNER" );
static const trait_id trait_SPINES( "SPINES" );
static const trait_id trait_THORNS( "THORNS" );

void Character::passive_absorb_hit( const bodypart_id& bp, damage_unit& du ) const
{
    // >0 check because some mutations provide negative armor
    // Thin skin check goes before subdermal armor plates because SUBdermal
    if( du.amount > 0.0f ) {
    // HACK: Get rid of this as soon as CUT and STAB are split
    if( du.type == DT_STAB ) {
            damage_unit du_copy = du;
            du_copy.type = DT_CUT;
            du.amount -= mutation_armor( bp, du_copy );
        } else {
            du.amount -= mutation_armor( bp, du );
        }
    }
    du.amount -= bionic_armor_bonus( bp, du.type ); // Check for passive armor bionics
    du.amount -= mabuff_armor_bonus( du.type );
    du.amount = std::max( 0.0f, du.amount );
}

static void destroyed_armor_msg(
    Character& who, const std::string& pre_damage_name, const bool holds_items,
    units::mass item_weight, units::volume item_volume )
{
    const bool show_popup = get_option<bool>( "CLOTHING_DESTRUCTION_POPUP" );
    const bool container_only = !get_option<bool>( "CLOTHING_DESTRUCTION_POPUP_CONTENTS" );
    const units::mass required_weight = units::from_gram( get_option<int>(
                                            "CLOTHING_DESTRUCTION_"
                                            "POPUP_MIN_WEIGHT" ) );
    const units::volume required_volume = units::from_milliliter( get_option<int>(
            "CLOTHING_"
            "DESTRUCTION_"
            "POPUP_MIN_"
            "VOLUME" ) );
    const bool weight_ok = required_weight == units::mass{} || item_weight >= required_weight;
    const bool volume_ok = required_volume == units::volume{} || item_volume >= required_volume;
    const bool contents_ok = !container_only || holds_items;
    const bool should_show_popup = show_popup && weight_ok && volume_ok && contents_ok;
    if( who.is_avatar() ) {
        g->memorial().add(
            //~ %s is armor name
            pgettext( "memorial_male", "Worn %s was completely destroyed." ),
            pgettext( "memorial_female", "Worn %s was completely destroyed." ), pre_damage_name );
        if( should_show_popup ) { popup( _( "Your %s is completely destroyed!" ), pre_damage_name ); }
    } else if( who.is_npc() && who.as_npc()->is_following() && should_show_popup ) {
        popup( _( "%1$s's %2$s is completely destroyed!" ), who.as_npc()->get_name(), pre_damage_name );
    }
    who.add_msg_player_or_npc(
        m_bad, _( "Your %s is completely destroyed!" ), _( "<npcname>'s %s is completely destroyed!" ),
        pre_damage_name );
}

static void item_armor_enchantment_adjust(
    const Character& guy, damage_unit& du, const item& armor )
{
    switch( du.type ) {
        case DT_ACID:
            du.amount +=
                armor.bonus_from_enchantments( guy, du.amount, enchant_vals::mod::ITEM_ARMOR_ACID );
            break;
        case DT_BASH:
            du.amount +=
                armor.bonus_from_enchantments( guy, du.amount, enchant_vals::mod::ITEM_ARMOR_BASH );
            break;
        case DT_BIOLOGICAL:
            du.amount +=
                armor.bonus_from_enchantments( guy, du.amount, enchant_vals::mod::ITEM_ARMOR_BIO );
            break;
        case DT_COLD:
            du.amount +=
                armor.bonus_from_enchantments( guy, du.amount, enchant_vals::mod::ITEM_ARMOR_COLD );
            break;
        case DT_DARK:
            du.amount +=
                armor.bonus_from_enchantments( guy, du.amount, enchant_vals::mod::ITEM_ARMOR_DARK );
            break;
        case DT_LIGHT:
            du.amount +=
                armor.bonus_from_enchantments( guy, du.amount, enchant_vals::mod::ITEM_ARMOR_LIGHT );
            break;
        case DT_PSI:
            du.amount +=
                armor.bonus_from_enchantments( guy, du.amount, enchant_vals::mod::ITEM_ARMOR_PSI );
            break;
        case DT_CUT:
            du.amount +=
                armor.bonus_from_enchantments( guy, du.amount, enchant_vals::mod::ITEM_ARMOR_CUT );
            break;
        case DT_ELECTRIC:
            du.amount +=
                armor.bonus_from_enchantments( guy, du.amount, enchant_vals::mod::ITEM_ARMOR_ELEC );
            break;
        case DT_HEAT:
            du.amount +=
                armor.bonus_from_enchantments( guy, du.amount, enchant_vals::mod::ITEM_ARMOR_HEAT );
            break;
        case DT_STAB:
            du.amount +=
                armor.bonus_from_enchantments( guy, du.amount, enchant_vals::mod::ITEM_ARMOR_STAB );
            break;
        case DT_BULLET:
            du.amount +=
                armor.bonus_from_enchantments( guy, du.amount, enchant_vals::mod::ITEM_ARMOR_BULLET );
            break;
        default:
            return;
    }
    du.amount = std::max( 0.0f, du.amount );
}

// adjusts damage unit depending on type by enchantments.
// the ITEM_ enchantments only affect the damage resistance for that one item, while the others
// affect all of them
static void armor_enchantment_adjust( const Character& guy, damage_unit& du )
{
    switch( du.type ) {
        case DT_ACID:
            du.amount += guy.bonus_from_enchantments( du.amount, enchant_vals::mod::ARMOR_ACID );
            break;
        case DT_BASH:
            du.amount += guy.bonus_from_enchantments( du.amount, enchant_vals::mod::ARMOR_BASH );
            break;
        case DT_BIOLOGICAL:
            du.amount += guy.bonus_from_enchantments( du.amount, enchant_vals::mod::ARMOR_BIO );
            break;
        case DT_COLD:
            du.amount += guy.bonus_from_enchantments( du.amount, enchant_vals::mod::ARMOR_COLD );
            break;
        case DT_DARK:
            du.amount += guy.bonus_from_enchantments( du.amount, enchant_vals::mod::ARMOR_DARK );
            break;
        case DT_LIGHT:
            du.amount += guy.bonus_from_enchantments( du.amount, enchant_vals::mod::ARMOR_LIGHT );
            break;
        case DT_PSI:
            du.amount += guy.bonus_from_enchantments( du.amount, enchant_vals::mod::ARMOR_PSI );
            break;
        case DT_CUT:
            du.amount += guy.bonus_from_enchantments( du.amount, enchant_vals::mod::ARMOR_CUT );
            break;
        case DT_ELECTRIC:
            du.amount += guy.bonus_from_enchantments( du.amount, enchant_vals::mod::ARMOR_ELEC );
            break;
        case DT_HEAT:
            du.amount += guy.bonus_from_enchantments( du.amount, enchant_vals::mod::ARMOR_HEAT );
            break;
        case DT_STAB:
            du.amount += guy.bonus_from_enchantments( du.amount, enchant_vals::mod::ARMOR_STAB );
            break;
        case DT_BULLET:
            du.amount += guy.bonus_from_enchantments( du.amount, enchant_vals::mod::ARMOR_BULLET );
            break;
        default:
            return;
    }
    du.amount = std::max( 0.0f, du.amount );
}

void Character::absorb_hit( const bodypart_id& bp, damage_instance& dam )
{
    std::vector<detached_ptr<item>> worn_remains;
    bool armor_destroyed = false;

    for( damage_unit& elem : dam.damage_units ) {
        if( elem.amount < 0 ) {
            // Prevents 0 damage hits (like from hallucinations) from ripping armor
            elem.amount = 0;
            continue;
        }

        // The bio_ads CBM absorbs percentage melee damage and ranged damage (where possible) after
        // armour.
        if( has_active_bionic( bio_ads ) && ( elem.amount > 0 )
            && ( elem.type == DT_BASH || elem.type == DT_CUT || elem.type == DT_STAB
                 || elem.type == DT_BULLET ) ) {
            float elem_multi = 1;
            bionic& bio = get_bionic_state( bio_ads );
            // HACK: Halves charge rate when hit for the next 3 turns, doesn't stack. See
            // bionics.cpp for more information.
            bio.charge_timer = 6;
            // Bullet affected significantly more than stab, stab more than cut, cut more than bash.
            if( elem.type == DT_BASH ) {
                elem_multi = 0.8;
            } else if( elem.type == DT_CUT ) {
                elem_multi = 0.7;
            } else if( elem.type == DT_STAB ) {
                elem_multi = 0.55;
            } else if( elem.type == DT_BULLET ) {
                elem_multi = 0.25;
            }
            units::energy ads_cost = elem.amount * 500_J;
            if( bio.energy_stored >= ads_cost ) {
                dam.mult_damage( elem_multi );
                bio.energy_stored -= ads_cost;
            } else if( bio.energy_stored < ads_cost && bio.energy_stored != 0_kJ ) {
                // If you get hit and you lack energy it either deactivates, or deactivates and
                // shorts out. Either way you still get protection.
                dam.mult_damage( elem_multi );
                bio.energy_stored = 0_kJ;
                deactivate_bionic( bio );
                const units::energy shatter_thresh = ( elem.type == DT_BULLET ) ? 20_kJ : 15_kJ;
                if( ads_cost >= shatter_thresh ) {
                    if( bio.incapacitated_time == 0_turns ) {
                        add_msg_if_player(
                            m_bad,
                            _( "Your forcefield shatters and the feedback shorts out the %s!" ),
                            bio.info().name );
                    }
                    int over = units::to_kilojoule( ads_cost - ( shatter_thresh - 5_kJ ) );
                    bio.incapacitated_time += ( ( over / 5 ) ) * 1_turns;
                } else {
                    add_msg_if_player( m_bad, _( "Your forcefield crackles and the %s powers down." ),
                                       bio.info().name );
                }
            } else {
                // You tried to (re)activate it and immediately enter combat, no mitigation for you.
                deactivate_bionic( bio );
                add_msg_if_player(
                    m_bad, _( "The %s is interrupted and powers down." ), bio.info().name );
            }
        }

        armor_enchantment_adjust( *this, elem );

        // Only the outermost armor can be set on fire
        bool outermost = true;
        // The worn vector has the innermost item first, so
        // iterate reverse to damage the outermost (last in worn vector) first.
        for( auto iter = worn.rbegin(); iter != worn.rend(); ) {
            item& armor = **iter;

            if( !armor.covers( bp ) ) {
                ++iter;
                continue;
            }

            const std::string pre_damage_name = armor.tname();
            bool destroy = false;

            item_armor_enchantment_adjust( *this, elem, armor );
            // Heat damage can set armor on fire
            // Even though it doesn't cause direct physical damage to it
            if( outermost && elem.type == DT_HEAT && elem.amount >= 1.0f ) {
                // TODO: Different fire intensity values based on damage
                fire_data frd{2};
                destroy = armor.burn( frd );
                int fuel = roll_remainder( frd.fuel_produced );
                if( fuel > 0 ) {
                    add_effect( effect_onfire, time_duration::from_turns( fuel + 1 ), bp.id(), 0,
                                false, true );
                }
            }

            if( !destroy ) { destroy = armor_absorb( elem, armor, bp ); }

            if( destroy ) {
                if( g->u.sees( *this ) ) {
                    spawn_armor_feedback(
                        *this, remove_color_tags( pre_damage_name ), _( "destroyed" ), m_info, true );
                }
                destroyed_armor_msg(
                    *this, pre_damage_name, armor.contents.empty(), armor.weight(), armor.volume() );
                armor_destroyed = true;
                armor.on_takeoff( *this );

                for( detached_ptr<item> &it : armor.contents.clear_items() ) {
                    worn_remains.push_back( std::move( it ) );
                }
                // decltype is the type name of the iterator, note that reverse_iterator::base
                // returns the iterator to the next element, not the one the revers_iterator points
                // to.
                // http://stackoverflow.com/questions/1830158/how-to-call-erase-with-a-reverse-iterator
                location_vector<item>::iterator eit = iter.base();
                eit--;
                iter = decltype( iter )( worn.erase( std::move( eit ) ) ); // We std::move this in to
                // prevent it from counting
                // towards the active iterators
            } else {
                ++iter;
                outermost = false;
            }
        }

        passive_absorb_hit( bp, elem );

        if( elem.type == DT_BASH ) {
            if( has_trait( trait_LIGHT_BONES ) ) { elem.amount *= 1.4; }
            if( has_trait( trait_HOLLOW_BONES ) ) { elem.amount *= 1.8; }
        }

        elem.amount = std::max( elem.amount, 0.0f );
    }
    map& here = get_map();
    for( detached_ptr<item> &remain : worn_remains ) {
        here.add_item_or_charges( bub_pos(), std::move( remain ) );
    }
    if( armor_destroyed ) { drop_invalid_inventory(); }
}

bool Character::armor_absorb( damage_unit& du, item& armor, const bodypart_id& bp )
{
    if( rng( 1, 100 ) > armor.get_coverage( bp ) ) { return false; }
    // If the attack has already been negated by other armor, don't bother.
    if( du.amount <= 0 ) { return false; }
    armor.mitigate_damage( du );
    // We're indestructible, bail out here.
    if( armor.has_flag( flag_UNBREAKABLE ) ) { return false; }

    // We want armor's own resistance to this type, not the resistance it grants
    const int armors_own_resist = armor.damage_resist( du.type, true );
    if( armors_own_resist > 1000 ) {
        // This is some weird type that doesn't damage armors
        return false;
    }

    // Scale chance of article taking damage based on the number of parts it covers.
    // This represents large articles being able to take more punishment
    // before becoming ineffective or being destroyed.
    const int num_parts_covered = armor.get_covered_body_parts().count();
    if( !one_in( num_parts_covered ) ) { return false; }

    // Don't damage armor as much when bypassed by armor piercing
    // Most armor piercing damage comes from bypassing armor, not forcing through
    const int raw_dmg = du.amount * std::min( 1.0f, du.damage_multiplier );
    if( raw_dmg > armors_own_resist ) {
        // If damage is above armor value, the chance to avoid armor damage is
        // 50% + 50% * 1/dmg
        if( one_in( raw_dmg ) || one_in( 2 ) ) { return false; }
    } else {
        // Sturdy items and power armors never take chip damage.
        // Other armors have 0.5% of getting damaged from hits below their armor value.
        if( armor.has_flag( flag_STURDY ) || !one_in( 200 ) ) { return false; }
    }

    const material_type& material = armor.get_random_material();
    std::string damage_verb =
        ( du.type == DT_BASH ) ? material.bash_dmg_verb() : material.cut_dmg_verb();

    const std::string pre_damage_name = armor.tname();
    const std::string pre_damage_adj = armor.get_base_material().dmg_adj( armor.damage_level( 4 ) );

    // add "further" if the damage adjective and verb are the same
    std::string format_string =
        ( pre_damage_adj == damage_verb ) ? _( "Your %1$s is %2$s further!" ) : _( "Your %1$s is %2$s!" );
    add_msg_if_player( m_bad, format_string, pre_damage_name, damage_verb );
    // item is damaged
    if( is_player() ) {
        spawn_armor_feedback( *this, remove_color_tags( pre_damage_name ), damage_verb, m_info );
    }

    return armor.mod_damage(
               armor.has_flag( flag_FRAGILE )
               ? rng( 2 * itype::damage_scale, 3 * itype::damage_scale )
               : itype::damage_scale,
               du.type );
}

float Character::bionic_armor_bonus( const bodypart_id& bp, damage_type dt ) const
{
    float result = 0.0f;
    if( dt == DT_CUT || dt == DT_STAB ) {
        for( const bionic& i : get_bionic_collection() ) {
            const bionic_id& bid = i.id;
            const auto cut_prot = bid->cut_protec.find( bp.id() );
            if( cut_prot != bid->cut_protec.end() ) { result += cut_prot->second; }
        }
    } else if( dt == DT_BASH ) {
        for( const bionic& i : get_bionic_collection() ) {
            const bionic_id& bid = i.id;
            const auto bash_prot = bid->bash_protec.find( bp.id() );
            if( bash_prot != bid->bash_protec.end() ) { result += bash_prot->second; }
        }
    } else if( dt == DT_BULLET ) {
        for( const bionic& i : get_bionic_collection() ) {
            const bionic_id& bid = i.id;
            const auto bullet_prot = bid->bullet_protec.find( bp.id() );
            if( bullet_prot != bid->bullet_protec.end() ) { result += bullet_prot->second; }
        }
    }

    return result;
}

std::map<bodypart_id, int> Character::get_armor_fire(
    const std::map<bodypart_id, std::vector<const item *>> &clothing_map ) const
{
    return get_all_armor_type( DT_HEAT, clothing_map );
}

void Character::on_dodge( Creature* source, int difficulty )
{
    static const matec_id tec_none( "tec_none" );

    // Each avoided hit consumes an available dodge
    // When no more available we are likely to fail player::dodge_roll
    dodges_left--;

    // dodging throws of our aim unless we are either skilled at dodging or using a small weapon
    const item& weapon = primary_weapon();
    if( is_armed() && weapon.is_gun() ) {
        recoil +=
            std::max( weapon.volume() / 250_ml - get_skill_level( skill_dodge ), 0 ) * rng( 0, 100 );
        recoil = std::min( MAX_RECOIL, recoil );
    }

    // Even if we are not to train still call practice to prevent skill rust
    difficulty = std::max( difficulty, 0 );
    as_player()->practice( skill_dodge, difficulty * 2, difficulty );

    martial_arts_data->ma_ondodge_effects( *this );

    // For adjacent attackers check for techniques usable upon successful dodge
    if( source && square_dist( bub_pos(), source->bub_pos() ) == 1 ) {
        matec_id tec = pick_technique( *source, primary_weapon(), false, true, false );

        if( tec != tec_none && !is_dead_state() ) {
            if( get_stamina() < get_stamina_max() / 3 ) {
                add_msg( m_bad, _( "You try to counterattack but you are too exhausted!" ) );
            } else {
                melee_attack( *source, false, &tec );
            }
        }
    }
    cata::run_hooks( "on_creature_dodged", [ &, this]( auto & params ) {
        params["char"] = this;
        params["source"] = source;
        params["difficulty"] = difficulty;
    } );
}

void Character::did_hit( Creature& target ) { enchantment_cache->cast_hit_you( *this, target ); }

void Character::on_hit(
    Creature* source, bodypart_id bp_hit, dealt_projectile_attack const* const proj )
{
    check_dead_state();
    if( source == nullptr || proj != nullptr ) { return; }

    if( !source->is_hallucination() ) {
        // Gain reduced experience for failed attempts to dodge
        const int difficulty = source->get_melee();
        as_player()->practice( skill_dodge, std::max( difficulty, 0 ), difficulty, true );
    }

    bool u_see = g->u.sees( *this );
    units::energy trigger_cost_base = bio_ods->power_trigger;
    if( has_active_bionic( bio_ods ) && get_power_level() >= trigger_cost_base * 4 ) {
        if( is_player() ) {
            add_msg( m_good, _( "Your offensive defense system shocks %s in mid-attack!" ),
                     source->disp_name() );
        } else if( u_see ) {
            add_msg( _( "%1$s's offensive defense system shocks %2$s in mid-attack!" ), disp_name(),
                     source->disp_name() );
        }
        int shock = rng( 1, 4 );
        mod_power_level( -shock * trigger_cost_base );
        damage_instance ods_shock_damage;
        ods_shock_damage.add_damage( DT_ELECTRIC, shock * 5 );
        // Should hit body part used for attack
        source->deal_damage( this, bodypart_id( "torso" ), ods_shock_damage );
    }
    if( !wearing_something_on( bp_hit ) && ( has_trait( trait_SPINES ) ||
            has_trait( trait_QUILLS ) ) ) {
        int spine = rng( 1, has_trait( trait_QUILLS ) ? 20 : 8 );
        if( !is_player() ) {
            if( u_see ) {
                add_msg( _( "%1$s's %2$s puncture %3$s in mid-attack!" ), name,
                         ( has_trait( trait_QUILLS ) ? _( "quills" ) : _( "spines" ) ), source->disp_name() );
            }
        } else {
            add_msg( m_good, _( "Your %1$s puncture %2$s in mid-attack!" ),
                     ( has_trait( trait_QUILLS ) ? _( "quills" ) : _( "spines" ) ), source->disp_name() );
        }
        damage_instance spine_damage;
        spine_damage.add_damage( DT_STAB, spine );
        source->deal_damage( this, bodypart_id( "torso" ), spine_damage );
    }
    if( ( !( wearing_something_on( bp_hit ) ) ) && ( has_trait( trait_THORNS ) )
        && ( !( source->has_weapon() ) ) ) {
        if( !is_player() ) {
            if( u_see ) {
                add_msg( _( "%1$s's %2$s scrape %3$s in mid-attack!" ), name, _( "thorns" ),
                         source->disp_name() );
            }
        } else {
            add_msg( m_good, _( "Your thorns scrape %s in mid-attack!" ), source->disp_name() );
        }
        int thorn = rng( 1, 4 );
        damage_instance thorn_damage;
        thorn_damage.add_damage( DT_CUT, thorn );
        // In general, critters don't have separate limbs
        // so safer to target the torso
        source->deal_damage( this, bodypart_id( "torso" ), thorn_damage );
    }
    if( ( !( wearing_something_on( bp_hit ) ) ) && ( has_trait( trait_CF_HAIR ) ) ) {
        if( !is_player() ) {
            if( u_see ) {
                add_msg( _( "%1$s gets a load of %2$s's %3$s stuck in!" ), source->disp_name(), name,
                         ( _( "hair" ) ) );
            }
        } else {
            add_msg( m_good, _( "Your hairs detach into %s!" ), source->disp_name() );
        }
        source->add_effect( effect_stunned, 2_turns );
        if( one_in( 3 ) ) { // In the eyes!
            source->add_effect( effect_blind, 2_turns );
        }
    }

    map& here = get_map();
    const optional_vpart_position veh_part = here.veh_at( bub_pos() );
    bool in_skater_vehicle =
        in_vehicle && veh_part.part_with_feature( "SEAT_REQUIRES_BALANCE", false );

    if( ( worn_with_flag( flag_REQUIRES_BALANCE ) || in_skater_vehicle ) && !is_on_ground() ) {
        int rolls = 4;
        if( worn_with_flag( flag_ROLLER_ONE ) && !in_skater_vehicle ) { rolls += 2; }
        if( has_trait( trait_PROF_SKATER ) ) { rolls--; }
        if( has_trait( trait_DEFT ) ) { rolls--; }

        if( stability_roll() < dice( rolls, 10 ) ) {
            if( !is_player() ) {
                if( u_see ) { add_msg( _( "%1$s loses their balance while being hit!" ), name ); }
            } else {
                add_msg( m_bad, _( "You lose your balance while being hit!" ) );
            }
            if( in_skater_vehicle ) {
                g->fling_creature( this, rng_float( 0_degrees, 360_degrees ), 10 );
            }
            // This kind of downing is not subject to immunity.
            add_effect( effect_downed, 2_turns, bodypart_str_id::NULL_ID(), 0, true );
        }
    }
    enchantment_cache->cast_hit_me( *this, source );
}

/*
    Where damage to character is actually applied to hit body parts
    Might be where to put bleed stuff rather than in player::deal_damage()
 */
void Character::apply_damage(
    Creature* source, item* source_weapon, item* source_projectile, bodypart_id hurt, int dam,
    const bool bypass_med )
{
    if( is_dead_state() || has_trait( trait_DEBUG_NODMG ) ) {
        // don't do any more damage if we're already dead
        // Or if we're debugging and don't want to die
        return;
    }

    if( hurt.id().is_null() ) {
        debugmsg( "Wacky body part hurt!" );
        hurt = bodypart_id( "torso" );
    }

    mod_pain( dam / 2 );

    const bodypart_id& part_to_damage = hurt->main_part;

    const int dam_to_bodypart = std::min( dam, get_part_hp_cur( part_to_damage ) );

    mod_part_hp_cur( part_to_damage, -dam_to_bodypart );
    get_event_bus().send<event_type::character_takes_damage>( getID(), dam_to_bodypart );

    const item& weapon = primary_weapon();
    if( !weapon.is_null() && !as_player()->can_wield( weapon ).success()
        && can_unwield( weapon ).success() ) {
        add_msg_if_player(
            _( "You are no longer able to wield your %s and drop it!" ), weapon.display_name() );
        put_into_vehicle_or_drop( *this, item_drop_reason::tumbling, remove_primary_weapon() );
    }

    if( dam > get_painkiller() ) { on_hurt( source ); }

    if( is_dead_state() ) {
        // if the player killed himself, add it to the kill count list
        if( !is_npc() && !get_killer() && source == g->u.as_character() ) {
            g->events().send<event_type::character_kills_character>(
                get_player_character().getID(), getID(), get_name() );
        }
        set_killer( source );
        if( source_weapon ) { source_weapon->add_npc_kill( get_name() ); }
        if( source_projectile ) { source_projectile->add_npc_kill( get_name() ); }
    }

    if( !bypass_med ) {
        // remove healing effects if damaged
        int remove_med = roll_remainder( dam / 5.0f );
        if( remove_med > 0 && has_effect( effect_bandaged, part_to_damage.id() ) ) {
            remove_med -= reduce_healing_effect( effect_bandaged, remove_med, part_to_damage );
        }
        if( remove_med > 0 && has_effect( effect_disinfected, part_to_damage.id() ) ) {
            reduce_healing_effect( effect_disinfected, remove_med, part_to_damage );
        }
    }
}
void Character::apply_damage(
    Creature* source, item* source_weapon, bodypart_id hurt, int dam, const bool bypass_med )
{
    apply_damage( source, source_weapon, nullptr, hurt, dam, bypass_med );
}
void Character::apply_damage( Creature* source, bodypart_id hurt, int dam, const bool bypass_med )
{
    apply_damage( source, nullptr, nullptr, hurt, dam, bypass_med );
}

dealt_damage_instance Character::deal_damage(
    Creature* source, bodypart_id bp, const damage_instance& d, item* source_weapon,
    item* source_projectile, bool is_crit, bool is_graze )
{
    if( has_trait( trait_DEBUG_NODMG ) ) { return dealt_damage_instance(); }

    if( bp.id().is_null() ) {
        debugmsg( "Wacky bodypart hit!" );
        return dealt_damage_instance();
    }

    // damage applied here
    dealt_damage_instance dealt_dams =
        Creature::deal_damage( source, bp, d, source_weapon, source_projectile, is_crit, is_graze );
    // block reduction should be by applied this point
    int dam = dealt_dams.total_damage();

    // TODO: Pre or post blit hit tile onto "this"'s location here
    if( dam > 0 && g->u.sees( bub_pos() ) ) { g->draw_hit_player( *this, dam ); }

    // handle snake artifacts
    if( has_artifact_with( AEP_SNAKES ) && dam >= 6 ) {
        const int snakes = dam / 6;
        int spawned = 0;
        for( int i = 0; i < snakes; i++ ) {
            if( monster * const snake = g->place_critter_around( mon_shadow_snake, bub_pos(), 1 ) ) {
                snake->friendly = -1;
                spawned++;
            }
        }
        if( spawned == 1 ) {
            add_msg( m_warning, _( "A snake sprouts from your body!" ) );
        } else if( spawned >= 2 ) {
            add_msg( m_warning, _( "Some snakes sprout from your body!" ) );
        }
    }

    // And slimespawners too
    if( ( has_trait( trait_SLIMESPAWNER ) ) && ( dam >= 10 ) && one_in( 20 - dam ) ) {
        if( monster * const slime = g->place_critter_around( mon_player_blob, bub_pos(), 1 ) ) {
            slime->friendly = -1;
            add_msg_if_player( m_warning, _( "Slime is torn from you, and moves on its own!" ) );
        }
    }

    // Acid blood effects.
    bool u_see = g->u.sees( *this );
    int cut_dam = dealt_dams.type_damage( DT_CUT );
    if( source && has_trait( trait_ACIDBLOOD ) && !one_in( 3 ) && ( dam >= 4 || cut_dam > 0 )
        && ( rl_dist( g->u.bub_pos(), source->bub_pos() ) <= 1 ) ) {
        if( is_player() ) {
            add_msg( m_good, _( "Your acidic blood splashes %s in mid-attack!" ), source->disp_name() );
        } else if( u_see ) {
            add_msg( _( "%1$s's acidic blood splashes on %2$s in mid-attack!" ), disp_name(),
                     source->disp_name() );
        }
        damage_instance acidblood_damage;
        acidblood_damage.add_damage( DT_ACID, rng( 4, 16 ) );
        if( !one_in( 4 ) ) {
            source->deal_damage( this, bodypart_id( "arm_l" ), acidblood_damage );
            source->deal_damage( this, bodypart_id( "arm_r" ), acidblood_damage );
        } else {
            source->deal_damage( this, bodypart_id( "torso" ), acidblood_damage );
            source->deal_damage( this, bodypart_id( "head" ), acidblood_damage );
        }
    }

    int recoil_mul = 100;

    if( bp == bodypart_id( "eyes" ) ) {
        if( dam > 5 || cut_dam > 0 ) {
            const time_duration minblind = std::max( 1_turns, 1_turns * ( dam + cut_dam ) / 10 );
            const time_duration maxblind = std::min( 5_turns, 1_turns * ( dam + cut_dam ) / 4 );
            add_effect( effect_blind, rng( minblind, maxblind ) );
        }
    } else if( bp == bodypart_id( "hand_l" ) || bp == bodypart_id( "arm_l" )
               || bp == bodypart_id( "hand_r" ) || bp == bodypart_id( "arm_r" ) ) {
        recoil_mul = 200;
    } else if( bp == bodypart_id( "num_bp" ) ) {
        debugmsg( "Wacky body part hit!" );
    }


    // TODO: Scale with damage in a way that makes sense for power armors, plate armor and naked
    // skin.
    recoil += recoil_mul * primary_weapon().volume() / 250_ml;
    recoil = std::min( MAX_RECOIL, recoil );
    // looks like this should be based off of dealt damages, not d as d has no damage reduction
    // applied.
    //  Skip all this if the damage isn't from a creature. e.g. an explosion.
    if( source != nullptr ) {
        if( source->has_flag( MF_GRABS ) && !source->is_hallucination()
            && !source->has_effect( effect_grabbing ) ) {
            /** @EFFECT_DEX increases chance to avoid being grabbed */

            if( has_grab_break_tec() && ( rng( 0, get_dex() ) > rng( 0, 10 ) ) ) {
                if( has_effect( effect_grabbed ) ) {
                    add_msg_if_player(
                        m_warning, _( "%s tries to grab you as well, but you bat it away!" ),
                        source->disp_name( false, true ) );
                } else {
                    add_msg_player_or_npc(
                        m_info, _( "%s tries to grab you, but you break its grab!" ),
                        _( "%s tries to grab <npcname>, but they break its grab!" ),
                        source->disp_name( false, true ) );
                }
            } else {
                int prev_effect = get_effect_int( effect_grabbed );
                add_effect( effect_grabbed, 2_turns, body_part_torso, prev_effect + 2 );
                source->add_effect( effect_grabbing, 2_turns );
                add_msg_player_or_npc(
                    m_bad, _( "You are grabbed by %s!" ), _( "<npcname> is grabbed by %s!" ),
                    source->disp_name() );
            }
        }
    }


    on_hurt( source );
    return dealt_dams;
}
dealt_damage_instance Character::deal_damage(
    Creature* source, bodypart_id bp, const damage_instance& d, item* source_weapon, bool is_crit,
    bool is_graze )
{
    return deal_damage( source, bp, d, source_weapon, nullptr, is_crit, is_graze );
}
dealt_damage_instance Character::deal_damage(
    Creature* source, bodypart_id bp, const damage_instance& d, bool is_crit, bool is_graze )
{
    return deal_damage( source, bp, d, nullptr, nullptr, is_crit, is_graze );
}

int Character::reduce_healing_effect(
    const efftype_id& eff_id, int remove_med, const bodypart_id& hurt )
{
    const body_part hurt_token = hurt->token;
    effect& e = get_effect( eff_id, hurt.id() );
    int intensity = e.get_intensity();
    if( remove_med < intensity ) {
        if( eff_id == effect_bandaged ) {
            add_msg_if_player(
                m_bad, _( "Bandages on your %s were damaged!" ), body_part_name( hurt_token ) );
        } else if( eff_id == effect_disinfected ) {
            add_msg_if_player(
                m_bad, _( "You got some filth on your disinfected %s!" ), body_part_name( hurt_token ) );
        }
    } else {
        if( eff_id == effect_bandaged ) {
            add_msg_if_player(
                m_bad, _( "Bandages on your %s were destroyed!" ), body_part_name( hurt_token ) );
        } else if( eff_id == effect_disinfected ) {
            add_msg_if_player(
                m_bad, _( "Your %s is no longer disinfected!" ), body_part_name( hurt_token ) );
        }
    }
    e.mod_duration( -6_hours * remove_med );
    return intensity;
}

void Character::heal( const bodypart_id& healed, int dam )
{
    const int max_hp = get_part_hp_max( healed );
    const int cur_hp = get_part_hp_cur( healed );
    const int effective_heal = std::min( dam, max_hp - cur_hp );
    mod_part_hp_cur( healed, effective_heal );
    g->events().send<event_type::character_heals_damage>( getID(), effective_heal );
    if( cur_hp + dam >= max_hp ) { remove_effect( effect_disabled, healed.id() ); }
    // update morale in case healing reduced perceived pain
    morale->on_stat_change( "perceived_pain", get_perceived_pain() );
}

void Character::healall( int dam )
{
    for( const bodypart_id& bp : get_all_body_parts() ) {
        heal( bp, dam );
        mod_part_healed_total( bp, dam );
    }
}

void Character::hurtall( int dam, Creature* source, bool disturb /*= true*/ )
{
    if( is_dead_state() || has_trait( trait_DEBUG_NODMG ) || dam <= 0 ) { return; }

    for( const bodypart_id& bp : get_all_body_parts( true ) ) {
        // Don't use apply_damage here or it will annoy the player with 6 queries
        const int dam_to_bodypart = std::min( dam, get_part_hp_cur( bp ) );
        mod_part_hp_cur( bp, -dam_to_bodypart );
        g->events().send<event_type::character_takes_damage>( getID(), dam_to_bodypart );
    }

    // Low pain: damage is spread all over the body, so not as painful as 6 hits in one part
    mod_pain( dam );
    on_hurt( source, disturb );
}

int Character::hitall( int dam, int vary, Creature* source )
{
    int damage_taken = 0;
    for( const bodypart_id& bp : get_all_body_parts( true ) ) {
        int ddam = vary ? dam * rng( 100 - vary, 100 ) / 100 : dam;
        int cut = 0;
        auto damage = damage_instance::physical( ddam, cut, 0 );
        damage_taken += deal_damage( source, bp, damage ).total_damage();
    }
    return damage_taken;
}

void Character::on_hurt( Creature* source, bool disturb /*= true*/ )
{
    if( has_trait( trait_ADRENALINE ) && !has_effect( effect_adrenaline )
        && ( get_part_hp_cur( bodypart_id( "head" ) ) < 25
             || get_part_hp_cur( bodypart_id( "torso" ) ) < 15 ) ) {
        add_effect( effect_adrenaline, 3_minutes );
    }

    if( disturb ) {
        if( has_effect( effect_sleep ) && !has_effect( effect_narcosis ) ) { wake_up(); }
        if( !is_npc() && !has_effect( effect_narcosis ) ) {
            if( source != nullptr ) {
                g->cancel_activity_or_ignore_query(
                    distraction_type::attacked,
                    string_format( _( "You were attacked by %s!" ), source->disp_name() ) );
            } else {
                g->cancel_activity_or_ignore_query( distraction_type::attacked, _( "You were hurt!" ) );
            }
        }
    }
}
