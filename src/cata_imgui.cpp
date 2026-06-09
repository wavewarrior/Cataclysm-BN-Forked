// Include imgui.h before any other header so the game's DebugLog macro
// doesn't collide with ImGui::DebugLog.
#pragma push_macro( "DebugLog" )
#undef DebugLog
#include "imgui.h"
#pragma pop_macro( "DebugLog" )

#include "cata_imgui.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "output.h"

namespace cataimgui
{

// ---------------------------------------------------------------------------
// Color-name → ImVec4 table.  Mirrors the game's terminal-16 ANSI palette so
// ImGui menus look reasonable without coupling to windowsPalette / SDL.
// ---------------------------------------------------------------------------
static const auto &color_table()
{
    static const std::map<std::string, ImVec4, std::less<>> table = {
        { "black",        ImVec4( 0.00f, 0.00f, 0.00f, 1.00f ) },
        { "red",          ImVec4( 0.80f, 0.20f, 0.20f, 1.00f ) },
        { "green",        ImVec4( 0.20f, 0.70f, 0.20f, 1.00f ) },
        { "blue",         ImVec4( 0.20f, 0.40f, 0.80f, 1.00f ) },
        { "yellow",       ImVec4( 0.80f, 0.70f, 0.20f, 1.00f ) },
        { "magenta",      ImVec4( 0.80f, 0.30f, 0.80f, 1.00f ) },
        { "cyan",         ImVec4( 0.20f, 0.70f, 0.70f, 1.00f ) },
        { "white",        ImVec4( 0.80f, 0.80f, 0.80f, 1.00f ) },
        { "light_red",    ImVec4( 1.00f, 0.40f, 0.40f, 1.00f ) },
        { "light_green",  ImVec4( 0.40f, 0.90f, 0.40f, 1.00f ) },
        { "light_blue",   ImVec4( 0.40f, 0.60f, 1.00f, 1.00f ) },
        { "light_yellow", ImVec4( 1.00f, 0.90f, 0.40f, 1.00f ) },
        { "light_magenta",ImVec4( 1.00f, 0.50f, 1.00f, 1.00f ) },
        { "light_cyan",   ImVec4( 0.50f, 0.90f, 0.90f, 1.00f ) },
        { "light_gray",   ImVec4( 0.70f, 0.70f, 0.70f, 1.00f ) },
        { "light_grey",   ImVec4( 0.70f, 0.70f, 0.70f, 1.00f ) },
        { "dark_gray",    ImVec4( 0.30f, 0.30f, 0.30f, 1.00f ) },
        { "dark_grey",    ImVec4( 0.30f, 0.30f, 0.30f, 1.00f ) },
        { "h_white",      ImVec4( 1.00f, 1.00f, 1.00f, 1.00f ) },
        { "c_white",      ImVec4( 0.80f, 0.80f, 0.80f, 1.00f ) },
        { "c_light_gray", ImVec4( 0.70f, 0.70f, 0.70f, 1.00f ) },
        { "c_dark_gray",  ImVec4( 0.30f, 0.30f, 0.30f, 1.00f ) },
        { "c_red_red",    ImVec4( 0.80f, 0.20f, 0.20f, 1.00f ) },
        { "c_green",      ImVec4( 0.20f, 0.70f, 0.20f, 1.00f ) },
        { "c_light_green",ImVec4( 0.40f, 0.90f, 0.40f, 1.00f ) },
        { "c_magenta",    ImVec4( 0.80f, 0.30f, 0.80f, 1.00f ) },
        { "c_cyan",       ImVec4( 0.20f, 0.70f, 0.70f, 1.00f ) },
        { "c_light_blue", ImVec4( 0.40f, 0.60f, 1.00f, 1.00f ) },
        { "c_yellow",     ImVec4( 0.80f, 0.70f, 0.20f, 1.00f ) },
        { "c_brown",      ImVec4( 0.50f, 0.35f, 0.10f, 1.00f ) },
        { "c_dark_red",   ImVec4( 0.50f, 0.10f, 0.10f, 1.00f ) },
        { "c_pink",       ImVec4( 1.00f, 0.60f, 0.80f, 1.00f ) },
    };
    return table;
}

static auto lookup_color( const std::string &name ) -> ImVec4
{
    auto lookup = []( const std::string &n ) -> std::optional<ImVec4> {
        const auto &table = color_table();
        const auto it = table.find( n );
        if( it != table.end() ) {
            return it->second;
        }
        return std::nullopt;
    };

    // Try exact name first
    if( auto c = lookup( name ) ) {
        return *c;
    }

    // h_ prefixed highlight colors: brighten the base color
    if( name.size() > 2 && name[0] == 'h' && name[1] == '_' ) {
        if( auto base = lookup( name.substr( 2 ) ) ) {
            base->x = std::min( 1.0f, base->x * 1.25f );
            base->y = std::min( 1.0f, base->y * 1.25f );
            base->z = std::min( 1.0f, base->z * 1.25f );
            return *base;
        }
    }

    // Try with c_ prefix (some internal names omit it)
    if( auto c = lookup( "c_" + name ) ) {
        return *c;
    }

    return ImVec4( 0.80f, 0.80f, 0.80f, 1.00f );
}

void text_colored( nc_color c, const std::string &text )
{
    draw_colored_text( colorize( text, c ) );
}

void draw_colored_text( const std::string &text, nc_color /*base_fallback*/ )
{
    ImVec4 current_color( 0.80f, 0.80f, 0.80f, 1.00f );

    const auto segments = split_by_color( text );

    // The first rendered segment must NOT call SameLine: a leading SameLine(0,0)
    // snaps the cursor to the end of the previous item (e.g. a full-width
    // InvisibleButton), clobbering any SetCursorScreenPos the caller set and
    // pushing the text off-screen. Continuation between segments is what wants
    // SameLine. Callers that need this text to follow a prior item on the same
    // line call SameLine themselves before invoking us.
    bool first = true;
    for( std::string seg : segments ) {
        if( seg.empty() ) {
            continue;
        }

        // split_by_color leaves the color tag and the text that follows it in
        // the SAME segment (e.g. "<color_red>foo"). Parse the leading tag, then
        // strip it with rm_prefix and render the remaining text — do NOT skip
        // the whole segment, or the text after the tag is lost.
        if( seg[0] == '<' ) {
            if( seg.rfind( "</color>", 0 ) == 0 ) {
                current_color = ImVec4( 0.80f, 0.80f, 0.80f, 1.00f );
                seg = rm_prefix( seg );
            } else if( seg.rfind( "<color_", 0 ) == 0 ) {
                const size_t tag_close = seg.find( '>' );
                if( tag_close != std::string::npos ) {
                    std::string color_name = seg.substr( 7, tag_close - 7 );
                    current_color = lookup_color( color_name );
                }
                seg = rm_prefix( seg );
            }
        }

        if( seg.empty() ) {
            continue;
        }

        if( !first ) {
            ImGui::SameLine( 0, 0 );
        }
        ImGui::TextColored( current_color, "%s", seg.c_str() );
        first = false;
    }
}

} // namespace cataimgui
