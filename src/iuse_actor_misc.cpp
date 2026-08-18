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


place_trap_actor::place_trap_actor( const std::string& type )
    : iuse_actor( type ),
      needs_neighbor_terrain( ter_str_id::NULL_ID() ),
      outer_layer_trap( trap_str_id::NULL_ID() ) {}

place_trap_actor::data::data(): trap( trap_str_id::NULL_ID() ) {}

void place_trap_actor::data::load( const JsonObject& obj )
{
    assign( obj, "trap", trap );
    assign( obj, "done_message", done_message );
    assign( obj, "practice", practice );
    assign( obj, "moves", moves );
}

void place_trap_actor::load( const JsonObject& obj )
{
    assign( obj, "allow_underwater", allow_underwater );
    assign( obj, "allow_under_player", allow_under_player );
    assign( obj, "needs_solid_neighbor", needs_solid_neighbor );
    assign( obj, "needs_neighbor_terrain", needs_neighbor_terrain );
    assign( obj, "bury_question", bury_question );
    if( !bury_question.empty() ) {
        JsonObject buried_json = obj.get_object( "bury" );
        buried_data.load( buried_json );
    }
    unburied_data.load( obj );
    assign( obj, "outer_layer_trap", outer_layer_trap );
}

std::unique_ptr<iuse_actor> place_trap_actor::clone() const
{
    return std::make_unique<place_trap_actor>( *this );
}

static bool is_solid_neighbor( const tripoint_bub_ms& pos, point offset )
{
    map& here = get_map();
    const auto a = pos + offset;
    const auto b = pos - offset;
    return here.move_cost( a ) != 2 && here.move_cost( b ) != 2;
}

static bool has_neighbor( const tripoint_bub_ms& pos, const ter_id& terrain_id )
{
    map& here = get_map();
    for( const tripoint_bub_ms& t : here.points_in_radius( pos, 1, 0 ) ) {
        if( here.ter( t ) == terrain_id ) { return true; }
    }
    return false;
}

bool place_trap_actor::is_allowed(
    player& p, const tripoint_bub_ms& pos, const std::string& name ) const
{
    if( !allow_under_player && pos == p.bub_pos() ) {
        p.add_msg_if_player(
            m_info, _( "Yeah.  Place the %s at your feet.  Real damn smart move." ), name );
        return false;
    }
    map& here = get_map();
    if( here.move_cost( pos ) != 2 ) {
        p.add_msg_if_player( m_info, _( "You can't place a %s there." ), name );
        return false;
    }
    if( needs_solid_neighbor ) {
        if( !is_solid_neighbor( pos, point_east ) && !is_solid_neighbor( pos, point_south )
            && !is_solid_neighbor( pos, point_south_east )
            && !is_solid_neighbor( pos, point_north_east ) ) {
            p.add_msg_if_player( m_info, _( "You must place the %s between two solid tiles." ), name );
            return false;
        }
    }
    if( needs_neighbor_terrain && !has_neighbor( pos, needs_neighbor_terrain ) ) {
        p.add_msg_if_player(
            m_info, _( "The %s needs a %s adjacent to it." ), name,
            needs_neighbor_terrain.obj().name() );
        return false;
    }
    const trap& existing_trap = here.tr_at( pos );
    if( !existing_trap.is_null() ) {
        if( existing_trap.can_see( pos, p ) ) {
            p.add_msg_if_player(
                m_info, _( "You can't place a %s there.  It contains a trap already." ), name );
        } else {
            p.add_msg_if_player( m_bad, _( "You trigger a %s!" ), existing_trap.name() );
            existing_trap.trigger( pos, &p );
        }
        return false;
    }
    return true;
}

static void place_and_add_as_known( player& p, const tripoint_bub_ms& pos, const trap_str_id& id )
{
    map& here = get_map();
    here.trap_set( pos, id );
    const trap& tr = here.tr_at( pos );
    if( !tr.can_see( pos, p ) ) { p.add_known_trap( pos, tr ); }
}

int place_trap_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    const bool could_bury = !bury_question.empty();
    if( !allow_underwater && p.is_underwater() ) {
        p.add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }
    if( p.is_mounted() ) {
        p.add_msg_if_player( m_info, _( "You can't do that while mounted." ) );
        return 0;
    }
    const std::optional<tripoint_bub_ms> pos_ = choose_adjacent(
            string_format( _( "Place %s where?" ), it.tname() ) );
    if( !pos_ ) { return 0; }
    auto pos = *pos_;

    if( !is_allowed( p, pos, it.tname() ) ) { return 0; }

    map& here = get_map();
    int distance_to_trap_center =
        unburied_data.trap.obj().get_trap_radius() + outer_layer_trap.obj().get_trap_radius() + 1;
    if( unburied_data.trap.obj().get_trap_radius() > 0 ) {
        // Math correction for multi-tile traps
        pos.x() = ( pos.x() - p.bub_pos().x() ) * distance_to_trap_center + p.bub_pos().x();
        pos.y() = ( pos.y() - p.bub_pos().y() ) * distance_to_trap_center + p.bub_pos().y();
        for( const tripoint_bub_ms& t :
             here.points_in_radius( pos, outer_layer_trap.obj().get_trap_radius(), 0 ) ) {
            if( !is_allowed( p, t, it.tname() ) ) {
                p.add_msg_if_player(
                    m_info,
                    _( "That trap needs a space in %d tiles radius to be clear, centered %d tiles "
                       "from you." ),
                    outer_layer_trap.obj().get_trap_radius(), distance_to_trap_center );
                return 0;
            }
        }
    }

    const bool has_shovel = p.has_quality( quality_id( "DIG" ), 3 );
    const bool is_diggable = here.ter( pos )->is_diggable();
    bool bury = false;
    if( could_bury && has_shovel && is_diggable ) { bury = query_yn( _( bury_question ) ); }
    const auto& data = bury ? buried_data : unburied_data;

    p.add_msg_if_player( m_info, _( data.done_message ), distance_to_trap_center );
    p.practice( skill_id( "traps" ), data.practice );
    p.mod_moves( -data.moves );

    place_and_add_as_known( p, pos, data.trap );
    for( const tripoint_bub_ms& t :
         here.points_in_radius( pos, data.trap.obj().get_trap_radius(), 0 ) ) {
        if( t != pos ) { place_and_add_as_known( p, t, outer_layer_trap ); }
    }
    return 1;
}

void emit_actor::load( const JsonObject& obj )
{
    assign( obj, "emits", emits );
    assign( obj, "scale_qty", scale_qty );
}

int emit_actor::use( player &, item& it, bool, const tripoint_bub_ms& pos ) const
{
    map& here = get_map();
    const float scaling = scale_qty ? it.charges : 1;
    for( const auto& e : emits ) { here.emit_field( pos, e, scaling ); }

    return 1;
}

std::unique_ptr<iuse_actor> emit_actor::clone() const
{
    return std::make_unique<emit_actor>( *this );
}

void emit_actor::finalize( const itype_id& my_item_type )
{
    /*
    // TODO: This must be called after all finalization
    for( const auto& e : emits ) {
        if( !e.is_valid() ) {
            debugmsg( "Item %s has unknown emit source %s", my_item_type.c_str(), e.c_str() );
        }
    }
    */

    if( scale_qty && !item::count_by_charges( my_item_type ) ) {
        debugmsg( "Item %s has emit_actor with scale_qty, but is not counted by charges",
                  my_item_type.c_str() );
        scale_qty = false;
    }
}

void saw_barrel_actor::load( const JsonObject& jo ) { assign( jo, "cost", cost ); }

int saw_barrel_actor::use( player& p, item& it, bool t, const tripoint_bub_ms & ) const
{
    if( t ) { return 0; }

auto loc = game_menus::inv::saw_barrel( p, it );

if( !loc ) {
    p.add_msg_if_player( _( "Never mind." ) );
        return 0;
    }

    loc->obtain( p );
    p.add_msg_if_player( _( "You saw down the barrel of your %s." ), loc->tname() );
    loc->put_in( item::spawn( "barrel_small", calendar::turn ) );

    return 0;
}

ret_val<bool> saw_barrel_actor::can_use_on( const player &, const item &,
        const item& target ) const
{
    if( !target.is_gun() ) { return ret_val<bool>::make_failure( _( "It's not a gun." ) ); }

    if( target.type->gun->barrel_volume <= 0_ml ) {
    return ret_val<bool>::make_failure( _( "The barrel is too short." ) );
    }

    if( target.gunmod_find( itype_barrel_small ) ) {
    return ret_val<bool>::make_failure( _( "The barrel is already sawn-off." ) );
    }

    const auto gunmods = target.gunmods();
    const bool modified_barrel = std::any_of( gunmods.begin(), gunmods.end(), []( const item * mod ) {
        return mod->type->gunmod->location == gunmod_location( "barrel" );
    } );

    if( modified_barrel ) {
    return ret_val<bool>::make_failure( _( "Can't saw off modified barrels." ) );
    }

    return ret_val<bool>::make_success();
}

std::unique_ptr<iuse_actor> saw_barrel_actor::clone() const
{
    return std::make_unique<saw_barrel_actor>( *this );
}

void saw_stock_actor::load( const JsonObject& jo ) { assign( jo, "cost", cost ); }

int saw_stock_actor::use( player& p, item& it, bool t, const tripoint_bub_ms & ) const
{
    if( t ) { return 0; }

auto loc = game_menus::inv::saw_stock( p, it );

if( !loc ) {
    p.add_msg_if_player( _( "Never mind." ) );
        return 0;
    }

    loc->obtain( p );
    p.add_msg_if_player( _( "You saw down the stock of your %s." ), loc->tname() );
    loc->put_in( item::spawn( "stock_small", calendar::turn ) );

    return 0;
}

ret_val<bool> saw_stock_actor::can_use_on( const player &, const item &, const item& target ) const
{
    if( !target.is_gun() ) { return ret_val<bool>::make_failure( _( "It's not a gun." ) ); }

    if( target.gunmod_find( itype_stock_small ) ) {
    return ret_val<bool>::make_failure( _( "The stock is already sawn-off." ) );
    }

    // Exclude pistols and the like that have had a stock mount bubba'd onto them.
    const auto gunmods = target.gunmods();
    const bool external_stock = std::any_of( gunmods.begin(), gunmods.end(), []( const item * mod ) {
        return mod->type->gunmod->location == gunmod_location( "stock mount" );
    } );

    if( external_stock ) {
    return ret_val<bool>::make_failure( _( "You can't saw anything off this." ) );
    }

    // Don't allow trying to stack stock mods.
    const bool modified_stock = std::any_of( gunmods.begin(), gunmods.end(), []( const item * mod ) {
        return mod->type->gunmod->location == gunmod_location( "stock" );
    } );

    if( modified_stock ) { return ret_val<bool>::make_failure( _( "Can't cut off modified stocks." ) ); }

    // Also bail out if there's no unmodified stock to touch at all.
    if( target.get_free_mod_locations( gunmod_location( "stock" ) ) == 0
            || target.type->gun->skill_used == skill_id( "pistol" ) ) {
        return ret_val<bool>::make_failure( _( "This doesn't have a stock." ) );
    }

    // Stock ideally should be made out of wood.
    if( !target.made_of( material_id( "wood" ) ) ) {
        return ret_val<bool>::make_failure( _( "Can't cut off non-wooden stocks." ) );
    }

    return ret_val<bool>::make_success();
}

std::unique_ptr<iuse_actor> saw_stock_actor::clone() const
{
    return std::make_unique<saw_stock_actor>( *this );
}

int install_bionic_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    if( p.can_install_bionics( *it.type, p, false ) ) {
    return p.install_bionics( *it.type, p, false ) ? it.type->charges_to_use() : 0;
    } else {
        return 0;
    }
}

ret_val<bool> install_bionic_actor::can_use(
    const Character& p, const item& it, bool, const tripoint_bub_ms & ) const
{
    if( !it.is_bionic() ) { return ret_val<bool>::make_failure(); }
    const bionic_id& bid = it.type->bionic->id;
    if( p.is_mounted() ) {
    return ret_val<bool>::make_failure( _( "You can't install bionics while mounted." ) );
    }
    if( !get_option<bool>( "MANUAL_BIONIC_INSTALLATION" ) && !p.has_trait( trait_DEBUG_BIONICS ) ) {
    return ret_val<bool>::make_failure( _( "You can't self-install bionics." ) );
    } else if( !p.has_trait( trait_DEBUG_BIONICS ) ) {
    if( it.has_fault( fault_bionic_nonsterile ) && !p.has_trait( trait_INFRESIST ) ) {
            return ret_val<bool>::make_failure( _( "This CBM is not sterile, you can't install it." ) );
        } else if( units::energy_max - p.get_max_power_level() < bid->capacity ) {
            return ret_val<bool>::make_failure( _( "Max power capacity already reached" ) );
        }
    }

    if( !bid->has_flag( flag_MULTIINSTALL ) && p.has_bionic( bid ) ) {
    return ret_val<bool>::make_failure( _( "You have already installed this bionic." ) );
    } else if( bid->upgraded_bionic && !p.has_bionic( bid->upgraded_bionic ) ) {
    return ret_val<bool>::make_failure( _( "There is nothing to upgrade." ) );
    } else if( character_funcs::has_upgraded_bionic( p, bid ) ) {
    return ret_val<bool>::make_failure( _( "You have a superior version installed." ) );
    }

    return ret_val<bool>::make_success();
}

std::unique_ptr<iuse_actor> install_bionic_actor::clone() const
{
    return std::make_unique<install_bionic_actor>( *this );
}

void install_bionic_actor::finalize( const itype_id& my_item_type )
{
    if( !my_item_type->bionic ) {
        debugmsg( "Item %s has install_bionic actor, but it's not a bionic.", my_item_type.c_str() );
    }
}

int detach_gunmods_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    auto mods = it.gunmods();

    mods.erase(
        std::remove_if(
            mods.begin(), mods.end(), std::bind( &item::is_irremovable, std::placeholders::_1 ) ),
        mods.end() );

    uilist prompt;
    prompt.text = _( "Remove which modification?" );

    for( size_t i = 0; i != mods.size(); ++i ) { prompt.addentry( i, true, -1, mods[i]->tname() ); }

    prompt.query();

    if( prompt.ret >= 0 ) {
        item* gm = mods[prompt.ret];
        avatar_funcs::gunmod_remove( *p.as_avatar(), it, *gm );
    } else {
        p.add_msg_if_player( _( "Never mind." ) );
    }

    return 0;
}

ret_val<bool> detach_gunmods_actor::can_use(
    const Character& p, const item& it, bool, const tripoint_bub_ms & ) const
{
    const auto mods = it.gunmods();

    if( mods.empty() ) { return ret_val<bool>::make_failure( _( "Doesn't appear to be modded." ) ); }

    const bool no_removables = std::
                               all_of( mods.begin(), mods.end(), std::bind( &item::is_irremovable, std::placeholders::_1 ) );

    if( no_removables ) {
        return ret_val<bool>::make_failure( _( "None of the mods can be removed." ) );
    }

    if( p.is_worn( it ) ) { // Prevent removal of shoulder straps and thereby making the gun
        // un-wearable again.
        return ret_val<bool>::make_failure( _( "Has to be taken off first." ) );
    }

    return ret_val<bool>::make_success();
}

std::unique_ptr<iuse_actor> detach_gunmods_actor::detach_gunmods_actor::clone() const
{
    return std::make_unique<detach_gunmods_actor>( *this );
}

void detach_gunmods_actor::finalize( const itype_id& my_item_type )
{
    if( !my_item_type->gun ) {
        debugmsg( "Item %s has detach_gunmods_actor actor, but it's a gun.", my_item_type.c_str() );
    }
}

std::unique_ptr<iuse_actor> mutagen_actor::clone() const
{
    return std::make_unique<mutagen_actor>( *this );
}

void mutagen_actor::load( const JsonObject& obj )
{
    mutation_category = mutation_category_id( obj.get_string( "mutation_category", "ANY" ) );
    is_weak = obj.get_bool( "is_weak", false );
    is_strong = obj.get_bool( "is_strong", false );
}

int mutagen_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    mutagen_attempt checks =
        mutagen_common_checks( p, it, false, mutagen_technique::consumed_mutagen );

    if( !checks.allowed ) { return checks.charges_used; }

bool no_category = mutation_category == mutation_category_id( "ANY" );
bool balanced = get_option<bool>( "BALANCED_MUTATIONS" );
int accumulated_mutagen = p.get_effect_int( effect_accumulated_mutagen );
if( balanced && !is_strong && is_weak && accumulated_mutagen < 2 && no_category
        && !p.query_yn( _( "Looking at it just makes you tired.  It probably won't work.  Do you "
                           "want to try anyway?" ) ) ) {
        return 0;
    }
    if( is_weak && !one_in( 3 ) && !balanced ) {
    // Nothing! Mutagenic flesh often just fails to work.
    return it.type->charges_to_use();
    }

    if( balanced && no_category ) {
    for( int i = ( is_strong ? 1 : 0 ) + ( is_weak ? 0 : 1 ); i > 0; i-- ) {
            p.add_effect( effect_accumulated_mutagen, 2_days, bodypart_str_id::NULL_ID() );
        }
    }
    const mutation_category_trait& m_category = mutation_category_trait::get_category(
            mutation_category );

    if( p.has_trait( trait_MUT_JUNKIE ) ) {
    p.add_msg_if_player( m_good, _( "You quiver with anticipation…" ) );
        p.add_morale( MORALE_MUTAGEN, 5, 50 );
    }

    p.add_msg_if_player( m_category.mutagen_message() );

    if( one_in( 6 ) ) {
    p.add_msg_player_or_npc(
        m_bad, _( "You suddenly feel dizzy, and collapse to the ground." ),
        _( "<npcname> suddenly collapses to the ground!" ) );
        p.add_effect( effect_downed, 20_turns, bodypart_str_id::NULL_ID(), 0 );
    }

    int mut_count = 1 + ( is_strong ? one_in( 3 ) : 0 );

    for( int i = 0; i < mut_count; i++ ) {
    p.mutate_category( m_category.id );
        p.mod_pain( m_category.mutagen_pain * rng( 1, 5 ) );
    }
    // burn calories directly
    p.mod_stored_nutr( m_category.mutagen_hunger * mut_count );
    p.mod_thirst( m_category.mutagen_thirst * mut_count );
    p.mod_fatigue( m_category.mutagen_fatigue * mut_count );

    return it.type->charges_to_use();
}

std::unique_ptr<iuse_actor> mutagen_iv_actor::clone() const
{
    return std::make_unique<mutagen_iv_actor>( *this );
}

void mutagen_iv_actor::load( const JsonObject& obj )
{
    mutation_category = mutation_category_id( obj.get_string( "mutation_category", "ANY" ) );
    tier = obj.get_int( "tier", 1 ); // fallback of 1 because IV mutagen usually is used for
    // thresholds
}

int mutagen_iv_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    mutagen_attempt checks =
        mutagen_common_checks( p, it, false, mutagen_technique::injected_mutagen );

    if( !checks.allowed ) { return checks.charges_used; }

const mutation_category_trait& m_category = mutation_category_trait::get_category(
        mutation_category );

if( p.has_trait( trait_MUT_JUNKIE ) ) {
    p.add_msg_if_player( m_category.junkie_message() );
    } else {
        p.add_msg_if_player( m_category.iv_message() );
    }

    // try to cross the threshold to be able to get post-threshold mutations this iv.
    test_crossing_threshold( p, m_category, tier );

    // TODO: Remove the "is_player" part, implement NPC screams
    if( p.is_player() && !( p.has_trait( trait_NOPAIN ) ) && m_category.iv_sound ) {
        p.mod_pain( m_category.iv_pain );
        /** @EFFECT_STR increases volume of painful shouting when using IV mutagen */
        sound_event se;
        se.origin = p.bub_pos();
        se.volume = m_category.iv_noise + p.str_cur;
        se.category = sounds::sound_t::alert;
        se.description = m_category.iv_sound_message();
        se.id = m_category.iv_sound_id();
        se.variant = m_category.iv_sound_variant();
        sounds::sound( se );
    }

    int mut_count = m_category.iv_min_mutations;
    for( int i = 0; i < m_category.iv_additional_mutations; ++i ) {
    if( !one_in( m_category.iv_additional_mutations_chance ) ) { ++mut_count; }
    }

    for( int i = 0; i < mut_count; i++ ) {
    p.mutate_category( m_category.id );
        p.mod_pain( m_category.iv_pain * rng( 1, 5 ) );
    }

    p.mod_stored_kcal( -10 * m_category.iv_hunger * mut_count );
    p.mod_thirst( m_category.iv_thirst * mut_count );
    p.mod_fatigue( m_category.iv_fatigue * mut_count );

    if( m_category.id == mutation_category_id( "CHIMERA" ) ) {
    p.add_morale( MORALE_MUTAGEN_CHIMERA, m_category.iv_morale, m_category.iv_morale_max );
    } else if( m_category.id == mutation_category_id( "ELFA" ) ) {
    p.add_morale( MORALE_MUTAGEN_ELF, m_category.iv_morale, m_category.iv_morale_max );
    } else if( m_category.iv_morale > 0 ) {
    p.add_morale( MORALE_MUTAGEN_MUTATION, m_category.iv_morale, m_category.iv_morale_max );
    }

    if( m_category.iv_sleep && !one_in( 3 ) ) {
    p.add_msg_if_player( m_bad, m_category.iv_sleep_message() );
        /** @EFFECT_INT reduces sleep duration when using IV mutagen */
        p.fall_asleep( time_duration::from_turns( m_category.iv_sleep_dur - p.int_cur * 5 ) );
    }

    // try crossing again after getting new in-category mutations.
    test_crossing_threshold( p, m_category, tier );

    return it.type->charges_to_use();
}

std::unique_ptr<iuse_actor> deploy_tent_actor::clone() const
{
    return std::make_unique<deploy_tent_actor>( *this );
}

void deploy_tent_actor::load( const JsonObject& obj )
{
    assign( obj, "radius", radius );
    assign( obj, "wall", wall );
    assign( obj, "floor", floor );
    assign( obj, "floor_center", floor_center );
    assign( obj, "door_opened", door_opened );
    assign( obj, "door_closed", door_closed );
    assign( obj, "broken_type", broken_type );
}

int deploy_tent_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    int diam = 2 * radius + 1;
    if( p.is_mounted() ) {
        p.add_msg_if_player( _( "You cannot do that while mounted." ) );
        return 0;
    }
    const std::optional<tripoint_rel_ms> dir = choose_direction(
            string_format( _( "Put up the %s where (%dx%d clear area)?" ), it.tname(), diam, diam ) );
    if( !dir ) { return 0; }
    const auto direction = *dir;

    map& here = get_map();
    // We place the center of the structure (radius + 1)
    // spaces away from the player.
    // First check there's enough room.
    const tripoint_bub_ms& center =
        p.bub_pos()
        + tripoint_rel_ms( ( radius + 1 ) * direction.x(), ( radius + 1 ) * direction.y(), 0 );
    for( const tripoint_bub_ms& dest : here.points_in_radius( center, radius ) ) {
        if( const auto vp = here.veh_at( dest ) ) {
            add_msg( m_info, _( "The %s is in the way." ), vp->vehicle().name );
            return 0;
        }
        if( const Creature * const c = g->critter_at( dest ) ) {
            add_msg( m_info, _( "%s is in the way." ), c->disp_name( false, true ) );
            return 0;
        }
        if( here.impassable( dest ) || !here.has_flag( "FLAT", dest ) ) {
            add_msg( m_info, _( "The %s in that direction isn't suitable for placing the %s." ),
                     here.name( dest ), it.tname() );
            return 0;
        }
        if( here.has_furn( dest ) ) {
            add_msg( m_info, _( "There is already furniture (%s) there." ), here.furnname( dest ) );
            return 0;
        }
    }
    // Make a square of floor surrounded by wall.
    for( const tripoint_bub_ms& dest : here.points_in_radius( center, radius ) ) {
        here.furn_set( dest, wall );
    }
    for( const tripoint_bub_ms& dest : here.points_in_radius( center, radius - 1 ) ) {
        here.furn_set( dest, floor );
    }
    // Place the center floor and the door.
    if( floor_center ) { here.furn_set( center, *floor_center ); }
    here.furn_set( p.bub_pos() + direction, door_closed );
    add_msg( m_info, _( "You set up the %s on the ground." ), it.tname() );
    add_msg( m_info, _( "Examine the center square to pack it up again." ) );
    return 1;
}

bool deploy_tent_actor::check_intact( const tripoint_bub_ms& center ) const
{
    map& here = get_map();
    for( const tripoint_bub_ms& dest : here.points_in_radius( center, radius ) ) {
        const furn_id fid = here.furn( dest );
        if( dest == center && floor_center ) {
            if( fid != *floor_center ) { return false; }
        } else if( square_dist( dest, center ) < radius ) {
            // So we are inside the tent
            if( fid != floor ) { return false; }
        } else {
            // We are on the border of the tent
            if( fid != wall && fid != door_opened && fid != door_closed ) { return false; }
        }
    }
    return true;
}

void weigh_self_actor::info( const item &, std::vector<iteminfo> &dump ) const
{
    dump.emplace_back( "DESCRIPTION", _( "Use this item to weigh yourself.  Includes everything you "
                       "are wearing." ) );
}

int weigh_self_actor::use( player& p, item &, bool, const tripoint_bub_ms & ) const
{
    if( p.is_mounted() ) {
    p.add_msg_if_player( m_info, _( "You cannot weigh yourself while mounted." ) );
        return 0;
    }
    // this is a weight, either in kgs or in lbs.
    double weight = convert_weight( p.get_weight() );
    if( weight > convert_weight( max_weight ) ) {
    popup( _( "ERROR: Max weight of %.0f %s exceeded" ), convert_weight( max_weight ),
           weight_units() );
    } else {
        popup( "%.0f %s", weight, weight_units() );
    }
    return 0;
}

void weigh_self_actor::load( const JsonObject& jo ) { assign( jo, "max_weight", max_weight ); }

std::unique_ptr<iuse_actor> weigh_self_actor::clone() const
{
    return std::make_unique<weigh_self_actor>( *this );
}

void gps_device_actor::info( const item &, std::vector<iteminfo> &dump ) const
{
    dump.emplace_back(
    "DESCRIPTION",
    string_format( _( "This item uses up (%.2f) additional charges per tile revealed." ),
    additional_charges_per_tile ) );
}

int gps_device_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    float charges_built_up = 1.0;
    const tripoint_abs_omt center = p.abs_omt_pos();

    std::string query =
        string_input_popup().title( _( "Search for location:" ) ).width( 40 ).query_string();

    if( query.size() < 3 ) {
        p.add_msg_if_player( m_info, _( "Please enter at least 3 characters." ) );
        return 0;
    }

    // Exclude natural terrain types. This item should NOT obsolete other items, just be useful for
    // the player. This helps with that philosophy, since to actually properly survey the area you
    // still need to get to high ground.
    static const std::vector<std::string> natural_terrains = {
        "air",         "forest",     "forest_thick", "forest_water",  "field",      "lake_surface",
        "lake_shore",  "swamp",      "stream",       "stream_corner", "stream_end", "river_center",
        "river_shore", "river_bank", "deep_water",   "shallow_water"
    };

    // Build list of matching terrain IDs whose display name matches the query
    std::vector<std::string> matching_ids;
    for( const oter_t &oter : overmap_terrains::get_all() ) {
        // get_name() returns the human‐readable display name
        if( lcmatch( oter.get_name(), query ) ) { matching_ids.push_back( oter.get_mapgen_id() ); }
    }

    // Configure search to look only for those matching IDs
    omt_find_params params{};
    params.search_range = {0, radius};
    params.types.clear();
    for( const auto& id_str : matching_ids ) {
        params.types.emplace_back( id_str, ot_match_type::type );
    }
    for( const std::string& nt : natural_terrains ) {
        params.exclude_types.emplace_back( nt, ot_match_type::type );
    }
    params.existing_only = false;
    params.search_layers = omt_find_above_ground_layer;
    params.explored = false;
    if( it.has_flag( flag_USE_UPS ) ) {
        params.max_results = static_cast<size_t>(
                                 1 + p.charges_of( itype_UPS ) / additional_charges_per_tile );
    } else {
        params.max_results = static_cast<size_t>(
                                 1 + it.ammo_remaining() / additional_charges_per_tile );
    }
    params.popup = make_shared_fast<throbber_popup>( _( "Searching…" ) );

    const auto places = get_overmapbuffer( p.get_dimension() ).find_all( center, params );
    params.popup = nullptr;

    if( places.empty() ) {
        p.add_msg_if_player( m_info, _( "No locations found for \"%s\"." ), query );
        return 1;
    }

    // Group by display name
    std::multimap<std::string, tripoint_abs_omt> grouped;
    std::set<std::string> unique_names;
    for( const auto& pt : places ) {
        const std::string name = get_overmapbuffer( p.get_dimension() ).ter( pt ).obj().get_name();
        grouped.insert( {name, pt} );
        unique_names.insert( name );
        charges_built_up += additional_charges_per_tile;
    }
    if( it.has_flag( flag_USE_UPS ) ) {
        if( !p.has_charges( itype_UPS, charges_built_up ) ) {
            p.add_msg_if_player(
                m_info, _( "Requires %.1f charges, but only %d remaining." ), charges_built_up,
                p.charges_of( itype_UPS ) - 1 );
            return 1;
        }
    } else if( 1 + it.ammo_remaining() < charges_built_up ) {
        p.add_msg_if_player(
            m_info, _( "Requires %.1f charges, but only %d remaining." ), charges_built_up,
            it.ammo_remaining() - 1 );
        return 1;
    }

    // I don't think this will actually ever be called, but we're leaving it here for now
    if( unique_names.empty() ) {
        p.add_msg_if_player( m_info, _( "Nothing new to display." ) );
        return 1;
    }

    p.add_msg_if_player( m_good, _( "You add the GPS results to your map." ) );
    // Device has enough charge and nothing has gone wrong, reveal on overmap the locations!
    for( const auto& pt : places ) { get_overmapbuffer( p.get_dimension() ).reveal( pt, 0 ); }
    uistate.overmap_highlighted_omts.clear();

    // Let the player pick which name to highlight
    const std::vector<std::string> name_list( unique_names.begin(), unique_names.end() );
    uilist ui;
    for( size_t i = 0; i < name_list.size(); ++i ) {
        ui.addentry( i, true, MENU_AUTOASSIGN,
                     string_format( "%s (%d)", name_list[i], grouped.count( name_list[i] ) ) );
    }
    ui.query();
    if( ui.ret < 0 ) { return charges_built_up; }

    const tripoint_abs_omt plr_pos = p.abs_omt_pos();
    auto range = grouped.equal_range( name_list[ui.ret] );
    const int count = std::distance( range.first, range.second );
    if( count == 0 ) { return charges_built_up; }

    // Highlight all matching points
    std::transform(
        range.first, range.second,
        std::inserter( uistate.overmap_highlighted_omts, uistate.overmap_highlighted_omts.end() ),
    []( const auto & e ) { return e.second; } );

    if( count == 1 ) {
        ui::omap::choose_point( range.first->second );
        return charges_built_up;
    }

    // If there are multiple, ask for closest vs random
    ui.reset();
    ui.addentry( 0, true, 'c', _( "Closest" ) );
    ui.addentry( 1, true, 'r', _( "Random" ) );
    ui.query();
    if( ui.ret < 0 ) { return charges_built_up; }

    if( ui.ret == 1 ) {
        auto it = range.first;
        std::advance( it, rng( 0, count - 1 ) );
        ui::omap::choose_point( it->second );
    } else {
        const auto cmp = [&]( const auto & a, const auto & b ) {
            return trig_dist_squared( plr_pos.raw(), a.second.raw() )
                   < trig_dist_squared( plr_pos.raw(), b.second.raw() );
        };
        const auto it = std::min_element( range.first, range.second, cmp );
        ui::omap::choose_point( it->second );
    }

    return charges_built_up;
}

void gps_device_actor::load( const JsonObject& jo )
{
    assign( jo, "radius", radius );
    assign( jo, "additional_charges_per_tile", additional_charges_per_tile );
}

std::unique_ptr<iuse_actor> gps_device_actor::clone() const
{
    return std::make_unique<gps_device_actor>( *this );
}

void sew_advanced_actor::load( const JsonObject& obj )
{
    // Mandatory:
    for( const std::string line : obj.get_array( "materials" ) ) { materials.emplace( line ); }
    for( const std::string line : obj.get_array( "clothing_mods" ) ) {
        clothing_mods.emplace_back( line );
    }

    // TODO: Make skill non-mandatory while still erroring on invalid skill
    const std::string skill_string = obj.get_string( "skill" );
    used_skill = skill_id( skill_string );
    if( !used_skill.is_valid() ) { obj.throw_error( "Invalid skill", "skill" ); }
}

int sew_advanced_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    if( p.is_npc() ) { return 0; }
if( p.is_mounted() ) {
    p.add_msg_if_player( m_info, _( "You cannot do that while mounted." ) );
        return 0;
    }
    if( p.is_underwater() ) {
    p.add_msg_if_player( m_info, _( "You can't do that while underwater." ) );
        return 0;
    }

    if( !character_funcs::can_see_fine_details( p ) ) {
    add_msg( m_info, _( "You can't see to sew!" ) );
        return 0;
    }

    auto filter = [this]( const item & itm ) {
        return itm.is_armor() && !itm.is_firearm() && itm.made_of_any( materials );
    };
    // note: if !p.is_npc() then p is avatar.
    item* loc = game_menus::inv::
                titled_filter_menu( filter, *p.as_avatar(), _( "Enhance which clothing?" ), "", 1 );
    if( !loc ) {
    p.add_msg_if_player( m_info, _( "You do not have that item!" ) );
        return 0;
    }
    item& mod = *loc;
    if( &mod == &it ) {
    p.add_msg_if_player( m_info, _( "This can be used to repair or modify other items, not "
                                    "itself." ) );
        return 0;
    }

    // Gives us an item with the mod added or removed (toggled)
    const auto modded_copy = []( const item & proto, const flag_id & mod_type ) {
        item* mcopy = item::spawn_temporary( proto );
        if( mcopy->has_own_flag( mod_type ) == 0 ) {
            mcopy->set_flag( mod_type );
        } else {
            mcopy->unset_flag( mod_type );
        }

        return mcopy;
    };

    // Cache available materials
    std::map<itype_id, bool> has_enough;
    const int items_needed = mod.volume() / 750_ml + 1;
    const inventory& crafting_inv = p.crafting_inventory();
    // Go through all discovered repair items and see if we have any of them available
for( auto cm : clothing_mods::get_all() ) {
    auto item_string = cm.item_string;

    if( cm.use_base_material ) {
            for( auto& mat : mod.made_of() ) {
                if( materials.contains( mat ) && mat.obj().repaired_with() != itype_id::NULL_ID() ) {
                    item_string = mat.obj().repaired_with();
                    break;
                }
            }
        }

        has_enough[item_string] =
            item::count_by_charges( item_string )
            ? crafting_inv.has_charges( item_string, items_needed )
            : crafting_inv.has_amount( item_string, items_needed );
    }

    int mod_count = 0;
for( auto& cm : clothing_mods::get_all() ) { mod_count += mod.has_own_flag( cm.flag ); }

    // We need extra thread to lose it on bad rolls
    const int thread_needed = mod.volume() / 125_ml + 10;

    std::vector<std::string> valid_mods;
    if( mod.find_armor_data() ) { valid_mods = mod.find_armor_data()->valid_mods; }

    if( mod.has_flag( flag_VARSIZE ) && !mod.has_flag( flag_OVERSIZE ) ) {
    valid_mods.push_back( "resized_large" );
    }
    if( !mod.has_flag( flag_UNDERSIZE ) && mod.has_flag( flag_OVERSIZE ) ) {
    valid_mods.push_back( "resized_small" );
    }

    const auto get_compare_color =
    [&]( const int before, const int after, const bool higher_is_better ) {
        return before == after
               ? c_unset
               : ( ( after > before ) == higher_is_better ? c_light_green : c_red );
    };
    const auto get_volume_compare_color =
    [&]( const units::volume & before, const units::volume & after, const bool higher_is_better ) {
        return before == after
               ? c_unset
               : ( ( after > before ) == higher_is_better ? c_light_green : c_red );
    };
    const auto format_desc_string =
        [&]( const std::string & label, const int before, const int after,
    const bool higher_is_better ) {
        return colorize( string_format( "%s: %d->%d\n", label, before, after ),
                         get_compare_color( before, after, higher_is_better ) );
    };

    uilist tmenu;
    tmenu.text = _( "How do you want to modify it?" );

    int index = 0;
for( auto cm : clothing_mods ) {
    auto obj = cm.obj();
        item& temp_item = *modded_copy( mod, obj.flag );
        temp_item.update_clothing_mod_val();

        bool enab = false;
        std::string prompt;
        // TODO: Fix for UTF-8 strings
        // TODO: find other places where this is used and make a global function for all
        static const auto tolower = []( std::string t ) {
            if( !t.empty() ) { t.front() = std::tolower( t.front() ); }
            return t;
        };
        const bool already_resized =
            mod.has_flag( flag_resized_large ) || mod.has_flag( flag_resized_small );
        auto item_string = obj.item_string;

        if( obj.use_base_material ) {
            for( auto& mat : mod.made_of() ) {
                if( materials.contains( mat ) && mat.obj().repaired_with() != itype_id::NULL_ID() ) {
                    item_string = mat.obj().repaired_with();
                    break;
                }
            }
        }

        if( !mod.has_own_flag( obj.flag ) ) {
            // Mod not already present, check if modification is possible
            if( obj.restricted
                && std::find( valid_mods.begin(), valid_mods.end(), obj.flag.str() )
                == valid_mods.end()
                && std::find( valid_mods.begin(), valid_mods.end(), obj.id.str() )
                == valid_mods.end() ) {
                //~ %1$s: modification desc, %2$s: mod name
                prompt = string_format(
                             _( "Can't %1$s (incompatible with %2$s)" ), tolower( obj.implement_prompt ),
                             mod.tname( 1, false ) );
            } else if( ( obj.flag == flag_resized_large || obj.flag == flag_resized_small )
                       && already_resized ) {
                //~ %1$s: modification desc
                prompt =
                    string_format( _( "Can't %1$s (already resized)" ), tolower( obj.implement_prompt ) );
            } else if( it.ammo_remaining() < thread_needed ) {
                //~ %1$s: modification desc, %2$d: number of charges needed
                prompt = string_format(
                             _( "Can't %1$s (need %2$d charges loaded)" ), tolower( obj.implement_prompt ),
                             thread_needed );
            } else if( !has_enough[item_string] ) {
                //~ %1$s: modification desc, %2$d: number of items needed, %3$s: items needed
                prompt = string_format(
                             _( "Can't %1$s (need %2$d %3$s)" ), tolower( obj.implement_prompt ), items_needed,
                             item::nname( item_string, items_needed ) );
            } else {
                // Modification is possible unless we're wearing it and doing so would make it not
                // fit
                if( p.is_worn( mod ) && !p.can_wear( temp_item ).success() ) {
                    prompt = string_format(
                                 _( "Can't %s while wearing it" ), tolower( obj.implement_prompt ) );
                } else {
                    enab = true;
                    //~ %1$s: modification desc, %2$d: number of items needed, %3$s: items needed,
                    //%4$s: number of charges needed
                    prompt = string_format(
                                 _( "%1$s (%2$d %3$s and %4$d charges)" ), tolower( obj.implement_prompt ),
                                 items_needed, item::nname( item_string, items_needed ), thread_needed );
                }
            }

        } else {
            // Mod already present, give option to destroy, unless we're wearing it and doing so
            // would make it not fit
            if( p.is_worn( mod ) && !p.can_wear( temp_item ).success() ) {
                prompt = string_format( _( "Can't %s while wearing it" ), tolower( obj.destroy_prompt ) );
            } else {
                enab = true;
                prompt = _( obj.destroy_prompt );
            }
        }
        std::string desc;
        desc += format_desc_string( _( "Bash" ), mod.bash_resist(), temp_item.bash_resist(), true );
        desc += format_desc_string( _( "Cut" ), mod.cut_resist(), temp_item.cut_resist(), true );
        desc += format_desc_string(
                    _( "Ballistic" ), mod.bullet_resist(), temp_item.bullet_resist(), true );
        desc += format_desc_string( _( "Acid" ), mod.acid_resist(), temp_item.acid_resist(), true );
        desc += format_desc_string( _( "Fire" ), mod.fire_resist(), temp_item.fire_resist(), true );
        desc += format_desc_string( _( "Warmth" ), mod.get_warmth(), temp_item.get_warmth(), true );
        desc += format_desc_string(
                    _( "Encumbrance" ), mod.get_avg_encumber( p ), temp_item.get_avg_encumber( p ), false );
        auto before = mod.get_storage();
        auto after = temp_item.get_storage();
        desc += colorize(
                    string_format( "%s: %s %s->%s %s\n", _( "Storage" ), format_volume( before ),
                                   volume_units_abbr(), format_volume( after ), volume_units_abbr() ),
                    get_volume_compare_color( before, after, true ) );

        tmenu.addentry_desc( index++, enab, MENU_AUTOASSIGN, prompt, desc );
    }
    tmenu.textwidth = 80;
    tmenu.desc_enabled = true;
    tmenu.query();
    const int choice = tmenu.ret;

    if( choice < 0 || choice >= static_cast<int>( clothing_mods.size() ) ) { return 0; }

    // The mod player picked
    const flag_id& the_mod = clothing_mods[choice].obj().flag;

    // If the picked mod already exists, player wants to destroy it
    if( mod.has_own_flag( the_mod ) ) {
    if( query_yn( _( "Are you sure?  You will not gain any materials back." ) ) ) {
            mod.unset_flag( the_mod );
        }
        mod.update_clothing_mod_val();

        return 0;
    }

    std::vector<item_comp> comps;

    auto item_string = clothing_mods[choice].obj().item_string;

    if( clothing_mods[choice].obj().use_base_material ) {
    for( auto& mat : mod.made_of() ) {
            if( materials.contains( mat ) && mat.obj().repaired_with() != itype_id::NULL_ID() ) {
                item_string = mat.obj().repaired_with();
                break;
            }
        }
    }

    comps.emplace_back( item_string, items_needed );

    // TODO: this may take up to 2 minutes, and so should start an activity instead
    p.moves -= to_moves<int>( 30_seconds * character_funcs::fine_detail_vision_mod( p ) );
    p.practice( used_skill, items_needed * 3 + 3 );
    /** @EFFECT_TAILOR randomly improves clothing modification efforts */
    int rn = dice( 3, 2 + p.get_skill_level( used_skill ) ); // Skill
    /** @EFFECT_DEX randomly improves clothing modification efforts */
    rn += rng( 0, p.dex_cur / 2 ); // Dexterity
    /** @EFFECT_PER randomly improves clothing modification efforts */
    rn += rng( 0, p.per_cur / 2 ); // Perception
    rn -= mod_count * 10;        // Other mods

    if( rn <= 8 ) {
    const std::string startdurability = mod.durability_indicator( true );
        const auto destroyed = mod.inc_damage();
        const std::string resultdurability = mod.durability_indicator( true );
        p.add_msg_if_player(
            m_bad, _( "You damage your %s trying to modify it!  ( %s-> %s)" ), mod.tname( 1, false ),
            startdurability, resultdurability );
        if( destroyed ) {
            p.add_msg_if_player( m_bad, _( "You destroy it!" ) );
            p.i_rem_keep_contents( p.get_item_position( &mod ) );
        }
        return thread_needed / 2;
    } else if( rn <= 10 ) {
    p.add_msg_if_player( m_bad, _( "You fail to modify the clothing, and you waste charges and "
                                   "materials." ) );
        p.consume_items( comps, 1, is_crafting_component );
        return thread_needed;
    } else if( rn <= 14 ) {
    p.add_msg_if_player(
        m_mixed, _( "You modify your %s, but waste a lot of charges." ), mod.tname() );
        p.consume_items( comps, 1, is_crafting_component );
        mod.set_flag( the_mod );
        mod.update_clothing_mod_val();
        return thread_needed;
    }

    p.add_msg_if_player( m_good, _( "You modify your %s!" ), mod.tname() );
    mod.set_flag( the_mod );
    mod.update_clothing_mod_val();
    p.consume_items( comps, 1, is_crafting_component );
    return thread_needed / 2;
}

std::unique_ptr<iuse_actor> sew_advanced_actor::clone() const
{
    return std::make_unique<sew_advanced_actor>( *this );
}
