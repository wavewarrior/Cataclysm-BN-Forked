// Item type/damage-state queries: chip resistance, damage color/symbol, material
// queries, and is_* type predicates — split out of item.cpp. .cpp-only, no API changes.

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
#include "light_emission.h"
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

// File-scope id constants (moved with methods; internal linkage).
static const item_category_id itemcat_maps( "maps" );
static const itype_id itype_water( "water" );
static const itype_id itype_water_acid( "water_acid" );
static const itype_id itype_water_acid_weak( "water_acid_weak" );

int item::chip_resistance( bool worst ) const
{
    int res = worst ? INT_MAX : INT_MIN;
    for( const material_type *mat : made_of_types() ) {
        const int val = mat->chip_resist();
        res = worst ? std::min( res, val ) : std::max( res, val );
    }

    if( res == INT_MAX || res == INT_MIN ) {
        return 2;
    }

    if( res <= 0 ) {
        return 0;
    }

    return res;
}

std::optional<resistances> item::damage_resistance_override() const
{
    if( is_null() || !type->armor ) {
    return std::optional<resistances>();
    }

    return type->armor->resistance;
}

int item::min_damage() const
{
    return type->damage_min();
}

int item::max_damage() const
{
    return type->damage_max();
}

float item::get_relative_health() const
{
    return ( max_damage() + 1.0f - damage() ) / ( max_damage() + 1.0f );
}

bool item::mod_damage( int qty, damage_type dt )
{
    bool destroy = false;

    if( count_by_charges() ) {
        charges -= std::min( type->stack_size * qty / itype::damage_scale, charges );
        destroy |= charges == 0;
    }

    if( qty > 0 ) {
        on_damage( qty, dt );
    }

    if( !count_by_charges() ) {
        destroy |= damage_ + qty > max_damage();

        damage_ = std::max( std::min( damage_ + qty, max_damage() ), min_damage() );
    }

    if( destroy && type->iequippable_callbacks ) {
        type->iequippable_callbacks->call_on_break( get_avatar(), *this );
    }

    return destroy;
}

bool item::mod_damage( const int qty )
{
    return mod_damage( qty, DT_NULL );
}

bool item::inc_damage( const damage_type dt )
{
    return mod_damage( itype::damage_scale, dt );
}

bool item::inc_damage()
{
    return inc_damage( DT_NULL );
}

nc_color item::damage_color() const
{
    // TODO: unify with veh_interact::countDurability
    switch( damage_level( 4 ) ) {
    default:
        // reinforced
        if( damage() <= min_damage() ) {
                // fully reinforced
                return c_green;
            } else {
                return c_light_green;
            }
        case 0:
            return c_light_green;
        case 1:
            return c_yellow;
        case 2:
            return c_magenta;
        case 3:
            return c_light_red;
        case 4:
            if( damage() >= max_damage() ) {
                return c_dark_gray;
            } else {
                return c_red;
            }
    }
}

std::string item::damage_symbol() const
{
    switch( damage_level( 4 ) ) {
    default:
        // reinforced
        return _( R"(++)" );
        case 0:
            return _( R"(||)" );
        case 1:
            return _( R"(|\)" );
        case 2:
            return _( R"(|.)" );
        case 3:
            return _( R"(\.)" );
        case 4:
            if( damage() >= max_damage() ) {
                return _( R"(XX)" );
            } else {
                return _( R"(..)" );
            }

    }
}

std::string item::durability_indicator( bool include_intact ) const
{
    std::string outputstring;

    if( damage() < 0 )  {
        if( get_option<bool>( "ITEM_HEALTH_BAR" ) ) {
            outputstring = colorize( damage_symbol() + "\u00A0", damage_color() );
        } else if( is_gun() ) {
            outputstring = pgettext( "damage adjective", "accurized " );
        } else {
            outputstring = pgettext( "damage adjective", "reinforced " );
        }
    } else if( has_flag( flag_CORPSE ) ) {
        if( damage() > 0 ) {
            switch( damage_level( 4 ) ) {
                case 1:
                    outputstring = pgettext( "damage adjective", "bruised " );
                    break;
                case 2:
                    outputstring = pgettext( "damage adjective", "damaged " );
                    break;
                case 3:
                    outputstring = pgettext( "damage adjective", "mangled " );
                    break;
                default:
                    outputstring = pgettext( "damage adjective", "pulped " );
                    break;
            }
        }
    } else if( get_option<bool>( "ITEM_HEALTH_BAR" ) ) {
        outputstring = colorize( damage_symbol() + "\u00A0", damage_color() );
    } else {
        outputstring = string_format( "%s ", get_base_material().dmg_adj( damage_level( 4 ) ) );
        if( include_intact && outputstring == " " ) {
            outputstring = _( "fully intact " );
        }
    }

    return  outputstring;
}

const std::set<itype_id> &item::repaired_with() const
{
    static std::set<itype_id> no_repair;
    return has_flag( flag_NO_REPAIR )  ? no_repair : type->repair;
}

void item::mitigate_damage( damage_unit &du ) const
{
    const resistances res = resistances( *this );
    const float mitigation = res.get_effective_resist( du );
    // get_effective_resist subtracts the flat penetration value before multiplying the remaining armor.
    // therefore, res_pen is reduced by the full value of the item's armor value even though mitigation might be smaller (such as an attack with a 0.5 armor multiplier)
    du.res_pen = std::max( 0.0f, du.res_pen - res.type_resist( du.type ) );
    du.amount = std::max( 0.0f, du.amount - mitigation );
}


bool item::is_two_handed( const Character &guy ) const
{
    // Big weapons are always two-handed if you're Medium or smaller.
    if( has_flag( flag_ALWAYS_TWOHAND ) && guy.get_size() <= creature_size::medium ) {
        return true;
    }
    // Large characters get a penalty when trying to wield two-handed weapons, Huge characters treat them like normal.
    const float str_factor = has_flag( flag_ALWAYS_TWOHAND ) &&
                             guy.get_size() != creature_size::huge ?  2.0f : 4.0f;
    ///\EFFECT_STR determines which weapons can be wielded with one hand
    return ( ( weight() / 113_gram ) > guy.str_cur * str_factor );
}

const std::vector<material_id> &item::made_of() const
{
    if( is_corpse() ) {
    return corpse->mat;
}
return type->materials;
}

const std::map<quality_id, int> &item::quality_of() const
{
    return type->qualities;
}

std::vector<const material_type *> item::made_of_types() const
{
    std::vector<const material_type *> material_types_composed_of;
    for( const material_id &mat_id : made_of() ) {
        material_types_composed_of.push_back( &mat_id.obj() );
    }
    return material_types_composed_of;
}

bool item::made_of_any( const std::set<material_id> &mat_idents ) const
{
    const std::vector<material_id> &mats = made_of();
    if( mats.empty() ) {
        return false;
    }

    return std::ranges::any_of( mats, [&mat_idents]( const material_id & e ) {
        return mat_idents.count( e );
    } );
}

bool item::only_made_of( const std::set<material_id> &mat_idents ) const
{
    const std::vector<material_id> &mats = made_of();
    if( mats.empty() ) {
        return false;
    }

    return std::ranges::all_of( mats, [&mat_idents]( const material_id & e ) {
        return mat_idents.count( e );
    } );
}

bool item::made_of( const material_id &mat_ident ) const
{
    const std::vector<material_id> &materials = made_of();
    return std::ranges::contains( materials, mat_ident );
}

bool item::contents_made_of( const phase_id phase ) const
{
    return !contents.empty() && contents.front().made_of( phase );
}

bool item::contents_normally_made_of( const phase_id phase ) const
{
    return !contents.empty() && contents.front().type->phase == phase;
}

bool item::made_of( phase_id phase ) const
{
    if( is_null() ) {
    return false;
}
return type->phase == phase;
}

bool item::conductive() const
{
    if( is_null() ) {
    return false;
}

if( has_flag( flag_CONDUCTIVE ) ) {
    return true;
}

if( has_flag( flag_NONCONDUCTIVE ) ) {
    return false;
}

// If any material has electricity resistance equal to or lower than flesh (1) we are conductive.
const std::vector<const material_type *> &mats = made_of_types();
return std::ranges::any_of( mats, []( const material_type * mt ) {
    return mt->elec_resist() <= 1;
    } );
}

bool item::reinforceable() const
{
    if( is_null() || has_flag( flag_NO_REPAIR ) ) {
    return false;
}

// If a material is reinforceable, so are we
const std::vector<const material_type *> &mats = made_of_types();
return std::ranges::any_of( mats, []( const material_type * mt ) {
    return mt->reinforces();
    } );
}

bool item::destroyed_at_zero_charges() const
{
    return ( is_ammo() || is_food() );
}

bool item::is_gun() const
{
    return !!type->gun;
}

bool item::is_firearm() const
{
    return is_gun() && !has_flag( flag_PRIMITIVE_RANGED_WEAPON );
}

int item::get_reload_time() const
{
    if( !is_gun() && !is_magazine() ) {
    return 0;
}

int reload_time = is_gun() ? type->gun->reload_time : type->magazine->reload_time;
for( const item *mod : gunmods() ) {
    reload_time = ( reload_time * ( 100 + mod->type->gunmod->reload_modifier ) / 100 );
    }

    return reload_time;
}

bool item::is_silent() const
{
    // Most guns with a suppressor installed will be under this value, also see item::gun_noise in ranged.cpp
    return gun_noise().volume < 50;
}

bool item::is_gunmod() const
{
    return !!type->gunmod;
}

bool item::is_bionic() const
{
    return !!type->bionic;
}

bool item::is_magazine() const
{
    return !!type->magazine;
}

bool item::is_battery() const
{
    return !!type->battery;
}

bool item::is_ammo_belt() const
{
    return is_magazine() && has_flag( flag_MAG_BELT );
}

bool item::is_bandolier() const
{
    return type->can_use( "bandolier" );
}

bool item::is_holster() const
{
    return type->can_use( "holster" );
}

bool item::is_ammo() const
{
    return !!type->ammo;
}

bool item::is_comestible() const
{
    return !!get_comestible();
}

bool item::is_food() const
{
    return is_comestible() && ( get_comestible()->comesttype == "FOOD" ||
                                get_comestible()->comesttype == "DRINK" );
}

bool item::is_medication() const
{
    return is_comestible() && get_comestible()->comesttype == "MED";
}

bool item::is_brewable() const
{
    return !!type->brewable;
}

bool item::is_food_container() const
{
    return ( !contents.empty() && contents.front().is_food() ) ||
    ( is_craft() &&
    craft_data_->making->create_result()->is_food_container() );
}

bool item::is_med_container() const
{
    return !contents.empty() && contents.front().is_medication();
}

bool item::is_corpse() const
{
    return corpse != nullptr && has_flag( flag_CORPSE );
}

const mtype *item::get_mtype() const
{
    return corpse;
}

template<typename Item>
static Item *get_food_impl( Item *it )
{
    if( it->is_food() ) {
        return it;
    } else if( it->is_food_container() && !it->contents.empty() ) {
        return &it->contents.front();
    } else {
        return nullptr;
    }
}

item *item::get_food()
{
    return get_food_impl( this );
}

const item *item::get_food() const
{
    return get_food_impl( this );
}

void item::set_mtype( const mtype *const m )
{
    // This is potentially dangerous, e.g. for corpse items, which *must* have a valid mtype pointer.
    if( m == nullptr ) {
        debugmsg( "setting item::corpse of %s to NULL", tname() );
        return;
    }
    corpse = m;
}

bool item::is_ammo_container() const
{
    return !is_magazine() && !contents.empty() && contents.front().is_ammo();
}

bool item::is_melee() const
{
    for( int idx = DT_NULL + 1; idx != NUM_DT; ++idx ) {
        if( is_melee( static_cast<damage_type>( idx ) ) ) {
            return true;
        }
    }
    return false;
}

bool item::is_melee( damage_type dt ) const
{
    return damage_melee( dt ) > MELEE_STAT;
}

const islot_armor *item::find_armor_data() const
{
    if( type->armor ) {
    return &*type->armor;
}
// Currently the only way to make a non-armor item into armor is to install a gun mod.
// The gunmods are stored in the items contents, as are the contents of a container, and the
// tools in a tool belt (a container actually), or the ammo in a quiver (container again).
for( const item *mod : gunmods() ) {
        if( mod->type->armor ) {
            return &*mod->type->armor;
        }
    }
    return nullptr;
}

bool item::is_pet_armor( bool on_pet ) const
{
    bool is_worn = on_pet && !get_var( "pet_armor", "" ).empty();
    return has_flag( flag_IS_PET_ARMOR ) && ( is_worn || !on_pet );
}

bool item::is_armor() const
{
    return find_armor_data() != nullptr || has_flag( flag_IS_ARMOR );
}

bool item::is_book() const
{
    return !!type->book;
}

bool item::is_map() const
{
    return get_category().get_id() == itemcat_maps;
}

bool item::is_container() const
{
    return !!type->container;
}

bool item::is_watertight_container() const
{
    return type->container && type->container->watertight && type->container->seals;
}

bool item::is_non_resealable_container() const
{
    return type->container && !type->container->seals && type->container->unseals_into;
}

bool item::is_in_container() const
{
    auto location = static_cast<item_location *>( &*loc );
    return location->where() == item_location_type::container || ( parent_item() &&
            parent_item()->is_container() );
}

bool item::is_bucket() const
{
    // That "preserves" part is a hack:
    // Currently all non-empty cans are effectively sealed at all times
    // Making them buckets would cause weirdness
    return type->container &&
           type->container->watertight &&
           !type->container->seals &&
           !type->container->unseals_into;
}

bool item::is_bucket_nonempty() const
{
    return is_bucket() && !is_container_empty();
}

bool item::is_engine() const
{
    return !!type->engine;
}

bool item::is_wheel() const
{
    return !!type->wheel;
}

bool item::is_fuel() const
{
    return !!type->fuel;
}

bool item::is_toolmod() const
{
    return !is_gunmod() && type->mod;
}

bool item::is_faulty() const
{
    return is_engine() ? !faults.empty() : false;
}

bool item::is_irremovable() const
{
    return has_flag( flag_IRREMOVABLE );
}

int item::wind_resist() const
{
    std::vector<const material_type *> materials = made_of_types();
    if( materials.empty() ) {
        debugmsg( "Called item::wind_resist on an item (%s [%s]) made of nothing!", tname(), typeId() );
        return 99;
    }

    int best = -1;
    for( const material_type *mat : materials ) {
        std::optional<int> resistance = mat->wind_resist();
        if( resistance && *resistance > best ) {
            best = *resistance;
        }
    }

    // Default to 99% effective
    if( best == -1 ) {
        return 99;
    }

    return best;
}

std::set<fault_id> item::faults_potential() const
{
    std::set<fault_id> res;
    res.insert( type->faults.begin(), type->faults.end() );
    return res;
}

int item::wheel_area() const
{
    return is_wheel() ? type->wheel->diameter * type->wheel->width : 0;
}

float item::fuel_energy() const
{
    return is_fuel() ? type->fuel->energy : 0.0f;
}

ter_id item::fuel_pump_terrain() const
{
    return is_fuel() ? type->fuel->pump_terrain : t_null;
}

bool item::has_explosion_data() const
{
    return is_fuel() ? type->fuel->has_explode_data : false;
}

struct fuel_explosion item::get_explosion_data()
{
    static struct fuel_explosion null_data;
    return has_explosion_data() ? type->fuel->explosion_data : null_data;
}

float item::get_kcal_mult() const
{
    return get_var( "kcal_mult", 1.0 );
}
void item::set_kcal_mult( float val )
{
    set_var( "kcal_mult", val );
}
bool item::is_container_empty() const
{
    return contents.empty();
}

bool item::is_container_full( bool allow_bucket ) const
{
    if( is_container_empty() ) {
    return false;
}
if( is_watertight_container() ) {
    return get_remaining_capacity_for_liquid( contents.front(), allow_bucket ) == 0;
    } else if( !is_reloadable_with( contents.front().typeId() ) ) {
        return true;
    } else {
        int ammo = contents.front().charges_per_volume( get_container_capacity() ) -
                   contents.front().charges;
        return ammo <= 0;
    }
}

bool item::can_unload_liquid() const
{
    if( is_container_empty() ) {
    return true;
}

const item &cts = contents.front();
bool cts_is_frozen_liquid = cts.made_of( LIQUID ) && cts.made_of( SOLID );
return is_bucket() || !cts_is_frozen_liquid;
}

bool item::can_reload_with( const ammotype &ammo ) const
{
    return is_reloadable_helper( ammo->default_ammotype(), false );
}

bool item::can_reload_with( const itype_id &ammo ) const
{
    return is_reloadable_helper( ammo, false );
}

bool item::is_reloadable_with( const itype_id &ammo ) const
{
    return is_reloadable_helper( ammo, true );
}

bool item::is_reloadable_helper( const itype_id &ammo, bool now ) const
{
    // empty ammo is passed for listing possible ammo apparently, so it needs to return true.
    if( !is_reloadable() ) {
    return false;
} else if( is_watertight_container() ) {
    if( ammo.is_empty() ) {
            return now ? !is_container_full() : true;
        } else {
            return now ? ( is_container_empty() || contents.front().typeId() == ammo ) : true;
        }
    } else if( is_container() ) {
    if( ammo.is_empty() ) {
            return now ? !is_container_full() : true;
        } else if( ammo->phase == LIQUID ) {
            return false;
        } else {
            return now ? ( is_container_empty() || contents.front().typeId() == ammo ) : true;
        }
    } else if( magazine_integral() ) {
    if( !ammo.is_empty() ) {
            if( now && ammo_data() ) {
                if( ammo_current() != ammo ) {
                    return false;
                }
            } else {
                const itype *at = &*ammo;
                if( ( !at->ammo || !ammo_types().contains( at->ammo->type ) ) &&
                    !magazine_compatible().contains( ammo ) ) {
                    return false;
                }
            }
        }
        return now ? ( ammo_remaining() < ammo_capacity() ) : true;
    } else {
        return ammo.is_empty() ? true : magazine_compatible().count( ammo );
    }
}

bool item::is_craft() const
{
    return craft_data_ != nullptr;
}

bool item::is_pocket_dimension_key() const
{
    return pocket_dim.has_value();
}

bool item::is_funnel_container( units::volume &bigger_than ) const
{
    if( !is_bucket() && !is_watertight_container() ) {
    return false;
}
// TODO: consider linking funnel to item or -making- it an active item
if( get_container_capacity() <= bigger_than ) {
    return false; // skip contents check, performance
}
if(
        contents.empty() ||
        contents.front().typeId() == itype_water ||
        contents.front().typeId() == itype_water_acid ||
        contents.front().typeId() == itype_water_acid_weak ) {
    bigger_than = get_container_capacity();
        return true;
    }
    return false;
}

bool item::is_emissive() const
{
    return light.luminance > 0 || type->light_emission > 0;
}

bool item::is_deployable() const
{
    return type->can_use( "deploy_furn" );
}

bool item::is_tool() const
{
    return !!type->tool;
}

bool item::is_transformable() const
{
    return type->use_methods.contains( "transform" );
}

bool item::is_artifact() const
{
    return !!type->artifact;
}

bool item::is_relic() const
{
    return !!relic_data;
}

const std::vector<enchantment> &item::get_enchantments() const
{
    if( !is_relic() ) {
    static const std::vector<enchantment> fallback;
    return fallback;
}
return relic_data->get_enchantments();
}

const material_type &item::get_random_material() const
{
    return random_entry( made_of(), material_id::NULL_ID() ).obj();
}

const material_type &item::get_base_material() const
{
    const std::vector<material_id> &mats = made_of();
    return mats.empty() ? material_id::NULL_ID().obj() : mats.front().obj();
}

bool item::operator<( const item& other ) const
{
    const item_category& cat_a = get_category();
    const item_category& cat_b = other.get_category();
    if( cat_a != cat_b ) {
        return cat_a < cat_b;
    } else {
        const item* me = is_container() && !contents.empty() ? &contents.front() : this;
        const item* rhs =
            other.is_container() && !other.contents.empty() ? &other.contents.front() : &other;

        const itype* me_type = me->type;
        const itype* rhs_type = rhs->type;
        if( !me_type || !rhs_type ) { return !!me_type; }

        if( me_type->get_id() == rhs_type->get_id() ) {
            if( me->is_money() ) { return me->charges > rhs->charges; }
            return me->charges < rhs->charges;
        } else {
            std::string n1 = me_type->nname( 1 );
            std::string n2 = rhs_type->nname( 1 );
            return localized_compare( n1, n2 );
        }
    }
}


const std::string &item::get_category_id() const
{
    if( is_container() && !contents.empty() ) {
    return contents.front().get_category().get_id().str();
    }

    static item_category null_category;
    return type->category_force.is_valid() ? type->category_force.obj().get_id().str() :
           null_category.get_id().str();
}

const item_category &item::get_category() const
{
    if( is_container() && !contents.empty() ) {
    return contents.front().get_category();
    }

    static item_category null_category;
    return type->category_force.is_valid() ? type->category_force.obj() : null_category;
}
