// Item display/naming: color_in_inventory, tname, display_name, display_money, color
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

// File-scope id constants (moved with display methods; internal linkage).
static const fault_id fault_bionic_nonsterile( "fault_bionic_nonsterile" );
static const flag_id flag_genome_drive( "GENOME_DRIVE" );
static const itype_id itype_barrel_small( "barrel_small" );
static const itype_id itype_stock_small( "stock_small" );
static const skill_id skill_survival( "survival" );
static const trait_id trait_WOOLALLERGY( "WOOLALLERGY" );

nc_color item::color_in_inventory() const
{
    return item::color_in_inventory( get_avatar() );
}

nc_color item::color_in_inventory( const player &p ) const
{
    // Only item not otherwise colored gets colored as favorite
    nc_color ret = is_favorite ? c_white : c_light_gray;
    if( type->can_use( "learn_spell" ) ) {
        const use_function *iuse = get_use( "learn_spell" );
        const learn_spell_actor *actor_ptr =
            static_cast<const learn_spell_actor *>( iuse->get_actor_ptr() );
        for( const std::string &spell_id_str : actor_ptr->spells ) {
            const spell_id sp_id( spell_id_str );
            if( p.magic->knows_spell( sp_id ) && !p.magic->get_spell( sp_id ).is_max_level() ) {
                ret = c_yellow;
            }
            if( !p.magic->knows_spell( sp_id ) && p.magic->can_learn_spell( p, sp_id ) ) {
                return c_light_blue;
            }
        }
    } else if( has_flag( flag_WET ) ) {
        ret = c_cyan;
    } else if( has_flag( flag_LITCIG ) ) {
        ret = c_red;
    } else if( is_armor() && p.has_trait( trait_WOOLALLERGY ) &&
               ( made_of( material_id( "wool" ) ) || has_own_flag( flag_wooled ) ) ) {
        ret = c_red;
    } else if( has_own_flag( flag_DIRTY ) ) {
        ret = c_brown;
    } else if( is_bionic() ) {
        if( ( !p.has_bionic( type->bionic->id ) &&
              !character_funcs::has_upgraded_bionic( p, type->bionic->id ) ) ||
            type->bionic->id->has_flag( flag_MULTIINSTALL ) ) {
            ret = p.bionic_installation_issues( type->bionic->id ).empty() ? c_green : c_red;
        } else if( !has_fault( fault_bionic_nonsterile ) ) {
            ret = c_dark_gray;
        }
    } else if( has_flag( flag_LEAK_DAM ) && has_flag( flag_RADIOACTIVE ) && damage() > 0 ) {
        ret = c_light_green;
    } else if( is_active() && !is_food() && !is_food_container() && !is_corpse() ) {
        // Active items show up as yellow
        ret = c_yellow;
    } else if( is_corpse() && ( can_revive() || corpse->zombify_into ) && !has_flag( flag_PULPED ) ) {
        // Only reviving corpses are yellow
        ret = c_yellow;
    } else if( const item *food = get_food() ) {
        const bool preserves = type->container && type->container->preserves;

        // Give color priority to allergy (allergy > inedible by freeze or other conditions)
        // TODO: refactor u.will_eat to let this section handle coloring priority without duplicating code.
        if( p.allergy_type( *food ) != morale_type( "morale_null" ) ) {
            return c_red;
        }

        // Default: permafood, drugs
        // Brown: rotten (for non-saprophages) or non-rotten (for saprophages)
        // Dark gray: inedible
        // Red: morale penalty
        // Yellow: will rot soon
        // Cyan: will rot eventually
        const ret_val<edible_rating> rating = p.will_eat( *food );
        // TODO: More colors
        switch( rating.value() ) {
            case edible_rating::edible:
            case edible_rating::too_full:
                if( preserves ) {
                    // Nothing, canned food won't rot
                } else if( food->is_going_bad() ) {
                    ret = c_yellow;
                } else if( food->goes_bad() ) {
                    ret = c_cyan;
                }
                break;
            case edible_rating::inedible:
            case edible_rating::inedible_mutation:
                ret = c_dark_gray;
                break;
            case edible_rating::allergy:
            case edible_rating::allergy_weak:
            case edible_rating::cannibalism:
                ret = c_red;
                break;
            case edible_rating::rotten:
                ret = c_brown;
                break;
            case edible_rating::nausea:
            case edible_rating::bloated:
                ret = c_pink;
                break;
            case edible_rating::no_tool:
                break;
        }
    } else if( is_gun() ) {
        // Guns are green if you are carrying ammo for them
        // ltred if you have ammo but no mags
        // Gun with integrated mag counts as both
        for( const ammotype &at : ammo_types() ) {
            // get_ammo finds uncontained ammo, find_ammo finds ammo in magazines
            bool has_ammo = !character_funcs::get_ammo_items( p, at ).empty() ||
                            !character_funcs::find_ammo_items_or_mags( p, *this, false, -1 ).empty();
            bool has_mag = magazine_integral() ||
                           !character_funcs::find_ammo_items_or_mags( p, *this, true, -1 ).empty();
            if( has_ammo && has_mag ) {
                ret = c_green;
                break;
            } else if( has_ammo || has_mag ) {
                ret = c_light_red;
                break;
            }
        }
    } else if( is_ammo() ) {
        // Likewise, ammo is green if you have guns that use it
        // ltred if you have the gun but no mags
        // Gun with integrated mag counts as both
        bool has_gun = p.has_item_with( [this]( const item & i ) {
            return i.is_gun() && i.ammo_types().contains( ammo_type() );
        } );
        bool has_mag = p.has_item_with( [this]( const item & i ) {
            return ( i.is_gun() && i.magazine_integral() && i.ammo_types().contains( ammo_type() ) ) ||
                   ( i.is_magazine() && i.ammo_types().contains( ammo_type() ) );
        } );
        if( has_gun && has_mag ) {
            ret = c_green;
        } else if( has_gun || has_mag ) {
            ret = c_light_red;
        }
    } else if( is_magazine() ) {
        // Magazines are green if you have guns and ammo for them
        // ltred if you have one but not the other
        bool has_gun = p.has_item_with( [this]( const item & it ) {
            return it.is_gun() && it.magazine_compatible().contains( typeId() );
        } );
        bool has_ammo = !character_funcs::find_ammo_items_or_mags( p, *this, false, -1 ).empty();
        if( has_gun && has_ammo ) {
            ret = c_green;
        } else if( has_gun || has_ammo ) {
            ret = c_light_red;
        }
    } else if( is_book() ) {
        const islot_book &tmp = *type->book;
        // Player doesn't actually interested if NPC has identified book yet.
        // So we check identification for human avatar.
        if( get_avatar().has_identified( typeId() ) ) {
            if( tmp.skill && // Book can improve skill: blue
                p.get_skill_level_object( tmp.skill ).can_train() &&
                p.get_skill_level( tmp.skill ) >= tmp.req &&
                p.get_skill_level( tmp.skill ) < tmp.level ) {
                ret = c_light_blue;
            } else if( type->can_use( "MA_MANUAL" ) &&
                       !p.martial_arts_data->has_martialart( martial_art_learned_from( *type ) ) ) {
                ret = c_light_blue;
            } else if( tmp.skill && // Book can't improve skill right now, but maybe later: pink
                       p.get_skill_level_object( tmp.skill ).can_train() &&
                       p.get_skill_level( tmp.skill ) < tmp.level ) {
                ret = c_pink;
            } else if( !p.studied_all_recipes(
                           *type ) ) { // Book can't improve skill anymore, but has more recipes: yellow
                ret = c_yellow;
            }
        } else if( tmp.skill || type->can_use( "MA_MANUAL" ) ) {
            // Book can teach you something and hasn't been identified yet
            ret = c_red;
        } else {
            // "just for fun" book that they haven't read yet
            ret = c_magenta;
        }
    }
    return ret;
}


std::string item::tname( unsigned int quantity, bool with_prefix, unsigned int truncate ) const
{
    int dirt_level = get_var( "dirt", 0 ) / 2000;
    std::string dirt_symbol;
    // TODO: MATERIALS put this in json

    // these symbols are unicode square characeters of different heights, representing a rough
    // estimation of fouling in a gun. This appears instead of "faulty"
    // since most guns will have some level of fouling in them, and usually it is not a big deal.
    switch( dirt_level ) {
        case 0:
            dirt_symbol = "";
            break;
        case 1:
            dirt_symbol = "<color_white>\u2581</color>";
            break;
        case 2:
            dirt_symbol = "<color_light_gray>\u2583</color>";
            break;
        case 3:
            dirt_symbol = "<color_light_gray>\u2585</color>";
            break;
        case 4:
            dirt_symbol = "<color_dark_gray>\u2587</color>";
            break;
        case 5:
            dirt_symbol = "<color_brown>\u2588</color>";
            break;
        default:
            dirt_symbol = "";
    }
    std::string damtext;

    // for portions of string that have <color_ etc in them, this aims to truncate the whole string correctly
    unsigned int truncate_override = 0;

    if( ( damage() != 0 || ( get_option<bool>( "ITEM_HEALTH_BAR" ) && is_armor() ) ) && !is_null() &&
        with_prefix ) {
        damtext = durability_indicator();
        if( get_option<bool>( "ITEM_HEALTH_BAR" ) ) {
            // get the utf8 width of the tags
            truncate_override = utf8_width( damtext, false ) - utf8_width( damtext, true );
        }
    }

    if( !faults.empty() ) {
        const bool silent = std::ranges::any_of( faults, []( const fault_id & f ) -> bool { return f->has_flag( "SILENT" ); } );
        if( silent ) {
            damtext.insert( 0, dirt_symbol );
        } else {
            damtext.insert( 0, _( "faulty " ) + dirt_symbol );
        }
    }

    std::string vehtext;
    if( is_engine() && engine_displacement() > 0 ) {
        vehtext = string_format( pgettext( "vehicle adjective", "%2.1fL " ),
                                 engine_displacement() / 100.0f );

    } else if( is_wheel() && type->wheel->diameter > 0 ) {
        vehtext = string_format( pgettext( "vehicle adjective", "%s " ),
                                 wheel_dimensions::format_for_display( type->wheel->diameter ).c_str() );
    }

    std::string burntext;
    if( with_prefix && !made_of( LIQUID ) ) {
        if( volume() >= 1_liter && burnt * 125_ml >= volume() ) {
            burntext = pgettext( "burnt adjective", "badly burnt " );
        } else if( burnt > 0 ) {
            burntext = pgettext( "burnt adjective", "burnt " );
        }
    }

    std::string maintext;
    if( is_corpse() || item_vars_.contains( "name" ) ) {
        maintext = type_name( quantity );
    } else if( is_craft() ) {
        maintext = string_format( _( "in progress %s" ), craft_data_->making->result_name() );
        if( charges > 1 ) {
            maintext += string_format( " (%d)", charges );
        }
        const int percent_progress = item_counter / 100000;
        maintext += string_format( " (%d%%)", percent_progress );
    } else {
        std::string labeltext = label( quantity );

        int modamt = 0;
        if( is_tool() ) {
            modamt += toolmods().size();
        }
        if( is_gun() ) {
            for( const item *mod : gunmods() ) {
                if( !type->gun->built_in_mods.contains( mod->typeId() ) ) {
                    modamt++;
                }
            }
        }
        if( is_armor() && has_clothing_mod() ) {
            modamt++;
        }
        if( modamt ) {
            labeltext += string_format( "+%d", modamt );
        }

        if( is_gun() || is_tool() || is_magazine() ) {
            maintext = labeltext;
        } else if( contents.num_item_stacks() == 1 ) {
            const item &contents_item = contents.front();
            const unsigned contents_count =
                ( ( contents_item.made_of( LIQUID ) || contents_item.is_food() ) &&
                  contents_item.charges > 1 )
                ? contents_item.charges
                : quantity;
            maintext = string_format( pgettext( "item name", "%2$s (%1$s)" ), labeltext,
                                      contents_item.tname( contents_count, with_prefix ) );
        } else if( !contents.empty() ) {
            maintext = string_format( vpgettext( "item name",
                                                 //~ %1$s: item name, %2$zd: content size
                                                 "%1$s with %2$zd item",
                                                 "%1$s with %2$zd items", contents.num_item_stacks() ),
                                      labeltext, contents.num_item_stacks() );
        } else {
            maintext = labeltext;
        }
    }

    avatar &you = get_avatar();
    std::string tagtext;
    if( is_food() ) {
        if( has_flag( flag_HIDDEN_POISON ) && you.get_skill_level( skill_survival ) >= 3 ) {
            tagtext += _( " (poisonous)" );
        } else if( has_flag( flag_HIDDEN_HALLU ) && you.get_skill_level( skill_survival ) >= 5 ) {
            tagtext += _( " (hallucinogenic)" );
        }
    }
    if( is_book() ) {
        if( !you.has_identified( typeId() ) ) {
            tagtext += _( " (unread)" );
        }
    }
    if( has_var( "bionics_scanned_by" ) ) {
        if( has_flag( flag_CBM_SCANNED ) ) {
            tagtext += _( " (bionic detected)" );
        } else {
            tagtext += _( " (scanned)" );
        }
    }
    if( has_flag( flag_ETHEREAL_ITEM ) ) {
        tagtext += string_format( _( " (%s turns)" ), get_var( "ethereal" ) );
    } else if( goes_bad() || is_food() ) {
        if( has_own_flag( flag_DIRTY ) ) {
            tagtext += _( " (dirty)" );
        } else if( rotten() ) {
            tagtext += _( " (rotten)" );
        } else if( is_going_bad() ) {
            tagtext += _( " (old)" );
        } else if( is_fresh() ) {
            tagtext += _( " (fresh)" );
        }
        if( is_loaded() ) {
            const auto temp = rot::temperature_flag_for_location( get_map(), *this );
            if( temp == temperature_flag::TEMP_FREEZER ) {
                tagtext += _( " (frozen)" );
            } else if( temp == temperature_flag::TEMP_FRIDGE || temp == temperature_flag::TEMP_ROOT_CELLAR ) {
                tagtext += _( " (cold)" );
            }
        }
    }

    const sizing sizing_level = get_sizing( you );

    if( sizing_level == sizing::human_sized_small_char ) {
        tagtext += _( " (too big)" );
    } else if( sizing_level == sizing::big_sized_small_char ) {
        tagtext += _( " (huge!)" );
    } else if( sizing_level == sizing::human_sized_big_char ||
               sizing_level == sizing::small_sized_human_char ) {
        tagtext += _( " (too small)" );
    } else if( sizing_level == sizing::small_sized_big_char ) {
        tagtext += _( " (tiny!)" );
    } else if( !has_flag( flag_FIT ) && has_flag( flag_VARSIZE ) ) {
        tagtext += _( " (poor fit)" );
    }

    if( is_bionic() && !has_fault( fault_bionic_nonsterile ) ) {
        tagtext += _( " (sterile)" );
    }

    if( is_tool() && has_flag( flag_USE_UPS ) && !has_flag( flag_NAT_UPS ) ) {
        tagtext += _( " (UPS)" );
    }
    if( is_tool() && has_flag( flag_HEATS_FOOD ) ) {
        tagtext += _( " (heats)" );
    }


    if( has_var( "specimen_sample" ) ) {
        const std::string specimen_name = get_var( "specimen_name" );
        const int progress = get_var( "specimen_sample_progress", 0 );
        const auto specimen_id = mtype_id( get_var( "specimen_sample" ) );
        const auto size = cloning_utils::specimen_required_sample_size( specimen_id );
        if( has_flag( flag_genome_drive ) && size > 0 && progress < size ) {
            tagtext += string_format( " (%s [%d/%d])", specimen_name, progress, size );
        } else {
            tagtext += string_format( " (%s)", specimen_name );
        }
    }

    if( has_var( "place_monster_override" ) ) {
        tagtext += string_format( " (%s)", get_var( "place_monster_override_name" ) );
    }

    if( has_var( "NANOFAB_GROUP_ID" ) ) {
        std::string group_id_str = get_var( "NANOFAB_GROUP_ID" );
        const std::string prefix = "nanofab_template_";

        // Remove prefix if it exists
        if( group_id_str.rfind( prefix, 0 ) == 0 ) {
            group_id_str = group_id_str.substr( prefix.size() );
        }

        // Replace underscores with spaces
        std::replace( group_id_str.begin(), group_id_str.end(), '_', ' ' );

        // Append to tag text
        tagtext += string_format( " (%s)", group_id_str );
    } else if( has_var( "NANOFAB_ITEM_ID" ) ) {
        itype_id item = itype_id( get_var( "NANOFAB_ITEM_ID" ) );
        tagtext += string_format( " (%s [%d])", nname( item ), std::max( 1, item->volume / 250_ml ) * 5 );
    }

    if( already_used_by_player( you ) ) {
        tagtext += _( " (used)" );
    }
    if( has_flag( flag_IS_UPS ) && get_var( "cable" ) == "plugged_in" ) {
        tagtext += _( " (plugged in)" );
    }

    std::string modtext;
    if( gunmod_find( itype_barrel_small ) ) {
        modtext += _( "sawn-off " );
    }
    if( gunmod_find( itype_stock_small ) ) {
        modtext += _( "pistol " );
    }
    if( has_flag( flag_DIAMOND ) ) {
        modtext += std::string( pgettext( "Adjective, as in diamond katana", "diamond" ) ) + " ";
    }

    // Collects all flags from the item and its type, then appends their display tags to the item
    // name. This is used to display tags from both the item and its type, such as (wet) or (XL).
    namespace ranges = std::ranges;
    const auto display_tags = []( const auto & flags ) {
        using namespace std::views;
        const auto get_tag = transform( []( const flag_id & f ) -> const translation & { return f->tag(); } );
        const auto has_tag = filter( []( const translation & tag ) { return !tag.empty(); } );
        const auto translate_tag = transform( []( const translation & tag ) { return tag.translated(); } );
        return flags | get_tag | has_tag | translate_tag;
    };
    std::vector<std::string> flag_tags;
    ranges::copy( display_tags( get_flags() ), std::back_inserter( flag_tags ) );
    ranges::copy( display_tags( type->item_tags ), std::back_inserter( flag_tags ) );
    if( !flag_tags.empty() ) {
        tagtext += " " + enumerate_as_string( flag_tags, enumeration_conjunction::space );
    }

    if( is_favorite ) {
        tagtext += _( " *" ); // Display asterisk for favorite items
    }

    // Display throw-slot marker for items marked in the avatar's quick-throw slots.
    if( g ) {
        const auto &u = g->u;
        for( int i = 0; i < avatar::MAX_THROW_SLOTS; ++i ) {
            if( u.get_throw_slot( i ) == typeId() ) {
                tagtext += string_format( _( " [T%d]" ), i + 1 );
                break;
            }
        }
    }

    //~ This is a string to construct the item name as it is displayed. This format string has been added for maximum flexibility. The strings are: %1$s: Damage text (e.g. "bruised"). %2$s: burn adjectives (e.g. "burnt"). %3$s: tool modifier text (e.g. "atomic"). %4$s: vehicle part text (e.g. "3.8-Liter"). $5$s: main item text (e.g. "apple"). %6s: tags (e.g. "(wet) (poor fit)").
    std::string ret = string_format( _( "%1$s%2$s%3$s%4$s%5$s%6$s" ), damtext, burntext, modtext,
                                     vehtext, maintext, tagtext );

    if( truncate != 0 ) {
        ret = utf8_truncate( ret, truncate + truncate_override );
    }

    if( item_vars_.contains( "item_note" ) ) {
        //~ %s is an item name. This style is used to denote items with notes.
        return string_format( _( "*%s*" ), ret );
    } else {
        return ret;
    }
}

std::string item::display_money( unsigned int quantity, unsigned int total,
                                 const std::optional<unsigned int> &selected ) const
{
    if( selected ) {
    //~ This is a string to display the selected and total amount of money in a stack of cash cards.
    //~ %1$s is the display name of cash cards.
    //~ %2$s is the total amount of money.
    //~ %3$s is the selected amount of money.
    //~ Example: "cash cards $15.35 of $20.48"
    return string_format( pgettext( "cash card and money", "%1$s %3$s of %2$s" ), tname( quantity ),
                          format_money( total ), format_money( *selected ) );
    } else {
        //~ This is a string to display the total amount of money in a stack of cash cards.
        //~ %1$s is the display name of cash cards.
        //~ %2$s is the total amount of money on the cash cards.
        //~ Example: "cash cards $20.48"
        return string_format( pgettext( "cash card and money", "%1$s %2$s" ), tname( quantity ),
                              format_money( total ) );
    }
}

std::string item::display_name( unsigned int quantity ) const
{
    std::string name = tname( quantity );
    std::string sidetxt;
    std::string amt;

    switch( get_side() ) {
        case side::BOTH:
        case side::num_sides:
            break;
        case side::LEFT:
            sidetxt = string_format( " (%s)", _( "left" ) );
            break;
        case side::RIGHT:
            sidetxt = string_format( " (%s)", _( "right" ) );
            break;
    }
    avatar &you = get_avatar();
    static const itype_id itype_battery( "battery" );
    int amount = 0;
    int max_amount = 0;
    bool has_item = is_container() && contents.num_item_stacks() == 1;
    bool has_ammo = is_ammo_container() && contents.num_item_stacks() == 1;
    bool contains = has_item || has_ammo;
    bool show_amt = false;
    // We should handle infinite charges properly in all cases.
    if( contains ) {
        amount = contents.front().charges;
        max_amount = contents.front().charges_per_volume( get_container_capacity() );
    } else if( is_book() && get_chapters() > 0 ) {
        // a book which has remaining unread chapters
        amount = get_remaining_chapters( you );
    } else if( ammo_capacity() > 0 ) {
        // anything that can be reloaded including tools, magazines, guns and auxiliary gunmods
        // but excluding bows etc., which have ammo, but can't be reloaded
        amount = ammo_remaining();
        max_amount = ammo_capacity();
        show_amt = !has_flag( flag_RELOAD_AND_SHOOT );
    } else if( count_by_charges() && !has_infinite_charges() ) {
        // A chargeable item
        amount = charges;
        max_amount = ammo_capacity();
    } else if( is_battery() ) {
        show_amt = true;
        amount = to_joule( energy_remaining() );
        max_amount = to_joule( type->battery->max_capacity );
    }

    std::string ammotext;
    if( ( ( is_gun() && ammo_required() ) || is_magazine() ) && get_option<bool>( "AMMO_IN_NAMES" ) ) {
        if( !ammo_current().is_null() ) {
            ammotext = ammo_current()->nname( 1 );
        } else {
            ammotext = ammotype( *ammo_types().begin() )->name();
        }
    }

    if( amount || show_amt ) {
        if( is_money() ) {
            amt = string_format( " $%.2f", amount / 100.0 );
        } else {
            if( !ammotext.empty() ) {
                ammotext = " " + ammotext;
            }

            if( max_amount != 0 ) {
                if( !contains ) {
                    int percentage = ( static_cast<float>( amount ) / max_amount ) * 100;
                    nc_color color = c_green;
                    if( percentage >= 100 ) {
                        color = c_green;
                    } else if( percentage >= 75 ) {
                        color = c_light_green;
                    } else if( percentage >= 50 ) {
                        color = c_yellow;
                    } else if( percentage >= 25 ) {
                        color = c_light_red;
                    } else if( percentage > 0 ) {
                        color = c_red;
                    } else if( percentage <= 0 ) {
                        color = c_light_gray;
                    }
                    amt = colorize( string_format( " (%i/%i%s)", amount, max_amount, ammotext ),
                                    color );
                } else {
                    amt = string_format( " (%i/%i%s)", amount, max_amount, ammotext );
                }

            } else {
                amt = string_format( " (%i%s)", amount, ammotext );
            }
        }
    } else if( !ammotext.empty() ) {
        amt = " (" + ammotext + ")";
    }

    // HACK: This is a hack to prevent possible crashing when displaying maps as items during character creation
    if( is_map() && calendar::turn != calendar::turn_zero ) {
        const auto map_pos_omt = get_var( "reveal_map_center_omt", you.abs_omt_pos() );
        const auto map_pos = project_to<coords::sm>( map_pos_omt );
        const city *c = get_overmapbuffer( you.get_dimension() ).closest_city( map_pos ).city;
        if( c != nullptr ) {
            name = string_format( "%s %s", c->name, name );
        }
    }

    return string_format( "%s%s%s", name, sidetxt, amt );
}

std::string item::debug_name() const
{
    return typeId().str();
}

nc_color item::color() const
{
    if( is_null() ) {
    return c_black;
}
if( is_corpse() ) {
    return corpse->color;
}
return type->color;
}
