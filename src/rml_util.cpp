#include "rml_util.h"

#include "color.h"
#include "input.h"
#include "lighting/rmlui_layer.h"
#include "output.h"
#include "path_info.h"
#include "rml_screen.h"
#include "sdl_utils.h"
#include "string_formatter.h"
#include "ui_manager.h"
#include "ui_theme.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <unordered_map>

// Escape &, < and > so raw text is safe inside data-rml (which parses markup).
std::string rml_escape( const std::string& s )
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
// Colours with a "game_colors" override in data/gui/theme.json render at the
// themed hex so RmlUi menus match the theme (the raw curses RGB is a harsh pure
// green/red); everything else falls back to the curses RGB. Scoped to RmlUi text
// only — the tile/world render path doesn't use this function.
static std::unordered_map<int, std::string> g_hex_cache;

std::string nc_color_to_hex( const nc_color& color )
{
    const int key = color;
    const auto it = g_hex_cache.find( key );
    if( it != g_hex_cache.end() ) { return it->second; }
    std::string hex;
    if( !ui_theme::game_color_hex( color, hex ) ) {
        const SDL_Color sdl = curses_color_to_SDL( color );
        // Format as #rrggbbaa (alpha always 255 for terminal colours).
        hex = string_format( "#%02x%02x%02x%02x", sdl.r, sdl.g, sdl.b, 255 );
    }
    g_hex_cache[key] = hex;
    return hex;
}

void clear_nc_color_cache() { g_hex_cache.clear(); }

// Convert a game string with <color_xxx> tags to RML markup with
// <span style="color:…"> spans. Plain text segments are HTML-escaped.
std::string cata_text_to_rml( const std::string& s )
{
    const std::vector<std::string> segs = split_by_color( s );
    std::string result;
    result.reserve( segs.size() * 60 + s.size() );

    // Each segment is either plain text or a colour tag with the text that
    // FOLLOWS it glued on (split_by_color keeps the tag and trailing run
    // together, e.g. "<color_white>Save"). Mirror print_colored_text: parse the
    // leading tag, emit the span markup, then strip the tag (rm_prefix) and emit
    // the remaining text. Dropping that trailing text is what blanked the rows.
    // Span depth is tracked rather than mirrored 1:1 from the tags.  Game strings
    // routinely open a colour and never close it (print_colored_text resets per
    // line, so nothing forced them to), and foldstring() splits a coloured run
    // across lines, leaving each line individually unbalanced.  A stray </span>
    // or an unclosed <span> makes RmlUi reject the WHOLE document with
    // "Closing tag 'body' mismatched ... was expecting 'span'", which blanks the
    // screen rather than just mis-colouring it.  So: ignore unmatched closes and
    // close whatever is still open at the end.
    int open_spans = 0;
    for( const auto& seg : segs ) {
        if( seg.empty() ) { continue; }
        if( seg[0] == '<' ) {
            const color_tag_parse_result tag = get_color_from_tag( seg, report_color_error::no );
            std::string rest = seg;
            if( tag.type != color_tag_parse_result::non_color_tag ) {
                // Strip the leading <...> tag, keeping the text after it.
                rest = rm_prefix( seg );
            }
            if( tag.type == color_tag_parse_result::open_color_tag ) {
                result += "<span style=\"color:";
                result += nc_color_to_hex( tag.color );
                result += "\">";
                ++open_spans;
            } else if( tag.type == color_tag_parse_result::close_color_tag && open_spans > 0 ) {
                result += "</span>";
                --open_spans;
            }
            result += rml_escape( rest );
        } else {
            result += rml_escape( seg );
        }
    }
    while( open_spans-- > 0 ) { result += "</span>"; }
    return result;
}

std::vector<std::string> item_info_rml_lines( item_info_data& data )
{
    // format_item_info builds the colour-tagged body (incl. the +/- compare
    // deltas when item_compare is non-empty); split on '\n' without re-wrapping
    // (RmlUi's pre-wrap handles visual wrap) and convert each line to markup.
    const std::string body = format_item_info( data.get_item_display(), data.get_item_compare() );
    std::vector<std::string> out;
    for( const std::string& line : foldstring( body, 100000 ) ) {
        out.push_back( cata_text_to_rml( line ) );
    }
    return out;
}

void rml_examine_item( item_info_data& data )
{
    if( !rmlui_layer::ready() ) { return; }
    struct ex_model_t {
        Rml::String info_html;
        Rml::DataModelHandle handle;
    } ex_model;

    input_context ex_ctxt( "AIM_EXAMINE" );
    ex_ctxt.register_action( "PAGE_UP" );
    ex_ctxt.register_action( "PAGE_DOWN" );
    ex_ctxt.register_action( "QUIT" );

    rml_doc ex_rml;
    ex_rml.open( true, "aim_examine", ex_ctxt, [&ex_model]( Rml::DataModelConstructor & c ) {
        c.Bind( "info_html", &ex_model.info_html );
        ex_model.handle = c.GetModelHandle();
    } );
    if( !ex_rml ) { return; }

    for( const auto& line : item_info_rml_lines( data ) ) { ex_model.info_html += line + "<br/>"; }
    ex_model.handle.DirtyVariable( "info_html" );

    Rml::Element* pane = ex_rml.document()->GetElementById( "aim-ex-info" );
    int scroll = 0;
    do {
        ui_manager::redraw();
        const std::string act = ex_ctxt.handle_input();
        if( pane ) {
            const auto h = static_cast<int>( pane->GetScrollHeight() / 4 );
            if( act == "PAGE_UP" ) {
                scroll = std::max( 0, scroll - h );
                pane->SetScrollTop( static_cast<float>( scroll ) );
            } else if( act == "PAGE_DOWN" ) {
                scroll += h;
                pane->SetScrollTop( static_cast<float>( scroll ) );
            }
        }
        if( act == "QUIT" ) { break; }
    } while( true );
}
