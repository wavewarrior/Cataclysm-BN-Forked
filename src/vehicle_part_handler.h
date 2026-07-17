#pragma once
#ifndef CATA_SRC_VEHICLE_PART_HANDLER_H
#define CATA_SRC_VEHICLE_PART_HANDLER_H

#include "avatar.h"
#include "debug.h"
#include "detached_ptr.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "player_activity.h"
#include "translations.h"
#include "vehicle.h"

class RemovePartHandler
{
    public:
        virtual ~RemovePartHandler() = default;

        virtual void unboard( const tripoint_bub_ms &loc ) = 0;
        virtual detached_ptr<item> add_item_or_charges( const tripoint_bub_ms &loc, detached_ptr<item> &&it,
                bool permit_oob ) = 0;
        virtual void set_transparency_cache_dirty( int z ) = 0;
        virtual void set_floor_cache_dirty( int z ) = 0;
        virtual void removed( vehicle &veh, int part ) = 0;
        virtual void spawn_animal_from_part( item &base, const tripoint_bub_ms &loc ) = 0;
        virtual auto part_location( const vehicle &veh, const int part ) const -> tripoint_bub_ms = 0;
};

class DefaultRemovePartHandler : public RemovePartHandler
{
    public:
        ~DefaultRemovePartHandler() override = default;

        void unboard( const tripoint_bub_ms &loc ) override {
            g->m.unboard_vehicle( loc );
        }
        detached_ptr<item> add_item_or_charges( const tripoint_bub_ms &loc, detached_ptr<item> &&it,
                                                bool /*permit_oob*/ ) override {
            return g->m.add_item_or_charges( loc, std::move( it ) );
        }
        void set_transparency_cache_dirty( const int z ) override {
            map &here = get_map();
            here.set_transparency_cache_dirty( z );
            here.set_seen_cache_dirty( tripoint_bub_ms::zero() );
        }
        void set_floor_cache_dirty( const int z ) override {
            get_map().set_floor_cache_dirty( z );
        }
        void removed( vehicle &veh, const int part ) override {
            avatar &player_character = get_avatar();
            const player_activity &act = *player_character.activity;
            map &here = get_map();
            if( act.id() == ACT_VEHICLE && act.moves_left > 0 && act.values.size() > 6 ) {
                if( veh_pointer_or_null( here.veh_at( tripoint_bub_ms( act.values[0], act.values[1],
                                                      player_character.bub_pos().z() ) ) ) == &veh ) {
                    if( act.values[6] >= part ) {
                        player_character.cancel_activity();
                        add_msg( m_info, _( "The vehicle part you were working on has gone!" ) );
                    }
                }
            }
            if( g->u.get_grab_type() == OBJECT_VEHICLE &&
                g->u.bub_pos() + g->u.grab_point == veh.bub_part_location( part ) ) {
                if( veh.parts_at_relative( veh.part( part ).mount, false ).empty() ) {
                    add_msg( m_info, _( "The vehicle part you were holding has been destroyed!" ) );
                    g->u.grab( OBJECT_NONE );
                }
            }
            here.dirty_vehicle_list.insert( &veh );
        }
        void spawn_animal_from_part( item &base, const tripoint_bub_ms &loc ) override {
            base.release_monster( loc, 1 );
        }
        auto part_location( const vehicle &veh, const int part ) const -> tripoint_bub_ms override {
            return veh.bub_part_location( part );
        }
};

class MapgenRemovePartHandler : public RemovePartHandler
{
    private:
        map &m;

    public:
        MapgenRemovePartHandler( map &m ) : m( m ) { }
        ~MapgenRemovePartHandler() override = default;

        void unboard( const tripoint_bub_ms &/*loc*/ ) override {
            debugmsg( "Tried to unboard during mapgen!" );
        }
        detached_ptr<item> add_item_or_charges( const tripoint_bub_ms &loc, detached_ptr<item> &&it,
                                                bool permit_oob ) override {
            if( !m.inbounds( loc ) ) {
            point_sm_ms offset;
            if( !m.is_out_of_bounds( loc ) && m.get_submap_at( loc, offset ) != nullptr ) {
                    return m.add_item_or_charges( loc, std::move( it ) );
                }
                if( !permit_oob ) {
                    return std::move( it );
                }
                auto copy = loc;
                m.clip_to_bounds( copy );
                assert( m.inbounds( copy ) );
                return add_item_or_charges( copy, std::move( it ), false );
            }
            return m.add_item_or_charges( loc, std::move( it ) );
        }
        void set_transparency_cache_dirty( const int /*z*/ ) override {}
        void set_floor_cache_dirty( const int /*z*/ ) override {}
        void removed( vehicle &veh, const int /*part*/ ) override {
            m.dirty_vehicle_list.insert( &veh );
        }
        void spawn_animal_from_part( item &/*base*/, const tripoint_bub_ms &/*loc*/ ) override {
            debugmsg( "Tried to spawn animal from vehicle part during mapgen!" );
        }
        auto part_location( const vehicle &veh, const int part ) const -> tripoint_bub_ms override {
            return m.abs_to_bub( veh.abs_part_location( part ) );
        }
};

#endif // CATA_SRC_VEHICLE_PART_HANDLER_H
