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


std::unique_ptr<iuse_actor> iuse_transform::clone() const
{
    return std::make_unique<iuse_transform>( *this );
}

void iuse_transform::load( const JsonObject& obj )
{
    obj.read( "target", target, true );

    obj.read( "msg", msg_transform );
    obj.read( "container", container );
    if( obj.has_member( "target_charges" ) && obj.has_member( "rand_target_charges" ) ) {
        obj.throw_error( "Transform actor specified both fixed and random target charges", "target_"
                         "charge"
                         "s" );
    }
    obj.read( "target_charges", ammo_qty );
    if( obj.has_array( "rand_target_charges" ) ) {
        for( const int charge : obj.get_array( "rand_target_charges" ) ) {
            random_ammo_qty.push_back( charge );
        }
        if( random_ammo_qty.size() < 2 ) {
            obj.throw_error( "You must specify two or more values to choose between", "rand_target_"
                             "charges" );
        }
    }
    obj.read( "target_ammo", ammo_type );

    obj.read( "countdown", countdown );

    if( !ammo_type.is_empty() && !container.is_empty() ) {
        obj.throw_error( "Transform actor specified both ammo type and container type", "target_"
                         "ammo" );
    }

    obj.read( "active", active );

    obj.read( "moves", moves );
    if( moves < 0 ) { obj.throw_error( "transform actor specified negative moves", "moves" ); }

    obj.read( "need_fire", need_fire );
    need_fire = std::max( need_fire, 0 );
    if( !obj.read( "need_charges_msg", need_charges_msg ) ) {
        need_charges_msg = to_translation( "The %s is empty!" );
    }

    obj.read( "need_charges", need_charges );
    need_charges = std::max( need_charges, 0 );
    if( !obj.read( "need_fire_msg", need_fire_msg ) ) {
        need_fire_msg = to_translation( "You need a source of fire!" );
    }
    obj.read( "transform_charges", transform_charges );

    obj.read( "need_worn", need_worn );
    obj.read( "need_wielding", need_wielding );
    obj.read( "need_dry", need_dry );

    obj.read( "qualities_needed", qualities_needed );

    obj.read( "menu_text", menu_text );
}

int iuse_transform::use( player& p, item& it, bool t, const tripoint_bub_ms& pos ) const
{
    if( t ) {
    return 0; // invoked from active item processing, do nothing.
}

const bool possess =
    p.has_item( it )
    || ( it.has_flag( flag_ALLOWS_REMOTE_USE ) && square_dist( p.bub_pos(), pos ) == 1 );

    if( possess && need_worn && !p.is_worn( it ) ) {
        p.add_msg_if_player(
            m_info, _( "You need to wear the %1$s before activating it." ), it.tname() );
        return 0;
    }
    if( possess && need_wielding && !p.is_wielding( it ) ) {
        p.add_msg_if_player(
            m_info, _( "You need to wield the %1$s before activating it." ), it.tname() );
        return 0;
    }
    // No charge consumption at this point, there are still points of failure later.
    if( need_charges || transform_charges ) {
    if( it.has_flag( flag_POWERARMOR_MOD ) && character_funcs::can_interface_armor( p ) ) {
            if( possess ) {
                const int bio_power = units::to_kilojoule( p.get_power_level() );
                if( bio_power < need_charges || bio_power < transform_charges ) {
                    p.add_msg_if_player( m_info, need_charges_msg, it.tname() );
                    return 0;
                }
            } else {
                return 0;
            }
        } else {
            const int item_charges = it.units_remaining( p );
            if( item_charges < need_charges || item_charges < transform_charges ) {
                p.add_msg_if_player( m_info, need_charges_msg, it.tname() );
                return 0;
            }
        }
    }


    if( need_fire && possess ) {
    if( !p.use_charges_if_avail( itype_fire, need_fire ) ) {
            p.add_msg_if_player( m_info, need_fire_msg, it.tname() );
            return 0;
        }
        if( p.is_underwater() ) {
            p.add_msg_if_player( m_info, _( "You can't do that while underwater" ) );
            return 0;
        }
    }

    // All checks complete the damn thing can finally transform
    // Consume charges if necessary at this point.
    if( transform_charges ) { p.consume_charges( it, transform_charges ); }

    if( possess && !msg_transform.empty() ) {
        p.add_msg_if_player( m_neutral, msg_transform, it.tname() );
    }
    // We want this separate and not if/else because the preceding statement will always return true
    // if a transform message is defined.
    if( p.is_npc() && get_player_character().sees( p ) ) {
        if( !it.has_flag( flag_COMBAT_NPC_ON ) ) {
            add_msg( m_info, _( "%s activates their %s." ), p.disp_name(), it.display_name() );
        } else {
            add_msg( m_info, _( "%s deactivates their %s." ), p.disp_name(), it.display_name() );
        }
    }

    if( possess ) { p.moves -= moves; }

// Update Luminosity as object is "removed"
get_map().update_lum( it, false );

if( p.is_worn( it ) ) { p.on_item_takeoff( it ); }
    if( container.is_empty() ) {
        it.convert( target );
        if( ammo_qty >= 0 || !random_ammo_qty.empty() ) {
            int qty;
            if( !random_ammo_qty.empty() ) {
                const auto index = rng( 1, random_ammo_qty.size() - 1 );
                qty = rng( random_ammo_qty[index - 1], random_ammo_qty[index] );
            } else {
                qty = ammo_qty;
            }
            if( !ammo_type.is_empty() ) {
                it.ammo_set( ammo_type, qty );
            } else if( !it.ammo_current().is_null() ) {
                it.ammo_set( it.ammo_current(), qty );
            } else {
                it.set_charges( qty );
            }
            // If we're setting target charges then check for integral mods too.
            if( it.type->gun ) {
                for( const itype_id& mod : it.type->gun->built_in_mods ) {
                    detached_ptr<item> content = item::spawn( mod, calendar::turn, qty );
                    content->set_flag( flag_IRREMOVABLE );
                    it.put_in( std::move( content ) );
                }
                for( const itype_id& mod : it.type->gun->default_mods ) {
                    it.put_in( item::spawn( mod, calendar::turn, qty ) );
                }
            }
        }
    } else {
        it.convert( container );
        it.put_in( item::spawn( target, calendar::turn, std::max( ammo_qty, 1 ) ) );
    }
    if( p.is_worn( it ) ) {
        p.reset_encumbrance();
        // This is most likely wrong: it doubles temperature shift for the turn!
        p.update_bodytemp( get_map(), get_weather() );
        p.on_item_wear( it );
    }
    p.inv_update_invlet_cache_with_item( it );
    // Update luminosity as object is "added"
    get_map().update_lum( it, true );
    ( active || countdown ) ? it.activate() : it.deactivate();
    it.set_counter( countdown > 0 ? countdown : it.type->countdown_interval );
    // Check for gaining or losing night vision, eye encumbrance effects, clairvoyance from
    // transforming relics, etc.
    p.recalc_sight_limits();
    get_map().invalidate_lightmap_caches();

    return 0;
}

ret_val<bool> iuse_transform::can_use(
    const Character& p, const item &, bool, const tripoint_bub_ms & ) const
{
    if( need_dry && p.is_underwater() ) {
    return ret_val<bool>::make_failure( _( "This item cannot be used while underwater." ) );
    }
    if( qualities_needed.empty() ) { return ret_val<bool>::make_success(); }

    std::map<quality_id, int> unmet_reqs;
    inventory inv;
    inv.form_from_map( p.bub_pos(), 1, &p, true, true );
for( const auto& quality : qualities_needed ) {
    if( !p.has_quality( quality.first, quality.second )
            && !inv.has_quality( quality.first, quality.second ) ) {
            unmet_reqs.insert( quality );
        }
    }
    if( unmet_reqs.empty() ) { return ret_val<bool>::make_success(); }
    std::string unmet_reqs_string = enumerate_as_string(
    unmet_reqs.begin(), unmet_reqs.end(), [&]( const std::pair<quality_id, int> &unmet_req ) {
        return string_format( "%s %d", unmet_req.first.obj().name, unmet_req.second );
    } );
    return ret_val<bool>::make_failure(
               vgettext( "You need a tool with %s.", "You need tools with %s.", unmet_reqs.size() ),
               unmet_reqs_string );
}

std::string iuse_transform::get_name() const
{
    if( !menu_text.empty() ) { return menu_text.translated(); }
    return iuse_actor::get_name();
}

void iuse_transform::finalize( const itype_id & )
{
    if( !target.is_valid() ) { debugmsg( "Invalid transform target: %s", target.c_str() ); }

    if( !container.is_empty() ) {
        if( !container.is_valid() ) {
            debugmsg( "Invalid transform container: %s", container.c_str() );
        }

        item* dummy = item::spawn_temporary( target );
        if( ammo_qty > 1 && !dummy->count_by_charges() ) {
            debugmsg( "Transform target with container must be an item with charges, got "
                      "non-charged: %s",
                      target.c_str() );
        }
    }
}

void iuse_transform::info( const item& it, std::vector<iteminfo> &dump ) const
{
    item& dummy = *item::spawn_temporary( target, calendar::turn, std::max( ammo_qty, 1 ) );
    if( it.has_flag( flag_FIT ) ) { dummy.set_flag( flag_FIT ); }
    dump.emplace_back( "TOOL", string_format( _( "<bold>Turns into</bold>: %s" ), dummy.tname() ) );
    if( countdown > 0 ) { dump.emplace_back( "TOOL", _( "Countdown: " ), countdown ); }

    const auto* explosion_use = dummy.get_use( "explosion" );
    if( explosion_use != nullptr ) { explosion_use->get_actor_ptr()->info( it, dump ); }
}

std::unique_ptr<iuse_actor> unpack_actor::clone() const
{
    return std::make_unique<unpack_actor>( *this );
}

void unpack_actor::load( const JsonObject& obj )
{
    obj.read( "group", unpack_group );
    obj.read( "items_fit", items_fit );
}

int unpack_actor::use( player& p, item& it, bool, const tripoint_bub_ms & ) const
{
    std::vector<detached_ptr<item>> items = item_group::items_from( unpack_group, calendar::turn );
    item* last_armor = &null_item_reference();

    p.add_msg_if_player( _( "You unpack the %s." ), it.tname() );

    map& here = get_map();
    for( detached_ptr<item> &content : items ) {
        if( content->is_armor() ) {
            if( items_fit ) {
                content->set_flag( flag_FIT );
            } else if( content->typeId() == last_armor->typeId() ) {
                if( last_armor->has_flag( flag_FIT ) ) {
                    content->set_flag( flag_FIT );
                } else if( !last_armor->has_flag( flag_FIT ) ) {
                    content->unset_flag( flag_FIT );
                }
            }
            last_armor = &*content;
        }


        here.add_item_or_charges( p.bub_pos(), std::move( content ) );
    }

    it.detach();

    return 0;
}

void unpack_actor::info( const item &, std::vector<iteminfo> &dump ) const
{
    dump.emplace_back( "DESCRIPTION", _( "This item could be unpacked to receive something." ) );
}

std::unique_ptr<iuse_actor> countdown_actor::clone() const
{
    return std::make_unique<countdown_actor>( *this );
}

void countdown_actor::load( const JsonObject& obj )
{
    obj.read( "name", name );
    obj.read( "interval", interval );
    obj.read( "message", message );
}

int countdown_actor::use( player& p, item& it, bool t, const tripoint_bub_ms& pos ) const
{
    if( t ) { return 0; }

if( it.is_active() ) { return 0; }

    if( p.sees( pos ) && !message.empty() ) { p.add_msg_if_player( m_neutral, _( message ), it.tname() ); }

    it.activate();
    it.set_counter( interval > 0 ? interval : it.type->countdown_interval );
    return 0;
}

ret_val<bool> countdown_actor::can_use(
    const Character &, const item& it, bool, const tripoint_bub_ms & ) const
{
    if( it.is_active() ) { return ret_val<bool>::make_failure( _( "It's already been triggered." ) ); }

    return ret_val<bool>::make_success();
}

std::string countdown_actor::get_name() const
{
    if( !name.empty() ) { return name; }
return iuse_actor::get_name();
}

void countdown_actor::info( const item& it, std::vector<iteminfo> &dump ) const
{
    dump.emplace_back(
    "TOOL", _( "Countdown: " ), interval > 0 ? interval : it.type->countdown_interval );
    const auto countdown_actor = it.type->countdown_action.get_actor_ptr();
    if( countdown_actor != nullptr ) { countdown_actor->info( it, dump ); }
}

std::unique_ptr<iuse_actor> ammobelt_actor::clone() const
{
    return std::make_unique<ammobelt_actor>( *this );
}

void ammobelt_actor::load( const JsonObject& obj ) { belt = itype_id( obj.get_string( "belt" ) ); }

void ammobelt_actor::info( const item &, std::vector<iteminfo> &dump ) const
{
    dump.emplace_back( "AMMO", string_format( _( "Can be used to assemble: %s" ),
    item::nname( belt ) ) );
}

int ammobelt_actor::use( player& p, item &, bool, const tripoint_bub_ms & ) const
{
    detached_ptr<item> mag = item::spawn( belt );
    mag->ammo_unset();

    if( !p.can_reload( *mag ) ) {
        p.add_msg_if_player( _( "Insufficient ammunition to assemble %s" ), mag->tname() );
        return 0;
    }

    item_reload_option opt = character_funcs::select_ammo( p, *mag, true );
    if( opt ) {
        p.assign_activity( std::make_unique<player_activity>(
                               std::make_unique<reload_activity_actor>(
                                   safe_reference<item>( *mag ), safe_reference<item>( *opt.ammo ), opt.qty() ) ) );
        p.i_add( std::move( mag ) );
    }

    return 0;
}

