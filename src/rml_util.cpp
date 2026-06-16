#include "rml_util.h"

#include <unordered_map>

#include "color.h"
#include "output.h"
#include "sdl_utils.h"
#include "string_formatter.h"

// Escape &, < and > so raw text is safe inside data-rml (which parses markup).
std::string rml_escape( const std::string &s )
{
    std::string result;
    result.reserve( s.size() );
    for( const char c : s ) {
        switch( c ) {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '\n':
                result += "<br/>";
                break;
            default:
                result += c;
                break;
        }
    }
    return result;
}

// Cache an nc_color → "#rrggbbaa" hex string so repeated lookups are cheap.
//
// Common foreground colours are remapped to the gruvbox palette so RmlUi menus
// match theme.rcss (the raw curses RGB is a harsh pure green/red that clashes).
// This is scoped to RmlUi text only — the tile/world render path doesn't use this
// function. Unmapped colours (backgrounds, attrs) fall back to the curses RGB.
std::string nc_color_to_hex( const nc_color &color )
{
    static std::unordered_map<int, std::string> cache;
    const int key = color;
    const auto it = cache.find( key );
    if( it != cache.end() ) {
        return it->second;
    }
    // Lazy-built (the c_* macros resolve colours at runtime, after colour load).
    static const std::unordered_map<int, std::string> gruvbox = {
        { int( c_black ),       "#1d2021ff" },
        { int( c_dark_gray ),   "#928374ff" },
        { int( c_light_gray ),  "#ebdbb2ff" },
        { int( c_white ),       "#fbf1c7ff" },
        { int( c_red ),         "#cc241dff" },
        { int( c_light_red ),   "#fb4934ff" },
        { int( c_green ),       "#98971aff" },
        { int( c_light_green ), "#b8bb26ff" },
        { int( c_brown ),       "#d79921ff" },
        { int( c_yellow ),      "#fabd2fff" },
        { int( c_blue ),        "#458588ff" },
        { int( c_light_blue ),  "#83a598ff" },
        { int( c_cyan ),        "#689d6aff" },
        { int( c_light_cyan ),  "#8ec07cff" },
        { int( c_magenta ),     "#b16286ff" },
        { int( c_pink ),        "#d3869bff" },
    };
    std::string hex;
    const auto git = gruvbox.find( key );
    if( git != gruvbox.end() ) {
        hex = git->second;
    } else {
        const SDL_Color sdl = curses_color_to_SDL( color );
        // Format as #rrggbbaa (alpha always 255 for terminal colours).
        hex = string_format( "#%02x%02x%02x%02x", sdl.r, sdl.g, sdl.b, 255 );
    }
    cache[key] = hex;
    return hex;
}

// Convert a game string with <color_xxx> tags to RML markup with
// <span style="color:…"> spans. Plain text segments are HTML-escaped.
std::string cata_text_to_rml( const std::string &s )
{
    const std::vector<std::string> segs = split_by_color( s );
    std::string result;
    result.reserve( segs.size() * 60 + s.size() );

    // Each segment is either plain text or a colour tag with the text that
    // FOLLOWS it glued on (split_by_color keeps the tag and trailing run
    // together, e.g. "<color_white>Save"). Mirror print_colored_text: parse the
    // leading tag, emit the span markup, then strip the tag (rm_prefix) and emit
    // the remaining text. Dropping that trailing text is what blanked the rows.
    for( const auto &seg : segs ) {
        if( seg.empty() ) {
            continue;
        }
        if( seg[0] == '<' ) {
            const color_tag_parse_result tag =
                get_color_from_tag( seg, report_color_error::no );
            std::string rest = seg;
            if( tag.type != color_tag_parse_result::non_color_tag ) {
                // Strip the leading <...> tag, keeping the text after it.
                rest = rm_prefix( seg );
            }
            if( tag.type == color_tag_parse_result::open_color_tag ) {
                result += "<span style=\"color:";
                result += nc_color_to_hex( tag.color );
                result += "\">";
            } else if( tag.type == color_tag_parse_result::close_color_tag ) {
                result += "</span>";
            }
            result += rml_escape( rest );
        } else {
            result += rml_escape( seg );
        }
    }
    return result;
}

std::vector<std::string> item_info_rml_lines( item_info_data &data )
{
    // format_item_info builds the colour-tagged body (incl. the +/- compare
    // deltas when item_compare is non-empty); split on '\n' without re-wrapping
    // (RmlUi's pre-wrap handles visual wrap) and convert each line to markup.
    const std::string body = format_item_info( data.get_item_display(), data.get_item_compare() );
    std::vector<std::string> out;
    for( const std::string &line : foldstring( body, 100000 ) ) {
        out.push_back( cata_text_to_rml( line ) );
    }
    return out;
}
