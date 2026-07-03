// iteminfo_food.cpp — food and medicine display info methods.
// Split from item_info.cpp to reduce translation-unit size.

#include "item.h"

#include <algorithm>
#include <ranges>
#include <sstream>
#include <string>

#include "avatar.h"
#include "bionics.h"
#include "cached_item_options.h"
#include "character.h"
#include "character_stat.h"
#include "color.h"
#include "effect.h"
#include "flag.h"
#include "game.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "options.h"
#include "output.h"
#include "rot.h"
#include "skill.h"
#include "stomach.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "vitamin.h"
#include "recipe_dictionary.h"
#include "string_id_utils.h"
#include "string_utils.h"

// File-local helper (duplicated from item_info.cpp — anon namespace, no ODR issue).
namespace
{
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
}

static const bionic_id bio_digestion( "bio_digestion" );
static const skill_id skill_survival( "survival" );
static const trait_id trait_CARNIVORE( "CARNIVORE" );
static const trait_id trait_SAPROVORE( "SAPROVORE" );
static const trait_id trait_WOOLALLERGY( "WOOLALLERGY" );
static const trait_flag_str_id trait_flag_CANNIBAL( "CANNIBAL" );
static const vitamin_id vitamin_human_flesh_vitamin( "human_flesh_vitamin" );


void item::med_info( const item *med_item, std::vector<iteminfo> &info, const iteminfo_query *parts,
                     int batch, bool ) const
{
    const cata::value_ptr<islot_comestible> &med_com = med_item->get_comestible();
    if( med_com->quench != 0 && parts->test( iteminfo_parts::MED_QUENCH ) ) {
        info.emplace_back( "MED", _( "Quench: " ), med_com->quench );
    }

    if( med_item->get_comestible_fun() != 0 && parts->test( iteminfo_parts::MED_JOY ) ) {
        info.emplace_back( "MED", _( "Enjoyability: " ),
                           get_avatar().fun_for( *med_item ).first );
    }

    if( med_com->stim != 0 && parts->test( iteminfo_parts::MED_STIMULATION ) ) {
        std::string name = string_format( "%s <stat>%s</stat>", _( "Stimulation:" ),
                                          med_com->stim > 0 ? _( "Upper" ) : _( "Downer" ) );
        info.emplace_back( "MED", name );
    }

    if( parts->test( iteminfo_parts::MED_PORTIONS ) ) {
        info.emplace_back( "MED", _( "Portions: " ),
                           std::abs( static_cast<int>( med_item->charges ) * batch ) );
    }

    if( med_com->addict && parts->test( iteminfo_parts::DESCRIPTION_MED_ADDICTING ) ) {
        info.emplace_back( "DESCRIPTION", _( "* Consuming this item is <bad>addicting</bad>." ) );
    }
}

void item::food_info( const item *food_item, std::vector<iteminfo> &info,
                      const iteminfo_query *parts, int batch, bool debug,
                      temperature_flag temperature ) const
{
    nutrients min_nutr;
    nutrients max_nutr;
    avatar &you = get_avatar();

    std::string recipe_exemplar = get_var( "recipe_exemplar", "" );
    if( recipe_exemplar.empty() ) {
        min_nutr = max_nutr = you.compute_effective_nutrients( *food_item );
    } else {
        std::tie( min_nutr, max_nutr ) =
            you.compute_nutrient_range( *food_item, recipe_id( recipe_exemplar ) );
    }

    bool show_nutr = parts->test( iteminfo_parts::FOOD_NUTRITION ) ||
                     parts->test( iteminfo_parts::FOOD_VITAMINS );
    if( show_nutr && !food_item->has_flag( flag_NUTRIENT_OVERRIDE ) ) {
        info.emplace_back(
            "FOOD", _( "Nutrition will <color_cyan>vary with available ingredients</color>." ) );
        if( recipe_dict.is_item_on_loop( food_item->typeId() ) ) {
            info.emplace_back(
                "FOOD", _( "Nutrition range cannot be calculated accurately due to "
                           "<color_red>recipe loops</color>." ) );
        }
    }

    const std::string space = "  ";
    if( max_nutr.kcal != 0 || food_item->get_comestible()->quench != 0 ) {
        if( parts->test( iteminfo_parts::FOOD_NUTRITION ) ) {
            info.emplace_back( "FOOD", _( "<bold>Calories (kcal)</bold>: " ),
                               "", iteminfo::no_newline, you.compute_effective_nutrients( *food_item ).kcal );
            if( max_nutr.kcal != min_nutr.kcal ) {
                info.emplace_back( "FOOD", _( "-" ),
                                   "", iteminfo::no_newline, you.compute_effective_nutrients( *food_item ).kcal );
            }
        }
        if( parts->test( iteminfo_parts::FOOD_QUENCH ) ) {
            info.emplace_back( "FOOD", space + _( "Quench: " ),
                               food_item->get_comestible()->quench );
        }
    }

    const std::pair<int, int> fun_for_food_item = you.fun_for( *food_item );
    if( fun_for_food_item.first != 0 && parts->test( iteminfo_parts::FOOD_JOY ) ) {
        info.emplace_back( "FOOD", _( "Enjoyability: " ), fun_for_food_item.first );
    }

    if( parts->test( iteminfo_parts::FOOD_PORTIONS ) ) {
        info.emplace_back( "FOOD", _( "Portions: " ),
                           std::abs( static_cast<int>( food_item->charges ) * batch ) );
    }
    if( food_item->corpse != nullptr && parts->test( iteminfo_parts::FOOD_SMELL ) &&
        ( debug || ( g != nullptr && ( you.has_trait( trait_CARNIVORE ) ||
                                       you.has_artifact_with( AEP_SUPER_CLAIRVOYANCE ) ) ) ) ) {
        info.emplace_back( "FOOD", _( "Smells like: " ) + food_item->corpse->nname() );
    }

    auto format_vitamin = [&]( const std::pair<vitamin_id, int> &v, bool display_vitamins ) {
        const bool is_vitamin = v.first->type() == vitamin_type::VITAMIN;
        // only display vitamins that we actually require
        if( you.vitamin_rate( v.first ) == 0_turns || v.second == 0 ||
            display_vitamins != is_vitamin || v.first->has_flag( "NO_DISPLAY" ) ) {
            return std::string();
        }
        const double multiplier = you.vitamin_rate( v.first ) / 1_days * 100;
        const int min_value = min_nutr.get_vitamin( v.first );
        const int max_value = v.second;
        const int min_rda = std::lround( min_value * multiplier );
        const int max_rda = std::lround( max_value * multiplier );
        const std::string format = min_rda == max_rda ? "%s (%i%%)" : "%s (%i-%i%%)";
        return string_format( format, v.first->name(), min_value, max_value );
    };

    const auto max_nutr_vitamins = sorted_lex( max_nutr.vitamins );
    const std::string required_vits = enumerate_as_string(
                                          max_nutr_vitamins.begin(),
                                          max_nutr_vitamins.end(),
    [&]( const std::pair<vitamin_id, int> &v ) {
        return format_vitamin( v, true );
    } );

    const std::string effect_vits = enumerate_as_string(
                                        max_nutr_vitamins.begin(),
                                        max_nutr_vitamins.end(),
    [&]( const std::pair<vitamin_id, int> &v ) {
        return format_vitamin( v, false );
    } );

    if( !required_vits.empty() && parts->test( iteminfo_parts::FOOD_VITAMINS ) ) {
        info.emplace_back( "FOOD", _( "Vitamins (RDA): " ), required_vits );
    }

    if( !effect_vits.empty() && parts->test( iteminfo_parts::FOOD_VIT_EFFECTS ) ) {
        info.emplace_back( "FOOD", _( "Other contents: " ), effect_vits );
    }

    insert_separation_line( info );

    if( you.allergy_type( *food_item ) != morale_type( "morale_null" ) ) {
        info.emplace_back( "DESCRIPTION",
                           _( "* This food will cause an <bad>allergic reaction</bad>." ) );
    }

    if( food_item->has_vitamin( vitamin_human_flesh_vitamin ) &&
        parts->test( iteminfo_parts::FOOD_CANNIBALISM ) ) {
        if( !you.has_trait_flag( trait_flag_CANNIBAL ) ) {
            info.emplace_back( "DESCRIPTION",
                               _( "* This food contains <bad>human flesh</bad>." ) );
        } else {
            info.emplace_back( "DESCRIPTION",
                               _( "* This food contains <good>human flesh</good>." ) );
        }
    }

    if( food_item->is_tainted() && parts->test( iteminfo_parts::FOOD_CANNIBALISM ) ) {
        info.emplace_back( "DESCRIPTION",
                           _( "* This food is <bad>tainted</bad> and will poison you." ) );
    }

    ///\EFFECT_SURVIVAL >=3 allows detection of poisonous food
    if( food_item->has_flag( flag_HIDDEN_POISON ) && you.get_skill_level( skill_survival ) >= 3 &&
        parts->test( iteminfo_parts::FOOD_POISON ) ) {
        info.emplace_back( "DESCRIPTION",
                           _( "* On closer inspection, this appears to be "
                              "<bad>poisonous</bad>." ) );
    }

    ///\EFFECT_SURVIVAL >=5 allows detection of hallucinogenic food
    if( food_item->has_flag( flag_HIDDEN_HALLU ) && you.get_skill_level( skill_survival ) >= 5 &&
        parts->test( iteminfo_parts::FOOD_HALLUCINOGENIC ) ) {
        info.emplace_back( "DESCRIPTION",
                           _( "* On closer inspection, this appears to be "
                              "<neutral>hallucinogenic</neutral>." ) );
    }

    if( food_item->goes_bad() && parts->test( iteminfo_parts::FOOD_ROT ) ) {
        const std::string rot_time = to_string_clipped( food_item->get_shelf_life() );
        info.emplace_back( "DESCRIPTION",
                           string_format( _( "* This food is <neutral>perishable</neutral>, "
                                             "and at room temperature has an estimated nominal "
                                             "shelf life of <info>%s</info>." ), rot_time ) );


        if( parts->test( iteminfo_parts::FOOD_ROT_STORAGE ) ) {
            const char *temperature_description;
            bool print_freshness_duration = false;
            const bool preserved_by_container = food_item->is_in_preserving_container();
            // There should be a better way to do this...
            if( preserved_by_container ) {
                temperature_description = _( "* Current storage conditions <good>fully</good> "
                                             "protect this item from rot.  It will stay fresh indefinitely." );
            } else {
                switch( temperature ) {
                    case temperature_flag::TEMP_NORMAL:
                    case temperature_flag::TEMP_HEATER: {
                        temperature_description = _( "* Current storage conditions <bad>do not</bad> "
                                                     "protect this item from rot." );
                    }
                    break;
                    case temperature_flag::TEMP_FRIDGE:
                    case temperature_flag::TEMP_ROOT_CELLAR: {
                        temperature_description = _( "* Current storage conditions <neutral>partially</neutral> "
                                                     "protect this item from rot.  It will stay fresh at least <info>%s</info>." );
                        print_freshness_duration = true;
                    }
                    break;
                    case temperature_flag::TEMP_FREEZER: {
                        temperature_description = _( "* Current storage conditions <good>fully</good> "
                                                     "protect this item from rot.  It will stay fresh indefinitely." );
                    }
                    break;
                    default: {
                        temperature_description = "BUGGED TEMPERATURE INFO";
                    }
                }
            }

            if( print_freshness_duration ) {
                time_duration remaining_fresh = food_item->minimum_freshness_duration( temperature );
                std::string time_string = to_string_clipped( remaining_fresh );
                info.emplace_back( "DESCRIPTION", string_format( temperature_description, time_string ) );
            } else {
                info.emplace_back( "DESCRIPTION", temperature_description );
            }
        }

        if( !food_item->rotten() ) {
            info.emplace_back( "DESCRIPTION", get_freshness_description( *food_item ) );
        }

        if( food_item->has_flag( flag_NO_PARASITES ) ) {
            info.emplace_back( "DESCRIPTION",
                               _( "* It seems that deep freezing <good>killed all "
                                  "parasites</good>." ) );
        }
        if( food_item->rotten() ) {
            if( you.has_bionic( bio_digestion ) ) {
                info.emplace_back( "DESCRIPTION",
                                   _( "This food has started to <neutral>rot</neutral>, "
                                      "but <info>your bionic digestion can tolerate "
                                      "it</info>." ) );
            } else if( you.has_trait( trait_SAPROVORE ) ) {
                info.emplace_back( "DESCRIPTION",
                                   _( "This food has started to <neutral>rot</neutral>, "
                                      "but <info>you can tolerate it</info>." ) );
            } else {
                info.emplace_back( "DESCRIPTION",
                                   _( "This food has started to <bad>rot</bad>. "
                                      "<info>Eating</info> it would be a <bad>very bad "
                                      "idea</bad>." ) );
            }
        }
    }
}
