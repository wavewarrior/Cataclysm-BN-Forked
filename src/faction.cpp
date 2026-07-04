#include "faction.h"

#include <bitset>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "avatar.h"
#include "bionics.h"
#include "character.h"
#include "cursesdef.h"
#include "debug.h"
#include "game.h"
#include "game_constants.h"
#include "input.h"
#include "item.h"
#include "json.h"
#include "line.h"
#include "mtype.h"
#include "npc.h"
#include "output.h"
#include "overmapbuffer.h"
#include "overmapbuffer_registry.h"
#include "pimpl.h"
#include "player.h"
#include "point.h"
#include "skill.h"
#include "string_formatter.h"
#include "text_snippets.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"

#include <RmlUi/Core.h>

#include "rml_screen.h"
#include "rml_util.h"

static const bionic_id bio_infolink( "bio_infolink" );

namespace npc_factions
{
std::vector<faction_template> all_templates;
} // namespace npc_factions

faction_template::faction_template()
{
    likes_u_ = 0;
    respects_u_ = 0;
    known_by_u_ = true;
    food_supply_ = 0;
    wealth_ = 0;
    size_ = 0;
    power_ = 0;
    lone_wolf_faction_ = false;
    currency_ = itype_id::NULL_ID();
}

faction::faction( const faction_template &templ )
{
    id_ = templ.id_;
    // first init *all* members, than copy those from the template
    static_cast<faction_template &>( *this ) = templ;
}

void faction_template::load( const JsonObject &jsobj )
{
    faction_template fac( jsobj );
    npc_factions::all_templates.emplace_back( fac );
}

void faction_template::check_consistency()
{
    for( const faction_template &fac : npc_factions::all_templates ) {
        for( const auto &epi : fac.epilogue_data_ ) {
            if( !epi.id.is_valid() ) {
                debugmsg( "There's no snippet with id %s", epi.id.str() );
            }
        }
    }
}

void faction_template::reset()
{
    npc_factions::all_templates.clear();
}

void faction_template::load_relations( const JsonObject &jsobj )
{
    for( const JsonMember fac : jsobj.get_object( "relations" ) ) {
        JsonObject rel_jo = fac.get_object();
        std::bitset<npc_factions::rel_types> fac_relation( 0 );
        for( const auto &rel_flag : npc_factions::relation_strs ) {
            fac_relation.set( rel_flag.second, rel_jo.get_bool( rel_flag.first, false ) );
        }
        relations_[fac.name()] = fac_relation;
    }
}

faction_template::faction_template( const JsonObject &jsobj )
    : name_( jsobj.get_string( "name" ) )
    , likes_u_( jsobj.get_int( "likes_u" ) )
    , respects_u_( jsobj.get_int( "respects_u" ) )
    , known_by_u_( jsobj.get_bool( "known_by_u" ) )
    , id_( faction_id( jsobj.get_string( "id" ) ) )
    , desc_( jsobj.get_string( "description" ) )
    , size_( jsobj.get_int( "size" ) )
    , power_( jsobj.get_int( "power" ) )
    , food_supply_( jsobj.get_int( "food_supply" ) )
    , wealth_( jsobj.get_int( "wealth" ) )
{
    if( jsobj.has_string( "currency" ) ) {
        jsobj.read( "currency", currency_, true );
    } else {
        currency_ = itype_id::NULL_ID();
    }
    lone_wolf_faction_ = jsobj.get_bool( "lone_wolf_faction", false );
    load_relations( jsobj );
    mon_faction_ = mfaction_str_id( jsobj.get_string( "mon_faction", "human" ) );
    for( const JsonObject jao : jsobj.get_array( "epilogues" ) ) {
        epilogue_data_.emplace( faction_epilogue{
            .power_min = jao.get_int( "power_min", std::numeric_limits<int>::min() ),
            .power_max = jao.get_int( "power_max", std::numeric_limits<int>::max() ),
            .id = snippet_id( jao.get_string( "id", "epilogue_faction_default" ) )
        } );
    }
}

std::string faction::describe() const
{
    std::string ret = _( desc_ );
    return ret;
}

std::vector<std::string> faction::epilogue() const
{
    std::vector<std::string> ret;
    for( const faction_epilogue &epilogue_entry : epilogue_data_ ) {
        if( power_ >= epilogue_entry.power_min && power_ < epilogue_entry.power_max ) {
            ret.emplace_back( epilogue_entry.id->translated() );
        }
    }
    return ret;
}

void faction::add_to_membership( const character_id &guy_id, const std::string &guy_name,
                                 const bool known )
{
    members[guy_id] = std::make_pair( guy_name, known );
}

void faction::remove_member( const character_id &guy_id )
{
    for( auto it = members.cbegin(), next_it = it; it != members.cend(); it = next_it ) {
        ++next_it;
        if( guy_id == it->first ) {
            members.erase( it );
            break;
        }
    }
    if( members.empty() ) {
        for( const faction_template &elem : npc_factions::all_templates ) {
            // This is a templated base faction - don't delete it, just leave it as zero members for now.
            // Only want to delete dynamically created factions.
            if( elem.id_ == id_ ) {
                return;
            }
        }
        g->faction_manager_ptr->remove_faction( id_ );
    }
}

// Used in game.cpp
std::string fac_ranking_text( int val )
{
    if( val <= -100 ) {
        return _( "Archenemy" );
    }
    if( val <= -80 ) {
        return _( "Wanted Dead" );
    }
    if( val <= -60 ) {
        return _( "Enemy of the People" );
    }
    if( val <= -40 ) {
        return _( "Wanted Criminal" );
    }
    if( val <= -20 ) {
        return _( "Not Welcome" );
    }
    if( val <= -10 ) {
        return _( "Pariah" );
    }
    if( val <= -5 ) {
        return _( "Disliked" );
    }
    if( val >= 100 ) {
        return _( "Hero" );
    }
    if( val >= 80 ) {
        return _( "Idol" );
    }
    if( val >= 60 ) {
        return _( "Beloved" );
    }
    if( val >= 40 ) {
        return _( "Highly Valued" );
    }
    if( val >= 20 ) {
        return _( "Valued" );
    }
    if( val >= 10 ) {
        return _( "Well-Liked" );
    }
    if( val >= 5 ) {
        return _( "Liked" );
    }

    return _( "Neutral" );
}

// Used in game.cpp
std::string fac_respect_text( int val )
{
    // Respected, feared, etc.
    if( val >= 100 ) {
        return pgettext( "Faction respect", "Legendary" );
    }
    if( val >= 80 ) {
        return pgettext( "Faction respect", "Unchallenged" );
    }
    if( val >= 60 ) {
        return pgettext( "Faction respect", "Mighty" );
    }
    if( val >= 40 ) {
        return pgettext( "Faction respect", "Famous" );
    }
    if( val >= 20 ) {
        return pgettext( "Faction respect", "Well-Known" );
    }
    if( val >= 10 ) {
        return pgettext( "Faction respect", "Spoken Of" );
    }

    // Disrespected, laughed at, etc.
    if( val <= -100 ) {
        return pgettext( "Faction respect", "Worthless Scum" );
    }
    if( val <= -80 ) {
        return pgettext( "Faction respect", "Vermin" );
    }
    if( val <= -60 ) {
        return pgettext( "Faction respect", "Despicable" );
    }
    if( val <= -40 ) {
        return pgettext( "Faction respect", "Parasite" );
    }
    if( val <= -20 ) {
        return pgettext( "Faction respect", "Leech" );
    }
    if( val <= -10 ) {
        return pgettext( "Faction respect", "Laughingstock" );
    }

    return pgettext( "Faction respect", "Neutral" );
}

std::string fac_wealth_text( int val, int size )
{
    //Wealth per person
    val = val / size;
    if( val >= 1000000 ) {
        return pgettext( "Faction wealth", "Filthy rich" );
    }
    if( val >= 750000 ) {
        return pgettext( "Faction wealth", "Affluent" );
    }
    if( val >= 500000 ) {
        return pgettext( "Faction wealth", "Prosperous" );
    }
    if( val >= 250000 ) {
        return pgettext( "Faction wealth", "Well-Off" );
    }
    if( val >= 100000 ) {
        return pgettext( "Faction wealth", "Comfortable" );
    }
    if( val >= 85000 ) {
        return pgettext( "Faction wealth", "Wanting" );
    }
    if( val >= 70000 ) {
        return pgettext( "Faction wealth", "Failing" );
    }
    if( val >= 50000 ) {
        return pgettext( "Faction wealth", "Impoverished" );
    }
    return pgettext( "Faction wealth", "Destitute" );
}

std::string faction::food_supply_text()
{
    //Convert to how many days you can support the population
    int val = food_supply_ / ( size_ * 288 );
    if( val >= 30 ) {
        return pgettext( "Faction food", "Overflowing" );
    }
    if( val >= 14 ) {
        return pgettext( "Faction food", "Well-Stocked" );
    }
    if( val >= 6 ) {
        return pgettext( "Faction food", "Scrapping By" );
    }
    if( val >= 3 ) {
        return pgettext( "Faction food", "Malnourished" );
    }
    return pgettext( "Faction food", "Starving" );
}

nc_color faction::food_supply_color()
{
    int val = food_supply_ / ( size_ * 288 );
    if( val >= 30 ) {
        return c_green;
    } else if( val >= 14 ) {
        return c_light_green;
    } else if( val >= 6 ) {
        return c_yellow;
    } else if( val >= 3 ) {
        return c_light_red;
    } else {
        return c_red;
    }
}

auto faction::relationship_flags_with( const faction_id &guy_id ) const ->
const std::bitset<npc_factions::rel_types> *
{
    const auto rel_data = relations_.find( guy_id.c_str() );
    return rel_data != relations_.end() ? &rel_data->second : nullptr;
}

bool faction::has_relationship( const faction_id &guy_id, npc_factions::relationship flag ) const
{
    const auto rel_data = relationship_flags_with( guy_id );
    return rel_data != nullptr && rel_data->test( flag );
}

std::string fac_combat_ability_text( int val )
{
    if( val >= 150 ) {
        return pgettext( "Faction combat lvl", "Legendary" );
    }
    if( val >= 130 ) {
        return pgettext( "Faction combat lvl", "Expert" );
    }
    if( val >= 115 ) {
        return pgettext( "Faction combat lvl", "Veteran" );
    }
    if( val >= 105 ) {
        return pgettext( "Faction combat lvl", "Skilled" );
    }
    if( val >= 95 ) {
        return pgettext( "Faction combat lvl", "Competent" );
    }
    if( val >= 85 ) {
        return pgettext( "Faction combat lvl", "Untrained" );
    }
    if( val >= 75 ) {
        return pgettext( "Faction combat lvl", "Crippled" );
    }
    if( val >= 50 ) {
        return pgettext( "Faction combat lvl", "Feeble" );
    }
    return pgettext( "Faction combat lvl", "Worthless" );
}

void npc_factions::finalize()
{
    g->faction_manager_ptr->create_if_needed();
}

void faction_manager::clear()
{
    factions.clear();
}

void faction_manager::remove_faction( const faction_id &id )
{
    if( id.str().empty() || id == faction_id( "no_faction" ) ) {
        return;
    }
    for( auto it = factions.cbegin(), next_it = it; it != factions.cend(); it = next_it ) {
        ++next_it;
        if( id == it->first ) {
            factions.erase( it );
            break;
        }
    }
}

void faction_manager::create_if_needed()
{
    if( !factions.empty() ) {
        return;
    }
    for( const auto &fac_temp : npc_factions::all_templates ) {
        factions[fac_temp.id_] = fac_temp;
    }
}

faction *faction_manager::add_new_faction( const std::string &name_new, const faction_id &id_new,
        const faction_id &template_id )
{
    for( const faction_template &fac_temp : npc_factions::all_templates ) {
        if( template_id == fac_temp.id_ ) {
            faction fac( fac_temp );
            fac.name_ = name_new;
            fac.id_ = id_new;
            factions[fac.id_] = fac;
            return &factions[fac.id_];
        }
    }
    return nullptr;
}

faction *faction_manager::get( const faction_id &id, const bool complain )
{
    if( id.is_null() ) {
        return get( faction_id( "no_faction" ) );
    }
    for( auto &elem : factions ) {
        if( elem.first == id ) {
            if( !elem.second.validated ) {
                for( const faction_template &fac_temp : npc_factions::all_templates ) {
                    if( fac_temp.id_ == id ) {
                        elem.second.currency_ = fac_temp.currency_;
                        elem.second.lone_wolf_faction_ = fac_temp.lone_wolf_faction_;
                        elem.second.name_ = fac_temp.name_;
                        elem.second.desc_ = fac_temp.desc_;
                        elem.second.mon_faction_ = fac_temp.mon_faction_;
                        elem.second.epilogue_data_ = fac_temp.epilogue_data_;
                        for( const auto &rel_data : fac_temp.relations_ ) {
                            if( !elem.second.relations_.contains( rel_data.first ) ) {
                                elem.second.relations_[rel_data.first] = rel_data.second;
                            }
                        }
                        break;
                    }
                }
                elem.second.validated = true;
            }
            return &elem.second;
        }
    }
    for( const faction_template &elem : npc_factions::all_templates ) {
        // id isn't already in factions map, so load in the template.
        if( elem.id_ == id ) {
            factions[elem.id_] = elem;
            if( !factions.empty() ) {
                factions[elem.id_].validated = true;
            }
            return &factions[elem.id_];
        }
    }
    // Sometimes we add new IDs to the map, sometimes we want to check if its already there.
    if( complain ) {
        debugmsg( "Requested non-existing faction '%s'", id.str() );
    }
    return nullptr;
}

template<>
const faction &string_id<faction>::obj() const
{
    const faction *ptr = g->faction_manager_ptr->get( *this, true );
    if( ptr ) {
        return *ptr;
    } else {
        static faction null_fac;
        return null_fac;
    }
}

template<>
bool string_id<faction>::is_valid() const
{
    return g->faction_manager_ptr->get( *this, false ) != nullptr;
}

int npc::follower_interaction_flag() const
{
    static const flag_id json_flag_TWO_WAY_RADIO( "TWO_WAY_RADIO" );
    const bool u_has_radio = g->u.has_item_with_flag( json_flag_TWO_WAY_RADIO, true ) ||
                             g->u.has_bionic( bio_infolink );
    const bool guy_has_radio = has_item_with_flag( json_flag_TWO_WAY_RADIO, true ) ||
                               has_bionic( bio_infolink );
    const tripoint_abs_omt player_abspos = get_player_character().abs_omt_pos();
    if( rl_dist( player_abspos, abs_omt_pos() ) > 3 ||
        ( rl_dist( g->u.bub_pos(), bub_pos() ) > SEEX * 2 || !g->u.sees( bub_pos() ) ) ) {
        if( u_has_radio && guy_has_radio ) {
            // TODO: better range calculation than just elevation.
            int max_range = 200;
            max_range *= ( 1 + ( g->u.bub_pos().z() * 0.1 ) );
            max_range *= ( 1 + ( bub_pos().z() * 0.1 ) );
            if( ( ( g->u.bub_pos().z() >= 0 && bub_pos().z() >= 0 ) ||
                  ( g->u.bub_pos().z() == bub_pos().z() ) ) &&
                square_dist( g->u.abs_sm_pos(), abs_sm_pos() ) <= max_range ) {
                return 2;
            }
        }
        return 0;
    }
    return 1;
}

std::string npc::faction_info_text() const
{
    // Parallel to npc::faction_display, producing the same lines as one
    // colour-tagged string for the RmlUi detail pane (curses path untouched).
    std::vector<std::string> lines;
    lines.emplace_back( _( "Press enter to talk to this follower" ) );
    lines.emplace_back( _( "Press s to swap to this follower" ) );

    const int flag = follower_interaction_flag();
    std::string can_see;
    nc_color see_color = c_light_red;
    if( flag == 1 ) {
        can_see = _( "Within interaction range" );
        see_color = c_light_green;
    } else if( flag == 2 ) {
        can_see = _( "Within radio range" );
        see_color = c_light_green;
    } else {
        static const flag_id json_flag_TWO_WAY_RADIO( "TWO_WAY_RADIO" );
        const bool u_has_radio = g->u.has_item_with_flag( json_flag_TWO_WAY_RADIO, true ) ||
                                 g->u.has_bionic( bio_infolink );
        const bool guy_has_radio = has_item_with_flag( json_flag_TWO_WAY_RADIO, true ) ||
                                   has_bionic( bio_infolink );
        if( u_has_radio && guy_has_radio ) {
            can_see = _( "Not within radio range" );
        } else if( guy_has_radio && !u_has_radio ) {
            can_see = _( "You do not have a radio" );
        } else if( !guy_has_radio && u_has_radio ) {
            can_see = _( "Follower does not have a radio" );
        } else {
            can_see = _( "Both you and follower need a radio" );
        }
    }
    lines.emplace_back( colorize( can_see, see_color ) );

    std::string current_status = _( "Status: " );
    nc_color status_col = c_white;
    if( current_target() != nullptr ) {
        current_status += _( "In Combat!" );
        status_col = c_light_red;
    } else if( in_sleep_state() ) {
        current_status += _( "Sleeping" );
    } else if( is_following() ) {
        current_status += _( "Following" );
    } else if( is_leader() ) {
        current_status += _( "Leading" );
    } else if( is_patrolling() ) {
        current_status += _( "Patrolling" );
    } else if( is_guarding() ) {
        current_status += _( "Guarding" );
    }
    lines.emplace_back( colorize( current_status, status_col ) );

    const std::pair <std::string, nc_color> condition = hp_description();
    lines.emplace_back( colorize( _( "Condition: " ) + condition.first, condition.second ) );
    const std::pair <std::string, nc_color> hunger_pair = get_hunger_description();
    const std::pair <std::string, nc_color> thirst_pair = get_thirst_description();
    const std::pair <std::string, nc_color> fatigue_pair = get_fatigue_description();
    const std::string nominal = pgettext( "needs", "Nominal" );
    lines.emplace_back( colorize( _( "Hunger: " ) +
                                  ( hunger_pair.first.empty() ? nominal : hunger_pair.first ), hunger_pair.second ) );
    lines.emplace_back( colorize( _( "Thirst: " ) +
                                  ( thirst_pair.first.empty() ? nominal : thirst_pair.first ), thirst_pair.second ) );
    lines.emplace_back( colorize( _( "Fatigue: " ) +
                                  ( fatigue_pair.first.empty() ? nominal : fatigue_pair.first ), fatigue_pair.second ) );
    lines.emplace_back( _( "Wielding: " ) + primary_weapon().tname() );

    const auto skillslist = Skill::get_skills_sorted_by( [&]( const Skill & a, const Skill & b ) {
        const int level_a = get_skill_level( a.ident() );
        const int level_b = get_skill_level( b.ident() );
        return localized_compare( std::make_pair( -level_a, a.name() ),
                                  std::make_pair( -level_b, b.name() ) );
    } );
    std::vector<std::string> skill_strs;
    for( size_t i = 0; i < skillslist.size() && skill_strs.size() < 3; i++ ) {
        if( !skillslist[ i ]->is_combat_skill() ) {
            skill_strs.push_back( string_format( "%s: %d", skillslist[i]->name(),
                                                 get_skill_level( skillslist[i]->ident() ) ) );
        }
    }
    lines.emplace_back( string_format( _( "Best combat skill: %s: %d" ),
                                       best_skill().obj().name(), best_skill_level() ) );
    // Guard the skill list (curses faction_display indexes [0..2] unconditionally;
    // here we only emit what exists).
    std::string other = _( "Best other skills: " );
    if( !skill_strs.empty() ) {
        other += skill_strs[0];
    }
    lines.emplace_back( other );
    for( size_t i = 1; i < skill_strs.size(); i++ ) {
        lines.emplace_back( skill_strs[i] );
    }

    std::string out;
    for( size_t i = 0; i < lines.size(); i++ ) {
        if( i > 0 ) {
            out += '\n';
        }
        out += lines[i];
    }
    return out;
}

std::string faction::faction_info_text() const
{
    // Parallel to faction::faction_display (curses path untouched).
    std::string out = string_format( _( "Attitude to you:           %s" ),
                                     fac_ranking_text( likes_u_ ) );
    out += '\n';
    out += string_format( _( "Faction strength:       %s" ), power_ );
    out += '\n';
    out += _( desc_ );
    return out;
}

// ── RmlUi render path (full UI→RmlUi migration) ──────────────────────────────
// The faction manager is a 4-tab list+detail screen (Followers / Other factions /
// Lore / Creatures). The RmlUi doc renders the tab bar, the left list, and the
// right detail pane (each tab's *_faction_info_text() / lore snippet run through
// cata_text_to_rml). Render-only: the keyboard loop owns navigation + CONFIRM /
// SWAPTONPC, and the follower interaction flag is now computed in the loop
// (npc::follower_interaction_flag) so it no longer depends on the curses draw.
namespace
{
struct faction_rml_tab {
    std::string label_rml;
    bool selected = false;
};
struct faction_rml_row {
    std::string text_rml;
    bool selected = false;
};
struct faction_rml_session {
    Rml::Vector<faction_rml_tab> tabs;
    Rml::Vector<faction_rml_row> rows;
    std::string detail_rml;
    Rml::DataModelHandle handle;
};
bool g_faction_types_registered = false;
void register_faction_rml_types( Rml::DataModelConstructor &c )
{
    if( g_faction_types_registered ) {
        return;
    }
    g_faction_types_registered = true;
    Rml::StructHandle<faction_rml_tab> th = c.RegisterStruct<faction_rml_tab>();
    th.RegisterMember( "label_rml", &faction_rml_tab::label_rml );
    th.RegisterMember( "selected", &faction_rml_tab::selected );
    c.RegisterArray<Rml::Vector<faction_rml_tab>>();
    Rml::StructHandle<faction_rml_row> rh = c.RegisterStruct<faction_rml_row>();
    rh.RegisterMember( "text_rml", &faction_rml_row::text_rml );
    rh.RegisterMember( "selected", &faction_rml_row::selected );
    c.RegisterArray<Rml::Vector<faction_rml_row>>();
}
} // namespace

bool &faction_rmlui_enabled()
{
    static bool enabled = true;
    return enabled;
}

void faction_manager::display() const
{
    catacurses::window w_missions;
    int entries_per_page = 0;

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        const point term( TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0,
                          TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0 );

        w_missions = catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH,
                                         point( term.y, term.x ) );

        entries_per_page = FULL_SCREEN_HEIGHT - 4;

        ui.position_from_window( w_missions );
    } );
    ui.mark_resize();

    enum class tab_mode : int {
        TAB_FOLLOWERS = 0,
        TAB_OTHERFACTIONS,
        TAB_LORE,
        TAB_CREATURES,
        NUM_TABS,
        FIRST_TAB = 0,
        LAST_TAB = NUM_TABS - 1
    };
    g->validate_npc_followers();
    tab_mode tab = tab_mode::FIRST_TAB;
    size_t selection = 0;
    input_context ctxt( "FACTION MANAGER" );
    ctxt.register_cardinal();
    ctxt.register_updown();
    ctxt.register_action( "ANY_INPUT" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "SWAPTONPC" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    std::vector<npc *> followers;
    std::vector<const faction *> valfac; // Factions that we know of.
    npc *guy = nullptr;
    const faction *cur_fac = nullptr;
    bool interactable = false;
    bool radio_interactable = false;
    size_t active_vec_size = 0;

    std::vector<std::pair<snippet_id, std::string>> lore; // Lore we have seen
    std::pair<snippet_id, std::string> *snippet = nullptr;

    std::vector<mtype_id> creatures; // Creatures we've recorded
    mtype_id cur_creature = mtype_id::NULL_ID();

    // ---- RmlUi render path (F.3 rml_doc harness) ----------------------------
    // Render-only: tab bar + left list + right detail pane. The keyboard loop
    // below owns navigation and CONFIRM/SWAPTONPC; the follower interaction flag
    // is set in that loop (not here), so this doc can be skipped without breaking
    // the actions. `rml_data` is declared before `rml` so the doc tears down while
    // the bound buffers are alive.
    faction_rml_session rml_data;
    rml_doc rml;
    const auto sync_rml = [&]() {
        if( !rml ) {
            return;
        }
        const std::array<std::string, 4> tab_labels = {
            _( "YOUR FOLLOWERS" ), _( "OTHER FACTIONS" ), _( "LORE" ), _( "CREATURES" )
        };
        rml_data.tabs.clear();
        for( int i = 0; i < 4; i++ ) {
            faction_rml_tab t;
            t.label_rml = cata_text_to_rml( tab_labels[i] );
            t.selected = ( static_cast<int>( tab ) == i );
            rml_data.tabs.push_back( t );
        }

        rml_data.rows.clear();
        rml_data.detail_rml.clear();
        const auto add_row = [&]( const std::string & text, size_t i ) {
            faction_rml_row r;
            r.text_rml = cata_text_to_rml( text );
            r.selected = ( selection == i );
            rml_data.rows.push_back( r );
        };
        switch( tab ) {
            case tab_mode::TAB_FOLLOWERS:
                for( size_t i = 0; i < followers.size(); i++ ) {
                    add_row( followers[i]->disp_name(), i );
                }
                rml_data.detail_rml = guy
                                      ? cata_text_to_rml( guy->faction_info_text() )
                                      : cata_text_to_rml( colorize( _( "You have no followers" ), c_light_red ) );
                break;
            case tab_mode::TAB_OTHERFACTIONS:
                for( size_t i = 0; i < valfac.size(); i++ ) {
                    add_row( _( valfac[i]->name() ), i );
                }
                rml_data.detail_rml = cur_fac
                                      ? cata_text_to_rml( cur_fac->faction_info_text() )
                                      : cata_text_to_rml( colorize( _( "You don't know of any factions." ), c_light_red ) );
                break;
            case tab_mode::TAB_LORE:
                for( size_t i = 0; i < lore.size(); i++ ) {
                    add_row( _( lore[i].second ), i );
                }
                rml_data.detail_rml = snippet != nullptr
                                      ? cata_text_to_rml( SNIPPET.get_snippet_by_id( snippet->first ).value().translated() )
                                      : cata_text_to_rml( colorize( _( "You haven't learned anything about the world." ),
                                              c_light_red ) );
                break;
            case tab_mode::TAB_CREATURES:
                for( size_t i = 0; i < creatures.size(); i++ ) {
                    add_row( string_format( "%s  %s", colorize( creatures[i]->sym, creatures[i]->color ),
                                            creatures[i]->nname() ), i );
                }
                rml_data.detail_rml = !cur_creature.is_null()
                                      ? cata_text_to_rml( cur_creature->faction_info_text() )
                                      : cata_text_to_rml( colorize(
                                              _( "You haven't recorded sightings of any creatures.  Taking photos can be a good way to keep track of them." ),
                                              c_light_red ) );
                break;
            case tab_mode::NUM_TABS:
                break;
        }
        rml_data.handle.DirtyVariable( "tabs" );
        rml_data.handle.DirtyVariable( "rows" );
        rml_data.handle.DirtyVariable( "detail_rml" );
    };
    rml.open( faction_rmlui_enabled(), "faction", ctxt,
    [&]( Rml::DataModelConstructor & c ) {
        register_faction_rml_types( c );
        c.Bind( "tabs", &rml_data.tabs );
        c.Bind( "rows", &rml_data.rows );
        c.Bind( "detail_rml", &rml_data.detail_rml );
        rml_data.handle = c.GetModelHandle();
    } );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        // RmlUi path owns the screen — sync the model and skip the curses draw.
        if( rml ) {
            sync_rml();
            return;
        }
    } );

    for( const auto &elem : get_avatar().get_snippets() ) {
        std::optional<translation> name = SNIPPET.get_name_by_id( elem );
        if( name && !name->empty() ) {
            lore.push_back( std::pair<snippet_id, std::string>( elem, name->translated() ) );
        } else {
            lore.push_back( std::pair<snippet_id, std::string>( elem, elem.str() ) );
        }
    }

    auto compare_second =
        []( const std::pair<snippet_id, std::string> &a,
    const std::pair<snippet_id, std::string> &b ) {
        return localized_compare( a.second, b.second );
    };
    std::sort( lore.begin(), lore.end(), compare_second );

    creatures.assign( get_avatar().get_known_monsters().begin(),
                      get_avatar().get_known_monsters().end() );

    std::sort( creatures.begin(), creatures.end(), []( const mtype_id & a, const mtype_id & b ) {
        return localized_compare( a->nname(), b->nname() );
    } );


    while( true ) {
        // create a list of NPCs, visible and the ones on overmapbuffer
        followers.clear();
        for( auto &elem : g->get_follower_list() ) {
            shared_ptr_fast<npc> npc_to_get = nullptr;
            for_each_overmapbuffer( [&]( const std::string &, overmapbuffer & omb ) {
                if( !npc_to_get ) {
                    npc_to_get = omb.find_npc( elem );
                }
            } );
            if( !npc_to_get ) {
                continue;
            }
            npc *npc_to_add = npc_to_get.get();
            followers.push_back( npc_to_add );
        }
        valfac.clear();
        for( const auto &elem : g->faction_manager_ptr->all() ) {
            if( elem.second.known_by_u() && elem.second.id() != faction_id( "your_followers" ) ) {
                valfac.push_back( &elem.second );
            }
        }
        guy = nullptr;
        cur_fac = nullptr;
        interactable = false;
        snippet = nullptr;
        radio_interactable = false;

        if( tab < tab_mode::FIRST_TAB || tab >= tab_mode::NUM_TABS ) {
            debugmsg( "The sanity check failed because tab=%d", static_cast<int>( tab ) );
            tab = tab_mode::FIRST_TAB;
        }
        active_vec_size = 0;
        if( tab == tab_mode::TAB_FOLLOWERS ) {
            if( selection < followers.size() ) {
                guy = followers[selection];
                // Compute the interaction flag here (not in the draw) so both the
                // curses and RmlUi paths get it.
                const int flag = guy->follower_interaction_flag();
                radio_interactable = ( flag == 2 );
                interactable = ( flag == 1 );
            }
            active_vec_size = followers.size();
        } else if( tab == tab_mode::TAB_OTHERFACTIONS ) {
            if( selection < valfac.size() ) {
                cur_fac = valfac[selection];
            }
            active_vec_size = valfac.size();

        } else if( tab == tab_mode::TAB_LORE ) {
            if( selection < lore.size() ) {
                snippet = &lore[selection];
            }
            active_vec_size = lore.size();
        } else if( tab == tab_mode::TAB_CREATURES ) {
            if( selection < creatures.size() ) {
                cur_creature = creatures[selection];
            }
            active_vec_size = creatures.size();
        }

        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "NEXT_TAB" || action == "RIGHT" ) {
            tab = static_cast<tab_mode>( static_cast<int>( tab ) + 1 );
            if( tab >= tab_mode::NUM_TABS ) {
                tab = tab_mode::FIRST_TAB;
            }
            selection = 0;
        } else if( action == "PREV_TAB" || action == "LEFT" ) {
            tab = static_cast<tab_mode>( static_cast<int>( tab ) - 1 );
            if( tab < tab_mode::FIRST_TAB ) {
                tab = tab_mode::LAST_TAB;
            }
            selection = 0;
        } else if( action == "DOWN" ) {
            selection++;
            if( selection >= active_vec_size ) {
                selection = 0;
            }
        } else if( action == "UP" ) {
            if( selection == 0 ) {
                selection = active_vec_size == 0 ? 0 : active_vec_size - 1;
            } else {
                selection--;
            }
        } else if( action == "CONFIRM" && guy ) {
            if( guy->has_companion_mission() ) {
                guy->reset_companion_mission();
                popup( _( "%s returns from their mission" ), guy->disp_name() );
            } else {
                if( tab == tab_mode::TAB_FOLLOWERS && guy && ( interactable || radio_interactable ) ) {
                    guy->talk_to_u( radio_interactable );
                }
            }
        } else if( action == "QUIT" ) {
            break;
        } else if( action == "SWAPTONPC" && guy && interactable ) {
            get_avatar().control_npc( *guy );
        }
    }
}
