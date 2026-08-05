#include "sdl_window.h"
#include <atomic>
#include <algorithm>

#include "cached_options.h"
#include "cata_tiles.h"
#include "catacharset.h"
#include "color.h"
#include "color_loader.h"
#include "cursesdef.h"
#include "cursesport.h"
#include "debug.h"
#include "font_loader.h"
#include "game_constants.h"
#include "game_ui.h"
#include "get_version.h"
#include "options.h"
#include "output.h"
#include "sdltiles.h"
#include "sdl_cursor.h"
#include "sdl_display.h"
#include "sdl_font.h"
#include "sdl_framebuffer.h"
#include "sdl_geometry.h"
#include "sdl_wrappers.h"
#include "sdlsound.h"
#include "string_formatter.h"
#include "string_utils.h"
#include "ui_manager.h"
#include "widget_icon.h"
#include "lighting/render_state.h"
#include "lighting/rmlui_layer.h"

#define dbg(x) DebugLogFL((x), DC::SDL)

static void ClearScreen()
{
    SetRenderDrawColor( g_display.renderer, 0, 0, 0, 255 );
    RenderClear( g_display.renderer );
}

static void InitSDL()
{
    SDL_InitFlags init_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO;

    throwErrorIf( !SDL_Init( init_flags ), "SDL_Init failed" );
    throwErrorIf( !TTF_Init(), "TTF_Init failed" );
    printErrorIf( !SDL_InitSubSystem( SDL_INIT_JOYSTICK ), "Initializing joystick subsystem failed" );

    // SDL uses the OS's input delay; there is no API to query or set INPUT_DELAY directly.

    atexit( TTF_Quit );
    atexit( SDL_Quit );
}

namespace
{

// try_sdl_update() throttles the input-driven redraw path to
// g_display.interval ms. That was hardcoded to 25 — a curses-era FRAMERATE
// leftover exposed by no option — which measured as a hard 40 fps ceiling:
// gameplay sat at exactly 39-40 fps no matter how cheap the frame was.
// Derive it from the refresh rate of the display the window actually opened
// on instead (multi-monitor: the window's display, not display 0).
auto sync_update_interval_to_display( SDL_Window *window ) -> void
{
    const auto display_id = SDL_GetDisplayForWindow( window );
    if( display_id == 0 ) {
        dbg( DL::Info ) << "SDL_GetDisplayForWindow failed (" << SDL_GetError()
                        << "); keeping redraw interval at " << g_display.interval << " ms";
        return;
    }
    const auto *mode = SDL_GetCurrentDisplayMode( display_id );
    if( !mode ) {
        dbg( DL::Info ) << "SDL_GetCurrentDisplayMode failed (" << SDL_GetError()
                        << "); keeping redraw interval at " << g_display.interval << " ms";
        return;
    }
    if( mode->refresh_rate <= 0.0f ) {
        dbg( DL::Info ) << "Display " << display_id << " reports an unknown refresh rate; "
                           "keeping redraw interval at " << g_display.interval << " ms";
        return;
    }
    const auto derived = static_cast<uint32_t>( 1000.0f / mode->refresh_rate );
    // Clamp to 4..33 ms. Never 0: try_sdl_update() would then refresh_display()
    // on every CheckMessages call (~1 kHz), a severe regression. 33 ms keeps a
    // ~30 fps floor should a display report something absurd.
    g_display.interval = std::clamp<uint32_t>( derived, 4, 33 );
    dbg( DL::Info ) << "Display " << display_id << " refresh rate " << mode->refresh_rate
                    << " Hz -> input redraw interval " << g_display.interval << " ms";
}

} // namespace


//Registers, creates, and shows the Window!!
static void WinCreate()
{
    std::string version = string_format( "Cataclysm: Bright Nights - %s", getVersionString() );

    // Common flags used for fulscreen and for windowed
    int window_flags = 0;
    g_display.WindowWidth = g_display.TERMINAL_WIDTH * fontwidth * g_display.scaling_factor;
    g_display.WindowHeight = g_display.TERMINAL_HEIGHT * fontheight * g_display.scaling_factor;
    // HIGH_PIXEL_DENSITY: SDL3 industry-standard HiDPI path. Window opens
    // at full physical resolution (e.g. 3680×2196 on a 4K Win11 monitor at
    // 200% scaling), GPU swapchain matches. UI continues to lay out in
    // LOGICAL pixels from SDL_GetWindowSize; sprite_batcher::begin_pass is
    // called with viewport=physical and proj=logical so logical-coord draws
    // stretch across the full physical framebuffer. See SDL3 HiDPI README.
    window_flags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    const auto screen_mode = get_option<std::string>( "FULLSCREEN" );
    const auto minimize = get_option<bool>( "MINIMIZE_ON_FOCUS_LOSS" );

    SDL_SetHint( SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, minimize ? "1" : "0" );

    if( screen_mode == "fullscreen" ) {
        window_flags |= SDL_WINDOW_FULLSCREEN;
        g_display.fullscreen = true;
    } else if( screen_mode == "windowedbl" ) {
        window_flags |= SDL_WINDOW_FULLSCREEN;
        g_display.fullscreen = true;
    } else if( screen_mode == "maximized" ) {
        window_flags |= SDL_WINDOW_MAXIMIZED;
    }

    int display = std::stoi( get_option<std::string>( "DISPLAY" ) );
    {
        int display_count = 0;
        SDL_DisplayID *displays = SDL_GetDisplays( &display_count );
        if( display < 0 || display >= display_count || !displays ) {
            display = 0;
        }
        SDL_free( displays );
    }

    g_display.window.reset( SDL_CreateWindow( version.c_str(), g_display.WindowWidth,
                            g_display.WindowHeight, window_flags ) );
    throwErrorIf( !g_display.window, "SDL_CreateWindow failed" );
    SDL_SetWindowPosition( g_display.window.get(), SDL_WINDOWPOS_CENTERED_DISPLAY( display ),
                           SDL_WINDOWPOS_CENTERED_DISPLAY( display ) );
    // Redraw throttle follows the panel, not a hardcoded 40 fps (see above).
    sync_update_interval_to_display( g_display.window.get() );
    SDL_StartTextInput( g_display.window.get() );

    // Hidden mirror window for the legacy SDL_Renderer. Same dimensions as
    // the visible one so display_buffer textures match the pixel grid that
    // the GPU bridge in refresh_display() will eventually sample.
    g_display.legacy_window.reset( SDL_CreateWindow( "cataclysm_legacy", g_display.WindowWidth,
                                   g_display.WindowHeight,
                                   SDL_WINDOW_HIDDEN ) );
    throwErrorIf( !g_display.legacy_window, "SDL_CreateWindow (legacy mirror) failed" );

    // On Android SDL seems janky in windowed mode so we're fullscreen all the time.
    // Fullscreen mode is now modified so it obeys terminal width/height, rather than
    // overwriting it with this calculation.
    // With SDL_WINDOW_HIGH_PIXEL_DENSITY the swapchain is sized in PHYSICAL
    // pixels but SDL_GetWindowSize / SDL_CreateWindow's size args are LOGICAL
    // (smaller on HiDPI / Retina). The UI lays out in TERMINAL cells * pixel
    // size — if those cells are computed from logical px the layout lands in
    // a half-quadrant of the physical swapchain. Resync WindowWidth/Height
    // and TERMINAL_* from the physical pixel size for BOTH fullscreen and
    // windowed startup. (SDL3 migration regression #8336.)
    // HiDPI: WindowWidth/Height in LOGICAL pixels (the coord system the UI
    // queues draws in). Swapchain is at physical pixels; the projection-vs-
    // viewport split inside sprite_batcher::begin_pass handles the stretch.
    SDL_GetWindowSize( g_display.window.get(), &g_display.WindowWidth, &g_display.WindowHeight );
    g_display.TERMINAL_WIDTH  = g_display.WindowWidth / fontwidth / g_display.scaling_factor;
    g_display.TERMINAL_HEIGHT = g_display.WindowHeight / fontheight / g_display.scaling_factor;
    // Initialize framebuffer caches (self-validating per-family)
    cache_initialize_all(
        g_display.TERMINAL_HEIGHT, g_display.TERMINAL_WIDTH,
        /*overmap*/ OVERMAP_WINDOW_HEIGHT, OVERMAP_WINDOW_WIDTH,
        /*terrain*/ TERRAIN_WINDOW_HEIGHT, TERRAIN_WINDOW_WIDTH
    );

    g_display.format = SDL_GetWindowPixelFormat( g_display.window.get() );
    throwErrorIf( g_display.format == SDL_PIXELFORMAT_UNKNOWN, "SDL_GetWindowPixelFormat failed" );

    int renderer_id = -1;
    bool software_renderer = get_option<std::string>( "RENDERER" ).empty();
    std::string renderer_name;
    if( software_renderer ) {
        renderer_name = "software";
    } else {
        renderer_name = get_option<std::string>( "RENDERER" );
    }

    // Phase 2i-B-5 conflict avoidance: the visible window has already
    // been claimed by an SDL_GPU device (lighting::gpu_device, driver
    // typically direct3d12 on Win11). The hidden mirror window we're
    // about to create an SDL_Renderer for is used for the bridge
    // readback + the legacy fallback draw paths (rotated sprites,
    // pixel_minimap, vehicle_preview clip). If THAT SDL_Renderer also
    // picks direct3d12, two D3D12 device instances coexist in the
    // same process — observed symptom is SDL_RenderTextureRotated
    // returning false with a garbled error string ("Parameter
    // 'joystick' is invalid") and tiles never actually drawing.
    //
    // The user's RENDERER setting still drives what backend the
    // application *thinks* it's using (the active driver name is
    // logged + reused everywhere else), but the hidden renderer is
    // forced to a non-D3D12, non-"gpu" alternative so it can coexist
    // with the SDL_GPU device. direct3d11 is preferred; opengl is a
    // fallback. The user-facing RENDERER option only matters for the
    // bridge readback texture format, which is the same regardless.
    if( !software_renderer ) {
        const std::string lower_name = to_lower_case( renderer_name );
        if( lower_name == "direct3d12" || lower_name == "gpu" ) {
            const std::array<const char *, 3> alt_priorities{ "direct3d11", "direct3d", "opengl" };
            const int num_drivers = SDL_GetNumRenderDrivers();
            for( const char *alt : alt_priorities ) {
                for( int i = 0; i < num_drivers; ++i ) {
                    const char *name = SDL_GetRenderDriver( i );
                    if( name && std::string( name ) == alt ) {
                        DebugLog( DL::Info, DC::Main )
                                << "RENDERER='" << renderer_name
                                << "' would conflict with the SDL_GPU device on the visible "
                           "window; forcing the hidden mirror SDL_Renderer to '" << alt
                                << "' instead (visible window still on SDL_GPU).";
                        renderer_name = alt;
                        break;
                    }
                }
                if( renderer_name == alt ) {
                    break;
                }
            }
        }
    }

    const int numRenderDrivers = SDL_GetNumRenderDrivers();
    for( int i = 0; i < numRenderDrivers; i++ ) {
        const char *name = SDL_GetRenderDriver( i );
        if( name && renderer_name == name ) {
            renderer_id = i;
            DebugLog( DL::Info, DC::Main ) << "Active renderer: " << i << "/" << name;
            break;
        }
    }

    if( !software_renderer ) {
        dbg( DL::Info ) << "Attempting to initialize accelerated SDL renderer.";

        const char *renderer_driver = renderer_id >= 0 ? SDL_GetRenderDriver( renderer_id ) : nullptr;
        g_display.renderer.reset( SDL_CreateRenderer( g_display.legacy_window.get(), renderer_driver ) );
        if( printErrorIf( !g_display.renderer,
                          "Failed to initialize accelerated renderer, falling back to software rendering" ) ) {
            software_renderer = true;
        } else {
            if( get_option<bool>( "VSYNC" ) ) {
                SDL_SetRenderVSync( g_display.renderer.get(), 1 );
            }
            SetRenderDrawBlendMode( g_display.renderer, SDL_BLENDMODE_NONE );
        }
    }

    if( software_renderer ) {
        g_display.renderer.reset( SDL_CreateRenderer( g_display.legacy_window.get(), "software" ) );
        throwErrorIf( !g_display.renderer, "Failed to initialize software renderer" );
        SetRenderDrawBlendMode( g_display.renderer, SDL_BLENDMODE_NONE );
    }

    SDL_SetWindowMinimumSize( g_display.window.get(),
                              fontwidth * FULL_SCREEN_WIDTH * g_display.scaling_factor,
                              fontheight * FULL_SCREEN_HEIGHT * g_display.scaling_factor );

    ClearScreen();

    // Errors here are ignored, worst case: the option does not work as expected,
    // but that won't crash
    if( get_option<std::string>( "HIDE_CURSOR" ) != "show" && SDL_CursorVisible() ) {
        SDL_HideCursor();
    } else {
        SDL_ShowCursor();
    }

    // Initialize joysticks.
    int numjoy = 0;
    SDL_JoystickID *joystick_ids = SDL_GetJoysticks( &numjoy );

    if( get_option<bool>( "ENABLE_JOYSTICK" ) && numjoy >= 1 ) {
        if( numjoy > 1 ) {
            dbg( DL::Warn ) << "You have more than one gamepads/joysticks plugged in, "
                               "only the first will be used.";
        }
        g_display.joystick = SDL_OpenJoystick( joystick_ids[0] );
        printErrorIf( g_display.joystick == nullptr, "SDL_OpenJoystick failed" );
        if( g_display.joystick ) {
            SDL_SetJoystickEventsEnabled( true );
        }
    } else {
        g_display.joystick = nullptr;
    }
    SDL_free( joystick_ids );

    // Set up audio mixer.
    init_sound();

    dbg( DL::Info ) << "USE_COLOR_MODULATED_TEXTURES is set to " <<
                    get_option<bool>( "USE_COLOR_MODULATED_TEXTURES" );
    //initialize the alternate rectangle texture for replacing SDL_RenderFillRect
    if( get_option<bool>( "USE_COLOR_MODULATED_TEXTURES" ) && !software_renderer ) {
        g_display.geometry = std::make_unique<ColorModulatedGeometryRenderer>( g_display.renderer );
    } else {
        g_display.geometry = std::make_unique<DefaultGeometryRenderer>();
    }

    // Phase 2i-B-1: claim the *visible* window for the SDL_GPU device.
    // The legacy SDL_Renderer above now lives on the hidden mirror window
    // and its output is invisible until subsequent commits bridge or
    // replace each draw call site. From this point on, only the GPU
    // present in refresh_display() actually reaches the user's screen.
    //
    // If init fails the visible window remains blank for the session —
    // the legacy path keeps running invisibly. The SDL log carries the
    // exact failure mode.
    lighting::init_render_state_on( g_display.window.get() );
    // Bring RmlUi up NOW, not on the first rendered frame.  rml_doc::open() gives up
    // permanently when the layer isn't ready, and every screen whose curses drawing
    // was deleted during the migration then paints nothing while its input loop still
    // swallows keys — the black, unescapable main menu.  begin_frame() also inits
    // lazily, but that is the first RENDERED frame, and the main menu opens its
    // document before it ever draws, so it lost the race whenever device setup ran
    // slow.  init() is idempotent, so the lazy call stays as a fallback for the case
    // where the device genuinely isn't ready yet.  Mirrors the WinDestroy teardown.
    if( auto &rs = lighting::get_render_state(); rs.ready() ) {
        rmlui_layer::init( rs.device() );
    }
    // Dear ImGui inits lazily in refresh_display (device readiness isn't
    // guaranteed at WinCreate); torn down in WinDestroy.
}

static void WinDestroy()
{
    // RmlUi holds GPU resources on the shared device — tear it down BEFORE the
    // device is destroyed by shutdown_render_state(). No-op if never inited.
    rmlui_layer::shutdown();

    // Tear the SDL_GPU lighting stack down before SDL_Quit. Idempotent;
    // safe even if try_init_render_state() never succeeded.
    lighting::shutdown_render_state();

    destroy_game_cursors();
    shutdown_sound();
    tilecontext.reset();

    if( g_display.joystick ) {
        SDL_CloseJoystick( g_display.joystick );

        g_display.joystick = nullptr;
    }
    g_display.geometry.reset();
    g_display.format = SDL_PIXELFORMAT_UNKNOWN;
    g_display.renderer.reset();
    g_display.legacy_window.reset();
    g_display.window.reset();
}

bool handle_resize( int w, int h )
{
    // Guard against recursive re-entry: handle_resize → ui_manager::screen_resized
    // → redraw → try_sdl_update → CheckMessages → handle_resize again.
    // The inner call would operate on partially-updated display state and crash.
    static std::atomic<bool> in_resize{false};
    if( in_resize ) {
        return false;
    }

    if( ( w != g_display.WindowWidth ) || ( h != g_display.WindowHeight ) ) {
        in_resize = true;
        g_display.WindowWidth = w;
        g_display.WindowHeight = h;
        g_display.TERMINAL_WIDTH = g_display.WindowWidth / fontwidth / g_display.scaling_factor;
        g_display.TERMINAL_HEIGHT = g_display.WindowHeight / fontheight / g_display.scaling_factor;
        g_display.need_invalidate_framebuffers = true;
        catacurses::stdscr = catacurses::newwin( g_display.TERMINAL_HEIGHT, g_display.TERMINAL_WIDTH,
                             point_zero );
        game_ui::init_ui();
        ui_manager::screen_resized();
        // Keep the UI compositor texture sized to the physical swapchain so
        // the composite blit stays 1:1 after a window resize.
        {
            auto &rs = lighting::get_render_state();
            if( rs.ready() ) {
                int pw = 0;
                int ph = 0;
                SDL_GetWindowSizeInPixels( g_display.window.get(), &pw, &ph );
                if( rs.ui_target() ) {
                    rs.ui_target()->resize( pw, ph );
                }
                if( rs.world_target() ) {
                    rs.world_target()->resize( pw, ph );
                }
                if( rs.shadow_mask() ) {
                    rs.shadow_mask()->resize( pw, ph );
                }
                if( rs.world_ldr_target() ) {
                    rs.world_ldr_target()->resize( pw, ph );
                }
                // Bloom half-res textures track the world_target size.
                rs.bloom().resize( static_cast<std::uint32_t>( pw ),
                                   static_cast<std::uint32_t>( ph ) );
                // Font cell size may have changed; drop stale-size icon rasters
                // so the next request re-rasterizes crisp at the new size.
                widget_icon::clear();
            }
        }
        in_resize = false;
        return true;
    }
    return false;
}

void resize_term( const int cell_w, const int cell_h )
{
    int w = cell_w * fontwidth * g_display.scaling_factor;
    int h = cell_h * fontheight * g_display.scaling_factor;
    SDL_SetWindowSize( g_display.window.get(), w, h );
    SDL_GetWindowSize( g_display.window.get(), &w, &h );
    handle_resize( w, h );
}

void toggle_fullscreen_window()
{
    static int restore_win_w = get_option<int>( "TERMINAL_X" ) * fontwidth * g_display.scaling_factor;
    static int restore_win_h = get_option<int>( "TERMINAL_Y" ) * fontheight * g_display.scaling_factor;

    if( g_display.fullscreen ) {
        if( printErrorIf( !SDL_SetWindowFullscreen( g_display.window.get(), false ),
                          "SDL_SetWindowFullscreen failed" ) ) {
            return;
        }
        SDL_RestoreWindow( g_display.window.get() );
        SDL_SetWindowSize( g_display.window.get(), restore_win_w, restore_win_h );
        SDL_SetWindowMinimumSize( g_display.window.get(),
                                  fontwidth * FULL_SCREEN_WIDTH * g_display.scaling_factor,
                                  fontheight * FULL_SCREEN_HEIGHT * g_display.scaling_factor );
    } else {
        restore_win_w = g_display.WindowWidth;
        restore_win_h = g_display.WindowHeight;
        SDL_SetWindowFullscreenMode( g_display.window.get(), nullptr );
        if( printErrorIf( !SDL_SetWindowFullscreen( g_display.window.get(), true ),
                          "SDL_SetWindowFullscreen failed" ) ) {
            return;
        }
    }
    int nw = 0;
    int nh = 0;
    SDL_GetWindowSize( g_display.window.get(), &nw, &nh );
    handle_resize( nw, nh );
    g_display.fullscreen = !g_display.fullscreen;
}

//Check for any window messages (keypress, paint, mousemove, etc)


//***********************************
//Pseudo-Curses Functions           *
//***********************************

// Measures scaling factor for high-dpi displays
static std::pair<float, float> get_display_scale( int display_index )
{
    SDL_Window *w = SDL_CreateWindow( "probe", 16, 16,
                                      SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY );
    if( !w ) {
        return std::make_pair( 1.0f, 1.0f );
    }
    SDL_SetWindowPosition( w, SDL_WINDOWPOS_CENTERED_DISPLAY( display_index ),
                           SDL_WINDOWPOS_CENTERED_DISPLAY( display_index ) );

    int lw, lh;
    SDL_GetWindowSize( w, &lw, &lh );
    int pw, ph;
    SDL_GetWindowSizeInPixels( w, &pw, &ph );
    SDL_DestroyWindow( w );

    float scale_w = lw ? static_cast<float>( pw ) / static_cast<float>( lw ) : 1.0f;
    float scale_h = lh ? static_cast<float>( ph ) / static_cast<float>( lh ) : 1.0f;
    return std::make_pair( scale_w, scale_h );
}

static void init_term_size_and_scaling_factor()
{
    g_display.scaling_factor = 1;
    point terminal( get_option<int>( "TERMINAL_X" ), get_option<int>( "TERMINAL_Y" ) );

    if( get_option<std::string>( "SCALING_FACTOR" ) == "2" ) {
        g_display.scaling_factor = 2;
    } else if( get_option<std::string>( "SCALING_FACTOR" ) == "4" ) {
        g_display.scaling_factor = 4;
    }

    if( g_display.scaling_factor > 1 ) {

        int max_width, max_height;

        const int current_display_idx = std::stoi( get_option<std::string>( "DISPLAY" ) );
        int display_count = 0;
        SDL_DisplayID *display_list = SDL_GetDisplays( &display_count );
        const SDL_DisplayID current_display_id = ( display_list && current_display_idx < display_count )
            ? display_list[current_display_idx]
            : SDL_GetPrimaryDisplay();
        SDL_free( display_list );

        const SDL_DisplayMode *current_display = SDL_GetDesktopDisplayMode( current_display_id );

        if( current_display ) {
            if( get_option<std::string>( "FULLSCREEN" ) == "no" ) {

                // Make a maximized test window to determine maximum windowed size
                SDL_Window_Ptr test_window;
                test_window.reset( SDL_CreateWindow( "test_window",
                                                     FULL_SCREEN_WIDTH * fontwidth,
                                                     FULL_SCREEN_HEIGHT * fontheight,
                                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MAXIMIZED
                                                   ) );
                SDL_SetWindowPosition( test_window.get(),
                                       SDL_WINDOWPOS_CENTERED_DISPLAY( current_display_idx ),
                                       SDL_WINDOWPOS_CENTERED_DISPLAY( current_display_idx ) );

                SDL_GetWindowSizeInPixels( test_window.get(), &max_width, &max_height );

                // If the video subsystem isn't reset the test window messes things up later
                test_window.reset();
                SDL_QuitSubSystem( SDL_INIT_VIDEO );
                SDL_InitSubSystem( SDL_INIT_VIDEO );

            } else {
                // For fullscreen or window borderless maximum size is the display size
                auto [ dpi_scale_w, dpi_scale_h ] = get_display_scale( current_display_idx );
                max_width = static_cast<int>( dpi_scale_w * current_display->w );
                max_height = static_cast<int>( dpi_scale_h * current_display->h );
            }
        } else {
            dbg( DL::Warn ) << "Failed to get current Display Mode, assuming infinite display size.";
            max_width = INT_MAX;
            max_height = INT_MAX;
        }

        if( terminal.x * fontwidth > max_width ||
            FULL_SCREEN_WIDTH * fontwidth * g_display.scaling_factor > max_width ) {
            if( FULL_SCREEN_WIDTH * fontwidth * g_display.scaling_factor > max_width ) {
                dbg( DL::Warn ) << "SCALING_FACTOR set too high for display size, resetting to 1";
                g_display.scaling_factor = 1;
                terminal.x = max_width / fontwidth;
                terminal.y = max_height / fontheight;
                get_options().get_option( "SCALING_FACTOR" ).setValue( "1" );
            } else {
                terminal.x = max_width / fontwidth;
            }
        }

        if( terminal.y * fontheight > max_height ||
            FULL_SCREEN_HEIGHT * fontheight * g_display.scaling_factor > max_height ) {
            if( FULL_SCREEN_HEIGHT * fontheight * g_display.scaling_factor > max_height ) {
                dbg( DL::Warn ) << "SCALING_FACTOR set too high for display size, resetting to 1";
                g_display.scaling_factor = 1;
                terminal.x = max_width / fontwidth;
                terminal.y = max_height / fontheight;
                get_options().get_option( "SCALING_FACTOR" ).setValue( "1" );
            } else {
                terminal.y = max_height / fontheight;
            }
        }

        terminal.x -= terminal.x % g_display.scaling_factor;
        terminal.y -= terminal.y % g_display.scaling_factor;

        terminal.x = std::max( FULL_SCREEN_WIDTH * g_display.scaling_factor, terminal.x );
        terminal.y = std::max( FULL_SCREEN_HEIGHT * g_display.scaling_factor, terminal.y );

        get_options().get_option( "TERMINAL_X" ).setValue(
            std::max( FULL_SCREEN_WIDTH * g_display.scaling_factor, terminal.x ) );
        get_options().get_option( "TERMINAL_Y" ).setValue(
            std::max( FULL_SCREEN_HEIGHT * g_display.scaling_factor, terminal.y ) );

        get_options().save();
    }

    g_display.TERMINAL_WIDTH = terminal.x / g_display.scaling_factor;
    g_display.TERMINAL_HEIGHT = terminal.y / g_display.scaling_factor;
}

//Basic Init, create the font, backbuffer, etc
void catacurses::init_interface()
{
    g_display.last_input = input_event();
    g_display.inputdelay = -1;

    InitSDL();

    get_options().init();
    get_options().load();
    get_options().save();

    font_loader fl;
    fl.load();
    fl.fontwidth = get_option<int>( "FONT_WIDTH" );
    fl.fontheight = get_option<int>( "FONT_HEIGHT" );
    fl.fontsize = get_option<int>( "FONT_SIZE" );
    fl.fontblending = get_option<bool>( "FONT_BLENDING" );
    fl.map_fontsize = get_option<int>( "MAP_FONT_SIZE" );
    fl.map_fontwidth = get_option<int>( "MAP_FONT_WIDTH" );
    fl.map_fontheight = get_option<int>( "MAP_FONT_HEIGHT" );
    fl.overmap_fontsize = get_option<int>( "OVERMAP_FONT_SIZE" );
    fl.overmap_fontwidth = get_option<int>( "OVERMAP_FONT_WIDTH" );
    fl.overmap_fontheight = get_option<int>( "OVERMAP_FONT_HEIGHT" );
    ::fontwidth = fl.fontwidth;
    ::fontheight = fl.fontheight;

    init_term_size_and_scaling_factor();

    WinCreate();

    dbg( DL::Info ) << "Initializing SDL Tiles context";
    tilecontext = std::make_shared<cata_tiles>( g_display.renderer, g_display.geometry );
    const auto tilesName = get_option<std::string>( "TILES" );
    const auto omTilesName = get_option<std::string>( "OVERMAP_TILES" );
    try {
        std::vector<mod_id> dummy;
        tilecontext->load_tileset(
            tilesName,
            dummy,
            /*precheck=*/true,
            /*force=*/false,
            /*pump_events=*/true
        );
    } catch( const std::exception &err ) {
        // Tiles-only fork: no ASCII fallback. Log the precheck failure but stay in
        // tiles mode; a genuinely broken tileset surfaces at the real load below.
        dbg( DL::Error ) << "failed to check for tileset: " << err.what();
    }
    if( tilesName == omTilesName ) {
        overmap_tilecontext = tilecontext;
    } else {
        try {
            overmap_tilecontext = std::make_shared<cata_tiles>( g_display.renderer,
                                  g_display.geometry );
            std::vector<mod_id> dummy;
            overmap_tilecontext->load_tileset(
                omTilesName,
                dummy,
                /*precheck=*/true,
                /*force=*/false,
                /*pump_events=*/true
            );
        } catch( const std::exception &err ) {
            // Tiles-only fork: no ASCII fallback; stay in tiles mode and log.
            dbg( DL::Error ) << "failed to check for overmap tileset: " << err.what();
        }
    }
    color_loader<SDL_Color>().load( windowsPalette );
    init_colors();

    // initialize sound set
    load_soundset();

    try {
        g_display.font = std::make_unique<FontFallbackList>( g_display.renderer, g_display.format,
                         fl.fontwidth, fl.fontheight,
                         windowsPalette, fl.typeface, fl.fontsize, fl.fontblending );
        g_display.map_font = std::make_unique<FontFallbackList>( g_display.renderer, g_display.format,
                             fl.map_fontwidth,
                             fl.map_fontheight,
                             windowsPalette, fl.map_typeface, fl.map_fontsize, fl.fontblending );
        g_display.overmap_font = std::make_unique<FontFallbackList>( g_display.renderer, g_display.format,
                                 fl.overmap_fontwidth,
                                 fl.overmap_fontheight,
                                 windowsPalette, fl.overmap_typeface, fl.overmap_fontsize, fl.fontblending );
    } catch( std::exception &e ) {
        g_display.font.reset();
        g_display.map_font.reset();
        g_display.overmap_font.reset();
        throw;
    }

    stdscr = newwin( get_terminal_height(), get_terminal_width(), point_zero );
    //newwin calls `new WINDOW`, and that will throw, but not return nullptr.

}

//Ends the terminal, destroy everything
void catacurses::endwin()
{
    tilecontext.reset();
    overmap_tilecontext.reset();
    g_display.font.reset();
    g_display.map_font.reset();
    g_display.overmap_font.reset();
    WinDestroy();
}
