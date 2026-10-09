
#include "coop_world_event_interpreter.h"

#include "calendar.h"
#include "field.h"
#include "field_type.h"
#include "json.h"
#include "map.h"
#include "type_id.h"

#include <algorithm>

auto coop_world_event_interpreter::is_replicated( coop_event_type type ) -> bool
{
    using evt = coop_event_type;
    switch( type ) {
        case evt::terrain_changed:
        case evt::furniture_changed:
        case evt::field_created:
        case evt::field_changed:
        case evt::field_expired:
            return true;
        default:
            return false;
    }
}

auto coop_world_event_interpreter::field_intensity_carrier( const coop_world_event& ev ) -> int
{
    // Field events carry their intensity in `creature_id`: the new intensity for
    // created/changed, the pre-expiry intensity for expired.  Reading it through
    // this one accessor keeps the carrier in a single place.
    return ev.creature_id;
}

auto coop_world_event_interpreter::apply( map& target, const coop_world_event& ev )
-> coop_recorded_event
{
    coop_recorded_event recorded;
    recorded.type = ev.type;
    recorded.pos = ev.pos;

    const tripoint_bub_ms bpos = abs_to_map_local( target, ev.pos );

    switch( ev.type ) {
        case coop_event_type::terrain_changed: {
            const ter_id ter{ ev.value };
            recorded.terrain_or_furniture = ev.value;
            recorded.old_terrain_or_furniture = target.ter( bpos ).to_i();
            if( ter ) { target.ter_set( bpos, ter ); }
            break;
        }
        case coop_event_type::furniture_changed: {
            recorded.terrain_or_furniture = ev.value;
            recorded.old_terrain_or_furniture = target.furn( bpos ).to_i();
            target.furn_set( bpos, furn_id{ ev.value } );
            break;
        }
        case coop_event_type::field_created: {
            const field_type_id ftype{ ev.value };
            // A creation implies no field of this type was present beforehand.
            recorded.field = ev.value;
            recorded.old_intensity = 0;
            // A carrier of 0 means the sender gave no intensity (an older build),
            // so the field is created at the lowest intensity.
            recorded.new_intensity = std::max( field_intensity_carrier( ev ), 1 );
            if( ftype ) {
                target.add_field( bpos, ftype, recorded.new_intensity, 0_turns );
            }
            break;
        }
        case coop_event_type::field_changed: {
            const field_type_id ftype{ ev.value };
            const int new_intensity = field_intensity_carrier( ev );
            recorded.field = ev.value;
            recorded.new_intensity = new_intensity;
            if( ftype && new_intensity > 0 ) {
                field_entry* fe = target.get_field( bpos ).find_field( ftype );
                if( fe ) {
                    recorded.old_intensity = fe->get_field_intensity();
                    fe->set_field_intensity( new_intensity );
                }
            }
            break;
        }
        case coop_event_type::field_expired: {
            // The pre-expiry intensity travels in the carrier, so a rollback
            // restores the field exactly.  0 (older sender) falls back to 1 in invert.
            recorded.field = ev.value;
            recorded.old_intensity = field_intensity_carrier( ev );
            target.remove_field( bpos, field_type_id{ ev.value } );
            break;
        }
        default:
            // Not replicated: nothing to apply, nothing worth recording.
            break;
    }

    return recorded;
}

auto coop_world_event_interpreter::invert( const coop_recorded_event& recorded )
-> coop_inverted_event
{
    coop_inverted_event inv;
    inv.pos = recorded.pos;

    switch( recorded.type ) {
        case coop_event_type::terrain_changed:
            inv.op = coop_inverse_op::set_terrain;
            inv.terrain_or_furniture = recorded.old_terrain_or_furniture;
            break;
        case coop_event_type::furniture_changed:
            inv.op = coop_inverse_op::set_furniture;
            inv.terrain_or_furniture = recorded.old_terrain_or_furniture;
            break;
        case coop_event_type::field_created:
            // Undoing a creation removes the field; no type flip, so the inverse
            // of the inverse is impossible by construction (defect D1).
            inv.op = coop_inverse_op::remove_field;
            inv.field = recorded.field;
            break;
        case coop_event_type::field_expired:
            // Restores the carried pre-expiry intensity; a sender that carried
            // none (older build) leaves 0, which restores at the lowest intensity.
            inv.op = coop_inverse_op::add_field;
            inv.field = recorded.field;
            inv.intensity = recorded.old_intensity > 0 ? recorded.old_intensity : 1;
            break;
        case coop_event_type::field_changed:
            inv.op = coop_inverse_op::set_field_intensity;
            inv.field = recorded.field;
            inv.intensity = recorded.old_intensity;
            break;
        default:
            break;
    }

    return inv;
}

auto coop_world_event_interpreter::apply_inverse( map& target, const coop_inverted_event& inv )
-> void
{
    const tripoint_bub_ms bpos = abs_to_map_local( target, inv.pos );

    switch( inv.op ) {
        case coop_inverse_op::none:
            break;
        case coop_inverse_op::set_terrain:
            target.ter_set( bpos, ter_id{ inv.terrain_or_furniture } );
            break;
        case coop_inverse_op::set_furniture:
            target.furn_set( bpos, furn_id{ inv.terrain_or_furniture } );
            break;
        case coop_inverse_op::remove_field:
            target.remove_field( bpos, field_type_id{ inv.field } );
            break;
        case coop_inverse_op::add_field:
            if( field_type_id{ inv.field } ) {
                target.add_field( bpos, field_type_id{ inv.field }, inv.intensity, 0_turns );
            }
            break;
        case coop_inverse_op::set_field_intensity:
            if( field_type_id{ inv.field } ) {
                field_entry* fe = target.get_field( bpos ).find_field(
                                      field_type_id{ inv.field } );
                if( fe ) { fe->set_field_intensity( inv.intensity ); }
            }
            break;
    }
}

auto coop_world_event_interpreter::write_events(
    JsonOut& out, const std::vector<coop_world_event> &events ) -> void
{
    out.member( "events" );
    out.start_array();
    for( const coop_world_event& ev : events ) {
        out.start_object();
        out.member( "ev", static_cast<int>( ev.type ) );
        out.member( "x", ev.pos.x() );
        out.member( "y", ev.pos.y() );
        out.member( "z", ev.pos.z() );
        out.member( "v", ev.value );
        if( ev.creature_id != 0 ) { out.member( "cid", ev.creature_id ); }
        out.end_object();
    }
    out.end_array();
}

auto coop_world_event_interpreter::read_event( JsonIn& in ) -> coop_world_event
{
    coop_world_event ev;
    int ev_type = 0;

    in.start_object();
    while( !in.end_object() ) {
        const std::string key = in.get_member_name();
        if( key == "ev" ) {
            ev_type = in.get_int();
        } else if( key == "x" ) {
            ev.pos.x() = in.get_int();
        } else if( key == "y" ) {
            ev.pos.y() = in.get_int();
        } else if( key == "z" ) {
            ev.pos.z() = in.get_int();
        } else if( key == "v" ) {
            ev.value = in.get_int();
        } else if( key == "cid" ) {
            ev.creature_id = in.get_int();
        } else {
            in.skip_value();
        }
    }

    ev.type = static_cast<coop_event_type>( ev_type );
    return ev;
}
