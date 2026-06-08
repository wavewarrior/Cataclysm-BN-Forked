#include "sdl_input.h"

#include <algorithm>
#include <limits>
#include <string>

#include "cached_options.h" // test_mode
#include "cursesdef.h" // KEY_*, KEY_F, KEY_NUM, catacurses::stdscr, wrefresh
#include "options.h"   // get_option

// File-scope state for ALT+nnnn code entry and arrow-key combos.
// These were file-scope statics in sdltiles.cpp and stay file-scope here.
static int alt_buffer = 0;
static bool alt_down = false;
static int arrow_combo_modifier = 0;

static constexpr int ERR = -1;

namespace {

auto sdl_keycode_opposite_arrow( SDL_Keycode key ) -> SDL_Keycode
{
    switch( key ) {
        case SDLK_UP:
            return SDLK_DOWN;
        case SDLK_DOWN:
            return SDLK_UP;
        case SDLK_LEFT:
            return SDLK_RIGHT;
        case SDLK_RIGHT:
            return SDLK_LEFT;
    }
    return 0;
}

auto sdl_keycode_is_arrow( SDL_Keycode key ) -> bool
{
    return static_cast<bool>( sdl_keycode_opposite_arrow( key ) );
}

auto arrow_combo_to_numpad( SDL_Keycode mod, SDL_Keycode key ) -> int
{
    if( ( mod == SDLK_UP    && key == SDLK_RIGHT ) ||
        ( mod == SDLK_RIGHT && key == SDLK_UP ) ) {
        return KEY_NUM( 9 );
    }
    if( ( mod == SDLK_UP    && key == SDLK_UP ) ) {
        return KEY_NUM( 8 );
    }
    if( ( mod == SDLK_UP    && key == SDLK_LEFT ) ||
        ( mod == SDLK_LEFT  && key == SDLK_UP ) ) {
        return KEY_NUM( 7 );
    }
    if( ( mod == SDLK_RIGHT && key == SDLK_RIGHT ) ) {
        return KEY_NUM( 6 );
    }
    if( mod == sdl_keycode_opposite_arrow( key ) ) {
        return KEY_NUM( 5 );
    }
    if( ( mod == SDLK_LEFT  && key == SDLK_LEFT ) ) {
        return KEY_NUM( 4 );
    }
    if( ( mod == SDLK_DOWN  && key == SDLK_RIGHT ) ||
        ( mod == SDLK_RIGHT && key == SDLK_DOWN ) ) {
        return KEY_NUM( 3 );
    }
    if( ( mod == SDLK_DOWN  && key == SDLK_DOWN ) ) {
        return KEY_NUM( 2 );
    }
    if( ( mod == SDLK_DOWN  && key == SDLK_LEFT ) ||
        ( mod == SDLK_LEFT  && key == SDLK_DOWN ) ) {
        return KEY_NUM( 1 );
    }
    return 0;
}

} // namespace

namespace sdl_input {

void begin_alt_code()
{
    alt_buffer = 0;
    alt_down = true;
}

auto add_alt_code( char c ) -> bool
{
    if( alt_down ) {
        if( c >= '0' && c <= '9' ) {
            alt_buffer = alt_buffer * 10 + ( c - '0' );
        }

        // Hardcoded alt-tab check. TODO: Handle alt keys properly
        if( c == '\t' ) {
            return true;
        }
    }
    return false;
}

auto end_alt_code() -> int
{
    alt_down = false;
    return alt_buffer;
}

auto handle_dpad( display_context &d ) -> int
{
    // Check if we have a gamepad d-pad event.
    if( SDL_GetJoystickHat( d.joystick, 0 ) != SDL_HAT_CENTERED ) {
        // When someone tries to press a diagonal, they likely will
        // press a single direction first. Wait a few milliseconds to
        // give them time to press both of the buttons for the diagonal.
        int button = SDL_GetJoystickHat( d.joystick, 0 );
        int lc = ERR;
        if( button == SDL_HAT_LEFT ) {
            lc = JOY_LEFT;
        } else if( button == SDL_HAT_DOWN ) {
            lc = JOY_DOWN;
        } else if( button == SDL_HAT_RIGHT ) {
            lc = JOY_RIGHT;
        } else if( button == SDL_HAT_UP ) {
            lc = JOY_UP;
        } else if( button == SDL_HAT_LEFTUP ) {
            lc = JOY_LEFTUP;
        } else if( button == SDL_HAT_LEFTDOWN ) {
            lc = JOY_LEFTDOWN;
        } else if( button == SDL_HAT_RIGHTUP ) {
            lc = JOY_RIGHTUP;
        } else if( button == SDL_HAT_RIGHTDOWN ) {
            lc = JOY_RIGHTDOWN;
        }

        if( d.delaydpad == std::numeric_limits<Uint64>::max() ) {
            d.delaydpad = SDL_GetTicks() + d.dpad_delay;
            d.queued_dpad = lc;
        }

        // Okay it seems we're ready to process.
        if( SDL_GetTicks() > d.delaydpad ) {

            if( lc != ERR ) {
                if( d.dpad_continuous && ( lc & d.lastdpad ) == 0 ) {
                    // Continuous movement should only work in the same or similar directions.
                    d.dpad_continuous = false;
                    d.lastdpad = lc;
                    return 0;
                }

                d.last_input = input_event( lc, input_event_t::gamepad );
                d.lastdpad = lc;
                d.queued_dpad = ERR;

                if( !d.dpad_continuous ) {
                    d.delaydpad = SDL_GetTicks() + 200;
                    d.dpad_continuous = true;
                } else {
                    d.delaydpad = SDL_GetTicks() + 60;
                }
                return 1;
            }
        }
    } else {
        d.dpad_continuous = false;
        d.delaydpad = std::numeric_limits<Uint64>::max();

        // If we didn't hold it down for a while, just
        // fire the last registered press.
        if( d.queued_dpad != ERR ) {
            d.last_input = input_event( d.queued_dpad, input_event_t::gamepad );
            d.queued_dpad = ERR;
            return 1;
        }
    }

    return 0;
}

auto keysym_to_curses( SDL_Keycode sym, SDL_Keymod mod ) -> int
{
    if( sym >= SDLK_KP_1 && sym <= SDLK_KP_0 ) {
        return 0;
    }

    const std::string diag_mode = get_option<std::string>( "DIAG_MOVE_WITH_MODIFIERS_MODE" );

    if( diag_mode == "mode1" ) {
        if( mod & SDL_KMOD_CTRL && sdl_keycode_is_arrow( sym ) ) {
            return handle_arrow_combo( sym );
        } else {
            end_arrow_combo();
        }
    }

    if( diag_mode == "mode2" ) {
        //Shift + Cursor Arrow (diagonal clockwise)
        if( mod & SDL_KMOD_SHIFT ) {
            switch( sym ) {
                case SDLK_LEFT:
                    return inp_mngr.get_first_char_for_action( "LEFTUP" );
                case SDLK_RIGHT:
                    return inp_mngr.get_first_char_for_action( "RIGHTDOWN" );
                case SDLK_UP:
                    return inp_mngr.get_first_char_for_action( "RIGHTUP" );
                case SDLK_DOWN:
                    return inp_mngr.get_first_char_for_action( "LEFTDOWN" );
            }
        }
        //Ctrl + Cursor Arrow (diagonal counter-clockwise)
        if( mod & SDL_KMOD_CTRL ) {
            switch( sym ) {
                case SDLK_LEFT:
                    return inp_mngr.get_first_char_for_action( "LEFTDOWN" );
                case SDLK_RIGHT:
                    return inp_mngr.get_first_char_for_action( "RIGHTUP" );
                case SDLK_UP:
                    return inp_mngr.get_first_char_for_action( "LEFTUP" );
                case SDLK_DOWN:
                    return inp_mngr.get_first_char_for_action( "RIGHTDOWN" );
            }
        }
    }

    if( diag_mode == "mode3" ) {
        //Shift + Cursor Left/RightArrow
        if( mod & SDL_KMOD_SHIFT ) {
            switch( sym ) {
                case SDLK_LEFT:
                    return inp_mngr.get_first_char_for_action( "LEFTUP" );
                case SDLK_RIGHT:
                    return inp_mngr.get_first_char_for_action( "RIGHTUP" );
            }
        }
        //Ctrl + Cursor Left/Right Arrow
        if( mod & SDL_KMOD_CTRL ) {
            switch( sym ) {
                case SDLK_LEFT:
                    return inp_mngr.get_first_char_for_action( "LEFTDOWN" );
                case SDLK_RIGHT:
                    return inp_mngr.get_first_char_for_action( "RIGHTDOWN" );
            }
        }
    }

    if( mod & SDL_KMOD_CTRL && sym >= 'a' && sym <= 'z' ) {
        // ASCII ctrl codes, ^A through ^Z.
        return sym - 'a' + '\1';
    }
    switch( sym ) {
        // This is special: allow entering a Unicode character with ALT+number
        case SDLK_RALT:
        case SDLK_LALT:
            begin_alt_code();
            return -1;
        // The following are simple translations:
        case SDLK_KP_ENTER:
        case SDLK_RETURN:
        case SDLK_RETURN2:
            return '\n';
        case SDLK_BACKSPACE:
        case SDLK_KP_BACKSPACE:
            return KEY_BACKSPACE;
        case SDLK_DELETE:
            return KEY_DC;
        case SDLK_ESCAPE:
            return KEY_ESCAPE;
        case SDLK_TAB:
            if( mod & SDL_KMOD_SHIFT ) {
                return KEY_BTAB;
            }
            return '\t';
        case SDLK_LEFT:
            return KEY_LEFT;
        case SDLK_RIGHT:
            return KEY_RIGHT;
        case SDLK_UP:
            return KEY_UP;
        case SDLK_DOWN:
            return KEY_DOWN;
        case SDLK_PAGEUP:
            return KEY_PPAGE;
        case SDLK_PAGEDOWN:
            return KEY_NPAGE;
        case SDLK_HOME:
            return KEY_HOME;
        case SDLK_END:
            return KEY_END;
        case SDLK_F1:
            return KEY_F( 1 );
        case SDLK_F2:
            return KEY_F( 2 );
        case SDLK_F3:
            return KEY_F( 3 );
        case SDLK_F4:
            return KEY_F( 4 );
        case SDLK_F5:
            return KEY_F( 5 );
        case SDLK_F6:
            return KEY_F( 6 );
        case SDLK_F7:
            return KEY_F( 7 );
        case SDLK_F8:
            return KEY_F( 8 );
        case SDLK_F9:
            return KEY_F( 9 );
        case SDLK_F10:
            return KEY_F( 10 );
        case SDLK_F11:
            return KEY_F( 11 );
        case SDLK_F12:
            return KEY_F( 12 );
        case SDLK_F13:
            return KEY_F( 13 );
        case SDLK_F14:
            return KEY_F( 14 );
        case SDLK_F15:
            return KEY_F( 15 );
        // Every other key is ignored as there is no curses constant for it.
        // TODO: add more if you find more.
        default:
            return 0;
    }
}

auto handle_arrow_combo( SDL_Keycode key ) -> int
{
    if( !arrow_combo_modifier ) {
        arrow_combo_modifier = key;
        return 0;
    }
    return arrow_combo_to_numpad( arrow_combo_modifier, key );
}

void end_arrow_combo()
{
    arrow_combo_modifier = 0;
}

auto gamepad_available( const display_context &d ) -> bool
{
    return d.joystick != nullptr;
}

// Dispatch overrides — these are declared in input.h as input_manager member
// functions.  They live here because they form the SDL input pipeline.
} // namespace sdl_input

void input_manager::set_timeout( const int t )
{
    input_timeout = t;
    g_display.inputdelay = t;
}

void input_manager::pump_events()
{
    if( test_mode ) {
        return;
    }

    // Handle all events, but ignore any keypress
    sdl_input::CheckMessages( g_display );

    g_display.last_input = input_event();
    previously_pressed_key = 0;
}

input_event input_manager::get_input_event()
{
    previously_pressed_key = 0;

    // standards note: getch is sometimes required to call refresh
    // see, e.g., http://linux.die.net/man/3/getch
    // so although it's non-obvious, that refresh() call (and maybe InvalidateRect?) IS supposed to be there
    // however, the refresh call has not effect when nothing has been drawn, so
    // we can skip it if `needupdate` is false to improve performance during mouse
    // move events.
    if( g_display.needupdate ) {
        wrefresh( catacurses::stdscr );
    }

    if( g_display.inputdelay < 0 ) {
        do {
            sdl_input::CheckMessages( g_display );
            if( g_display.last_input.type != input_event_t::error ) {
                break;
            }
            SDL_Delay( 1 );
        } while( g_display.last_input.type == input_event_t::error );
    } else if( g_display.inputdelay > 0 ) {
        Uint64 starttime = SDL_GetTicks();
        Uint64 endtime = 0;
        bool timedout = false;
        do {
            sdl_input::CheckMessages( g_display );
            endtime = SDL_GetTicks();
            if( g_display.last_input.type != input_event_t::error ) {
                break;
            }
            SDL_Delay( 1 );
            timedout = endtime >= starttime + g_display.inputdelay;
            if( timedout ) {
                g_display.last_input.type = input_event_t::timeout;
            }
        } while( !timedout );
    } else {
        sdl_input::CheckMessages( g_display );
    }

    if( g_display.last_input.type == input_event_t::mouse ) {
        float mx, my;
        SDL_GetMouseState( &mx, &my );
        g_display.last_input.mouse_pos.x = static_cast<int>( mx );
        g_display.last_input.mouse_pos.y = static_cast<int>( my );
    } else if( g_display.last_input.type == input_event_t::keyboard ) {
        previously_pressed_key = g_display.last_input.get_first_input();
    }

    return g_display.last_input;
}
