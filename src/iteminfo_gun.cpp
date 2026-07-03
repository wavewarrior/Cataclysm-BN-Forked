// iteminfo_gun.cpp — gun, gunmod, ammo, magazine display info methods.
// Split from item_info.cpp to reduce translation-unit size.

#include "item.h"

#include <algorithm>
#include <numeric>
#include <ranges>
#include <sstream>
#include <string>

#include "ammo.h"
#include "damage.h"
#include "dispersion.h"
#include "explosion.h"
#include "flag.h"
#include "gun_mode.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "options.h"
#include "output.h"
#include "ranged.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "avatar.h"
#include "magic_enchantment.h"
#include "skill.h"
#include "martialarts.h"

static const ammo_effect_str_id ammo_effect_BLACKPOWDER( "BLACKPOWDER" );
static const ammo_effect_str_id ammo_effect_INCENDIARY( "INCENDIARY" );
static const ammo_effect_str_id ammo_effect_NEVER_MISFIRES( "NEVER_MISFIRES" );
static const ammo_effect_str_id ammo_effect_RECYCLED( "RECYCLED" );

static const skill_id skill_throw( "throw" );

// TODO: Get rid of, handle multiple types gracefully
static int get_ranged_pierce( const common_ranged_data &ranged )
{
    if( ranged.damage.empty() ) {
        return 0;
    }
    return ranged.damage.damage_units.front().res_pen;
}

static float get_ranged_armor_mult( const common_ranged_data &ranged )
{
    if( ranged.damage.empty() ) {
        return 0.0f;
    }
    return ranged.damage.damage_units.front().res_mult;
}


void item::magazine_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int /*batch*/,
                          bool /*debug*/ ) const
{
    if( !is_magazine() || has_flag( flag_NO_RELOAD ) ) {
    return;
}

if( parts->test( iteminfo_parts::MAGAZINE_CAPACITY ) ) {
    for( const ammotype &at : ammo_types() ) {
            const std::string fmt = string_format( vgettext( "<num> round of %s",
                                                   "<num> rounds of %s", ammo_capacity() ),
                                                   at->name() );
            info.emplace_back( "MAGAZINE", _( "Capacity: " ), fmt, iteminfo::no_flags,
                               ammo_capacity() );
        }
    }
    if( parts->test( iteminfo_parts::MAGAZINE_RELOAD ) ) {
    info.emplace_back( "MAGAZINE", _( "Reload time: " ), _( "<num> moves per round" ),
                       iteminfo::lower_is_better, type->magazine->reload_time );
    }
    insert_separation_line( info );
}

void item::ammo_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int /* batch */,
                      bool /* debug */ ) const
{
    if( is_gun() || !ammo_data() || !parts->test( iteminfo_parts::AMMO_REMAINING_OR_TYPES ) ) {
    return;
}

const std::string space = "  ";
if( ammo_remaining() > 0 ) {
    info.emplace_back( "AMMO", _( "<bold>Ammunition</bold>: " ),
                       ammo_data()->nname( ammo_remaining() ) );
    } else if( is_ammo() ) {
    info.emplace_back( "AMMO", _( "<bold>Ammunition type</bold>: " ), ammo_type()->name() );
    }

    const islot_ammo &ammo = *ammo_data()->ammo;
    if( !ammo.damage.empty() || ammo.force_stat_display ) {
    bool has_flat_dmg = !ammo.damage.empty() && ammo.damage.damage_units.front().amount > 0;
        bool display_flat_dmg = parts->test( iteminfo_parts::AMMO_DAMAGE_VALUE );
        // TODO: Multiple units
        bool has_dmg_multiplier = ammo.damage.damage_units.front().damage_multiplier != 1.0;
        bool display_dmg_multiplier = parts->test( iteminfo_parts::AMMO_DAMAGE_PROPORTIONAL );
        bool didnt_print_dmg = false;

        // TODO: Deduplicate with damage display
        bool has_flat_arpen = get_ranged_pierce( ammo ) != 0;
        bool display_flat_arpen = parts->test( iteminfo_parts::AMMO_DAMAGE_AP );
        bool has_armor_mult = get_ranged_armor_mult( ammo ) != 1.0;
        bool display_armor_mult = parts->test( iteminfo_parts::AMMO_DAMAGE_AP_PROPORTIONAL );

        iteminfo::flags f = ( has_flat_arpen ||
                              has_armor_mult ) ? iteminfo::no_newline : iteminfo::no_flags;
        iteminfo::flags fd = ( has_flat_arpen ||
                               has_armor_mult ) ? iteminfo::no_newline | iteminfo::is_decimal : iteminfo::is_decimal;

        if( has_flat_dmg && has_dmg_multiplier
            && has_dmg_multiplier && display_dmg_multiplier ) {
            if( ammo.shot ) {
                info.emplace_back( "AMMO", _( "Damage: " ), "",
                                   iteminfo::no_newline, ammo.damage.total_damage() );
                info.emplace_back( "AMMO", "x", "",
                                   fd,
                                   ammo.shot->count );
                info.emplace_back( "AMMO", "/", "",
                                   fd,
                                   ammo.damage.damage_units.front().damage_multiplier );
            } else {
                info.emplace_back( "AMMO", _( "Damage: " ), "",
                                   iteminfo::no_newline, ammo.damage.total_damage() );
                info.emplace_back( "AMMO", "/", "",
                                   fd,
                                   ammo.damage.damage_units.front().damage_multiplier );
            }
            // Messy ifs...
        } else if( display_dmg_multiplier && has_dmg_multiplier ) {
            info.emplace_back( "AMMO", _( "Damage multiplier: " ), "",
                               fd,
                               ammo.damage.damage_units.front().damage_multiplier );
        } else if( display_flat_dmg && has_flat_dmg ) {
            if( ammo.shot ) {
                info.emplace_back( "AMMO", _( "Damage: " ), "",
                                   iteminfo::no_newline, ammo.damage.total_damage() );
                info.emplace_back( "AMMO", "x", "",
                                   f,
                                   ammo.shot->count );
            } else {
                info.emplace_back( "AMMO", _( "Damage: " ), "",
                                   f, ammo.damage.total_damage() );
            }
        } else {
            didnt_print_dmg = true;
        }

        // Ugly, but handles edge cases better than mandatory space
        static const std::string no_space;
        const std::string &maybe_space = didnt_print_dmg ? no_space : space;

        if( has_flat_arpen && display_flat_arpen
            && has_armor_mult && display_armor_mult ) {
            info.emplace_back( "AMMO", maybe_space + _( "Armor-pierce: " ), "",
                               iteminfo::no_newline, get_ranged_pierce( ammo ) );
            info.emplace_back( "AMMO", "/", "",
                               iteminfo::is_decimal | iteminfo::lower_is_better,
                               get_ranged_armor_mult( ammo ) );
        } else if( has_armor_mult && display_armor_mult ) {
            info.emplace_back( "AMMO", maybe_space + _( "Armor multiplier: " ), "",
                               iteminfo::is_decimal | iteminfo::lower_is_better, get_ranged_armor_mult( ammo ) );
        } else if( display_flat_arpen && has_flat_arpen ) {
            info.emplace_back( "AMMO", maybe_space + _( "Armor-pierce: " ), get_ranged_pierce( ammo ) );
        }
        if( parts->test( iteminfo_parts::AMMO_DAMAGE_RANGE ) && ammo.range != 0 ) {
            info.emplace_back( "AMMO", _( "Range: " ), "", iteminfo::no_newline, ammo.shape
                               ? static_cast<int>( ammo.shape->get_range() )
                               : ammo.range );
        }
        if( parts->test( iteminfo_parts::AMMO_DAMAGE_DISPERSION ) && ammo.dispersion != 0 ) {
            info.emplace_back( "AMMO", space + _( "Dispersion: " ), "",
                               iteminfo::lower_is_better, ammo.dispersion );
        }
        if( ammo.shot ) {
            info.emplace_back( "AMMO", space + _( "Pellet count: " ), "<num>", iteminfo::no_flags,
                               ammo.shot->count );
            info.emplace_back( "AMMO", space + _( "Pattern half-angle: " ), _( "<num> degrees" ),
                               iteminfo::is_decimal, ammo.shot->half_angle );
        }
        if( parts->test( iteminfo_parts::AMMO_DAMAGE_RECOIL ) && ammo.recoil != 0 ) {
            info.emplace_back( "AMMO", _( "Recoil: " ), "",
                               iteminfo::lower_is_better | iteminfo::no_newline, ammo.recoil );
        }
    }

    // TODO: De-hardcode. Have it imply a flag? Just include description_on_item in ammo_effect struct?
    std::vector<std::string> fx;
    if( ammo.shape &&
        parts->test( iteminfo_parts::AMMO_SHAPE ) ) {
    fx.emplace_back( string_format(
                         _( "This ammo will produce effects with the following shape:\n<bold>%s</bold>" ),
                         ammo.shape->get_description() ) );
    }
    if( ammo.ammo_effects.contains( ammo_effect_RECYCLED ) &&
        parts->test( iteminfo_parts::AMMO_FX_RECYCLED ) ) {
    fx.emplace_back(
        _( "This ammo has been <info>hand-loaded</info> and has a <bad>small chance to misfire</bad>." ) );
    }
    if( ammo.ammo_effects.contains( ammo_effect_BLACKPOWDER ) &&
        parts->test( iteminfo_parts::AMMO_FX_BLACKPOWDER ) ) {
    fx.emplace_back(
        _( "This ammo has been loaded with <bad>blackpowder</bad>, and will quickly "
           "clog up most guns, and cause rust if the gun is not cleaned." ) );
    }
    if( ammo.ammo_effects.contains( ammo_effect_NEVER_MISFIRES ) &&
        parts->test( iteminfo_parts::AMMO_FX_CANTMISSFIRE ) ) {
    fx.emplace_back( _( "This ammo <good>never misfires</good>." ) );
    }
    if( ammo.ammo_effects.contains( ammo_effect_INCENDIARY ) &&
        parts->test( iteminfo_parts::AMMO_FX_INCENDIARY ) ) {
    fx.emplace_back( _( "This ammo <neutral>starts fires</neutral>." ) );
    }
    if( !fx.empty() ) {
    insert_separation_line( info );
        for( const std::string &e : fx ) {
            info.emplace_back( "AMMO", e );
        }
    }
}

namespace
{
auto nname( const itype_id &id ) -> std::string
{
    return item::nname( id );
}
} // namespace

void item::gun_info( const item *mod, std::vector<iteminfo> &info, const iteminfo_query *parts,
                     int /* batch */, bool /* debug */ ) const
{
    const islot_gun &gun = *mod->type->gun;
    const Skill &skill = *mod->gun_skill();
    avatar &viewer = get_avatar();

    // many statistics are dependent upon loaded ammo
    // if item is unloaded (or is RELOAD_AND_SHOOT) shows approximate stats using default ammo
    const item *loaded_mod = mod;
    if( mod->ammo_required() && !mod->ammo_remaining() ) {
        item &tmp = *item::spawn_temporary( *mod );
        tmp.ammo_set( mod->magazine_current() ? tmp.common_ammo_default() : tmp.ammo_default() );
        if( tmp.ammo_data() == nullptr ) {
            insert_separation_line( info );
            info.emplace_back( "GUN",
                               _( "Weapon <bad>can't be loaded in its current state</bad>." ) );
            return;
        }
        loaded_mod = &tmp;
        if( parts->test( iteminfo_parts::GUN_DEFAULT_AMMO ) ) {
            insert_separation_line( info );
            info.emplace_back( "GUN",
                               _( "Weapon is <bad>not loaded</bad>, so stats below assume the default ammo: " ),
                               string_format( "<stat>%s</stat>",
                                              loaded_mod->ammo_data()->nname( 1 ) ) );
        }
    }

    const itype *curammo = loaded_mod->ammo_data();
    if( mod->ammo_required() && !curammo ) {
        debugmsg( "curammo is nullptr in item::gun_info()" );
        return;
    }
    damage_unit gun_du = gun_damage( false ).damage_units.front();

    gun_du.damage_multiplier *= ranged::str_draw_damage_modifier( *mod, viewer );

    damage_unit thrown_du = damage_unit( DT_STAB, 0 );

    damage_unit ammo_du = curammo != nullptr
                          ? curammo->ammo->damage.damage_units.front()
                          : damage_unit( DT_STAB, 0 );

    if( skill.ident() == skill_throw && curammo != nullptr ) {
        item &tmp = *item::spawn_temporary( item( curammo ) );

        thrown_du.amount += ranged::throw_damage( tmp,
                            get_avatar().get_skill_level( skill_throw ),
                            get_avatar().get_str() );
    }

    if( parts->test( iteminfo_parts::GUN_DAMAGE ) ) {
        insert_separation_line( info );
        info.emplace_back( "GUN", _( "<bold>Ranged damage</bold>: " ), "", iteminfo::no_newline,
                           gun_du.amount );
    }

    if( mod->ammo_required() ) {
        // ammo_damage, sum_of_damage, and ammo_mult not shown so don't need to translate.
        if( parts->test( iteminfo_parts::GUN_DAMAGE_LOADEDAMMO ) ) {
            assert( curammo ); // Appease clang-tidy
            damage_instance ammo_dam = curammo->ammo->damage;
            info.emplace_back( "GUN", "ammo_damage", "",
                               iteminfo::no_newline | iteminfo::no_name |
                               iteminfo::show_plus, std::max( ammo_du.amount, thrown_du.amount ) );
        }

        if( parts->test( iteminfo_parts::GUN_DAMAGE_TOTAL ) ) {
            // Intentionally not using total_damage() as it applies multipliers
            int total_damage = gun_du.amount + std::max( ammo_du.amount, thrown_du.amount );
            // Apply enchantment bonuses to damage display
            int base_bullet_damage = static_cast<int>( total_damage );
            int ench_damage_bonus = viewer.bonus_from_enchantments( base_bullet_damage,
                                    enchant_vals::mod::RANGED_DAMAGE_BULLET, true );
            int displayed_damage = total_damage + ench_damage_bonus;

            info.emplace_back( "GUN", "sum_of_damage", _( " = <num>" ),
                               iteminfo::no_newline | iteminfo::no_name,
                               displayed_damage );
            if( curammo != nullptr && curammo->ammo->shot ) {
                info.emplace_back( "GUN", "x", "",
                                   iteminfo::no_flags,
                                   curammo->ammo->shot->count );
            }

            if( ench_damage_bonus != 0 ) {
                info.emplace_back( "GUN", "ench_damage", _( " (enchanted: <num>)" ),
                                   iteminfo::no_name | iteminfo::show_plus,
                                   ench_damage_bonus );
            }
        }
    }
    info.back().bNewLine = true;
    avatar &you = get_avatar();
    int base_gun_range = loaded_mod->gun_range( true ); // Without player bonuses
    int max_gun_range = loaded_mod->gun_range( &you ); // Includes enchantment bonuses
    if( max_gun_range > 0 && parts->test( iteminfo_parts::GUN_MAX_RANGE ) ) {
        info.emplace_back( "GUN", _( "Maximum range: " ), "<num>", iteminfo::no_flags,
                           max_gun_range );

        // Show enchantment bonus if present
        int ench_range_bonus = you.bonus_from_enchantments( base_gun_range,
                               enchant_vals::mod::RANGED_RANGE, true );
        if( ench_range_bonus != 0 ) {
            info.emplace_back( "GUN", "ench_range", _( " (enchanted: <num>)" ),
                               iteminfo::no_name | iteminfo::show_plus,
                               ench_range_bonus );
        }
    }

    // TODO: This doesn't cover multiple damage types
    if( parts->test( iteminfo_parts::GUN_ARMORPIERCE ) ) {
        info.emplace_back( "GUN", _( "Armor-pierce: " ), "",
                           iteminfo::no_newline, get_ranged_pierce( gun ) );
    }
    if( mod->ammo_required() ) {
        assert( curammo ); // Appease clang-tidy
        int ammo_pierce = get_ranged_pierce( *curammo->ammo );
        // ammo_armor_pierce and sum_of_armor_pierce don't need to translate.
        if( parts->test( iteminfo_parts::GUN_ARMORPIERCE_LOADEDAMMO ) ) {
            info.emplace_back( "GUN", "ammo_armor_pierce", "",
                               iteminfo::no_newline | iteminfo::no_name |
                               iteminfo::show_plus, ammo_pierce );
        }
        if( parts->test( iteminfo_parts::GUN_ARMORPIERCE_TOTAL ) ) {
            info.emplace_back( "GUN", "sum_of_armor_pierce", _( " = <num>" ),
                               iteminfo::no_name,
                               get_ranged_pierce( gun ) + ammo_pierce );
        }
    }
    info.back().bNewLine = true;

    if( gun_du.damage_multiplier != 1.0 || ammo_du.damage_multiplier != 1.0 ) {
        if( parts->test( iteminfo_parts::GUN_DAMAGEMULT ) ) {
            info.emplace_back( "GUN", _( "Damage multiplier: " ), "",
                               iteminfo::no_newline | iteminfo::is_decimal,
                               gun_du.damage_multiplier );
        }

        if( mod->ammo_required() ) {
            if( parts->test( iteminfo_parts::GUN_DAMAGEMULT_AMMO ) ) {
                info.emplace_back( "GUN", "ammo_mult", "*",
                                   iteminfo::no_newline | iteminfo::no_name | iteminfo::is_decimal,
                                   ammo_du.damage_multiplier );
            }

            if( parts->test( iteminfo_parts::GUN_DAMAGEMULT_TOTAL ) ) {
                info.emplace_back( "GUN", "sum_of_damage", _( " = <num>" ),
                                   iteminfo::no_newline | iteminfo::no_name | iteminfo::is_decimal,
                                   gun_du.damage_multiplier * ammo_du.damage_multiplier );
            }
        }
        info.back().bNewLine = true;
    }

    if( gun_du.res_mult != 1.0 || ammo_du.res_mult != 1.0 ) {
        if( parts->test( iteminfo_parts::GUN_ARMORMULT ) ) {
            info.emplace_back( "GUN", _( "Armor multiplier: " ), "",
                               iteminfo::no_newline | iteminfo::lower_is_better | iteminfo::is_decimal,
                               gun_du.res_mult );
        }
        if( mod->ammo_required() ) {
            if( parts->test( iteminfo_parts::GUN_ARMORMULT_LOADEDAMMO ) ) {
                info.emplace_back( "GUN", "ammo_armor_mult", _( "*<num>" ),
                                   iteminfo::no_newline | iteminfo::no_name |
                                   iteminfo::lower_is_better | iteminfo::is_decimal,
                                   ammo_du.res_mult );
            }
            if( parts->test( iteminfo_parts::GUN_ARMORMULT_TOTAL ) ) {
                info.emplace_back( "GUN", "final_armor_mult", _( " = <num>" ),
                                   iteminfo::no_name | iteminfo::lower_is_better | iteminfo::is_decimal,
                                   gun_du.res_mult * ammo_du.res_mult );
            }
        }
        info.back().bNewLine = true;
    }

    if( parts->test( iteminfo_parts::GUN_DISPERSION ) ) {
        info.emplace_back( "GUN", _( "Dispersion: " ), "",
                           iteminfo::no_newline | iteminfo::lower_is_better,
                           mod->gun_dispersion( false, false ) );
    }
    if( mod->ammo_required() ) {
        int ammo_dispersion = curammo->ammo->dispersion;
        // ammo_dispersion and sum_of_dispersion don't need to translate.
        if( parts->test( iteminfo_parts::GUN_DISPERSION_LOADEDAMMO ) ) {
            info.emplace_back( "GUN", "ammo_dispersion", "",
                               iteminfo::no_newline | iteminfo::lower_is_better |
                               iteminfo::no_name | iteminfo::show_plus,
                               ammo_dispersion );
        }
        if( parts->test( iteminfo_parts::GUN_DISPERSION_TOTAL ) ) {
            int base_dispersion = loaded_mod->gun_dispersion( true, false );
            info.emplace_back( "GUN", "sum_of_dispersion", _( " = <num>" ),
                               iteminfo::lower_is_better | iteminfo::no_name | iteminfo::no_newline,
                               base_dispersion );

            // effective_dispersion includes ALL bonuses from character, skills, etc.
            // but enchantment is applied last in get_weapon_dispersion
            int effective_dispersion = static_cast<int>( ranged::get_weapon_dispersion( you, *this ).max() );
            info.emplace_back( "GUN", "eff_dispersion", _( " (effective: <num>)" ),
                               iteminfo::lower_is_better | iteminfo::no_name,
                               effective_dispersion );

            // Calculate enchantment bonus the same way as ranged.cpp:
            // Use the effective dispersion (before final enchantment) as base
            // Note: This isn't perfect because effective already includes some effects,
            // but it's consistent with how ranged.cpp applies enchantments
            int ench_dispersion_bonus = you.bonus_from_enchantments( effective_dispersion,
                                        enchant_vals::mod::RANGED_DISPERSION, true );

            if( ench_dispersion_bonus != 0 ) {
                info.emplace_back( "GUN", "ench_dispersion", _( " (enchanted: <num>)" ),
                                   iteminfo::lower_is_better | iteminfo::no_name | iteminfo::show_plus,
                                   ench_dispersion_bonus );
            }
        }
    }
    info.back().bNewLine = true;

    // if effective sight dispersion differs from actual sight dispersion display both
    int act_disp = mod->sight_dispersion();
    int eff_disp = ranged::effective_dispersion( you, act_disp );
    int adj_disp = eff_disp - act_disp;

    if( parts->test( iteminfo_parts::GUN_DISPERSION_SIGHT ) ) {
        info.emplace_back( "GUN", _( "Sight dispersion: " ), "",
                           iteminfo::no_newline | iteminfo::lower_is_better,
                           act_disp );

        if( adj_disp ) {
            info.emplace_back( "GUN", "sight_adj_disp", "",
                               iteminfo::no_newline | iteminfo::lower_is_better |
                               iteminfo::no_name | iteminfo::show_plus, adj_disp );
            info.emplace_back( "GUN", "sight_eff_disp", _( " = <num>" ),
                               iteminfo::lower_is_better | iteminfo::no_name,
                               eff_disp );
        }
    }

    bool bipod = mod->has_flag( flag_BIPOD );

    if( loaded_mod->gun_recoil() ) {
        if( parts->test( iteminfo_parts::GUN_RECOIL_PERCENTAGE ) ) {
            info.emplace_back( "GUN", _( "Recoil percentage: " ), "",
                               iteminfo::no_newline | iteminfo::lower_is_better,
                               std::ceil( 100 * loaded_mod->gun_recoil_multiplier() ) );
        }
        if( bipod && parts->test( iteminfo_parts::GUN_RECOIL_PERCENTAGE_BIPOD ) ) {
            info.emplace_back( "GUN", "bipod_recoil_percentage", _( " (with bipod <num>)" ),
                               iteminfo::lower_is_better | iteminfo::no_name,
                               std::ceil( 100 * loaded_mod->gun_recoil_multiplier( true ) ) );
        }
        info.back().bNewLine = true;

        if( parts->test( iteminfo_parts::GUN_RECOIL ) ) {
            int base_recoil = loaded_mod->gun_recoil();
            int ench_recoil_bonus = you.bonus_from_enchantments( base_recoil,
                                    enchant_vals::mod::RANGED_RECOIL, true );
            int effective_recoil = std::max( 0, base_recoil + ench_recoil_bonus );

            info.emplace_back( "GUN", _( "Effective recoil: " ), "",
                               iteminfo::no_newline | iteminfo::lower_is_better,
                               effective_recoil );

            if( ench_recoil_bonus != 0 ) {
                info.emplace_back( "GUN", "ench_recoil", _( " (enchanted: <num>)" ),
                                   iteminfo::lower_is_better | iteminfo::no_name | iteminfo::show_plus,
                                   ench_recoil_bonus );
            }
        }
        if( bipod && parts->test( iteminfo_parts::GUN_RECOIL_BIPOD ) ) {
            info.emplace_back( "GUN", "bipod_recoil", _( " (with bipod <num>)" ),
                               iteminfo::lower_is_better | iteminfo::no_name,
                               loaded_mod->gun_recoil( true ) );
        }
    }
    info.back().bNewLine = true;

    std::map<gun_mode_id, gun_mode> fire_modes = mod->gun_all_modes();
    if( parts->test( iteminfo_parts::GUN_BURST_PENALTY ) ) {
        if( std::ranges::any_of( fire_modes,
        []( const std::pair<gun_mode_id, gun_mode> &e ) {
        return e.second.qty > 1 && !e.second.melee();
        } ) ) {
            info.emplace_back( "GUN", _( "Burst fire penalty: " ), "",
                               iteminfo::no_newline | iteminfo::lower_is_better,
                               ranged::burst_penalty( you, *this, loaded_mod->gun_recoil() ) );
            if( bipod ) {
                info.emplace_back( "GUN", "bipod_burst", _( " (with bipod <num>)" ),
                                   iteminfo::lower_is_better | iteminfo::no_name,
                                   ranged::burst_penalty( you, *this, loaded_mod->gun_recoil( true ) ) );
            }
        }
        info.back().bNewLine = true;
    }

    if( parts->test( iteminfo_parts::GUN_RELOAD_TIME ) ) {
        int base_reload_time = mod->get_reload_time();
        int ench_reload_bonus = you.bonus_from_enchantments( base_reload_time,
                                enchant_vals::mod::RANGED_RELOAD_TIME, true );
        int effective_reload = std::max( 25, base_reload_time + ench_reload_bonus );

        info.emplace_back( "GUN", _( "Reload time: " ),
                           has_flag( flag_RELOAD_ONE ) ? _( "<num> moves per round" ) :
                           _( "<num> moves " ),
                           iteminfo::lower_is_better | ( ench_reload_bonus != 0 ? iteminfo::no_newline : iteminfo::no_flags ),
                           effective_reload );

        if( ench_reload_bonus != 0 ) {
            info.emplace_back( "GUN", "ench_reload", _( " (enchanted: <num>)" ),
                               iteminfo::lower_is_better | iteminfo::no_name | iteminfo::show_plus,
                               ench_reload_bonus );
        }
    }

    if( parts->test( iteminfo_parts::GUN_USEDSKILL ) ) {
        info.emplace_back( "GUN", _( "Skill used: " ),
                           "<info>" + skill.name() + "</info>" );
    }

    if( mod->magazine_integral() || mod->magazine_current() ) {
        if( mod->magazine_current() && parts->test( iteminfo_parts::GUN_MAGAZINE ) ) {
            info.emplace_back( "GUN", _( "Magazine: " ),
                               string_format( "<stat>%s</stat>",
                                              mod->magazine_current()->tname() ) );
        }
        if( mod->ammo_capacity() && parts->test( iteminfo_parts::GUN_CAPACITY ) ) {
            for( const ammotype &at : mod->ammo_types() ) {
                const std::string fmt = string_format( vgettext( "<num> round of %s",
                                                       "<num> rounds of %s",
                                                       mod->ammo_capacity() ), at->name() );
                info.emplace_back( "GUN", _( "Capacity: " ), fmt, iteminfo::no_flags,
                                   mod->ammo_capacity() );
            }
        }
    } else if( parts->test( iteminfo_parts::GUN_TYPE ) ) {
        info.emplace_back( "GUN", _( "Type: " ), enumerate_as_string( mod->ammo_types().begin(),
        mod->ammo_types().end(), []( const ammotype & at ) {
            return at->name();
        }, enumeration_conjunction::none ) );
    }

    if( mod->ammo_data() && parts->test( iteminfo_parts::AMMO_REMAINING ) ) {
        info.emplace_back( "AMMO", _( "Ammunition: " ), string_format( "<stat>%s</stat>",
                           mod->ammo_data()->nname( mod->ammo_remaining() ) ) );
    }

    if( mod->ammo_required() > 1 ) {
        info.emplace_back( "AMMO", string_format( "Uses <stat>%i</stat> ammo per shot",
                           mod->ammo_required() ) );
    }

    if( mod->get_gun_ups_drain() && parts->test( iteminfo_parts::AMMO_UPSCOST ) ) {
        info.emplace_back( "AMMO",
                           string_format( vgettext( "Uses <stat>%i</stat> charge of UPS per shot",
                                          "Uses <stat>%i</stat> charges of UPS per shot",
                                          mod->get_gun_ups_drain() ),
                                          mod->get_gun_ups_drain() ) );
    }

    if( skill.ident() == skill_throw ) {
        info.emplace_back( "GUN",
                           _( "Damage/range will vary with <info>throwing skill and ammo.</info>" ) );
    }

    if( parts->test( iteminfo_parts::GUN_AIMING_STATS ) ) {
        insert_separation_line( info );
        int final_aim = ranged::aim_per_move( you, *mod, MAX_RECOIL );
        double add = you.bonus_from_enchantments( 0, enchant_vals::mod::RANGED_AIM_SPEED, false );
        double mul = ( you.bonus_from_enchantments( 1000, enchant_vals::mod::RANGED_AIM_SPEED,
                       false ) - add ) / 1000.0;
        int base_aim = final_aim;
        if( 1.0 + mul != 0.0 ) {
            base_aim = std::round( ( final_aim - add ) / ( 1.0 + mul ) );
        }
        int ench_aim_bonus = final_aim - base_aim;
        info.emplace_back( "GUN", _( "<bold>Base aim speed</bold>: " ), "<num>",
                           ( ench_aim_bonus != 0 ) ? iteminfo::no_newline : iteminfo::no_flags,
                           final_aim );
        if( ench_aim_bonus != 0 ) {
            info.emplace_back( "GUN", "ench_aim_speed", _( " (enchanted: <num>)" ),
                               iteminfo::no_name | iteminfo::show_plus,
                               ench_aim_bonus );
            info.back().bNewLine = true;
        }

        for( const ranged::aim_type &type : ranged::get_aim_types( you, *mod ) ) {
            // Nameless aim levels don't get an entry.
            if( type.name.empty() ) {
                continue;
            }
            // For item comparison to work correctly each info object needs a
            // distinct tag per aim type.
            const std::string tag = "GUN_" + type.name;
            info.emplace_back( tag, string_format( "<info>%s</info>", type.name ) );
            int max_dispersion = ranged::get_weapon_dispersion( you, *loaded_mod ).max();
            int range = range_with_even_chance_of_good_hit( max_dispersion + type.threshold );
            info.emplace_back( tag, _( "Even chance of good hit at range: " ),
                               _( "<num>" ), iteminfo::no_flags, range );
            int aim_mv = ranged::gun_engagement_moves( you, *mod, type.threshold );
            info.emplace_back( tag, _( "Time to reach aim level: " ), _( "<num> moves " ),
                               iteminfo::lower_is_better, aim_mv );
        }
    }

    if( parts->test( iteminfo_parts::GUN_FIRE_MODES ) ) {
        std::vector<std::string> fm;
        for( const std::pair<const gun_mode_id, gun_mode> &e : fire_modes ) {
            if( e.second.target == this && !e.second.melee() ) {
                fm.emplace_back( string_format( "%s (%i)", e.second.tname(), e.second.qty ) );
            }
        }
        if( !fm.empty() ) {
            insert_separation_line( info );
            info.emplace_back( "GUN", _( "<bold>Fire modes</bold>: " ) +
                               enumerate_as_string( fm ) );
        }
    }

    if( !magazine_integral() && parts->test( iteminfo_parts::GUN_ALLOWED_MAGAZINES ) ) {
        const auto &compat = magazine_compatible();
        if( !compat.empty() ) {
            insert_separation_line( info );
            info.emplace_back( "DESCRIPTION", _( "<bold>Compatible magazines</bold>: " )
                               + enumerate_as_string( compat, ::nname ) );
        }
    }

    if( !gun.valid_mod_locations.empty() && parts->test( iteminfo_parts::DESCRIPTION_GUN_MODS ) ) {
        insert_separation_line( info );

        std::string mod_str = _( "<bold>Mods</bold>: " );

        std::map<gunmod_location, int> mod_locations = get_mod_locations();

        int iternum = 0;
        for( std::pair<const gunmod_location, int> &elem : mod_locations ) {
            if( iternum != 0 ) {
                mod_str += "; ";
            }
            const int free_slots = ( elem ).second - get_free_mod_locations( elem.first );
            mod_str += string_format( "<bold>%d/%d</bold> %s", free_slots,  elem.second,
                                      elem.first.name() );
            bool first_mods = true;
            for( const item *gmod : gunmods() ) {
                if( gmod->type->gunmod->location == ( elem ).first ) { // if mod for this location
                    if( first_mods ) {
                        mod_str += ": ";
                        first_mods = false;
                    } else {
                        mod_str += ", ";
                    }
                    mod_str += string_format( "<stat>%s</stat>", gmod->tname() );
                }
            }
            iternum++;
        }
        mod_str += ".";
        info.emplace_back( "DESCRIPTION", mod_str );
    }

    if( mod->casings_count() && parts->test( iteminfo_parts::DESCRIPTION_GUN_CASINGS ) ) {
        insert_separation_line( info );
        std::string tmp = vgettext( "Contains <stat>%i</stat> casing",
                                    "Contains <stat>%i</stat> casings", mod->casings_count() );
        info.emplace_back( "DESCRIPTION", string_format( tmp, mod->casings_count() ) );
    }
}

void item::gunmod_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int /* batch */,
                        bool /* debug */ ) const
{
    if( !is_gunmod() ) {
    return;
}
const islot_gunmod &mod = *type->gunmod;

if( is_gun() && parts->test( iteminfo_parts::DESCRIPTION_GUNMOD ) ) {
    info.emplace_back( "DESCRIPTION",
                       _( "This mod <info>must be attached to a gun</info>, "
                          "it can not be fired separately." ) );
    }
    if( has_flag( flag_REACH_ATTACK ) && parts->test( iteminfo_parts::DESCRIPTION_GUNMOD_REACH ) ) {
    info.emplace_back( "DESCRIPTION",
                       _( "When attached to a gun, <good>allows</good> making "
                          "<info>reach melee attacks</info> with it." ) );
    }
    if( mod.dispersion != 0 && parts->test( iteminfo_parts::GUNMOD_DISPERSION ) ) {
    info.emplace_back( "GUNMOD", _( "Dispersion modifier: " ), "",
                       iteminfo::lower_is_better | iteminfo::show_plus,
                       mod.dispersion );
    }
    if( mod.sight_dispersion != -1 && parts->test( iteminfo_parts::GUNMOD_DISPERSION_SIGHT ) ) {
    info.emplace_back( "GUNMOD", _( "Sight dispersion: " ), "",
                       iteminfo::lower_is_better, mod.sight_dispersion );
    }
    if( mod.aim_speed >= 0 && parts->test( iteminfo_parts::GUNMOD_AIMSPEED ) ) {
    info.emplace_back( "GUNMOD", _( "Aim speed: " ), "",
                       iteminfo::lower_is_better, mod.aim_speed );
    }
    int total_damage = static_cast<int>( mod.damage.total_damage() );
    if( total_damage != 0 && parts->test( iteminfo_parts::GUNMOD_DAMAGE ) ) {
    info.emplace_back( "GUNMOD", _( "Damage: " ), "", iteminfo::show_plus,
                       total_damage );
    }
    int pierce = get_ranged_pierce( mod );
    if( get_ranged_pierce( mod ) != 0 && parts->test( iteminfo_parts::GUNMOD_ARMORPIERCE ) ) {
    info.emplace_back( "GUNMOD", _( "Armor-pierce: " ), "", iteminfo::show_plus,
                       pierce );
    }
    if( mod.handling != 0 && parts->test( iteminfo_parts::GUNMOD_HANDLING ) ) {
    info.emplace_back( "GUNMOD", _( "Handling modifier: " ), "",
                       iteminfo::show_plus, mod.handling );
    }
    if( mod.range != 0 && parts->test( iteminfo_parts::GUNMOD_RANGE ) ) {
    info.emplace_back( "GUNMOD", _( "Range modifier: " ), "",
                       iteminfo::show_plus, mod.range );
    }
    if( !type->mod->ammo_modifier.empty() && parts->test( iteminfo_parts::GUNMOD_AMMO ) ) {
    for( const ammotype &at : type->mod->ammo_modifier ) {
            info.emplace_back( "GUNMOD", string_format( _( "Ammo: <stat>%s</stat>" ),
                               at->name() ) );
        }
    }
    if( mod.reload_modifier != 0 && parts->test( iteminfo_parts::GUNMOD_RELOAD ) ) {
    info.emplace_back( "GUNMOD", _( "Reload modifier: " ), _( "<num>%" ),
                       iteminfo::lower_is_better, mod.reload_modifier );
    }
    if( mod.min_str_required_mod > 0 && parts->test( iteminfo_parts::GUNMOD_STRENGTH ) ) {
    info.emplace_back( "GUNMOD", _( "Minimum strength required modifier: " ),
                       mod.min_str_required_mod );
    }
    if( !mod.add_mod.empty() && parts->test( iteminfo_parts::GUNMOD_ADD_MOD ) ) {
    insert_separation_line( info );

        std::string mod_loc_str = _( "<bold>Adds mod locations: </bold> " );

        std::map<gunmod_location, int> mod_locations = mod.add_mod;

        int iternum = 0;
        for( std::pair<const gunmod_location, int> &elem : mod_locations ) {
            if( iternum != 0 ) {
                mod_loc_str += "; ";
            }
            mod_loc_str += string_format( "<bold>%s</bold> %s", elem.second, elem.first.name() );
            iternum++;
        }
        mod_loc_str += ".";
        info.emplace_back( "GUNMOD", mod_loc_str );
    }

    insert_separation_line( info );

    if( parts->test( iteminfo_parts::GUNMOD_USEDON ) ) {
    std::string used_on_str = _( "<bold>Used on:</bold>" );

        if( !mod.usable.empty() ) {
            used_on_str += _( "\n  Specific: " ) + enumerate_as_string( mod.usable.begin(),
            mod.usable.end(), []( const itype_id & used_on ) {
                return string_format( "<info>%s</info>", used_on->nname( 1 ) );
            } );
        }

        if( !mod.usable_category.empty() ) {
            used_on_str += _( "\n  Category: " );
            std::vector<std::string> combination;
            combination.reserve( mod.usable_category.size() );
            for( const std::unordered_set<weapon_category_id> &catgroup : mod.usable_category ) {
                combination.emplace_back( ( "[" ) + enumerate_as_string( catgroup.begin(),
                catgroup.end(), []( const weapon_category_id & wcid ) {
                    return string_format( "<info>%s</info>", wcid->name().translated() );
                }, enumeration_conjunction::none ) + ( "]" ) );
            }
            used_on_str += enumerate_as_string( combination, enumeration_conjunction::or_ );
        }

        info.emplace_back( "GUNMOD", used_on_str );
    }

    if( !( mod.exclusion.empty() && mod.exclusion_category.empty() ) &&
            parts->test( iteminfo_parts::GUNMOD_EXCLUSION ) ) {
        std::string exclusion_str = _( "<bold>Cannot be used on:</bold>" );

        if( !mod.exclusion.empty() ) {
            exclusion_str += _( "\n  Specific: " ) + enumerate_as_string( mod.exclusion.begin(),
            mod.exclusion.end(), []( const itype_id & excluded ) {
                return string_format( "<info>%s</info>", excluded->nname( 1 ) );
            } );
        }

        if( !mod.exclusion_category.empty() ) {
            exclusion_str += _( "\n  Category: " );
            std::vector<std::string> combination;
            combination.reserve( mod.exclusion_category.size() );
            for( const std::unordered_set<weapon_category_id> &catgroup : mod.exclusion_category ) {
                combination.emplace_back( ( "[" ) + enumerate_as_string( catgroup.begin(),
                catgroup.end(), []( const weapon_category_id & wcid ) {
                    return string_format( "<info>%s</info>", wcid->name().translated() );
                }, enumeration_conjunction::none ) + ( "]" ) );
            }
            exclusion_str += enumerate_as_string( combination, enumeration_conjunction::or_ );
        }

        info.emplace_back( "GUNMOD", exclusion_str );
    }

    if( parts->test( iteminfo_parts::GUNMOD_LOCATION ) ) {
    info.emplace_back( "GUNMOD", string_format( _( "Location: %s" ),
                       mod.location.name() ) );
    }

    if( !mod.blacklist_mod.empty() && parts->test( iteminfo_parts::GUNMOD_BLACKLIST_MOD ) ) {
    std::string mod_black_str = _( "<bold>Incompatible with mod location: </bold> " );

        int iternum = 0;
        for( const gunmod_location &black : mod.blacklist_mod ) {
            if( iternum != 0 ) {
                mod_black_str += ", ";
            }
            mod_black_str += string_format( "%s", black.name() );
            iternum++;
        }
        mod_black_str += ".";
        info.emplace_back( "GUNMOD", mod_black_str );
    }
}

