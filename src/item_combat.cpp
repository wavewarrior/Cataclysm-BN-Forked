// Item combat methods: DPS calculation, melee damage, weapon stats,
// and physical resistance — split out of item.cpp. .cpp-only, no API changes.

#include "active_tile_data_def.h"
#include "ammo.h"
#include "ascii_art.h"
#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "cached_item_options.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "catalua_icallback_actor.h"
#include "character.h"
#include "character_encumbrance.h"
#include "character_functions.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "character_stat.h"
#include "cloning_utils.h"
#include "clothing_mod.h"
#include "clzones.h"
#include "color.h"
#include "craft_command.h"
#include "damage.h"
#include "debug.h"
#include "dispersion.h"
#include "drop_token.h"
#include "effect.h" // for weed_msg
#include "enums.h"
#include "explosion.h"
#include "faction.h"
#include "fault.h"
#include "field_type.h"
#include "fire.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "gun_mode.h"
#include "iexamine.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "item_category.h"
#include "item_factory.h"
#include "item_group.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "line.h"
#include "locations.h"
#include "magic.h"
#include "magic_enchantment.h"
#include "map.h"
#include "martialarts.h"
#include "material.h"
#include "melee.h"
#include "messages.h"
#include "mod_manager.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmapbuffer.h"
#include "pimpl.h"
#include "player.h"
#include "player_activity.h"
#include "pldata.h"
#include "point.h"
#include "profile.h"
#include "projectile.h"
#include "ranged.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "relic.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "rot.h"
#include "scores_ui.h"
#include "skill.h"
#include "stomach.h"
#include "string_formatter.h"
#include "string_id_utils.h"
#include "string_utils.h"
#include "text_snippets.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "units_energy.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vitamin.h"
#include "vpart_position.h"
#include "weather.h"
#include "weather_gen.h"
#include "wheel_dimensions.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>

// File-scope id constants (moved with combat methods; internal linkage).
static const matec_id rapid_strike( "RAPID" );

/*
 * 0 based lookup table of accuracy - monster defense converted into number of hits per 10000
 * attacks
 * data painstakingly looked up at http://onlinestatbook.com/2/calculators/normal_dist.html
 */
static const double hits_by_accuracy[41] = {
    0,    1,    2,    3,    7,    // -20 to -16
    13,   26,   47,   82,   139,  // -15 to -11
    228,  359,  548,  808,  1151, // -10 to -6
    1587, 2119, 2743, 3446, 4207, // -5 to -1
    5000,                         // 0
    5793, 6554, 7257, 7881, 8413, // 1 to 5
    8849, 9192, 9452, 9641, 9772, // 6 to 10
    9861, 9918, 9953, 9974, 9987, // 11 to 15
    9993, 9997, 9998, 9999, 10000 // 16 to 20
};

double item::effective_dps( const player& guy, const monster& mon ) const
{
    return effective_dps( guy, mon, melee::default_attack( *this ) );
}

double item::effective_dps(
    const player& guy, const monster& mon, const attack_statblock& attack ) const
{
    const float mon_dodge = mon.get_dodge();
    // TODO: Handle multiple attacks
    float base_hit = guy.get_dex() / 4.0f + guy.get_hit_weapon( *this, attack );
    base_hit *= std::max( 0.25f, 1.0f - guy.encumb( body_part_torso ) / 100.0f );
    float mon_defense = mon_dodge + mon.size_melee_penalty() / 5.0;
    constexpr double hit_trials = 10000.0;
    const int rng_mean = std::max( std::min( static_cast<int>( base_hit - mon_defense ), 20 ),
                                   -20 ) + 20;
    double num_all_hits = hits_by_accuracy[rng_mean];
    /* critical hits have two chances to occur: triple critical hits happen much less frequently,
     * and double critical hits can only occur if a hit roll is more than 1.5 * monster dodge.
     * Not the hit roll used to determine the attack, another one.
     * the way the math works, some percentage of the total hits are eligible to be double
     * critical hits, and the rest are eligible to be triple critical hits, but in each case,
     * only some small percent of them actually become critical hits.
     */
    const int rng_high_mean =
        std::max( std::min( static_cast<int>( base_hit - 1.5 * mon_dodge ), 20 ), -20 ) + 20;
    double num_high_hits = hits_by_accuracy[rng_high_mean] * num_all_hits / hit_trials;
    double double_crit_chance = guy.crit_chance( 4, 0, *this, attack );
    double crit_chance = guy.crit_chance( 0, 0, *this, attack );
    double num_low_hits = std::max( 0.0, num_all_hits - num_high_hits );

    double moves_per_attack = guy.attack_cost( *this );
    // attacks that miss do no damage but take time
    double total_moves = ( hit_trials - num_all_hits ) * moves_per_attack;
    double total_damage = 0.0;
    double num_crits =
        std::min( num_low_hits * crit_chance + num_high_hits * double_crit_chance, num_all_hits );
    // critical hits are counted separately
    double num_hits = num_all_hits - num_crits;
    // sum average damage past armor and return the number of moves required to achieve
    // that damage
    // @todo Update for attack_statblock
    const attack_statblock default_attack = melee::default_attack( *this );
    const auto calc_effective_damage =
        [ &, moves_per_attack](
    const double num_strikes, const bool crit, const player & guy, const monster & mon ) {
        monster temp_mon( mon );
        double subtotal_damage = 0;
        damage_instance base_damage;
        melee::roll_all_damage( guy, crit, base_damage, true, *this, default_attack );
        damage_instance dealt_damage = base_damage;
        temp_mon.absorb_hit( bodypart_id( "torso" ), dealt_damage );
        dealt_damage_instance dealt_dams;
        for( const damage_unit& dmg_unit : dealt_damage.damage_units ) {
            int cur_damage = 0;
            int total_pain = 0;
            temp_mon.deal_damage_handle_type(
                dmg_unit, bodypart_id( "torso" ), cur_damage, total_pain );
            if( cur_damage > 0 ) { dealt_dams.dealt_dams[dmg_unit.type] += cur_damage; }
        }
        double damage_per_hit = dealt_dams.total_damage();
        subtotal_damage = damage_per_hit * num_strikes;
        double subtotal_moves = moves_per_attack * num_strikes;

        if( has_technique( rapid_strike ) ) {
            monster temp_rs_mon( mon );
            damage_instance rs_base_damage;
            melee::roll_all_damage( guy, crit, rs_base_damage, true, *this, default_attack );
            damage_instance dealt_rs_damage = rs_base_damage;
            for( damage_unit& dmg_unit : dealt_rs_damage.damage_units ) {
                dmg_unit.damage_multiplier *= 0.66;
            }
            temp_rs_mon.absorb_hit( bodypart_id( "torso" ), dealt_rs_damage );
            dealt_damage_instance rs_dealt_dams;
            for( const damage_unit& dmg_unit : dealt_rs_damage.damage_units ) {
                int cur_damage = 0;
                int total_pain = 0;
                temp_rs_mon.deal_damage_handle_type(
                    dmg_unit, bodypart_id( "torso" ), cur_damage, total_pain );
                if( cur_damage > 0 ) { rs_dealt_dams.dealt_dams[dmg_unit.type] += cur_damage; }
            }
            double rs_damage_per_hit = rs_dealt_dams.total_damage();
            subtotal_moves *= 0.5;
            subtotal_damage *= 0.5;
            subtotal_moves += moves_per_attack * num_strikes * 0.33;
            subtotal_damage += rs_damage_per_hit * num_strikes * 0.5;
        }
        return std::make_pair( subtotal_moves, subtotal_damage );
    };
    std::pair<double, double> crit_summary = calc_effective_damage( num_crits, true, guy, mon );
    total_moves += crit_summary.first;
    total_damage += crit_summary.second;
    std::pair<double, double> summary = calc_effective_damage( num_hits, false, guy, mon );
    total_moves += summary.first;
    total_damage += summary.second;
    return total_damage * to_moves<double>( 1_seconds ) / total_moves;
}

struct dps_comp_data {
    mtype_id mon_id;
    bool display;
    bool evaluate;
};

static const std::vector<std::pair<translation, dps_comp_data>> dps_comp_monsters = {
    {to_translation( "Best" ), {mtype_id( "debug_mon" ), true, false}},
    {to_translation( "Vs. Agile" ), {mtype_id( "debug_mon_agile" ), true, true}},
    {to_translation( "Vs. Armored" ), {mtype_id( "debug_mon_armored" ), true, true}},
    {to_translation( "Vs. Mixed" ), {mtype_id( "debug_mon_mixed" ), false, true}},
};

std::map<std::string, double> item::dps(
    const bool for_display, const bool for_calc, const player& guy,
    const attack_statblock& attack ) const
{
    std::map<std::string, double> results;
    for( const std::pair<translation, dps_comp_data> &comp_mon : dps_comp_monsters ) {
        if( ( comp_mon.second.display != for_display ) && ( comp_mon.second.evaluate != for_calc ) ) {
            continue;
        }
        monster test_mon = monster( comp_mon.second.mon_id );
        results[comp_mon.first.translated()] = effective_dps( guy, test_mon, attack );
    }
    return results;
}

std::map<std::string, double> item::dps(
    const bool for_display, const bool for_calc, const attack_statblock& attack ) const
{
    return dps( for_display, for_calc, get_avatar(), attack );
}

double item::average_dps( const player& guy, const attack_statblock& attack ) const
{
    const auto& dps_data = dps( false, true, guy, attack );
    const double sum = std::transform_reduce(
                           dps_data.begin(), dps_data.end(), 0.0, std::plus{},
                           []( const auto & entry ) -> double { return entry.second; } );

    return sum / dps_data.size();
}
int item::attack_cost() const
{
    int base = 65 + ( volume() / 62.5_ml + weight() / 60_gram ) / count();
    int bonus = bonus_from_enchantments_wielded( base, enchant_vals::mod::ITEM_ATTACK_COST, true );
    return std::max( 0, base + bonus );
}

int item::stamina_cost() const { return get_avatar().get_melee_stamina_cost( *this ); }

int item::damage_melee( damage_type dt ) const
{
    return damage_melee( melee::default_attack( *this ), dt );
}

int item::damage_melee( const attack_statblock& attack, damage_type dt ) const
{
    assert( dt >= DT_NULL && dt < NUM_DT );
    if( is_null() ) { return 0; }

    // effectiveness is reduced by 10% per damage level
    int res = attack.damage.type_damage( dt );
    res -= res * std::max( damage_level( 4 ), 0 ) * 0.1;

    // apply type specific flags
    switch( dt ) {
        case DT_BASH:
            if( has_flag( flag_REDUCED_BASHING ) ) { res *= 0.5; }
            break;

        case DT_CUT:
        case DT_STAB:
            if( has_flag( flag_DIAMOND ) ) { res *= 1.3; }
            break;

        default:
            break;
    }

    // @todo: This probably breaks attack_statblock logic completely...
    // consider any melee gunmods
    if( is_gun() ) {
        const std::vector<const item *> &mods = gunmods();
        return std::accumulate( mods.begin(), mods.end(), res, [dt]( int last_max, const item * it ) {
            return it->has_flag( flag_MELEE_GUNMOD )
                   ? std::max( last_max, it->damage_melee( dt ) )
                   : last_max;
        } );
    }

    switch( dt ) {
        case DT_BASH:
            res += bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_BASH, true );
            break;
        case DT_CUT:
            res += bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_CUT, true );
            break;
        case DT_STAB:
            res += bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_STAB, true );
            break;
        case DT_BULLET:
            res +=
                bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_BULLET, true );
            break;
        case DT_ACID:
            res += bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_ACID, true );
            break;
        case DT_BIOLOGICAL:
            res += bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_BIO, true );
            break;
        case DT_COLD:
            res += bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_COLD, true );
            break;
        case DT_DARK:
            res += bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_DARK, true );
            break;
        case DT_ELECTRIC:
            res +=
                bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_ELECTRIC, true );
            break;
        case DT_HEAT:
            res += bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_FIRE, true );
            break;
        case DT_LIGHT:
            res += bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_LIGHT, true );
            break;
        case DT_PSI:
            res += bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_PSI, true );
            break;
        case DT_TRUE:
            res += bonus_from_enchantments_wielded( res, enchant_vals::mod::ITEM_DAMAGE_TRUE, true );
            break;
        default:
            break;
    }
    // Apply melee damage bonus
    const auto& bonus = get_melee_damage_bonus();
    res += bonus.type_damage( dt );

    return std::max( res, 0 );
}

std::map<std::string, attack_statblock> item::get_attacks() const
{
    if( is_null() ) { return {{"DEFAULT", attack_statblock{}}}; }

std::map<std::string, attack_statblock> result;

// TODO: Cache
for( const auto& attack : type->attacks ) {
    attack_statblock modified_attack = attack.second;
    const auto& bonus = get_melee_damage_bonus();
        for( damage_unit& du : modified_attack.damage.damage_units ) {
            // effectiveness is reduced by 10% per damage level
            du.amount -= du.amount * std::max( damage_level( 4 ), 0 ) * 0.1;
            // apply type specific flags
            switch( du.type ) {
                case DT_BASH:
                    if( has_flag( flag_REDUCED_BASHING ) ) { du.amount *= 0.5; }
                    break;

                case DT_CUT:
                case DT_STAB:
                    if( has_flag( flag_DIAMOND ) ) { du.amount *= 1.3; }
                    break;

                default:
                    break;
            }

            switch( du.type ) {
                case DT_BASH:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_BASH, true );
                    break;
                case DT_CUT:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_CUT, true );
                    break;
                case DT_STAB:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_STAB, true );
                    break;
                case DT_BULLET:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_BULLET, true );
                    break;
                case DT_ACID:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_ACID, true );
                    break;
                case DT_BIOLOGICAL:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_BIO, true );
                    break;
                case DT_COLD:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_COLD, true );
                    break;
                case DT_DARK:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_DARK, true );
                    break;
                case DT_ELECTRIC:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_ELECTRIC, true );
                    break;
                case DT_HEAT:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_FIRE, true );
                    break;
                case DT_LIGHT:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_LIGHT, true );
                    break;
                case DT_PSI:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_PSI, true );
                    break;
                case DT_TRUE:
                    du.amount += bonus_from_enchantments_wielded(
                                     du.amount, enchant_vals::mod::ITEM_DAMAGE_TRUE, true );
                    break;
                default:
                    break;
            }
            // Apply melee damage bonus
            du.amount += bonus.type_damage( du.type );
        }
        result[attack.first] = modified_attack;
    }

    // consider any melee gunmods
    if( is_gun() ) {
    if( get_option<bool>( "LIMITED_BAYONETS" ) ) {
            // TODO: Multiple bayonets with multiple attacks each - add all attacks, resolve id
            // conflicts
            const std::vector<const item *> &mods = gunmods();
            float best_damage = 0.0f;
            const attack_statblock* best = nullptr;
            for( const item * gunmod_ptr : mods ) {
                const item& gunmod = *gunmod_ptr;
                if( gunmod.has_flag( flag_MELEE_GUNMOD ) ) {
                    // TODO: Handle multiple attacks here - add all of them as separate attacks
                    assert( !gunmod.type->attacks.empty() );
                    const attack_statblock& first_attack = gunmod.type->attacks.begin()->second;
                    float damage_sum = std::accumulate(
                                           first_attack.damage.begin(), first_attack.damage.end(), 0.0f,
                    []( float amount_sum, const damage_unit & du ) {
                        // Ignore multipliers for now because it's a temporary hack
                        return amount_sum + du.amount;
                    } );
                    if( damage_sum > best_damage ) {
                        best = &first_attack;
                        best_damage = damage_sum;
                    }
                }
            }
            if( best != nullptr ) {
                attack_statblock gunmod_attack = *best;
                gunmod_attack.to_hit = type->m_to_hit;
                result["BAYONET"] = gunmod_attack;
            }
        } else {
            // Old logic here - max dmg for each type
            const std::vector<const item *> &mods = gunmods();
            for( const item * it : mods ) {
                const attack_statblock& attack = melee::default_attack( *it );
                for( auto& dmg : attack.damage ) { result["DEFAULT"].damage.add( dmg ); }
            }
        }
    }

    return result;
}

auto item::get_melee_damage_bonus() const -> const damage_instance& { return melee_damage_bonus; }

auto item::set_melee_damage_bonus( const damage_instance& bonus ) -> void
{
    melee_damage_bonus = bonus;
}

auto item::get_melee_hit_bonus() const -> int { return melee_hit_bonus; }

auto item::set_melee_hit_bonus( int bonus ) -> void { melee_hit_bonus = bonus; }

auto item::get_ranged_damage_bonus() const -> const damage_instance& { return ranged_damage_bonus; }

auto item::set_ranged_damage_bonus( const damage_instance& damages ) -> void
{
    ranged_damage_bonus = damages;
}

auto item::get_range_bonus() const -> int { return range_bonus; }

auto item::set_range_bonus( int bonus ) -> void { range_bonus = bonus; }

auto item::get_dispersion_bonus() const -> int { return dispersion_bonus; }

auto item::set_dispersion_bonus( int bonus ) -> void { dispersion_bonus = bonus; }

auto item::get_recoil_bonus() const -> int { return recoil_bonus; }

auto item::set_recoil_bonus( int bonus ) -> void { recoil_bonus = bonus; }

damage_instance item::base_damage_melee() const
{
    // TODO: Caching
    damage_instance ret;
    for( size_t i = DT_NULL + 1; i < NUM_DT; i++ ) {
        damage_type dt = static_cast<damage_type>( i );
        int dam = damage_melee( dt );
        if( dam > 0 ) { ret.add_damage( dt, dam ); }
    }

    return ret;
}

damage_instance item::base_damage_thrown() const
{
    // TODO: Create a separate cache for individual items (for modifiers like diamond etc.)
    return type->thrown_damage;
}

int item::reach_range( const Character& guy ) const
{
    int res = 1;

    if( has_flag( flag_REACH_ATTACK ) ) { res = has_flag( flag_REACH3 ) ? 3 : 2; }

    // for guns consider any attached gunmods
    if( is_gun() && !is_gunmod() ) {
        for( const std::pair<const gun_mode_id, gun_mode> &m : gun_all_modes() ) {
            if( guy.is_npc() && m.second.flags.contains( "NPC_AVOID" ) ) { continue; }
            if( m.second.melee() ) { res = std::max( res, m.second.qty ); }
        }
    }

    return std::max( 1, res );
}

bool item::can_shatter() const
{
    static const std::set<material_id> is_glass{material_id( "glass" )};
    return only_made_of( is_glass ) || has_flag( flag_SHATTERS );
}
