#include "iuse_software_sokoban.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>

#include "cata_utility.h"
#include "catacharset.h"
#include "color.h"
#include "cursesdef.h"
#include "fstream_utils.h"
#include "input.h"
#include "minigame_rml.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "rml_screen.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"

sokoban_game::sokoban_game() = default;

void sokoban_game::parse_level( std::istream &fin )
{
    /*
    # Wall
    $ Package
    space Floor
    . Goal
    * Package on Goal
    @ Sokoban
    + Sokoban on Goal
    */

    iCurrentLevel = 0;
    iNumLevel = 0;

    vLevel.clear();
    vUndo.clear();
    vLevelDone.clear();

    std::string sLine;
    while( !fin.eof() ) {
        safe_getline( fin, sLine );

        if( sLine.substr( 0, 3 ) == "; #" ) {
            iNumLevel++;
            continue;
        } else if( sLine[0] == ';' ) {
            continue;
        }

        if( sLine.empty() ) {
            //Find level start
            vLevel.resize( iNumLevel + 1 );
            vLevelDone.resize( iNumLevel + 1 );
            mLevelInfo[iNumLevel]["MaxLevelY"] = 0;
            mLevelInfo[iNumLevel]["MaxLevelX"] = 0;
            mLevelInfo[iNumLevel]["PlayerY"] = 0;
            mLevelInfo[iNumLevel]["PlayerX"] = 0;
            continue;
        }

        if( mLevelInfo[iNumLevel]["MaxLevelX"] < sLine.length() ) {
            mLevelInfo[iNumLevel]["MaxLevelX"] = sLine.length();
        }

        for( size_t i = 0; i < sLine.length(); i++ ) {
            if( sLine[i] == '@' ) {
                if( mLevelInfo[iNumLevel]["PlayerY"] == 0 && mLevelInfo[iNumLevel]["PlayerX"] == 0 ) {
                    mLevelInfo[iNumLevel]["PlayerY"] = mLevelInfo[iNumLevel]["MaxLevelY"];
                    mLevelInfo[iNumLevel]["PlayerX"] = i;
                } else {
                    // TODO: describe why it's invalid
                    throw std::runtime_error( "invalid content of sokoban file" );
                }
            }

            if( sLine[i] == '.' || sLine[i] == '*' || sLine[i] == '+' ) {
                vLevelDone[iNumLevel].emplace_back( static_cast<int>
                                                    ( mLevelInfo[iNumLevel]["MaxLevelY"] ), static_cast<int>( i ) );
            }

            vLevel[iNumLevel][mLevelInfo[iNumLevel]["MaxLevelY"]][i] = sLine[i];
        }

        mLevelInfo[iNumLevel]["MaxLevelY"]++;
    }
}

bool sokoban_game::check_win()
{
    for( auto &elem : vLevelDone[iCurrentLevel] ) {
        if( mLevel[elem.first][elem.second] != "*" ) {
            return false;
        }
    }
    return true;
}

int sokoban_game::start_game()
{
    int iScore = 0;
    int iMoves = 0;
    iTotalMoves = 0;

    point dir;

    using namespace std::placeholders;
    read_from_file( PATH_INFO::sokoban(), std::bind( &sokoban_game::parse_level, this, _1 ) );

    catacurses::window w_sokoban;
    ui_adaptor ui;
    ui.on_screen_resize( [&]( ui_adaptor & ) {
        const point iOffset( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                             TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 );
        w_sokoban = catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH,
                                        iOffset );
        ui.position_from_window( w_sokoban );
    } );
    ui.mark_resize();

    input_context ctxt( "SOKOBAN" );
    ctxt.register_cardinal();
    ctxt.register_action( "NEXT" );
    ctxt.register_action( "PREV" );
    ctxt.register_action( "RESET" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "UNDO" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    // Tier 9: render through the shared char-grid RmlUi widget when enabled.
    minigame_rml::open( minigames_rmlui_enabled(), ctxt );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        if( minigame_rml::active() ) {
            minigame_rml::set_title(
                colorize( string_format( _( "Sokoban  Level: %d/%d  Score: %d  Moves: %d  Total: %d" ),
                                         iCurrentLevel + 1, iNumLevel, iScore, iMoves, iTotalMoves ),
                          c_white ) );
            const int maxx = mLevelInfo[iCurrentLevel]["MaxLevelX"];
            const int maxy = mLevelInfo[iCurrentLevel]["MaxLevelY"];
            std::vector<std::string> grid;
            grid.reserve( maxy );
            for( int y = 0; y < maxy; y++ ) {
                std::string row;
                const auto rowit = mLevel.find( y );
                for( int x = 0; x < maxx; x++ ) {
                    std::string tile = " ";
                    if( rowit != mLevel.end() ) {
                        const auto cellit = rowit->second.find( x );
                        if( cellit != rowit->second.end() && !cellit->second.empty() ) {
                            tile = cellit->second;
                        }
                    }
                    // RML markup is foreground-only, so the curses red-background goal
                    // tiles become distinct foreground colours; walls are '#' (the curses
                    // box-drawing connection glyphs don't port to the RmlUi font).
                    if( tile == "#" ) {
                        row += colorize( "#", c_white );
                    } else if( tile == "@" ) {
                        row += colorize( "@", c_light_cyan );
                    } else if( tile == "+" ) {
                        row += colorize( "@", c_light_green );
                    } else if( tile == "$" ) {
                        row += colorize( "$", c_brown );
                    } else if( tile == "*" ) {
                        row += colorize( "*", c_green );
                    } else if( tile == "." ) {
                        row += colorize( ".", c_red );
                    } else {
                        row += ' ';
                    }
                }
                grid.push_back( row );
            }
            minigame_rml::set_grid( grid );
            minigame_rml::set_footer(
                _( "<+> next   <-> prev   <r>eset   <u>ndo move   <q>uit" ) );
            minigame_rml::sync();
        }
    } );

    point pl;

    bool bNewLevel = true;
    bool bMoved = false;
    do {
        if( bNewLevel ) {
            bNewLevel = false;

            iMoves = 0;
            vUndo.clear();

            pl.y = mLevelInfo[iCurrentLevel]["PlayerY"];
            pl.x = mLevelInfo[iCurrentLevel]["PlayerX"];
            mLevel = vLevel[iCurrentLevel];
        }

        std::string action;
        if( check_win() ) {
            //we won yay
            if( !mAlreadyWon[iCurrentLevel] ) {
                iScore += 500;
                mAlreadyWon[iCurrentLevel] = true;
            }
            action = "NEXT";

        } else {
            ui_manager::redraw();
            //Check input
            action = ctxt.handle_input();
        }

        bMoved = false;
        if( const std::optional<tripoint_rel_ms> vec = ctxt.get_direction( action ) ) {
            dir = vec->xy().raw();
            bMoved = true;
        } else if( action == "QUIT" ) {
            minigame_rml::close();
            return iScore;
        } else if( action == "UNDO" ) {
            point pl_new;
            bool bUndoSkip = false;
            //undo move
            if( !vUndo.empty() ) {
                //reset last player pos
                mLevel[pl.y][pl.x] = mLevel[pl.y][pl.x] == "+" ? "." : " ";
                pl_new = vUndo[vUndo.size() - 1].old;
                mLevel[pl_new.y][pl_new.x] = vUndo[vUndo.size() - 1].sTileOld;

                vUndo.pop_back();

                bUndoSkip = true;
            }

            if( bUndoSkip && !vUndo.empty() ) {
                dir = vUndo[vUndo.size() - 1].old;

                if( vUndo[vUndo.size() - 1].sTileOld == "$" ||
                    vUndo[vUndo.size() - 1].sTileOld == "*" ) {
                    mLevel[pl.y][pl.x] = mLevel[pl.y][pl.x] == "." ? "*" : "$";
                    point np = pl + dir;
                    mLevel[np.y][np.x] = mLevel[np.y][np.x] == "*" ?
                                         "." : " ";

                    vUndo.pop_back();
                }
            }

            if( bUndoSkip ) {
                pl = pl_new;
            }
        } else if( action == "RESET" ) {
            //reset level
            bNewLevel = true;
        } else if( action == "NEXT" ) {
            //next level
            iCurrentLevel++;
            if( iCurrentLevel >= iNumLevel ) {
                iCurrentLevel = 0;
            }
            bNewLevel = true;
        } else if( action == "PREV" ) {
            //previous level
            iCurrentLevel--;
            if( iCurrentLevel < 0 ) {
                iCurrentLevel = iNumLevel - 1;
            }
            bNewLevel = true;
        }

        if( bMoved ) {
            //check if we can move the player
            std::string sMoveTo = mLevel[pl.y + dir.y][pl.x + dir.x];
            bool bMovePlayer = false;

            if( sMoveTo != "#" ) {
                if( sMoveTo == "$" || sMoveTo == "*" ) {
                    //Check if we can move the package
                    point p_pack = pl + dir * 2;
                    std::string sMovePackTo = mLevel[p_pack.y][p_pack.x];
                    if( sMovePackTo == "." || sMovePackTo == " " ) {
                        //move both
                        bMovePlayer = true;
                        mLevel[p_pack.y][p_pack.x] = sMovePackTo == "." ? "*" : "$";

                        vUndo.emplace_back( dir, sMoveTo );

                        iMoves--;
                    }
                } else {
                    bMovePlayer = true;
                }

                if( bMovePlayer ) {
                    //move player
                    vUndo.emplace_back( pl, mLevel[pl.y][pl.x] );

                    mLevel[pl.y][pl.x] = mLevel[pl.y][pl.x] == "+" ? "." : " ";
                    mLevel[pl.y + dir.y][pl.x + dir.x] = sMoveTo == "." || sMoveTo == "*" ? "+" : "@";

                    pl += dir;

                    iMoves++;
                    iTotalMoves++;
                }
            }
        }

    } while( true );

    return iScore;
}
