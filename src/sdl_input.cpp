#include "sdl_input.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include "cached_options.h" // test_mode
#include "catacharset.h"
#include "cata_utility.h"   // restore_on_out_of_scope
#include "cuboid_rectangle.h"
#include "cursesdef.h" // KEY_*, KEY_F, KEY_NUM, catacurses::stdscr, wrefresh
#include "game_ui.h"    // reinitialize_framebuffer
#include "lighting/rmlui_layer.h"
#include "lighting/sprite_batcher.h" // lighting::debug_params
#include "options.h"   // get_option
#include "output.h"    // refresh_display
#include "runtime_handlers.h"
#include "sdltiles.h"  // handle_resize
#include "ui_manager.h" // ui_manager::invalidate, ui_manager::redraw_invalidated
#include "sdl_lighting_devui.h"
#include "debug.h"

#define dbg(x) DebugLogFL((x),DC::Main)

// File-scope state for ALT+nnnn code entry and arrow-key combos.
// These were file-scope statics in sdltiles.cpp and stay file-scope here.
static int alt_buffer = 0;
static bool alt_down = false;
static int arrow_combo_modifier = 0;

static constexpr int ERR = -1;

namespace
{

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

namespace sdl_input
{

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

} // namespace sdl_input

namespace sdl_input
{

// ---------------------------------------------------------------------------
// try_sdl_update — throttle display refreshes to the framerate interval.
// These were file-scope statics in sdltiles.cpp.  lastupdate / interval are in
// display_context; refresh_display is declared in output.h.
// ---------------------------------------------------------------------------
static void try_sdl_update( display_context &d )
{
    const Uint64 now = SDL_GetTicks();
    if( now - d.lastupdate >= d.interval ) {
        refresh_display();
    } else {
        d.needupdate = true;
    }
}

// ---------------------------------------------------------------------------
// CheckMessages — main SDL event loop.
//
// CheckMessages — main SDL event loop.
//
// Polls SDL_PollEvent, dispatches input to the ImGui dev layer first (so
// the F4 tuning panel catches mouse/keyboard before the game does), then
// routes to the curses / game input pipeline.  Also handles window resize,
// render-target reset, debug F-key toggles (F5–F12), and Android text input.
//
// Defined here because the input pipeline (pump_events / get_input_event in
// this same file) is the sole caller.  The debug F-key coupling and ImGui
// dependency are acceptable for now; a future extraction could introduce a
// callback-based hook for the ~30 lines of F-key logic.
// ---------------------------------------------------------------------------
// Last cursor position seen in an SDL motion event, in window pixels. Persistent
// on purpose: display_context::last_input is wiped at the top of CheckMessages and
// overwritten by every keyboard event, so it cannot answer "where is the cursor"
// inside a keyboard-driven modal loop. Read via get_tracked_mouse_pos().
namespace
{
point s_last_mouse_px = point_zero;
// Right mouse button down/up, tracked from the SDL event stream for the same
// reason as the cursor position above: SDL_GetMouseState() only reports state
// while a window holds mouse focus, and inside the ranged-targeting modal loop it
// comes back empty — which made hold-to-aim never observe the release and behave
// like a toggle. The event stream is authoritative regardless of focus.
bool s_rmb_down = false;
// Physically-held NON-MODIFIER keys, tracked from the event stream for the same
// reason as s_rmb_down. Indexed by SDL scancode; 512 covers SDL_SCANCODE_COUNT
// with a bounds check below, so the array stays valid if SDL grows the enum.
constexpr size_t SCANCODE_SLOTS = 512;
std::array<bool, SCANCODE_SLOTS> s_key_down{};
int s_non_mod_keys_held = 0;

auto is_modifier_scancode( uint32_t sc ) -> bool
{
    switch( sc ) {
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT:
        case SDL_SCANCODE_LGUI:
        case SDL_SCANCODE_RGUI:
        case SDL_SCANCODE_MODE:
            return true;
        default:
            return false;
    }
}

// Edge-triggered, so SDL key repeat cannot inflate the count.
void note_key_state( uint32_t sc, bool down )
{
    if( sc >= SCANCODE_SLOTS || is_modifier_scancode( sc ) ) { return; }
    if( s_key_down[sc] == down ) { return; }
    s_key_down[sc] = down;
    s_non_mod_keys_held += down ? 1 : -1;
}

// SDL delivers no key-up while the window is unfocused, so an alt-tab mid-hold
// would strand the count above zero forever and a hold-to-close UI would never
// close. Treat losing focus as "everything came up".
void forget_held_keys()
{
    s_key_down.fill( false );
    s_non_mod_keys_held = 0;
}
} // namespace

auto last_mouse_px() -> point
{
    return s_last_mouse_px;
}

auto rmb_down() -> bool
{
    return s_rmb_down;
}

auto non_modifier_keys_held() -> int
{
    return s_non_mod_keys_held;
}

void CheckMessages( display_context &d )
{
    static int frame = 0;
    if( ++frame % 120 == 0 ) {
        dbg( DL::Info ) << "[input] CheckMessages called, frame=" << frame;
    }
    SDL_Event ev;
    bool quit = false;
    bool text_refresh = false;
    bool is_repeat = false;
    if( handle_dpad( d ) ) {
        return;
    }

    d.last_input = input_event();

    std::optional<point> resize_dims;
    bool render_target_reset = false;

    while( SDL_PollEvent( &ev ) ) {
        static uint64_t event_count = 0;
        if( ++event_count % 60 == 0 ) {
            dbg( DL::Info ) << "[input] SDL_PollEvent processed " << event_count << " events";
        }
        // Physical right-button state, recorded BEFORE any capture gate: the RmlUi
        // layer and the dev-panel click handlers below all `continue` past the switch,
        // so a release consumed by an open document would otherwise leave this latched
        // on and hold-to-aim could never end. Buttons are hardware state, not an
        // action — whoever handles the event, the button really did move.
        if( ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_RIGHT ) {
            s_rmb_down = true;
        } else if( ev.type == SDL_EVENT_MOUSE_BUTTON_UP && ev.button.button == SDL_BUTTON_RIGHT ) {
            s_rmb_down = false;
        } else if( ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP ) {
            // Same placement rationale as the buttons above: recorded BEFORE the
            // capture gate, so a release swallowed by an open RmlUi document still
            // clears the hold.
            note_key_state( ev.key.scancode, ev.type == SDL_EVENT_KEY_DOWN );
        } else if( ev.type == SDL_EVENT_WINDOW_FOCUS_LOST ) {
            forget_held_keys();
        }
        // RmlUi layer sees events first; captures mouse (only) while a menu is open.
        const bool rmlui_capture = rmlui_layer::process_event( ev );
        // Open/close toggle (F4) — handled BEFORE the capture gate so the panel
        // can always be closed. P0 dev key; revisit for collisions when promoting
        // past P0.
        if( ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_F4 ) {
            sdl_lighting_devui::devui_visible() = !sdl_lighting_devui::devui_visible();
            d.needupdate = true;
            continue;
        }
        // Dev test-light placement: a world click (not over the RmlUi panel) while the
        // dev panel is open with place-mode on drops a static light. Was the ImGui mouse
        // path; consume the click so it doesn't also trigger a game action.
        if( !rmlui_capture && ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            ev.button.button == SDL_BUTTON_LEFT ) {
            dbg( DL::Info ) << "[light_vis] LEFT_CLICK: rmlui_capture=" << rmlui_capture
                            << " calling place_test_light()";
            if( sdl_lighting_devui::place_test_light() ) {
                d.needupdate = true;
                continue;
            }
        }
        // Dev test-sound placement: a world click while sound place-mode is on drops a
        // test sound at that location. Consume the click so it doesn't trigger a game action.
        if( !rmlui_capture && ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            ev.button.button == SDL_BUTTON_LEFT ) {
            dbg( DL::Info ) << "[sound_vis] LEFT_CLICK: rmlui_capture=" << rmlui_capture
                            << " calling place_test_sound()";
            if( sdl_lighting_devui::place_test_sound() ) {
                d.needupdate = true;
                continue;
            }
        }
        // RmlUi consumed this mouse/keyboard event — keep it out of game.
        if( rmlui_capture ) {
            continue;
        }
        switch( ev.type ) {
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                break;
            case SDL_EVENT_WINDOW_EXPOSED:
                d.needupdate = true;
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                resize_dims = point( ev.window.data1, ev.window.data2 );
                break;
            case SDL_EVENT_RENDER_TARGETS_RESET:
                render_target_reset = true;
                break;
            case SDL_EVENT_KEY_DOWN: {
                is_repeat = ev.key.repeat;
                //hide mouse cursor on keyboard input
                if( get_option<std::string>( "HIDE_CURSOR" ) != "show" && SDL_CursorVisible() ) {
                    SDL_HideCursor();
                }
                const int lc = keysym_to_curses( ev.key.key, ev.key.mod );
                // Debug/tuning F-key handlers
                if( lc == KEY_F( 5 ) ) {
                    // F5: toggle debug HUD display
                    g_dbg_lighting = !g_dbg_lighting;
                    break;
                } else if( lc == KEY_F( 6 ) ) {
                    // F6: toggle shader debug visualization
                    g_dbg_lighting_shader = !g_dbg_lighting_shader;
                    break;
                } else if( lc == KEY_F( 7 ) ) {
                    // F7: cycle debug visualization mode (0-16). Modes include:
                    // 8 = B/W emitter-only (bypasses tint for main-menu blue),
                    // 9 = surface normal (Sobel), 10 = AO, 11 = shadow mask (game tiles only),
                    // 15 = vision frontier (frontier_cov), 16 = light_mode
                    // (red=unlit, green=gpu_lit, blue=memory).
                    g_current_dbg_mode = ( g_current_dbg_mode + 1 ) % 17u;
                    g_dbg_params.debug_mode = g_current_dbg_mode;
                    break;
                } else if( lc == KEY_F( 8 ) ) {
                    // F8: decrease emitter/sun/sky scales.
                    // Shift+F8: less dither.  Ctrl+F8: fewer dither bands.
                    using namespace lighting_dbg_range;
                    if( ev.key.mod & SDL_KMOD_ALT ) {
                        g_dbg_params.gi_strength =
                            std::max( GI_MIN, g_dbg_params.gi_strength - GI_STEP );
                    } else if( ev.key.mod & SDL_KMOD_CTRL ) {
                        g_dbg_params.dither_bands =
                            std::max( DBND_MIN, g_dbg_params.dither_bands - DBND_STEP );
                    } else if( ev.key.mod & SDL_KMOD_SHIFT ) {
                        g_dbg_params.dither_amt =
                            std::max( DAMT_MIN, g_dbg_params.dither_amt - DAMT_STEP );
                    } else {
                        g_dbg_params.emitter_scale =
                            std::max( SCALE_MIN, g_dbg_params.emitter_scale - SCALE_STEP );
                        g_dbg_params.sun_scale =
                            std::max( SCALE_MIN, g_dbg_params.sun_scale - SCALE_STEP );
                        g_dbg_params.sky_scale =
                            std::max( SCALE_MIN, g_dbg_params.sky_scale - SCALE_STEP );
                    }
                    break;
                } else if( lc == KEY_F( 9 ) ) {
                    // F9: increase emitter/sun/sky scales.
                    // Shift+F9: more dither.  Ctrl+F9: more dither bands.
                    using namespace lighting_dbg_range;
                    if( ev.key.mod & SDL_KMOD_ALT ) {
                        g_dbg_params.gi_strength =
                            std::min( GI_MAX, g_dbg_params.gi_strength + GI_STEP );
                    } else if( ev.key.mod & SDL_KMOD_CTRL ) {
                        g_dbg_params.dither_bands =
                            std::min( DBND_MAX, g_dbg_params.dither_bands + DBND_STEP );
                    } else if( ev.key.mod & SDL_KMOD_SHIFT ) {
                        g_dbg_params.dither_amt =
                            std::min( DAMT_MAX, g_dbg_params.dither_amt + DAMT_STEP );
                    } else {
                        g_dbg_params.emitter_scale =
                            std::min( SCALE_MAX, g_dbg_params.emitter_scale + SCALE_STEP );
                        g_dbg_params.sun_scale =
                            std::min( SCALE_MAX, g_dbg_params.sun_scale + SCALE_STEP );
                        g_dbg_params.sky_scale =
                            std::min( SCALE_MAX, g_dbg_params.sky_scale + SCALE_STEP );
                    }
                    break;
                } else if( lc == KEY_F( 10 ) ) {
                    // F10: menu emitter radius input ± 100 (Shift = down).
                    // Input feeds make_omni; HUD shows 3·√r as actual radius.
                    const bool shift = ( ev.key.mod & SDL_KMOD_SHIFT ) != 0;
                    const float step = shift ? -100.0f : 100.0f;
                    menu_emitter_tuning::radius_input = std::clamp(
                                                            menu_emitter_tuning::radius_input + step, 1.0f, 10000.0f );
                    break;
                } else if( lc == KEY_F( 11 ) ) {
                    // F11: cycle menu emitter position preset.
                    // 0 top-left (8.5, 4.5) — current default
                    // 1 screen centre (~40, 22) for a 80×45 tile viewport
                    // 2 bottom-right (~70, 38)
                    menu_emitter_tuning::pos_preset =
                        ( menu_emitter_tuning::pos_preset + 1 ) % 3;
                    switch( menu_emitter_tuning::pos_preset ) {
                        case 0:
                            menu_emitter_tuning::pos_x = 8.5f;
                            menu_emitter_tuning::pos_y = 4.5f;
                            break;
                        case 1:
                            menu_emitter_tuning::pos_x = 40.0f;
                            menu_emitter_tuning::pos_y = 22.0f;
                            break;
                        case 2:
                            menu_emitter_tuning::pos_x = 70.0f;
                            menu_emitter_tuning::pos_y = 38.0f;
                            break;
                    }
                    break;
                } else if( lc == KEY_F( 12 ) ) {
                    // F12: toggle bright-blue debug backdrop.
                    menu_emitter_tuning::blue_backdrop =
                        !menu_emitter_tuning::blue_backdrop;
                    break;
                }
                if( lc <= 0 ) {
                    if( ev.key.key >= SDLK_KP_1 && ev.key.key <= SDLK_KP_0 ) {
                        d.last_input = input_event( ev.key.key - SDLK_KP_1 + NUMPAD_1, input_event_t::keyboard );
                    } else {
                        // a key we don't know in curses and won't handle.
                        break;
                    }
                } else if( add_alt_code( lc ) ) {
                    // key was handled
                } else {
                    d.last_input = input_event( lc, input_event_t::keyboard );
                }
            }
            break;
            case SDL_EVENT_KEY_UP: {
                is_repeat = ev.key.repeat;
                if( ev.key.key == SDLK_LALT || ev.key.key == SDLK_RALT ) {
                    int code = end_alt_code();
                    if( code ) {
                        d.last_input = input_event( code, input_event_t::keyboard );
                        d.last_input.text = utf32_to_utf8( code );
                    }
                }
            }
            break;
            case SDL_EVENT_TEXT_INPUT:
                if( !add_alt_code( *ev.text.text ) ) {
                    if( strlen( ev.text.text ) > 0 ) {
                        const unsigned lc = UTF8_getch( ev.text.text );
                        d.last_input = input_event( lc, input_event_t::keyboard );
#if defined(SDL_PLATFORM_ANDROID)
                        if( !android_is_hardware_keyboard_available() ) {
                            if( !is_string_input( touch_input_context ) && !touch_input_context.allow_text_entry ) {
                                if( get_option<bool>( "ANDROID_AUTO_KEYBOARD" ) ) {
                                    SDL_StopTextInput( d.window.get() );
                                }

                                quick_shortcuts_t &qsl = quick_shortcuts_map[get_quick_shortcut_name(
                                                             touch_input_context.get_category() )];
                                qsl.remove( d.last_input );
                                add_quick_shortcut( qsl, d.last_input, false, true );
                                refresh_display();
                            } else if( lc == '\n' || lc == KEY_ESCAPE ) {
                                if( get_option<bool>( "ANDROID_AUTO_KEYBOARD" ) ) {
                                    SDL_StopTextInput( d.window.get() );
                                }
                            }
                        }
#endif
                    } else {
                        // no key pressed in this event
                        d.last_input = input_event();
                        d.last_input.type = input_event_t::keyboard;
                    }
                    d.last_input.text = ev.text.text;
                    text_refresh = true;
                }
                break;
            case SDL_EVENT_TEXT_EDITING: {
                if( strlen( ev.edit.text ) > 0 ) {
                    const unsigned lc = UTF8_getch( ev.edit.text );
                    d.last_input = input_event( lc, input_event_t::keyboard );
                } else {
                    // no key pressed in this event
                    d.last_input = input_event();
                    d.last_input.type = input_event_t::keyboard;
                }
                d.last_input.edit = ev.edit.text;
                d.last_input.edit_refresh = true;
                text_refresh = true;
            }
            break;
            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
                d.last_input = input_event( ev.jbutton.button, input_event_t::keyboard );
                break;
            case SDL_EVENT_JOYSTICK_AXIS_MOTION:
                // on gamepads, the axes are the analog sticks
                // TODO: somehow get the "digipad" values from the axes
                break;
            case SDL_EVENT_MOUSE_MOTION:
                // Record the position unconditionally, BEFORE the HIDE_CURSOR gate
                // and independently of last_input: CheckMessages resets last_input
                // at the top of every call (line 416) and every keyboard event
                // reassigns it, so last_input.mouse_pos is (0, 0) the moment the
                // player touches a key. Consumers that need "where is the cursor"
                // during a keyboard-driven loop (the ranged-targeting crosshair)
                // read this instead — see get_tracked_mouse_pos().
                s_last_mouse_px = point( static_cast<int>( ev.motion.x ),
                                         static_cast<int>( ev.motion.y ) );
                if( get_option<std::string>( "HIDE_CURSOR" ) == "show" ||
                    get_option<std::string>( "HIDE_CURSOR" ) == "hidekb" ) {
                    if( !SDL_CursorVisible() ) {
                        SDL_ShowCursor();
                    }

                    // Only monitor motion when cursor is visible
                    d.last_input = input_event( MOUSE_MOVE, input_event_t::mouse );
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                switch( ev.button.button ) {
                    case SDL_BUTTON_LEFT:
                        d.last_input = input_event( MOUSE_BUTTON_LEFT, input_event_t::mouse );
                        break;
                    case SDL_BUTTON_RIGHT:
                        d.last_input = input_event( MOUSE_BUTTON_RIGHT, input_event_t::mouse );
                        break;
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                // capturing_input(), NOT active(): the sidebar HUD is a PASSIVE
                // RmlUi document that stays open for the whole game and defaults ON,
                // so active() is permanently true and gating on it swallowed every
                // right/middle click — including the MOUSE_RIGHT_DOWN that AIM_HOLD
                // is bound to, which disabled right-click-to-aim outright.
                if( ev.button.button == SDL_BUTTON_RIGHT && !rmlui_layer::capturing_input() ) {
                    d.last_input = input_event( MOUSE_BUTTON_RIGHT_DOWN, input_event_t::mouse );
                } else if( ev.button.button == SDL_BUTTON_MIDDLE
                           && !rmlui_layer::capturing_input() ) {
                    d.last_input = input_event( MOUSE_BUTTON_MIDDLE, input_event_t::mouse );
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if( ev.wheel.y > 0 ) {
                    d.last_input = input_event( SCROLLWHEEL_UP, input_event_t::mouse );
                } else if( ev.wheel.y < 0 ) {
                    d.last_input = input_event( SCROLLWHEEL_DOWN, input_event_t::mouse );
                }
                break;

            case SDL_EVENT_QUIT:
                quit = true;
                break;
        }
        if( text_refresh && !is_repeat ) {
            break;
        }
    }

    // While an RmlUi doc (dev panel or menu) is open, repaint every CheckMessages
    // tick — not only on input events — so it animates/updates continuously.
    // get_input_event spins CheckMessages ~1 kHz while waiting; vsync on
    // submit_frame caps actual redraws to the display rate. Without this an idle
    // panel (no mouse motion) looks frozen.
    if( rmlui_layer::active() ) {
        d.needupdate = true;
    }

    bool resized = false;
    if( resize_dims.has_value() ) {
        restore_on_out_of_scope<input_event> prev_last_input( d.last_input );
        d.needupdate = resized = handle_resize( resize_dims.value().x, resize_dims.value().y );
    }
    if( !resized && render_target_reset ) {
        reinitialize_framebuffer( true );
        d.needupdate = true;
        restore_on_out_of_scope<input_event> prev_last_input( d.last_input );
        // FIXME: SDL_RENDER_TARGETS_RESET only seems to be fired after the first redraw
        // when restoring the window after system sleep, rather than immediately
        // on focus gain. This seems to mess up the first redraw and
        // causes black screen that lasts ~0.5 seconds before the screen
        // contents are redrawn in the following code.
        ui_manager::invalidate( rectangle<point>( point_zero, point( d.WindowWidth, d.WindowHeight ) ),
                                false );
        ui_manager::redraw_invalidated();
    }
    if( d.needupdate ) {
        try_sdl_update( d );
    }
    if( quit ) {
        exit_handler( 0 );
    }
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

    // SDL3 asserts that the event queue is only pumped from the main thread
    // (SDL_PumpEventsInternal -> SDL_IsMainThread_REAL).  Many finalization
    // routines call this to keep the window responsive while loading, and some
    // of them also run on the prewarm/worker threads, where driving SDL and the
    // UI is both illegal and pointless.  Make the pump a no-op off the main
    // thread so callers never have to know which thread they are on.
    if( !SDL_IsMainThread() ) {
        return;
    }

    // Handle all events, but ignore any keypress
    sdl_input::CheckMessages( g_display );

    g_display.last_input = input_event();
    previously_pressed_key = 0;
}

input_event input_manager::get_input_event()
{
    // Unlike pump_events(), this one cannot degrade to a no-op off the main
    // thread: the inputdelay < 0 branch below spins until a real event arrives,
    // so a silent guard would hang instead of crash.  Blocking on user input
    // from a worker is a design error — fail loudly.
    assert( SDL_IsMainThread() &&
            "get_input_event() must be called from the main thread" );

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
