#include "iuse_actor.h"

#include "action.h"
#include "active_tile_data_def.h"
#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "addiction.h"
#include "ammo.h"
#include "animation.h"
#include "assign.h"
#include "avatar.h"
#include "avatar_functions.h"
#include "bionics.h"
#include "bodypart.h"
#include "cached_options.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catalua_hooks.h"
#include "catalua_icallback_actor.h"
#include "catalua_sol.h"
#include "character.h"
#include "character_functions.h"
#include "character_id.h"
#include "cloning_utils.h"
#include "clothing_mod.h"
#include "crafting.h"
#include "creature.h"
#include "debug.h"
#include "dimension_info.h"
#include "effect.h"
#include "enum_conversions.h"
#include "enums.h"
#include "explosion.h"
#include "field_type.h"
#include "flag.h"
#include "flat_set.h"
#include "game.h"
#include "game_inventory.h"
#include "handle_liquid.h"
#include "hsv_color.h"
#include "iexamine.h"
#include "int_id.h"
#include "inventory.h"
#include "item.h"
#include "item_contents.h"
#include "item_factory.h"
#include "item_group.h"
#include "item_reload_option.h"
#include "itype.h"
#include "json.h"
#include "line.h"
#include "locations.h"
#include "magic.h"
#include "map.h"
#include "map_iterator.h"
#include "map_selector.h"
#include "map_utils.h"
#include "mapdata.h"
#include "material.h"
#include "memory_fast.h"
#include "messages.h"
#include "monster.h"
#include "morale_types.h"
#include "mtype.h"
#include "mutation.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmap_special.h"
#include "overmap_ui.h"
#include "overmapbuffer.h"
#include "player.h"
#include "player_activity.h"
#include "pldata.h"
#include "popup.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "rng.h"
#include "skill.h"
#include "sounds.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "submap_load_manager.h"
#include "text_snippets.h"
#include "translations.h"
#include "trap.h"
#include "type_id.h"
#include "ui.h"
#include "uistate.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vehicle_selector.h"
#include "visitable.h"
#include "vitamin.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "weather.h"
#include "world_type.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <ranges>
#include <ret_val.h>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static const activity_id ACT_FIRSTAID( "ACT_FIRSTAID" );
static const activity_id ACT_MAKE_ZLAVE( "ACT_MAKE_ZLAVE" );
static const activity_id ACT_RELOAD( "ACT_RELOAD" );
static const activity_id ACT_REPAIR_ITEM( "ACT_REPAIR_ITEM" );
static const activity_id ACT_SPELLCASTING( "ACT_SPELLCASTING" );
static const activity_id ACT_STUDY_SPELL( "ACT_STUDY_SPELL" );
static const activity_id ACT_START_FIRE( "ACT_START_FIRE" );
static const activity_id ACT_VIBE( "ACT_VIBE" );

static const efftype_id effect_accumulated_mutagen( "accumulated_mutagen" );
static const efftype_id effect_asthma( "asthma" );
static const efftype_id effect_bandaged( "bandaged" );
static const efftype_id effect_bite( "bite" );
static const efftype_id effect_cig( "cig" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_pet( "pet" );
static const efftype_id effect_disinfected( "disinfected" );
static const efftype_id effect_downed( "downed" );
static const efftype_id effect_infected( "infected" );
static const efftype_id effect_hallu( "hallu" );
static const efftype_id effect_music( "music" );
static const efftype_id effect_playing_instrument( "playing_instrument" );
static const efftype_id effect_recover( "recover" );
static const efftype_id effect_run( "run" );
static const efftype_id effect_sleep( "sleep" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_visuals( "visuals" );

static const fault_id fault_bionic_nonsterile( "fault_bionic_nonsterile" );

static const bionic_id bio_syringe( "bio_syringe" );

static const itype_id itype_barrel_small( "barrel_small" );
static const itype_id itype_brazier( "brazier" );
static const itype_id itype_char_smoker( "char_smoker" );
static const itype_id itype_fire( "fire" );
static const itype_id itype_stock_small( "stock_small" );
static const itype_id itype_syringe( "syringe" );
static const itype_id itype_fertilizer( "fertilizer" );
static const itype_id itype_genome_drive( "genome_drive" );
static const itype_id itype_usb_drive( "usb_drive" );
static const flag_id flag_genome_drive( "GENOME_DRIVE" );
static const itype_id itype_mutagen( "mutagen" );
static const itype_id itype_biomaterial( "biomaterial" );

static const skill_id skill_fabrication( "fabrication" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_survival( "survival" );

static const species_id HUMAN( "HUMAN" );
static const species_id ZOMBIE( "ZOMBIE" );

static const trait_id trait_CENOBITE( "CENOBITE" );
static const trait_id trait_DEBUG_BIONICS( "DEBUG_BIONICS" );
static const trait_id trait_TOLERANCE( "TOLERANCE" );
static const trait_id trait_INFRESIST( "INFRESIST" );
static const trait_id trait_LIGHTWEIGHT( "LIGHTWEIGHT" );
static const trait_id trait_PACIFIST( "PACIFIST" );
static const trait_id trait_PSYCHOPATH( "PSYCHOPATH" );
static const trait_id trait_PYROMANIA( "PYROMANIA" );
static const trait_id trait_NOPAIN( "NOPAIN" );
static const trait_id trait_MASOCHIST( "MASOCHIST" );
static const trait_id trait_MASOCHIST_MED( "MASOCHIST_MED" );
static const trait_id trait_MUT_JUNKIE( "MUT_JUNKIE" );
static const trait_id trait_SAPIOVORE( "SAPIOVORE" );

static const trait_flag_str_id trait_flag_PRED1( "PRED1" );
static const trait_flag_str_id trait_flag_PRED2( "PRED2" );
static const trait_flag_str_id trait_flag_PRED3( "PRED3" );
static const trait_flag_str_id trait_flag_PRED4( "PRED4" );

static const itype_id itype_UPS( "UPS" );

static const mtype_id mon_hallu_multicooker( "mon_hallu_multicooker" );


static const species_id species_HALLUCINATION( "HALLUCINATION" );
static const species_id species_ROBOT( "ROBOT" );
static const species_id species_ZOMBIE( "ZOMBIE" );
static const species_id species_NETHER( "NETHER" );
static const species_id species_SKELETON( "SKELETON" );

static const flag_id flag_NO_PAINT( "NO_PAINT" );

class npc;


std::unique_ptr<iuse_actor> deploy_furn_actor::clone() const
{
    return std::make_unique<deploy_furn_actor>( *this );
}

void deploy_furn_actor::info( const item &, std::vector<iteminfo> &dump ) const
{
    std::vector<std::string> can_function_as;
    const furn_t &the_furn = furn_type.obj();
    const std::string furn_name = the_furn.name();
    const std::set<itype_id> &pseudo_list = the_furn.crafting_pseudo_items;

    if( the_furn.workbench ) { can_function_as.emplace_back( _( "a <info>crafting station</info>" ) ); }
    if( the_furn.has_flag( "BUTCHER_EQ" ) ) {
        can_function_as.emplace_back( _( "a place to hang <info>corpses for butchering</info>" ) );
    }
    if( the_furn.has_flag( "FLAT_SURF" ) ) {
        can_function_as.emplace_back( _( "a flat surface to <info>butcher</info> onto or <info>eat "
                                         "meals</info> from" ) );
    }
    if( the_furn.has_flag( "CAN_SIT" ) ) {
        can_function_as.emplace_back( _( "a place to <info>sit</info>" ) );
    }
    if( the_furn.has_flag( "HIDE_PLACE" ) ) {
        can_function_as.emplace_back( _( "a place to <info>hide</info>" ) );
    }
    if( the_furn.has_flag( "FIRE_CONTAINER" ) ) {
        can_function_as.emplace_back( _( "a safe place to <info>contain a fire</info>" ) );
    }
    if( pseudo_list.contains( itype_char_smoker ) ) {
        can_function_as.emplace_back( _( "a place to <info>smoke or dry food</info> for "
                                         "preservation" ) );
    }

    if( can_function_as.empty() ) {
        dump.emplace_back(
            "DESCRIPTION",
            string_format( _( "Can be <info>activated</info> to deploy as furniture "
                              "(<stat>%s</stat>)." ),
                           furn_name ) );
    } else {
        std::string furn_usages =
            enumerate_as_string( can_function_as, enumeration_conjunction::or_ );
        dump.emplace_back(
            "DESCRIPTION",
            string_format( _( "Can be <info>activated</info> to deploy as furniture "
                              "(<stat>%s</stat>), which can then be used as %s." ),
                           furn_name, furn_usages ) );
    }
}

void deploy_furn_actor::load( const JsonObject& obj )
{
    furn_type = furn_str_id( obj.get_string( "furn_type" ) );
}

int deploy_furn_actor::use( player& p, item& it, bool t, const tripoint_bub_ms& pos ) const
{
    if( t ) { return 0; }

if( p.is_mounted() ) {
        p.add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    tripoint_bub_ms pnt = pos;
    if( const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent( _( "Deploy where?" ) ) ) {
        pnt = *pnt_;
    } else {
        return 0;
    }

    if( pnt == p.bub_pos() ) {
        p.add_msg_if_player( m_info, _( "You attempt to become one with the furniture.  It doesn't "
                                        "work." ) );
        return 0;
    }

    map& here = get_map();
    optional_vpart_position veh_there = here.veh_at( pnt );
    if( veh_there.has_value() ) {
        // TODO: check for protrusion+short furniture, wheels+tiny furniture, NOCOLLIDE flag, etc.
        // and/or integrate furniture deployment with construction (which already seems to perform
        // these checks sometimes?)
        p.add_msg_if_player(
            m_info, _( "The space under %s is too cramped to deploy a %s in." ),
            veh_there.value().vehicle().disp_name(), it.tname() );
        return 0;
    }

    // For example: dirt = 2, long grass = 3
    if( here.move_cost( pnt ) != 2 && here.move_cost( pnt ) != 3 ) {
        p.add_msg_if_player( m_info, _( "You can't deploy a %s there." ), it.tname() );
        return 0;
    }

    if( here.has_furn( pnt ) ) {
        p.add_msg_if_player( m_info, _( "There is already furniture at that location." ) );
        return 0;
    }

    // It shouldn't be possible to deploy a NOITEM furniture on top of items.
    const furn_t &furn_obj = furn_type.obj();
    if( ( furn_obj.has_flag( TFLAG_SEALED ) || furn_obj.has_flag( TFLAG_NOITEM ) )
            && !here.i_at( pnt ).empty() ) {
        p.add_msg_if_player( m_info, _( "Can't put that here - items in the way." ) );
        return 0;
    }

    here.furn_set( pnt, furn_type );
    here.furn_vars( pnt )->merge( it.item_vars() );
    p.mod_moves( to_turns<int>( 2_seconds ) );
    return 1;
}

std::unique_ptr<iuse_actor> reveal_map_actor::clone() const
{
    return std::make_unique<reveal_map_actor>( *this );
}

void reveal_map_actor::load( const JsonObject& obj )
{
    radius = obj.get_int( "radius" );
    message = obj.get_string( "message" );
    std::string ter;
    ot_match_type ter_match_type;
    for( const JsonValue entry : obj.get_array( "terrain" ) ) {
        if( entry.test_string() ) {
            ter = entry.get_string();
            ter_match_type = ot_match_type::contains;
        } else {
            JsonObject jo = entry.get_object();
            ter = jo.get_string( "om_terrain" );
            ter_match_type =
                jo.get_enum_value<ot_match_type>( "om_terrain_match_type", ot_match_type::contains );
        }
        omt_types.emplace_back( ter, ter_match_type );
    }
    if( obj.has_array( "terrain_view" ) ) {
        for( const JsonValue entry : obj.get_array( "terrain_view" ) ) {
            if( entry.test_string() ) {
                ter = entry.get_string();
                ter_match_type = ot_match_type::contains;
            } else {
                JsonObject jo = entry.get_object();
                ter = jo.get_string( "om_terrain" );
                ter_match_type = jo.get_enum_value <
                                 ot_match_type > ( "om_terrain_match_type", ot_match_type::contains );
            }
            omt_types_view.emplace_back( ter, ter_match_type );
        }
    } else {
        omt_types_view = omt_types;
    }
    if( obj.has_array( "terrain_view_exclude" ) ) {
        for( const JsonValue entry : obj.get_array( "terrain_view_exclude" ) ) {
            if( entry.test_string() ) {
                ter = entry.get_string();
                ter_match_type = ot_match_type::contains;
            } else {
                JsonObject jo = entry.get_object();
                ter = jo.get_string( "om_terrain" );
                ter_match_type = jo.get_enum_value <
                                 ot_match_type > ( "om_terrain_match_type", ot_match_type::contains );
            }
            omt_types_view_exclude.emplace_back( ter, ter_match_type );
        }
    } else {
        omt_types_view_exclude.emplace_back( "subway", ot_match_type::contains );
        omt_types_view_exclude.emplace_back( "hiway", ot_match_type::contains );
        omt_types_view_exclude.emplace_back( "road", ot_match_type::contains );
        omt_types_view_exclude.emplace_back( "forest_trail", ot_match_type::contains );
        omt_types_view_exclude.emplace_back( "bridge", ot_match_type::contains );
        omt_types_view_exclude.emplace_back( "roof", ot_match_type::contains );
    };
}

void reveal_map_actor::reveal_targets( const tripoint_abs_omt& map ) const
{
    omt_find_params params{};
    params.search_range = {0, radius};
    params.search_layers = omt_find_all_layers;
    params.types = omt_types;
    params.existing_only = false;
    params.popup = make_shared_fast<throbber_popup>( _( "Please wait…" ) );

    /*
     * Stagger parallel map generation starting from center (0), outwards
     * so the generated maps have a neighbor to latch onto when generating roads/rivers
     * 5 4 3 2 3 4 5
     * 4 3 2 1 2 3 4
     * 3 2 1 0 1 2 3
     * 4 3 2 1 2 3 4
     * 5 4 3 2 3 4 5
     */

    const point_abs_om origin_om_pos = project_to<coords::om>( map.xy() );

    // Generate a Square fitting the requested map radius
    const point_abs_omt omt_bb_min = map.xy() - point_rel_omt{radius, radius};
    const point_abs_omt omt_bb_max = map.xy() + point_rel_omt{radius, radius};

    // OM Corners of bounding box
    const point_abs_om om_bb_min = project_to<coords::om>( omt_bb_min );
    const point_abs_om om_bb_max = project_to<coords::om>( omt_bb_max );

    // Iterate through range [om_bb_min, om_bb_max] to get the OM we want, then sort by manhattan
    // distance
    std::map<int, std::vector<point_abs_om>> om_to_generate;
    for( int x = om_bb_min.x(); x <= om_bb_max.x(); ++x ) {
        for( int y = om_bb_min.y(); y <= om_bb_max.y(); ++y ) {
            auto dist = manhattan_dist( origin_om_pos, {x, y} );
            auto& vec = om_to_generate[dist]; // if the vector for this distance doesn't exist it
            // will be created empty
            vec.emplace_back( x, y );
        }
    }

    // Drain any in-flight lazy-border generation workers before spawning our own
    // overmap futures.  Without this, a background worker calling
    // overmapbuffer::get() and our futures calling it concurrently can both
    // observe a partially-initialised overmap in the map.
    submap_loader.drain_lazy_loads();

    auto& omb = get_overmapbuffer( get_avatar().get_dimension() );
    for( const auto& [_, to_gen] : om_to_generate ) { omb.generate( to_gen ); }

    const auto places = omb.find_all( map, params );
    for( auto& place : places ) { omb.reveal( place, 0 ); }
}

void reveal_map_actor::show_revealed( player& p, item& item, const tripoint_abs_omt& center ) const
{
    uistate.overmap_highlighted_omts.clear();

    omt_find_params params{};
    params.search_range = {0, radius};
    params.types = omt_types_view;
    params.exclude_types = omt_types_view_exclude;
    params.existing_only = true;
    // TODO: Add support for variable reveal z-range to reveal_map iuse_action JSON
    params.search_layers = omt_find_all_layers;
    params.explored = false;
    params.popup = make_shared_fast<throbber_popup>( _( "Please wait…" ) );

    const auto places = get_overmapbuffer( p.get_dimension() ).find_all( center, params );

    // Delete popup after search is done, before showing uilist
    params.popup = nullptr;

    // Group tiles by name
    std::multimap<std::string, tripoint_abs_omt> mm;
    std::set<std::string> utypes;
for( auto& place : places ) {
    auto desc = get_overmapbuffer( p.get_dimension() ).ter( place ).id().obj().get_name();
        mm.insert( {desc, place} );
        utypes.insert( desc );
    }

    if( utypes.empty() ) {
        p.add_msg_if_player( _( "There isn't anything new on the %s." ), item.tname() );
        return;
    }

    // Show selector for each group
    std::vector<std::string> otypes( utypes.begin(), utypes.end() );
    uilist ui;
    for( uint64_t i = 0; i < otypes.size(); ++i ) {
        auto& desc = otypes[i];
        ui.addentry( i, true, MENU_AUTOASSIGN, string_format( "%s (%d)", desc, mm.count( desc ) ) );
    }
    ui.query();

    if( ui.ret < 0 ) { return; }

const tripoint_abs_omt plrPos = p.abs_omt_pos();
auto eqRange = mm.equal_range( otypes[ui.ret] );

    // TODO: Cluster tripoints to collapse direct neighbor tiles (helipads, etc)?

    const auto sz = std::distance( eqRange.first, eqRange.second );

    // Shouldn't ever be hit, since multimap shouldn't have an entry with no overmap tiles, but
    if( sz == 0 ) { return; }

std::transform(
    eqRange.first, eqRange.second,
                   std::inserter( uistate.overmap_highlighted_omts, uistate.overmap_highlighted_omts.end() ),
                       []( const auto & e ) -> tripoint_abs_omt { return e.second; } );

    // Only one overmap tile of type
    if( sz == 1 ) {
    ui::omap::choose_point( eqRange.first->second );
        return;
    }

    ui.reset();
    ui.addentry( 0, true, 'c', _( "Closest" ) );
    ui.addentry( 1, true, 'r', _( "Random" ) );
    ui.query();

    if( ui.ret < 0 ) { return; }

if( ui.ret == 1 ) {
    // Pick random
    auto it = eqRange.first;
    std::advance( it, rng( 0, sz - 1 ) );
        ui::omap::choose_point( it->second );
    } else {
        // Pick closest
        const auto pred_dist =
            [&]( const std::pair<std::string, tripoint_abs_omt> &a,
        const std::pair<std::string, tripoint_abs_omt> &b ) {
            auto da = trig_dist_squared( plrPos.raw(), a.second.raw() );
            auto db = trig_dist_squared( plrPos.raw(), b.second.raw() );
            return da < db;
        };
        const auto it = std::min_element( eqRange.first, eqRange.second, pred_dist );
        ui::omap::choose_point( it->second );
    }
}

int reveal_map_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    if( !it.already_used_by_player( p ) && g->get_levz() < 0 ) {
    p.add_msg_if_player( _( "You should read your %s when you get to the surface." ), it.tname() );
        return 0;
    } else if( !character_funcs::can_see_fine_details( p ) ) {
    p.add_msg_if_player( _( "It's too dark to read." ) );
        return 0;
    }

    const tripoint_abs_omt plrPos = p.abs_omt_pos();
    const auto mapPos = it.get_var( "reveal_map_center_omt", plrPos );

    if( it.already_used_by_player( p ) ) {
    show_revealed( p, it, mapPos );
        return 0;
    }

    reveal_targets( mapPos );
    if( !message.empty() ) { p.add_msg_if_player( m_good, "%s", _( message ) ); }
    it.mark_as_used_by_player( p );
    show_revealed( p, it, mapPos );
    return 0;
}

void firestarter_actor::load( const JsonObject& obj )
{
    moves_cost_fast = obj.get_int( "moves", moves_cost_fast );
    moves_cost_slow = obj.get_int( "moves_slow", moves_cost_fast * 10 );
    need_sunlight = obj.get_bool( "need_sunlight", false );
}

std::unique_ptr<iuse_actor> firestarter_actor::clone() const
{
    return std::make_unique<firestarter_actor>( *this );
}

bool firestarter_actor::prep_firestarter_use( const player& p, tripoint_bub_ms& pos )
{
    // checks for fuel are handled by use and the activity, not here
    if( pos == p.bub_pos() ) {
        if( const std::optional<tripoint_bub_ms> pnt_ = choose_adjacent( _( "Light where?" ) ) ) {
            pos = *pnt_;
        } else {
            return false;
        }
    }
    if( pos == p.bub_pos() ) {
        p.add_msg_if_player( m_info, _( "You would set yourself on fire." ) );
        p.add_msg_if_player( _( "But you're already smokin' hot." ) );
        return false;
    }
    map& here = get_map();
    if( here.get_field( pos, fd_fire ) ) {
        // check if there's already a fire
        p.add_msg_if_player( m_info, _( "There is already a fire." ) );
        return false;
    }
    // Check for a brazier.
    bool has_unactivated_brazier = false;
    for( const item * const& i : here.i_at( pos ) ) {
        if( i->typeId() == itype_brazier ) { has_unactivated_brazier = true; }
    }
    return !has_unactivated_brazier
           || query_yn( _( "There's a brazier there but you haven't set it up to contain the fire.  "
                    "Continue?" ) );
}

void firestarter_actor::resolve_firestarter_use( player& p, const tripoint_bub_ms& pos )
{
    if( get_map().add_field( pos, fd_fire, 1, 10_minutes ) ) {
        if( !p.has_trait( trait_PYROMANIA ) ) {
            p.add_msg_if_player( _( "You successfully light a fire." ) );
        } else {
            if( one_in( 4 ) ) {
                p.add_msg_if_player( m_mixed, _( "You light a fire, but it isn't enough.  You need "
                                                 "to light more." ) );
            } else {
                p.add_msg_if_player( m_good, _( "You happily light a fire." ) );
                p.add_morale( MORALE_PYROMANIA_STARTFIRE, 5, 10, 6_hours, 4_hours );
                p.rem_morale( MORALE_PYROMANIA_NOFIRE );
            }
        }
    }
}

ret_val<bool> firestarter_actor::can_use(
    const Character& p, const item& it, bool, const tripoint_bub_ms & ) const
{
    if( p.is_underwater() ) {
    return ret_val<bool>::make_failure( _( "You can't do that while underwater." ) );
    }

    if( !( it.has_flag( flag_USE_UPS ) && p.has_charges( itype_UPS, it.ammo_required() ) )
            && ( it.ammo_remaining() < it.ammo_required() ) ) {
        return ret_val<bool>::make_failure( _( "This tool doesn't have enough charges." ) );
    }

    if( need_sunlight && light_mod( p.bub_pos() ) <= 0.0f ) {
        return ret_val<bool>::make_failure( _( "You need direct sunlight to light a fire with "
                                               "this." ) );
    }

    return ret_val<bool>::make_success();
}

float firestarter_actor::light_mod( const tripoint_bub_ms& pos ) const
{
    if( !need_sunlight ) { return 1.0f; }

const float light_level = g->natural_light_level( pos.z() );
    if( get_weather().weather_id->sun_intensity >= sun_intensity_type::normal
            && light_level >= 60.0f && weather::is_sheltered( get_map(), pos ) ) {
        return std::pow( light_level / 80.0f, 8 );
    }

    return 0.0f;
}

int firestarter_actor::moves_cost_by_fuel( const tripoint_bub_ms& pos ) const
{
    map& here = get_map();
    if( here.flammable_items_at( pos, 100 ) ) { return moves_cost_fast; }

    if( here.flammable_items_at( pos, 10 ) ) { return ( moves_cost_slow + moves_cost_fast ) / 2; }

    return moves_cost_slow;
}

int firestarter_actor::use( player& p, item& it, bool t, const tripoint_bub_ms& spos ) const
{
    if( t ) { return 0; }

auto pos = spos;
float light = light_mod( p.bub_pos() );
    if( !prep_firestarter_use( p, pos ) ) { return 0; }

    double skill_level = p.get_skill_level( skill_survival );
    /** @EFFECT_SURVIVAL speeds up fire starting */
    float moves_modifier = std::pow( 0.8, std::min( 5.0, skill_level ) );
    const int moves_base = moves_cost_by_fuel( pos );
    const double moves_per_turn = to_moves<double>( 1_turns );
    const int min_moves =
        std::min<int>( moves_base, std::sqrt( 1 + moves_base / moves_per_turn ) * moves_per_turn );
    const int moves = std::max<int>( min_moves, moves_base * moves_modifier ) / light;
    if( moves > to_moves<int>( 1_minutes ) ) {
        // If more than 1 minute, inform the player
        p.add_msg_if_player(
            m_info,
            need_sunlight
            ? _( "If the current weather holds, it will take around %d minutes to light a fire." )
            : _( "At your skill level, it will take around %d minutes to light a fire." ),
            moves / to_moves<int>( 1_minutes ) );
    } else if( moves < to_moves<int>( 2_turns ) && get_map().is_flammable( pos ) ) {
        // If less than 2 turns, don't start a long action
        resolve_firestarter_use( p, pos );
        p.mod_moves( -moves );
        return it.type->charges_to_use();
    }

    // skill gains are handled by the activity, but stored here in the index field
    const int potential_skill_gain = moves_modifier + moves_cost_fast / 100.0 + 2;
    p.assign_activity(
        std::make_unique<player_activity>(
            std::make_unique<start_fire_activity_actor>( &it, bub_to_abs( pos ), potential_skill_gain,
                moves ) ) );
    p.activity->add_tool( &it );
    p.activity->values.push_back( g->natural_light_level( pos.z() ) );
    // charges to use are handled by the activity
    return 0;
}

void inscribe_actor::load( const JsonObject& obj )
{
    assign( obj, "cost", cost );
    assign( obj, "on_items", on_items );
    assign( obj, "on_terrain", on_terrain );
    assign( obj, "material_restricted", material_restricted );

    if( obj.has_array( "material_whitelist" ) ) {
        material_whitelist.clear();
        assign( obj, "material_whitelist", material_whitelist );
    }

    assign( obj, "verb", verb );
    assign( obj, "gerund", gerund );

    if( !on_items && !on_terrain ) {
        obj.throw_error(
            R"(Tried to create an useless inscribe_actor, at least on of "on_items" or "on_terrain" should be true)" );
    }
}

std::unique_ptr<iuse_actor> inscribe_actor::clone() const
{
    return std::make_unique<inscribe_actor>( *this );
}

bool inscribe_actor::item_inscription( item& tool, item& cut ) const
{
    if( !cut.made_of( SOLID ) ) {
    add_msg( m_info, _( "You can't inscribe an item that isn't solid!" ) );
        return false;
    }

    if( material_restricted && !cut.made_of_any( material_whitelist ) ) {
    std::string lower_verb = verb.translated();
        std::transform( lower_verb.begin(), lower_verb.end(), lower_verb.begin(), ::tolower );
        add_msg( m_info, _( "You can't %1$s %2$s because of the material it is made of." ), lower_verb,
                 cut.display_name() );
        return false;
    }

    enum inscription_type {
        INSCRIPTION_LABEL,
        INSCRIPTION_NOTE,
    };

    uilist menu;
    menu.text = string_format( _( "%s meaning?" ), verb );
    menu.addentry( INSCRIPTION_LABEL, true, -1, _( "It's a label" ) );
    menu.addentry( INSCRIPTION_NOTE, true, -1, _( "It's a note" ) );
    menu.query();

    std::string carving;
    std::string carving_tool;
    switch( menu.ret ) {
    case INSCRIPTION_LABEL:
        carving = "item_label";
        carving_tool = "item_label_tool";
        break;
    case INSCRIPTION_NOTE:
        carving = "item_note";
        carving_tool = "item_note_tool";
        break;
    default:
        return false;
}

const bool hasnote = cut.has_var( carving );
std::string messageprefix =
    ( hasnote ? _( "(To delete, clear the text and confirm)\n" ) : "" ) +
    //~ %1$s: gerund (e.g. carved), %2$s: item name
    string_format( pgettext( "carving", "%1$s on the %2$s is: " ), gerund, cut.type_name() );

string_input_popup popup;
popup.title( string_format( _( "%s what?" ), verb ) )
         .width( 64 )
         .text( hasnote ? cut.get_var( carving ) : std::string() )
         .description( messageprefix )
         .identifier( "inscribe_item" )
         .max_length( 128 )
         .query();
    if( popup.canceled() ) { return false; }
const std::string message = popup.text();
if( message.empty() ) {
    cut.erase_var( carving );
        cut.erase_var( carving_tool );
    } else {
        cut.set_var( carving, message );
        cut.set_var( carving_tool, tool.typeId().str() );
    }

    return true;
}

int inscribe_actor::use( player& p, item& it, bool t, const tripoint_bub_ms & ) const
{
    if( t ) { return 0; }

int choice = INT_MAX;
if( on_terrain && on_items ) {
    uilist imenu;
    imenu.text = string_format( _( "%s on what?" ), verb );
        imenu.addentry( 0, true, MENU_AUTOASSIGN, _( "The terrain" ) );
        imenu.addentry( 1, true, MENU_AUTOASSIGN, _( "An item" ) );
        imenu.query();
        choice = imenu.ret;
    } else if( on_terrain ) {
    choice = 0;
} else {
    choice = 1;
}

if( choice < 0 || choice > 1 ) { return 0; }

if( choice == 0 ) {
    const auto dest_ = choose_adjacent( _( "Write where?" ) );
        if( !dest_ ) { return 0; }
        return iuse::
               handle_ground_graffiti( p, &it, string_format( _( "%s what?" ), verb ), dest_.value() );
    }

    item* loc = game_menus::inv::titled_menu( get_avatar(), _( "Inscribe which item?" ) );
    if( !loc ) {
    p.add_msg_if_player( m_info, _( "Never mind." ) );
        return 0;
    }
    item& cut = *loc;
    if( &cut == &it ) {
    p.add_msg_if_player( _( "You try to bend your %s, but fail." ), it.tname() );
        return 0;
    }
    // inscribe_item returns false if the action fails or is canceled somehow.

    if( item_inscription( it, cut ) ) { return cost >= 0 ? cost : it.ammo_required(); }

    return 0;
}

void cauterize_actor::load( const JsonObject& obj )
{
    assign( obj, "cost", cost );
    assign( obj, "flame", flame );
}

std::unique_ptr<iuse_actor> cauterize_actor::clone() const
{
    return std::make_unique<cauterize_actor>( *this );
}

static heal_actor prepare_dummy()
{
    heal_actor dummy;
    dummy.limb_power = -2;
    dummy.head_power = -2;
    dummy.torso_power = -2;
    dummy.bleed = 1.0f;
    dummy.bite = 0.5f;
    dummy.move_cost = 100;
    return dummy;
}

bool cauterize_actor::cauterize_effect( player& p, item& it, bool force )
{
    // TODO: Make this less hacky
    static const heal_actor dummy = prepare_dummy();
    bodypart_str_id hpart = dummy.use_healing_item( p, p, it, force );
    if( hpart ) {
        p.add_msg_if_player( m_neutral, _( "You cauterize yourself." ) );
        if( !( p.has_trait( trait_NOPAIN ) ) ) {
            p.mod_pain( 15 );
            p.add_msg_if_player( m_bad, _( "It hurts like hell!" ) );
        } else {
            p.add_msg_if_player( m_neutral, _( "It itches a little." ) );
        }
        if( p.has_effect( effect_bite, hpart ) ) { p.add_effect( effect_bite, 260_minutes, hpart ); }

        p.moves = 0;
        return true;
    }

    return false;
}

int cauterize_actor::use( player& p, item& it, bool t, const tripoint_bub_ms & ) const
{
    if( t ) { return 0; }
if( p.is_mounted() ) {
        p.add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    bool has_disease = p.has_effect( effect_bite ) || p.has_effect( effect_bleed );
    bool did_cauterize = false;

    if( has_disease ) {
    did_cauterize = cauterize_effect( p, it, false );
    } else {
        const bool can_have_fun =
            p.has_trait( trait_MASOCHIST ) || p.has_trait( trait_MASOCHIST_MED )
            || p.has_trait( trait_CENOBITE );

        if( can_have_fun && query_yn( _( "Cauterize yourself for fun?" ) ) ) {
            did_cauterize = cauterize_effect( p, it, true );
        }
    }

    if( !did_cauterize ) { return 0; }

if( flame ) {
    p.use_charges( itype_fire, 4 );
        return 0;

    } else {
        return cost >= 0 ? cost : it.ammo_required();
    }
}

ret_val<bool> cauterize_actor::can_use(
    const Character& p, const item& it, bool, const tripoint_bub_ms & ) const
{
    if( !p.has_effect( effect_bite ) && !p.has_effect( effect_bleed ) && !p.has_trait( trait_MASOCHIST )
    && !p.has_trait( trait_MASOCHIST_MED ) && !p.has_trait( trait_CENOBITE ) ) {

    return ret_val<bool>::make_failure( _( "You are not bleeding or bitten, there is no need to "
                                           "cauterize yourself." ) );
    }
    if( p.is_mounted() ) {
    return ret_val<bool>::make_failure( _( "You cannot cauterize while mounted." ) );
    }

    if( flame ) {
    if( !p.has_charges( itype_fire, 4 ) ) {
            return ret_val<bool>::make_failure( _( "You need a source of flame (4 charges worth) "
                                                   "before you can cauterize yourself." ) );
        }
    } else {
        if( !it.units_sufficient( p ) ) {
            return ret_val<bool>::make_failure(
                       _( "You need at least %d charges to cauterize wounds." ), it.ammo_required() );
        }
    }

    if( p.is_underwater() ) {
    return ret_val<bool>::make_failure( _( "You can't do that while underwater." ) );
    }

    return ret_val<bool>::make_success();
}

void enzlave_actor::load( const JsonObject& obj ) { assign( obj, "cost", cost ); }

std::unique_ptr<iuse_actor> enzlave_actor::clone() const
{
    return std::make_unique<enzlave_actor>( *this );
}

int enzlave_actor::use( player& p, item& it, bool t, const tripoint_bub_ms & ) const
{
    if( t ) { return 0; }
if( p.is_mounted() ) {
        p.add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    map_stack items = get_map().i_at( p.bub_pos().xy() );
    std::vector<const item *> corpses;

for( item * const& corpse_candidate : items ) {
    const mtype* mt = corpse_candidate->get_mtype();
        if( corpse_candidate->is_corpse() && mt->in_species( ZOMBIE )
            && mt->made_of( material_id( "flesh" ) ) && mt->in_species( HUMAN )
            && corpse_candidate->is_active() && !corpse_candidate->has_var( "zlave" ) ) {
            corpses.push_back( corpse_candidate );
        }
    }

    if( corpses.empty() ) {
        p.add_msg_if_player( _( "No suitable corpses" ) );
        return 0;
    }

    int tolerance_level = 9;
    if( p.has_trait( trait_PSYCHOPATH ) || p.has_trait( trait_SAPIOVORE ) ) {
        tolerance_level = 0;
    } else if( p.has_trait_flag( trait_flag_PRED4 ) ) {
        tolerance_level = 5;
    } else if( p.has_trait_flag( trait_flag_PRED3 ) ) {
        tolerance_level = 7;
    }

    // Survival skill increases your willingness to get things done,
    // but it doesn't make you feel any less bad about it.
    /** @EFFECT_SURVIVAL increases tolerance for enzlavement */
    if( p.get_morale_level()
            <= ( 15 * ( tolerance_level - p.get_skill_level( skill_survival ) ) ) - 150 ) {
        add_msg( m_neutral, _( "The prospect of cutting up the corpse and letting it rise again as a "
                               "slave is too much for you to deal with right now." ) );
        return 0;
    }

    uilist amenu;

    amenu.text = _( "Selectively butcher the downed zombie into a zombie slave?" );
    for( size_t i = 0; i < corpses.size(); i++ ) {
        amenu.addentry( i, true, -1, corpses[i]->display_name() );
    }

    amenu.query();

    if( amenu.ret < 0 ) {
    p.add_msg_if_player( _( "Make love, not zlave." ) );
        return 0;
    }

    if( tolerance_level == 0 ) {
    // You just don't care, no message.
} else if( tolerance_level <= 5 ) {
    add_msg( m_neutral, _( "Well, it's more constructive than just chopping 'em into gooey "
                           "meat…" ) );
    } else {
        add_msg( m_bad, _( "You feel horrible for mutilating and enslaving someone's corpse." ) );

        /** @EFFECT_SURVIVAL decreases moral penalty and duration for enzlavement */
        int moraleMalus = -50 * ( 5.0 / p.get_skill_level( skill_survival ) );
        int maxMalus = -250 * ( 5.0 / p.get_skill_level( skill_survival ) );
        time_duration duration = 30_minutes * ( 5.0 / p.get_skill_level( skill_survival ) );
        time_duration decayDelay = 3_minutes * ( 5.0 / p.get_skill_level( skill_survival ) );

        if( p.has_trait( trait_PACIFIST ) ) {
            moraleMalus *= 5;
            maxMalus *= 3;
        } else if( p.has_trait_flag( trait_flag_PRED1 ) ) {
            moraleMalus /= 4;
        } else if( p.has_trait_flag( trait_flag_PRED2 ) ) {
            moraleMalus /= 5;
        }

        p.add_morale( MORALE_MUTILATE_CORPSE, moraleMalus, maxMalus, duration, decayDelay );
    }

    const int selected_corpse = amenu.ret;

    const item* body = corpses[selected_corpse];
    const mtype* mt = body->get_mtype();

    // HP range for zombies is roughly 36 to 120, with the really big ones having 180 and 480 hp.
    // Speed range is 20 - 120 (for humanoids, dogs get way faster)
    // This gives us a difficulty ranging roughly from 10 - 40, with up to +25 for corpse damage.
    // An average zombie with an undamaged corpse is 0 + 8 + 14 = 22.
    int difficulty = ( body->damage_level( 4 ) * 5 ) + ( mt->hp / 10 ) + ( mt->speed / 5 );
    // 0 - 30
    /** @EFFECT_DEX increases chance of success for enzlavement */

    /** @EFFECT_SURVIVAL increases chance of success for enzlavement */

    /** @EFFECT_FIRSTAID increases chance of success for enzlavement */
    int skills =
        p.get_skill_level( skill_survival ) + p.get_skill_level( skill_firstaid ) + ( p.dex_cur / 2 );
    skills *= 2;

    int success = rng( 0, skills ) - rng( 0, difficulty );

    /** @EFFECT_FIRSTAID speeds up enzlavement */
    const int moves = difficulty * to_moves<int>( 12_seconds ) / p.get_skill_level( skill_firstaid );

    p.assign_activity(
        std::make_unique<player_activity>( std::make_unique<make_zlave_activity_actor>(
                corpses[selected_corpse]->display_name(), success ) ),
        moves );

    return cost >= 0 ? cost : it.ammo_required();
}

ret_val<bool> enzlave_actor::can_use(
    const Character& p, const item &, bool, const tripoint_bub_ms & ) const
{
    /** @EFFECT_SURVIVAL >=1 allows enzlavement */

    /** @EFFECT_FIRSTAID >=1 allows enzlavement */

    // TODO: Extract such checks into some kind of 'stat_requirements' class.
    if( p.get_skill_level( skill_survival ) < 1 ) {
    //~ %s - name of the required skill.
    return ret_val<bool>::make_failure( _( "You need at least %s 1." ), skill_survival->name() );
    }
    if( p.is_mounted() ) {
    return ret_val<bool>::make_failure( _( "You cannot do that while mounted." ) );
    }
    if( p.get_skill_level( skill_firstaid ) < 1 ) {
    //~ %s - name of the required skill.
    return ret_val<bool>::make_failure( _( "You need at least %s 1." ), skill_firstaid->name() );
    }

    return ret_val<bool>::make_success();
}

void fireweapon_off_actor::load( const JsonObject& obj )
{
    obj.read( "target_id", target_id, true );
    success_message = obj.get_string( "success_message", "hsss" );
    lacks_fuel_message = obj.get_string( "lacks_fuel_message" );
    failure_message = obj.get_string( "failure_message", "hsss" );
    noise = obj.get_int( "noise", 0 );
    moves = obj.get_int( "moves", 0 );
    success_chance = obj.get_int( "success_chance", INT_MIN );
}

std::unique_ptr<iuse_actor> fireweapon_off_actor::clone() const
{
    return std::make_unique<fireweapon_off_actor>( *this );
}

int fireweapon_off_actor::use( player& p, item& it, bool t, const tripoint_bub_ms & ) const
{
    if( t ) { return 0; }

if( it.charges <= 0 ) {
    p.add_msg_if_player( _( lacks_fuel_message ) );
        return 0;
    }

    p.moves -= moves;
    if( rng( 0, 10 ) - it.damage_level( 4 ) > success_chance && !p.is_underwater() ) {
        if( noise > 0 ) {
            sounds::sound( p.bub_pos(), noise, sounds::sound_t::combat, _( success_message ) );
        }
        p.add_msg_if_player( _( success_message ) );
        if( p.is_npc() && get_player_character().sees( p ) ) {
            add_msg( m_info, _( "%s activates their %s." ), p.disp_name(), it.display_name() );
        }
        it.convert( target_id );
        it.activate();
    } else if( !failure_message.empty() ) {
        p.add_msg_if_player( m_bad, _( failure_message ) );
    }

    return it.type->charges_to_use();
}

ret_val<bool> fireweapon_off_actor::can_use(
    const Character& p, const item& it, bool, const tripoint_bub_ms & ) const
{
    if( it.charges < it.type->charges_to_use() ) {
    return ret_val<bool>::make_failure( _( "This tool doesn't have enough charges." ) );
    }

    if( p.is_underwater() ) {
    return ret_val<bool>::make_failure( _( "You can't do that while underwater." ) );
    }

    return ret_val<bool>::make_success();
}

void fireweapon_on_actor::load( const JsonObject& obj )
{
    noise_message = obj.get_string( "noise_message", "hsss" );
    voluntary_extinguish_message = obj.get_string( "voluntary_extinguish_message" );
    charges_extinguish_message = obj.get_string( "charges_extinguish_message" );
    water_extinguish_message = obj.get_string( "water_extinguish_message" );
    noise = obj.get_int( "noise", 0 );
    noise_chance = obj.get_int( "noise_chance", 1 );
    auto_extinguish_chance = obj.get_int( "auto_extinguish_chance", 0 );
    if( auto_extinguish_chance > 0 ) {
        auto_extinguish_message = obj.get_string( "auto_extinguish_message" );
    }
}

std::unique_ptr<iuse_actor> fireweapon_on_actor::clone() const
{
    return std::make_unique<fireweapon_on_actor>( *this );
}

int fireweapon_on_actor::use( player& p, item& it, bool t, const tripoint_bub_ms & ) const
{
    bool extinguish = true;
    if( it.charges == 0 ) {
        p.add_msg_if_player( m_bad, _( charges_extinguish_message ) );
        // Revert when it runs out of charges is handled in process_tool.
        extinguish = false;
    } else if( p.is_underwater() ) {
        p.add_msg_if_player( m_bad, _( water_extinguish_message ) );
    } else if( auto_extinguish_chance > 0 && one_in( auto_extinguish_chance ) ) {
        p.add_msg_if_player( m_bad, _( auto_extinguish_message ) );
    } else if( !t ) {
        p.add_msg_if_player( _( voluntary_extinguish_message ) );
    } else {
        extinguish = false;
    }

    if( extinguish ) {
        if( p.is_npc() && get_player_character().sees( p ) ) {
            add_msg( m_info, _( "%s deactivates their %s." ), p.disp_name(), it.display_name() );
        }
        it.revert( &p, false );
        it.deactivate();
        return 0;
    } else if( one_in( noise_chance ) ) {
        if( noise > 0 ) {
            sounds::sound( p.bub_pos(), noise, sounds::sound_t::combat, _( noise_message ) );
        }
        p.add_msg_if_player( _( noise_message ) );
    }

    return it.type->charges_to_use();
}

void manualnoise_actor::load( const JsonObject& obj )
{
    no_charges_message = obj.get_string( "no_charges_message" );
    use_message = obj.get_string( "use_message" );
    noise_message = obj.get_string( "noise_message", "hsss" );
    noise_id = obj.get_string( "noise_id", "misc" );
    noise_variant = obj.get_string( "noise_variant", "default" );
    noise = obj.get_int( "noise", 0 );
    moves = obj.get_int( "moves", 0 );
}

std::unique_ptr<iuse_actor> manualnoise_actor::clone() const
{
    return std::make_unique<manualnoise_actor>( *this );
}

int manualnoise_actor::use( player& p, item& it, bool t, const tripoint_bub_ms & ) const
{
    if( t ) { return 0; }
if( it.type->charges_to_use() != 0 && it.charges < it.type->charges_to_use() ) {
        p.add_msg_if_player( _( no_charges_message ) );
        return 0;
    }
    {
        p.moves -= moves;
        if( noise > 0 ) {
            sounds::sound(
                p.bub_pos(), noise, sounds::sound_t::activity,
                noise_message.empty() ? _( "Hsss" ) : _( noise_message ), true, noise_id,
                noise_variant );
        }
        p.add_msg_if_player( _( use_message ) );
    }
    return it.type->charges_to_use();
}

ret_val<bool> manualnoise_actor::can_use(
    const Character &, const item& it, bool, const tripoint_bub_ms & ) const
{
    if( it.charges < it.type->charges_to_use() ) {
    return ret_val<bool>::make_failure( _( "This tool doesn't have enough charges." ) );
    }

    return ret_val<bool>::make_success();
}

std::unique_ptr<iuse_actor> musical_instrument_actor::clone() const
{
    return std::make_unique<musical_instrument_actor>( *this );
}

void musical_instrument_actor::load( const JsonObject& obj )
{
    speed_penalty = obj.get_int( "speed_penalty", 10 );
    volume = obj.get_int( "volume" );
    fun = obj.get_int( "fun" );
    fun_bonus = obj.get_int( "fun_bonus", 0 );
    if( !obj.read( "description_frequency", description_frequency ) ) {
        obj.throw_error( "missing member \"description_frequency\"" );
    }
    player_descriptions = obj.get_string_array( "player_descriptions" );
    npc_descriptions = obj.get_string_array( "npc_descriptions" );
}

int musical_instrument_actor::use( player& p, item& it, bool t, const tripoint_bub_ms & ) const
{
    if( p.is_mounted() ) {
    p.add_msg_player_or_npc(
        m_bad, _( "You can't play music while mounted." ),
        _( "<npcname> can't play music while mounted." ) );
        it.deactivate();
        return 0;
    }
    if( p.is_underwater() ) {
    p.add_msg_player_or_npc(
        m_bad, _( "You can't play music underwater" ),
        _( "<npcname> can't play music underwater" ) );
        it.deactivate();
        return 0;
    }

    if( p.has_effect( effect_sleep ) || p.has_effect( effect_stunned ) ||
        p.has_effect( effect_asthma ) ) {
    p.add_msg_player_or_npc(
        m_bad, _( "You stop playing your %s" ), _( "<npcname> stops playing their %s" ),
        it.display_name() );
        it.deactivate();
        return 0;
    }

    if( !t && it.is_active() ) {
    p.add_msg_player_or_npc(
        _( "You stop playing your %s" ), _( "<npcname> stops playing their %s" ),
        it.display_name() );
        it.deactivate();
        return 0;
    }

    // Check for worn or wielded - no "floating"/bionic instruments for now
    // TODO: Distinguish instruments played with hands and with mouth, consider encumbrance
    const int inv_pos = p.get_item_position( &it );
    if( inv_pos >= 0 || inv_pos == INT_MIN ) {
    p.add_msg_player_or_npc(
        m_bad, _( "You need to hold or wear %s to play it" ),
        _( "<npcname> needs to hold or wear %s to play it" ), it.display_name() );
        it.deactivate();
        return 0;
    }

    // At speed this low you can't coordinate your actions well enough to play the instrument
    if( p.get_speed() <= 25 + speed_penalty ) {
    p.add_msg_player_or_npc(
        m_bad, _( "You feel too weak to play your %s" ),
        _( "<npcname> feels too weak to play their %s" ), it.display_name() );
        it.deactivate();
        return 0;
    }

    // We can play the music now
    if( !it.is_active() ) {
    p.add_msg_player_or_npc(
        m_good, _( "You start playing your %s" ), _( "<npcname> starts playing their %s" ),
        it.display_name() );
        it.activate();
    }

    if( p.get_effect_int( effect_playing_instrument ) <= speed_penalty ) {
    // Only re-apply the effect if it wouldn't lower the intensity
    p.add_effect( effect_playing_instrument, 2_turns, bodypart_str_id::NULL_ID(), speed_penalty );
    }

    std::string desc = "music";
    /** @EFFECT_PER increases morale bonus when playing an instrument */
    const int morale_effect = fun + fun_bonus * p.per_cur;
    if( morale_effect >= 0 && calendar::once_every( description_frequency ) ) {
    if( !player_descriptions.empty() && p.is_player() ) {
            desc = _( random_entry( player_descriptions ) );
        }
    } else if( morale_effect < 0 && calendar::once_every( 1_minutes ) ) {
    // No musical skills = possible morale penalty
    if( p.is_player() ) {
            desc = _( "You produce an annoying sound" );
        } else {
            desc = string_format( _( "%s produces an annoying sound" ), p.disp_name( false ) );
        }
        // Continuous sound messages only print every so often, so this ensures when it does print
        // it'll be the right one.
    } else if( !npc_descriptions.empty() && p.is_npc() ) {
    desc = string_format( _( "%1$s %2$s" ), p.disp_name( false ), random_entry( npc_descriptions ) );
    }

    if( morale_effect >= 0 ) {
    sounds::sound( p.bub_pos(), volume, sounds::sound_t::music, desc, true, "musical_instrument",
                   it.typeId().str() );
    } else {
        sounds::sound( p.bub_pos(), volume, sounds::sound_t::music, desc, true,
                       "musical_instrument_bad", it.typeId().str() );
    }

    if( !p.has_effect( effect_music ) && p.can_hear( p.bub_pos(), volume ) ) {
        // Sound code doesn't describe noises at the player position
        if( p.is_player() && desc != "music" ) { add_msg( m_info, desc ); }
        p.add_effect( effect_music, 1_turns );
        const int sign = morale_effect > 0 ? 1 : -1;
        p.add_morale( MORALE_MUSIC, sign, morale_effect, 5_minutes, 2_minutes, true );
    }

    return 0;
}

ret_val<bool> musical_instrument_actor::can_use(
    const Character& p, const item &, bool, const tripoint_bub_ms & ) const
{
    // TODO: (maybe): Mouth encumbrance? Smoke? Lack of arms? Hand encumbrance?
    if( p.is_underwater() ) {
    return ret_val<bool>::make_failure( _( "You can't do that while underwater." ) );
    }
    if( p.is_mounted() ) {
    return ret_val<bool>::make_failure( _( "You can't do that while mounted." ) );
    }

    return ret_val<bool>::make_success();
}

std::unique_ptr<iuse_actor> learn_spell_actor::clone() const
{
    return std::make_unique<learn_spell_actor>( *this );
}

void learn_spell_actor::load( const JsonObject& obj ) { spells = obj.get_string_array( "spells" ); }

void learn_spell_actor::info( const item &, std::vector<iteminfo> &dump ) const
{
    std::string message;
    if( spells.size() == 1 ) {
        message = _( "This can teach you a spell." );
    } else {
        message = _( "This can teach you a number of spells." );
    }
    dump.emplace_back( "DESCRIPTION", message );
    dump.emplace_back( "DESCRIPTION", _( "Spells Contained:" ) );
    for( const std::string& sp : spells ) {
        dump.emplace_back( "SPELL", spell_id( sp ).obj().name.translated() );
    }
}

int learn_spell_actor::use( player& p, item &, bool, const tripoint_bub_ms & ) const
{
    if( !character_funcs::can_see_fine_details( p ) ) {
    p.add_msg_if_player( _( "It's too dark to read." ) );
        return 0;
    }
    std::vector<uilist_entry> uilist_initializer;
    uilist spellbook_uilist;
    spellbook_callback sp_cb;
    bool know_it_all = true;
for( const std::string& sp_id_str : spells ) {
    const spell_id sp_id( sp_id_str );
        sp_cb.add_spell( sp_id );
        uilist_entry entry( sp_id.obj().name.translated() );
        if( p.magic->knows_spell( sp_id ) ) {
            const spell sp = p.magic->get_spell( sp_id );
            entry.ctxt = string_format( _( "Level %u" ), sp.get_level() );
            if( sp.is_max_level() ) {
                entry.ctxt += _( " (Max)" );
                entry.enabled = false;
            } else {
                know_it_all = false;
            }
        } else {
            if( p.magic->can_learn_spell( p, sp_id ) ) {
                entry.ctxt = _( "Study to Learn" );
                know_it_all = false;
            } else {
                entry.ctxt = _( "Can't learn!" );
                entry.enabled = false;
            }
        }
        uilist_initializer.emplace_back( entry );
    }

    if( know_it_all ) {
    add_msg( m_info, _( "You already know everything this could teach you." ) );
        return 0;
    }

    spellbook_uilist.entries = uilist_initializer;
    spellbook_uilist.w_height_setup = 24;
    spellbook_uilist.w_width_setup = 80;
    spellbook_uilist.callback = &sp_cb;
    spellbook_uilist.menu_style = "info"; // RmlUi: two-column with spell description panel
    spellbook_uilist.title = _( "Study a spell:" );
    spellbook_uilist.pad_left_setup = 38;
    spellbook_uilist.query();
    const int action = spellbook_uilist.ret;
    if( action < 0 ) { return 0; }
const bool knows_spell = p.magic->knows_spell( spells[action] );
std::string mode = "learn";
bool gain_level = false;
if( knows_spell ) {
    mode = "study";
    const int study_time = uilist(
    _( "Spend how long studying?" ), {
        {to_moves<int>( 30_minutes ), true, -1, _( "30 minutes" )},
            {to_moves<int>( 1_hours ), true, -1, _( "1 hour" )},
            {to_moves<int>( 2_hours ), true, -1, _( "2 hours" )},
            {to_moves<int>( 4_hours ), true, -1, _( "4 hours" )},
            {to_moves<int>( 8_hours ), true, -1, _( "8 hours" )},
            {10100, true, -1, _( "Until you gain a spell level" )}
        } );
        if( study_time <= 0 ) { return 0; }
        if( study_time == 10100 ) { gain_level = true; }
        p.assign_activity( std::make_unique<player_activity>(
                               std::make_unique <
                               study_spell_activity_actor > ( spells[action], mode, gain_level, study_time ) ) );
    } else {
        p.assign_activity(
            std::make_unique<player_activity>( std::make_unique<study_spell_activity_actor>(
                    spells[action], mode, gain_level,
                    p.magic->time_to_learn_spell( p, spells[action] ) ) ) );
    }
    return 0;
}

std::unique_ptr<iuse_actor> cast_spell_actor::clone() const
{
    return std::make_unique<cast_spell_actor>( *this );
}

void cast_spell_actor::load( const JsonObject& obj )
{
    no_fail = obj.get_bool( "no_fail" );
    item_spell = spell_id( obj.get_string( "spell_id" ) );
    spell_level = obj.get_int( "level" );
    need_worn = obj.get_bool( "need_worn", false );
    need_wielding = obj.get_bool( "need_wielding", false );
}

void cast_spell_actor::info( const item &, std::vector<iteminfo> &dump ) const
{
    //~ %1$s: spell name, %2$i: spell level
    const std::string message =
        string_format( _( "This item casts %1$s at level %2$i." ), item_spell->name, spell_level );
    dump.emplace_back( "DESCRIPTION", message );
    if( no_fail ) { dump.emplace_back( "DESCRIPTION", _( "This item never fails." ) ); }
}

int cast_spell_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    if( need_worn && !p.is_worn( it ) ) {
    p.add_msg_if_player(
        m_info, _( "You need to wear the %1$s before activating it." ), it.tname() );
        return 0;
    }
    if( need_wielding && !p.is_wielding( it ) ) {
    p.add_msg_if_player(
        m_info, _( "You need to wield the %1$s before activating it." ), it.tname() );
        return 0;
    }

    spell casting = spell( spell_id( item_spell ) );
    bool mana_override = false;
    if( it.has_flag( flag_USE_PLAYER_ENERGY ) ) {
    if( p.magic->has_enough_energy( p, casting ) ) {
            mana_override = true;
        } else {
            p.add_msg_if_player( m_info, _( "You lack the energy to cast %s." ), casting.name() );
            return 0;
        }
    }

    auto cast_spell = std::make_unique <
                      player_activity > ( std::make_unique<spellcasting_activity_actor>(
                              casting.id().str(), spell_level, no_fail, mana_override, casting.casting_time( p ) ) );
    cast_spell->targets.emplace_back( &it );
    p.assign_activity( std::move( cast_spell ), false );
    return 0;
}

std::unique_ptr<iuse_actor> holster_actor::clone() const
{
    return std::make_unique<holster_actor>( *this );
}

void holster_actor::load( const JsonObject& obj )
{
    holster_prompt = obj.get_string( "holster_prompt", "" );
    holster_msg = obj.get_string( "holster_msg", "" );
    assign( obj, "max_volume", max_volume );
    if( !assign( obj, "min_volume", min_volume ) ) { min_volume = max_volume / 3; }

    assign( obj, "max_weight", max_weight );
    multi = obj.get_int( "multi", multi );
    draw_cost = obj.get_int( "draw_cost", draw_cost );

    auto tmp = obj.get_string_array( "skills" );
    std::transform( tmp.begin(), tmp.end(),
    std::back_inserter( skills ), []( const std::string & elem ) {
        return skill_id( elem );
    } );

    flags = obj.get_string_array( "flags" );
}

bool holster_actor::can_holster( const item& obj ) const
{
    if( obj.volume() > max_volume || obj.volume() < min_volume ) { return false; }
if( max_weight > 0_gram && obj.weight() > max_weight ) { return false; }
if( obj.is_active() ) { return false; }
return std::any_of( flags.begin(), flags.end(),
[&]( const std::string & f ) { return obj.has_flag( flag_id( f ) ); } )
    || std::find( skills.begin(), skills.end(), obj.gun_skill() ) != skills.end();
}

detached_ptr<item> holster_actor::store( player& p, item& holster, detached_ptr<item>&& obj ) const
{
    if( obj->is_null() || holster.is_null() ) {
    debugmsg( "Null item was passed to holster_actor" );
        return std::move( obj );
    }

    // if selected item is unsuitable inform the player why not
    if( obj->volume() > max_volume ) {
    p.add_msg_if_player(
        m_info, _( "Your %1$s is too big to fit in your %2$s" ), obj->tname(), holster.tname() );
        return std::move( obj );
    }

    if( obj->volume() < min_volume ) {
    p.add_msg_if_player(
        m_info, _( "Your %1$s is too small to fit in your %2$s" ), obj->tname(), holster.tname() );
        return std::move( obj );
    }

    if( max_weight > 0_gram && obj->weight() > max_weight ) {
    p.add_msg_if_player(
        m_info, _( "Your %1$s is too heavy to fit in your %2$s" ), obj->tname(), holster.tname() );
        return std::move( obj );
    }

    if( obj->is_active() ) {
    p.add_msg_if_player(
        m_info, _( "You don't think putting your %1$s in your %2$s is a good idea" ),
        obj->tname(), holster.tname() );
        return std::move( obj );
    }

    if( std::none_of( flags.begin(), flags.end(),
    [&]( const std::string & f ) { return obj->has_flag( flag_id( f ) ); } )
    && std::find( skills.begin(), skills.end(), obj->gun_skill() ) == skills.end() ) {
        p.add_msg_if_player(
            m_info, _( "You can't put your %1$s in your %2$s" ), obj->tname(), holster.tname() );
        return std::move( obj );
    }

    p.add_msg_if_player(
        holster_msg.empty() ? _( "You holster your %s" ) : _( holster_msg ), obj->tname(),
        holster.tname() );

    // Holsters ignore penalty effects (e.g. GRABBED) when determining number of moves to consume
    character_funcs::store_in_container( p, holster, std::move( obj ), false, draw_cost );
    return detached_ptr<item>();
}

int holster_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    if( p.is_wielding( it ) ) {
    p.add_msg_if_player( _( "You need to unwield your %s before using it." ), it.tname() );
        return 0;
    }

    int pos = 0;
    std::vector<std::string> opts;

    if( static_cast<int>( it.contents.num_item_stacks() ) < multi ) {
        std::string prompt = holster_prompt.empty() ? _( "Holster item" ) : _( holster_prompt );
        opts.push_back( prompt );
        pos = -1;
    }

    std::vector<item *> top_contents{it.contents.all_items_top()};
    std::transform(
        top_contents.begin(), top_contents.end(), std::back_inserter( opts ),
    []( const item * elem ) { return string_format( _( "Draw %s" ), elem->display_name() ); } );

    item* internal_item = nullptr;
    if( opts.size() > 1 ) {
    int ret = uilist( string_format( _( "Use %s" ), it.tname() ), opts );
        if( ret < 0 ) {
            pos = -2;
        } else {
            pos += ret;
            if( opts.size() != it.contents.num_item_stacks() ) { ret--; }
            auto iter = std::next( top_contents.begin(), ret );
            internal_item = *iter;
        }
    } else if( !it.contents.empty() ) {
    internal_item = &it.contents.front();
    }

    if( pos < -1 ) {
    p.add_msg_if_player( _( "Never mind." ) );
        return 0;
    }

    if( pos >= 0 ) {
    // Worn holsters ignore penalty effects (e.g. GRABBED) when determining number of moves to
    // consume
    bool penalties;
    int cost;
    if( p.is_worn( it ) ) {
            penalties = false;
            cost = draw_cost;
        } else {
            penalties = true;
            cost = INVENTORY_HANDLING_PENALTY;
        }
        character_funcs::try_wield_contents( p, it, internal_item, penalties, cost );

    } else {
        item* loc = game_menus::inv::holster( p, it );

        if( !loc ) {
            p.add_msg_if_player( _( "Never mind." ) );
            return 0;
        }
        store( p, it, loc->detach() );
    }

    return 0;
}

void holster_actor::info( const item &, std::vector<iteminfo> &dump ) const
{
    std::string message =
        vgettext( "Can be activated to store a suitable item.",
        "Can be activated to store suitable items.", multi );
    dump.emplace_back( "DESCRIPTION", message );
    dump.emplace_back( "TOOL", _( "Num items: " ), "<num>", iteminfo::no_flags, multi );
    dump.emplace_back(
    "TOOL", _( "Item volume: Min: " ), string_format( "<num> %s", volume_units_abbr() ),
    iteminfo::is_decimal | iteminfo::no_newline | iteminfo::lower_is_better,
    convert_volume( min_volume.value() ) );
    dump.emplace_back( "TOOL", _( "  Max: " ), string_format( "<num> %s", volume_units_abbr() ),
    iteminfo::is_decimal, convert_volume( max_volume.value() ) );

    if( max_weight > 0_gram ) {
    dump.emplace_back(
        "TOOL", _( "Max item weight: " ), string_format( _( "<num> %s" ), weight_units() ),
        iteminfo::is_decimal, convert_weight( max_weight ) );
    }
}

units::volume holster_actor::max_stored_volume() const { return max_volume * multi; }

std::unique_ptr<iuse_actor> bandolier_actor::clone() const
{
    return std::make_unique<bandolier_actor>( *this );
}

void bandolier_actor::load( const JsonObject& obj )
{
    capacity = obj.get_int( "capacity", capacity );
    ammo.clear();
    for( auto& e : obj.get_tags( "ammo" ) ) { ammo.insert( ammotype( e ) ); }

    draw_cost = obj.get_int( "draw_cost", draw_cost );
}

void bandolier_actor::info( const item &, std::vector<iteminfo> &dump ) const
{
    if( !ammo.empty() ) {
    auto str = enumerate_as_string(
                   ammo.begin(), ammo.end(),
    [&]( const ammotype & a ) { return string_format( "<stat>%s</stat>", a->name() ); },
        enumeration_conjunction::or_ );

        dump.emplace_back(
            "TOOL",
            string_format(
                vgettext( "Can be activated to store a single round of ",
                          "Can be activated to store up to <stat>%i</stat> rounds of ", capacity ),
                capacity ),
            str );
    }
}

bool bandolier_actor::is_valid_ammo_type( const itype& t ) const
{
    if( !t.ammo ) { return false; }
return ammo.contains( t.ammo->type );
}

bool bandolier_actor::can_store( const item& bandolier, const item& obj ) const
{
    if( !bandolier.contents.empty()
    && ( bandolier.contents.front().typeId() != obj.typeId()
             || bandolier.contents.front().charges >= capacity ) ) {
        return false;
    }

    return is_valid_ammo_type( *obj.type );
}

bool bandolier_actor::reload( player& p, item& obj ) const
{
    if( !obj.is_bandolier() ) {
    debugmsg( "Invalid item passed to bandolier_actor" );
        return false;
    }
    // find all nearby compatible ammo (matching type currently contained if appropriate)
    auto found = p.nearby( [&]( const item * e, const item * parent ) {
        return parent != &obj && can_store( obj, *e );
    } );

    if( found.empty() ) {
    p.add_msg_if_player( m_bad, _( "No matching ammo for the %1$s" ), obj.type_name() );
        return false;
    }

    // convert these into reload options and display the selection prompt
    std::vector<item_reload_option> opts;
    std::transform(
        std::make_move_iterator( found.begin() ), std::make_move_iterator( found.end() ),
    std::back_inserter( opts ), [&]( item * e ) { return item_reload_option( &p, &obj, &obj, *e ); } );

    item_reload_option sel = character_funcs::select_ammo( p, obj, std::move( opts ) );
    if( !sel ) {
    return false; // canceled menu
}

p.mod_moves( -sel.moves() );

// add or stack the ammo dependent upon existing contents
if( obj.contents.empty() ) {
    obj.put_in( sel.ammo->split( sel.qty() ) );
    } else {
        obj.contents.front().charges += sel.qty();
        if( sel.ammo->charges > sel.qty() ) {
            sel.ammo->charges -= sel.qty();
        } else {
            sel.ammo->detach();
        }
    }

    p.add_msg_if_player(
        _( "You store the %1$s in your %2$s" ), obj.contents.front().tname( sel.qty() ),
        obj.type_name() );

    return true;
}

int bandolier_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    if( p.is_wielding( it ) ) {
    p.add_msg_if_player( _( "You need to unwield your %s before using it." ), it.type_name() );
        return 0;
    }

    uilist menu;
    menu.text = _( "Store ammo" );

    std::vector<std::function<void()>> actions;

    menu.addentry( -1, it.contents.empty() || it.contents.front().charges < capacity, 'r',
                   _( "Store ammo in %s" ), it.type_name() );

    actions.emplace_back( [&] { reload( p, it ); } );

    menu.addentry( -1, !it.contents.empty(), 'u', _( "Unload %s" ), it.type_name() );

    actions.emplace_back( [&] {
        it.contents.front().attempt_detach( [&p]( detached_ptr<item>&& it )
        {
            it = p.i_add_or_drop( std::move( it ) );
            if( it ) { p.add_msg_if_player( _( "Never mind." ) ); }
            return std::move( it );
        } );
    } );

    menu.query();
    if( menu.ret >= 0 ) { actions[menu.ret](); }

    return 0;
}

units::volume bandolier_actor::max_stored_volume() const
{
    // This is relevant only for bandoliers with the non-rigid flag

    // Find all valid ammo
    auto ammo_types = item_controller->find( [&]( const itype & t ) { return is_valid_ammo_type( t ); } );
    // Figure out which has the greatest volume and calculate on that basis
    units::volume max_ammo_volume{};
    for( const auto * ammo_type : ammo_types ) {
        max_ammo_volume = std::max( max_ammo_volume, ammo_type->volume / ammo_type->stack_size );
    }
    return max_ammo_volume * capacity;
}
