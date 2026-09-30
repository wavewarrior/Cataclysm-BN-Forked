#include "coop_vehicle_sync.h"

#include "coop_client.h"
#include "coop_session.h"
#include "game.h"
#include "json.h"
#include "map.h"
#include "npc.h"
#include "units_utility.h"
#include "vehicle.h"
#include "vpart_position.h"

#include <cmath>

auto make_coop_vehicle_pose( const vehicle &veh, std::uint32_t vid, bool host_driving )
-> coop_vehicle_pose
{
    coop_vehicle_pose pose;
    pose.vid = vid;
    pose.anchor = veh.abs_ms_location();
    if( veh.box2d_position_authority ) {
        const tripoint_bub_ms bub = veh.bub_ms_location();
        pose.frac_x = veh.physics_pos.x - static_cast<float>( bub.x() );
        pose.frac_y = veh.physics_pos.y - static_cast<float>( bub.y() );
    }
    pose.angle = veh.physics_angle;
    pose.face_deg = static_cast<int>( std::lround( units::to_degrees( veh.face.dir() ) ) );
    pose.steer_deg = static_cast<int>( std::lround( units::to_degrees( veh.turn_dir ) ) );
    pose.velocity = veh.velocity;
    pose.cruise_velocity = veh.cruise_velocity;
    pose.engine_on = veh.engine_on;
    pose.authority = veh.box2d_position_authority;
    pose.host_driving = host_driving;
    return pose;
}

auto write_coop_vehicle_poses( JsonOut &jout, const std::vector<coop_vehicle_pose> &poses )
-> void
{
    jout.start_array();
for( const coop_vehicle_pose &p : poses ) {
    jout.start_object();
        jout.member( "vid", p.vid );
        jout.member( "ax", p.anchor.x() );
        jout.member( "ay", p.anchor.y() );
        jout.member( "az", p.anchor.z() );
        jout.member( "fx", p.frac_x );
        jout.member( "fy", p.frac_y );
        jout.member( "ang", p.angle );
        jout.member( "face", p.face_deg );
        jout.member( "steer", p.steer_deg );
        jout.member( "vel", p.velocity );
        jout.member( "cruise", p.cruise_velocity );
        jout.member( "eng", p.engine_on );
        jout.member( "auth", p.authority );
        jout.member( "hd", p.host_driving );
        jout.end_object();
    }
    jout.end_array();
}

auto read_coop_vehicle_poses( JsonIn &jin ) -> std::vector<coop_vehicle_pose>
{
    std::vector<coop_vehicle_pose> poses;
    jin.start_array();
    while( !jin.end_array() ) {
        JsonObject o = jin.get_object();
        o.allow_omitted_members();
        coop_vehicle_pose p;
        p.vid = static_cast<std::uint32_t>( o.get_int( "vid", 0 ) );
        p.anchor = tripoint_abs_ms{ o.get_int( "ax", 0 ), o.get_int( "ay", 0 ), o.get_int( "az", 0 ) };
        p.frac_x = static_cast<float>( o.get_float( "fx", 0.0 ) );
        p.frac_y = static_cast<float>( o.get_float( "fy", 0.0 ) );
        p.angle = static_cast<float>( o.get_float( "ang", 0.0 ) );
        p.face_deg = o.get_int( "face", 0 );
        p.steer_deg = o.get_int( "steer", 0 );
        p.velocity = o.get_int( "vel", 0 );
        p.cruise_velocity = o.get_int( "cruise", 0 );
        p.engine_on = o.get_bool( "eng", false );
        p.authority = o.get_bool( "auth", false );
        p.host_driving = o.get_bool( "hd", false );
        poses.push_back( p );
    }
    return poses;
}

auto apply_coop_vehicle_pose( map &here, vehicle &veh, const coop_vehicle_pose &pose ) -> bool
{
    const tripoint_rel_ms delta = pose.anchor - veh.abs_ms_location();
    if( delta != tripoint_rel_ms::zero() ) {
        here.displace_vehicle( veh, delta );
    }
    const bool rotated = veh.face.dir() != units::from_degrees( pose.face_deg )
                         || veh.physics_angle != pose.angle;
    veh.box2d_position_authority = pose.authority;
    if( pose.authority ) {
        const tripoint_bub_ms bub = veh.bub_ms_location();
        veh.physics_pos = rl_vec2d{ static_cast<float>( bub.x() ) + pose.frac_x,
                                    static_cast<float>( bub.y() ) + pose.frac_y };
        veh.physics_angle = pose.angle;
        veh.render_offset_x = pose.frac_x;
        veh.render_offset_y = pose.frac_y;
        veh.refresh_precalc( pose.angle );
        veh.face.init( units::from_degrees( pose.face_deg ) );
    } else {
        veh.render_offset_x = 0.0f;
        veh.render_offset_y = 0.0f;
        veh.set_facing_and_pivot( units::from_degrees( pose.face_deg ), veh.pivot_point(), true );
    }
    veh.turn_dir = units::from_degrees( pose.steer_deg );
    veh.velocity = pose.velocity;
    veh.cruise_velocity = pose.cruise_velocity;
    veh.engine_on = pose.engine_on;
    // The client avatar rides along with the swapped/displaced vehicle until proxy
    // reconciliation runs later in the same apply_sync.
    veh.commit_occupants();
    return rotated;
}

auto coop_partner_driven_vehicle() -> const vehicle *
{
    coop_session &sess = coop_session::get();
    if( !sess.is_coop() ) {
        return nullptr;
    }
    if( sess.is_host() ) {
        const npc *proxy = g->critter_by_id<npc>( sess.proxy_npc_id );
        if( proxy == nullptr ) {
            return nullptr;
        }
        const optional_vpart_position vp = g->m.veh_at( proxy->bub_pos() );
        if( !vp ) {
            return nullptr;
        }
        vehicle &veh = const_cast<vehicle &>( vp->vehicle() ); // *NOLINT*
        return veh.player_in_control( *proxy ) ? &veh : nullptr;
    }
    if( sess.is_client() && g->coop_client_ != nullptr ) {
        return g->coop_client_->host_driven_vehicle();
    }
    return nullptr;
}
