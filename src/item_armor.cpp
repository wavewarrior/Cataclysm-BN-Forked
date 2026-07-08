// Item armor/clothing property queries: encumbrance, coverage, warmth, storage
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

// File-scope constant for clothing mod variable names
static const std::string CLOTHING_MOD_VAR_PREFIX( "clothing_mod_" );


units::volume item::get_storage() const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return is_pet_armor() ? type->pet_armor->storage : 0_ml;
    }
    units::volume storage = armor->storage;
    float mod = get_clothing_mod_val( clothing_mod_type_storage );
    storage += std::lround( mod ) * units::legacy_volume_factor;

    return storage;
}

float item::get_weight_capacity_modifier() const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return 1;
    }
    return armor->weight_capacity_modifier;
}

units::mass item::get_weight_capacity_bonus() const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return 0_gram;
    }
    return armor->weight_capacity_bonus;
}

int item::get_env_resist( int override_base_resist ) const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return is_pet_armor() ? type->pet_armor->env_resist : 0;
    }
    // modify if item is a gas mask and has filter
    int resist_base = armor->env_resist;
    int resist_filter = get_var( "overwrite_env_resist", 0 );
    int resist = std::max( { resist_base, resist_filter, override_base_resist } );

    return std::lround( resist * get_relative_health() );
}

int item::get_base_env_resist_w_filter() const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return is_pet_armor() ? type->pet_armor->env_resist_w_filter : 0;
    }
    return armor->env_resist_w_filter;
}

bool item::is_power_armor() const
{
    return ( has_flag( flag_POWERARMOR_EXO ) || has_flag( flag_POWERARMOR_EXTERNAL ) ||
    has_flag( flag_POWERARMOR_MOD ) );
}

int item::get_avg_encumber( const Character &who ) const
{
    const islot_armor *armor = find_armor_data();
    if( !armor ) {
        // handle wearable guns (e.g. shoulder strap) as special case
        return is_gun() ? volume() / 750_ml : 0;
    }

    int avg_encumber = 0;
    int avg_ctr = 0;

    for( const armor_portion_data &entry : armor->data ) {
        for( const bodypart_str_id &limb : entry.covers ) {
            int encumber = get_encumber( who, limb.id() );
            if( encumber ) {
                avg_encumber += encumber;
                ++avg_ctr;
            }
        }
    }
    if( avg_encumber == 0 ) {
        return 0;
    } else {
        return avg_encumber / avg_ctr;
    }
}

int item::get_encumber( const Character &who, const bodypart_id &bodypart ) const
{

    units::volume contents_volume( 0_ml );

    contents_volume += contents.item_size_modifier();

    if( who.is_worn( *this ) ) {
        const islot_armor *armor = find_armor_data();

        if( armor != nullptr ) {
            for( const armor_portion_data &entry : armor->data ) {
                if( entry.covers.test( bodypart.id() ) ) {
                    if( entry.max_encumber != 0 ) {
                        units::volume char_storage( 0_ml );

                        for( const item * const &e : who.worn ) {
                            char_storage += e->get_storage();
                        }

                        if( char_storage != 0_ml ) {
                            // Cast up to 64 to prevent overflow. Dividing before would prevent this but lose data.
                            contents_volume += units::from_milliliter( static_cast<int64_t>( armor->storage.value() ) *
                                               who.inv_volume().value() / char_storage.value() );
                        }
                    }
                }
            }
        }
    }

    return get_encumber_when_containing( who, contents_volume, bodypart );
}

int item::get_encumber_when_containing(
    const Character &who, const units::volume &contents_volume, const bodypart_id &bodypart ) const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        // handle wearable guns (e.g. shoulder strap) as special case
        return is_gun() ? volume() / 750_ml : 0;
    }

    int encumber = 0;

    for( const armor_portion_data &entry : armor->data ) {
        if( entry.covers.test( bodypart.id() ) ) {
            encumber = entry.encumber;
            // Non-rigid items add additional encumbrance proportional to their volume
            bool any_encumb_increase = std::ranges::any_of( armor->data,
            []( armor_portion_data data ) {
                return data.encumber != data.max_encumber;
            } );
            if( !type->rigid || any_encumb_increase ) {
                const int capacity = get_total_capacity().value();
                if( entry.max_encumber == 0 ) {
                    encumber += contents_volume / 500_ml;
                } else {
                    if( capacity <= 0 ) {
                        debugmsg( "Non-rigid item (%s) without storage capacity.", tname() );
                    } else {
                        // Cast up to 64 to prevent overflow. Dividing before would prevent this but lose data.
                        encumber += static_cast<int64_t>( entry.max_encumber - entry.encumber ) * contents_volume.value() /
                                    capacity;
                    }
                }
            }
        }
    }

    // Fit checked before changes, fitting shouldn't reduce penalties from patching.
    if( has_flag( flag_FIT ) && has_flag( flag_VARSIZE ) ) {
        encumber = std::max( encumber / 2, encumber - 10 );
    }

    // TODO: Should probably have sizing affect coverage
    const sizing sizing_level = get_sizing( who );
    switch( sizing_level ) {
        case sizing::small_sized_human_char:
        case sizing::small_sized_big_char:
            // non small characters have a HARD time wearing undersized clothing
            encumber *= 3;
            break;
        case sizing::human_sized_small_char:
        case sizing::big_sized_small_char:
            // clothes bag up around smol characters and encumber them more
            encumber *= 2;
            break;
        default:
            break;
    }
    encumber += std::lround( get_clothing_mod_val( clothing_mod_type_encumbrance ) );

    return encumber;
}

layer_level item::get_layer() const
{
    if( type->armor ) {
    // We assume that an item will never have per-item flags defining its
    // layer, so we can defer to the itype.
    return type->layer;
}

if( has_flag( flag_PERSONAL ) ) {
        return PERSONAL_LAYER;
    } else if( has_flag( flag_SKINTIGHT ) ) {
        return UNDERWEAR_LAYER;
    } else if( has_flag( flag_WAIST ) ) {
        return WAIST_LAYER;
    } else if( has_flag( flag_OUTER ) ) {
        return OUTER_LAYER;
    } else if( has_flag( flag_BELTED ) ) {
        return BELTED_LAYER;
    } else if( has_flag( flag_AURA ) ) {
        return AURA_LAYER;
    } else {
        return REGULAR_LAYER;
    }
}

int item::get_avg_coverage() const
{
    const islot_armor *armor = find_armor_data();
    if( !armor ) {
        // handle wearable guns (e.g. shoulder strap) as special case
        return is_gun() ? std::min( volume() / 500_ml, 100 ) : 0;
    }
    int avg_coverage = 0;
    int avg_ctr = 0;
    for( const armor_portion_data &entry : armor->data ) {
        for( const bodypart_str_id &limb : entry.covers ) {
            int coverage = get_coverage( limb );
            if( coverage ) {
                avg_coverage += coverage;
                ++avg_ctr;
            }
        }
    }
    if( avg_coverage == 0 ) {
        return 0;
    } else {
        avg_coverage /= avg_ctr;
        return avg_coverage;
    }
}

int item::get_coverage( const bodypart_id &bodypart ) const
{
    if( std::optional<armor_portion_data> portion_data = portion_for_bodypart( bodypart ) ) {
        return portion_data->coverage;
    }
    return 0;
}

std::optional<armor_portion_data> item::portion_for_bodypart( const bodypart_id &bodypart ) const
{
    const islot_armor *armor = find_armor_data();
    if( !armor ) {
        return std::optional<armor_portion_data>();
    }
    for( const armor_portion_data &entry : armor->data ) {
        if( entry.covers.test( bodypart.id() ) ) {
            return entry;
        }
    }
    return std::optional<armor_portion_data>();
}

int item::get_thickness() const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return is_pet_armor() ? type->pet_armor->thickness : 0;
    }
    return armor->thickness;
}

int item::get_warmth() const
{
    const islot_armor *armor = find_armor_data();
    if( armor == nullptr ) {
        return 0;
    }
    int result = armor->warmth;

    result += get_clothing_mod_val( clothing_mod_type_warmth );

    return result;
}

units::volume item::get_pet_armor_max_vol() const
{
    return is_pet_armor() ? type->pet_armor->max_vol : 0_ml;
}

units::volume item::get_pet_armor_min_vol() const
{
    return is_pet_armor() ? type->pet_armor->min_vol : 0_ml;
}

std::string item::get_pet_armor_bodytype() const
{
    return is_pet_armor() ? type->pet_armor->bodytype : "";
}
#if defined(_MSC_VER)
// Deal with MSVC compiler bug (#17791, #17958)
#pragma optimize("", off)
#endif
template <typename ResistGetter>
static int phys_resist(
    const item& it, damage_type dt, clothing_mod_type cmt, ResistGetter resist_getter,
    bool to_self )
{
    if( it.is_null() ) { return 0; }

    float resist = 0;
    int eff_thickness = 1;

    // base resistance
    // Don't give reinforced items +armor, just more resistance to ripping
    const int dmg = it.damage_level( 4 );
    const int eff_damage = to_self ? std::min( dmg, 0 ) : std::max( dmg, 0 );
    eff_thickness = std::max( 1, it.get_thickness() - eff_damage );

    float mod = it.get_clothing_mod_val( cmt );

    std::optional<resistances> overriden_resistance = it.damage_resistance_override();
    if( overriden_resistance ) {
        float base_resistance = 0.0f;
        auto iter = overriden_resistance->flat.find( dt );
        if( iter != overriden_resistance->flat.end() ) { base_resistance = iter->second; }

        // We can have 0 thickness items, so need to check for it to ensure we don't get NaN in
        // calcs
        const int thickness = it.get_thickness();
        const float damaged_resistance =
            ( thickness == 0 ) ? 0.0f : base_resistance * eff_thickness / thickness;

        return std::lround( damaged_resistance + mod );
    }

    const std::vector<const material_type *> mat_types = it.made_of_types();
    if( !mat_types.empty() ) {
        for( const material_type * mat : mat_types ) { resist += ( mat->*resist_getter )(); }
        // Average based on number of materials.
        resist /= mat_types.size();
    }

    return std::lround( ( resist * eff_thickness ) + mod );
}

int item::bash_resist( bool to_self ) const
{
    return phys_resist( *this, DT_BASH, clothing_mod_type_bash, &material_type::bash_resist, to_self );
}

int item::cut_resist( bool to_self ) const
{
    return phys_resist( *this, DT_CUT, clothing_mod_type_cut, &material_type::cut_resist, to_self );
}

#if defined(_MSC_VER)
#pragma optimize("", on)
#endif

int item::stab_resist( bool to_self ) const
{
    // Better than hardcoding it in multiple places
    return static_cast<int>( 0.8f * cut_resist( to_self ) );
}

int item::bullet_resist( bool to_self ) const
{
    return phys_resist(
           *this, DT_BULLET, clothing_mod_type_bullet, &material_type::bullet_resist, to_self );
}

int item::acid_resist( bool to_self, int base_env_resist ) const
{
    if( to_self ) {
    // Currently no items are damaged by acid
    return INT_MAX;
}

if( is_null() ) { return 0.0; }

    float resist = 0.0;
    float mod = get_clothing_mod_val( clothing_mod_type_acid );

    std::optional<resistances> overriden_resistance = damage_resistance_override();
    if( overriden_resistance && overriden_resistance->flat.contains( DT_ACID ) ) {
        return std::lround( overriden_resistance->flat[DT_ACID] + mod );
    }

    const std::vector<const material_type *> mat_types = made_of_types();
    if( !mat_types.empty() ) {
        // Not sure why cut and bash get an armor thickness bonus but acid doesn't,
        // but such is the way of the code.

        for( const material_type * mat : mat_types ) { resist += mat->acid_resist(); }
        // Average based on number of materials.
        resist /= mat_types.size();
    }

    const int env = get_env_resist( base_env_resist );
    if( env < 10 ) {
    // Low env protection means it doesn't prevent acid seeping in.
    resist *= env / 10.0f;
}

return std::lround( resist + mod );
}

int item::fire_resist( bool to_self, int base_env_resist ) const
{
    if( to_self ) {
    // Fire damages items in a different way
    return INT_MAX;
}

if( is_null() ) { return 0.0; }

    float mod = get_clothing_mod_val( clothing_mod_type_fire );

    std::optional<resistances> overriden_resistance = damage_resistance_override();
    if( overriden_resistance && overriden_resistance->flat.contains( DT_HEAT ) ) {
        return std::lround( overriden_resistance->flat[DT_HEAT] + mod );
    }

    float resist = 0.0;

    const std::vector<const material_type *> mat_types = made_of_types();
    if( !mat_types.empty() ) {
        for( const material_type * mat : mat_types ) { resist += mat->fire_resist(); }
        // Average based on number of materials.
        resist /= mat_types.size();
    }

    const int env = get_env_resist( base_env_resist );
    if( env < 10 ) {
    // Iron resists immersion in magma, iron-clad knight won't.
    resist *= env / 10.0f;
}

return std::lround( resist + mod );
}

int item::damage_resist( damage_type dt, bool to_self ) const
{
    switch( dt ) {
    case DT_NULL:
    case NUM_DT:
        return 0;
    case DT_TRUE:
    case DT_BIOLOGICAL:
    case DT_ELECTRIC:
    case DT_COLD:
    case DT_DARK:
    case DT_LIGHT:
    case DT_PSI:
        // Currently hardcoded:
        // Items can never be damaged by those types
        // But they provide 0 protection from them
        return to_self ? INT_MAX : 0;
    case DT_BASH:
        return bash_resist( to_self );
        case DT_CUT:
            return cut_resist( to_self );
        case DT_ACID:
            return acid_resist( to_self );
        case DT_STAB:
            return stab_resist( to_self );
        case DT_HEAT:
            return fire_resist( to_self );
        case DT_BULLET:
            return bullet_resist( to_self );
        default:
            debugmsg( "Invalid damage type: %d", dt );
    }

    return 0;
}

bool item::covers( const bodypart_id& bp ) const { return get_covered_body_parts().test( bp.id() ); }

body_part_set item::get_covered_body_parts() const { return get_covered_body_parts( get_side() ); }

body_part_set item::get_covered_body_parts( const side s ) const
{
    body_part_set res;

    if( is_gun() ) {
        // Currently only used for guns with the should strap mod, other guns might
        // go on another bodypart.
        res.set( bodypart_str_id( "torso" ) );
    }

    const islot_armor* armor = find_armor_data();
    if( armor == nullptr ) { return res; }

    for( const armor_portion_data& data : armor->data ) { res.unify_set( data.covers ); }

    if( !armor->sided ) {
        return res; // Just ignore the side.
    }

    switch( s ) {
        case side::BOTH:
        case side::num_sides:
            break;

        case side::LEFT:
            res.reset( bodypart_str_id( "arm_r" ) );
            res.reset( bodypart_str_id( "hand_r" ) );
            res.reset( bodypart_str_id( "leg_r" ) );
            res.reset( bodypart_str_id( "foot_r" ) );
            break;

        case side::RIGHT:
            res.reset( bodypart_str_id( "arm_l" ) );
            res.reset( bodypart_str_id( "hand_l" ) );
            res.reset( bodypart_str_id( "leg_l" ) );
            res.reset( bodypart_str_id( "foot_l" ) );
            break;
    }

    return res;
}

bool item::is_sided() const
{
    const islot_armor* armor = find_armor_data();
    return armor ? armor->sided : false;
}

side item::get_side() const
{
    // MSVC complains if directly cast double to enum
    return static_cast<side>( static_cast<int>( get_var( "lateral",
           static_cast<int>( side::BOTH ) ) ) );
}

bool item::set_side( side s )
{
    if( !is_sided() ) { return false; }

    if( s == side::BOTH ) {
        erase_var( "lateral" );
    } else {
        set_var( "lateral", static_cast<int>( s ) );
    }

    return true;
}

bool item::swap_side() { return set_side( opposite_side( get_side() ) ); }

bool item::is_worn_only_with( const item& it ) const
{
    return ( ( has_flag( flag_POWERARMOR_EXTERNAL ) || has_flag( flag_POWERARMOR_MOD ) )
    && it.has_flag( flag_POWERARMOR_EXO ) );
}

item::sizing item::get_sizing( const Character& who ) const
{
    const islot_armor* armor_data = find_armor_data();
    if( !armor_data ) { return sizing::ignore; }
    bool to_ignore = true;
    for( const armor_portion_data& piece : armor_data->data ) {
        if( piece.encumber != 0 || piece.max_encumber != 0 ) { to_ignore = false; }
    }
    if( to_ignore ) {
        return sizing::ignore;
    } else {
        const bool small = who.get_size() == creature_size::tiny;
        const bool big = who.get_size() == creature_size::huge;

        // due to the iterative nature of these features, something can fit and be
        // undersized/oversized but that is fine because we have separate logic to adjust
        // encumberance per each. One day we may want to have fit be a flag that only applies if a
        // piece of clothing is sized for you as there is a bit of cognitive dissonance when
        // something 'fits' and is 'oversized' and the same time
        const bool undersize = has_flag( flag_UNDERSIZE ) || has_flag( flag_resized_small );
        const bool oversize = has_flag( flag_OVERSIZE ) || has_flag( flag_resized_large );

        if( undersize ) {
            if( small ) {
                return sizing::small_sized_small_char;
            } else if( big ) {
                return sizing::small_sized_big_char;
            } else {
                return sizing::small_sized_human_char;
            }
        } else if( oversize ) {
            if( big ) {
                return sizing::big_sized_big_char;
            } else if( small ) {
                return sizing::big_sized_small_char;
            } else {
                return sizing::big_sized_human_char;
            }
        } else {
            if( big ) {
                return sizing::human_sized_big_char;
            } else if( small ) {
                return sizing::human_sized_small_char;
            } else {
                return sizing::human_sized_human_char;
            }
        }
    }
}

bool item::has_clothing_mod() const
{
for( const clothing_mod &cm : clothing_mods::get_all() ) {
    if( has_own_flag( cm.flag ) ) {
            return true;
        }
    }
    return false;
}

namespace
{
const std::string &get_clothing_mod_val_key( clothing_mod_type type )
{
    const static auto cache = ( []() {
        std::array<std::string, clothing_mods::all_clothing_mod_types.size()> res;
        for( const clothing_mod_type &type : clothing_mods::all_clothing_mod_types ) {
            res[type] = CLOTHING_MOD_VAR_PREFIX
                        + clothing_mods::string_from_clothing_mod_type( clothing_mods::all_clothing_mod_types[type] );
        }
        return res;
    } )();

    return cache[ type ];
}
} // namespace

float item::get_clothing_mod_val( clothing_mod_type type ) const
{
    return get_var( get_clothing_mod_val_key( type ), 0.0f );
}

void item::update_clothing_mod_val()
{
    for( const clothing_mod_type &type : clothing_mods::all_clothing_mod_types ) {
        float tmp = 0.0;
        for( const clothing_mod &cm : clothing_mods::get_all_with( type ) ) {
            if( has_own_flag( cm.flag ) ) {
                tmp += cm.get_mod_val( type, *this );
            }
        }
        set_var( get_clothing_mod_val_key( type ), tmp );
    }
}
