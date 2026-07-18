#ifdef COOP_ENABLED

#include "coop_overmap.h"

#include "coop_proto.h"
#include "json.h"
#include "overmapbuffer.h"

#include <sstream>

auto build_overmap_sync_packet( const std::vector<tripoint_abs_omt> &tiles ) -> std::string
{
    std::ostringstream oss;
    JsonOut jout( oss );
    jout.start_object();
    jout.member( "t", static_cast<int>( coop_pkt::overmap_sync ) );
    jout.member( "tiles" );
    jout.start_array();
    for( const auto &tp : tiles ) {
        jout.start_array();
        jout.write( tp.x() );
        jout.write( tp.y() );
        jout.write( tp.z() );
        jout.end_array();
    }
    jout.end_array();
    jout.end_object();
    return oss.str();
}

auto parse_overmap_sync_tiles( const std::string &json_buf )
-> std::vector<tripoint_abs_omt>
{
    std::vector<tripoint_abs_omt> result;
    std::istringstream iss( json_buf );
    JsonIn jin( iss );
    JsonObject pkt = jin.get_object();
    pkt.allow_omitted_members();
    if( !pkt.has_array( "tiles" ) ) {
        return result;
    }
    JsonArray arr = pkt.get_array( "tiles" );
    while( arr.has_more() ) {
        JsonArray tp_arr = arr.next_array();
        result.emplace_back( tp_arr.get_int( 0 ), tp_arr.get_int( 1 ), tp_arr.get_int( 2 ) );
    }
    return result;
}

auto apply_overmap_sync_tiles( const std::vector<tripoint_abs_omt> &tiles,
                               const std::string &dim_id ) -> void
{
    overmapbuffer &omb = get_overmapbuffer( dim_id );
    for( const auto &pos : tiles ) {
        omb.set_seen( pos, true );
    }
}

#endif // COOP_ENABLED
