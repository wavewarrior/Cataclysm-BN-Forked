
#include "coop_packets.h"

#include "coop_proto.h"
#include "json.h"

#include <sstream>

auto build_world_seed_packet( const world_seed_data& d ) -> std::string
{
    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    jout.member( "t", static_cast<int>( coop_pkt::world_seed ) );
    jout.member( "d" );
    jout.start_object();
    jout.member( "turn", d.turn );
    jout.member( "spawn_x", d.spawn_pos.x() );
    jout.member( "spawn_y", d.spawn_pos.y() );
    jout.member( "spawn_z", d.spawn_pos.z() );
    jout.member( "player_name", d.player_name );
    jout.member( "world_name", d.world_name );
    jout.member( "rng_seed", std::to_string( d.rng_seed ) );
    // String-encoded like rng_seed: unsigned values above INT_MAX do not survive get_int().
    jout.member( "world_seed", std::to_string( d.world_seed ) );
    jout.member( "session_token", d.session_token );
    jout.end_object();
    jout.end_object();
    return oss.str();
}

auto build_action_packet( const action_packet_data& d ) -> std::string
{
    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    jout.member( "t", static_cast<int>( coop_pkt::action ) );
    jout.member( "d" );
    jout.start_object();
    jout.member( "seq", d.seq );
    jout.member( "key", d.key );
    jout.member( "ctx", d.ctx_json ); // wire key is "ctx", not "ctx_json"
    jout.end_object();
    jout.end_object();
    return oss.str();
}

auto parse_world_seed_packet( const std::string& buf ) -> std::optional<world_seed_data>
{
    try {
        std::istringstream iss( buf );
        JsonIn jin( iss );
        JsonObject pkt = jin.get_object();
        pkt.allow_omitted_members();
        if( static_cast<coop_pkt>( pkt.get_int( "t" ) ) != coop_pkt::world_seed ) {
            return std::nullopt;
        }
        JsonObject d = pkt.get_object( "d" );
        d.allow_omitted_members();
        world_seed_data result;
        result.turn = d.get_int( "turn", 0 );
        result.spawn_pos.x() = d.get_int( "spawn_x", 0 );
        result.spawn_pos.y() = d.get_int( "spawn_y", 0 );
        result.spawn_pos.z() = d.get_int( "spawn_z", 0 );
        result.player_name = d.get_string( "player_name", "" );
        result.world_name = d.get_string( "world_name", "" );
        result.rng_seed = static_cast<unsigned int>( std::stoul( d.get_string( "rng_seed", "0" ) ) );
        result.world_seed =
            static_cast<unsigned int>( std::stoul( d.get_string( "world_seed", "0" ) ) );
        result.session_token = d.get_string( "session_token", "" );
        return result;
    } catch( const JsonError &e ) {
        DebugLog( DL::Error, DC::Main ) << "[coop] parse_world_seed: " << e.what();
        return std::nullopt;
    }
}

auto parse_action_packet( const std::string& buf ) -> std::optional<action_packet_data>
{
    try {
        std::istringstream iss( buf );
        JsonIn jin( iss );
        JsonObject pkt = jin.get_object();
        pkt.allow_omitted_members();
        if( static_cast<coop_pkt>( pkt.get_int( "t" ) ) != coop_pkt::action ) {
            return std::nullopt;
        }
        JsonObject d = pkt.get_object( "d" );
        d.allow_omitted_members();
        action_packet_data result;
        result.seq = static_cast<uint32_t>( d.get_int( "seq", 0 ) );
        result.key = d.get_string( "key", "" );
        result.ctx_json = d.get_string( "ctx", "" ); // wire key is "ctx"
        return result;
    } catch( const JsonError & ) {
        return std::nullopt;
    }
}

auto parse_sync_header( const std::string& buf ) -> std::optional<sync_header_data>
{
    try {
        std::istringstream iss( buf );
        JsonIn jin( iss );
        sync_header_data result;
        jin.start_object();
        while( !jin.end_object() ) {
            const std::string key = jin.get_member_name();
            if( key == "t" ) {
                if( static_cast<coop_pkt>( jin.get_int() ) != coop_pkt::sync ) {
                    return std::nullopt;
                }
            } else if( key == "turn" ) {
                result.turn = jin.get_int();
            } else if( key == "last_seq" ) {
                result.last_seq = jin.get_int();
            } else if( key == "tiles" || key == "monsters" ) {
                jin.skip_value();
            } else if( key == "proxy_ax" ) {
                result.proxy_pos.x() = jin.get_int();
            } else if( key == "proxy_ay" ) {
                result.proxy_pos.y() = jin.get_int();
            } else if( key == "proxy_az" ) {
                result.proxy_pos.z() = jin.get_int();
                result.has_proxy_pos = true;
            } else if( key == "host_ax" ) {
                result.host_pos.x() = jin.get_int();
            } else if( key == "host_ay" ) {
                result.host_pos.y() = jin.get_int();
            } else if( key == "host_az" ) {
                result.host_pos.z() = jin.get_int();
                result.has_host_pos = true;
            } else {
                jin.skip_value();
            }
        }
        return result;
    } catch( const JsonError & ) {
        return std::nullopt;
    }
}

auto build_join_info_packet( const join_info_data& d ) -> std::string
{
    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    jout.member( "t", static_cast<int>( coop_pkt::join_info ) );
    jout.member( "d" );
    jout.start_object();
    jout.member( "ax", d.pos.x() );
    jout.member( "ay", d.pos.y() );
    jout.member( "az", d.pos.z() );
    if( !d.worn_json.empty() ) { jout.member( "worn", d.worn_json ); }
    jout.end_object();
    jout.end_object();
    return oss.str();
}

auto parse_join_info_packet( const std::string& buf ) -> std::optional<join_info_data>
{
    try {
        std::istringstream iss( buf );
        JsonIn jin( iss );
        JsonObject pkt = jin.get_object();
        pkt.allow_omitted_members();
        if( static_cast<coop_pkt>( pkt.get_int( "t" ) ) != coop_pkt::join_info ) {
            return std::nullopt;
        }
        JsonObject d = pkt.get_object( "d" );
        d.allow_omitted_members();
        join_info_data result;
        result.pos.x() = d.get_int( "ax", 0 );
        result.pos.y() = d.get_int( "ay", 0 );
        result.pos.z() = d.get_int( "az", 0 );
        result.worn_json = d.get_string( "worn", {} );
        return result;
    } catch( const JsonError & ) {
        return std::nullopt;
    }
}

auto parse_vertical_move_ctx( const std::string& buf ) -> std::optional<vertical_move_ctx>
{
    if( buf.empty() ) { return std::nullopt; }
try {
    std::istringstream iss( buf );
        JsonIn jin( iss );
        JsonObject ctx = jin.get_object();
        ctx.allow_omitted_members();
        vertical_move_ctx result;
        result.landing.x() = ctx.get_int( "ax", 0 );
        result.landing.y() = ctx.get_int( "ay", 0 );
        result.landing.z() = ctx.get_int( "az", 0 );
        return result;
    } catch( const JsonError & ) {
        return std::nullopt;
    }
}

auto build_skill_sync_fields( JsonOut& jout,
                              const std::vector<std::pair<std::string, int>> &skills ) -> void
{
    jout.member( "skills" );
    jout.start_array();
for( const auto& [id, lvl] : skills ) {
    jout.start_array();
        jout.write( id );
        jout.write( lvl );
        jout.end_array();
    }
    jout.end_array();
}

auto parse_skill_sync_fields( const JsonObject& d )
-> std::vector<std::pair<std::string, int>>
{
    std::vector<std::pair<std::string, int>> result;
    if( !d.has_array( "skills" ) ) {
        return result;
    }
    for( const JsonArray& entry : d.get_array( "skills" ) ) {
        if( entry.size() >= 2 ) {
            result.emplace_back( entry.get_string( 0 ), entry.get_int( 1 ) );
        }
    }
    return result;
}

