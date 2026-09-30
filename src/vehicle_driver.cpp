#include "vehicle_driver.h"

#include "avatar.h"
#include "game.h"
#include "map.h"
#include "player.h"
#include "veh_type.h"
#include "vehicle.h"
#include "vpart_position.h"

auto vehicle_driver( const vehicle &veh ) -> const Character *
{
    avatar &u = get_avatar();
    if( veh.player_in_control( u ) ) {
        return &u;
    }
    // Boarded characters (the co-op proxy) are checked by their LOCAL control condition
    // only: vehicle::player_in_control() falls through to remote_controlled(), which can
    // drop the avatar's remote control and message the player once per call.
    for( const int p : veh.boarded_parts() ) {
        player *psg = veh.get_passenger( p );
        if( psg == nullptr || psg == &u || !psg->controlling_vehicle ) {
            continue;
        }
        const optional_vpart_position pvp = get_map().veh_at( psg->bub_pos() );
        if( pvp && &pvp->vehicle() == &veh
            && veh.part_with_feature( pvp->part_index(), VPFLAG_CONTROLS, false ) >= 0 ) {
            return psg;
        }
    }
    return nullptr;
}
