#include "catalua_log.h"

namespace cata
{

lua_log_handler::lua_log_handler()
{
    set_log_capacity( DEFAULT_LUA_LOG_CAPACITY );
}

void lua_log_handler::set_log_capacity( size_t lines )
{
    if( lines < entries.size() ) {
        // Will erase old entries
        entries.resize( lines );
    }
    capacity = lines;
}

void lua_log_handler::add( LuaLogLevel level, std::string &&text )
{
    while( entries.size() >= capacity ) {
        entries.pop_back();
    }
    if( capturing ) {
        capturing->push_back( lua_log_msg{ level, text } );
    }
    entries.push_front( lua_log_msg{ level, text } );
}

lua_log_handler::capture::capture( lua_log_handler &handler )
    : owner( handler )
{
    owner.capturing = &collected;
}

lua_log_handler::capture::~capture()
{
    owner.capturing = nullptr;
}

void lua_log_handler::clear()
{
    entries.clear();
}

lua_log_handler &get_lua_log_instance()
{
    static lua_log_handler log;
    return log;
}

} // namespace cata
