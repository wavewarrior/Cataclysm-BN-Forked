#include "ui_theme.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <unordered_map>

#include "color.h"
#include "debug.h"
#include "fstream_utils.h"
#include "json.h"
#include "path_info.h"

namespace
{
// token -> hex (from theme.json "rcss")
std::unordered_map<std::string, std::string> g_rcss;
// nc_color name -> hex (from theme.json "game_colors"), resolved to int keys lazily
std::unordered_map<std::string, std::string> g_game_by_name;
std::unordered_map<int, std::string> g_game_by_int;
bool g_game_built = false;
// Insertion order (JSON order) for a stable editor UI.
std::vector<std::string> g_rcss_order;
std::vector<std::string> g_game_order;

unsigned hex_pair( const std::string &s, std::size_t i )
{
    const auto v = []( char c ) -> unsigned {
        if( c >= '0' && c <= '9' )
        {
            return c - '0';
        }
        const char l = c | 0x20;
        if( l >= 'a' && l <= 'f' )
        {
            return 10 + l - 'a';
        }
        return 0;
    };
    return i + 1 < s.size() ? v( s[i] ) * 16 + v( s[i + 1] ) : 0;
}

// "#rrggbbaa" (or "#rrggbb") -> RGBA floats 0..1.
void hex_to_rgba( const std::string &hex, float out[4] )
{
    out[0] = out[1] = out[2] = 0.f;
    out[3] = 1.f;
    if( hex.size() < 7 || hex[0] != '#' ) {
        return;
    }
    out[0] = hex_pair( hex, 1 ) / 255.f;
    out[1] = hex_pair( hex, 3 ) / 255.f;
    out[2] = hex_pair( hex, 5 ) / 255.f;
    out[3] = hex.size() >= 9 ? hex_pair( hex, 7 ) / 255.f : 1.f;
}

std::string rgba_to_hex( const float in[4] )
{
    const auto b = []( float f ) -> unsigned {
        const long v = std::lround( f * 255.f );
        return static_cast<unsigned>( v < 0 ? 0 : ( v > 255 ? 255 : v ) );
    };
    char buf[10];
    ( void )std::snprintf( buf, sizeof( buf ), "#%02x%02x%02x%02x", b( in[0] ), b( in[1] ),
                           b( in[2] ), b( in[3] ) );
    return std::string( buf );
}

// The c_* macros resolve through all_colors at runtime, so the name->nc_color
// table can only be built after colour load; both int maps defer to first use.
const std::unordered_map<std::string, nc_color> &color_names_table()
{
    static const std::unordered_map<std::string, nc_color> names = {
        { "c_black", c_black }, { "c_dark_gray", c_dark_gray }, { "c_light_gray", c_light_gray },
        { "c_white", c_white }, { "c_red", c_red }, { "c_light_red", c_light_red },
        { "c_green", c_green }, { "c_light_green", c_light_green }, { "c_brown", c_brown },
        { "c_yellow", c_yellow }, { "c_blue", c_blue }, { "c_light_blue", c_light_blue },
        { "c_cyan", c_cyan }, { "c_light_cyan", c_light_cyan }, { "c_magenta", c_magenta },
        { "c_pink", c_pink },
    };
    return names;
}

void build_int_map( const std::unordered_map<std::string, std::string> &by_name,
                    std::unordered_map<int, std::string> &by_int, const char *what )
{
    const auto &names = color_names_table();
    by_int.clear();
    for( const auto &kv : by_name ) {
        const auto it = names.find( kv.first );
        if( it != names.end() ) {
            by_int[ static_cast<int>( it->second ) ] = kv.second;
        } else {
            DebugLog( DL::Warn, DC::Main ) << "ui_theme: unknown " << what << " '" << kv.first << "'";
        }
    }
}
} // namespace

void ui_theme::load()
{
    g_rcss.clear();
    g_game_by_name.clear();
    g_game_by_int.clear();
    g_rcss_order.clear();
    g_game_order.clear();
    g_game_built = false;
    const std::string path = PATH_INFO::datadir() + "gui/theme.json";
    const bool ok = read_from_file_json( path, []( JsonIn & jsin ) {
        JsonObject root = jsin.get_object();
        if( root.has_object( "rcss" ) ) {
            for( JsonMember m : root.get_object( "rcss" ) ) {
                if( !m.is_comment() ) {
                    g_rcss[ m.name() ] = static_cast<std::string>( m );
                    g_rcss_order.push_back( m.name() );
                }
            }
        }
        if( root.has_object( "game_colors" ) ) {
            for( JsonMember m : root.get_object( "game_colors" ) ) {
                if( !m.is_comment() ) {
                    g_game_by_name[ m.name() ] = static_cast<std::string>( m );
                    g_game_order.push_back( m.name() );
                }
            }
        }
    }, true );
    if( !ok ) {
        DebugLog( DL::Warn, DC::Main ) << "ui_theme: could not read " << path;
    }
}

void ui_theme::substitute_tokens( std::string &s )
{
    std::string::size_type pos = 0;
    while( ( pos = s.find( "{{", pos ) ) != std::string::npos ) {
        const std::string::size_type end = s.find( "}}", pos + 2 );
        if( end == std::string::npos ) {
            break;
        }
        const std::string token = s.substr( pos + 2, end - pos - 2 );
        const auto it = g_rcss.find( token );
        std::string repl;
        if( it != g_rcss.end() ) {
            repl = it->second;
        } else {
            repl = "#ff00ffff";
            DebugLog( DL::Warn, DC::Main ) << "ui_theme: unknown rcss token {{" << token << "}}";
        }
        s.replace( pos, end - pos + 2, repl );
        pos += repl.size();
    }
}

bool ui_theme::game_color_hex( const nc_color &c, std::string &out )
{
    if( !g_game_built ) {
        build_int_map( g_game_by_name, g_game_by_int, "game colour" );
        g_game_built = true;
    }
    const auto it = g_game_by_int.find( static_cast<int>( c ) );
    if( it == g_game_by_int.end() ) {
        return false;
    }
    out = it->second;
    return true;
}

const std::vector<std::string> &ui_theme::rcss_names()
{
    return g_rcss_order;
}

const std::vector<std::string> &ui_theme::game_color_names()
{
    return g_game_order;
}

bool ui_theme::get_rcss_rgba( const std::string &name, float out[4] )
{
    const auto it = g_rcss.find( name );
    if( it == g_rcss.end() ) {
        return false;
    }
    hex_to_rgba( it->second, out );
    return true;
}

void ui_theme::set_rcss_rgba( const std::string &name, const float in[4] )
{
    g_rcss[ name ] = rgba_to_hex( in );
}

bool ui_theme::get_game_rgba( const std::string &name, float out[4] )
{
    const auto it = g_game_by_name.find( name );
    if( it == g_game_by_name.end() ) {
        return false;
    }
    hex_to_rgba( it->second, out );
    return true;
}

void ui_theme::set_game_rgba( const std::string &name, const float in[4] )
{
    g_game_by_name[ name ] = rgba_to_hex( in );
    g_game_built = false;  // force re-resolve so reopened screens pick it up
}

void ui_theme::save()
{
    const std::string path = PATH_INFO::datadir() + "gui/theme.json";
    std::ofstream f( path, std::ios::binary | std::ios::trunc );
    if( !f ) {
        DebugLog( DL::Warn, DC::Main ) << "ui_theme: could not write " << path;
        return;
    }
    f << "{\n";
    f << "  \"//\": \"Central UI theme for the RmlUi menus. 'rcss' values fill {{token}} "
      "placeholders in data/gui/*.rcss; 'game_colors' override the nc_color->hex used for "
      "menu text. Editable live in the F4 Theme tab.\",\n\n";
    f << "  \"rcss\": {\n";
    for( std::size_t i = 0; i < g_rcss_order.size(); i++ ) {
        const std::string &k = g_rcss_order[i];
        f << "    \"" << k << "\": \"" << g_rcss[k] << "\"" << ( i + 1 < g_rcss_order.size() ? "," : "" )
          << "\n";
    }
    f << "  },\n\n";
    f << "  \"game_colors\": {\n";
    for( std::size_t i = 0; i < g_game_order.size(); i++ ) {
        const std::string &k = g_game_order[i];
        f << "    \"" << k << "\": \"" << g_game_by_name[k] << "\""
          << ( i + 1 < g_game_order.size() ? "," : "" ) << "\n";
    }
    f << "  }\n";
    f << "}\n";
}
