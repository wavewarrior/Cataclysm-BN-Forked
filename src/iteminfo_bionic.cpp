// iteminfo_bionic.cpp — bionic (CBM) display info methods.
// Split from item_info.cpp to reduce translation-unit size.

#include "item.h"

#include <algorithm>
#include <ranges>
#include <string>

#include "avatar.h"
#include "bionics.h"
#include "bodypart.h"
#include "flag.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "options.h"
#include "output.h"
#include "string_id_utils.h"
#include "translations.h"
#include "type_id.h"
#include "character_stat.h"
#include "units.h"
#include "units_utility.h"

namespace
{
auto nname( const itype_id &id ) -> std::string
{
    return item::nname( id );
}
} // namespace


void item::bionic_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int /*batch*/,
                        bool /*debug*/ ) const
{
    if( !is_bionic() ) {
    return;
}

// TODO: Unhide when enforcing limits
if( get_option < bool >( "CBM_SLOTS_ENABLED" )
        && parts->test( iteminfo_parts::DESCRIPTION_CBM_SLOTS ) ) {
    info.emplace_back( "DESCRIPTION", list_occupied_bps( type->bionic->id,
                       _( "This bionic is installed in the following body "
                          "part(s):" ) ) );
    }
    insert_separation_line( info );

    const bionic_id bid = type->bionic->id;
    const std::vector<itype_id> &fuels = bid->fuel_opts;
    if( !fuels.empty() ) {
    const int &fuel_numb = fuels.size();

        info.emplace_back( "DESCRIPTION",
                           vgettext( "* This bionic can produce power from the following fuel: ",
                                     "* This bionic can produce power from the following fuels: ",
                                     fuel_numb ) + enumerate_as_string( fuels.begin(),
                                             fuels.end(), []( const itype_id & id ) -> std::string { return "<info>" + id->nname( 1 ) + "</info>"; } ) );
    }

    insert_separation_line( info );

    if( bid->capacity > 0_J ) {
    info.emplace_back( "CBM", _( "<bold>Power Capacity</bold>:" ), _( " <num> J" ),
                       iteminfo::no_newline,
                       units::to_joule( bid->capacity ) );
    }

    insert_separation_line( info );

    if( !bid->required_bionics.empty() ) {
    for( const bionic_id &req_bid : bid->required_bionics ) {
            info.emplace_back( "CBM", string_format( "* This CBM requires another CBM to also be installed: %s",
                               req_bid->name ) );
        }
    }

    insert_separation_line( info );

    if( !bid->encumbrance.empty() ) {
    info.emplace_back( "DESCRIPTION", _( "<bold>Encumbrance</bold>: " ),
                       iteminfo::no_newline );
        for( const std::pair< const bodypart_str_id, int > element : sorted_lex( bid->encumbrance ) ) {
            info.emplace_back( "CBM", body_part_name_as_heading( element.first->token, 1 ),
                               " <num> ", iteminfo::no_newline, element.second );
        }
    }

    if( !bid->env_protec.empty() ) {
    info.emplace_back( "DESCRIPTION",
                       bid->activated ? _( "<bold>Environmental Protection (activated)</bold>: " ) :
                       _( "<bold>Environmental Protection</bold>: " ),
                       iteminfo::no_newline );
        for( const std::pair< const bodypart_str_id, int > element : sorted_lex( bid->env_protec ) ) {
            info.emplace_back( "CBM", body_part_name_as_heading( element.first->token, 1 ),
                               " <num> ", iteminfo::no_newline, element.second );
        }
    }

    if( !bid->bash_protec.empty() ) {
    info.emplace_back( "DESCRIPTION",
                       _( "<bold>Bash Protection</bold>: " ),
                       iteminfo::no_newline );
        for( const std::pair< const bodypart_str_id, int > element : sorted_lex( bid->bash_protec ) ) {
            info.emplace_back( "CBM", body_part_name_as_heading( element.first->token, 1 ),
                               " <num> ", iteminfo::no_newline, element.second );
        }
    }

    if( !bid->cut_protec.empty() ) {
    info.emplace_back( "DESCRIPTION",
                       _( "<bold>Cut Protection</bold>: " ),
                       iteminfo::no_newline );
        for( const std::pair< const bodypart_str_id, int > element : sorted_lex( bid->cut_protec ) ) {
            info.emplace_back( "CBM", body_part_name_as_heading( element.first->token, 1 ),
                               " <num> ", iteminfo::no_newline, element.second );
        }
    }

    if( !bid->bullet_protec.empty() ) {
    info.emplace_back( "DESCRIPTION", _( "<bold>Ballistic Protection</bold>: " ),
                       iteminfo::no_newline );
        for( const auto &element : bid->bullet_protec ) {
            info.emplace_back( "CBM", body_part_name_as_heading( element.first->token, 1 ),
                               " <num> ", iteminfo::no_newline, element.second );
        }
    }

    if( !bid->stat_bonus.empty() ) {
    info.emplace_back( "DESCRIPTION", _( "<bold>Stat Bonus</bold>: " ),
                       iteminfo::no_newline );
        for( const auto &element : bid->stat_bonus ) {
            info.emplace_back( "CBM", get_stat_name( element.first ), " <num> ",
                               iteminfo::no_newline, element.second );
        }
    }

    const units::mass weight_bonus = bid->weight_capacity_bonus;
    const float weight_modif = bid->weight_capacity_modifier;
    if( weight_modif != 1 ) {
    std::string modifier;
    if( weight_modif < 1 ) {
            modifier = "<num><bad>x</bad>";
        } else {
            modifier = "<num><color_light_green>x</color>";
        }
        info.emplace_back( "CBM",
                           _( "<bold>Weight capacity modifier</bold>: " ), modifier,
                           iteminfo::no_newline | iteminfo::is_decimal,
                           weight_modif );
    }
    if( weight_bonus != 0_gram ) {
    std::string bonus;
    if( weight_bonus < 0_gram ) {
            bonus = string_format( "<num> <bad>%s</bad>", weight_units() );
        } else {
            bonus = string_format( "<num> <color_light_green>%s</color>", weight_units() );
        }
        info.emplace_back( "CBM", _( "<bold>Weight capacity bonus</bold>: " ), bonus,
                           iteminfo::no_newline | iteminfo::is_decimal,
                           convert_weight( weight_bonus ) );
    }
}

