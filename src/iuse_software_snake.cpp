#include "iuse_software_snake.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "catacharset.h"  // utf8_width()
#include "color.h"
#include "cursesdef.h"
#include "input.h"
#include "minigame_rml.h"
#include "output.h"
#include "point.h"
#include "rml_screen.h"
#include "rng.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"

snake_game::snake_game() = default;

int snake_game::start_game()
{
    std::vector<std::pair<int, int> > vSnakeBody;
    std::map<int, std::map<int, bool> > mSnakeBody;

    catacurses::window w_snake;
    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ui ) {
        const point iOffset( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                             TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 );

        w_snake = catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH,
                                      iOffset );

        ui.position_from_window( w_snake );
    } );
    ui.mark_resize();

    //Snake start position
    vSnakeBody.emplace_back( FULL_SCREEN_HEIGHT / 2, FULL_SCREEN_WIDTH / 2 );
    mSnakeBody[FULL_SCREEN_HEIGHT / 2][FULL_SCREEN_WIDTH / 2] = true;

    //Snake start direction
    int iDirY = 0;
    int iDirX = 1;

    //Snake start length
    size_t iSnakeBody = 10;

    //GameSpeed aka inputdelay/timeout
    int iGameSpeed = 100;

    //Score
    int iScore = 0;
    int iFruitPosY = 0;
    int iFruitPosX = 0;

    input_context ctxt( "SNAKE" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    // Tier 9: render through the shared char-grid RmlUi widget when enabled.
    minigame_rml::open( minigames_rmlui_enabled(), ctxt );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( minigame_rml::active() ) {
            minigame_rml::set_title( colorize( _( "S N A K E" ), c_white ) + "   " +
                                     colorize( string_format( _( "Score: %d" ), iScore ), c_white ) );
            const point head( vSnakeBody.back().second, vSnakeBody.back().first );
            std::vector<std::string> grid;
            grid.reserve( FULL_SCREEN_HEIGHT - 2 );
            for( int y = 1; y <= FULL_SCREEN_HEIGHT - 2; y++ ) {
                std::string row;
                for( int x = 1; x <= FULL_SCREEN_WIDTH - 2; x++ ) {
                    if( x == iFruitPosX && y == iFruitPosY ) {
                        row += colorize( "*", c_light_red );
                    } else if( x == head.x && y == head.y ) {
                        row += colorize( "#", c_white );
                    } else if( mSnakeBody[y][x] ) {
                        row += colorize( "#", c_light_gray );
                    } else {
                        row += ' ';
                    }
                }
                grid.push_back( row );
            }
            minigame_rml::set_grid( grid );
            minigame_rml::set_footer( _( "<q>uit" ) );
            minigame_rml::sync();
        }
    } );

    do {
        //Check if we hit a border
        if( vSnakeBody[vSnakeBody.size() - 1].first + iDirY == 0 ) {
            vSnakeBody.emplace_back( vSnakeBody[vSnakeBody.size() - 1].first +
                                     iDirY + FULL_SCREEN_HEIGHT - 2,
                                     vSnakeBody[vSnakeBody.size() - 1].second + iDirX );

        } else if( vSnakeBody[vSnakeBody.size() - 1].first + iDirY == FULL_SCREEN_HEIGHT - 1 ) {
            vSnakeBody.emplace_back( vSnakeBody[vSnakeBody.size() - 1].first +
                                     iDirY - FULL_SCREEN_HEIGHT + 2,
                                     vSnakeBody[vSnakeBody.size() - 1].second + iDirX );

        } else if( vSnakeBody[vSnakeBody.size() - 1].second + iDirX == 0 ) {
            vSnakeBody.emplace_back( vSnakeBody[vSnakeBody.size() - 1].first + iDirY,
                                     vSnakeBody[vSnakeBody.size() - 1].second +
                                     iDirX + FULL_SCREEN_WIDTH - 2 );

        } else if( vSnakeBody[vSnakeBody.size() - 1].second + iDirX == FULL_SCREEN_WIDTH - 1 ) {
            vSnakeBody.emplace_back( vSnakeBody[vSnakeBody.size() - 1].first + iDirY,
                                     vSnakeBody[vSnakeBody.size() - 1].second +
                                     iDirX - FULL_SCREEN_WIDTH + 2 );

        } else {
            vSnakeBody.emplace_back( vSnakeBody[vSnakeBody.size() - 1].first + iDirY,
                                     vSnakeBody[vSnakeBody.size() - 1].second + iDirX );
        }

        //Check if we hit ourselves
        if( mSnakeBody[vSnakeBody[vSnakeBody.size() - 1].first]
            [vSnakeBody[vSnakeBody.size() - 1].second] ) {
            //We are dead :(
            break;
        } else {
            //Add new position to map
            mSnakeBody[vSnakeBody[vSnakeBody.size() - 1].first]
            [vSnakeBody[vSnakeBody.size() - 1].second] = true;
        }

        //Have we eaten the forbidden fruit?
        if( vSnakeBody[vSnakeBody.size() - 1].first == iFruitPosY &&
            vSnakeBody[vSnakeBody.size() - 1].second == iFruitPosX ) {
            iScore += 500;
            iSnakeBody += 10;
            iGameSpeed -= 3;

            iFruitPosY = 0;
            iFruitPosX = 0;
        }

        //Check if we are longer than our max size
        if( vSnakeBody.size() > iSnakeBody ) {
            mSnakeBody[vSnakeBody[0].first][vSnakeBody[0].second] = false;
            vSnakeBody.erase( vSnakeBody.begin(), vSnakeBody.begin() + 1 );
        }

        //On full length add a fruit
        if( iFruitPosX == 0 && iFruitPosY == 0 ) {
            do {
                iFruitPosY = rng( 1, FULL_SCREEN_HEIGHT - 2 );
                iFruitPosX = rng( 1, FULL_SCREEN_WIDTH - 2 );
            } while( mSnakeBody[iFruitPosY][iFruitPosX] );
        }

        ui_manager::redraw();

        const std::string action = ctxt.handle_input( iGameSpeed );

        if( action == "UP" ) {
            if( iDirY != 1 ) {
                iDirY = -1;
                iDirX = 0;
            }
        } else if( action == "DOWN" ) {
            if( iDirY != -1 ) {
                iDirY = 1;
                iDirX = 0;
            }
        } else if( action == "LEFT" ) {
            if( iDirX != 1 ) {
                iDirY = 0;
                iDirX = -1;
            }
        } else if( action == "RIGHT" ) {
            if( iDirX != -1 ) {
                iDirY = 0;
                iDirX = 1;
            }
        } else if( action == "QUIT" ) {
            minigame_rml::close();
            return iScore;
        }

    } while( true );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( minigame_rml::active() ) {
            minigame_rml::set_title( colorize( _( "GAME OVER" ), c_light_red ) );
            minigame_rml::set_grid( { colorize( string_format( _( "TOTAL SCORE: %d" ), iScore ),
                                                c_yellow ) } );
            minigame_rml::set_footer( _( "Press 'q' or ESC to exit." ) );
            minigame_rml::sync();
        }
    } );
    do {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "QUIT" ) {
            break;
        }
    } while( true );

    minigame_rml::close();
    return iScore;
}
