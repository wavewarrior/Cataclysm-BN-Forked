#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <optional>
#include <vector>

#include "cached_options.h"
#include "color.h"
#include "message_types.h"
#include "string_formatter.h"
#include "enums.h"
#include "debug.h"

class JsonOut;
class JsonObject;
class translation;

namespace catacurses
{
class window;
} // namespace catacurses

namespace Messages
{
/// A rich message entry for the animated log: text, color, type, timestamp, seq.
struct rich_message {
    std::string time;
    std::string text;
    game_message_type type = m_neutral;
    nc_color color;
    unsigned seq = 0;
};
std::vector<std::pair<std::string, std::string>> recent_messages( size_t count );
std::vector<std::pair<std::string, std::string>> recent_messages_colored(
    size_t count ); // deprecated, remove after hud_log migration
auto recent_messages_rich( size_t count ) -> std::vector<rich_message>;
void add_msg( std::string msg );
void add_msg( const game_message_params &params, std::string msg );
void clear_messages();
void deactivate();
size_t size();
bool has_undisplayed_messages();
void display_messages();
void serialize( JsonOut &json );
void deserialize( const JsonObject &json );
} // namespace Messages

void add_msg( std::string msg );
template<typename ...Args>
inline void add_msg( const std::string &msg, Args &&... args )
{
    return add_msg( string_format( msg, std::forward<Args>( args )... ) );
}
template<typename ...Args>
inline void add_msg( const char *const msg, Args &&... args )
{
    return add_msg( string_format( msg, std::forward<Args>( args )... ) );
}
template<typename ...Args>
inline void add_msg( const translation &msg, Args &&... args )
{
    return add_msg( string_format( msg, std::forward<Args>( args )... ) );
}

void add_msg( const game_message_params &params, std::string msg );
template<typename ...Args>
inline void add_msg( const game_message_params &params, const std::string &msg, Args &&... args )
{
    if( params.type == m_debug && !debug_mode ) {
        return;
    }
    return add_msg( params, string_format( msg, std::forward<Args>( args )... ) );
}
template<typename ...Args>
inline void add_msg( const game_message_params &params, const char *const msg, Args &&... args )
{
    if( params.type == m_debug && !debug_mode ) {
        return;
    }
    return add_msg( params, string_format( msg, std::forward<Args>( args )... ) );
}


