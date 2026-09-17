#include "avatar.h" // IWYU pragma: associated
#include "newcharacter.h" // IWYU pragma: associated

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <functional>
#include <iosfwd>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "addiction.h"
#include "bionics.h"
#include "cached_options.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "debug.h"
#   include "character_preview.h"
#   include "cata_tiles.h"
#include "character.h"
#include "character_martial_arts.h"
#include "color.h"
#include "cursesdef.h"
#include "enchantments/enchantment.h"
#include "filesystem.h"
#include "fstream_utils.h"
#include "game.h"
#include "game_constants.h"
#include "ime.h"
#include "input.h"
#include "int_id.h"
#include "inventory.h"
#include "json.h"
#include "lightmap.h"
#include "npc_class.h"
#include "magic/magic.h"
#include "make_static.h"
#include "mapsharing.h"
#include "martialarts.h"
#include "monster.h"
#include "mutation.h"
#include "name.h"
#include "options.h"
#include "output.h"
#include "path_info.h"
#include "pimpl.h"
#include "pldata.h"
#include "profession.h"
#include "ranged.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include <RmlUi/Core.h>
#include "rml_screen.h"
#include "rml_util.h"
#include "rng.h"
#include "scenario.h"
#include "sdltiles.h"
#include "skill.h"
#include "start_location.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "translations.h"
#include "type_id.h"
#include "ui.h"
#include "ui_manager.h"
#include "units_utility.h"
#include "veh_type.h"
#include "worldfactory.h"

static const std::string flag_CHALLENGE( "CHALLENGE" );
static const std::string flag_CITY_START( "CITY_START" );
static const std::string flag_SECRET( "SECRET" );

const std::string type_hair_style( "hair_style" );
const std::string type_hair_color( "hair_color" );

static const flag_id json_flag_no_auto_equip( "no_auto_equip" );
static const flag_id json_flag_auto_wield( "auto_wield" );

static const trait_id trait_XS( "XS" );
static const trait_id trait_XXXL( "XXXL" );

static const trait_flag_str_id flag_MALE_EXCLUSIVE( "MALE_EXCLUSIVE" );
static const trait_flag_str_id flag_FEMALE_EXCLUSIVE( "FEMALE_EXCLUSIVE" );
static const trait_flag_str_id flag_MALE_PREFERRED( "MALE_PREFERRED" );
static const trait_flag_str_id flag_FEMALE_PREFERRED( "FEMALE_PREFERRED" );

static auto profession_age_limits_enabled() -> bool
{
    if( world_generator && world_generator->active_world ) {
    return world_generator->active_world->info->WORLD_OPTIONS["ENFORCE_PROFESSION_AGE_RANGE"]
               .value_as<bool>();
    }
    return false;
}

static auto profession_age_bounds( const profession &prof ) -> std::pair<int, int>
{
    if( profession_age_limits_enabled() ) {
    if( const auto range = prof.starting_age_range() ) {
            return { range->min, range->max };
        }
    }
    return { profession::min_age, profession::max_age };
}

static auto random_age_for_profession( const profession &prof ) -> int
{
    const auto [min_age, max_age] = profession_age_bounds( prof );
    if( min_age == max_age ) {
        return min_age;
    }
    return rng( min_age, max_age );
}

static auto scenario_is_selectable( const scenario &scen, const bool cities_enabled ) -> bool
{
    return !scen.scen_is_blacklisted() && ( !scen.has_flag( flag_CITY_START ) || cities_enabled );
}

static auto first_selectable_scenario( const bool cities_enabled ) -> const scenario * // *NOPAD*
{
    const auto &scenarios = scenario::get_all();
    const auto iter = std::ranges::find_if( scenarios, [cities_enabled]( const scenario & scen ) {
        return scenario_is_selectable( scen, cities_enabled );
    } );
    return iter != scenarios.end() ? &( *iter ) : scenario::generic();
}

// Colors used in this file: (Most else defaults to c_light_gray)
#define COL_STAT_ACT        c_white   // Selected stat
#define COL_STAT_BONUS      c_light_green // Bonus
#define COL_STAT_NEUTRAL    c_white   // Neutral Property
#define COL_STAT_PENALTY    c_light_red   // Penalty
#define COL_TR_GOOD         c_green   // Good trait descriptive text
#define COL_TR_GOOD_OFF_ACT c_light_gray  // A toggled-off good trait
#define COL_TR_GOOD_ON_ACT  c_light_green // A toggled-on good trait
#define COL_TR_GOOD_OFF_PAS c_dark_gray  // A toggled-off good trait
#define COL_TR_GOOD_ON_PAS  c_green   // A toggled-on good trait
#define COL_TR_BAD          c_red     // Bad trait descriptive text
#define COL_TR_BAD_OFF_ACT  c_light_gray  // A toggled-off bad trait
#define COL_TR_BAD_ON_ACT   c_light_red   // A toggled-on bad trait
#define COL_TR_BAD_OFF_PAS  c_dark_gray  // A toggled-off bad trait
#define COL_TR_BAD_ON_PAS   c_red     // A toggled-on bad trait
#define COL_TR_NEUT         c_brown     // Neutral trait descriptive text
#define COL_TR_NEUT_OFF_ACT c_dark_gray  // A toggled-off neutral trait
#define COL_TR_NEUT_ON_ACT  c_yellow   // A toggled-on neutral trait
#define COL_TR_NEUT_OFF_PAS c_dark_gray  // A toggled-off neutral trait
#define COL_TR_NEUT_ON_PAS  c_brown     // A toggled-on neutral trait
#define COL_SKILL_USED      c_green   // A skill with at least one point
#define COL_HEADER          c_white   // Captions, like "Profession items"

enum {
    HIGH_STAT = 14 // The point after which stats cost double
};

namespace
{

static bool has_any_magic( const profession_id &prof )
{
    for( spell_type spell : spell_type::get_all() ) {
        if( spell.starting_spell || g->scen->spellquery( spell.id ) ||
            prof->is_allowed_spell( spell.id ) ) {
            return true;
        }
    }
    return false;
}

static int get_max_tabs( const profession_id &prof )
{
    int max = 8;
    if( prof->forbids_bionics() || g->scen->forbids_bionics() ) {
        max--;
    }
    if( !has_any_magic( prof ) || prof->forbids_spells() || g->scen->forbids_spells() ) {
        max--;
    }
    return max;
}
}

int skill_increment_cost( const Character &u, const skill_id &skill );


#include "newcharacter_ui.h"

// Legacy curses screen ported from upstream: every other character-creation
// tab is RmlUi (see newcharacter_ui.cpp); MAGIC is the lone curses holdout.
static void draw_character_tabs( const catacurses::window &w, const std::string &sTab,
                                 const profession_id &prof )
{
    std::vector<std::string> tab_captions = {
        _( "POINTS" ),
        _( "SCENARIO" ),
        _( "PROFESSION" ),
        _( "STATS" ),
        _( "TRAITS" ),
        _( "SKILLS" ),
        _( "OVERVIEW" ),
    };
    if( has_any_magic( prof ) && !( prof->forbids_spells() ||
                                    g->scen->forbids_spells() ) ) {
        tab_captions.insert( tab_captions.begin() + 5, _( "MAGIC" ) );
    }
    if( !prof->forbids_bionics() && !g->scen->forbids_bionics() ) {
        tab_captions.insert( tab_captions.begin() + 5, _( "BIONICS" ) );
    }

    draw_tabs( w, tab_captions, sTab );
    draw_border_below_tabs( w );

    for( int i = 1; i < TERMX - 1; i++ ) {
        mvwputch( w, point( i, 4 ), BORDER_COLOR, LINE_OXOX );
    }
    mvwputch( w, point( 0, 4 ), BORDER_COLOR, LINE_XXXO ); // |-
    mvwputch( w, point( TERMX - 1, 4 ), BORDER_COLOR, LINE_XOXX ); // -|
}
static void draw_points( const catacurses::window &w, points_left &points, int netPointCost = 0 )
{
    // Clear line (except borders)
    mvwprintz( w, point( 2, 3 ), c_black, std::string( getmaxx( w ) - 3, ' ' ) );
    std::string points_msg = points.to_string();
    int pMsg_length = utf8_width( remove_color_tags( points_msg ), true );
    nc_color color = c_light_gray;
    print_colored_text( w, point( 2, 3 ), color, c_light_gray, points_msg );
    if( netPointCost > 0 ) {
        mvwprintz( w, point( pMsg_length + 2, 3 ), c_red, "(-%d)", std::abs( netPointCost ) );
    } else if( netPointCost < 0 ) {
        mvwprintz( w, point( pMsg_length + 2, 3 ), c_green, "(+%d)", std::abs( netPointCost ) );
    }
}

tab_direction set_magic( avatar &u, points_left &points )
{
    ui_adaptor ui;
    catacurses::window w;
    catacurses::window w_description;
    int iContentHeight = 0;
    const auto init_windows = [&]( ui_adaptor & ui ) {
        iContentHeight = TERMY - 6;
        w = catacurses::newwin( TERMY, TERMX, point_zero );
        w_description = catacurses::newwin( iContentHeight, TERMX / 2 - 4, point( TERMX / 2, 5 ) );
        ui.position_from_window( w );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    std::vector<spell_id> sorted_spells;
    for( spell_type spell : spell_type::get_all() ) {
        if( spell.starting_spell || g->scen->spellquery( spell.id ) ||
            u.prof->is_allowed_spell( spell.id ) ) {
            sorted_spells.push_back( spell.id );
        }
    }
    std::ranges::sort( sorted_spells, [&]( const spell_id & left, const spell_id & right ) {
        // Sort by name in class
        // Within a sorted list of classes
        if( left->spell_class == right->spell_class ) {
            return localized_compare( left->name.translated(), right->name.translated() );
        } else {
            if( left->spell_class == trait_id( "NONE" ) ) {
                return true;
            } else if( right->spell_class == trait_id( "NONE" ) ) {
                return false;
            }
            return localized_compare( left->spell_class->name(), right->spell_class->name() );
        }
        return false;
    } );

    // Actual line that the skill takes up.
    int display_line = 0;
    trait_id current_spell_class = trait_id::NULL_ID();
    std::vector<std::pair<spell_id, int>> spell_list;
    for( const spell_id &spell : sorted_spells ) {
        if( current_spell_class != spell->spell_class ) {
            current_spell_class = spell->spell_class;
            display_line++;
        }
        spell_list.emplace_back( spell, display_line );
        display_line++;
    }

    const int num_spells = spell_list.size();
    int cur_offset = 0;
    int cur_pos = 0;
    spell_id current_spell = spell_list[cur_pos].first;
    int selected = 0;

    input_context ctxt( "NEW_CHAR_SKILLS" );
    ctxt.register_cardinal();
    ctxt.register_action( "SCROLL_DOWN" );
    ctxt.register_action( "SCROLL_UP" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "RANDOMIZE" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );

    const int remaining_points_length = utf8_width( points.to_string(), true );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        draw_character_tabs( w, _( "MAGIC" ), u.prof );

        draw_points( w, points );
        // Clear the bottom of the screen.
        werase( w_description );
        mvwprintz( w, point( remaining_points_length + 9, 3 ), c_light_gray,
                   std::string( getmaxx( w ) - remaining_points_length - 10, ' ' ) );

        // Write the hint as to upgrade costs
        const bool knows = u.magic->knows_spell( current_spell );
        const int cost = knows ? current_spell->increase_points : current_spell->starting_points;
        const int level = knows ? u.magic->get_spell( current_spell ).get_level() : 0;
        // We have two different strings to pluralize, so we have to use two
        // translation calls.
        const nc_color color = points.trait_points_left() >= cost ? COL_SKILL_USED : c_light_red;
        mvwprintz( w, point( remaining_points_length + 9, 3 ), color,
                   //~ Second string is e.g. "1 level" or "2 levels"
                   vgettext( "Upgrading %s by 1 level costs %d point",
                             "Upgrading %s by 1 level costs %d points", cost ),
                   current_spell->name, cost );

        std::string rec_disp = current_spell->description.translated();

        const auto vFolded = foldstring( rec_disp, getmaxx( w_description ) );
        int iLines = vFolded.size();

        if( selected < 0 ) {
            selected = 0;
        } else if( iLines < iContentHeight ) {
            selected = 0;
        } else if( selected >= iLines - iContentHeight ) {
            selected = iLines - iContentHeight;
        }

        fold_and_print_from( w_description, point_zero, getmaxx( w_description ),
                             selected, COL_SKILL_USED, rec_disp );

        draw_scrollbar( w, selected, iContentHeight, iLines,
                        point( getmaxx( w ) - 1, 5 ), BORDER_COLOR, true );

        // spell_list[cur_pos].second - 1 so the first skill is considered the first item on scrollbar.
        // display_line - 1 and iContentHeight - 1 compensates for the offset by the prior.
        // same application in the draw_scrolbar function later.
        calcStartPos( cur_offset, spell_list[cur_pos].second - 1, iContentHeight - 1, display_line - 1 );
        current_spell_class = trait_id::NULL_ID();
        for( int i = 0; i < num_spells && spell_list[i].second - cur_offset - 1 < iContentHeight; ++i ) {
            const int y = 5 + spell_list[i].second - cur_offset;
            // Necessary because cur_offset doesn't indicate the first object to read. A bit hacky.
            if( y < 5 ) {
                continue;
            }
            const trait_id &spell_class = spell_list[i].first->spell_class;
            const spell_id &thisspell = spell_list[i].first;
            if( current_spell_class != spell_class && y - 1 >= 5 ) {
                // Clear the line
                mvwprintz( w, point( 2, y - 1 ), c_light_gray, std::string( getmaxx( w ) - 3, ' ' ) );
                if( spell_class == trait_id( "NONE" ) ) {
                    mvwprintz( w, point( 2, y - 1 ), c_yellow, _( "Classless" ) );
                } else {
                    mvwprintz( w, point( 2, y - 1 ), c_yellow, spell_class->name() );
                }
                current_spell_class = spell_class;
            }
            if( y < iContentHeight + 5 ) {
                // Clear the line. 2 for x-coord because category names will be scrolled over.
                mvwprintz( w, point( 2, y ), c_light_gray, std::string( getmaxx( w ) - 3, ' ' ) );
                if( !u.magic->knows_spell( thisspell ) ) {
                    mvwprintz( w, point( 4, y ),
                               ( i == cur_pos ? h_light_gray : c_light_gray ), thisspell->name.translated() );
                } else {
                    mvwprintz( w, point( 4, y ),
                               ( i == cur_pos ? hilite( COL_SKILL_USED ) : COL_SKILL_USED ),
                               thisspell->name.translated() );
                    mvwprintz( w, point( TERMX / 2 - 10, y ),
                               ( i == cur_pos ? hilite( COL_SKILL_USED ) : COL_SKILL_USED ),
                               " (%d)", u.magic->get_spell( thisspell ).get_level() );
                }
            }
        }

        draw_scrollbar( w, spell_list[cur_pos].second - 1, iContentHeight - 1, display_line - 1, point( 0,
                        5 ) );

        wnoutrefresh( w );
        wnoutrefresh( w_description );
    } );

    do {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "DOWN" ) {
            cur_pos = modulo( cur_pos + 1, num_spells );
            current_spell = spell_list[cur_pos].first;
        } else if( action == "UP" ) {
            cur_pos = modulo( cur_pos - 1, num_spells );
            current_spell = spell_list[cur_pos].first;
        } else if( action == "RANDOMIZE" ) {
            cur_pos = modulo( rng( 0, num_spells - 1 ), num_spells );
        } else if( action == "LEFT" ) {
            const spell_id &thisspell = spell_list[cur_pos].first;
            bool knows = u.magic->knows_spell( thisspell );
            const int level = knows ? u.magic->get_spell( thisspell ).get_level() : -1;
            if( knows ) {
                if( level - 1 < 0 ) {
                    u.magic->forget_spell( thisspell );
                    points.trait_points += thisspell->starting_points;
                } else {
                    u.magic->get_spell( thisspell ).set_level( level - 1 );
                    points.trait_points += thisspell->increase_points;
                }
            }
        } else if( action == "RIGHT" ) {
            const spell_id &thisspell = spell_list[cur_pos].first;
            if( !u.magic->can_learn_spell( u, thisspell ) ) {
                popup( _( "You cannot learn this spell!" ) );
            } else if( g->scen->is_forbidden_spell( thisspell ) ) {
                popup( _( "This scenario prevents you from taking this spell" ) );
            } else if( u.prof->is_forbidden_spell( thisspell ) ) {
                popup( _( "This profession prevents you from taking this spell" ) );
            } else {
                const bool knows = u.magic->knows_spell( thisspell );
                const int level = knows ? u.magic->get_spell( thisspell ).get_level() : -1;
                if( level < thisspell->max_starting_level ) {
                    const int cost = knows ? thisspell->increase_points : thisspell->starting_points;
                    points.trait_points -= cost;
                    if( !knows ) {
                        u.magic->learn_spell( thisspell, u, true );
                    } else {
                        u.magic->get_spell( thisspell ).set_level( level + 1 );
                    }
                }
            }
        } else if( action == "SCROLL_DOWN" ) {
            selected++;
        } else if( action == "SCROLL_UP" ) {
            selected--;
        } else if( action == "PREV_TAB" ) {
            return tab_direction::BACKWARD;
        } else if( action == "NEXT_TAB" ) {
            return tab_direction::FORWARD;
        } else if( action == "QUIT" && query_yn( _( "Return to main menu?" ) ) ) {
            return tab_direction::QUIT;
        }
    } while( true );
}

void Character::pick_name( bool bUseDefault )
{
    if( bUseDefault && !get_option<std::string>( "DEF_CHAR_NAME" ).empty() ) {
        name = get_option<std::string>( "DEF_CHAR_NAME" );
    } else {
        name = Name::generate( male );
    }
}

static matype_id choose_ma_style( const character_type type, const std::vector<matype_id> &styles,
                                  const avatar &u )
{
    if( type == character_type::NOW || type == character_type::FULL_RANDOM ) {
        return random_entry( styles );
    }
    if( styles.size() == 1 ) {
        return styles.front();
    }

    input_context ctxt( "MELEE_STYLE_PICKER" );
    ctxt.register_action( "SHOW_DESCRIPTION" );

    uilist menu;
    menu.allow_cancel = false;
    menu.text = string_format( _( "Select a style.\n"
                                  "\n"
                                  "STR: <color_white>%d</color>, DEX: <color_white>%d</color>, "
                                  "PER: <color_white>%d</color>, INT: <color_white>%d</color>\n"
                                  "Press [<color_yellow>%s</color>] for more info.\n" ),
                               u.get_str(), u.get_dex(), u.get_per(), u.get_int(),
                               ctxt.get_desc( "SHOW_DESCRIPTION" ) );
    ma_style_callback callback( 0, styles );
    menu.callback = &callback;
    menu.input_category = "MELEE_STYLE_PICKER";
    menu.additional_actions.emplace_back( "SHOW_DESCRIPTION", translation() );
    menu.desc_enabled = true;

    for( auto &s : styles ) {
        auto &style = s.obj();
        menu.addentry_desc( style.name.translated(), style.description.translated() );
    }
    while( true ) {
        menu.query( true );
        auto &selected = styles[menu.ret];
        if( query_yn( _( "Use this style?" ) ) ) {
            return selected;
        }
    }
}


static void learn_spells( const profession &prof, avatar &you )
{
    for( const std::pair<spell_id, int> spell_pair : prof.spells() ) {
        you.magic->learn_spell( spell_pair.first, you, true );
        spell &sp = you.magic->get_spell( spell_pair.first );
        sp.gain_levels( spell_pair.second );
    }
}

void avatar::randomize( const bool random_scenario, points_left &points, bool play_now )
{
    const int max_stat_points = points.is_freeform() ? 20 : MAX_STAT;
    const int max_trait_points = get_option<int>( "MAX_TRAIT_POINTS" );
    // Reset everything to the defaults to have a clean state.
    *this = avatar();

    male = ( rng( 1, 100 ) > 50 );
    if( !MAP_SHARING::isSharing() ) {
        play_now ? pick_name() : pick_name( true );
    } else {
        name = MAP_SHARING::getUsername();
    }
    // if adjusting min and max height from 145 and 200, make sure to see set_description()
    init_height = rng( 145, 200 );
    const auto cities_enabled =
        world_generator->active_world->info->WORLD_OPTIONS["CITY_SIZE"].getValue() !=
        "0";
    if( random_scenario ) {
        std::vector<const scenario *> scenarios;
        for( const auto &scen : scenario::get_all() ) {
            if( !scen.has_flag( flag_CHALLENGE ) && scenario_is_selectable( scen, cities_enabled ) ) {
                scenarios.emplace_back( &scen );
            }
        }
        if( scenarios.empty() ) {
            g->scen = first_selectable_scenario( cities_enabled );
        } else {
            g->scen = random_entry( scenarios );
        }
    } else if( !scenario_is_selectable( *g->scen, cities_enabled ) ) {
        g->scen = first_selectable_scenario( cities_enabled );
    }

    prof = g->scen->weighted_random_profession();
    random_start_location = true;
    // if adjusting min and max age from 16 and 55, make sure to see set_description()
    set_base_age( random_age_for_profession( *prof ) );

    str_max = rng( 6, HIGH_STAT - 2 );
    dex_max = rng( 6, HIGH_STAT - 2 );
    int_max = rng( 6, HIGH_STAT - 2 );
    per_max = rng( 6, HIGH_STAT - 2 );
    points.stat_points = points.stat_points - str_max - dex_max - int_max - per_max;
    points.skill_points = points.skill_points - prof->point_cost() - g->scen->point_cost();
    // The default for each stat is 8, and that default does not cost any points.
    // Values below give points back, values above require points. The line above has removed
    // to many points, therefore they are added back.
    points.stat_points += 8 * 4;

    int num_gtraits = 0;
    int num_btraits = 0;
    int tries = 0;
    newcharacter::add_traits( *this, points ); // adds mandatory profession/scenario traits.
    for( const trait_id &mut : get_mutations() ) {
        const mutation_branch &mut_info = mut.obj();
        if( mut_info.profession ) {
            continue;
        }
        // Scenario/profession traits do not cost any points, but they are counted toward
        // the limit (MAX_TRAIT_POINTS)
        if( mut_info.points >= 0 ) {
            num_gtraits += mut_info.points;
        } else {
            num_btraits -= mut_info.points;
        }
    }

    /* The loops variable is used to prevent the algorithm running in an infinite loop */
    unsigned int loops = 0;

    while( loops <= 100000 && ( !points.is_valid() || rng( -3, 20 ) > points.skill_points_left() ) ) {
        loops++;
        trait_id rn;
        if( num_btraits < max_trait_points && one_in( 3 ) ) {
            tries = 0;
            do {
                rn = newcharacter::random_bad_trait();
                tries++;
            } while( ( has_trait( rn ) || num_btraits - rn->points > max_trait_points ) &&
                     tries < 5 );

            if( tries < 5 && !newcharacter::has_conflicting_trait( *this, rn ) ) {
                toggle_trait( rn );
                points.trait_points -= rn->points;
                num_btraits -= rn->points;
            }
        } else {
            switch( rng( 1, 4 ) ) {
                case 1:
                    if( str_max > 5 ) {
                        str_max--;
                        points.stat_points++;
                    }
                    break;
                case 2:
                    if( dex_max > 5 ) {
                        dex_max--;
                        points.stat_points++;
                    }
                    break;
                case 3:
                    if( int_max > 5 ) {
                        int_max--;
                        points.stat_points++;
                    }
                    break;
                case 4:
                    if( per_max > 5 ) {
                        per_max--;
                        points.stat_points++;
                    }
                    break;
            }
        }
    }

    loops = 0;
    while( points.has_spare() && loops <= 100000 ) {
        const bool allow_stats = points.stat_points_left() > 0;
        const bool allow_traits = points.trait_points_left() > 0 && num_gtraits < max_trait_points;
        int r = rng( 1, 9 );
        trait_id rn;
        switch( r ) {
            case 1:
            case 2:
            case 3:
            case 4:
                if( allow_traits ) {
                    rn = newcharacter::random_good_trait();
                    auto &mdata = rn.obj();
                    if( !has_trait( rn ) && points.trait_points_left() >= mdata.points &&
                        num_gtraits + mdata.points <= max_trait_points &&
                        !newcharacter::has_conflicting_trait( *this, rn ) ) {
                        toggle_trait( rn );
                        points.trait_points -= mdata.points;
                        num_gtraits += mdata.points;
                    }
                    break;
                }
            /* fallthrough */
            case 5:
                if( allow_stats ) {
                    switch( rng( 1, 4 ) ) {
                        case 1:
                            if( str_max < HIGH_STAT ) {
                                str_max++;
                                points.stat_points--;
                            } else if( points.stat_points_left() >= 2 && str_max < max_stat_points ) {
                                str_max++;
                                points.stat_points = points.stat_points - 2;
                            }
                            break;
                        case 2:
                            if( dex_max < HIGH_STAT ) {
                                dex_max++;
                                points.stat_points--;
                            } else if( points.stat_points_left() >= 2 && dex_max < max_stat_points ) {
                                dex_max++;
                                points.stat_points = points.stat_points - 2;
                            }
                            break;
                        case 3:
                            if( int_max < HIGH_STAT ) {
                                int_max++;
                                points.stat_points--;
                            } else if( points.stat_points_left() >= 2 && int_max < max_stat_points ) {
                                int_max++;
                                points.stat_points = points.stat_points - 2;
                            }
                            break;
                        case 4:
                            if( per_max < HIGH_STAT ) {
                                per_max++;
                                points.stat_points--;
                            } else if( points.stat_points_left() >= 2 && per_max < max_stat_points ) {
                                per_max++;
                                points.stat_points = points.stat_points - 2;
                            }
                            break;
                    }
                    break;
                }
            /* fallthrough */
            case 6:
            case 7:
            case 8:
            case 9:
                const skill_id aSkill = Skill::random_skill();
                const int level = get_skill_level( aSkill, true );

                if( level < points.skill_points_left() && level < MAX_SKILL && loops > 10000 ) {
                    points.skill_points -= skill_increment_cost( *this, aSkill );
                    // For balance reasons, increasing a skill from level 0 gives you 1 extra level for free
                    set_skill_level( aSkill, ( level == 0 ? 2 : level + 1 ) );
                }
                break;
        }
        loops++;
    }

    randomize_cosmetics();
    set_body();
}

void Character::clear_cosmetic_traits( std::string mutation_type, trait_id new_trait )
{
    for( const mutation_branch &mb : mutation_branch::get_all() ) {
        if( mb.points == 0 && mb.types.contains( mutation_type ) ) {
            if( has_trait( mb.id ) && mb.id != new_trait ) {
                toggle_trait( mb.id );
            }
        }
    }
}

namespace
{

auto set_cosmetic_trait( Character &c, std::string mutation_type, const trait_id &trait ) -> void
{
    if( trait.is_valid() ) {
        c.clear_cosmetic_traits( mutation_type, trait );

        if( !c.has_trait( trait ) ) {
            c.toggle_trait( trait );
        }
    }
}


auto default_hair_style_for( const avatar &u ) -> trait_id
{
    static const auto male_default_hair_style = trait_id( "hair_medium" );
    static const auto female_default_hair_style = trait_id( "hair_long" );
    return u.male ? male_default_hair_style : female_default_hair_style;
}

auto set_default_hair_style( avatar &u ) -> void
{
    set_cosmetic_trait( u, type_hair_style, default_hair_style_for( u ) );
}


} // namespace

auto selected_cosmetic_trait( const Character &c,
                              const std::string &mutation_type ) -> std::optional<trait_id>
{
    const auto mutations = get_mutations_in_type( mutation_type );
    const auto has_trait = std::bind_front( &Character::has_trait, &c );
    const auto selected = std::ranges::find_if( mutations, has_trait );
    return selected != mutations.end() ? std::optional<trait_id>( *selected ) : std::nullopt;
}

auto restore_cosmetic_trait( Character &c, const std::string &mutation_type,
                             const std::optional<trait_id> &trait ) -> void
{
    if( trait && g->scen->traitquery( *trait ) ) {
        set_cosmetic_trait( c, mutation_type, *trait );
    }
}

auto restore_or_default_hair_style( avatar &u, const std::optional<trait_id> &hair_style ) -> void
{
    restore_cosmetic_trait( u, type_hair_style, hair_style );
    if( !selected_cosmetic_trait( u, type_hair_style ) ) {
        set_default_hair_style( u );
    }
}

void avatar::randomize_cosmetics()
{
    std::ranges::for_each( get_all_mutation_type_ids(), [this]( const std::string & type_id ) {
        const bool mandatory = mutation_type_is_mandatory( type_id );
        const int chance = mutation_type_random_chance( type_id );
        if( mandatory || ( chance > 0 && x_in_y( chance, 100 ) ) ) {
            randomize_cosmetic_trait( type_id );
        }
    } );
}

bool avatar::create( character_type type, const std::string &tempname )
{
    // TODO: This block should not be needed
    if( get_body().contains( body_part_arm_r ) ) {
        remove_primary_weapon();
    }

    prof = profession::generic();
    g->scen = scenario::generic();
    male = get_option<std::string>( "DEF_CHAR_GENDER" ) == "male";

    const bool interactive = type != character_type::NOW &&
                             type != character_type::FULL_RANDOM;

    int tab = 0;
    points_left points = points_left();

    switch( type ) {
        case character_type::CUSTOM:
            // We can randomize cosmetics for a custom character, it's fine. Not sure I like the idea of a "default" appearance
            randomize_cosmetics();
            // don't make them bald!
            set_default_hair_style( *this );
            break;
        case character_type::RANDOM:
            //random scenario, default name if exist
            randomize( true, points );
            tab = get_max_tabs( prof );
            break;
        case character_type::NOW:
            //default world, fixed scenario, random name
            randomize( false, points, true );
            break;
        case character_type::FULL_RANDOM:
            //default world, random scenario, random name
            randomize( true, points, true );
            break;
        case character_type::TEMPLATE:
            if( !load_template( tempname, points ) ) {
                return false;
            }
            // We want to prevent recipes known by the template from being applied to the
            // new character. The recipe list will be rebuilt when entering the game.
            // Except if it is a character transfer template
            if( points.limit != points_left::TRANSFER ) {
                learned_recipes->clear();
            }
            tab = get_max_tabs( prof );
            break;
    }

    auto nameExists = [&]( const std::string & name ) {
        return world_generator->active_world->info->save_exists( save_t::from_save_id( name ) ) &&
               !query_yn( _( "A save with the name '%s' already exists in this world.\n"
                      "Saving will overwrite the already existing character.\n\n"
                      "Continue anyways?" ), name );
    };
    set_body();
    const bool allow_reroll = true;
    tab_direction result = tab_direction::QUIT;
    do {
        if( !interactive ) {
            // no window is created because "Play now"  does not require any configuration
            if( nameExists( name ) ) {
                return false;
            }

            break;
        }

        if( points.limit == points_left::TRANSFER ) {
            tab = get_max_tabs( prof );
        }

        switch( tab ) {
            case 0:
                result = set_points( *this, points );
                break;
            case 1:
                result = set_scenario( *this, points, result );
                break;
            case 2:
                result = set_profession( *this, points, result );
                break;
            case 3:
                result = set_stats( *this, points );
                break;
            case 4:
                result = set_traits( *this, points );
                break;
            case 5:
                if( prof->forbids_bionics() || g->scen->forbids_bionics() ) {
                    if( !has_any_magic( prof ) ||
                        prof->forbids_spells() || g->scen->forbids_spells() ) {
                        result = set_skills( *this, points );
                    } else {
                        result = set_magic( *this, points );
                    }
                } else {
                    result = set_bionics( *this, points );
                }
                break;
            case 6:
                if( prof->forbids_bionics() || g->scen->forbids_bionics() ) {
                    if( !has_any_magic( prof ) ||
                        prof->forbids_spells() || g->scen->forbids_spells() ) {
                        result = set_description( *this, allow_reroll, points );
                    } else {
                        result = set_skills( *this, points );
                    }
                } else if( !has_any_magic( prof ) ||
                           prof->forbids_spells() || g->scen->forbids_spells() ) {
                    result = set_skills( *this, points );
                } else {
                    result = set_magic( *this, points );
                }
                break;
            case 7:
                if( prof->forbids_bionics() || g->scen->forbids_bionics() ) {
                    result = set_description( *this, allow_reroll, points );
                } else if( !has_any_magic( prof ) ||
                           prof->forbids_spells() || g->scen->forbids_spells() ) {
                    result = set_description( *this, allow_reroll, points );
                } else {
                    result = set_skills( *this, points );
                }
                break;
            case 8:
                result = set_description( *this, allow_reroll, points );
                break;
        }

        switch( result ) {
            case tab_direction::NONE:
                break;
            case tab_direction::FORWARD:
                tab++;
                break;
            case tab_direction::BACKWARD:
                tab--;
                break;
            case tab_direction::QUIT:
                tab = -1;
                break;
        }

        if( !( tab >= 0 && tab <= get_max_tabs( prof ) ) ) {
            if( tab != -1 && nameExists( name ) ) {
                tab = get_max_tabs( prof );
            } else {
                break;
            }
        }

    } while( true );

    if( tab < 0 ) {
        return false;
    }

    if( points.limit == points_left::TRANSFER ) {
        return true;
    }

    save_template( _( "Last Character" ), points );

    recalc_hp();

    remove_primary_weapon( );

    // Grab the skills from the profession, if there are any
    // We want to do this before the recipes
    for( const profession::StartingSkill &e : prof->skills() ) {
        mod_skill_level( e.first, e.second );
    }

    // setup staring bank money
    cash = prof->starting_cash().value_or( rng( -200000, 200000 ) );

    if( has_trait( trait_XS ) ) {
        set_stored_kcal( 10000 );
        toggle_trait( trait_XS );
    }
    if( has_trait( trait_XXXL ) ) {
        set_stored_kcal( 125000 );
        toggle_trait( trait_XXXL );
    }

    // Learn recipes
    for( const auto &e : recipe_dict ) {
        const auto &r = e.second;
        if( !r.has_flag( flag_SECRET ) && !knows_recipe( &r ) && has_recipe_requirements( r ) ) {
            learn_recipe( &r );
        }
    }
    for( mtype_id elem : prof->pets() ) {
        starting_pets.push_back( elem );
    }

    if( g->scen->vehicle() != vproto_id::NULL_ID() ) {
        starting_vehicle = g->scen->vehicle();
    } else {
        starting_vehicle = prof->vehicle();
    }

    std::vector<detached_ptr<item>> prof_items = prof->items( male, get_mutations() );

    for( detached_ptr<item> &it : prof_items ) {
        if( it->has_flag( STATIC( flag_id( "WET" ) ) ) ) {
            it->activate();
            it->set_counter( 450 ); // Give it some time to dry off
        }
        if( it->is_book() ) {
            items_identified.insert( it->typeId() );
        }
        // TODO: debugmsg if food that isn't a seed is inedible
        if( it->has_flag( json_flag_no_auto_equip ) ) {
            it->unset_flag( json_flag_no_auto_equip );
            inv.add_item( std::move( it ), false );
        } else if( it->has_flag( json_flag_auto_wield ) ) {
            it->unset_flag( json_flag_auto_wield );
            if( !is_armed() ) {
                wield( std::move( it ) );
            } else {
                inv.add_item( std::move( it ), false );
            }
        } else if( it->is_armor() ) {
            // TODO: debugmsg if wearing fails
            wear_item( std::move( it ), false );
        } else {
            inv.add_item( std::move( it ), false );
        }
    }

    std::vector<addiction> prof_addictions = prof->addictions();
    for( std::vector<addiction>::const_iterator iter = prof_addictions.begin();
         iter != prof_addictions.end(); ++iter ) {
        addictions.push_back( *iter );
    }

    for( const bionic_id &bio : prof->CBMs() ) {
        add_bionic( bio );
    }
    // Adjust current energy level to maximum
    set_power_level( get_max_power_level() );

    for( const trait_id &t : get_base_traits() ) {
        std::vector<matype_id> styles;
        for( const matype_id &s : t->initial_ma_styles ) {
            if( !martial_arts_data->has_martialart( s ) ) {
                styles.push_back( s );
            }
        }
        if( !styles.empty() ) {
            const matype_id ma_type = choose_ma_style( type, styles, *this );
            martial_arts_data->add_martialart( ma_type );
            martial_arts_data->set_style( ma_type );
        }
    }

    // Activate some mutations right from the start.
    for( const trait_id &mut : get_mutations() ) {
        const auto &branch = mut.obj();
        if( branch.starts_active ) {
            my_mutations[mut].powered = true;
        }
    }

    learn_spells( *prof, *this );

    // Ensure that persistent morale effects (e.g. Optimist) are present at the start.
    apply_persistent_morale();

    return true;
}




// --- RmlUi render path (Tier 4 screen #4: new-character creator, sliced) -----
// One toggle lights every character-creation tab; each tab's on_redraw guards
// `if(rml){sync_rml();return;}` (slice 1 = the POINTS tab, set_points). Each tab
/** Handle the profession tab of the character generation menu */
// RmlUi model for the PROFESSION tab (slice 7). Single list + a big scrollable
// info buffer (items/skills/traits/bionics/...). Distinct per-model types.

// RmlUi model for the OVERVIEW tab (slice 8, the last newcharacter tab). The
// final summary form: editable name/height/age (selector-highlighted) + gender +
// location + scenario/profession + the six read-only summary panes (stats /
// skills / traits / bionics / misc / gear). Render-only doc; each pane is one
// colour-tagged string mirroring the curses block verbatim (the profession
// info_rml approach). Distinct per-model tab struct (RegisterStruct is
// context-global; worldfactory precedent).
std::vector<trait_id> Character::get_base_traits() const
{
    return std::vector<trait_id>( my_traits.begin(), my_traits.end() );
}

std::vector<trait_id> Character::get_mutations( bool include_hidden ) const
{
    std::vector<trait_id> result;
    for( const std::pair<const trait_id, char_trait_data> &t : my_mutations ) {
        if( include_hidden || t.first.obj().player_display ) {
            result.push_back( t.first );
        }
    }
    for( const trait_id &ench_trait : enchantment_cache->get_mutations() ) {
        if( include_hidden || ench_trait->player_display ) {
            bool found = false;
            for( const trait_id &exist : result ) {
                if( exist == ench_trait ) {
                    found = true;
                    break;
                }
            }
            if( !found ) {
                result.push_back( ench_trait );
            }
        }
    }
    return result;
}

void Character::clear_mutations()
{
    while( !my_traits.empty() ) {
        toggle_trait( *my_traits.begin() );
    }
    while( !my_mutations.empty() ) {
        unset_mutation( my_mutations.begin()->first );
    }
    cached_mutations.clear();
}

void Character::clear_skills()
{
    for( auto &sk : *_skills ) {
        sk.second.level( 0 );
    }
}

void newcharacter::add_traits( Character &ch )
{
    points_left points = points_left();
    add_traits( ch, points );
}

void newcharacter::add_traits( Character &ch, points_left &points )
{
    for( const trait_id &tr : ch.prof->get_locked_traits() ) {
        if( !ch.has_trait( tr ) ) {
            ch.toggle_trait( tr );
        } else {
            points.trait_points += tr->points;
        }
    }
    for( const trait_id &tr : g->scen->get_locked_traits() ) {
        if( !ch.has_trait( tr ) ) {
            ch.toggle_trait( tr );
        }
    }
}

trait_id newcharacter::random_good_trait()
{
    std::vector<trait_id> vTraitsGood;

    for( auto &traits_iter : mutation_branch::get_all() ) {
        if( traits_iter.points > 0 && g->scen->traitquery( traits_iter.id ) &&
            traits_iter.randomstartingtrait ) {
            vTraitsGood.push_back( traits_iter.id );
        }
    }

    return random_entry( vTraitsGood );
}

trait_id newcharacter::random_bad_trait()
{
    std::vector<trait_id> vTraitsBad;

    for( auto &traits_iter : mutation_branch::get_all() ) {
        if( traits_iter.points < 0 && g->scen->traitquery( traits_iter.id ) &&
            traits_iter.randomstartingtrait ) {
            vTraitsBad.push_back( traits_iter.id );
        }
    }

    return random_entry( vTraitsBad );
}

trait_id Character::get_random_trait( const std::function<bool( const mutation_branch & )> &func )
{
    std::vector<trait_id> vTraits;

    for( const mutation_branch &traits_iter : mutation_branch::get_all() ) {
        if( func( traits_iter ) ) {
            vTraits.push_back( traits_iter.id );
        }
    }

    return random_entry( vTraits );
}


auto newcharacter::add_default_mutation_type_traits( Character &ch ) -> void
{
for( const auto &default_mutation : get_default_mutations_for_types() ) {
    const auto mutations = get_mutations_in_type( default_mutation.type_id );
        const auto has_mutation_type = std::ranges::any_of( mutations, [&]( const auto & trait ) {
            return ch.has_trait( trait );
        } );
        if( !has_mutation_type && default_mutation.trait.is_valid() ) {
            if( ch.has_base_trait( default_mutation.trait ) ) {
                ch.set_mutation( default_mutation.trait );
            } else {
                ch.toggle_trait( default_mutation.trait );
            }
        }
    }
}

void Character::randomize_cosmetic_trait( std::string mutation_type )
{
    trait_id trait = get_random_trait( [&]( const mutation_branch & mb ) {
        if( mb.points != 0 || !mb.types.contains( mutation_type ) ) {
            return false;
        }
        if( male ) {
            return !mb.flags.contains( flag_FEMALE_EXCLUSIVE ) &&
                   !mb.flags.contains( flag_FEMALE_PREFERRED );
        } else {
            return !mb.flags.contains( flag_MALE_EXCLUSIVE ) &&
                   !mb.flags.contains( flag_MALE_PREFERRED );
        }
    } );

    if( trait.is_valid() ) {
        clear_cosmetic_traits( mutation_type, trait );

        if( !has_trait( trait ) ) {
            toggle_trait( trait );
        }
    }
}

void avatar::character_to_template( const std::string &name )
{
    points_left points;
    points.stat_points = 0;
    points.trait_points = 0;
    points.skill_points = 0;
    points.limit = points_left::TRANSFER;
    save_template( name, points );
}

void avatar::save_template( const std::string &name, const points_left &points )
{
    std::string name_san = ensure_valid_file_name( name );
    write_to_file( PATH_INFO::templatedir() + name_san + ".template", [&]( std::ostream & fout ) {
        JsonOut jsout( fout, true );

        jsout.start_array();

        jsout.start_object();
        jsout.member( "stat_points", points.stat_points );
        jsout.member( "trait_points", points.trait_points );
        jsout.member( "skill_points", points.skill_points );
        jsout.member( "limit", points.limit );
        jsout.member( "starting_vehicle", starting_vehicle );
        jsout.member( "random_start_location", random_start_location );
        if( !random_start_location ) {
            jsout.member( "start_location", start_location );
        }
        jsout.end_object();

        serialize( jsout );

        jsout.end_array();
    }, _( "player template" ) );
}

bool avatar::load_template( const std::string &template_name, points_left &points )
{
    return read_from_file_json( PATH_INFO::templatedir() + template_name +
    ".template", [&]( JsonIn & jsin ) {

        if( jsin.test_array() ) {
            // not a legacy template
            jsin.start_array();

            if( jsin.end_array() ) {
                return;
            }

            JsonObject jobj = jsin.get_object();

            points.stat_points = jobj.get_int( "stat_points" );
            points.trait_points = jobj.get_int( "trait_points" );
            points.skill_points = jobj.get_int( "skill_points" );
            points.limit = static_cast<points_left::point_limit>( jobj.get_int( "limit" ) );

            if( jobj.has_member( "starting_vehicle" ) ) {
                starting_vehicle = vproto_id( jobj.get_string( "starting_vehicle" ) );
            } else {
                starting_vehicle = vproto_id::NULL_ID();
            }
            random_start_location = jobj.get_bool( "random_start_location", true );
            const std::string jobj_start_location = jobj.get_string( "start_location", "" );

            // g->scen->allowed_start( loc.ident() ) is checked once scenario loads in avatar::load()
            for( const auto &loc : start_locations::get_all() ) {
                if( loc.id.str() == jobj_start_location ) {
                    random_start_location = false;
                    this->start_location = loc.id;
                    break;
                }
            }

            if( jsin.end_array() ) {
                return;
            }
        } else {
            points.stat_points = 0;
            points.trait_points = 0;
            points.skill_points = 0;
        }

        deserialize( jsin );

        if( MAP_SHARING::isSharing() ) {
            // just to make sure we have the right name
            name = MAP_SHARING::getUsername();
        }
    } );
}

points_left::points_left()
{
    limit = MULTI_POOL;
    init_from_options();
}

void points_left::init_from_options()
{
    stat_points = get_option<int>( "INITIAL_STAT_POINTS" );
    trait_points = get_option<int>( "INITIAL_TRAIT_POINTS" );
    skill_points = get_option<int>( "INITIAL_SKILL_POINTS" );
}

// Highest amount of points to spend on stats without points going invalid
int points_left::stat_points_left() const
{
    switch( limit ) {
    case FREEFORM:
    case ONE_POOL:
        return stat_points + trait_points + skill_points;
    case MULTI_POOL:
        return std::min( trait_points_left(),
                         stat_points + std::min( 0, trait_points + skill_points ) );
        case TRANSFER:
            return 0;
    }

    return 0;
}

int points_left::trait_points_left() const
{
    switch( limit ) {
    case FREEFORM:
    case ONE_POOL:
        return stat_points + trait_points + skill_points;
    case MULTI_POOL:
        return stat_points + trait_points + std::min( 0, skill_points );
        case TRANSFER:
            return 0;
    }

    return 0;
}

int points_left::skill_points_left() const
{
    return stat_points + trait_points + skill_points;
}

bool points_left::is_freeform()
{
    return limit == FREEFORM;
}

bool points_left::is_valid()
{
    return is_freeform() ||
           ( stat_points_left() >= 0 && trait_points_left() >= 0 &&
             skill_points_left() >= 0 );
}

bool points_left::has_spare()
{
    return !is_freeform() && is_valid() && skill_points_left() > 0;
}

std::string points_left::to_string()
{
    if( limit == MULTI_POOL ) {
        return string_format(
                   _( "Points left: <color_%s>%d</color>%c<color_%s>%d</color>%c<color_%s>%d</color>=<color_%s>%d</color>" ),
                   stat_points_left() >= 0 ? "light_gray" : "red", stat_points,
                   trait_points >= 0 ? '+' : '-',
                   trait_points_left() >= 0 ? "light_gray" : "red", std::abs( trait_points ),
                   skill_points >= 0 ? '+' : '-',
                   skill_points_left() >= 0 ? "light_gray" : "red", std::abs( skill_points ),
                   is_valid() ? "light_gray" : "red", stat_points + trait_points + skill_points );
    } else if( limit == ONE_POOL ) {
        return string_format( _( "Points left: %4d" ), skill_points_left() );
    } else if( limit == TRANSFER ) {
        return _( "Character Transfer: No changes can be made." );
    } else {
        return _( "Freeform" );
    }
}

namespace newcharacter
{

} // namespace newcharacter
