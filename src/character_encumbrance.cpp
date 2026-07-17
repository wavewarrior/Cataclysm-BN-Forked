#include "character.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "bionics.h"
#include "character_encumbrance.h"
#include "clothing_utils.h"
#include "bodypart.h"
#include "debug.h"
#include "flag.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "messages.h"
#include "mutation.h"
#include "options.h"
#include "output.h"
#include "player.h"
#include "translations.h"
#include "type_id.h"
#include "vehicle.h"
#include "vpart_position.h"
#include "weather.h"

static const skill_id skill_swimming( "swimming" );
static const bionic_id bio_climate( "bio_climate" );
static const bionic_id bio_shock_absorber( "bio_shock_absorber" );
static const trait_id trait_DEBUG_WEIGHTLESSNESS( "DEBUG_WEIGHTLESSNESS" );
static const trait_id trait_M_SKIN3( "M_SKIN3" );

void Character::reset_encumbrance() { *encumbrance_cache = calc_encumbrance(); }

char_encumbrance_data Character::calc_encumbrance() const
{
    return calc_encumbrance( null_item_reference() );
}

char_encumbrance_data Character::calc_encumbrance( const item& new_item ) const
{

    char_encumbrance_data enc;
    // Make sure we have all body parts here, so that we can use ::at
    for( const bodypart_id& bp : get_all_body_parts() ) { enc.elems[bp.id()]; }

    item_encumb( enc, new_item );
    mut_cbm_encumb( enc );

    // Get swimming skill level
    if( get_option<bool>( "althletics_encumbrance_buff" ) ) {
        int swim_skill = get_skill_level( skill_swimming );

        // Reduce encumbrance for each body part based on swimming skill
        for( auto& iter : enc.elems ) {
            encumbrance_data& edata = iter.second;

            // Reduce encumbrance by swim_skill, clamped at 0
            edata.encumbrance = std::max( 0, edata.encumbrance - swim_skill );
        }
    }

    return enc;
}

units::mass Character::get_weight() const
{
    if( has_trait( trait_DEBUG_WEIGHTLESSNESS ) ) { return 0_gram; }

const auto worn_weight = std::ranges::fold_left( worn, 0_gram,
[]( const auto sum, const auto * const itm ) { return sum + itm->weight(); } );

    auto ret = bodyweight();          // The base weight of the player's body
    ret += inv.weight();              // Weight of the stored inventory
    ret += worn_weight;               // Weight of worn items
    ret += primary_weapon().weight(); // Weight of wielded item
    ret += bionics_weight();          // Weight of installed bionics
    return ret;
}

char_encumbrance_data Character::get_encumbrance() const { return *encumbrance_cache; }

char_encumbrance_data Character::get_encumbrance( const item& new_item ) const
{
    return calc_encumbrance( new_item );
}

int Character::extra_encumbrance( layer_level level, const bodypart_str_id& bp ) const
{
    auto iter = encumbrance_cache->elems.find( bp );
    if( iter != encumbrance_cache->elems.end() ) {
        return iter->second.layer_penalty_details[static_cast<int>( level )].total;
    }

    return 0;
}

bool Character::change_side( item& it, bool interactive )
{
    const auto ret = can_swap( it );
    if( !ret.success() ) {
        if( interactive ) { add_msg_if_player( m_info, "%s", ret.c_str() ); }
        return false;
    }

    if( !it.swap_side() ) {
        if( interactive ) {
            add_msg_player_or_npc(
                m_info, _( "You cannot swap the side on which your %s is worn." ),
                _( "<npcname> cannot swap the side on which their %s is worn." ), it.tname() );
        }
        return false;
    }

    if( interactive ) {
        add_msg_player_or_npc(
            m_info, _( "You swap the side on which your %s is worn." ),
            _( "<npcname> swaps the side on which their %s is worn." ), it.tname() );
    }

    mod_moves( -250 );
    reset_encumbrance();

    return true;
}

bool Character::change_side( item* it, bool interactive )
{
    if( !it || !is_worn( *it ) ) {
        if( interactive ) {
            add_msg_player_or_npc(
                m_info, _( "You are not wearing that item." ),
                _( "<npcname> isn't wearing that item." ) );
        }
        return false;
    }

    return change_side( *it, interactive );
}

static void layer_item(
    char_encumbrance_data& vals, const item& it,
    std::map<bodypart_str_id, layer_level> &highest_layer_so_far, const Character& c )
{
    body_part_set covered_parts = it.get_covered_body_parts();
    for( const bodypart_id bp : c.get_all_body_parts() ) {
        if( !covered_parts.test( bp.id() ) ) { continue; }

        const auto item_layer = it.get_layer();
        int encumber_val = it.get_encumber( c, bp );
        // For the purposes of layering penalty, set a min of 2 and a max of 10 per item.
        int layering_encumbrance = std::min( 10, std::max( 2, encumber_val ) );

        /*
         * Setting layering_encumbrance to 0 at this point makes the item cease to exist
         * for the purposes of the layer penalty system. (normally an item has a minimum
         * layering_encumbrance of 2 )
         */
        if( it.has_flag( flag_SEMITANGIBLE ) ) {
            encumber_val = 0;
            layering_encumbrance = 0;
        }
        if( is_compact( it, c ) ) { layering_encumbrance = 0; }

        highest_layer_so_far[bp.id()] = std::max( highest_layer_so_far[bp.id()], item_layer );

        // Apply layering penalty to this layer, as well as any layer worn
        // within it that would normally be worn outside of it.
        for( layer_level penalty_layer = item_layer; penalty_layer <= highest_layer_so_far[bp.id()];
             ++penalty_layer ) {
            vals.elems[bp.id()].layer( penalty_layer, layering_encumbrance );
        }

        vals.elems[bp.id()].armor_encumbrance += encumber_val;
    }
}

bool Character::is_wearing_power_armor( bool* hasHelmet ) const
{
    bool result = false;
    for( auto& elem : worn ) {
        if( !elem->is_power_armor() ) { continue; }
        if( elem->has_flag( flag_POWERARMOR_EXO ) ) {
            result = true;
            if( hasHelmet == nullptr ) {
                // found power armor, helmet not requested, cancel loop
                return true;
            }
        }
        // found power armor, continue search for helmet
        if( elem->covers( bodypart_id( "head" ) ) ) {
            if( hasHelmet != nullptr ) { *hasHelmet = true; }
            return true;
        }
    }
    return result;
}

bool Character::is_wearing_active_power_armor() const
{
for( const auto &w : worn ) {
    if( w->has_flag( flag_POWERARMOR_EXO ) && w->is_active() ) {
            return true;
        }
    }
    return false;
}

bool Character::is_wearing_active_optcloak() const
{
for( const auto &w : worn ) {
    if( w->is_active() && w->has_flag( flag_ACTIVE_CLOAKING ) ) {
            return true;
        }
    }
    return false;
}

bool Character::in_climate_control()
{
    bool regulated_area = false;
    // Check
    if( has_active_bionic( bio_climate ) ) { return true; }
    map& here = get_map();
    if( has_trait( trait_M_SKIN3 ) && here.has_flag_ter_or_furn( "FUNGUS", bub_pos() )
        && in_sleep_state() ) {
        return true;
    }
    for( const auto& w : worn ) {
        if( w->has_flag( flag_CLIMATE_CONTROL ) ) { return true; }
    }
    if( calendar::turn >= next_climate_control_check ) {
        // save CPU and simulate acclimation.
        next_climate_control_check = calendar::turn + 20_turns;
        if( const optional_vpart_position vp = here.veh_at( bub_pos() ) ) {
            // TODO: (?) Force player to scrounge together an AC unit
            regulated_area =
                ( vp->is_inside() &&                   // Already checks for opened doors
                  vp->vehicle().total_power_w( true ) > 0 // Out of gas? No AC for you!
                );
        }
        // TODO: AC check for when building power is implemented
        last_climate_control_ret = regulated_area;
        if( !regulated_area ) {
            // Takes longer to cool down / warm up with AC, than it does to step outside and feel
            // cruddy.
            next_climate_control_check += 40_turns;
        }
    } else {
        return last_climate_control_ret;
    }
    return regulated_area;
}

static int wind_resistance_from_item_list(
    const std::vector<const item *> &items, const bodypart_id& bp )
{
    int total_exposed = 100;

    for( const item * it : items ) {
        const item& i = *it;
        int penalty = 100 - i.wind_resist();
        int coverage = std::max( 0, i.get_coverage( bp ) - penalty );
        total_exposed = total_exposed * ( 100 - coverage ) / 100;
    }

    return 100 - total_exposed;
}

namespace warmth
{

std::map<bodypart_id, int> wind_resistance_from_clothing(
    const std::map<bodypart_id, std::vector<const item *>> &clothing_map )
{
    std::map<bodypart_id, int> ret;
    for( const std::pair<const bodypart_id, std::vector<const item * >> &on_bp : clothing_map ) {
        ret[on_bp.first] = wind_resistance_from_item_list( on_bp.second, on_bp.first );
    }

    return ret;
}

} // namespace warmth

void layer_details::reset() { *this = layer_details(); }

// The stacking penalty applies by doubling the encumbrance of
// each item except the highest encumbrance one.
// So we add them together and then subtract out the highest.
int layer_details::layer( const int encumbrance )
{
    /*
     * We should only get to this point with an encumbrance value of 0
     * if the item is 'semitangible'. A normal item has a minimum of
     * 2 encumbrance for layer penalty purposes.
     * ( even if normally its encumbrance is 0 )
     */
    if( encumbrance == 0 ) {
        return total; // skip over the other logic because this item doesn't count
    }

    pieces.push_back( encumbrance );

    int current = total;
    if( encumbrance > max ) {
        total += max; // *now* the old max is counted, just ignore the new max
        max = encumbrance;
    } else {
        total += encumbrance;
    }
    return total - current;
}

location_vector<item>::iterator Character::position_to_wear_new_item( const item& new_item )
{
    // By default we put this item on after the last item on the same or any
    // lower layer.
    return std::
           ranges::
    find_if( std::ranges::reverse_view( worn ), [&]( const item * const & w ) {
        return w->get_layer() <= new_item.get_layer();
    } ).base();
}

/*
 * Encumbrance logic:
 * Some clothing is intrinsically encumbering, such as heavy jackets, backpacks, body armor, etc.
 * These simply add their encumbrance value to each body part they cover.
 * In addition, each article of clothing after the first in a layer imposes an additional penalty.
 * e.g. one shirt will not encumber you, but two is tight and starts to restrict movement.
 * Clothes on separate layers don't interact, so if you wear e.g. a light jacket over a shirt,
 * they're intended to be worn that way, and don't impose a penalty.
 * The default is to assume that clothes do not fit, clothes that are "fitted" either
 * reduce the encumbrance penalty by ten, or if that is already 0, they reduce the layering effect.
 *
 * Use cases:
 * What would typically be considered normal "street clothes" should not be considered encumbering.
 * T-shirt, shirt, jacket on torso/arms, underwear and pants on legs, socks and shoes on feet.
 * This is currently handled by each of these articles of clothing
 * being on a different layer and/or body part, therefore accumulating no encumbrance.
 */
void Character::item_encumb( char_encumbrance_data& vals, const item& new_item ) const
{

    // reset all layer data
    vals = char_encumbrance_data();

    // Figure out where new_item would be worn
    location_vector<item>::const_iterator new_item_position = worn.end();
    if( !new_item.is_null() ) {
        // const_cast required to work around g++-4.8 library bug
        // see the commit that added this comment to understand why
        new_item_position = const_cast<Character *>( this )->position_to_wear_new_item( new_item );
    }

    // Track highest layer observed so far so we can penalize out-of-order
    // items
    std::map<bodypart_str_id, layer_level> highest_layer_so_far;
    const auto& all_bps = get_all_body_parts();

    for( const bodypart_id& bp : all_bps ) { highest_layer_so_far[bp.id()] = PERSONAL_LAYER; }

    for( auto w_it = worn.begin(); w_it != worn.end(); ++w_it ) {
        if( w_it == new_item_position ) { layer_item( vals, new_item, highest_layer_so_far, *this ); }
        layer_item( vals, **w_it, highest_layer_so_far, *this );
    }

    if( worn.end() == new_item_position && !new_item.is_null() ) {
        layer_item( vals, new_item, highest_layer_so_far, *this );
    }

    // make sure values are sane
    for( const bodypart_id& bp : all_bps ) {
        encumbrance_data& elem = vals.elems[bp.id()];

        elem.armor_encumbrance = std::max( 0, elem.armor_encumbrance );

        // Add armor and layering penalties for the final values
        elem.encumbrance += elem.armor_encumbrance + elem.layer_penalty;
    }
    // @todo Debugmsg if there are bps not on our body list
}

int Character::encumb( const bodypart_str_id& bp ) const
{
    const auto iter = encumbrance_cache->elems.find( bp );
    if( iter != encumbrance_cache->elems.end() ) {
        // @todo Debugmsg?
        return iter->second.encumbrance;
    }
    return 0;
}

static void apply_mut_encumbrance(
    char_encumbrance_data& vals, const trait_id& mut, const body_part_set& oversize )
{
    for( const std::pair<const body_part, int> &enc : mut->encumbrance_always ) {
        vals.elems[convert_bp( enc.first )].encumbrance += enc.second;
    }

    for( const std::pair<const body_part, int> &enc : mut->encumbrance_covered ) {
        if( !oversize.test( convert_bp( enc.first ) ) ) {
            vals.elems[convert_bp( enc.first )].encumbrance += enc.second;
        }
    }
}

void Character::mut_cbm_encumb( char_encumbrance_data& vals ) const
{

for( const bionic &i : get_bionic_collection() ) {
    const bionic_id &bid = i.id;
    for( const std::pair<const bodypart_str_id, int> &element : bid->encumbrance ) {
            vals.elems[element.first].encumbrance += element.second;
        }
    }

    if( has_active_bionic( bio_shock_absorber ) ) {
    for( auto &val : vals.elems ) {
            val.second.encumbrance += 3; // Slight encumbrance to all parts except eyes
        }
        vals.elems[body_part_eyes].encumbrance -= 3;
    }

    // Lower penalty for bps covered only by XL armor
    const auto oversize = exclusive_flag_coverage( flag_OVERSIZE );
for( const trait_id &mut : get_mutations() ) {
    apply_mut_encumbrance( vals, mut, oversize );
    }
}

body_part_set Character::exclusive_flag_coverage( const flag_id& flag ) const
{
    body_part_set ret;
    ret.fill( get_all_body_parts() );

    for( const auto& elem : worn ) {
        if( !elem->has_flag( flag ) ) {
            // Unset the parts covered by this item
            ret.substract_set( elem->get_covered_body_parts() );
        }
    }

    return ret;
}

/*
 * Innate stats getters
 */

// get_stat() always gets total (current) value, NEVER just the base
// get_stat_bonus() is always just the bonus amount
