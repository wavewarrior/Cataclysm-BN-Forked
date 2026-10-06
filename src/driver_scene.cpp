#include "driver_scene.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "catalua_impl.h"
#include "catalua_loader.h"
#include "catalua_log.h"
#include "catalua_sol.h"
#include "driver_message_delta.h"
#include "init.h"
#include "input.h"
#include "json.h"
#include "path_info.h"

namespace driver_scene
{

namespace
{

constexpr const char *default_scenes_path = "tools/visual_verify/scenes";

/// Loads and runs the file; says what went wrong, empty when nothing did. Lua errors come back as
/// results of the protected call, and an exception from the bindings is turned into one by sol.
auto execute( sol::state &lua, const std::string &path, bool &passed ) -> std::string
{
    const cata::lua_loader::script_context_guard guard{ path };

    sol::load_result loaded = lua.load_file( path );
    if( !loaded.valid() ) {
        const sol::error err = loaded;
        return err.what();
    }

    sol::protected_function exec = loaded;
    // The same sandbox a mod script gets: globals are readable, and anything the Scene assigns
    // is kept in an environment that is discarded with it.
    const sol::environment env( lua, sol::create, lua.globals() );
    sol::set_environment( env, exec );

    const sol::protected_function_result ran = exec();
    if( !ran.valid() ) {
        const sol::error err = ran;
        return err.what();
    }
    // A Scene says no by returning false; returning nothing at all is fine.
    passed = !( ran.get_type() == sol::type::boolean && !ran.get<bool>() );
    return {};
}

} // namespace

auto default_dir() -> std::string
{
    return PATH_INFO::base_path() + default_scenes_path;
}

auto valid_name( const std::string &name ) -> bool
{
    return !name.empty() && std::ranges::all_of( name, []( char c ) {
        return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ||
               c == '_' || c == '-';
    } );
}

auto find( const std::string &dir, const std::string &name ) -> std::string
{
    const std::filesystem::path file = std::filesystem::path( dir ) / ( name + ".lua" );
    std::error_code ec;
    return std::filesystem::is_regular_file( file, ec ) ? file.string() : std::string();
}

auto run( const std::string &path ) -> result
{
    result out;
    cata::lua_state *state = DynamicDataLoader::get_instance().lua.get();
    if( !state ) {
        out.lines.emplace_back( "error: the game has no Lua state to run a Scene in" );
        return out;
    }

    // A read that unwinds out of a Scene leaves the input timeout it had set.
    const int timeout = inp_mngr.get_timeout();
    std::string error;
    {
        const cata::lua_log_handler::capture log( cata::get_lua_log_instance() );
        try {
            error = execute( state->lua, path, out.passed );
        } catch( const std::exception &err ) {
            error = err.what();
        }
        for( const cata::lua_log_msg &message : log.messages() ) {
            out.lines.push_back( message.text );
        }
    }
    inp_mngr.set_timeout( timeout );

    if( !error.empty() ) {
        out.passed = false;
        out.lines.push_back( "error: " + error );
    }
    return out;
}

auto write( JsonOut &jo, result scene ) -> bool
{
    bool cut = cap_messages( scene.lines, max_lines, max_line_bytes );
    size_t total = 0;
    for( const std::string &line : scene.lines ) {
        total += line.size();
    }
    // Drop the oldest lines until the rest fits: the summary a Scene ends with is the one kept.
    size_t dropped = 0;
    while( total > max_total_bytes && dropped + 1 < scene.lines.size() ) {
        total -= scene.lines[dropped++].size();
        cut = true;
    }
    scene.lines.erase( scene.lines.begin(),
                       scene.lines.begin() + static_cast<std::ptrdiff_t>( dropped ) );

    jo.member( "scene" );
    jo.start_object();
    jo.member( "status", std::string( scene.passed ? "passed" : "failed" ) );
    jo.member( "lines" );
    jo.start_array();
    for( const std::string &line : scene.lines ) {
        jo.write( line );
    }
    jo.end_array();
    jo.end_object();
    return cut;
}

} // namespace driver_scene
