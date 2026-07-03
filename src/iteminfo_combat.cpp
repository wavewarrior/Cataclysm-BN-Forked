// iteminfo_combat.cpp — combat and damage display info methods.
// Split from item_info.cpp to reduce translation-unit size.

#include "item.h"

#include <algorithm>
#include <numeric>
#include <ranges>
#include <string>

#include "avatar.h"
#include "damage.h"
#include "dispersion.h"
#include "explosion.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "iteminfo_format_utils.h"
#include "iteminfo_query.h"
#include "itype.h"
#include "martialarts.h"
#include "melee.h"
#include "options.h"
#include "output.h"
#include "ranged.h"
#include "skill.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "character_martial_arts.h"
#include "string_id_utils.h"
#include "units_utility.h"

static const skill_id skill_throw( "throw" );

void item::combat_info( std::vector<iteminfo> &info, const iteminfo_query *parts, int /*batch*/,
                        bool /*debug*/ ) const
{
    const std::string space = "  ";
    const std::string newline = "\n";

    bool print_attacks = false;

    // Old behavior - default to it for now
    if( type->attacks.contains( "DEFAULT" ) ) {
        const auto &attack = melee::default_attack( *this );
        int dmg_bash = damage_melee( DT_BASH );
        int dmg_cut = damage_melee( DT_CUT );
        int dmg_stab = damage_melee( DT_STAB );
        if( dmg_bash || dmg_cut || dmg_stab || type->m_to_hit > 0 ) {
            print_attacks = true;
        }

        if( parts->test( iteminfo_parts::BASE_DAMAGE ) ) {
            insert_separation_line( info );
            std::string sep;
            if( dmg_bash || dmg_cut || dmg_stab ) {
                info.emplace_back( "BASE", _( "<bold>Melee damage</bold>: " ), "", iteminfo::no_newline );
            }
            if( dmg_bash ) {
                info.emplace_back( "BASE", _( "Bash: " ), "", iteminfo::no_newline, dmg_bash );
                sep = space;
            }
            if( dmg_cut ) {
                info.emplace_back( "BASE", sep + _( "Cut: " ), "", iteminfo::no_newline, dmg_cut );
                sep = space;
            }
            if( dmg_stab ) {
                info.emplace_back( "BASE", sep + _( "Pierce: " ), "", iteminfo::no_newline, dmg_stab );
            }
        }

        if( dmg_bash || dmg_cut || dmg_stab ) {
            if( parts->test( iteminfo_parts::BASE_TOHIT ) ) {
                info.emplace_back( "BASE", space + _( "To-hit bonus: " ), "",
                                   iteminfo::show_plus, type->m_to_hit + get_melee_hit_bonus() );
            }

            if( parts->test( iteminfo_parts::BASE_MOVES ) ) {
                info.emplace_back( "BASE", _( "Moves per attack: " ), "",
                                   iteminfo::lower_is_better, attack_cost() );
                // This would be a bar if iteminfo was not very insistent on numbers
                info.emplace_back( "BASE", _( "Stamina Cost: " ), "", iteminfo::lower_is_better, stamina_cost() );
                info.emplace_back( "BASE", _( "Typical damage per second:" ), "" );
                const std::map<std::string, double> &dps_data = dps( true, false, attack );
                std::string sep;
                for( const std::pair<const std::string, double> &dps_entry : dps_data ) {
                    info.emplace_back( "BASE", sep + dps_entry.first + ": ", "",
                                       iteminfo::no_newline | iteminfo::is_decimal,
                                       dps_entry.second );
                    sep = space;
                }
                info.emplace_back( "BASE", "" );
            }
        }
    } else {
        print_attacks = true;

        for( const auto &attack_pr : get_attacks() ) {
            const auto &attack = attack_pr.second;

            if( parts->test( iteminfo_parts::BASE_DAMAGE ) ) {
                insert_separation_line( info );
                info.emplace_back( "BASE", _( "<bold>Melee damage</bold>:" ), "", iteminfo::no_newline );
                // if we have any armour penetration numbers, put every damage type on its own line
                bool line_by_line = attack.damage.has_armor_piercing();
                if( line_by_line ) {
                    info.emplace_back( "BASE", newline, "", iteminfo::no_newline );
                } else {
                    info.emplace_back( "BASE", space, "", iteminfo::no_newline );
                }
                damage_statblock_info( info, attack.damage, line_by_line );
            }

            if( parts->test( iteminfo_parts::BASE_TOHIT ) ) {
                info.emplace_back( "BASE", _( "To-hit bonus: " ), "",
                                   iteminfo::show_plus, attack.to_hit + get_melee_hit_bonus() );
            }

            if( parts->test( iteminfo_parts::BASE_MOVES ) ) {
                info.emplace_back( "BASE", _( "Moves per attack: " ), "",
                                   iteminfo::lower_is_better, attack_cost() );
                // This would be a bar if iteminfo was not very insistent on numbers
                info.emplace_back( "BASE", _( "Stamina Cost: " ), "", iteminfo::lower_is_better, stamina_cost() );
                info.emplace_back( "BASE", _( "Typical damage per second:" ), "" );
                const std::map<std::string, double> &dps_data = dps( true, false, attack );
                std::string sep;
                for( const std::pair<const std::string, double> &dps_entry : dps_data ) {
                    info.emplace_back( "BASE", sep + dps_entry.first + ": ", "",
                                       iteminfo::no_newline | iteminfo::is_decimal,
                                       dps_entry.second );
                    sep = space;
                }
                info.emplace_back( "BASE", "" );
            }
        }
    }

    if( parts->test( iteminfo_parts::DESCRIPTION_TECHNIQUES ) ) {
        std::set<matec_id> all_techniques = type->techniques;
        all_techniques.insert( techniques.begin(), techniques.end() );

        if( !all_techniques.empty() ) {
            const std::vector<matec_id> all_tec_sorted = sorted_lex( all_techniques );
            insert_separation_line( info );
            if( has_flag( flag_BLOCK_WHILE_WORN ) ) {
                matec_id tid;
                if( has_technique( matec_id( "WBLOCK_3" ) ) ) {
                    tid = matec_id( "WBLOCK_3" );
                } else if( has_technique( matec_id( "WBLOCK_2" ) ) ) {
                    tid = matec_id( "WBLOCK_2" );
                } else if( has_technique( matec_id( "WBLOCK_1" ) ) ) {
                    tid = matec_id( "WBLOCK_1" );
                }
                if( tid ) {
                    info.emplace_back( "DESCRIPTION", _( "<bold>Techniques when worn</bold>: " ) +
                                       string_format( "<stat>%s</stat>: <info>%s</info>", _( tid.obj().name ),
                                                      _( tid.obj().description ) ) );
                }

            }
            info.emplace_back( "DESCRIPTION", _( "<bold>Techniques when wielded</bold>: " ) +
            enumerate_as_string( all_tec_sorted.begin(), all_tec_sorted.end(), []( const matec_id & tid ) {
                return string_format( "<stat>%s</stat>: <info>%s</info>", _( tid.obj().name ),
                                      _( tid.obj().description ) );
            } ) );
        }
    }

    avatar &you = get_avatar();
    // display which martial arts styles character can use with this weapon
    if( parts->test( iteminfo_parts::DESCRIPTION_APPLICABLEMARTIALARTS ) ) {
        const std::string valid_styles = you.martial_arts_data->enumerate_known_styles( typeId() );
        if( !valid_styles.empty() ) {
            insert_separation_line( info );
            info.emplace_back( "DESCRIPTION",
                               _( "You know how to use this with these martial arts "
                                  "styles: " ) + valid_styles );
        }
    }

    if( !is_gunmod() && has_flag( flag_REACH_ATTACK ) &&
        parts->test( iteminfo_parts::DESCRIPTION_GUNMOD_ADDREACHATTACK ) ) {
        insert_separation_line( info );
        if( has_flag( flag_REACH3 ) ) {
            info.emplace_back( "DESCRIPTION",
                               _( "* This item can be used to make <stat>long reach "
                                  "attacks</stat>." ) );
        } else {
            info.emplace_back( "DESCRIPTION",
                               _( "* This item can be used to make <stat>reach "
                                  "attacks</stat>." ) );
        }
    }

    if( print_attacks || debug_mode ) {
        // @todo Handle multiple attacks
        const attack_statblock &default_attack = melee::default_attack( *this );
        damage_instance non_crit;
        melee::roll_all_damage( you, false, non_crit, true, *this, default_attack );
        damage_instance crit;
        melee::roll_all_damage( you, true, crit, true, *this, default_attack );
        int attack_cost = you.attack_cost( *this );
        insert_separation_line( info );
        if( parts->test( iteminfo_parts::DESCRIPTION_MELEEDMG ) ) {
            info.emplace_back( "DESCRIPTION", _( "<bold>Average melee damage</bold>:" ) );
        }
        // Chance of critical hit
        if( parts->test( iteminfo_parts::DESCRIPTION_MELEEDMG_CRIT ) ) {
            const auto &attack = melee::default_attack( *this );
            info.emplace_back( "DESCRIPTION",
                               string_format( _( "Critical hit chance <neutral>%d%% - %d%%</neutral>" ),
                                              static_cast<int>( you.crit_chance( 0, 100, *this, attack ) *
                                                      100 ),
                                              static_cast<int>( you.crit_chance( 100, 0, *this, attack ) *
                                                      100 ) ) );
        }
        // Bash damage
        if( parts->test( iteminfo_parts::DESCRIPTION_MELEEDMG_BASH ) ) {
            // NOTE: Using "BASE" instead of "DESCRIPTION", so numerical formatting will work
            // (output.cpp:format_item_info does not interpolate <num> for DESCRIPTION info)
            info.emplace_back( "BASE", _( "Bashing: " ), "<num>", iteminfo::no_newline,
                               non_crit.type_damage( DT_BASH ) );
            info.emplace_back( "BASE", space + _( "Critical bash: " ), "<num>", iteminfo::no_flags,
                               crit.type_damage( DT_BASH ) );
        }
        // Cut damage
        if( ( non_crit.type_damage( DT_CUT ) > 0.0f || crit.type_damage( DT_CUT ) > 0.0f )
            && parts->test( iteminfo_parts::DESCRIPTION_MELEEDMG_CUT ) ) {

            info.emplace_back( "BASE", _( "Cutting: " ), "<num>", iteminfo::no_newline,
                               non_crit.type_damage( DT_CUT ) );
            info.emplace_back( "BASE", space + _( "Critical cut: " ), "<num>", iteminfo::no_flags,
                               crit.type_damage( DT_CUT ) );
        }
        // Pierce/stab damage
        if( ( non_crit.type_damage( DT_STAB ) > 0.0f || crit.type_damage( DT_STAB ) > 0.0f )
            && parts->test( iteminfo_parts::DESCRIPTION_MELEEDMG_PIERCE ) ) {

            info.emplace_back( "BASE", _( "Piercing: " ), "<num>", iteminfo::no_newline,
                               non_crit.type_damage( DT_STAB ) );
            info.emplace_back( "BASE", space + _( "Critical pierce: " ), "<num>", iteminfo::no_flags,
                               crit.type_damage( DT_STAB ) );
        }
        // Moves
        if( parts->test( iteminfo_parts::DESCRIPTION_MELEEDMG_MOVES ) ) {
            info.emplace_back( "BASE", _( "Moves per attack: " ), "<num>",
                               iteminfo::lower_is_better, attack_cost );
        }
        insert_separation_line( info );
    }
}

// TODO: Deduplicated with ammo_info()
void item::damage_statblock_info( std::vector<iteminfo> &info, damage_instance attack,
                                  bool line_by_line ) const
{
    const std::string space = "  ";
    const std::string newline = "\n";
    std::string sep;

    /* TODO: All damage types can be defined for an attack, and will be displayed in the item description.
             However, non-physical types will be ignored by the melee damage roll, so they don't actually do any damage.
       TODO: damage_instance isn't ordered, so the damage types will be displayed in whatever order they were defined.
             This will probably be fine, but it might be a good idea to sort the damage_units. */
    for( const auto damage : attack ) {
        if( damage.amount != 0.0 ) {
            info.emplace_back( "BASE", sep + damage.get_name() + _( ": " ), "", iteminfo::no_newline,
                               damage.amount );

            if( damage.res_pen != 0.0 && damage.res_mult != 1.0 ) {
                // Both flat AP and an armor multiplier
                info.emplace_back( "BASE", _( "  Armor-pierce: " ), "", iteminfo::no_newline, damage.res_pen );
                info.emplace_back( "BASE", _( "/" ), "",
                                   iteminfo::no_newline | iteminfo::is_decimal | iteminfo::lower_is_better, damage.res_mult );
            } else if( damage.res_mult != 1.0 ) {
                // Only armor multiplier
                info.emplace_back( "BASE", _( "  Armor multiplier: " ), "",
                                   iteminfo::no_newline | iteminfo::is_decimal | iteminfo::lower_is_better, damage.res_mult );
            } else if( damage.res_pen != 0.0 ) {
                // Only flat AP
                info.emplace_back( "BASE", _( "Armor-pierce: " ), "", iteminfo::no_newline, damage.res_pen );
            }
            sep = line_by_line ? newline : space;
        }
    }
    info.emplace_back( "BASE", sep, "", iteminfo::no_newline );
}

