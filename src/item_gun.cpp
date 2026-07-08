// Item gun/ranged methods: gun stats, fire modes, ammo, sight, recoil
// — split out of item.cpp. .cpp-only, no API changes.

#include "item.h"

#include <algorithm>
#include <numeric>
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
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>

#include "active_tile_data_def.h"
#include "ammo.h"
#include "ascii_art.h"
#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "cached_item_options.h"
#include "catalua_icallback_actor.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_encumbrance.h"
#include "character_functions.h"
#include "character_id.h"
#include "character_martial_arts.h"
#include "character_stat.h"
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
#include "projectile.h"
#include "profile.h"
#include "ranged.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "relic.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "rot.h"
#include "scores_ui.h"
#include "cloning_utils.h"
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

// File-scope id constants (moved with gun methods; internal linkage).
static const std::string GUN_MODE_VAR_NAME( "item::mode" );
static const gun_mode_id gun_mode_REACH( "REACH" );
static const itype_id itype_tuned_mechanism( "tuned_mechanism" );
static const itype_id itype_waterproof_gunmod( "waterproof_gunmod" );
static const skill_id skill_throw( "throw" );
static const skill_id skill_unarmed( "unarmed" );

skill_id item::gun_skill() const
{
    if( !is_gun() ) {
    return skill_id::NULL_ID();
    }
    return type->gun->skill_used;
}

skill_id item::melee_skill() const
{
    if( !is_melee() ) {
    return skill_id::NULL_ID();
    }

    if( has_flag( flag_UNARMED_WEAPON ) ) {
    return skill_unarmed;
}

int hi = 0;
skill_id res = skill_id::NULL_ID();

for( int idx = DT_NULL + 1; idx != NUM_DT; ++idx ) {
    const int val = damage_melee( static_cast<damage_type>( idx ) );
        const skill_id &sk  = skill_by_dt( static_cast<damage_type>( idx ) );
        if( val > hi && sk ) {
            hi = val;
            res = sk;
        }
    }

    return res;
}

int item::gun_dispersion( bool with_ammo, bool with_scaling ) const
{
    if( !is_gun() ) {
    return 0;
}
int dispersion_sum = type->gun->dispersion;
for( const item *mod : gunmods() ) {
    dispersion_sum += mod->type->gunmod->dispersion;
}
dispersion_sum += get_dispersion_bonus();
int dispPerDamage = get_option< int >( "DISPERSION_PER_GUN_DAMAGE" );
dispersion_sum += damage_level( 4 ) * dispPerDamage;
dispersion_sum = std::max( dispersion_sum, 0 );
if( with_ammo && ammo_data() ) {
    dispersion_sum += ammo_data()->ammo->dispersion;
    }
    if( !with_scaling ) {
    return dispersion_sum;
}

// Dividing dispersion by 15 temporarily as a gross adjustment,
// will bake that adjustment into individual gun definitions in the future.
// Absolute minimum gun dispersion is 1.
double divider = get_option< float >( "GUN_DISPERSION_DIVIDER" );
dispersion_sum = std::max( static_cast<int>( std::round( dispersion_sum / divider ) ), 1 );

    return dispersion_sum;
}

int item::sight_dispersion() const
{
    if( !is_gun() ) {
    return 0;
}

int res = has_flag( flag_DISABLE_SIGHTS ) ? 90 : type->gun->sight_dispersion;

for( const item *e : gunmods() ) {
    const islot_gunmod &mod = *e->type->gunmod;
    if( mod.sight_dispersion < 0 || mod.aim_speed < 0 ) {
            continue; // skip gunmods which don't provide a sight
        }
        res = std::min( res, mod.sight_dispersion );
    }

    return res;
}

damage_instance item::gun_damage( bool with_ammo ) const
{
    if( !is_gun() ) {
    return damage_instance();
    }
    damage_instance ret = type->gun->damage;

for( const item *mod : gunmods() ) {
    ret.add( mod->type->gunmod->damage );
    }

    if( with_ammo && ammo_data() ) {
    ret.add( ammo_data()->ammo->damage );
    }

    ret.add( get_ranged_damage_bonus() );

    int item_damage = damage_level( 4 );
    if( item_damage > 0 ) {
    // TODO: This isn't a good solution for multi-damage guns/ammos
    for( damage_unit &du : ret ) {
            if( du.amount <= 1.0 ) {
                continue;
            }
            du.amount = std::max<float>( 1.0f, du.amount - item_damage * 2 );
        }
    }

    return ret;
}

double item::gun_recoil_multiplier( bool bipod ) const
{
    double handling = type->gun->handling;
    for( const item *mod : gunmods() ) {
        if( bipod || !mod->has_flag( flag_BIPOD ) ) {
            handling += mod->type->gunmod->handling;
        }
    }

    // Rescale from JSON units which are intentionally specified as integral values
    handling /= 10;

    // Handling will almost always be above 1.0
    if( handling > 1.0 ) {
        return 1.0 / handling;
    } else {
        return 2.0 - handling;
    }
}

int item::gun_recoil( bool bipod ) const
{
    if( !is_gun() || ( ammo_required() && !ammo_remaining() ) ) {
    return 0;
}

int qty = type->gun->recoil;
if( ammo_data() ) {
    qty += ammo_data()->ammo->recoil;
    }

    qty += get_recoil_bonus();

    return qty * gun_recoil_multiplier( bipod );
}

int item::gun_range( bool with_ammo ) const
{
    if( !is_gun() ) {
    return 0;
}
int ret = type->gun->range;
for( const item *mod : gunmods() ) {
    ret += mod->type->gunmod->range;
}
if( with_ammo && ammo_data() ) {
    const auto &ammo_shape = ammo_data()->ammo->shape;
        if( ammo_shape ) {
            ret = ammo_shape->get_range();
        } else {
            int ret_thrown = 0;
            if( gun_skill() == skill_throw && ammo_data() ) {
                const itype *curammo = ammo_data();
                item &tmp = *item::spawn_temporary( item( curammo ) );
                ret_thrown += get_avatar().throw_range( tmp );
            }
            ret += std::max( ammo_data()->ammo->range, ret_thrown );
        }
    }
    ret += get_range_bonus();
    return std::min( std::max( 0, ret ), g_max_view_distance );
}

int item::gun_range( const player *p ) const
{
    int ret = gun_range( true );
    if( p == nullptr ) {
        return ret;
    }
    if( !p->meets_requirements( *this ) ) {
        return 0;
    }

    // Reduce bow range if player has less than minimum strength.
    ret *= ranged::str_draw_range_modifier( *this, *p );

    // Apply enchantment bonuses to range
    int ench_range_bonus = p->bonus_from_enchantments( ret, enchant_vals::mod::RANGED_RANGE, true );
    ret = std::max( 1, ret + ench_range_bonus );

    return std::max( 0, ret );
}

int item::gun_speed( bool with_ammo ) const
{
    if( !is_gun() ) {
    return 10;
}
int ret = type->gun->speed;
for( const item *mod : gunmods() ) {
    ret += mod->type->gunmod->speed;
}
if( with_ammo && ammo_data() ) {
    ret += ammo_data()->ammo->speed;
    }
    return std::max( 0, ret );
}

double item::gun_aimed_crit_bonus( bool with_ammo ) const
{
    if( !is_gun() ) {
    return 0;
}
int ret = type->gun->aimedcritbonus;
for( const item *mod : gunmods() ) {
    ret += mod->type->gunmod->aimedcritbonus;
}
if( with_ammo && ammo_data() ) {
    ret += ammo_data()->ammo->aimedcritbonus;
    }
    return std::max( 0, ret );
}

double item::gun_aimed_crit_max_bonus( bool with_ammo ) const
{
    if( !is_gun() ) {
    return 0;
}
int ret = type->gun->aimedcritmaxbonus;
for( const item *mod : gunmods() ) {
    ret += mod->type->gunmod->aimedcritmaxbonus;
}
if( with_ammo && ammo_data() ) {
    ret += ammo_data()->ammo->aimedcritmaxbonus;
    }
    return std::max( 0, ret );
}

units::energy item::energy_remaining() const
{
    if( is_battery() ) {
    return energy;
}

return 0_J;
}

int item::ammo_remaining() const
{
    const item *mag = magazine_current();
    if( mag ) {
        return mag->ammo_remaining();
    }

    if( is_tool() || is_gun() ) {
        // includes auxiliary gunmods
        if( has_flag( flag_USES_BIONIC_POWER ) ) {
            int power = units::to_kilojoule( get_avatar().get_power_level() );
            return power;
        }
        return charges;
    }

    if( is_magazine() || is_bandolier() ) {
        int res = 0;
        for( const item *e : contents.all_items_top() ) {
            res += e->charges;
        }
        return res;
    }

    return 0;
}

int item::ammo_capacity() const
{
    return ammo_capacity( false );
}

int item::ammo_capacity( bool potential_capacity ) const
{
    int res = 0;

    const item *mag = magazine_current();

    if( has_flag( flag_USES_BIONIC_POWER ) ) {
        avatar &you = get_avatar();
        return you.get_max_power_level() / 1_kJ;
    }
    if( mag ) {
        return mag->ammo_capacity();
    }

    if( is_tool() ) {
        res = type->tool->max_charges;
        if( res == 0 && magazine_default() && potential_capacity ) {
            res = magazine_default()->magazine->capacity;
        }
        for( const item *e : toolmods() ) {
            res *= e->type->mod->capacity_multiplier;
        }
    }

    if( is_gun() ) {
        res = type->gun->clip;
        for( const item *e : gunmods() ) {
            res *= e->type->mod->capacity_multiplier;
        }
    }

    if( is_magazine() ) {
        res = type->magazine->capacity;
    }

    if( is_bandolier() ) {
        return dynamic_cast<const bandolier_actor *>
               ( type->get_use( "bandolier" )->get_actor_ptr() )->capacity;
    }

    return res;
}

int item::ammo_required() const
{
    if( is_tool() ) {
    return std::max( type->charges_to_use(), 0 );
    }

    if( is_gun() ) {
    if( ammo_types().empty() ) {
            return 0;
        } else if( has_flag( flag_FIRE_100 ) ) {
            return 100;
        } else if( has_flag( flag_FIRE_50 ) ) {
            return 50;
        } else if( has_flag( flag_FIRE_20 ) ) {
            return 20;
        } else {
            int modifier = 0;
            float multiplier = 1.0f;
            for( const item *mod : gunmods() ) {
                modifier += mod->type->gunmod->ammo_to_fire_modifier;
                multiplier *= mod->type->gunmod->ammo_to_fire_multiplier;
            }
            return ( type->gun->ammo_to_fire * multiplier ) + modifier;
        }
    }

    return 0;
}

bool item::ammo_sufficient( int qty ) const
{
    return ammo_remaining() >= ammo_required() * qty;
}

int item::ammo_consume( int qty, const tripoint_bub_ms &pos )
{
    if( qty < 0 ) {
        debugmsg( "Cannot consume negative quantity of ammo for %s", tname() );
        return 0;
    }

    item *mag = magazine_current();
    if( mag ) {
        const int res = mag->ammo_consume( qty, pos );
        if( res && ammo_remaining() == 0 ) {
            if( mag->has_flag( flag_MAG_DESTROY ) ) {
                remove_item( *mag );
            } else if( mag->has_flag( flag_MAG_EJECT ) ) {
                get_map().add_item( pos, remove_item( *mag ) );
            }
        }
        return res;
    }

    if( is_magazine() ) {
        int need = qty;
        while( !contents.empty() ) {
            item &e = contents.front();
            if( need >= e.charges ) {
                need -= e.charges;
                remove_item( contents.front() );
                e.destroy();
            } else {
                e.charges -= need;
                need = 0;
                break;
            }
        }
        return qty - need;

    } else if( is_tool() || is_gun() ) {
        qty = std::min( qty, charges );
        if( has_flag( flag_USES_BIONIC_POWER ) ) {
            avatar &you = get_avatar();
            charges = units::to_kilojoule( you.get_power_level() );
            you.mod_power_level( units::from_kilojoule( -qty ) );
        }
        charges -= qty;
        if( charges == 0 ) {
            curammo = nullptr;
        }
        return qty;
    }

    return 0;
}

const itype *item::ammo_data() const
{
    const item *mag = magazine_current();
    if( mag ) {
        return mag->ammo_data();
    }

    if( is_ammo() ) {
        return type;
    }

    if( is_magazine() ) {
        return !contents.empty() ? contents.front().ammo_data() : nullptr;
    }

    auto mods = is_gun() ? gunmods() : toolmods();
    for( const item *e : mods ) {
        if( !e->type->mod->ammo_modifier.empty() && e->ammo_current() &&
            e->ammo_current().is_valid() ) {
            return &*e->ammo_current();
        }
    }

    return curammo;
}

itype_id item::ammo_current() const
{
    const itype *ammo = ammo_data();
    return ammo ? ammo->get_id() : itype_id::NULL_ID();
}

const std::set<ammotype> &item::ammo_types( bool conversion ) const
{
    if( conversion ) {
    const std::vector<const item *> &mods = is_gun() ? gunmods() : toolmods();
        for( const item *e : mods ) {
            if( !e->type->mod->ammo_modifier.empty() ) {
                return e->type->mod->ammo_modifier;
            }
        }
    }

    if( is_gun() ) {
        return type->gun->ammo;
    } else if( is_tool() ) {
        return type->tool->ammo_id;
    } else if( is_magazine() ) {
        return type->magazine->type;
    }

    static std::set<ammotype> atypes = {};
    return atypes;
}

ammotype item::ammo_type() const
{
    if( is_ammo() ) {
    return type->ammo->type;
}
return ammotype::NULL_ID();
}

itype_id item::ammo_default( bool conversion ) const
{
    if( is_magazine() ) {
    return type->magazine->default_ammo;
} else if( is_tool() && type->tool->default_ammo != itype_id::NULL_ID() ) {
    return type->tool->default_ammo;
}

const std::set<ammotype> &atypes = ammo_types( conversion );
if( !atypes.empty() ) {
    itype_id res = ammotype( *atypes.begin() )->default_ammotype();
        if( !res.is_empty() ) {
            return res;
        }
    }
    return itype_id::NULL_ID();
}

itype_id item::common_ammo_default( bool conversion ) const
{
    if( !ammo_types( conversion ).empty() ) {
    for( const ammotype &at : ammo_types( conversion ) ) {
            const item *mag = magazine_current();
            if( mag && mag->type->magazine->type.contains( at ) ) {
                itype_id res = at->default_ammotype();
                if( !res.is_empty() ) {
                    return res;
                }
            }
        }
    }
    return itype_id::NULL_ID();
}

std::set<ammo_effect_str_id> item::ammo_effects( bool with_ammo ) const
{
    std::set<ammo_effect_str_id> res;

    if( !type->ammo_effects.empty() ) {
        res.insert( type->ammo_effects.begin(), type->ammo_effects.end() );
    }

    if( !is_gun() ) {
        return res;
    }

    res.insert( type->gun->ammo_effects.begin(), type->gun->ammo_effects.end() );
    if( with_ammo && ammo_data() ) {
        res.insert( ammo_data()->ammo->ammo_effects.begin(), ammo_data()->ammo->ammo_effects.end() );
    }

    for( const item *mod : gunmods() ) {
        res.insert( mod->type->gunmod->ammo_effects.begin(), mod->type->gunmod->ammo_effects.end() );
    }

    return res;
}

std::string item::ammo_sort_name() const
{
    if( is_magazine() || is_gun() || is_tool() ) {
    const std::set<ammotype> &types = ammo_types();
        if( !types.empty() ) {
            return ammotype( *types.begin() )->name();
        }
    }
    if( is_ammo() ) {
    return ammo_type()->name();
    }
    return "";
}

bool item::magazine_integral() const
{
    // If it has a default magazine, it can't have an integral magazine.
    if( magazine_default() ) {
    return false;
} else if( is_gun() ) {
    // We have an integral magazine if we're a gun with an ammo capacity (clip)
    return type->gun->clip;
} else if( is_tool() ) {
    // Or we are a tool with max_charges defined
    return type->tool->max_charges;
}
return true;
}

itype_id item::magazine_default( bool conversion ) const
{
    if( !ammo_types( conversion ).empty() ) {
    if( conversion ) {
            for( const item *m : is_gun() ? gunmods() : toolmods() ) {
                if( !m->type->mod->magazine_adaptor.empty() ) {
                    auto mags = m->type->mod->magazine_adaptor.find( ammotype( *ammo_types( conversion ).begin() ) );
                    if( mags != m->type->mod->magazine_adaptor.end() &&
                        !( *mags->second.begin() )->has_flag( flag_SPEEDLOADER ) ) {
                        return *( mags->second.begin() );
                    }
                }
            }
        }
        auto mag = type->magazine_default.find( ammotype( *ammo_types( conversion ).begin() ) );
        if( mag != type->magazine_default.end() && !mag->second->has_flag( flag_SPEEDLOADER ) ) {
            return mag->second;
        }
    }
    return itype_id::NULL_ID();
}

std::set<itype_id> item::magazine_compatible( bool conversion ) const
{
    std::set<itype_id> mags = {};
    // mods that define magazine_adaptor may override the items usual magazines
    const std::vector<const item *> &mods = is_gun() ? gunmods() : toolmods();
    for( const item *m : mods ) {
        if( !m->type->mod->magazine_adaptor.empty() ) {
            for( const ammotype &atype : ammo_types( conversion ) ) {
                if( m->type->mod->magazine_adaptor.contains( atype ) ) {
                    std::set<itype_id> magazines_for_atype = m->type->mod->magazine_adaptor.find( atype )->second;
                    mags.insert( magazines_for_atype.begin(), magazines_for_atype.end() );
                }
            }
            return mags;
        }
    }

    for( const ammotype &atype : ammo_types( conversion ) ) {
        if( type->magazines.contains( atype ) ) {
            std::set<itype_id> magazines_for_atype = type->magazines.find( atype )->second;
            mags.insert( magazines_for_atype.begin(), magazines_for_atype.end() );
        }
    }
    return mags;
}

item *item::magazine_current()
{
    return contents.get_item_with(
    []( const item & it ) {
        return it.is_magazine();
    } );
}

const item *item::magazine_current() const
{
    return const_cast<item *>( this )->magazine_current();
}

std::vector<item *> item::gunmods()
{
    return contents.gunmods();
}

std::vector<const item *> item::gunmods() const
{
    return contents.gunmods();
}

item *item::gunmod_find( const itype_id &mod )
{
    std::vector<item *> mods = gunmods();
    auto it = std::ranges::find_if( mods, [&mod]( item * e ) {
        return e->typeId() == mod;
    } );
    return it != mods.end() ? *it : nullptr;
}

const item *item::gunmod_find( const itype_id &mod ) const
{
    return const_cast<item *>( this )->gunmod_find( mod );
}

ret_val<bool> item::is_gunmod_compatible( const item &mod ) const
{
    if( !mod.is_gunmod() ) {
    debugmsg( "Tried checking compatibility of non-gunmod" );
        return ret_val<bool>::make_failure();
    }
    const islot_gunmod &g_mod = *mod.type->gunmod;

    if( !is_gun() ) {
    return ret_val<bool>::make_failure( _( "isn't a weapon" ) );

    } else if( is_gunmod() ) {
    return ret_val<bool>::make_failure( _( "is a gunmod and cannot be modded" ) );

    } else if( gunmod_find( mod.typeId() ) ) {
        return ret_val<bool>::make_failure( _( "already has a %s" ), mod.tname( 1 ) );

    } else if( !get_mod_locations().contains( g_mod.location ) ) {
    return ret_val<bool>::make_failure( _( "doesn't have a slot for this mod" ) );

    } else if( get_free_mod_locations( g_mod.location ) <= 0 ) {
    return ret_val<bool>::make_failure( _( "doesn't have enough room for another %s mod" ),
                                        mod.type->gunmod->location.name() );

    } else if( !g_mod.usable.empty() || !g_mod.usable_category.empty() || !g_mod.exclusion.empty() ||
               !g_mod.exclusion_category.empty() ) {
    // First check that it's not explicitly excluded by id.
    bool excluded = g_mod.exclusion.contains( this->typeId() );
        // Then check if it's excluded by category.
        for( const std::unordered_set<weapon_category_id> &mod_cat : g_mod.exclusion_category ) {
            if( excluded ) {
                break;
            }
            if( std::ranges::all_of( mod_cat, [this]( const weapon_category_id & wcid ) {
            return this->type->weapon_category.count( wcid );
            } ) ) {
                excluded = true;
            }
        }

        // Check that it's included by id, if so, override banned so it's allowed.
        // A check is already in item_factory so that explicit inclusion and exclusion of the same id throws errors.
        bool usable = g_mod.usable.contains( this->typeId() );
        if( usable ) {
            excluded = false;
        }
        // Then check that it's included by category. If banned is still true, skip, no point checking.
        for( const std::unordered_set<weapon_category_id> &mod_cat : g_mod.usable_category ) {
            if( usable || excluded ) {
                break;
            }
            if( std::ranges::all_of( mod_cat, [this]( const weapon_category_id & wcid ) {
            return this->type->weapon_category.count( wcid );
            } ) ) {
                usable = true;
            }
        }
        if( !usable || excluded ) {
            return ret_val<bool>::make_failure( _( "cannot have a %s" ), mod.tname() );
        }

    } else if( g_mod.location.str() == "underbarrel" &&
               !mod.has_flag( flag_PUMP_RAIL_COMPATIBLE ) && has_flag( flag_PUMP_ACTION ) ) {
    return ret_val<bool>::make_failure( _( "can only accept small mods on that slot" ) );

    } else if( !mod.type->mod->acceptable_ammo.empty() ) {
    bool compat_ammo = false;
    for( const ammotype &at : mod.type->mod->acceptable_ammo ) {
            if( ammo_types( false ).contains( at ) ) {
                compat_ammo = true;
            }
        }
        if( !compat_ammo ) {
            return ret_val<bool>::make_failure(
                       _( "%1$s cannot be used on item with no compatible ammo types" ), mod.tname( 1 ) );
        }
    } else if( mod.typeId() == itype_waterproof_gunmod && has_flag( flag_WATERPROOF_GUN ) ) {
    return ret_val<bool>::make_failure( _( "is already waterproof" ) );

    } else if( mod.typeId() == itype_tuned_mechanism && has_flag( flag_NEVER_JAMS ) ) {
    return ret_val<bool>::make_failure( _( "is already eminently reliable" ) );

    } else if( mod.has_flag( flag_BRASS_CATCHER ) && has_flag( flag_RELOAD_EJECT ) ) {
    return ret_val<bool>::make_failure( _( "cannot have a brass catcher" ) );

    } else if( ( !mod.type->mod->ammo_modifier.empty() || !mod.type->mod->magazine_adaptor.empty() )
                   && ( ammo_remaining() > 0 || magazine_current() ) ) {
        return ret_val<bool>::make_failure( _( "must be unloaded before installing this mod" ) );
    }

for( const gunmod_location &slot : mod.type->gunmod->blacklist_mod ) {
    if( get_mod_locations().contains( slot ) ) {
            return ret_val<bool>::make_failure( _( "cannot be installed on a weapon with \"%s\"" ),
                                                slot.name() );
        }
    }

    return ret_val<bool>::make_success();
}

std::map<gun_mode_id, gun_mode> item::gun_all_modes() const
{
    std::map<gun_mode_id, gun_mode> res;

    if( !is_gun() || is_gunmod() ) {
        return res;
    }

    std::vector<const item *> opts = gunmods();
    opts.push_back( this );

    for( const item *e : opts ) {

        // handle base item plus any auxiliary gunmods
        if( e->is_gun() ) {
            for( const std::pair<const gun_mode_id, gun_modifier_data> &m : e->type->gun->modes ) {
                // prefix attached gunmods, e.g. M203_DEFAULT to avoid index key collisions
                std::string prefix = e->is_gunmod() ? ( std::string( e->typeId() ) += "_" ) : "";
                std::ranges::transform( prefix, prefix.begin(),
                                        static_cast<int( * )( int )>( toupper ) );

                const int qty = m.second.qty();

                res.emplace( gun_mode_id( prefix + m.first.str() ), gun_mode( m.second.name(),
                             const_cast<item *>( e ),
                             qty, m.second.flags() ) );
            }

            // non-auxiliary gunmods may provide additional modes for the base item
        } else if( e->is_gunmod() ) {
            for( const std::pair<const gun_mode_id, gun_modifier_data> &m : e->type->gunmod->mode_modifier ) {
                //checks for melee gunmod, points to gunmod
                if( m.first == gun_mode_REACH ) {
                    res.emplace( m.first, gun_mode { m.second.name(), const_cast<item *>( e ),
                                                     m.second.qty(), m.second.flags() } );
                    //otherwise points to the parent gun, not the gunmod
                } else {
                    res.emplace( m.first, gun_mode { m.second.name(), const_cast<item *>( this ),
                                                     m.second.qty(), m.second.flags() } );
                }
            }
        }
    }

    return res;
}

gun_mode item::gun_get_mode( const gun_mode_id &mode ) const
{
    if( is_gun() ) {
    for( const std::pair<const gun_mode_id, gun_mode> &e : gun_all_modes() ) {
            if( e.first == mode ) {
                return e.second;
            }
        }
    }
    return gun_mode();
}

gun_mode item::gun_current_mode() const
{
    return gun_get_mode( gun_get_mode_id() );
}

gun_mode_id item::gun_get_mode_id() const
{
    if( !is_gun() || is_gunmod() ) {
    return gun_mode_id();
    }
    return gun_mode_id( get_var( GUN_MODE_VAR_NAME, "DEFAULT" ) );
}

bool item::gun_set_mode( const gun_mode_id &mode )
{
    if( !is_gun() || is_gunmod() || !gun_all_modes().contains( mode ) ) {
        return false;
    }
    set_var( GUN_MODE_VAR_NAME, mode.str() );
    return true;
}

void item::gun_cycle_mode()
{
    if( !is_gun() || is_gunmod() ) {
        return;
    }

    const gun_mode_id cur = gun_get_mode_id();
    const std::map<gun_mode_id, gun_mode> modes = gun_all_modes();

    const auto current_mode = std::ranges::find( modes, cur, []( const auto & pair ) {
        return pair.first;
    } );
    if( current_mode != modes.end() ) {
        const auto next_mode = std::next( current_mode );
        if( next_mode != modes.end() ) {
            gun_set_mode( next_mode->first );
            return;
        }
    }
    gun_set_mode( modes.begin()->first );

    return;
}

std::map<gunmod_location, int> item::get_mod_locations() const
{
    std::map<gunmod_location, int> mod_locations = type->gun->valid_mod_locations;

    for( const item * mod : gunmods() ) {
        if( !mod->type->gunmod->add_mod.empty() ) {
            std::map<gunmod_location, int> add_locations = mod->type->gunmod->add_mod;

            for( const std::pair<const gunmod_location, int> &add_location : add_locations ) {
                mod_locations[add_location.first] += add_location.second;
            }
        }
    }

    return mod_locations;
}

int item::get_free_mod_locations( const gunmod_location& location ) const
{
    if( !is_gun() ) { return 0; }

std::map<gunmod_location, int> mod_locations = get_mod_locations();

const auto loc = mod_locations.find( location );
if( loc == mod_locations.end() ) { return 0; }
int result = loc->second;
for( const item * elem : contents.all_items_top() ) {
    const cata::value_ptr<islot_gunmod> &mod = elem->type->gunmod;
    if( mod && mod->location == location ) { result--; }
    }
    return result;
}

int item::get_gun_ups_drain() const
{
    int draincount = 0;
    if( type->gun ) {
        int modifier = 0;
        float multiplier = 1.0f;
        for( const item * mod : gunmods() ) {
            modifier += mod->type->gunmod->ups_charges_modifier;
            multiplier *= mod->type->gunmod->ups_charges_multiplier;
        }
        draincount = ( type->gun->ups_charges * multiplier ) + modifier;
    }
    return draincount;
}
