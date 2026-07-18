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

auto apply_overmap_sync_packet( const std::string &json_buf,
                                const std::string &dim_id ) -> void
{
    std::istringstream iss( json_buf );
    JsonIn jin( iss );
    JsonObject pkt = jin.get_object();
    pkt.allow_omitted_members();
    if( !pkt.has_array( "tiles" ) ) {
    return;
}
JsonArray arr = pkt.get_array( "tiles" );
overmapbuffer &omb = get_overmapbuffer( dim_id );
while( arr.has_more() ) {
    JsonArray tp_arr = arr.next_array();
        const tripoint_abs_omt pos{
            tp_arr.get_int( 0 ), tp_arr.get_int( 1 ), tp_arr.get_int( 2 )
        };
        omb.set_seen( pos, true );
    }
}

#endif // COOP_ENABLED
