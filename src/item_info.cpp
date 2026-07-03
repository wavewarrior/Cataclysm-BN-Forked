// Item info display: item::info() and its per-domain *_info() builders,
// split out of item.cpp to shrink that translation unit. .cpp-only, no API changes.

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

auto rad_badge_color( const int rad ) -> std::string
{
    using pair_t = std::pair<const int, const translation>;

    static const std::array<pair_t, 6> values = {{
        pair_t{0,   to_translation( "color", "green" )},
        pair_t{30,  to_translation( "color", "blue" )},
        pair_t{60,  to_translation( "color", "yellow" )},
        pair_t{120, to_translation( "color", "orange" )},
        pair_t{240, to_translation( "color", "red" )},
        pair_t{500, to_translation( "color", "black" )},
    }};

    for( const auto &i : values ) {
        if( rad <= i.first ) {
            return i.second.translated();
        }
    }

    return values.back().second.translated();
}

// File-scope id constants (moved with the info methods; internal linkage).
static const ammo_effect_str_id ammo_effect_BLACKPOWDER( "BLACKPOWDER" );
static const ammo_effect_str_id ammo_effect_INCENDIARY( "INCENDIARY" );
static const ammo_effect_str_id ammo_effect_NEVER_MISFIRES( "NEVER_MISFIRES" );
static const ammo_effect_str_id ammo_effect_RECYCLED( "RECYCLED" );
static const bionic_id bio_digestion( "bio_digestion" );
static const itype_id itype_rad_badge( "rad_badge" );
static const quality_id qual_JACK( "JACK" );
static const quality_id qual_LIFT( "LIFT" );
static const skill_id skill_survival( "survival" );
static const skill_id skill_throw( "throw" );
static const trait_id trait_CARNIVORE( "CARNIVORE" );
static const trait_id trait_ILLITERATE( "ILLITERATE" );
static const trait_id trait_SAPROVORE( "SAPROVORE" );
static const trait_id trait_WOOLALLERGY( "WOOLALLERGY" );
static const trait_flag_str_id trait_flag_CANNIBAL( "CANNIBAL" );
static const vitamin_id vitamin_human_flesh_vitamin( "human_flesh_vitamin" );

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

// Generates a long-form description of the freshness of the given rottable food item.
// NB: Doesn't check for non-rottable!
static std::string get_freshness_description( const item &food_item )
{
    // So, skilled characters looking at food that is neither super-fresh nor about to rot
    // can guess its age as one of {quite fresh,midlife,past midlife,old soon}, and also
    // guess about how long until it spoils.
    const double rot_progress = food_item.get_relative_rot();
    const time_duration shelf_life = food_item.get_shelf_life();
    time_duration time_left = shelf_life - ( shelf_life * rot_progress );

    // Correct for an estimate that exceeds shelf life -- this happens especially with
    // fresh items.
    if( time_left > shelf_life ) {
        time_left = shelf_life;
    }
    avatar &you = get_avatar();
    if( food_item.is_fresh() ) {
        // Fresh food is assumed to be obviously so regardless of skill.
        if( you.can_estimate_rot() ) {
            return string_format( _( "* This food looks as <good>fresh</good> as it can be.  "
                                     "It still has <info>%s</info> until it spoils." ),
                                  to_string_approx( time_left ) );
        } else {
            return _( "* This food looks as <good>fresh</good> as it can be." );
        }
    } else if( food_item.is_going_bad() ) {
        // Old food likewise is assumed to be fairly obvious.
        if( you.can_estimate_rot() ) {
            return string_format( _( "* This food looks <bad>old</bad>.  "
                                     "It's just <info>%s</info> from becoming inedible." ),
                                  to_string_approx( time_left ) );
        } else {
            return _( "* This food looks <bad>old</bad>.  "
                      "It's on the brink of becoming inedible." );
        }
    }

    if( !you.can_estimate_rot() ) {
        // Unskilled characters only get a hint that more information exists...
        return _( "* This food looks <info>fine</info>.  If you were more skilled in "
                  "cooking or survival, you might be able to make a better estimation." );
    }

    // Otherwise, a skilled character can determine the below options:
    if( rot_progress < 0.3 ) {
        //~ here, %s is an approximate time span, e.g., "over 2 weeks" or "about 1 season"
        return string_format( _( "* This food looks <good>quite fresh</good>.  "
                                 "It has <info>%s</info> until it spoils." ),
                              to_string_approx( time_left ) );
    } else if( rot_progress < 0.5 ) {
        //~ here, %s is an approximate time span, e.g., "over 2 weeks" or "about 1 season"
        return string_format( _( "* This food looks like it is reaching its <neutral>midlife</neutral>.  "
                                 "There's <info>%s</info> before it spoils." ),
                              to_string_approx( time_left ) );
    } else if( rot_progress < 0.7 ) {
        //~ here, %s is an approximate time span, e.g., "over 2 weeks" or "about 1 season"
        return string_format( _( "* This food looks like it has <neutral>passed its midlife</neutral>.  "
                                 "Edible, but will go bad in <info>%s</info>." ),
                              to_string_approx( time_left ) );
    } else {
        //~ here, %s is an approximate time span, e.g., "over 2 weeks" or "about 1 season"
        return string_format( _( "* This food looks like it <bad>will be old soon</bad>.  "
                                 "It has <info>%s</info>, so if you plan to use it, it's now or never." ),
                              to_string_approx( time_left ) );
    }
}

static int get_base_env_resist( const item &it )
{
    const islot_armor *armor = it.find_armor_data();
    if( armor == nullptr ) {
        if( it.is_pet_armor() ) {
            return it.type->pet_armor->env_resist * it.get_relative_health();
        } else {
            return 0;
        }
    }

    return armor->env_resist * it.get_relative_health();
}

namespace
{
auto nname( const itype_id &id ) -> std::string
{
    return item::nname( id );
}
} // namespace


void item::basic_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int batch,
                       bool debug /* debug */ ) const
{
    if( display_mod_source && parts->test( iteminfo_parts::BASE_MOD_SRC ) ) {
    info.emplace_back( "BASE", string_format( _( "<stat>Origin: %s</stat>" ),
                       enumerate_as_string( type->src.begin(),
    type->src.end(), []( const std::pair<itype_id, mod_id> &source ) {
        return string_format( "'%s'", source.second->name() );
        }, enumeration_conjunction::arrow ) ) );
        insert_separation_line( info );
    }
    if( display_object_ids && parts->test( iteminfo_parts::BASE_ID ) ) {
    info.emplace_back( "BASE", colorize( string_format( "[%s]", type->get_id() ), c_light_blue ) );
        insert_separation_line( info );
    }

    const std::string space = "  ";
    if( parts->test( iteminfo_parts::BASE_MATERIAL ) ) {
    const std::vector<const material_type *> mat_types = made_of_types();
        if( !mat_types.empty() ) {
            const std::string material_list = enumerate_as_string( mat_types.begin(), mat_types.end(),
            []( const material_type * material ) {
                return string_format( "<stat>%s</stat>", material->name() );
            }, enumeration_conjunction::none );
            info.emplace_back( "BASE", string_format( _( "Material: %s" ), material_list ) );
        }
    }
    if( parts->test( iteminfo_parts::BASE_VOLUME ) ) {
    int converted_volume_scale = 0;
    const double converted_volume = round_up( convert_volume( volume().value(),
                                    &converted_volume_scale ) * batch, 3 );
        iteminfo::flags f = iteminfo::lower_is_better | iteminfo::no_newline;
        if( converted_volume_scale != 0 ) {
            f |= iteminfo::is_three_decimal;
        }
        info.emplace_back( "BASE", _( "Volume: " ),
                           string_format( "<num> %s", volume_units_abbr() ),
                           f, converted_volume );
    }
    if( parts->test( iteminfo_parts::BASE_WEIGHT ) ) {
    info.emplace_back( "BASE", space + _( "Weight: " ),
                       string_format( "<num> %s", weight_units() ),
                       iteminfo::lower_is_better | iteminfo::is_decimal,
                       convert_weight( weight() ) * batch );
    }
    if( !owner.is_null() ) {
    info.emplace_back( "BASE", string_format( _( "Owner: %s" ),
                       _( get_owner_name() ) ) );
    }
    if( parts->test( iteminfo_parts::BASE_CATEGORY ) ) {
    info.emplace_back( "BASE", _( "Category: " ),
                       "<header>" + get_category().name() + "</header>" );
    }
    if( !type->weapon_category.empty() && parts->test( iteminfo_parts::WEAPON_CATEGORY ) ) {
    const std::string weapon_categories = enumerate_as_string( type->weapon_category.begin(),
    type->weapon_category.end(), [&]( const weapon_category_id & elem ) {
        return elem->name().translated();
        }, enumeration_conjunction::none );
        info.emplace_back( "BASE", _( "Weapon Category: " ),
                           "<header>" + weapon_categories + "</header>" );
    }

    if( has_var( TINT_COLOR_VAR_NAME ) && !get_use( iuse_paint_stuff::IUSE_ACTION ) ) {
    const auto c = get_var<RGBColor>( TINT_COLOR_VAR_NAME, {} );
        const auto fg = get_var<RGBColor>( TINT_COLOR_FG_VAR_NAME, c );
        const auto bg = get_var<RGBColor>( TINT_COLOR_BG_VAR_NAME, c );
        if( fg != RGBColor{} || bg != RGBColor{} ) {
            if( bg != fg ) {
                info.emplace_back( "TOOL", string_format( _( "Painted With: <bold>%s, %s</bold>" ),
                                   bg.friendly_name(), fg.friendly_name() ) );
            } else if( bg == RGBColor{} ) {
                info.emplace_back( "TOOL", string_format( _( "Painted With: <bold>%s</bold>" ),
                                   fg.friendly_name() ) );
            } else {
                info.emplace_back( "TOOL", string_format( _( "Painted With: <bold>%s</bold>" ),
                                   bg.friendly_name() ) );
            }
        }
    }

    if( parts->test( iteminfo_parts::DESCRIPTION ) ) {
    insert_separation_line( info );
        const auto idescription = item_vars_.find( "description" );
        const std::optional<translation> snippet = SNIPPET.get_snippet_by_id( snip_id );
        if( snippet.has_value() && ( !get_avatar().has_trait( trait_ILLITERATE ) ||
                                     !has_flag( flag_SNIPPET_NEEDS_LITERACY ) ) ) {
            // Just use the dynamic description
            info.emplace_back( "DESCRIPTION", snippet.value().translated() );
            // only ever do the effect for a snippet the first time you see it
            if( !get_avatar().has_seen_snippet( snip_id ) ) {
                //note that you have seen the snippet
                get_avatar().add_snippet( snip_id );
            }
        } else if( idescription != item_vars_.end() ) {
            info.emplace_back( "DESCRIPTION", idescription->second );
        } else {
            if( is_craft() ) {
                const std::string desc = _( "This is an in progress %s.  "
                                            "It is %d percent complete." );
                const int percent_progress = item_counter / 100000;
                info.emplace_back( "DESCRIPTION", string_format( desc,
                                   craft_data_->making->result_name(),
                                   percent_progress ) );
            } else {
                info.emplace_back( "DESCRIPTION", type->description.translated() );
            }
        }
        const auto item_note = item_vars_.find( "item_note" );
        const auto item_note_tool = item_vars_.find( "item_note_tool" );

        if( item_note != item_vars_.end() && parts->test( iteminfo_parts::DESCRIPTION_NOTES ) ) {
            std::string ntext;
            const inscribe_actor *use_actor = nullptr;
            if( item_note_tool != item_vars_.end() ) {
                const use_function *use_func = itype_id( item_note_tool->second )->get_use( "inscribe" );
                use_actor = dynamic_cast<const inscribe_actor *>( use_func->get_actor_ptr() );
            }
            if( use_actor ) {
                //~ %1$s: gerund (e.g. carved), %2$s: item name, %3$s: inscription text
                ntext = string_format( pgettext( "carving", "<info>%1$s on the %2$s is:</info> %3$s" ),
                                       use_actor->gerund, tname(), item_note->second );
            } else {
                //~ %1$s: inscription text
                ntext = string_format( pgettext( "carving", "Note: %1$s" ), item_note->second );
            }
            info.emplace_back( "DESCRIPTION", ntext );
        }
        insert_separation_line( info );
    }

    insert_separation_line( info );

    if( parts->test( iteminfo_parts::BASE_REQUIREMENTS ) ) {
    // Display any minimal stat or skill requirements for the item
    std::vector<std::string> req;
    if( get_min_str() > 0 ) {
            avatar &viewer = get_avatar();
            if( has_flag( flag_STR_DRAW ) && ranged::get_str_draw_penalty( *this, viewer ) < 1.0f ) {
                if( ranged::get_str_draw_penalty( *this, viewer ) < 0.5f ) {
                    req.push_back( string_format( _( "%s %d <color_magenta>(Can't use!)</color>" ), _( "strength" ),
                                                  get_min_str() ) );
                } else if( ranged::get_str_draw_penalty( *this, viewer ) < 0.75f ) {
                    req.push_back( string_format( "%s %d <color_red>(Damage/Range 0.5x, Dispersion 2.0x)</color>",
                                                  _( "strength" ), get_min_str() ) );
                } else {
                    req.push_back( string_format( "%s %d <color_yellow>(Damage/Range 0.75x)</color>", _( "strength" ),
                                                  get_min_str() ) );
                }
            } else {
                req.push_back( string_format( "%s %d", _( "strength" ), get_min_str() ) );
            }
        }
        if( type->min_dex > 0 ) {
            req.push_back( string_format( "%s %d", _( "dexterity" ), type->min_dex ) );
        }
        if( type->min_int > 0 ) {
            req.push_back( string_format( "%s %d", _( "intelligence" ), type->min_int ) );
        }
        if( type->min_per > 0 ) {
            req.push_back( string_format( "%s %d", _( "perception" ), type->min_per ) );
        }
        for( const std::pair<const skill_id, int> sk : sorted_lex( type->min_skills ) ) {
            req.push_back( string_format( "%s %d", skill_id( sk.first )->name(), sk.second ) );
        }
        if( !req.empty() ) {
            info.emplace_back( "BASE", _( "<bold>Minimum requirements</bold>:" ) );
            info.emplace_back( "BASE", enumerate_as_string( req ) );
            insert_separation_line( info );
        }
    }

    if( has_var( "contained_name" ) && parts->test( iteminfo_parts::BASE_CONTENTS ) ) {
    info.emplace_back( "BASE", string_format( _( "Contains: %s" ),
                       get_var( "contained_name" ) ) );
    }
    if( count_by_charges() && !is_food() && !is_medication() &&
        parts->test( iteminfo_parts::BASE_AMOUNT ) ) {
    info.emplace_back( "BASE", _( "Amount: " ), "<num>", iteminfo::no_flags,
                       charges * batch );
    }
    if( debug && parts->test( iteminfo_parts::BASE_DEBUG ) ) {
    if( g != nullptr ) {
            info.emplace_back( "BASE", string_format( "itype_id: %s",
                               typeId().c_str() ) );
            info.emplace_back( "BASE", _( "age (hours): " ), "", iteminfo::lower_is_better,
                               to_hours<int>( age() ) );
            info.emplace_back( "BASE", _( "charges: " ), "", iteminfo::lower_is_better,
                               charges );
            info.emplace_back( "BASE", _( "damage: " ), "", iteminfo::lower_is_better,
                               damage_ );
            info.emplace_back( "BASE", _( "active: " ), "", iteminfo::lower_is_better,
                               is_active() );
            info.emplace_back( "BASE", _( "burn: " ), "", iteminfo::lower_is_better,
                               burnt );

            static const auto f = []( const flag_id & f ) -> std::string { return f.str(); };
            const std::string itype_tags_listed = enumerate_as_string( type->item_tags, f,
                                                  enumeration_conjunction::none );
            info.emplace_back( "BASE", string_format( _( "itype tags: %s" ), itype_tags_listed ) );

            const std::string tags_listed = enumerate_as_string( item_tags, f, enumeration_conjunction::none );
            info.emplace_back( "BASE", string_format( _( "tags: %s" ), tags_listed ) );

            for( auto const &imap : item_vars_ ) {
                info.emplace_back( "BASE",
                                   string_format( _( "item var: %s, %s" ), imap.first,
                                                  imap.second ) );
            }

            const item *food = get_food();
            if( food && food->goes_bad() ) {
                info.emplace_back( "BASE", _( "age (turns): " ),
                                   "", iteminfo::lower_is_better,
                                   to_turns<int>( food->age() ) );
                info.emplace_back( "BASE", _( "rot (turns): " ),
                                   "", iteminfo::lower_is_better,
                                   to_turns<int>( food->rot ) );
                info.emplace_back( "BASE", space + _( "shelf life (turns): " ),
                                   "", iteminfo::lower_is_better,
                                   to_turns<int>( food->get_shelf_life() ) );
                info.emplace_back( "BASE", _( "last rot: " ),
                                   "", iteminfo::lower_is_better,
                                   to_turn<int>( food->last_rot_check ) );
            }
        }
    }
}
void item::book_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int /* batch */,
                      bool /* debug */ ) const
{
    if( !is_book() ) {
    return;
}

Character &character = get_player_character();

insert_separation_line( info );
const islot_book &book = *type->book;
// Some things about a book you CAN tell by it's cover.
if( !book.skill && !type->can_use( "MA_MANUAL" ) && parts->test( iteminfo_parts::BOOK_SUMMARY ) ) {
    info.emplace_back( "BOOK", _( "Just for fun." ) );
    }
    if( type->can_use( "MA_MANUAL" ) && parts->test( iteminfo_parts::BOOK_SUMMARY ) ) {
    info.emplace_back( "BOOK",
                       _( "Some sort of <info>martial arts training "
                          "manual</info>." ) );
        const matype_id style_to_learn = martial_art_learned_from( *type );
        info.emplace_back( "BOOK",
                           string_format( _( "You can learn <info>%s</info> style "
                                             "from it." ), style_to_learn->name ) );
        info.emplace_back( "BOOK",
                           string_format( _( "This fighting style is <info>%s</info> "
                                             "to learn." ),
                                          martialart_difficulty( style_to_learn ) ) );
        info.emplace_back( "BOOK",
                           string_format( _( "It'd be easier to master if you'd have "
                                             "skill expertise in <info>%s</info>." ),
                                          style_to_learn->primary_skill->name() ) );
    }
    if( book.req == 0 && parts->test( iteminfo_parts::BOOK_REQUIREMENTS_BEGINNER ) ) {
    info.emplace_back( "BOOK", _( "It can be <info>understood by "
                       "beginners</info>." ) );
    }
    avatar &you = get_avatar();
    if( !you.has_identified( typeId() ) && parts->test( iteminfo_parts::BOOK_UNREAD ) ) {
        info.emplace_back( "BOOK",
                           _( "You have <info>never read</info> this book." ) );
    }
    if( book.skill ) {
    const SkillLevel &skill = you.get_skill_level_object( book.skill );
        if( parts->test( iteminfo_parts::BOOK_SKILLRANGE_MAX ) ) {
            const std::string skill_name = book.skill->name();
            const std::string fmt = string_format( _( "Can bring <info>%s skill to</info> "
                                                      "<num>." ), skill_name );
            info.emplace_back( "BOOK", "", skill.can_train() ? fmt : colorize( fmt, c_brown ),
                               iteminfo::no_flags, book.level );
            info.emplace_back( "BOOK", "",
                               string_format( _( "Your current <stat>%s skill</stat> is <num>." ), skill_name ),
                               iteminfo::no_flags, skill.level() );
        }

        if( book.req != 0 && parts->test( iteminfo_parts::BOOK_SKILLRANGE_MIN ) ) {
            const std::string fmt = string_format(
                                        _( "<info>Requires %s level</info> <num> to "
                                           "understand." ), book.skill.obj().name() );
            info.emplace_back( "BOOK", "", fmt,
                               iteminfo::lower_is_better, book.req );
        }
    }

    if( book.intel != 0 && parts->test( iteminfo_parts::BOOK_REQUIREMENTS_INT ) ) {
    info.emplace_back( "BOOK", "",
                       _( "Requires <info>intelligence of</info> <num> to easily "
                          "read." ), iteminfo::lower_is_better, book.intel );
    }
    if( character_funcs::get_book_fun_for( character, *this ) != 0 &&
        parts->test( iteminfo_parts::BOOK_MORALECHANGE ) ) {
    info.emplace_back( "BOOK", "",
                       _( "Reading this book affects your morale by <num>" ),
                       iteminfo::show_plus, character_funcs::get_book_fun_for( character, *this ) );
    }
    if( parts->test( iteminfo_parts::BOOK_TIMEPERCHAPTER ) ) {
    std::string fmt = vgettext(
                          "A chapter of this book takes <num> <info>minute to "
                          "read</info>.",
                          "A chapter of this book takes <num> <info>minutes to "
                          "read</info>.", book.time );
        if( type->use_methods.contains( "MA_MANUAL" ) ) {
            fmt = vgettext(
                      "<info>A training session</info> with this book takes "
                      "<num> <info>minute</info>.",
                      "<info>A training session</info> with this book takes "
                      "<num> <info>minutes</info>.", book.time );
        }
        info.emplace_back( "BOOK", "", fmt,
                           iteminfo::lower_is_better, book.time );
    }

    if( book.chapters > 0 && parts->test( iteminfo_parts::BOOK_NUMUNREADCHAPTERS ) ) {
    const int unread = get_remaining_chapters( you );
        std::string fmt = vgettext( "This book has <num> <info>unread chapter</info>.",
                                    "This book has <num> <info>unread chapters</info>.",
                                    unread );
        info.emplace_back( "BOOK", "", fmt, iteminfo::no_flags, unread );
    }

    std::vector<std::string> recipe_list;
for( const book_recipe &elem : book.recipes ) {
    const bool knows_it = you.knows_recipe( elem.recipe );
        const bool can_learn = you.get_skill_level( elem.recipe->skill_used )  >= elem.skill_level;
        // If the player knows it, they recognize it even if it's not clearly stated.
        if( elem.is_hidden() && !knows_it ) {
            continue;
        }
        if( knows_it ) {
            // In case the recipe is known, but has a different name in the book, use the
            // real name to avoid confusing the player.
            const std::string name = elem.recipe->result_name( /*decorated=*/true );
            recipe_list.push_back( "<bold>" + name + "</bold>" );
        } else if( !can_learn ) {
            recipe_list.push_back( "<color_brown>" + elem.name.translated() + "</color>" );
        } else {
            recipe_list.push_back( "<dark>" + elem.name.translated() + "</dark>" );
        }
    }

    if( !recipe_list.empty() && parts->test( iteminfo_parts::DESCRIPTION_BOOK_RECIPES ) ) {
    std::string recipe_line =
        string_format( vgettext( "This book contains %1$d crafting recipe: %2$s",
                                 "This book contains %1$d crafting recipes: %2$s",
                                 recipe_list.size() ),
                       recipe_list.size(), enumerate_as_string( recipe_list ) );

        insert_separation_line( info );
        info.emplace_back( "DESCRIPTION", recipe_line );
    }

    if( recipe_list.size() != book.recipes.size() &&
        parts->test( iteminfo_parts::DESCRIPTION_BOOK_ADDITIONAL_RECIPES ) ) {
    info.emplace_back( "DESCRIPTION",
                       _( "It might help you figuring out some <good>more "
                          "recipes</good>." ) );
    }
}

void item::container_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int /*batch*/,
                           bool /*debug*/ ) const
{
    if( !is_container() || !parts->test( iteminfo_parts::CONTAINER_DETAILS ) ) {
    return;
}

insert_separation_line( info );
const islot_container &c = *type->container;

std::string container_str =  _( "This container " );

if( c.seals ) {
    container_str += _( "can be <info>resealed</info>, " );
    }
    if( c.watertight ) {
    container_str += _( "is <info>watertight</info>, " );
    }
    if( c.preserves ) {
    container_str += _( "<good>prevents spoiling</good>, " );
    }

    container_str += string_format( _( "can store <info>%s %s</info>." ),
                                    format_volume( c.contains ), volume_units_long() );

    info.emplace_back( "CONTAINER", container_str );
}

void item::battery_info( std::vector<iteminfo> &info, const iteminfo_query * /*parts*/,
                         int /*batch*/, bool /*debug*/ ) const
{
    if( !is_battery() ) {
    return;
}

std::string info_string;
if( type->battery->max_capacity < 1_kJ ) {
    info_string = string_format( _( "<bold>Capacity</bold>: %dJ" ),
                                 to_joule( type->battery->max_capacity ) );
    } else if( type->battery->max_capacity >= 1_kJ ) {
    info_string = string_format( _( "<bold>Capacity</bold>: %dkJ" ),
                                 to_kilojoule( type->battery->max_capacity ) );
    }
    insert_separation_line( info );
    info.emplace_back( "BATTERY", info_string );
}

void item::tool_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int /*batch*/,
                      bool /*debug*/ ) const
{
    if( !is_tool() ) {
    return;
}

insert_separation_line( info );
if( ammo_capacity() != 0 && parts->test( iteminfo_parts::TOOL_CHARGES ) ) {
    info.emplace_back( "TOOL", string_format( _( "<bold>Charges</bold>: %d" ),
                       ammo_remaining() ) );
    }

    if( !magazine_integral() ) {
    if( magazine_current() && parts->test( iteminfo_parts::TOOL_MAGAZINE_CURRENT ) ) {
            info.emplace_back( "TOOL", _( "Magazine: " ),
                               string_format( "<stat>%s</stat>", magazine_current()->tname() ) );
        }

        if( parts->test( iteminfo_parts::TOOL_MAGAZINE_COMPATIBLE ) ) {
            const auto &compat = magazine_compatible();
            if( !compat.empty() ) {
                info.emplace_back( "TOOL", _( "Compatible magazines: " )
                                   + enumerate_as_string( compat, ::nname ) );
            }
        }
    } else if( ammo_capacity() != 0 && parts->test( iteminfo_parts::TOOL_CAPACITY ) ) {
    std::string tmp;
    bool bionic_tool = has_flag( flag_USES_BIONIC_POWER );
        if( !ammo_types().empty() ) {
            //~ "%s" is ammunition type. This types can't be plural.
            tmp = vgettext( "Maximum <num> charge of %s.", "Maximum <num> charges of %s.",
                            ammo_capacity() );
            tmp = string_format( tmp, enumerate_as_string( ammo_types().begin(),
            ammo_types().end(), []( const ammotype & at ) {
                return at->name();
            }, enumeration_conjunction::none ) );

            // No need to display max charges, since charges are always equal to bionic power
        } else if( !bionic_tool ) {
            tmp = vgettext( "Maximum <num> charge.", "Maximum <num> charges.", ammo_capacity() );
        }
        if( !bionic_tool ) {
            info.emplace_back( "TOOL", "", tmp, iteminfo::no_flags, ammo_capacity() );
        }
    }
    if( type->tool->ups_eff_mult != 1 ) {
    info.emplace_back( "TOOL", _( "UPS Efficiency Multiplier: " ),
                       string_format( "<stat>%s</stat>", type->tool->ups_eff_mult ) );

    }
}

void item::component_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int /*batch*/,
                           bool /*debug*/ ) const
{
    if( components.empty() || !parts->test( iteminfo_parts::DESCRIPTION_COMPONENTS_MADEFROM ) ) {
    return;
}
if( is_craft() ) {
    info.emplace_back( "DESCRIPTION", string_format( _( "Using: %s" ),
                       _( components_to_string() ) ) );
        // Ugly hack warning! Corpses have CBMs as their components
    } else if( !is_corpse() ) {
    info.emplace_back( "DESCRIPTION", string_format( _( "Made from: %s" ),
                       _( components_to_string() ) ) );
    } else if( get_var( "bionics_scanned_by", -1 ) == get_avatar().getID().get_value() ) {
    // TODO: Extract into a more proper place (function in namespace)
    std::string bionics_string = enumerate_as_string( components.begin(), components.end(),
    []( const item * const & entry ) -> std::string {
        return entry->is_bionic() ? entry->display_name() : "";
    }, enumeration_conjunction::none );
        info.emplace_back( "DESCRIPTION", string_format( _( "Contains: %s" ),
                           bionics_string ) );
    }
}

void item::repair_info( std::vector<iteminfo> &info, const iteminfo_query *parts,
                        int /*batch*/, bool /*debug*/ ) const
{
    if( !parts->test( iteminfo_parts::DESCRIPTION_REPAIREDWITH ) ) {
    return;
}
insert_separation_line( info );
const std::vector<itype_id> &rep = sorted_lex( repaired_with() );
if( !rep.empty() ) {
    info.emplace_back( "DESCRIPTION", string_format( _( "<bold>Repair</bold> using %s." ),
    enumerate_as_string( rep.begin(), rep.end(), []( const itype_id & e ) {
        return nname( e );
        }, enumeration_conjunction::or_ ) ) );
        if( reinforceable() ) {
            info.emplace_back( "DESCRIPTION", _( "* This item can be <good>reinforced</good>." ) );
        }
    } else {
        info.emplace_back( "DESCRIPTION", _( "* This item is <bad>not repairable</bad>." ) );
    }
}

void item::disassembly_info( std::vector<iteminfo> &info, const iteminfo_query *parts,
                             int /*batch*/, bool /*debug*/ ) const
{
    if( !components.empty() && parts->test( iteminfo_parts::DESCRIPTION_COMPONENTS_MADEFROM ) ) {
    return;
}
if( !parts->test( iteminfo_parts::DESCRIPTION_COMPONENTS_DISASSEMBLE ) ) {
    return;
}

const recipe &dis = recipe_dictionary::get_uncraft( typeId() );
const requirement_data &req = dis.disassembly_requirements();
if( !req.is_empty() ) {
    const std::string approx_time = to_string_approx( time_duration::from_turns( dis.time / 100 ) );

        const requirement_data::alter_item_comp_vector &comps_list = req.get_components();
        const std::string comps_str = enumerate_as_string( comps_list.begin(), comps_list.end(),
        []( const std::vector<item_comp> &comp_opts ) {
            return comp_opts.front().to_string();
        } );

        std::vector<std::string> reqs_list;
        const requirement_data::alter_tool_comp_vector &tools_list = req.get_tools();
        for( const std::vector<tool_comp> &it : tools_list ) {
            if( !it.empty() ) {
                reqs_list.push_back( it.front().to_string() );
            }
        }
        const requirement_data::alter_quali_req_vector &quals_list = req.get_qualities();
        for( const std::vector<quality_requirement> &it : quals_list ) {
            if( !it.empty() ) {
                reqs_list.push_back( it.front().to_colored_string() );
            }
        }

        std::string descr;
        if( reqs_list.empty() ) {
            //~ 1 is approx. time (e.g. 'about 5 minutes'), 2 is a list of items
            descr = string_format( _( "<bold>Disassembly</bold> takes %1$s and might yield: %2$s." ),
                                   approx_time, comps_str );
        } else {
            const std::string reqs_str = enumerate_as_string( reqs_list );
            descr = string_format(
                        //~ 1 is approx. time, 2 is a list of items and tools with qualities, 3 is a list of items.
                        //~ Bold text in the middle makes it easier to see where the second list starts.
                        _( "<bold>Disassembly</bold> takes %1$s, requires %2$s and <bold>might yield</bold>: %3$s." ),
                        approx_time, reqs_str, comps_str );
        }

        insert_separation_line( info );
        info.emplace_back( "DESCRIPTION", descr );
    }
}

void item::qualities_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int /*batch*/,
                           bool /*debug*/ ) const
{
    auto name_quality = [&info]( const std::pair<quality_id, int> &q ) {
        std::string str;
        if( q.first == qual_JACK || q.first == qual_LIFT ) {
            str = string_format( _( "Has level <info>%1$d %2$s</info> quality and "
                                    "is rated at <info>%3$d</info> %4$s" ),
                                 q.second, q.first.obj().name,
                                 static_cast<int>( convert_weight( q.second * TOOL_LIFT_FACTOR ) ),
                                 weight_units() );
        } else {
            str = string_format( _( "Has level <info>%1$d %2$s</info> quality." ),
                                 q.second, q.first.obj().name );
        }
        info.emplace_back( "QUALITIES", "", str );
    };

    if( parts->test( iteminfo_parts::QUALITIES ) ) {
        insert_separation_line( info );
        for( const std::pair<const quality_id, int> q : sorted_lex( type->qualities ) ) {
            name_quality( q );
        }
        auto crafting_speed_modifier = type->crafting_speed_modifier;
        std::ranges::for_each( type->qualities, [&]( const auto & quality_entry ) {
            const auto &quality = quality_entry.first.obj();
            const auto per_level_multiplier = quality.crafting_speed_bonus_per_level;
            if( per_level_multiplier <= 0.0f ) {
                return;
            }
            const auto extra_levels =
                quality_entry.second - quality.crafting_speed_level_offset;
            if( extra_levels <= 0 ) {
                return;
            }
            crafting_speed_modifier *= std::pow( per_level_multiplier, extra_levels );
        } );

        if( crafting_speed_modifier != 1.0f ) {
            const auto modifier_percent = static_cast<int>( crafting_speed_modifier * 100.0f );
            info.emplace_back( "QUALITIES", "",
                               string_format( _( "This item modifies crafting speed by <info>%d%%</info> when used in recipes." ),
                                              modifier_percent ) );
        }
    }

    if( parts->test( iteminfo_parts::QUALITIES_CONTAINED ) &&
    contents.has_any_with( []( const item & e ) {
    return !e.type->qualities.empty();
    } ) ) {

        info.emplace_back( "QUALITIES", "", _( "Contains items with qualities:" ) );
        std::map<quality_id, int, quality_id::LexCmp> most_quality;
        for( const item *e : contents.all_items_top() ) {
            for( const std::pair<const quality_id, int> &q : e->type->qualities ) {
                auto emplace_result = most_quality.emplace( q );
                if( !emplace_result.second &&
                    most_quality.at( emplace_result.first->first ) < q.second ) {
                    most_quality[ q.first ] = q.second;
                }
            }
        }
        for( const std::pair<const quality_id, int> &q : most_quality ) {
            name_quality( q );
        }
    }
}
void item::contents_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int batch,
                          bool debug ) const
{
    if( contents.empty() || !parts->test( iteminfo_parts::DESCRIPTION_CONTENTS ) ) {
    return;
}
const std::string space = "  ";

for( const item *mod : is_gun() ? gunmods() : toolmods() ) {
    std::string mod_str;
    if( mod->type->gunmod ) {
            if( mod->is_irremovable() ) {
                mod_str = _( "Integrated mod: " );
            } else {
                mod_str = _( "Mod: " );
            }
            mod_str += string_format( "<bold>%s</bold> (%s) ", mod->tname(),
                                      mod->type->gunmod->location.name() );
        }
        insert_separation_line( info );
        info.emplace_back( "DESCRIPTION", mod_str );
        info.emplace_back( "DESCRIPTION", mod->type->description.translated() );
    }
    bool contents_header = false;
for( const item *contents_item : contents.all_items_top() ) {
    if( !contents_item->type->mod ) {
            if( !contents_header ) {
                insert_separation_line( info );
                info.emplace_back( "DESCRIPTION", _( "<bold>Contents of this item</bold>:" ) );
                contents_header = true;
            } else {
                // Separate items with a blank line
                info.emplace_back( "DESCRIPTION", space );
            }

            const translation &description = contents_item->type->description;

            if( contents_item->made_of( LIQUID ) ) {
                units::volume contents_volume = contents_item->volume() * batch;
                int converted_volume_scale = 0;
                const double converted_volume =
                    round_up( convert_volume( contents_volume.value(),
                                              &converted_volume_scale ), 2 );
                info.emplace_back( "DESCRIPTION", contents_item->display_name() );
                iteminfo::flags f = iteminfo::no_newline;
                if( display_mod_source ) {
                    info.emplace_back( "DESCRIPTION", string_format( _( "<stat>Origin: %s</stat>" ),
                                       enumerate_as_string( contents_item->type->src.begin(),
                    contents_item->type->src.end(), []( const std::pair<itype_id, mod_id> &content_source ) {
                        return string_format( "'%s'", content_source.second->name() );
                    }, enumeration_conjunction::arrow ) ) );
                }
                if( display_object_ids ) {
                    info.emplace_back( "DESCRIPTION", colorize(
                                           string_format( "[%s]", contents_item->type->get_id() ),
                                           c_light_blue ) );
                }
                if( converted_volume_scale != 0 ) {
                    f |= iteminfo::is_decimal;
                }
                info.emplace_back( "CONTAINER", description + space,
                                   string_format( "<num> %s", volume_units_abbr() ), f,
                                   converted_volume );
            } else {
                info.emplace_back( "DESCRIPTION", contents_item->display_name() );
                if( display_mod_source ) {
                    info.emplace_back( "DESCRIPTION", string_format( _( "<stat>Origin: %s</stat>" ),
                                       enumerate_as_string( contents_item->type->src.begin(),
                    contents_item->type->src.end(), []( const std::pair<itype_id, mod_id> &content_source ) {
                        return string_format( "'%s'", content_source.second->name() );
                    }, enumeration_conjunction::arrow ) ) );
                }
                if( display_object_ids ) {
                    info.emplace_back( "DESCRIPTION", colorize(
                                           string_format( "[%s]", contents_item->type->get_id() ),
                                           c_light_blue ) );
                }
                info.emplace_back( "DESCRIPTION", description.translated() );
            }

            if( debug && contents_item && contents_item->goes_bad() ) {
                info.emplace_back( "CONTAINER", space );
                info.emplace_back( "CONTAINER", _( "age (turns): " ),
                                   "", iteminfo::lower_is_better,
                                   to_turns<int>( contents_item->age() ) );
                info.emplace_back( "CONTAINER", _( "rot (turns): " ),
                                   "", iteminfo::lower_is_better,
                                   to_turns<int>( contents_item->rot ) );
                info.emplace_back( "CONTAINER", space + _( "shelf life (turns): " ),
                                   "", iteminfo::lower_is_better,
                                   to_turns<int>( contents_item->get_shelf_life() ) );
                info.emplace_back( "CONTAINER", _( "last rot: " ),
                                   "", iteminfo::lower_is_better,
                                   to_turn<int>( contents_item->last_rot_check ) );
            }
        }
    }
}

void item::final_info( std::vector<iteminfo> &info, const iteminfo_query &parts_ref, int batch,
                       bool debug ) const
{
    if( is_null() ) {
    return;
}

// TODO: Remove
const iteminfo_query *parts = &parts_ref;

const std::string space = "  ";

insert_separation_line( info );

if( can_shatter() ) {
    info.emplace_back( "BASE",
                       _( "* This item will potentially <info>shatter</info> if used as a weapon"
                          " or thrown, instantly <bad>destroying it and spilling any contents</bad>." ) );
    }

    if( parts->test( iteminfo_parts::BASE_RIGIDITY ) ) {
    if( const islot_armor *armor = find_armor_data() ) {
            if( !type->rigid ) {
                info.emplace_back( "BASE",
                                   _( "* This item is <info>not rigid</info>.  Its"
                                      " volume and encumbrance increase with contents." ) );
            } else {
                bool any_encumb_increase = std::ranges::any_of( armor->data,
                []( armor_portion_data data ) {
                    return data.encumber != data.max_encumber;
                } );
                if( any_encumb_increase ) {
                    info.emplace_back( "BASE",
                                       _( "* This item is <info>not rigid</info>.  Its"
                                          " volume and encumbrance increase with contents." ) );
                }
            }
        }
    }

    if( parts->test( iteminfo_parts::DESCRIPTION_CONDUCTIVITY ) ) {
    if( !conductive() ) {
            info.emplace_back( "BASE", _( "* This item <good>does not "
                               "conduct</good> electricity." ) );
        } else if( has_flag( flag_CONDUCTIVE ) ) {
            info.emplace_back( "BASE",
                               _( "* This item effectively <bad>conducts</bad> "
                                  "electricity, as it has no guard." ) );
        } else {
            info.emplace_back( "BASE", _( "* This item <bad>conducts</bad> electricity." ) );
        }
    }

    avatar &you = get_avatar();
    if( is_armor() && you.has_trait( trait_WOOLALLERGY ) &&
        ( made_of( material_id( "wool" ) ) || has_own_flag( flag_wooled ) ) ) {
        info.emplace_back( "DESCRIPTION",
                           _( "* This clothing will give you an <bad>allergic "
                              "reaction</bad>." ) );
    }

    if( parts->test( iteminfo_parts::DESCRIPTION_FLAGS ) ) {
    // concatenate base and acquired flags...
    std::vector<flag_id> flags;
    std::set_union( type->get_flags().begin(), type->get_flags().end(),
                    get_flags().begin(), get_flags().end(),
                    std::back_inserter( flags ) );

        // ...and display those which have an info description
        for( const flag_id &e : sorted_lex( flags ) ) {
            const json_flag &f = e.obj();
            if( !f.info().empty() ) {
                info.emplace_back( "DESCRIPTION", string_format( "* %s", _( f.info() ) ) );
            }
        }
    }

    armor_fit_info( info, parts, batch, debug );

    if( is_tool() ) {
    if( is_power_armor() && parts->test( iteminfo_parts::DESCRIPTION_BIONIC_ARMOR_INTERFACE ) ) {
            info.emplace_back( "DESCRIPTION",
                               _( "* This tool can draw power from a <info>Bionic Armor Interface</info>" ) );
        }
        if( has_flag( flag_USE_UPS ) && parts->test( iteminfo_parts::DESCRIPTION_RECHARGE_UPSMODDED ) ) {
            info.emplace_back( "DESCRIPTION",
                               _( "* This tool uses a <info>universal power supply</info> "
                                  "and is <neutral>not compatible</neutral> with "
                                  "<info>standard batteries</info>." ) );
        } else if( has_flag( flag_RECHARGE ) && has_flag( flag_NO_RELOAD ) &&
                   parts->test( iteminfo_parts::DESCRIPTION_RECHARGE_NORELOAD ) ) {
            info.emplace_back( "DESCRIPTION",
                               _( "* This tool has a <info>rechargeable power cell</info> "
                                  "and is <neutral>not compatible</neutral> with "
                                  "<info>standard batteries</info>." ) );
        } else if( has_flag( flag_RECHARGE ) &&
                   parts->test( iteminfo_parts::DESCRIPTION_RECHARGE_UPSCAPABLE ) ) {
            info.emplace_back( "DESCRIPTION",
                               _( "* This tool has a <info>rechargeable power cell</info> "
                                  "and can be recharged in any <neutral>UPS-compatible "
                                  "recharging station</neutral>. You could charge it with "
                                  "<info>standard batteries</info>, but unloading it is "
                                  "impossible." ) );
        } else if( has_flag( flag_USES_BIONIC_POWER ) ) {
            info.emplace_back( "DESCRIPTION",
                               _( "* This tool <info>runs on bionic power</info>." ) );
        }
    }

    if( has_flag( flag_RADIO_ACTIVATION ) &&
        parts->test( iteminfo_parts::DESCRIPTION_RADIO_ACTIVATION ) ) {
    if( has_flag( flag_RADIO_MOD ) ) {
            info.emplace_back( "DESCRIPTION",
                               _( "* This item has been modified to listen to <info>radio "
                                  "signals</info>.  It can still be activated manually." ) );
        } else {
            info.emplace_back( "DESCRIPTION",
                               _( "* This item can only be activated by a <info>radio "
                                  "signal</info>." ) );
        }

        std::string signame;
        if( has_flag( flag_RADIOSIGNAL_1 ) ) {
            signame = "<color_c_red>red</color> radio signal";
        } else if( has_flag( flag_RADIOSIGNAL_2 ) ) {
            signame = "<color_c_blue>blue</color> radio signal";
        } else if( has_flag( flag_RADIOSIGNAL_3 ) ) {
            signame = "<color_c_green>green</color> radio signal";
        }
        if( parts->test( iteminfo_parts::DESCRIPTION_RADIO_ACTIVATION_CHANNEL ) ) {
            info.emplace_back( "DESCRIPTION",
                               string_format( _( "* It will be activated by the %s." ),
                                              signame ) );
        }

        if( has_flag( flag_RADIO_INVOKE_PROC ) &&
            parts->test( iteminfo_parts::DESCRIPTION_RADIO_ACTIVATION_PROC ) ) {
            info.emplace_back( "DESCRIPTION",
                               _( "* Activating this item with a <info>radio signal</info> will "
                                  "<neutral>detonate</neutral> it immediately." ) );
        }
    }

    bionic_info( info, parts, batch, debug );

    if( is_gun() && has_flag( flag_FIRE_TWOHAND ) &&
        parts->test( iteminfo_parts::DESCRIPTION_TWOHANDED ) ) {
    info.emplace_back( "DESCRIPTION",
                       _( "* This weapon needs <info>two free hands</info> "
                          "to fire." ) );
    }

    if( is_gunmod() && has_flag( flag_DISABLE_SIGHTS ) &&
        parts->test( iteminfo_parts::DESCRIPTION_GUNMOD_DISABLESSIGHTS ) ) {
    info.emplace_back( "DESCRIPTION",
                       _( "* This mod <bad>obscures sights</bad> of the "
                          "base weapon." ) );
    }

    if( is_gunmod() && has_flag( flag_CONSUMABLE ) &&
        parts->test( iteminfo_parts::DESCRIPTION_GUNMOD_CONSUMABLE ) ) {
    info.emplace_back( "DESCRIPTION",
                       _( "* This mod might <bad>suffer wear</bad> when firing "
                          "the base weapon." ) );
    }

    if( has_flag( flag_LEAK_DAM ) && has_flag( flag_RADIOACTIVE ) && damage() > 0
        && parts->test( iteminfo_parts::DESCRIPTION_RADIOACTIVITY_DAMAGED ) ) {
    info.emplace_back( "DESCRIPTION",
                       _( "* The casing of this item has <neutral>cracked</neutral>, "
                          "revealing an <info>ominous green glow</info>." ) );
    }

    if( has_flag( flag_LEAK_ALWAYS ) && has_flag( flag_RADIOACTIVE ) &&
        parts->test( iteminfo_parts::DESCRIPTION_RADIOACTIVITY_ALWAYS ) ) {
    info.emplace_back( "DESCRIPTION",
                       _( "* This object is <neutral>surrounded</neutral> by a "
                          "<info>sickly green glow</info>." ) );
    }

    if( is_brewable() || ( !contents.empty() && contents.front().is_brewable() ) ) {
        const item &brewed = !is_brewable() ? contents.front() : *this;
        if( parts->test( iteminfo_parts::DESCRIPTION_BREWABLE_DURATION ) ) {
            const time_duration btime = brewed.brewing_time();
            int btime_i = to_days<int>( btime );
            if( btime <= 2_days ) {
                btime_i = to_hours<int>( btime );
                info.emplace_back( "DESCRIPTION",
                                   string_format( vgettext( "* Once set in a vat, this "
                                                  "will ferment in around %d hour.",
                                                  "* Once set in a vat, this will ferment in "
                                                  "around %d hours.", btime_i ), btime_i ) );
            } else {
                info.emplace_back( "DESCRIPTION",
                                   string_format( vgettext( "* Once set in a vat, this "
                                                  "will ferment in around %d day.",
                                                  "* Once set in a vat, this will ferment in "
                                                  "around %d days.", btime_i ), btime_i ) );
            }
        }
        if( parts->test( iteminfo_parts::DESCRIPTION_BREWABLE_PRODUCTS ) ) {
            for( const itype_id &res : brewed.brewing_results() ) {
                info.emplace_back( "DESCRIPTION",
                                   string_format( _( "* Fermenting this will produce "
                                                     "<neutral>%s</neutral>." ),
                                                  nname( res, brewed.charges ) ) );
            }
        }
    }

    if( parts->test( iteminfo_parts::DESCRIPTION_FAULTS ) ) {
    for( const fault_id &e : faults ) {
            //~ %1$s is the name of a fault and %2$s is the description of the fault
            info.emplace_back( "DESCRIPTION", string_format( _( "* <bad>%1$s</bad>.  %2$s" ),
                               e.obj().name(), e.obj().description() ) );
        }
    }

    // does the item fit in any holsters?
    std::vector<const itype *> holsters = item_controller->find( [this]( const itype & e ) {
        if( !e.can_use( "holster" ) ) {
            return false;
        }
        const holster_actor *ptr = dynamic_cast<const holster_actor *>
                                   ( e.get_use( "holster" )->get_actor_ptr() );
        return ptr->can_holster( *this );
    } );

    if( !holsters.empty() && parts->test( iteminfo_parts::DESCRIPTION_HOLSTERS ) ) {
    insert_separation_line( info );
        info.emplace_back( "DESCRIPTION", _( "<bold>Can be stored in</bold>: " ) +
                           enumerate_as_string( holsters.begin(), holsters.end(),
        []( const itype * e ) {
            return e->nname( 1 );
        } ) );
    }

    if( parts->test( iteminfo_parts::DESCRIPTION_ACTIVATABLE_TRANSFORMATION ) ) {
    for( auto &u : type->use_methods ) {
            const delayed_transform_iuse *tt = dynamic_cast<const delayed_transform_iuse *>
                                               ( u.second.get_actor_ptr() );
            if( tt == nullptr ) {
                continue;
            }
            const int time_to_do = tt->time_to_do( *this );
            if( time_to_do <= 0 ) {
                info.emplace_back( "DESCRIPTION",
                                   _( "It's done and <info>can be activated</info>." ) );
            } else {
                const std::string time = to_string_clipped( time_duration::from_turns( time_to_do ) );
                info.emplace_back( "DESCRIPTION",
                                   string_format( _( "It will be done in %s." ),
                                                  time.c_str() ) );
            }
        }
    }

    if( this->get_var( "die_num_sides", 0 ) != 0 ) {
    info.emplace_back( "DESCRIPTION",
                       string_format( _( "* This item can be used as a <info>die</info>, "
                                         "and has <info>%d</info> sides." ),
                                      static_cast<int>( this->get_var( "die_num_sides",
                                              0 ) ) ) );
    }

    // Price and barter value
    const int price_preapoc = price( false ) * batch;
    const int price_postapoc = price( true ) * batch;
    if( parts->test( iteminfo_parts::BASE_PRICE ) ) {
    insert_separation_line( info );
        info.emplace_back( "BASE", _( "Price: " ), _( "$<num>" ),
                           iteminfo::is_decimal | iteminfo::lower_is_better | iteminfo::no_newline,
                           static_cast<double>( price_preapoc ) / 100 );
    }
    if( price_preapoc != price_postapoc && parts->test( iteminfo_parts::BASE_BARTER ) ) {
    info.emplace_back( "BASE", space + _( "Barter value: " ), _( "$<num>" ),
                       iteminfo::is_decimal | iteminfo::lower_is_better,
                       static_cast<double>( price_postapoc ) / 100 );
    }

    // Recipes using this item as an ingredient
    if( parts->test( iteminfo_parts::DESCRIPTION_APPLICABLE_RECIPES ) ) {
    itype_id tid = contents.empty() ? typeId() : contents.front().typeId();
        const inventory &crafting_inv = you.crafting_inventory();

        const recipe_subset available_recipe_subset = you.get_available_recipes( crafting_inv, nullptr,
            recipe_filter_by_component( tid ) );
        const std::set<const recipe *> &item_recipes = available_recipe_subset.of_component( tid );

        if( item_recipes.empty() ) {
            insert_separation_line( info );
            info.emplace_back( "DESCRIPTION",
                               _( "You know of nothing you could craft with it." ) );
        } else {
            if( item_recipes.size() > 24 ) {
                insert_separation_line( info );
                info.emplace_back( "DESCRIPTION",
                                   _( "You know dozens of things you could craft with it." ) );
            } else if( item_recipes.size() > 12 ) {
                insert_separation_line( info );
                info.emplace_back( "DESCRIPTION",
                                   _( "You could use it to craft various other things." ) );
            } else {
                // Extract item names from recipes and sort them
                std::vector<std::pair<std::string, bool>> result_names;
                std::ranges::transform(
                    item_recipes,
                    std::back_inserter( result_names ),
                [&crafting_inv]( const recipe * r ) {
                    bool can_make = r->deduped_requirements().can_make_with_inventory(
                                        crafting_inv, r->get_component_filter() );
                    return std::make_pair( r->result_name( /*decorated=*/true ), can_make );
                } );
                std::ranges::sort( result_names, localized_compare );
                const std::string recipes =
                    enumerate_as_string( result_names.begin(), result_names.end(),
                []( const std::pair<std::string, bool> &p ) {
                    if( p.second ) {
                        return p.first;
                    } else {
                        return string_format( "<dark>%s</dark>", p.first );
                    }
                } );
                insert_separation_line( info );
                info.emplace_back( "DESCRIPTION",
                                   string_format( _( "You could use it to craft: %s" ),
                                                  recipes ) );
            }
        }
    }
    if( get_option<bool>( "ENABLE_ASCII_ART_ITEM" ) ) {
    const ascii_art_id art = type->picture_id;
    if( art.is_valid() ) {
            for( const std::string &line : art->picture ) {
                info.emplace_back( "DESCRIPTION", line );
            }
        }
    }
}

std::vector<iteminfo> item::info() const
{
    return info( iteminfo_query::no_conditions, 1, temperature_flag::TEMP_NORMAL );
}

std::vector<iteminfo> item::info( int batch ) const
{
    return info( iteminfo_query::no_conditions, batch, temperature_flag::TEMP_NORMAL );
}

std::vector<iteminfo> item::info( temperature_flag temperature ) const
{
    return info( iteminfo_query::all, 1, temperature );
}

std::vector<iteminfo> item::info( const iteminfo_query &parts_ref, int batch,
                                  temperature_flag temperature ) const
{
    const bool debug = g != nullptr && debug_mode;

    // TODO: Use reference properly
    const iteminfo_query *parts = &parts_ref;
    std::vector<iteminfo> info;

    if( !is_null() ) {
        basic_info( info, parts, batch, debug );
    }

    const item *med_item = nullptr;
    if( is_medication() ) {
        med_item = this;
    } else if( is_med_container() ) {
        med_item = &contents.front();
    }
    if( med_item != nullptr ) {
        med_info( med_item, info, parts, batch, debug );
    }

    if( const item *food_item = get_food() ) {
        food_info( food_item, info, parts, batch, debug, temperature );
    }

    container_info( info, parts, batch, debug );
    contents_info( info, parts, batch, debug );
    combat_info( info, parts, batch, debug );

    magazine_info( info, parts, batch, debug );
    ammo_info( info, parts, batch, debug );

    const item *gun = nullptr;
    if( is_gun() ) {
        gun = this;
        const gun_mode aux = gun_current_mode();
        // if we have an active auxiliary gunmod display stats for this instead
        if( aux && aux->is_gunmod() && aux->is_gun() &&
            parts->test( iteminfo_parts::DESCRIPTION_AUX_GUNMOD_HEADER ) ) {
            gun = &*aux;
            info.emplace_back( "DESCRIPTION",
                               string_format( _( "Stats of the active <info>gunmod (%s)</info> "
                                                 "are shown." ), gun->tname() ) );
        }
    }
    if( gun != nullptr ) {
        gun_info( gun, info, parts, batch, debug );
    }

    gunmod_info( info, parts, batch, debug );
    armor_info( info, parts, batch, debug );
    animal_armor_info( info, parts, batch, debug );
    book_info( info, parts, batch, debug );
    battery_info( info, parts, batch, debug );
    tool_info( info, parts, batch, debug );
    component_info( info, parts, batch, debug );
    qualities_info( info, parts, batch, debug );

    // Uses for item (bandaging quality, holster capacity, grenade activation)
    if( parts->test( iteminfo_parts::DESCRIPTION_USE_METHODS ) ) {
        for( const std::pair<const std::string, use_function> &method : type->use_methods ) {
            insert_separation_line( info );
            method.second.dump_info( *this, info );
        }
    }

    repair_info( info, parts, batch, debug );
    disassembly_info( info, parts, batch, debug );

    final_info( info, parts_ref, batch, debug );

    if( !info.empty() && info.back().sName == "--" ) {
        info.pop_back();
    }

    return info;
}

std::string item::info_string() const
{
    return info_string( iteminfo_query::all, 1 );
}

std::string item::info_string( const iteminfo_query &parts, int batch,
                               temperature_flag temperature ) const
{
    std::vector<iteminfo> item_info = info( parts, batch, temperature );
    return format_item_info( item_info, {} );
}

iteminfo::iteminfo( const std::string &Type, const std::string &Name, const std::string &Fmt,
                    flags Flags, double Value )
{
    sType = Type;
    sName = replace_colors( Name );
    sFmt = replace_colors( Fmt );
    is_int = !( Flags & is_decimal || Flags & is_three_decimal );
    three_decimal = ( Flags & is_three_decimal );
    dValue = Value;
    bShowPlus = static_cast<bool>( Flags & show_plus );
    std::stringstream convert;
    if( bShowPlus ) {
        convert << std::showpos;
    }
    if( is_int ) {
        convert << std::setprecision( 0 );
    } else if( three_decimal ) {
        convert << std::setprecision( 3 );
    } else {
        convert << std::setprecision( 2 );
    }
    convert << std::fixed << Value;
    sValue = convert.str();
    bNewLine = !( Flags & no_newline );
    bLowerIsBetter = static_cast<bool>( Flags & lower_is_better );
    bDrawName = !( Flags & no_name );
}

iteminfo::iteminfo( const std::string &Type, const std::string &Name, double Value )
    : iteminfo( Type, Name, "", no_flags, Value )
{
}
