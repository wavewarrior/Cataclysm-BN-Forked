#include "dialogue_win.h"

#include "input.h"
#include "output.h"
#include "point.h"
#include "translations.h"
#include "ui_manager.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

void dialogue_window::resize_dialogue( ui_adaptor& ui )
{
    int win_beginy = TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 4 : 0;
    int win_beginx = TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 4 : 0;
    int maxy = win_beginy ? TERMY - 2 * win_beginy : FULL_SCREEN_HEIGHT;
    int maxx = win_beginx ? TERMX - 2 * win_beginx : FULL_SCREEN_WIDTH;
    d_win = catacurses::newwin( maxy, maxx, point( win_beginx, win_beginy ) );
    ui.position_from_window( d_win );
    curr_page = 0;
    draw_cache.clear();
    for( size_t idx = 0; idx < history.size(); idx++ ) { cache_msg( history[idx], idx ); }
}


void dialogue_window::print_header( const std::string & ) {}

void dialogue_window::clear_window_texts() {}

void dialogue_window::add_to_history( const std::string& msg )
{
    size_t idx = history.size();
    history.push_back( msg );
    cache_msg( msg, idx );
}

std::string dialogue_window::history_markup() const
{
    std::string out;
    for( size_t i = 0; i < history.size(); i++ ) {
        // Highlight the last two messages (the most recent exchange), like print_history.
        const nc_color base = ( i + 2 >= history.size() ) ? c_white : c_light_gray;
        if( i != 0 ) { out += "\n\n"; }
        out += colorize( history[i], base );
    }
    return out;
}

void dialogue_window::print_history() {}


void dialogue_window::cache_msg( const std::string &, size_t ) {}

void dialogue_window::refresh_response_display()
{
    curr_page = 0;
    can_scroll_down = false;
    can_scroll_up = false;
}

std::optional<size_t> dialogue_window::handle_scrolling( const int ch )
{
    if( ch == KEY_NPAGE && can_scroll_down ) { return next_page_start; }
    if( ch == KEY_PPAGE && can_scroll_up ) { return prev_page_start; }
    return std::nullopt;
}

void dialogue_window::display_responses( const std::vector<talk_data> &, size_t ) {}
