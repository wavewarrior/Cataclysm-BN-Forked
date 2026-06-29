#include "iuse_software_lightson.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "cata_utility.h"
#include "catacharset.h"
#include "color.h"
#include "cursesdef.h"
#include "input.h"
#include "minigame_rml.h"
#include "output.h"
#include "point.h"
#include "rml_screen.h"
#include "rng.h"
#include "translations.h"
#include "ui_manager.h"

void lightson_game::new_level()
{
    win = false;

    // level generation
    const int half_perimeter = rng( 8, 11 );
    const int lvl_width = rng( 4, 6 );
    const int lvl_height = half_perimeter - lvl_width;
    level_size = point( lvl_width, lvl_height );
    level.resize( lvl_height * lvl_width );

    const int steps_rng = half_perimeter / 2.0 + rng_float( 0.0, 2.0 );
    generate_change_coords( steps_rng );

    reset_level();
}

void lightson_game::reset_level()
{
    std::fill( level.begin(), level.end(), true );

    // change level
    std::for_each( change_coords.begin(), change_coords.end(), [this]( point & p ) {
        toggle_lights_at( p );
    } );

    position = point_zero;
}

bool lightson_game::get_value_at( point pt )
{
    return level[pt.y * level_size.x + pt.x];
}

void lightson_game::set_value_at( point pt, bool value )
{
    level[pt.y * level_size.x + pt.x] = value;
}

void lightson_game::toggle_value_at( point pt )
{
    set_value_at( pt, !get_value_at( pt ) );
}

void lightson_game::generate_change_coords( int changes )
{
    change_coords.resize( changes );
    const int size = level_size.y * level_size.x;

    point candidate;
    for( int k = 0; k < changes; k++ ) {
        do {
            const int candidate_index = rng( 0, size - 1 );

            candidate.x = candidate_index % level_size.x;
            candidate.y = ( candidate_index - candidate.x ) / level_size.x;
            // not accept repeatable coordinates
        } while( !( k == 0 ||
                    std::find( change_coords.begin(), change_coords.end(), candidate ) == change_coords.end() ) );
        change_coords[k] = candidate;
    }
}

bool lightson_game::check_win()
{
    return std::all_of( level.begin(), level.end(), []( bool i ) {
        return i;
    } );
}

void lightson_game::toggle_lights()
{
    toggle_lights_at( position );
}

void lightson_game::toggle_lights_at( point pt )
{
    toggle_value_at( pt );

    if( pt.y > 0 ) {
        toggle_value_at( pt + point_north );
    }
    if( pt.y < level_size.y - 1 ) {
        toggle_value_at( pt + point_south );
    }

    if( pt.x > 0 ) {
        toggle_value_at( pt + point_west );
    }
    if( pt.x < level_size.x - 1 ) {
        toggle_value_at( pt + point_east );
    }
}

int lightson_game::start_game()
{
    const int w_height = 15;

    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        const point iOffset( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                             TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 );
        w_border = catacurses::newwin( w_height, FULL_SCREEN_WIDTH, iOffset );
        ui.position_from_window( w_border );
    } );
    ui.mark_resize();

    input_context ctxt( "LIGHTSON" );
    ctxt.register_directions();
    ctxt.register_action( "TOGGLE_SPACE" );
    ctxt.register_action( "TOGGLE_5" );
    ctxt.register_action( "RESET" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    // Tier 9: render through the shared char-grid RmlUi widget when enabled.
    minigame_rml::open( minigames_rmlui_enabled(), ctxt );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( minigame_rml::active() ) {
            minigame_rml::set_title( colorize( _( "Lights on!" ), c_white ) );
            std::vector<std::string> grid;
            grid.reserve( level_size.y );
            for( int i = 0; i < level_size.y; i++ ) {
                std::string row;
                for( int j = 0; j < level_size.x; j++ ) {
                    const point current( j, i );
                    const bool on = get_value_at( current );
                    const char symbol = on ? '#' : '-';
                    // Cursor = bright yellow (RML markup is foreground-only, so the
                    // curses inverse-video hilite becomes a distinct colour).
                    const nc_color fg = position == current
                                        ? c_yellow
                                        : ( on ? c_white : c_dark_gray );
                    row += colorize( std::string( 1, symbol ), fg );
                }
                grid.push_back( row );
            }
            minigame_rml::set_grid( grid );
            minigame_rml::set_footer(
                colorize( _( "Game goal:" ), c_white ) + _( " Switch all the lights on." ) + "\n" +
                colorize( _( "Legend:" ), c_white ) + " " + colorize( "#", c_white ) + _( " on, " ) +
                colorize( "-", c_dark_gray ) + _( " off." ) + "\n" +
                _( "Toggle lights switches selected light and 4 its neighbors." ) + "\n\n" +
                _( "<spacebar or 5> toggle lights   <r>eset   <q>uit" ) );
            minigame_rml::sync();
        }
    } );

    win = true;
    int hasWon = 0;

    do {
        if( win ) {
            new_level();
        }
        ui_manager::redraw();
        std::string action = ctxt.handle_input();
        if( const std::optional<tripoint_rel_ms> vec = ctxt.get_direction( action ) ) {
            position.y = clamp( position.y + vec->y(), 0, level_size.y - 1 );
            position.x = clamp( position.x + vec->x(), 0, level_size.x - 1 );
        } else if( action == "TOGGLE_SPACE" || action == "TOGGLE_5" ) {
            toggle_lights();
            win = check_win();
            if( win ) {
                ui.invalidate_ui();
                popup_top( _( "Congratulations, you won!" ) );
                hasWon++;
            }
        } else if( action == "RESET" ) {
            reset_level();
        } else if( action == "QUIT" ) {
            break;
        }
    } while( true );

    minigame_rml::close();
    return hasWon;
}
