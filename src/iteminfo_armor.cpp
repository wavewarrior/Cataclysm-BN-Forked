#include <algorithm>
#include <numeric>
#include <string>
#include "avatar.h"
#include "catacharset.h"
#include "cata_algo.h"
#include "item.h"
#include "item_info.h"
#include "flag.h"
#include "iteminfo_query.h"
#include "iteminfo_format_utils.h"
#include "itype.h"
#include "output.h"
#include "string_id.h"
#include "string_id_utils.h"
#include "units_utility.h"

namespace
{

struct armor_portion_type {
    int encumber;
    int max_encumber;
    int coverage;

    auto operator==( const armor_portion_type &other ) const -> bool  = default;
};

struct body_part_display_info {
    translation to_display;
    armor_portion_type portion;
    bool active;
};

auto which_layer( const item &it ) -> std::string
{
    if( it.has_flag( flag_PERSONAL ) ) {
    return _( "<stat>Personal aura</stat>. " );
    } else if( it.has_flag( flag_SKINTIGHT ) ) {
    return  _( "<stat>Close to skin</stat>. " );
    } else if( it.has_flag( flag_BELTED ) ) {
    return  _( "<stat>Strapped</stat>. " );
    } else if( it.has_flag( flag_OUTER ) ) {
    return  _( "<stat>Outer</stat>. " );
    } else if( it.has_flag( flag_WAIST ) ) {
    return  _( "<stat>Waist</stat>. " );
    } else if( it.has_flag( flag_AURA ) ) {
    return  _( "<stat>Outer aura</stat>. " );
    } else {
        return  _( "<stat>Normal</stat>. " );
    }
}

auto sizing_info( const item::sizing sizing_level ) -> std::optional<std::string>
{
    using sizing = item::sizing;

    switch( sizing_level ) {
        case sizing::human_sized_small_char:
            return _( " <bad>(too big)</bad>" );
        case sizing::big_sized_small_char:
            return _( " <bad>(huge!)</bad>" );
        case sizing::small_sized_human_char:
        case sizing::human_sized_big_char:
            return _( " <bad>(too small)</bad>" );
        case sizing::small_sized_big_char:
            return _( " <bad>(tiny!)</bad>" );
        default:
            return {};
    }
}

using BodyPartInfoPair = std::pair<bodypart_str_id, body_part_display_info>;

/// filter info when it's not active or it's one-sided and the item doesn't cover it
auto parts_to_display( const item &it,
                       const islot_armor *armor,
                       const std::vector<BodyPartInfoPair> &xs ) -> std::vector<BodyPartInfoPair>
{
    auto result = std::vector<BodyPartInfoPair>();

    std::ranges::copy_if( xs, std::back_inserter( result ),
    [&it, armor]( const auto & piece ) {
        if( !piece.second.active ) {
            return false;
        }
        if( !armor->sided ) {
            return true;
        }
        const bodypart_str_id &covering_id = piece.first;
        return it.covers( covering_id.id() );
    } );

    return result;
}


auto translate_pair( const BodyPartInfoPair &piece ) -> std::string
{
    return piece.second.to_display.translated();
};

template<typename C>
auto max_utf8_width( const C &c ) -> int
{
    return std::transform_reduce(
           c.begin(), c.end(), 0,
    []( const int left, const int right ) -> int { return std::max( left, right ); },
    []( const auto & entry ) -> int { return utf8_width( entry.translated ); } );
}

const auto space = std::string {"  "};

auto same_for_all_parts()
{
    return  _( "<info>(for all parts)</info>" );
}

auto item_coverages( const std::vector<BodyPartInfoPair> &xs ) -> std::vector<iteminfo>
{
    const auto grouped = cata::group_by( xs, []( const auto & info ) -> int { return info.second.portion.coverage; } );
    const auto heading = string_format( "%s:", _( "<bold>Coverage</bold>" ) );
    const auto coverage_text = []( const int n ) -> std::string { return string_format( "<neutral>%s</neutral>%%", n ); };

    if( grouped.size() == 1 ) {
        return std::vector{ iteminfo( "ARMOR",
                                      string_format( "%s %s %s", heading, coverage_text( grouped.begin()->first ), same_for_all_parts() ) ) };
    }

    struct Entry {
        std::string translated;
        int coverage;
    };

    auto localized = std::vector<Entry>();
    for( const auto &[coverage, parts] : grouped ) {
        const auto parts_str = enumerate_as_string( parts, translate_pair, enumeration_conjunction::none );
        localized.emplace_back( Entry{ parts_str, coverage } );
    }

    const int width = max_utf8_width( localized );

    auto result = std::vector<iteminfo>();
    result.emplace_back( "ARMOR", heading );
    for( const auto &[parts_str, coverage] : localized ) {
        result.emplace_back( "ARMOR", string_format( "%s%s: %s", space,
                             utf8_justify( parts_str, -width, true ), coverage_text( coverage ) ) );
    }

    return result;
}

auto item_encumbrances( const std::vector<BodyPartInfoPair> &xs,
                        const std::string &format ) -> std::vector<iteminfo>
{
    struct Encumber {
        int min;
        int max;

        auto operator<( const Encumber &other ) const -> bool {
            return min < other.min || ( min == other.min && max < other.max );
        }
    };

    const auto heading = string_format( "%s:", _( "<bold>Encumbrance</bold>" ) );
    const auto encumber_range = []( const Encumber e ) -> std::string {
        return ( e.min == e.max )
        ? string_format( "<neutral>%d</neutral>", e.min )
        : string_format( "<neutral>%d-%d</neutral> (%s)", e.min, e.max,  _( "When Full" ) );
    };

    const auto grouped = cata::group_by( xs, []( const auto & info ) -> Encumber {
        const auto portion = info.second.portion;
        return {portion.encumber, portion.max_encumber};
    } );

    if( grouped.size() == 1 ) {
        return std::vector{ iteminfo( "ARMOR",
                                      string_format( "%s %s %s", heading, encumber_range( grouped.begin()->first ), same_for_all_parts() ) ) };
    }

    struct Entry {
        std::string translated;
        Encumber encumber;
    };

    auto localized = std::vector<Entry>();
    for( const auto &[encumber, parts] : grouped ) {
        const auto parts_str = enumerate_as_string( parts, translate_pair, enumeration_conjunction::none );

        localized.emplace_back( Entry{ parts_str, encumber } );
    }

    const int width = max_utf8_width( localized );

    auto result = std::vector<iteminfo>();
    result.reserve( xs.size() );
    result.emplace_back( iteminfo( "ARMOR", heading, format,
                                   iteminfo::lower_is_better ) );

    for( auto &[parts_str, encumber] : localized ) {
        const auto justified_parts_info = utf8_justify( parts_str, -width, true );
        if( encumber.min == encumber.max ) {
            result.emplace_back( "ARMOR",
                                 string_format( "%s%s: ", space, justified_parts_info ),
                                 "", iteminfo::lower_is_better, encumber.min );
        } else {
            result.emplace_back( "ARMOR",
                                 string_format( "%s%s: <neutral>%d-%d</neutral> (%s)",
                                                space, justified_parts_info,
                                                encumber.min, encumber.max, _( "When Full" ) ),
                                 "", iteminfo::lower_is_better ) ;
        }
    }
    return result;
}

} // namespace


// File-scope id constant and helper for armor info functions
static const itype_id itype_rad_badge( "rad_badge" );

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

void item::armor_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int batch,
                       bool debug ) const
{
    if( !is_armor() ) {
    return;
}

avatar &you = get_avatar();
body_part_set covered_parts = get_covered_body_parts();
const bool covers_anything = covered_parts.any();

int converted_storage_scale = 0;
const double converted_storage = round_up( convert_volume( get_storage().value(),
                                     &converted_storage_scale ), 2 );
    if( parts->test( iteminfo_parts::ARMOR_STORAGE ) && converted_storage > 0 ) {
    const iteminfo::flags f = converted_storage_scale == 0 ? iteminfo::no_flags : iteminfo::is_decimal;
    info.emplace_back( iteminfo( "ARMOR", _( "<bold>Storage</bold>: " ),
                                 string_format( "<num> %s", volume_units_abbr() ),
                                 f, converted_storage ) );
    }

    if( parts->test( iteminfo_parts::ARMOR_BODYPARTS ) ) {
    insert_separation_line( info );
        std::string coverage = _( "<bold>Covers</bold>: " );
        if( covers( bodypart_id( "head" ) ) ) {
            coverage += _( "The <info>head</info>. " );
        }
        if( covers( bodypart_id( "eyes" ) ) ) {
            coverage += _( "The <info>eyes</info>. " );
        }
        if( covers( bodypart_id( "mouth" ) ) ) {
            coverage += _( "The <info>mouth</info>. " );
        }
        if( covers( bodypart_id( "torso" ) ) ) {
            coverage += _( "The <info>torso</info>. " );
        }

        if( is_sided() && ( covers( bodypart_id( "arm_l" ) ) || covers( bodypart_id( "arm_r" ) ) ) ) {
            coverage += _( "Either <info>arm</info>. " );
        } else if( covers( bodypart_id( "arm_l" ) ) && covers( bodypart_id( "arm_r" ) ) ) {
            coverage += _( "The <info>arms</info>. " );
        } else if( covers( bodypart_id( "arm_l" ) ) ) {
            coverage += _( "The <info>left arm</info>. " );
        } else if( covers( bodypart_id( "arm_r" ) ) ) {
            coverage += _( "The <info>right arm</info>. " );
        }

        if( is_sided() && ( covers( bodypart_id( "hand_l" ) ) || covers( bodypart_id( "hand_r" ) ) ) ) {
            coverage += _( "Either <info>hand</info>. " );
        } else if( covers( bodypart_id( "hand_l" ) ) && covers( bodypart_id( "hand_r" ) ) ) {
            coverage += _( "The <info>hands</info>. " );
        } else if( covers( bodypart_id( "hand_l" ) ) ) {
            coverage += _( "The <info>left hand</info>. " );
        } else if( covers( bodypart_id( "hand_r" ) ) ) {
            coverage += _( "The <info>right hand</info>. " );
        }

        if( is_sided() && ( covers( bodypart_id( "leg_l" ) ) || covers( bodypart_id( "leg_r" ) ) ) ) {
            coverage += _( "Either <info>leg</info>. " );
        } else if( covers( bodypart_id( "leg_l" ) ) && covers( bodypart_id( "leg_r" ) ) ) {
            coverage += _( "The <info>legs</info>. " );
        } else if( covers( bodypart_id( "leg_l" ) ) ) {
            coverage += _( "The <info>left leg</info>. " );
        } else if( covers( bodypart_id( "leg_r" ) ) ) {
            coverage += _( "The <info>right leg</info>. " );
        }

        if( is_sided() && ( covers( bodypart_id( "foot_l" ) ) || covers( bodypart_id( "foot_r" ) ) ) ) {
            coverage += _( "Either <info>foot</info>. " );
        } else if( covers( bodypart_id( "foot_l" ) ) && covers( bodypart_id( "foot_r" ) ) ) {
            coverage += _( "The <info>feet</info>. " );
        } else if( covers( bodypart_id( "foot_l" ) ) ) {
            coverage += _( "The <info>left foot</info>. " );
        } else if( covers( bodypart_id( "foot_r" ) ) ) {
            coverage += _( "The <info>right foot</info>. " );
        }

        if( !covers_anything ) {
            coverage += _( "<info>Nothing</info>." );
        }

        info.emplace_back( iteminfo( "ARMOR", coverage ) );
    }

    if( parts->test( iteminfo_parts::ARMOR_LAYER ) && covers_anything ) {
    info.emplace_back( iteminfo( "ARMOR", _( "Layer: " ) + which_layer( *this ) ) );
        if( has_flag( flag_COMPACT ) || ( has_flag( flag_FIT ) && get_avg_encumber( you ) <= 10 ) ) {
            info.emplace_back( iteminfo( "ARMOR",
                                         _( "This item <good>won't conflict with</good> other items on the same <info>layer</info>." ) ) );
        }
    }

    if( parts->test( iteminfo_parts::ARMOR_WARMTH ) && covers_anything ) {
    info.emplace_back( iteminfo( "ARMOR", _( "Warmth: " ), get_warmth() ) );
    }

    insert_separation_line( info );

    if( parts->test( iteminfo_parts::ARMOR_ENCUMBRANCE ) && covers_anything ) {
    std::string format;
    const bool sizing_matters = get_sizing( you ) != sizing::ignore;
        if( has_flag( flag_FIT ) ) {
            format = _( " <info>(fits)</info>" );
        } else if( has_flag( flag_VARSIZE ) && sizing_matters ) {
            format = _( " <bad>(poor fit)</bad>" );
        }
        if( sizing_matters ) {
            const auto sizing_level = get_sizing( you );
            const auto sizing_info_str = sizing_info( sizing_level );
            if( sizing_info_str ) {
                format = sizing_info_str.value();
            }
        }

        const islot_armor *armor = find_armor_data();
        if( armor && !armor->data.empty() ) {
            std::map<bodypart_str_id, body_part_display_info> to_display_data;
            for( const armor_portion_data &piece : armor->data ) {
                if( piece.covers.none() ) {
                    continue;
                }
                for( const bodypart_str_id &covering_id : piece.covers ) {
                    if( covering_id ) {
                        const int encumbrance_when_full =
                            get_encumber_when_containing( you, get_total_capacity(), covering_id.id() );
                        to_display_data[covering_id] = { covering_id.obj().name_as_heading, {
                                get_encumber( you, covering_id ),
                                encumbrance_when_full,
                                piece.coverage
                            }, true
                        };
                    }
                }
            }
            // Handle things that use both sides to avoid showing L. Arm R. Arm etc when both are the same
            if( !armor->sided ) {
                for( const body_part &legacy_part : all_body_parts ) {
                    bodypart_str_id bp( convert_bp( legacy_part ) );
                    bodypart_str_id opposite = bp->opposite_part;
                    if( opposite != bp && covers( bp ) && covers( opposite )
                        && to_display_data.at( bp ).portion == to_display_data.at( opposite ).portion
                        && to_display_data.at( opposite ).active ) {
                        to_display_data.at( opposite ).to_display = bp->name_as_heading_multiple;
                        to_display_data.at( bp ).active = false;
                    }
                }
            }

            const auto &sorted = sorted_lex( to_display_data );
            const auto &to_display = parts_to_display( *this, armor, sorted );

            const auto &encumbrances = item_encumbrances( to_display, format );
            const auto &coverages = item_coverages( to_display );

            info.insert( info.end(), coverages.begin(), coverages.end() );
            info.insert( info.end(), encumbrances.begin(), encumbrances.end() );
        }
    }


    // Whatever the last entry was, we want a newline at this point
    info.back().bNewLine = true;

    if( covers_anything ) {
    armor_protection_info( info, parts, batch, debug );
    }

    const units::mass weight_bonus = get_weight_capacity_bonus();
    const float weight_modif = get_weight_capacity_modifier();
    if( weight_modif != 1 ) {
    std::string modifier;
    if( weight_modif < 1 ) {
            modifier = "<num><bad>x</bad>";
        } else {
            modifier = "<num><color_light_green>x</color>";
        }
        info.emplace_back( iteminfo( "ARMOR",
                                     _( "<bold>Weight capacity modifier</bold>: " ), modifier,
                                     iteminfo::no_newline | iteminfo::is_decimal, weight_modif ) );
    }
    if( weight_bonus != 0_gram ) {
    std::string bonus;
    if( weight_bonus < 0_gram ) {
            bonus = string_format( "<num> <bad>%s</bad>", weight_units() );
        } else {
            bonus = string_format( "<num> <color_light_green> %s</color>", weight_units() );
        }
        info.emplace_back( iteminfo( "ARMOR", _( "<bold>Weight capacity bonus</bold>: " ), bonus,
                                     iteminfo::no_newline | iteminfo::is_decimal,
                                     convert_weight( weight_bonus ) ) );
    }
}

void item::armor_protection_info( std::vector<iteminfo> &info, const iteminfo_query *parts,
                                  int /*batch*/,
                                  bool /*debug*/ ) const
{
    if( !is_armor() && !is_pet_armor() ) {
    return;
}

const std::string space = "  ";

if( parts->test( iteminfo_parts::ARMOR_PROTECTION ) ) {
    info.emplace_back( "ARMOR", _( "<bold>Protection</bold>: Bash: " ), "",
                       iteminfo::no_newline, bash_resist() );
        info.emplace_back( "ARMOR", space + _( "Cut: " ), "", iteminfo::no_newline, cut_resist() );
        info.emplace_back( "ARMOR", space + _( "Ballistic: " ), bullet_resist() );
        info.emplace_back( "ARMOR", space + _( "Acid: " ), "",
                           iteminfo::no_newline, acid_resist() );
        info.emplace_back( "ARMOR", space + _( "Fire: " ), "",
                           iteminfo::no_newline, fire_resist() );
        info.emplace_back( "ARMOR", space + _( "Environmental: " ),
                           get_base_env_resist( *this ) );
        if( type->can_use( "GASMASK" ) || type->can_use( "DIVE_TANK" ) ) {
            info.emplace_back( "ARMOR",
                               _( "<bold>Protection when active</bold>: " ) );
            info.emplace_back( "ARMOR", space + _( "Acid: " ), "",
                               iteminfo::no_newline,
                               acid_resist( false, get_base_env_resist_w_filter() ) );
            info.emplace_back( "ARMOR", space + _( "Fire: " ), "",
                               iteminfo::no_newline,
                               fire_resist( false, get_base_env_resist_w_filter() ) );
            info.emplace_back( "ARMOR", space + _( "Environmental: " ),
                               get_env_resist( get_base_env_resist_w_filter() ) );
        }

        if( damage() > 0 ) {
            info.emplace_back( "ARMOR",
                               _( "Protection values are <bad>reduced by damage</bad> and "
                                  "you may be able to <info>improve them by repairing this "
                                  "item</info>." ) );
        }
    }
}

void item::animal_armor_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int batch,
                              bool debug ) const
{
    if( !is_pet_armor() ) {
    return;
}

const std::string space = "  ";

int converted_storage_scale = 0;
const double converted_storage = round_up( convert_volume( get_storage().value(),
                                     &converted_storage_scale ), 2 );
    if( parts->test( iteminfo_parts::ARMOR_STORAGE ) && converted_storage > 0 ) {
    const iteminfo::flags f = converted_storage_scale == 0 ? iteminfo::no_flags : iteminfo::is_decimal;
    info.emplace_back( "ARMOR", space + _( "Storage: " ),
                       string_format( "<num> %s", volume_units_abbr() ),
                       f, converted_storage );
    }

    // Whatever the last entry was, we want a newline at this point
    info.back().bNewLine = true;

    armor_protection_info( info, parts, batch, debug );
}

void item::armor_fit_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int /*batch*/,
                           bool /*debug*/ ) const
{
    if( !is_armor() ) {
    return;
}

avatar &you = get_avatar();
const sizing sizing_level = get_sizing( you );

if( has_flag( flag_HELMET_COMPAT ) &&
        parts->test( iteminfo_parts::DESCRIPTION_FLAGS_HELMETCOMPAT ) ) {
    info.emplace_back( "DESCRIPTION",
                       _( "* This item can be <info>worn with a "
                          "helmet</info>." ) );
    }

    if( parts->test( iteminfo_parts::DESCRIPTION_FLAGS_FITS ) ) {
    switch( sizing_level ) {
            case sizing::human_sized_human_char:
                if( has_flag( flag_FIT ) ) {
                    info.emplace_back( "DESCRIPTION",
                                       _( "* This clothing <info>fits</info> you perfectly." ) );
                }
                break;
            case sizing::big_sized_big_char:
                if( has_flag( flag_FIT ) ) {
                    info.emplace_back( "DESCRIPTION", _( "* This clothing <info>fits</info> "
                                       "your large frame perfectly." ) );
                }
                break;
            case sizing::small_sized_small_char:
                if( has_flag( flag_FIT ) ) {
                    info.emplace_back( "DESCRIPTION", _( "* This clothing <info>fits</info> "
                                       "your small frame perfectly." ) );
                }
                break;
            case sizing::big_sized_human_char:
                info.emplace_back( "DESCRIPTION", _( "* This clothing is <bad>oversized</bad> "
                                   "and does <bad>not fit</bad> you." ) );
                break;
            case sizing::big_sized_small_char:
                info.emplace_back( "DESCRIPTION",
                                   _( "* This clothing is hilariously <bad>oversized</bad> "
                                      "and does <bad>not fit</bad> your <info>abnormally "
                                      "small mutated anatomy</info>." ) );
                break;
            case sizing::human_sized_big_char:
                info.emplace_back( "DESCRIPTION",
                                   _( "* This clothing is <bad>normal sized</bad> and does "
                                      "<bad>not fit</info> your <info>abnormally large "
                                      "mutated anatomy</info>." ) );
                break;
            case sizing::human_sized_small_char:
                info.emplace_back( "DESCRIPTION",
                                   _( "* This clothing is <bad>normal sized</bad> and does "
                                      "<bad>not fit</bad> your <info>abnormally small "
                                      "mutated anatomy</info>." ) );
                break;
            case sizing::small_sized_big_char:
                info.emplace_back( "DESCRIPTION",
                                   _( "* This clothing is hilariously <bad>undersized</bad> "
                                      "and does <bad>not fit</bad> your <info>abnormally "
                                      "large mutated anatomy</info>." ) );
                break;
            case sizing::small_sized_human_char:
                info.emplace_back( "DESCRIPTION", _( "* This clothing is <bad>undersized</bad> "
                                   "and does <bad>not fit</bad> you." ) );
                break;
            default:
                break;
        }
    }

    if( parts->test( iteminfo_parts::DESCRIPTION_FLAGS_VARSIZE ) ) {
    if( has_flag( flag_VARSIZE ) ) {
            std::string resize_str;
            if( has_flag( flag_FIT ) ) {
                switch( sizing_level ) {
                    case sizing::small_sized_human_char:
                        resize_str = _( "<info>can be upsized</info>" );
                        break;
                    case sizing::human_sized_small_char:
                        resize_str = _( "<info>can be downsized</info>" );
                        break;
                    case sizing::big_sized_human_char:
                    case sizing::big_sized_small_char:
                        resize_str = _( "<bad>can not be downsized</bad>" );
                        break;
                    case sizing::small_sized_big_char:
                    case sizing::human_sized_big_char:
                        resize_str = _( "<bad>can not be upsized</bad> without drastically altering it" );
                        break;
                    default:
                        break;
                }
                if( !resize_str.empty() ) {
                    std::string info_str = string_format( _( "* This clothing %s." ), resize_str );
                    info.emplace_back( "DESCRIPTION", info_str );
                }
            } else {
                switch( sizing_level ) {
                    case sizing::small_sized_human_char:
                        resize_str = _( " and <info>upsized</info>" );
                        break;
                    case sizing::human_sized_small_char:
                        resize_str = _( " and <info>downsized</info>" );
                        break;
                    case sizing::big_sized_human_char:
                    case sizing::big_sized_small_char:
                        resize_str = _( " but <bad>not downsized</bad>" );
                        break;
                    case sizing::small_sized_big_char:
                    case sizing::human_sized_big_char:
                        resize_str = _( " but <bad>not upsized</bad>" );
                        break;
                    default:
                        break;
                }
                std::string info_str = string_format( _( "* This clothing <info>can be "
                                                         "refitted</info>%s." ), resize_str );
                info.emplace_back( "DESCRIPTION", info_str );
            }
        } else {
            info.emplace_back( "DESCRIPTION", _( "* This clothing <bad>can not be refitted, "
                               "upsized, or downsized</bad>." ) );
        }
    }

    if( is_sided() && parts->test( iteminfo_parts::DESCRIPTION_FLAGS_SIDED ) ) {
    info.emplace_back( "DESCRIPTION",
                       _( "* This item can be worn on <info>either side</info> of "
                          "the body." ) );
    }
    if( ( is_power_armor() ) &&
            parts->test( iteminfo_parts::DESCRIPTION_FLAGS_POWERARMOR ) ) {
        if( parts->test( iteminfo_parts::DESCRIPTION_FLAGS_POWERARMOR_RADIATIONHINT ) ) {
            if( covers( bodypart_id( "head" ) ) && has_flag( flag_POWERARMOR_EXTERNAL ) ) {
                info.emplace_back( "DESCRIPTION",
                                   _( "* When worn with a power armor suit, it will "
                                      "<good>fully protect</good> you from "
                                      "<info>radiation</info>." ) );
            } else if( has_flag( flag_POWERARMOR_EXO ) ) {
                info.emplace_back( "DESCRIPTION",
                                   _( "* When worn with a power armor helmet, it will "
                                      "<good>fully protect</good> you from " "<info>radiation</info>." ) );
            }
        }
    }
    if( typeId() == itype_rad_badge && parts->test( iteminfo_parts::DESCRIPTION_IRRADIATION ) ) {
    info.emplace_back( "DESCRIPTION",
                       string_format( _( "* The film strip on the badge is %s." ),
                                      rad_badge_color( irradiation ) ) );
    }
}
