#include "veh_interact.h"

#include "activity_actor_definitions.h"
#include "activity_handlers.h"
#include "avatar.h"
#include "avatar_functions.h"
#include "calendar.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_functions.h"
#include "character_id.h"
#include "debug.h"
#include "enums.h"
#include "faction.h"
#include "fault.h"
#include "game.h"
#include "game_constants.h"
#include "handle_liquid.h"
#include "item.h"
#include "item_contents.h"
#include "itype.h"
#include "map.h"
#include "map_selector.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmapbuffer.h"
#include "player.h"
#include "player_activity.h"
#include "point.h"
#include "requirements.h"
#include "rml_util.h"
#include "skill.h"
#include "string_formatter.h"
#include "string_id.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "tileray.h"
#include "translations.h"
#include "ui.h"
#include "ui_manager.h"
#include "units.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "veh_type.h"
#include "veh_utils.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "vehicle_preview.h"
#include "vehicle_selector.h"
#include "vpart_position.h"
#include "vpart_range.h"
#include "wheel_dimensions.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <utility>





static const trait_id trait_DEBUG_HS( "DEBUG_HS" );
auto spawn_debug_install_base( const vpart_info& vpinfo ) -> detached_ptr<item>;

void veh_interact::complete_vehicle( Character& who )
{
    if( who.activity->values.size() < 6 ) {
        debugmsg( "Invalid activity ACT_VEHICLE values:%d", who.activity->values.size() );
        return;
    }
    const bool legacy = who.activity->values.size() == 8;

    map& here = get_map();
    tripoint_abs_ms abs_part_loc;
    if( legacy ) {
        abs_part_loc = tripoint_abs_ms(
                           who.activity->values[0], who.activity->values[1], who.activity->values[7] );
    } else {
        abs_part_loc = tripoint_abs_ms(
                           who.activity->values[0], who.activity->values[1], who.activity->values[2] );
    }
    optional_vpart_position vp = here.veh_at( abs_part_loc );
    if( !vp ) {
        // so the vehicle could have lost some of its parts from other NPCS works during this
        // player/NPCs activity. check the vehicle points that were stored at beginning of activity.
        if( !who.activity->coord_set.empty() ) {
            for( const auto pt : who.activity->coord_set ) {
                vp = here.veh_at( here.abs_to_bub( pt ) );
                if( vp ) { break; }
            }
        }
        // check again, to see if it really is a case of vehicle gone missing.
        if( !vp ) {
            debugmsg( "Activity ACT_VEHICLE: vehicle not found" );
            return;
        }
    }
    vehicle* const veh = &vp->vehicle();

    const vpart_id part_id( who.activity->str_values[0] );
    int vehicle_part = who.activity->values[6];
    tripoint_mnt_veh cursor_pos;
    if( legacy ) {
        cursor_pos = tripoint_mnt_veh( -who.activity->values[4], -who.activity->values[5], 0 );
    } else {
        cursor_pos = tripoint_mnt_veh(
                         who.activity->values[3], who.activity->values[4], who.activity->values[5] );
    }

    const vpart_info& vpinfo = part_id.obj();

    // cmd = Install Repair reFill remOve Siphon Unload reName relAbel
    switch( static_cast<char>( who.activity->index ) ) {
        case 'i': {
            const inventory& inv = who.crafting_inventory();

            const auto reqs = vpinfo.install_requirements();
            const auto using_debug_hammerspace = who.has_trait( trait_DEBUG_HS );
            if( !using_debug_hammerspace
                && !reqs.can_make_with_inventory( inv, is_crafting_component ) ) {
                add_msg( m_info, _( "You don't meet the requirements to install the %s." ),
                         vpinfo.name() );
                break;
            }
            detached_ptr<item> base;
            if( using_debug_hammerspace ) {
                base = spawn_debug_install_base( vpinfo );
            } else {
                // Consume items, extracting the specific base item for the installed part.
                for( const auto& e : reqs.get_components() ) {
                    for( auto& obj : who.consume_items( e, 1, is_crafting_component ) ) {
                        if( obj->typeId() == vpinfo.item ) { base = std::move( obj ); }
                    }
                }

                for( const auto& e : reqs.get_tools() ) { who.consume_tools( e ); }
            }
            if( !base ) {
                if( !using_debug_hammerspace ) {
                    add_msg( m_info, _( "Could not find base part in requirements for %s." ),
                             vpinfo.name() );
                    break;
                } else {
                    base = spawn_debug_install_base( vpinfo );
                }
            }

            who.invalidate_crafting_inventory();

            int partnum =
                !base->is_null() ? veh->install_part( cursor_pos, part_id, std::move( base ) ) : -1;
            if( partnum < 0 ) {
                debugmsg( "complete_vehicle install part fails dx=%d dy=%d dz=%d id=%s",
                          cursor_pos.x(), cursor_pos.y(), cursor_pos.z(), part_id.c_str() );
                break;
            }

            // Need map-relative coordinates to compare to output of look_around.
            // Need to call coord_translate() directly since it's a new part.
            const auto vehp = veh->mount_to_bubble( cursor_pos );

            if( vpinfo.has_flag( VPFLAG_CONE_LIGHT ) || vpinfo.has_flag( VPFLAG_WIDE_CONE_LIGHT )
                || vpinfo.has_flag( VPFLAG_HALF_CIRCLE_LIGHT ) ) {

                // Stash offset and set it to the location of the part so look_around will start
                // there.
                tripoint_rel_ms old_view_offset;

                auto* u = who.as_avatar();
                if( u ) {
                    old_view_offset = u->view_offset;
                    u->view_offset = vehp - who.bub_pos();
                }

                tripoint_rel_ms delta;
                do {
                    popup( _( "Press space, choose a facing direction for the new %s and confirm "
                              "with enter." ),
                           vpinfo.name() );
                    const std::optional<tripoint_bub_ms> chosen = g->look_around();
                    if( !chosen ) { continue; }
                    delta = *chosen - vehp;
                    // atan2 only gives reasonable values when delta is not all zero
                } while( delta == tripoint_rel_ms::zero() );

                // Restore previous view offsets.
                if( u ) { u->view_offset = old_view_offset; }

                units::angle dir = normalize( atan2( delta.raw().xy() ) - veh->face.dir() );

                veh->part( partnum ).direction = dir;
            }

            // TODO: allow boarding for non-players as well.
            player* const pl = g->critter_at<player>( vehp );
            if( vpinfo.has_flag( VPFLAG_BOARDABLE ) && pl ) { here.board_vehicle( vehp, pl ); }

            who.add_msg_if_player(
                m_good, _( "You install a %1$s into the %2$s." ), veh->part( partnum ).name(),
                veh->name );

            for( const auto& sk : vpinfo.install_skills ) {
                who.practice( sk.first, veh_utils::calc_xp_gain( vpinfo, sk.first, who ) );
            }
            here.add_vehicle_to_cache( veh );
            break;
        }

        case 'r': {
            veh_utils::repair_part( *veh, veh->part( vehicle_part ), who );
            break;
        }

        case 'f': {
            if( who.activity->targets.empty() || !who.activity->targets.front() ) {
                debugmsg( "Activity ACT_VEHICLE: missing refill source" );
                break;
            }

            item* src = &*who.activity->targets.front();
            struct vehicle_part& pt = veh->part( vehicle_part );
            if( pt.is_tank() && src->is_container() && !src->contents.empty() ) {
                src->contents.front().attempt_detach( [&pt]( detached_ptr<item>&& it ) {
                    return pt.base->fill_with( std::move( it ) );
                } );
                src->on_contents_changed();

                if( pt.ammo_remaining() != pt.ammo_capacity() ) {
                    //~ 1$s vehicle name, 2$s tank name
                    who.add_msg_if_player(
                        m_good, _( "You refill the %1$s's %2$s." ), veh->name, pt.name() );
                } else {
                    //~ 1$s vehicle name, 2$s tank name
                    who.add_msg_if_player(
                        m_good, _( "You completely refill the %1$s's %2$s." ), veh->name, pt.name() );
                }

                if( !src->contents.empty() ) {
                    who.add_msg_if_player( m_good, _( "There's some left over!" ) );
                }

            } else if( pt.is_fuel_store() ) {
                auto qty = src->charges;
                pt.base->reload( who, *src, qty );

                //~ 1$s vehicle name, 2$s reactor name
                who.add_msg_if_player(
                    m_good, _( "You refuel the %1$s's %2$s." ), veh->name, pt.name() );

            } else {
                debugmsg( "vehicle part is not reloadable" );
                break;
            }

            veh->invalidate_mass();
            break;
        }

        case 'o': {
            const inventory& inv = who.crafting_inventory();
            if( vehicle_part >= veh->part_count() ) {
                vehicle_part = veh->get_next_shifted_index( vehicle_part, who );
                if( vehicle_part == -1 ) {
                    who.add_msg_if_player(
                        m_info, _( "The %s has already been removed by someone else." ),
                        vpinfo.name() );
                    return;
                }
            }
            const auto reqs = vpinfo.removal_requirements();
            if( !reqs.can_make_with_inventory( inv, is_crafting_component ) ) {
                add_msg( m_info, _( "You don't meet the requirements to remove the %s." ),
                         vpinfo.name() );
                break;
            }
            for( const auto& e : reqs.get_components() ) {
                who.consume_items( e, 1, is_crafting_component );
            }
            for( const auto& e : reqs.get_tools() ) { who.consume_tools( e ); }

            who.invalidate_crafting_inventory();

            // This will be a list of all the items which arise from this removal.
            std::vector<detached_ptr<item>> resulting_items;

            // First we get all the contents of the part
            vehicle_stack contents = veh->get_items( vehicle_part );
            for( detached_ptr<item> &it : contents.clear() ) {
                resulting_items.push_back( std::move( it ) );
            }

            // Power cables must remove parts from the target vehicle, too.
            if( veh->part_flag( vehicle_part, "POWER_TRANSFER" ) ) {
                veh->remove_remote_part( vehicle_part );
            }
            if( veh->is_towing() || veh->is_towed() ) {
                std::cout << "vehicle is towing/towed" << '\n';
                vehicle* other_veh =
                    veh->is_towing() ? veh->tow_data.get_towed() : veh->tow_data.get_towed_by();
                if( other_veh ) {
                    std::cout << "other veh exists" << '\n';
                    other_veh->remove_part(
                        other_veh->part_with_feature( other_veh->get_tow_part(), "TOW_CABLE", true ) );
                    other_veh->tow_data.clear_towing();
                }
                veh->tow_data.clear_towing();
            }
            bool broken = veh->part( vehicle_part ).is_broken();

            if( broken ) {
                who.add_msg_if_player(
                    _( "You remove the broken %1$s from the %2$s." ), veh->part( vehicle_part ).name(),
                    veh->name );
            } else {
                who.add_msg_if_player(
                    _( "You remove the %1$s from the %2$s." ), veh->part( vehicle_part ).name(),
                    veh->name );
            }

            if( !broken ) {
                resulting_items.push_back( veh->part( vehicle_part ).properties_to_item() );
                for( const auto& sk : vpinfo.install_skills ) {
                    // removal is half as educational as installation
                    who.practice( sk.first, veh_utils::calc_xp_gain( vpinfo, sk.first, who ) / 2 );
                }

            } else {
                auto pieces = veh->part( vehicle_part ).pieces_for_broken_part();
                resulting_items
                .insert( resulting_items.end(), std::make_move_iterator( pieces.begin() ),
                         std::make_move_iterator( pieces.end() ) );
            }

            if( veh->part_count() < 2 ) {
                who.add_msg_if_player( _( "You completely dismantle the %s." ), veh->name );
                who.activity->set_to_null();
                here.destroy_vehicle( veh );
                here.reset_vehicle_cache();
            } else {
                auto mount = veh->part( vehicle_part ).mount;
                const auto& part_pos = veh->bub_part_location( vehicle_part );
                veh->remove_part( vehicle_part );
                // part_removal_cleanup calls refresh, so parts_at_relative is valid
                veh->part_removal_cleanup();
                if( veh->parts_at_relative( mount, true ).empty() ) {
                    get_map().clear_vehicle_point_from_cache( veh, part_pos );
                }
            }
            // This will be part of an NPC "job" where they need to clean up the acitivty items
            // afterwards
            if( who.is_npc() ) {
                for( detached_ptr<item> &it : resulting_items ) {
                    it->set_var( "activity_var", who.name );
                }
            }
            // Finally, put all the results somewhere (we wanted to wait until this
            // point because we don't want to put them back into the vehicle part
            // that just got removed).
            put_into_vehicle_or_drop( who, item_drop_reason::deliberate, resulting_items );
            break;
        }
    }
    who.invalidate_crafting_inventory();
}
