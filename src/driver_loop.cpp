#include "driver_loop.h"

#include <cstddef>
#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <unistd.h>

#include "avatar.h"
#include "calendar.h"
#include "game.h"
#include "json.h"

namespace
{

/// Buffered line reader over a raw descriptor. `read_line` is false at EOF or on a read error.
class line_reader
{
    public:
        explicit line_reader( int fd ) : fd_( fd ) {}

        auto read_line( std::string &out ) -> bool {
            size_t nl = buf_.find( '\n' );
            while( nl == std::string::npos ) {
                char chunk[4096];
                const ssize_t n = ::read( fd_, chunk, sizeof( chunk ) );
                if( n <= 0 ) {
                    return false;
                }
                buf_.append( chunk, static_cast<size_t>( n ) );
                nl = buf_.find( '\n' );
            }
            out = buf_.substr( 0, nl );
            buf_.erase( 0, nl + 1 );
            if( !out.empty() && out.back() == '\r' ) {
                out.pop_back();
            }
            return true;
        }

    private:
        int fd_;
        std::string buf_;
};

void write_all( int fd, const std::string &s )
{
    const char *ptr = s.data();
    size_t left = s.size();
    while( left > 0 ) {
        const ssize_t w = ::write( fd, ptr, left );
        if( w <= 0 ) {
            return;
        }
        ptr += w;
        left -= static_cast<size_t>( w );
    }
}

/// `id` is empty when the request carried none that could be read.
void begin_response( JsonOut &jo, const std::optional<int> &id, std::string_view status )
{
    jo.start_object();
    jo.member( "id" );
    if( id ) {
        jo.write( *id );
    } else {
        jo.write_null();
    }
    jo.member( "status", std::string( status ) );
}

auto error_line( const std::optional<int> &id, const std::string &why ) -> std::string
{
    std::ostringstream os;
    JsonOut jo( os, false );
    begin_response( jo, id, "error" );
    jo.member( "error", why );
    jo.end_object();
    return os.str() + "\n";
}

auto ping_line( int id ) -> std::string
{
    std::ostringstream os;
    JsonOut jo( os, false );
    begin_response( jo, id, "ok" );
    jo.member( "ready", true );
    jo.end_object();
    return os.str() + "\n";
}

auto quit_line( int id ) -> std::string
{
    std::ostringstream os;
    JsonOut jo( os, false );
    begin_response( jo, id, "ok" );
    jo.end_object();
    return os.str() + "\n";
}

/// Minimal lean observation: reads the world, never advances it.
auto state_line( int id ) -> std::string
{
    const avatar &u = get_avatar();
    std::ostringstream os;
    JsonOut jo( os, false );
    begin_response( jo, id, "ok" );
    jo.member( "boundary", std::string( "turn_complete" ) );
    jo.member( "turn", to_turn<int>( calendar::turn ) );
    jo.member( "time_passed", false );
    jo.member( "new_messages" );
    jo.start_array();
    jo.end_array();
    jo.member( "prompt" );
    jo.write_null();
    jo.member( "hp", u.hp_percentage() );
    jo.member( "pain", u.get_pain() );
    jo.member( "stamina", u.get_stamina() );
    jo.member( "hunger", u.get_stored_kcal() );
    jo.member( "thirst", u.get_thirst() );
    jo.member( "outcome", std::string( "completed" ) );
    jo.end_object();
    return os.str() + "\n";
}

} // namespace

void run_driver_loop( int fd )
{
    line_reader in( fd );
    std::string line;
    while( in.read_line( line ) ) {
        if( line.empty() ) {
            continue;
        }
        std::optional<int> id;
        try {
            std::istringstream ss( line );
            JsonIn jsin( ss );
            JsonObject jo = jsin.get_object();
            jo.allow_omitted_members();
            id = jo.get_int( "id" );
            const std::string cmd = jo.get_string( "cmd" );
            if( cmd == "ping" ) {
                write_all( fd, ping_line( *id ) );
            } else if( cmd == "state" ) {
                write_all( fd, state_line( *id ) );
            } else if( cmd == "quit" ) {
                write_all( fd, quit_line( *id ) );
                return;
            } else {
                write_all( fd, error_line( id, "unknown cmd '" + cmd + "'" ) );
            }
        } catch( const std::exception &err ) {
            write_all( fd, error_line( id, std::string( "bad request: " ) + err.what() ) );
        }
    }
}
