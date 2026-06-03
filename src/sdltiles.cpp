// MUST precede any game header: debug.h defines a function-like `DebugLog`
// macro that otherwise mangles ImGui::DebugLog in imgui.h (same reason
// imgui_layer.cpp includes imgui.h before debug.h).
#include "imgui.h"

#include "cursesdef.h" // IWYU pragma: associated
#include "sdltiles.h" // IWYU pragma: associated

#include <algorithm>
#include <array>
#include <cctype>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <stack>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "avatar.h"
#include "cached_options.h"
#include "cata_tiles.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "color.h"
#include "color_loader.h"
#include "cuboid_rectangle.h"
#include "cursesport.h"
#include "debug.h"
#include "dynamic_atlas.h"
#include "filesystem.h"
#include "font_loader.h"
#include "game.h"
#include "game_ui.h"
#include "get_version.h"
#include "hash_utils.h"
#include "input.h"
#include "runtime_handlers.h"
#include "json.h"
#include "make_static.h"
#include "mapbuffer.h"
#include "mission.h"
#include "npc.h"
#include "options.h"
#include "output.h"
#include "overmap_location.h"
#include "overmap_label.h"
#include "overmap_label_note.h"
#include "note_label_utils.h"
#include "overmap_special.h"
#include "overmap_ui.h"
#include "overmapbuffer.h"
#include "regional_settings.h"
#include "mongroup.h"
#include "path_info.h"
#include "point.h"
#include "rng.h"
#include "sdl_wrappers.h"
#include "sdl_geometry.h"
#include "sdl_utils.h"
#include "sdl_font.h"
#include "sdlsound.h"
#include "lighting/emitter_collector.h"
#include "lighting/frame_build.h"
#include "lighting/imgui_layer.h"
#include "lighting/render_state.h"
#include "lighting/snapshot.h"
#include "lighting/sdf_pass.h"
#include "map.h"
#include "lightmap.h"
#include "game_constants.h"
#include "string_formatter.h"
#include "uistate.h"
#include "ui_manager.h"
#include "wcwidth.h"
#include "widget_icon.h"
#include "sidebar_anim.h"
#include "worldfactory.h"

#if defined(__linux__)
#   include <cstdlib> // getenv()/setenv()
#endif

#if defined(_WIN32)
#   if 1 // HACK: Hack to prevent reordering of #include "platform_win.h" by IWYU
#       include "platform_win.h"
#   endif
#   include <shlwapi.h>
#endif

#define dbg(x) DebugLogFL((x),DC::SDL)

//***********************************
//Globals                           *
//***********************************

std::shared_ptr<cata_tiles> tilecontext;
std::shared_ptr<cata_tiles> overmap_tilecontext;
static Uint64 lastupdate = 0;
static uint32_t interval = 25;
static bool needupdate = false;
static bool need_invalidate_framebuffers = false;
static const std::string empty_string;

palette_array windowsPalette;

static Font_Ptr font;
static Font_Ptr map_font;
static Font_Ptr overmap_font;

static SDL_Window_Ptr window;
// Phase 2i-B-1: SDL_Renderer no longer claims the visible window — that
// belongs to the SDL_GPU device now (lighting::render_state). The legacy
// renderer keeps running on a hidden mirror window so every call site that
// still talks SDL_Renderer (cata_tiles draw_sprite_at, sdl_font glyph
// cache, pixel_minimap, vehicle_preview …) compiles and executes
// unchanged; its output is just invisible. Subsequent 2i-B-N commits port
// those call sites to the GPU stack and drop the hidden window.
static SDL_Window_Ptr legacy_window;
static SDL_Renderer_Ptr renderer;
static SDL_PixelFormat format = SDL_PIXELFORMAT_UNKNOWN;
static GeometryRenderer_Ptr geometry;
static int WindowWidth;        //Width of the actual window, not the curses window
static int WindowHeight;       //Height of the actual window, not the curses window
// input from various input sources. Each input source sets the type and
// the actual input value (key pressed, mouse button clicked, ...)
// This value is finally returned by input_manager::get_input_event.
static input_event last_input;

static constexpr int ERR = -1;
static int inputdelay;         //How long getch will wait for a character to be typed
static Uint64 delaydpad =
    std::numeric_limits<Uint64>::max();     // Used for entering diagonal directions with d-pad.
static Uint64 dpad_delay =
    100;   // Delay in milliseconds between registering a d-pad event and processing it.
static bool dpad_continuous = false;  // Whether we're currently moving continuously with the dpad.
static int lastdpad = ERR;      // Keeps track of the last dpad press.
static int queued_dpad = ERR;   // Queued dpad press, for individual button presses.
int fontwidth;          //the width of the font, background is always this size
int fontheight;         //the height of the font, background is always this size
static int TERMINAL_WIDTH;
static int TERMINAL_HEIGHT;
bool fullscreen;
int scaling_factor;

static SDL_Joystick *joystick; // Only one joystick for now.

using cata_cursesport::curseline;
using cata_cursesport::cursecell;
static std::vector<curseline> oversized_framebuffer;
static std::vector<curseline> terminal_framebuffer;
static std::weak_ptr<void> winBuffer; //tracking last drawn window to fix the framebuffer
static int fontScaleBuffer; //tracking zoom levels to fix framebuffer w/tiles

//***********************************
//Non-curses, Window functions      *
//***********************************

static bool operator==( const cata_cursesport::WINDOW *const lhs, const catacurses::window &rhs )
{
    return lhs == rhs.get();
}

static void ClearScreen()
{
    SetRenderDrawColor( renderer, 0, 0, 0, 255 );
    RenderClear( renderer );
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


//Registers, creates, and shows the Window!!
static void WinCreate()
{
    std::string version = string_format( "Cataclysm: Bright Nights - %s", getVersionString() );

    // Common flags used for fulscreen and for windowed
    int window_flags = 0;
    WindowWidth = TERMINAL_WIDTH * fontwidth * scaling_factor;
    WindowHeight = TERMINAL_HEIGHT * fontheight * scaling_factor;
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
        fullscreen = true;
    } else if( screen_mode == "windowedbl" ) {
        window_flags |= SDL_WINDOW_FULLSCREEN;
        fullscreen = true;
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

    ::window.reset( SDL_CreateWindow( version.c_str(), WindowWidth, WindowHeight, window_flags ) );
    throwErrorIf( !::window, "SDL_CreateWindow failed" );
    SDL_SetWindowPosition( ::window.get(), SDL_WINDOWPOS_CENTERED_DISPLAY( display ),
                           SDL_WINDOWPOS_CENTERED_DISPLAY( display ) );
    SDL_StartTextInput( ::window.get() );

    // Hidden mirror window for the legacy SDL_Renderer. Same dimensions as
    // the visible one so display_buffer textures match the pixel grid that
    // the GPU bridge in refresh_display() will eventually sample.
    ::legacy_window.reset( SDL_CreateWindow( "cataclysm_legacy", WindowWidth, WindowHeight,
                          SDL_WINDOW_HIDDEN ) );
    throwErrorIf( !::legacy_window, "SDL_CreateWindow (legacy mirror) failed" );

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
    SDL_GetWindowSize( ::window.get(), &WindowWidth, &WindowHeight );
    TERMINAL_WIDTH  = WindowWidth  / fontwidth  / scaling_factor;
    TERMINAL_HEIGHT = WindowHeight / fontheight / scaling_factor;
    // Initialize framebuffer caches
    terminal_framebuffer.resize( TERMINAL_HEIGHT );
    for( int i = 0; i < TERMINAL_HEIGHT; i++ ) {
        terminal_framebuffer[i].chars.assign( TERMINAL_WIDTH, cursecell( "" ) );
    }

    oversized_framebuffer.resize( TERMINAL_HEIGHT );
    for( int i = 0; i < TERMINAL_HEIGHT; i++ ) {
        oversized_framebuffer[i].chars.assign( TERMINAL_WIDTH, cursecell( "" ) );
    }

    format = SDL_GetWindowPixelFormat( ::window.get() );
    throwErrorIf( format == SDL_PIXELFORMAT_UNKNOWN, "SDL_GetWindowPixelFormat failed" );

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
        const std::string lower_name = []( std::string s ) {
            for( char &c : s ) {
                c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
            }
            return s;
        }( renderer_name );
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
        renderer.reset( SDL_CreateRenderer( ::legacy_window.get(), renderer_driver ) );
        if( printErrorIf( !renderer,
                          "Failed to initialize accelerated renderer, falling back to software rendering" ) ) {
            software_renderer = true;
        } else {
            if( get_option<bool>( "VSYNC" ) ) {
                SDL_SetRenderVSync( renderer.get(), 1 );
            }
            SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_NONE );
        }
    }

    if( software_renderer ) {
        renderer.reset( SDL_CreateRenderer( ::legacy_window.get(), "software" ) );
        throwErrorIf( !renderer, "Failed to initialize software renderer" );
        SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_NONE );
    }

    SDL_SetWindowMinimumSize( ::window.get(), fontwidth * FULL_SCREEN_WIDTH * scaling_factor,
                              fontheight * FULL_SCREEN_HEIGHT * scaling_factor );

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
        joystick = SDL_OpenJoystick( joystick_ids[0] );
        printErrorIf( joystick == nullptr, "SDL_OpenJoystick failed" );
        if( joystick ) {
            SDL_SetJoystickEventsEnabled( true );
        }
    } else {
        joystick = nullptr;
    }
    SDL_free( joystick_ids );

    // Set up audio mixer.
    init_sound();

    dbg( DL::Info ) << "USE_COLOR_MODULATED_TEXTURES is set to " <<
                    get_option<bool>( "USE_COLOR_MODULATED_TEXTURES" );
    //initialize the alternate rectangle texture for replacing SDL_RenderFillRect
    if( get_option<bool>( "USE_COLOR_MODULATED_TEXTURES" ) && !software_renderer ) {
        geometry = std::make_unique<ColorModulatedGeometryRenderer>( renderer );
    } else {
        geometry = std::make_unique<DefaultGeometryRenderer>();
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
    lighting::init_render_state_on( ::window.get() );
    // Dear ImGui inits lazily in refresh_display (device readiness isn't
    // guaranteed at WinCreate); torn down in WinDestroy.
}

static void WinDestroy()
{
    // ImGui holds GPU resources on the shared device — tear it down BEFORE the
    // device is destroyed by shutdown_render_state(). No-op if never inited.
    imgui_layer::shutdown();

    // Tear the SDL_GPU lighting stack down before SDL_Quit. Idempotent;
    // safe even if try_init_render_state() never succeeded.
    lighting::shutdown_render_state();

    shutdown_sound();
    tilecontext.reset();

    if( joystick ) {
        SDL_CloseJoystick( joystick );

        joystick = nullptr;
    }
    geometry.reset();
    format = SDL_PIXELFORMAT_UNKNOWN;
    renderer.reset();
    ::legacy_window.reset();
    ::window.reset();
}

static point draw_string( Font &font,
                          const SDL_Renderer_Ptr &renderer,
                          const GeometryRenderer_Ptr &geometry,
                          const std::string &str,
                          point p,
                          unsigned char color );

/// Converts a color from colorscheme to SDL_Color.
inline const SDL_Color &color_as_sdl( const unsigned char color )
{
    return windowsPalette[color];
}

// Draw a cached two-tone SVG widget icon into a sidebar panel cell, tinted by a
// curses color. Square, one cell tall. The icon is uploaded once at the current
// font cell size (re-rasterized on size change) and drawn as an unlit UI sprite
// via the font-glyph queue, so it composites with sidebar text and survives
// partial redraws. No-op when the window is invalid or no GPU device is live.
void draw_widget_icon( const catacurses::window &win, const point &cell,
                       const std::string &icon, const nc_color &color,
                       const sidebar_anim::icon_transform &tr )
{
    cata_cursesport::WINDOW *const w = win.get<cata_cursesport::WINDOW>();
    if( w == nullptr || fontwidth <= 0 || fontheight <= 0 ) {
        return;
    }
    const int px = fontheight; // square icon, one cell tall
    SDL_GPUTexture *tex = widget_icon::get( icon, px );
    if( tex == nullptr ) {
        return;
    }
    const float base = static_cast<float>( px );
    const float wpx = base * tr.scale;                 // uniform scale -> width
    const float hpx = base * tr.scale * tr.scale_y;    // extra vertical squash
    const float cell_x = static_cast<float>( ( w->pos.x + cell.x ) * fontwidth );
    const float cell_y = static_cast<float>( ( w->pos.y + cell.y ) * fontheight );
    // Horizontal: keep centred on the base box. Vertical: anchor at pivot_y, so a
    // scale_y < 1 squashes toward the top (0), centre (0.5) or bottom (1) edge —
    // the "hit from above/below" recoil. With scale_y=1, pivot_y=0.5 this reduces
    // to the plain centred (re)scale.
    const float x = cell_x - ( wpx - base ) * 0.5f;
    const float y = cell_y + tr.pivot_y * ( base - hpx ) + tr.offset_y;
    const SDL_Color c = curses_color_to_SDL( color );
    float r = c.r / 255.f;
    float g = c.g / 255.f;
    float b = c.b / 255.f;
    if( tr.blend > 0.f ) {
        const SDL_Color bc = curses_color_to_SDL( tr.blend_color );
        r += ( bc.r / 255.f - r ) * tr.blend;
        g += ( bc.g / 255.f - g ) * tr.blend;
        b += ( bc.b / 255.f - b ) * tr.blend;
    }
    // Spin: tr.rotation is degrees (clockwise); the instance field is radians.
    constexpr float deg_to_rad = 0.01745329252f;
    lighting::get_render_state().queue_font_glyph(
        tex, x, y, wpx, hpx, r, g, b, tr.alpha, /*lit=*/false,
        /*rotation=*/tr.rotation * deg_to_rad );
}

void draw_widget_icon( const catacurses::window &win, const point &cell,
                       const std::string &icon, const nc_color &color )
{
    draw_widget_icon( win, cell, icon, color, sidebar_anim::icon_transform{} );
}

void draw_widget_row_highlight( const catacurses::window &win, int row, int width_cells,
                                const nc_color &color, float alpha )
{
    cata_cursesport::WINDOW *const w = win.get<cata_cursesport::WINDOW>();
    if( w == nullptr || fontwidth <= 0 || fontheight <= 0 || alpha <= 0.f || width_cells <= 0 ) {
        return;
    }
    const float x = static_cast<float>( w->pos.x * fontwidth );
    const float y = static_cast<float>( ( w->pos.y + row ) * fontheight );
    const float wpx = static_cast<float>( width_cells * fontwidth );
    const float hpx = static_cast<float>( fontheight );
    const SDL_Color c = curses_color_to_SDL( color );
    // queue_ui_rect feeds the UI-rect layer, flushed before font glyphs, so the
    // row text draws over this bar.
    lighting::get_render_state().queue_ui_rect( x, y, wpx, hpx,
            c.r / 255.f, c.g / 255.f, c.b / 255.f, alpha );
}

// Debug overlay state — saved from the previous frame, drawn this frame.
struct TileCoordGlyph {
    float x, y;
    std::string text;
};
struct EmitterOverlayState {
    std::vector<lighting::gpu_emitter> snap;
    float cam_off_x = 0.f, cam_off_y = 0.f, tile_px = 32.f;
    float op_x = 0.f, op_y = 0.f;
    int player_x = 0, player_y = 0, player_z = 0;
    int screen_w = 0, screen_h = 0;
    int map_origin_x = 0, map_origin_y = 0;
    int draw_off_px_x = 0, draw_off_px_y = 0;
    Uint32 last_n_emit_pushed = 0; // actual count pushed to the GPU lp
    // Sampled at submit time so HUD shows what shader actually sees.
    float sdf_at_player = -1.f;
    float trans_at_player = -1.f;
    int   sdf_W_at_submit = 0;
    size_t sdf_size_at_submit = 0;
    // Tier 3 per-tile coord cache.
    std::vector<TileCoordGlyph> tile_labels;
    int cached_player_x = INT_MIN, cached_player_y = INT_MIN;
    float cached_cam_off_x = 0.f, cached_cam_off_y = 0.f;
    float cached_tile_px = 0.f;
    int cached_screen_w = 0, cached_screen_h = 0;
};
static EmitterOverlayState s_emo;
// Master toggle for the lighting debug HUD. Default ON while diagnosing
// the GPU lighting cutover. Flip to false to silence.
static bool g_dbg_lighting = true;
// When true, the fragment shader replaces lighting output with a
// distance/radius heatmap (R = inside emitter radius, G = tile grid,
// B = sky_vis). Implemented as a negative-ambient sentinel; the shader
// checks `ambient < -0.5` and short-circuits to the diagnostic colour.
// Shader heatmap overlay (final_rgb replaced with emitter-light visualisation
// for game tiles). Default OFF — when on AND the SDF is empty, all emitter
// contributions clamp to 0 and the heatmap renders pitch black, masking the
// real ambient floor that would otherwise be visible. Toggle on at runtime
// for emitter-pipeline diagnostics only.
static bool g_dbg_lighting_shader = false;
// Runtime tuning state for shader debug modes. Updated by F-key handlers.
static lighting::debug_params g_dbg_params{};
// Step-3 Phase 2 A/B: sprite GI source — true = GPU radiance cascade (default),
// false = CPU 1-bounce indirect (the oracle). Synced into render_state per frame.
static bool g_gi_use_rc = true;
// Tonemap pass controls (F4 sliders). Pre-AgX exposure (lit world is linear
// light above AgX's 0.18 mid-gray anchor → needs exposing down) + the AgX log2
// EV window (defaults are the canonical AgX range).
static float g_tonemap_exposure = 0.35f;
static float g_tonemap_min_ev = -12.47393f;
static float g_tonemap_max_ev = 4.026069f;
// 1-bounce indirect (fake GI) diffusion controls (F4 sliders). More passes =
// colored light bleeds/bounces deeper; higher decay = more energy per ring.
static int   g_gi_passes = 4;
static float g_gi_decay  = 0.6f;
// Current debug mode display (0-7, cycles through modes).
static uint32_t g_current_dbg_mode = 0u;
// Scale factors for individual light contributions (for tuning visualization).
static float g_emitter_scale = 1.0f;
static float g_sun_scale = 1.0f;
static float g_sky_scale = 1.0f;

// Main-menu decorative-emitter tuning (read by lighting/snapshot.cpp). F-keys:
//   F10 / Shift+F10  — radius input  ± 100 (pre-sqrt; HUD shows 3*sqrt(r))
//   F11 / Shift+F11  — position cycle (top-left / centre / bottom-right)
//   F12              — toggle bright blue debug backdrop (proves bg sprite
//                      reaches the swapchain even when emitter contribution is 0)
namespace menu_emitter_tuning
{
// raw input to make_omni; actual radius = 3·√r. Default 100 → ~30 tiles ≈
// 960 px corner glow at 32 px/tile. Tunable at runtime via F10 / Shift+F10
// (±100). Set very low (< 5) to disable the menu glow visually.
float radius_input = 100.0f;
float pos_x        = 8.5f;
float pos_y        = 4.5f;
int   pos_preset   = 0;         // 0 top-left, 1 centre, 2 bottom-right
bool  blue_backdrop = true;     // true: bright blue, false: black (lit)
}  // namespace menu_emitter_tuning

// Returns true if a curses cell BG fill should be suppressed: only for windows
// flagged transparent_backdrop (the main-menu decorative background) AND when
// the colour is opaque black (the default "empty cell" fill). This lets the
// lit-world emitter glow show through the decorative menu while every other
// window — popups, the OPTIONS panel, in-game UI — paints a solid backdrop and
// stays readable. Inlined; both reads are trivial in the per-cell hot loop.
static inline bool suppress_cell_bg( const cata_cursesport::WINDOW *win,
                                     const SDL_Color &c ) noexcept
{
    if( !win || !win->transparent_backdrop ) {
        return false;
    }
    return c.r == 0 && c.g == 0 && c.b == 0;
}

// Dear ImGui lighting/debug tuning panel (F4). Replaces the hand-rolled
// text+bar HUD with interactive widgets bound to the same globals the renderer
// reads (g_dbg_params, mirrored by g_*_scale / g_current_dbg_mode). Dev-facing
// only. Registered with imgui_layer in the lazy-init block below.
static void draw_lighting_dev_ui()
{
    // Closing via the title-bar X flips visible() too (same flag as F4).
    if( !ImGui::Begin( "Lighting Debug (F4)", &imgui_layer::visible() ) ) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox( "Debug HUD active (F5)", &g_dbg_lighting );
    ImGui::SameLine();
    ImGui::Checkbox( "Shader heatmap (F6)", &g_dbg_lighting_shader );

    static const char *mode_names[9] = {
        "off", "ambient", "emitter", "sun", "sky", "total", "SDF", "sky_vis", "emit_bw"
    };
    int mode = static_cast<int>( g_current_dbg_mode );
    if( ImGui::Combo( "mode (F7)", &mode, mode_names, 9 ) ) {
        g_current_dbg_mode = static_cast<uint32_t>( mode );
        g_dbg_params.debug_mode = g_current_dbg_mode;
    }

    ImGui::SeparatorText( "Light scales" );
    // Edit the g_*_scale mirrors then sync into g_dbg_params (the struct the
    // shader reads) — same path the F8/F9 handlers use, so keys + sliders agree.
    if( ImGui::SliderFloat( "emitter", &g_emitter_scale, 0.0f, 10.0f ) ) {
        g_dbg_params.emitter_scale = g_emitter_scale;
    }
    if( ImGui::SliderFloat( "sun", &g_sun_scale, 0.0f, 10.0f ) ) {
        g_dbg_params.sun_scale = g_sun_scale;
    }
    if( ImGui::SliderFloat( "sky", &g_sky_scale, 0.0f, 10.0f ) ) {
        g_dbg_params.sky_scale = g_sky_scale;
    }

    ImGui::SeparatorText( "Tonemap (AgX)" );
    ImGui::SliderFloat( "exposure", &g_tonemap_exposure, 0.0f, 2.0f );
    ImGui::SliderFloat( "min EV", &g_tonemap_min_ev, -20.0f, 0.0f );
    ImGui::SliderFloat( "max EV", &g_tonemap_max_ev, 0.0f, 12.0f );

    ImGui::SeparatorText( "Dither / GI / shadow" );
    ImGui::SliderFloat( "dither amt", &g_dbg_params.dither_amt, 0.0f, 1.0f );
    ImGui::SliderFloat( "dither bands", &g_dbg_params.dither_bands, 1.0f, 16.0f, "%.0f" );
    ImGui::SliderFloat( "GI strength", &g_dbg_params.gi_strength, 0.0f, 2.0f );
    ImGui::Checkbox( "GI: GPU radiance cascade (off = CPU oracle)", &g_gi_use_rc );
    ImGui::SliderInt( "GI bounces", &g_gi_passes, 0, 12 );
    ImGui::SliderFloat( "GI decay", &g_gi_decay, 0.0f, 0.95f );
    ImGui::SliderFloat( "shadow k", &g_dbg_params.shadow_k, 0.0f, 32.0f );
    int steps = static_cast<int>( g_dbg_params.shadow_steps );
    if( ImGui::SliderInt( "shadow steps", &steps, 1, 64 ) ) {
        g_dbg_params.shadow_steps = static_cast<uint32_t>( std::max( 1, steps ) );
    }

    ImGui::SeparatorText( "Vision (Stoneshard)" );
    // Each knob is independent so a single effect can be zeroed live to bisect.
    // vis curve: soft vision-edge falloff exponent on LIT tiles (0 = off/flat,
    //            >1 = steeper edge). night/day floor: ambient floor lerp'd by
    //            sun_intensity for darker, more immersive nights (equal = off).
    ImGui::SliderFloat( "vis curve", &g_dbg_params.vis_curve, 0.0f, 4.0f );
    ImGui::SliderFloat( "vis radius", &g_dbg_params.vis_radius, 0.0f, 40.0f, "%.1f" );
    ImGui::SliderFloat( "night floor", &g_dbg_params.night_floor, 0.0f, 0.30f );
    ImGui::SliderFloat( "day floor", &g_dbg_params.day_floor, 0.0f, 0.30f );

    ImGui::SeparatorText( "Tone grade (Stoneshard wash)" );
    ImGui::SliderFloat( "desaturate", &g_dbg_params.grade_desat, 0.0f, 1.0f );
    ImGui::SliderFloat( "cool tint", &g_dbg_params.grade_cool, 0.0f, 1.0f );
    ImGui::SliderFloat( "brightness", &g_dbg_params.grade_bright, 0.0f, 1.5f );

    ImGui::SeparatorText( "Memory fade (effect 3)" );
    // mem dim = brightness floor for remembered terrain (1=no dim, persists).
    // mem radius = distance over which memory fades from bright (near) to floor.
    ImGui::SliderFloat( "mem dim", &g_dbg_params.mem_dim, 0.0f, 1.0f );
    ImGui::SliderFloat( "mem radius", &g_dbg_params.mem_radius, 1.0f, 60.0f, "%.0f" );

    // Diagnostics — the former top-left curses HUD, now read-only ImGui text.
    // Reads s_emo (file-scope, populated by the g_dbg_lighting overlay block in
    // refresh_display BEFORE new_frame() runs, so values are current this frame).
    // s_emo is ONLY refreshed while g_dbg_lighting is on, so gate on it to avoid
    // showing frozen stale numbers. Every map access keeps its `g ?` guard — F4
    // is openable on the main menu where g == nullptr.
    ImGui::SeparatorText( "Diagnostics" );
    if( !g_dbg_lighting ) {
        ImGui::TextDisabled( "enable Debug HUD (F5) for live readout" );
    } else {
        auto &rs = lighting::get_render_state();
        const float tp = s_emo.tile_px > 0.f ? s_emo.tile_px : 32.f;
        const float sw = static_cast<float>( s_emo.screen_w );
        const float sh = static_cast<float>( s_emo.screen_h );

        ImGui::Text( "screen=%dx%d  tile_px=%.1f",
                     s_emo.screen_w, s_emo.screen_h, tp );

        const size_t cache_sz = g
                                ? get_map().access_cache( g->u.bub_pos().z() ).transparency_cache.size()
                                : 0;
        const int wanted = g
                           ? ( get_map().getmapsize() * SEEX )
                           * ( get_map().getmapsize() * SEEY )
                           : 0;
        ImGui::Text( "SDF pop=%d  rt=%dx%d  tex=%dx%d  cache=%zu/%d",
                     rs.sdf().populated() ? 1 : 0,
                     rs.sdf().map_w(), rs.sdf().map_h(),
                     rs.sdf().tex_w(), rs.sdf().tex_h(),
                     cache_sz, wanted );

        if( g && cache_sz > 0 ) {
            map &mm = get_map();
            const int H = mm.getmapsize() * SEEY;
            const auto &tc = mm.access_cache( g->u.bub_pos().z() ).transparency_cache;
            const int px = g->u.bub_pos().x();
            const int py = g->u.bub_pos().y();
            auto T = [&]( int x, int y ) -> float {
                const int i = x * H + y;
                return ( i >= 0 && i < static_cast<int>( tc.size() ) ) ? tc[i] : -1.f;
            };
            ImGui::Text( "trans@p=%.3f N=%.3f S=%.3f E=%.3f W=%.3f",
                         T( px, py ), T( px, py - 1 ), T( px, py + 1 ),
                         T( px + 1, py ), T( px - 1, py ) );
            ImGui::Text( "sdf@p=%.3f trans@p(submit)=%.3f sdfW=%d sz=%zu",
                         s_emo.sdf_at_player, s_emo.trans_at_player,
                         s_emo.sdf_W_at_submit, s_emo.sdf_size_at_submit );
        }

        ImGui::Text( "map_origin=(%d,%d)  draw_off_px=(%d,%d)",
                     s_emo.map_origin_x, s_emo.map_origin_y,
                     s_emo.draw_off_px_x, s_emo.draw_off_px_y );
        ImGui::Text( "cam_off=(%.2f,%.2f)  op=(%.0f,%.0f)",
                     s_emo.cam_off_x, s_emo.cam_off_y, s_emo.op_x, s_emo.op_y );
        ImGui::Text( "player=(%d,%d,%d)  emitters=%zu  pushed=%u",
                     s_emo.player_x, s_emo.player_y, s_emo.player_z,
                     s_emo.snap.size(), s_emo.last_n_emit_pushed );

        const float pscr_x = ( s_emo.player_x + s_emo.cam_off_x ) * tp + s_emo.op_x;
        const float pscr_y = ( s_emo.player_y + s_emo.cam_off_y ) * tp + s_emo.op_y;
        ImGui::Text( "player_screen=(%.1f,%.1f)  center=(%.1f,%.1f)",
                     pscr_x, pscr_y, sw * 0.5f, sh * 0.5f );
        const float dx = pscr_x - sw * 0.5f;
        const float dy = pscr_y - sh * 0.5f;
        ImGui::Text( "delta_to_center=(%.1f,%.1f)px  =(%.2f,%.2f)tiles",
                     dx, dy, dx / tp, dy / tp );

        if( !s_emo.snap.empty() ) {
            const lighting::gpu_emitter &e0 = s_emo.snap.front();
            const float ed_x = e0.pos_x - static_cast<float>( s_emo.player_x );
            const float ed_y = e0.pos_y - static_cast<float>( s_emo.player_y );
            const float ed   = std::sqrt( ed_x * ed_x + ed_y * ed_y );
            const char *in_r = ( ed < e0.radius ) ? "INSIDE" : "outside";
            ImGui::Text( "emit[0] pos=(%.1f,%.1f,%.1f) r=%.1f dist=%.2f %s",
                         e0.pos_x, e0.pos_y, e0.pos_z, e0.radius, ed, in_r );
        } else {
            ImGui::TextDisabled( "emit[0] (none)" );
        }
        ImGui::Text( "menu  F10:r_in=%.0f  F11:pos=(%.1f,%.1f)  F12:bgBlue=%s",
                     menu_emitter_tuning::radius_input,
                     menu_emitter_tuning::pos_x, menu_emitter_tuning::pos_y,
                     menu_emitter_tuning::blue_backdrop ? "ON" : "off" );
    }

    ImGui::End();
}

void refresh_display()
{
    needupdate = false;
    lastupdate = SDL_GetTicks();

    if( test_mode ) {
        return;
    }

    // All rendering is GPU-direct. Single pass: clear black, tile sprites,
    // UI rects, font glyphs. D3D12 requires one pass per swapchain texture;
    // set_texture() flushes segments inside the pass so all draw kinds coexist.
    auto &rs = lighting::get_render_state();

    if( !rs.ready() ) {
        return;
    }

    // One-time Dear ImGui init: the first frame the GPU device is actually
    // ready. Device readiness is NOT guaranteed at WinCreate time, so init must
    // be lazy here. No-op once ready(); fail-safe (a failure just means no dev UI).
    if( !imgui_layer::ready() ) {
        imgui_layer::init( rs.device().window_ptr(), rs.device().raw() );
        imgui_layer::set_dev_ui( draw_lighting_dev_ui );
    }

    rs.tile_batcher().begin_frame();
    rs.ui_batcher().begin_frame();
    rs.fonts().begin_frame();

    lighting::frame_context ctx = rs.device().begin_frame();
    if( !ctx.valid() ) {
        return;
    }
    if( !ctx.swapchain_tex ) {
        rs.device().submit_frame( ctx );
        return;
    }

    // Step-3 Phase 2: did the per-tile lighting rebuild this frame? Drives the
    // radiance-cascade gather under the same dirty gate (retain cascade on skip).
    bool rc_rebuild = false;
    // Phase 3+4: build emitter snapshot + per-tile SDF/sky-vis/GI/vision and
    // submit to the collector, THEN flush below onto this same frame's render
    // CB. Built at the frame head (not the tail) so the light data matches this
    // frame's sprites + camera; tail-building left lighting one frame stale (a
    // visible one-frame "snap" on a step). The build itself now lives in
    // lighting/frame_build.cpp (LIGHTING_REWORK_PLAN.md step 0).
    if( rs.collector() ) {
        // Dirty-gate the expensive per-tile build (CPU SDF BFS + GI diffusion +
        // big GPU upload). The per-tile data (transparency/sdf/sky_vis/seen)
        // only changes on a turn tick, a Z change, or a camera pan/scroll — so
        // key on {turn, z, camera-origin} and skip the rebuild when unchanged.
        // The emitter snapshot is built every frame regardless (transient
        // flashes age per-frame); when we skip the per-tile rebuild,
        // build_and_submit_lighting submits empty per-tile vectors and
        // flush_to_render_cb leaves last frame's GPU buffers resident.
        // Busted while the F4 dev panel is visible so live knob tuning stays
        // realtime. NOTE(verify): camera_cache (remote-view consoles) feeds
        // `vis`; if a console updates it without a turn/origin change the gate
        // could show stale vision — confirm in-game, add to the key if so.
        static time_point last_turn = calendar::before_time_starts;
        static int        last_z = INT_MIN;
        static point      last_origin{ INT_MIN, INT_MIN };
        bool rebuild_pertile = true;
        if( g && world_generator && world_generator->active_world ) {
            const time_point now = calendar::turn;
            const int z = g->u.bub_pos().z();
            const point origin = tilecontext
                                 ? tilecontext->get_tile_map_origin().raw()
                                 : point{ INT_MIN, INT_MIN };
            rebuild_pertile = imgui_layer::visible()
                              || now != last_turn || z != last_z
                              || origin != last_origin;
            if( rebuild_pertile ) {
                last_turn = now;
                last_z = z;
                last_origin = origin;
            }
        }
        lighting::frame_lighting_result fr =
            lighting::build_and_submit_lighting( rs, rebuild_pertile,
                    g_dbg_lighting, g_gi_passes, g_gi_decay );
        rc_rebuild = fr.built_pertile;
        if( fr.built_pertile ) {
            s_emo.sdf_at_player      = fr.sdf_at_player;
            s_emo.trans_at_player    = fr.trans_at_player;
            s_emo.sdf_W_at_submit    = fr.sdf_W;
            s_emo.sdf_size_at_submit = fr.sdf_size;
        }
        if( g_dbg_lighting ) {
            s_emo.snap = std::move( fr.snapshot_copy );
        }
    }

    // Drain this frame's pending emitter/SDF/transparency/sky_vis upload onto
    // THIS frame's render command buffer. Single CB = ordered: copy pass runs
    // before the render pass on the GPU, so the fragment shader samples freshly
    // uploaded textures instead of racing a worker-thread CB. Was the root
    // cause of the empty EmitterTex / corrupted SkyVisTex observed earlier.
    if( rs.collector() ) {
        rs.collector()->flush_to_render_cb( ctx.cmd_buffer );
    }

    rs.set_gi_use_rc( g_gi_use_rc ); // A/B: which GI texture the sprite binds

    // Step-3 Phase 2: gather the radiance cascade into rs.rc().cascade_texture()
    // on THIS frame's render CB — after the emitter/SDF upload (its inputs) and
    // before Pass W (its consumer). Rides the per-tile dirty gate (rc_rebuild):
    // on skip frames the pass is not re-run and the cascade texture is retained,
    // exactly like world_target. The sprite reads it as IndirectTex when the F4
    // GI source is RC (the default).
    if( rc_rebuild && rs.sdf().populated() && rs.rc().ready()
        && rs.collector() && rs.sdf().sdf_buffer() ) {
        lighting::rc_params rp{};
        rp.emitter_count = static_cast<std::uint32_t>( std::max( 0, rs.collector()->last_count() ) );
        rp.map_w         = static_cast<std::uint32_t>( rs.sdf().map_w() );
        rp.map_h         = static_cast<std::uint32_t>( rs.sdf().map_h() );
        rp.current_z     = g ? static_cast<float>( g->u.bub_pos().z() ) : 0.0f;
        rp.shadow_k      = g_dbg_params.shadow_k;
        rp.shadow_steps  = g_dbg_params.shadow_steps;
        rs.rc().record( ctx.cmd_buffer,
                        rs.collector()->emitter_buffer(), rs.sdf().sdf_buffer(),
                        rp.map_w, rp.map_h, rp );
    }

    // Phase 6/6b: stamp the per-frame lighting params onto the tile_batcher
    // BEFORE begin_pass. end_pass() reads impl.lp / impl.lp_emitter_buf when
    // it records SDL_BindGPUFragmentSamplers + SDL_BindGPUFragmentStorageBuffers
    // + SDL_PushGPU*UniformData onto the command buffer; doing this AFTER
    // end_pass means the recorded values are one frame stale (camera-drift
    // on motion; frame 0 fully black).
    if( rs.collector() ) {
        // Q10 refactor: assemble the per-frame lighting inputs the caller
        // alone knows (camera + tile geometry + time-of-day + ambient).
        // render_state::begin_lighting_frame() resolves the textures,
        // sampler, emitter count, and SDF dimensions internally from its
        // own subsystems — caller no longer threads those by hand.
        lighting::render_state::frame_light_inputs in{};
        in.tile_pixel_size = tilecontext
                             ? static_cast<float>( tilecontext->get_tile_width() )
                             : 32.0f;
        in.z_level         = g ? static_cast<float>( g->u.bub_pos().z() ) : 0.0f;
        // Restored to 0.05 (was 0.5 as band-aid for stale D3D12 emitter
        // sampler issue — band-aid pre-dates the single-CB
        // flush_to_render_cb rewrite and may no longer be needed. If
        // in-game lighting is broken after this, revert to 0.5 and
        // investigate sampler binding properly.
        in.ambient         = 0.05f;

        // Camera offset converts screen tile units → map tile coords:
        //   map_pos = tile_tu - camera_offset   (see sdf_pass.h comment)
        // On the main menu (g==nullptr) keep cam_off=(0,0) so the
        // decorative emitter coordinates stay consistent with the
        // screen-tile world_pos used by the background sprite.
        if( g && tilecontext && in.tile_pixel_size > 0.0f ) {
            const point map_origin  = tilecontext->get_tile_map_origin().raw();
            const point draw_offset = tilecontext->get_drawing_pixel_offset();
            in.camera_off_x = static_cast<float>( draw_offset.x ) / in.tile_pixel_size
                              - static_cast<float>( map_origin.x );
            in.camera_off_y = static_cast<float>( draw_offset.y ) / in.tile_pixel_size
                              - static_cast<float>( map_origin.y );
            if( g_dbg_lighting ) {
                s_emo.cam_off_x = in.camera_off_x;
                s_emo.cam_off_y = in.camera_off_y;
                s_emo.tile_px   = in.tile_pixel_size;
                s_emo.op_x      = static_cast<float>( draw_offset.x );
                s_emo.op_y      = static_cast<float>( draw_offset.y );
                s_emo.player_x  = g->u.bub_pos().x();
                s_emo.player_y  = g->u.bub_pos().y();
                s_emo.player_z  = g->u.bub_pos().z();
                s_emo.screen_w  = static_cast<int>( ctx.swapchain_w );
                s_emo.screen_h  = static_cast<int>( ctx.swapchain_h );
                s_emo.map_origin_x = map_origin.x;
                s_emo.map_origin_y = map_origin.y;
                s_emo.draw_off_px_x = draw_offset.x;
                s_emo.draw_off_px_y = draw_offset.y;
            }
        } else if( g_dbg_lighting ) {
            // Main menu / no-game path: still update the screen size so
            // the HUD shows a non-zero center cross and the emitter count
            // line reflects the collector state. Player / map-origin /
            // draw-offset all stay zero (no map loaded).
            s_emo.cam_off_x = 0.f;
            s_emo.cam_off_y = 0.f;
            s_emo.tile_px   = in.tile_pixel_size;
            s_emo.op_x      = 0.f;
            s_emo.op_y      = 0.f;
            s_emo.player_x  = 0;
            s_emo.player_y  = 0;
            s_emo.player_z  = 0;
            s_emo.screen_w  = static_cast<int>( ctx.swapchain_w );
            s_emo.screen_h  = static_cast<int>( ctx.swapchain_h );
            s_emo.map_origin_x = 0;
            s_emo.map_origin_y = 0;
            s_emo.draw_off_px_x = 0;
            s_emo.draw_off_px_y = 0;
        }

        // Compute sun/sky params from time-of-day (24h LUT). sun_hour is
        // renamed to avoid shadowing the hour_of_day<T>() template.
        const float sun_hour = g ? hour_of_day<float>( calendar::turn ) : 12.f;
        in.sun = lighting::make_sun_params( sun_hour );
        // Repurpose sun.sp_pad as the shader debug-heatmap sentinel.
        in.sun.sp_pad = g_dbg_lighting_shader ? 1.0f : 0.0f;

        // Runtime debug tuning: wire the globally controlled debug_params into
        // frame_light_inputs. shader uses these for debug visualization (F7 cycles
        // modes, F8/F9 adjust scales). Defaults are all zeroed (no-op).
        in.debug = g_dbg_params;
        // Inject the player map-tile centre as the radial vision-bubble origin
        // (DATA, not a knob — overwrites the unused g_dbg_params slots). Matches
        // the shader world_pos space (= map tile index). Main menu (g==null)
        // leaves it at 0 with vis_radius gating on sdf_map_w>0 anyway.
        if( g ) {
            in.debug.player_x = static_cast<float>( g->u.bub_pos().x() ) + 0.5f;
            in.debug.player_y = static_cast<float>( g->u.bub_pos().y() ) + 0.5f;
        }

        // Debug: log emitter count, texture state, and first emitter data every ~120 frames.
        static int emit_dbg_frame = 0;
        if( ++emit_dbg_frame >= 120 ) {
            emit_dbg_frame = 0;
            dbg( DL::Debug ) << "lighting: n_emit=" << rs.collector()->last_count()
                             << " emitter_buf=" << ( rs.collector()->emitter_buffer() ? "ok" : "NULL" )
                             << " sdf_tex=" << ( rs.sdf().sdf_texture() ? "ok" : "NULL" )
                             << " sampler=" << ( rs.gpu_sampler() ? "ok" : "NULL" )
                             << " cam_off=(" << in.camera_off_x << "," << in.camera_off_y << ")"
                             << " sdf=" << rs.sdf().map_w() << "x" << rs.sdf().map_h()
                             << " z=" << in.z_level
                             << " ambient=" << in.ambient
                             << " tile_px=" << in.tile_pixel_size;
        }
        s_emo.last_n_emit_pushed = static_cast<Uint32>( rs.collector()->last_count() );

        rs.begin_lighting_frame( in );
    }

    // Phase 8 main-menu background: when no world is loaded, inject a fullscreen
    // tile sprite (tint=0, game-tile mode) so the warm amber decorative emitter
    // shows as a lit gradient behind the UI text.  Added only when the tile queue
    // is empty (once per redraw cycle) to prevent stacking across frames.
    // Gate on active_world rather than !g: g is created before the main menu is
    // shown, so !g misses the menu state — must match snapshot.cpp:207.
    const bool no_world = !g || !world_generator || !world_generator->active_world;
    if( no_world && rs.tile_sprites_empty() && rs.geometry().white_texture() ) {
        lighting::sprite_instance bg{};
        bg.dst_x  = 0.f;
        bg.dst_y  = 0.f;
        bg.dst_w  = static_cast<float>( ctx.swapchain_w );
        bg.dst_h  = static_cast<float>( ctx.swapchain_h );
        bg.src_u  = 0.f;  bg.src_v  = 0.f;
        bg.src_uw = 1.f;  bg.src_vh = 1.f;
        // Debug backdrop (F12): bright blue floor — proves the bg sprite reaches
        // the swapchain and that the fragment shader can light a non-tile sprite.
        // Shader composites via max(tint, gpu_total), so a blue floor keeps the
        // bg visible AND any emitter contribution >0.3 in R/G shines through.
        // When toggled off, tint=0 ("game-tile mode") yields pure lit output.
        if( menu_emitter_tuning::blue_backdrop ) {
            bg.tint_r = 0.0f;  bg.tint_g = 0.0f;  bg.tint_b = 0.3f;
        } else {
            bg.tint_r = 0.0f;  bg.tint_g = 0.0f;  bg.tint_b = 0.0f;
        }
        bg.tint_a = 1.f;
        bg.rotation = 0.f;
        rs.queue_tile_sprite( rs.geometry().white_texture(), bg );
    }

    constexpr float clear_black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    // HiDPI: viewport (target_w/h) = physical swapchain so the rasterizer
    // fills the full framebuffer; projection (proj_w/h) = logical window
    // size so the shader's pixel→NDC math matches the logical coords the
    // UI / fonts queue draws at. GPU stretches logical → physical.
    int proj_w = 0;
    int proj_h = 0;
    SDL_GetWindowSize( ::window.get(), &proj_w, &proj_h );
    if( proj_w <= 0 || proj_h <= 0 ) {
        proj_w = static_cast<int>( ctx.swapchain_w );
        proj_h = static_cast<int>( ctx.swapchain_h );
    }

    // Phase 3: the UI compositor Pass A, the swapchain pass, and the composite
    // blit now run AFTER the transient LIGHT-DBG HUD is generated (below), so
    // Pass A can drain the transient queues into the compositor. The HUD block
    // is pure queue pushes (no open pass required), so it runs here first.
    // Lighting debug HUD. Tiers:
    //   1: top-left text strip (screen dims, tile_px, cam_off, player, op, n_emit)
    //   2: emitter markers + player + screen-center crosses
    //   3: per-tile (x,y) coord labels — cached, rebuilt on player/camera change
    //   4: tile grid lines (1px) at every tile boundary
    //
    // Route all overlay pushes (this block + the tuning widget below) into
    // render_state's per-frame transient queues. refresh_display runs every
    // frame, but ui_manager's clear_ui_queues() only fires on a redraw
    // cycle — without transient routing these pushes would accumulate on
    // no-input frames and ghost over composited UI slices. RAII guard
    // ensures the flag clears even if a push path throws.
    struct transient_routing_guard {
        lighting::render_state &rs;
        ~transient_routing_guard()
        {
            rs.set_transient_routing( false );
        }
    } _t_route{ rs };
    rs.set_transient_routing( true );

    // Render the HUD on the main menu too so we can verify the decorative
    // amber emitter is being collected/uploaded. Player / tile-coord tiers
    // are skipped when no game is loaded (gated below on `g`).
    if( g_dbg_lighting ) {
        constexpr float OL_PI = 3.14159265358979323846f;
        const float tp  = s_emo.tile_px > 0.f ? s_emo.tile_px : 32.f;
        const float sw  = static_cast<float>( ctx.swapchain_w );
        const float sh  = static_cast<float>( ctx.swapchain_h );

        // ── Tier 4: grid lines ─────────────────────────────────────────────
        // Vertical and horizontal 1px lines aligned to the tile lattice.
        // Anchor on op (drawing pixel offset) so lines coincide with sprite edges.
        {
            const float anchor_x = std::fmod( s_emo.op_x, tp );
            const float anchor_y = std::fmod( s_emo.op_y, tp );
            for( float x = anchor_x; x < sw; x += tp ) {
                rs.queue_ui_rect( x, 0.f, 1.f, sh, 0.25f, 0.25f, 0.30f, 0.35f );
            }
            for( float y = anchor_y; y < sh; y += tp ) {
                rs.queue_ui_rect( 0.f, y, sw, 1.f, 0.25f, 0.25f, 0.30f, 0.35f );
            }
        }

        // ── Tier 2: emitter markers (solid dot + dotted ring) ──────────────
        static bool emo_cam_logged = false;
        if( !emo_cam_logged ) {
            emo_cam_logged = true;
            dbg( DL::Debug ) << "overlay: cam=(" << s_emo.cam_off_x << ","
                             << s_emo.cam_off_y << ") tile_px=" << s_emo.tile_px
                             << " op=(" << s_emo.op_x << "," << s_emo.op_y
                             << ") snap=" << s_emo.snap.size();
        }
        for( const auto &e : s_emo.snap ) {
            const float sx  = ( e.pos_x + s_emo.cam_off_x ) * tp + s_emo.op_x;
            const float sy  = ( e.pos_y + s_emo.cam_off_y ) * tp + s_emo.op_y;
            const float rpx = e.radius * tp;
            const float cr  = e.r > 0.01f ? e.r : 1.0f;
            const float cg  = e.g > 0.01f ? e.g : 1.0f;
            const float cb  = e.b > 0.01f ? e.b : 1.0f;
            // Bright filled core.
            rs.queue_ui_rect( sx - 4.f, sy - 4.f, 8.f, 8.f, cr, cg, cb, 1.0f );
            // Dotted ring at radius.
            for( int i = 0; i < 48; ++i ) {
                const float a = 2.0f * OL_PI * static_cast<float>( i ) / 48.0f;
                rs.queue_ui_rect( sx + std::cos( a ) * rpx - 2.f,
                                  sy + std::sin( a ) * rpx - 2.f,
                                  4.f, 4.f, cr, cg, cb, 0.75f );
            }
        }

        // Player cross (bright green) at map-coord player pos.
        {
            const float px = ( s_emo.player_x + s_emo.cam_off_x ) * tp + s_emo.op_x;
            const float py = ( s_emo.player_y + s_emo.cam_off_y ) * tp + s_emo.op_y;
            rs.queue_ui_rect( px - 12.f, py - 1.f, 24.f, 2.f, 0.f, 1.f, 0.f, 1.f );
            rs.queue_ui_rect( px - 1.f, py - 12.f, 2.f, 24.f, 0.f, 1.f, 0.f, 1.f );
        }
        // Screen-center cross (cyan).
        {
            const float cx = sw * 0.5f;
            const float cy = sh * 0.5f;
            rs.queue_ui_rect( cx - 10.f, cy - 1.f, 20.f, 2.f, 0.f, 1.f, 1.f, 0.9f );
            rs.queue_ui_rect( cx - 1.f, cy - 10.f, 2.f, 20.f, 0.f, 1.f, 1.f, 0.9f );
        }

        // ── Tier 3: per-tile (x,y) coord labels, cached on player move ─────
        // Labels span ~5–6 glyphs at small font width and easily exceed a 32px
        // tile, smearing horizontally. Restrict to a small box around the
        // player and label every other tile so they stay legible.
        constexpr int TIER3_RADIUS = 6;   // tiles each side of player
        constexpr int TIER3_STEP   = 2;   // every Nth tile
        const bool cache_stale =
            s_emo.player_x != s_emo.cached_player_x ||
            s_emo.player_y != s_emo.cached_player_y ||
            s_emo.cam_off_x != s_emo.cached_cam_off_x ||
            s_emo.cam_off_y != s_emo.cached_cam_off_y ||
            s_emo.tile_px != s_emo.cached_tile_px ||
            s_emo.screen_w != s_emo.cached_screen_w ||
            s_emo.screen_h != s_emo.cached_screen_h;
        if( cache_stale && tp >= 16.f ) {
            s_emo.tile_labels.clear();
            const int mx0 = s_emo.player_x - TIER3_RADIUS;
            const int my0 = s_emo.player_y - TIER3_RADIUS;
            const int mx1 = s_emo.player_x + TIER3_RADIUS;
            const int my1 = s_emo.player_y + TIER3_RADIUS;
            for( int my = my0; my <= my1; my += TIER3_STEP ) {
                for( int mx = mx0; mx <= mx1; mx += TIER3_STEP ) {
                    const float tx = ( mx + s_emo.cam_off_x ) * tp + s_emo.op_x + 1.f;
                    const float ty = ( my + s_emo.cam_off_y ) * tp + s_emo.op_y + 1.f;
                    s_emo.tile_labels.push_back( {
                        tx, ty,
                        std::to_string( mx ) + "," + std::to_string( my )
                    } );
                }
            }
            s_emo.cached_player_x  = s_emo.player_x;
            s_emo.cached_player_y  = s_emo.player_y;
            s_emo.cached_cam_off_x = s_emo.cam_off_x;
            s_emo.cached_cam_off_y = s_emo.cam_off_y;
            s_emo.cached_tile_px   = tp;
            s_emo.cached_screen_w  = s_emo.screen_w;
            s_emo.cached_screen_h  = s_emo.screen_h;
        }
        if( font ) {
            for( const TileCoordGlyph &g_lbl : s_emo.tile_labels ) {
                // Tiny dark backdrop so labels remain readable over sprites.
                const float lw = static_cast<float>( g_lbl.text.size() ) *
                                 static_cast<float>( font->width );
                rs.queue_ui_rect( g_lbl.x - 1.f, g_lbl.y - 1.f,
                                  lw + 2.f, static_cast<float>( font->height ) + 2.f,
                                  0.f, 0.f, 0.f, 0.7f );
                draw_string( *font, renderer, geometry, g_lbl.text,
                             point( static_cast<int>( g_lbl.x ),
                                    static_cast<int>( g_lbl.y ) ),
                             14 ); // 14 = yellow
            }
        }

        // ── Tier 1: top-left HUD strip — MIGRATED to the F4 ImGui panel ────
        // The text readout now lives in draw_lighting_dev_ui() under the
        // "Diagnostics" header (reads the same s_emo). Removed here to stop the
        // double-render: the spatial overlays above (grid/markers/crosses/
        // labels) stay because they are world-aligned, not a top-left panel.
    }

    // Lighting tuning widget (F-key text+bar HUD) — REMOVED, fully migrated to
    // the F4 ImGui panel (draw_lighting_dev_ui). All its knobs are now interactive
    // sliders/combo there. The raw F-key handlers (F5–F12) still mutate the globals
    // for users without the ImGui panel; only the on-screen text widget is gone.

    // The transient LIGHT-DBG HUD (above) is re-pushed every frame and animates
    // (emitter markers + crosses track the camera), so force a recomposite
    // whenever it is active. clear_ui_queues() only invalidates on a ui_manager
    // redraw cycle, which the HUD does not go through.
    lighting::ui_composite_target *uct = rs.ui_target();
    if( uct && g_dbg_lighting ) {
        uct->invalidate();
    }

    // ── UI compositor Pass A (phase 4: dirty-gated) ────────────────────────
    // Re-render the UI into the compositor ONLY when something invalidated it
    // (a ui_manager redraw cycle via clear_ui_queues, the resize hook, or the
    // HUD above). On a clean frame Pass A is skipped and Pass B reuses the
    // persistent compositor texture from the last composite — this is the
    // partial-redraw flicker fix (no black sidebar when only a tooltip redrew).
    //
    // STICKY: the pass runs only when dirty AND there is UI to draw. A
    // transient-empty queue does NOT clear the compositor — the last composite
    // is retained and reused by the Pass B blit. consume_dirty() is guarded
    // behind any_ui (short-circuit) so the dirty flag is preserved across empty
    // frames and the composite happens once content returns. (An always-clear
    // variant blanked the whole UI on any frame the queue briefly emptied.)
    //
    // Two begin_pass/end_pass cycles on one batcher in one command buffer is
    // safe: end_pass uploads instances with cycle=true (fresh backing per pass)
    // and Pass A targets the compositor texture, not the swapchain.
    const bool any_ui = !rs.ui_rects_empty() || !rs.font_glyphs_empty();
    if( uct && uct->texture() && any_ui && uct->consume_dirty() ) {
        constexpr float clear_transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        rs.tile_batcher().begin_pass( ctx.cmd_buffer, uct->texture(),
                                      uct->width(), uct->height(),
                                      clear_transparent,
                                      static_cast<std::uint32_t>( proj_w ),
                                      static_cast<std::uint32_t>( proj_h ) );
        if( !rs.ui_rects_empty() && rs.geometry().white_texture() ) {
            rs.tile_batcher().set_texture( rs.geometry().white_texture(),
                                           rs.gpu_sampler(), /*is_lit=*/false );
            rs.flush_ui_rects( rs.tile_batcher() );
        }
        if( !rs.font_glyphs_empty() && rs.gpu_sampler() ) {
            rs.flush_font_glyphs( rs.tile_batcher(), rs.gpu_sampler() );
        }
        rs.tile_batcher().end_pass();
    }

    // ── Pass W: world accumulation ─────────────────────────────────────────
    // Render the lit-world tile sprites into the PERSISTENT world_target.
    //
    // Retention comes from SKIPPING the pass, not from the load-op: a frame that
    // enqueues no tiles (partial UI redraw → tile queue head-cleared in
    // redraw_invalidated, map adaptor not re-invalidated) doesn't run the pass
    // at all, so the last world is RETAINED instead of flashing black — the
    // in-game whole-screen-black flicker fix.
    //
    // When the pass DOES run it always LOADOP_CLEARs (like the old swapchain
    // tile pass), then repaints the full tile set. LOADOP_LOAD here would buy no
    // retention (the skip already does that) and would smear stale pixels wher-
    // ever a frame lacks full opaque coverage (unseen tiles, scroll edges,
    // overlay-only frames). needs_clear (the target's dirty flag, init + resize)
    // forces the pass to run at least once so the texture starts defined / resize
    // garbage is wiped even before any tiles exist. Lighting is already stamped
    // (begin_lighting_frame above); end_pass binds it for the lit tile segments.
    lighting::ui_composite_target *wt = rs.world_target();
    if( wt && wt->texture() ) {
        const bool needs_clear = wt->consume_dirty();
        const bool have_tiles  = !rs.tile_sprites_empty() && rs.gpu_sampler();
        if( needs_clear || have_tiles ) {
            // target_format = wt->format() selects the batcher's pipeline for
            // this target. Swapchain 8-bit in step 1a; RGBA16F once 1b flips
            // world_target — the per-format pipeline cache builds the HDR
            // variant on first use.
            rs.tile_batcher().begin_pass( ctx.cmd_buffer, wt->texture(),
                                          wt->width(), wt->height(),
                                          clear_black,
                                          static_cast<std::uint32_t>( proj_w ),
                                          static_cast<std::uint32_t>( proj_h ),
                                          wt->format() );
            if( have_tiles ) {
                rs.flush_tile_sprites( rs.tile_batcher(), rs.gpu_sampler() );
            }
            rs.tile_batcher().end_pass();
        }
    }

    // ── Pass T: tonemap resolve ────────────────────────────────────────────
    // Resolve the (HDR, once 1b lands) world_target through the tonemap pass
    // into the LDR world_ldr_target that Pass B blits. wt persists across
    // partial-redraw frames, so running this every frame keeps the LDR copy in
    // sync without coupling to whether Pass W ran. Identity shader in 1a/1b →
    // pixel-identical; AgX in 1c. Self-contained pass on world_ldr (no swapchain
    // pass conflict).
    lighting::ui_composite_target *wldr = rs.world_ldr_target();
    if( wt && wt->texture() && wldr && wldr->texture() && rs.gpu_sampler()
        && rs.tonemap().ready() ) {
        rs.tonemap().record( ctx.cmd_buffer, wt->texture(), rs.gpu_sampler(),
                             wldr->texture(), wldr->width(), wldr->height(),
                             g_tonemap_exposure, g_tonemap_min_ev, g_tonemap_max_ev );
    }

    // Dear ImGui dev UI: build the frame and upload its vertex/index buffers
    // OUTSIDE any render pass (prepare opens its own GPU copy pass), then draw
    // it as the LAST thing inside Pass B via the end_pass overlay so it shares
    // the single swapchain pass (D3D12 drops a 2nd pass on the same target).
    const bool imgui_active = imgui_layer::ready() && imgui_layer::visible();
    if( imgui_active ) {
        imgui_layer::new_frame();
        imgui_layer::prepare( ctx.cmd_buffer );
    }

    // ── Pass B: swapchain composite ────────────────────────────────────────
    // World (opaque) then UI (straight-alpha) blitted over the swapchain as
    // fullscreen quads. No direct tile/UI flush here — both layers are
    // persistent textures, so a partial redraw never blanks either layer.
    rs.tile_batcher().begin_pass( ctx.cmd_buffer, ctx.swapchain_tex,
                                  ctx.swapchain_w, ctx.swapchain_h,
                                  clear_black,
                                  static_cast<std::uint32_t>( proj_w ),
                                  static_cast<std::uint32_t>( proj_h ) );

    auto blit_layer = [&]( lighting::ui_composite_target *layer ) {
        if( !layer || !layer->texture() || !rs.gpu_sampler() ) {
            return;
        }
        lighting::sprite_instance quad{};
        quad.dst_x  = 0.f;          quad.dst_y  = 0.f;
        quad.dst_w  = static_cast<float>( proj_w );
        quad.dst_h  = static_cast<float>( proj_h );
        quad.src_u  = 0.f;  quad.src_v  = 0.f;
        quad.src_uw = 1.f;  quad.src_vh = 1.f;
        quad.tint_r = 1.f;  quad.tint_g = 1.f;
        quad.tint_b = 1.f;  quad.tint_a = 1.f;
        quad.rotation = 0.f;
        rs.tile_batcher().set_texture( layer->texture(), rs.gpu_sampler(),
                                       /*is_lit=*/false );
        rs.tile_batcher().draw( quad );
    };
    blit_layer( rs.world_ldr_target() ); // tonemapped world (opaque base)
    blit_layer( uct );                   // UI over the world (straight alpha)

    rs.tile_batcher().end_pass(
        imgui_active
        ? lighting::sprite_batcher::pass_overlay_fn(
    []( SDL_GPURenderPass * rp, SDL_GPUCommandBuffer * cb ) {
        imgui_layer::render_in_pass( rp, cb );
    } )
        : lighting::sprite_batcher::pass_overlay_fn{} );

    rs.device().submit_frame( ctx );
}

// only update if the set interval has elapsed
static void try_sdl_update()
{
    Uint64 now = SDL_GetTicks();
    if( now - lastupdate >= interval ) {
        refresh_display();
    } else {
        needupdate = true;
    }
}

// No-op: display_buffer removed. Remains until atlas lookup uses GPU-native
// keys and SDL_Renderer can be removed entirely (2i-B-7f Part B).
void set_displaybuffer_rendertarget() {}

static void invalidate_framebuffer( std::vector<curseline> &framebuffer, point p, int width,
                                    int height )
{
    for( int j = 0, fby = p.y; j < height; j++, fby++ ) {
        std::fill_n( framebuffer[fby].chars.begin() + p.x, width, cursecell( "" ) );
    }
}

static void invalidate_framebuffer( std::vector<curseline> &framebuffer )
{
    for( curseline &i : framebuffer ) {
        std::fill_n( i.chars.begin(), i.chars.size(), cursecell( "" ) );
    }
}

void reinitialize_framebuffer( const bool force_invalidate )
{
    static int prev_height = -1;
    static int prev_width = -1;
    //Re-initialize the framebuffer with new values.
    const int new_height = std::max( { TERMY, OVERMAP_WINDOW_HEIGHT, TERRAIN_WINDOW_HEIGHT } );
    const int new_width = std::max( { TERMX, OVERMAP_WINDOW_WIDTH, TERRAIN_WINDOW_WIDTH } );
    if( new_height != prev_height || new_width != prev_width ) {
        prev_height = new_height;
        prev_width = new_width;
        oversized_framebuffer.resize( new_height );
        for( int i = 0; i < new_height; i++ ) {
            oversized_framebuffer[i].chars.assign( new_width, cursecell( "" ) );
        }
        terminal_framebuffer.resize( new_height );
        for( int i = 0; i < new_height; i++ ) {
            terminal_framebuffer[i].chars.assign( new_width, cursecell( "" ) );
        }
    } else if( force_invalidate || need_invalidate_framebuffers ) {
        need_invalidate_framebuffers = false;
        invalidate_framebuffer( oversized_framebuffer );
        invalidate_framebuffer( terminal_framebuffer );
    }
}

static void invalidate_framebuffer_proportion( cata_cursesport::WINDOW *win )
{
    const int oversized_width = std::max( TERMX, std::max( OVERMAP_WINDOW_WIDTH,
                                          TERRAIN_WINDOW_WIDTH ) );
    const int oversized_height = std::max( TERMY, std::max( OVERMAP_WINDOW_HEIGHT,
                                           TERRAIN_WINDOW_HEIGHT ) );

    // check if the framebuffers/windows have been prepared yet
    if( oversized_height == 0 || oversized_width == 0 ) {
        return;
    }
    if( !g || win == nullptr ) {
        return;
    }
    if( win == g->w_overmap || win == g->w_terrain ) {
        return;
    }

    // track the dimensions for conversion
    const point termpixel( win->pos.x * font->width, win->pos.y * font->height );
    const int termpixel_x2 = termpixel.x + win->width * font->width - 1;
    const int termpixel_y2 = termpixel.y + win->height * font->height - 1;

    if( map_font != nullptr && map_font->width != 0 && map_font->height != 0 ) {
        const int mapfont_x = termpixel.x / map_font->width;
        const int mapfont_y = termpixel.y / map_font->height;
        const int mapfont_x2 = std::min( termpixel_x2 / map_font->width, oversized_width - 1 );
        const int mapfont_y2 = std::min( termpixel_y2 / map_font->height, oversized_height - 1 );
        const int mapfont_width = mapfont_x2 - mapfont_x + 1;
        const int mapfont_height = mapfont_y2 - mapfont_y + 1;
        invalidate_framebuffer( oversized_framebuffer, point( mapfont_x, mapfont_y ), mapfont_width,
                                mapfont_height );
    }

    if( overmap_font != nullptr && overmap_font->width != 0 && overmap_font->height != 0 ) {
        const int overmapfont_x = termpixel.x / overmap_font->width;
        const int overmapfont_y = termpixel.y / overmap_font->height;
        const int overmapfont_x2 = std::min( termpixel_x2 / overmap_font->width, oversized_width - 1 );
        const int overmapfont_y2 = std::min( termpixel_y2 / overmap_font->height,
                                             oversized_height - 1 );
        const int overmapfont_width = overmapfont_x2 - overmapfont_x + 1;
        const int overmapfont_height = overmapfont_y2 - overmapfont_y + 1;
        invalidate_framebuffer( oversized_framebuffer, point( overmapfont_x, overmapfont_y ),
                                overmapfont_width,
                                overmapfont_height );
    }
}

// clear the framebuffer when werase is called on certain windows that don't use the main terminal font
void cata_cursesport::handle_additional_window_clear( WINDOW *win )
{
    if( !g ) {
        return;
    }
    if( win == g->w_terrain || win == g->w_overmap ) {
        invalidate_framebuffer( oversized_framebuffer );
    }
}

void clear_window_area( const catacurses::window &win_ )
{
    cata_cursesport::WINDOW *const win = win_.get<cata_cursesport::WINDOW>();
    geometry->rect( renderer, point( win->pos.x * fontwidth, win->pos.y * fontheight ),
                    win->width * fontwidth, win->height * fontheight, color_as_sdl( catacurses::black ) );
}

void cata_cursesport::set_window_transparent_backdrop( const catacurses::window &win,
        bool transparent )
{
    if( cata_cursesport::WINDOW *const w = win.get<cata_cursesport::WINDOW>() ) {
        w->transparent_backdrop = transparent;
    }
}

static std::optional<std::pair<tripoint_abs_omt, std::string>> get_mission_arrow(
            const inclusive_cuboid<tripoint_abs_omt> &overmap_area, const tripoint_abs_omt &center )
{
    const auto *mission = get_avatar().get_active_mission();
    const bool custom_waypoint_valid = get_avatar().get_custom_mission_target() !=
                                       overmap::invalid_tripoint;
    if( mission == nullptr && !custom_waypoint_valid ) {
        return std::nullopt;
    }
    if( ( mission == nullptr || !mission->has_target() ) && !custom_waypoint_valid ) {
        return std::nullopt;
    }
    tripoint_abs_omt mission_target = custom_waypoint_valid
                                      ? get_avatar().get_custom_mission_target()
                                      : get_avatar().get_active_mission_target();  // Safe here because mission is non-null

    std::string mission_arrow_variant;
    if( overmap_area.contains( mission_target ) ) {
        mission_arrow_variant = "mission_cursor";
        return std::make_pair( mission_target, mission_arrow_variant );
    }

    inclusive_rectangle<point_abs_omt> area_flat( overmap_area.p_min.xy(), overmap_area.p_max.xy() );
    if( area_flat.contains( mission_target.xy() ) ) {
        int area_z = center.z();
        if( mission_target.z() > area_z ) {
            mission_arrow_variant = "mission_arrow_up";
        } else {
            mission_arrow_variant = "mission_arrow_down";
        }
        return std::make_pair( tripoint_abs_omt( mission_target.xy(), area_z ), mission_arrow_variant );
    }

    const std::vector<tripoint_abs_omt> traj = line_to( center,
            tripoint_abs_omt( mission_target.xy(), center.z() ) );

    if( traj.empty() ) {
        debugmsg( "Failed to gen overmap mission trajectory %s %s",
                  center.to_string(), mission_target.to_string() );
        return std::nullopt;
    }

    tripoint_abs_omt arr_pos = traj[0];
    for( auto it = traj.rbegin(); it != traj.rend(); it++ ) {
        if( overmap_area.contains( *it ) ) {
            arr_pos = *it;
            break;
        }
    }

    const int north_border_y = ( overmap_area.p_max.y() - overmap_area.p_min.y() ) / 3;
    const int south_border_y = north_border_y * 2;
    const int west_border_x = ( overmap_area.p_max.x() - overmap_area.p_min.x() ) / 3;
    const int east_border_x = west_border_x * 2;

    tripoint_abs_omt north_pmax( overmap_area.p_max );
    north_pmax.y() = overmap_area.p_min.y() + north_border_y;
    tripoint_abs_omt south_pmin( overmap_area.p_min );
    south_pmin.y() += south_border_y;
    tripoint_abs_omt west_pmax( overmap_area.p_max );
    west_pmax.x() = overmap_area.p_min.x() + west_border_x;
    tripoint_abs_omt east_pmin( overmap_area.p_min );
    east_pmin.x() += east_border_x;

    const inclusive_cuboid<tripoint_abs_omt> north_sector( overmap_area.p_min, north_pmax );
    const inclusive_cuboid<tripoint_abs_omt> south_sector( south_pmin, overmap_area.p_max );
    const inclusive_cuboid<tripoint_abs_omt> west_sector( overmap_area.p_min, west_pmax );
    const inclusive_cuboid<tripoint_abs_omt> east_sector( east_pmin, overmap_area.p_max );

    mission_arrow_variant = "mission_arrow_";
    if( north_sector.contains( arr_pos ) ) {
        mission_arrow_variant += 'n';
    } else if( south_sector.contains( arr_pos ) ) {
        mission_arrow_variant += 's';
    }
    if( west_sector.contains( arr_pos ) ) {
        mission_arrow_variant += 'w';
    } else if( east_sector.contains( arr_pos ) ) {
        mission_arrow_variant += 'e';
    }

    return std::make_pair( tripoint_abs_omt( arr_pos ), mission_arrow_variant );
}

std::string cata_tiles::get_omt_id_rotation_and_subtile(
    const tripoint_abs_omt &omp, int &rota, int &subtile )
{
    auto oter_at = []( const tripoint_abs_omt & p ) {
        const oter_id &cur_ter = ACTIVE_OVERMAP_BUFFER.ter( p );

        if( !uistate.overmap_show_forest_trails &&
            is_ot_match( "forest_trail", cur_ter, ot_match_type::type ) ) {
            return oter_id( "forest" );
        }

        return cur_ter;
    };

    oter_id ot_id = oter_at( omp );
    const oter_t &ot = *ot_id;
    oter_type_id ot_type_id = ot.get_type_id();
    const oter_type_t &ot_type = *ot_type_id;

    if( ot_type.has_connections() ) {
        // This would be for connected terrain

        // get terrain neighborhood
        const oter_type_id neighborhood[4] = {
            oter_at( omp + point_south )->get_type_id(),
            oter_at( omp + point_east )->get_type_id(),
            oter_at( omp + point_west )->get_type_id(),
            oter_at( omp + point_north )->get_type_id()
        };

        char val = 0;

        // populate connection information
        for( int i = 0; i < 4; ++i ) {
            if( ot_type.connects_to( neighborhood[i] ) ) {
                val += 1 << i;
            }
        }

        get_rotation_and_subtile( val, rota, subtile );
    } else {
        // 'Regular', nonlinear terrain only needs to worry about rotation, not
        // subtile
        ot.get_rotation_and_subtile( rota, subtile );
    }

    return ot_type_id.id().str();
}

static point draw_string( Font &font,
                          const SDL_Renderer_Ptr &renderer,
                          const GeometryRenderer_Ptr &geometry,
                          const std::string &str,
                          point p,
                          const unsigned char color )
{
    const char *cstr = str.c_str();
    int len = str.length();
    while( len > 0 ) {
        const uint32_t ch32 = UTF8_getch( &cstr, &len );
        const std::string ch = utf32_to_utf8( ch32 );
        font.OutputChar( renderer, geometry, ch, p, color );
        p.x += mk_wcwidth( ch32 ) * font.width;
    }
    return p;
}

void draw_sdl_text_outlined( const sdl_text_outline_options &opts )
{
    if( !font || !renderer || opts.text.empty() ) { return; }

    const auto outline_thickness = std::max( 0, opts.outline_thickness );
    for( auto y = -outline_thickness; y <= outline_thickness; ++y ) {
        for( auto x = -outline_thickness; x <= outline_thickness; ++x ) {
            if( x != 0 || y != 0 ) {
                draw_string( *font, renderer, geometry, opts.text, opts.pos_pixel + point( x, y ),
                             static_cast<unsigned char>( opts.outline_color ) );
            }
        }
    }
    draw_string( *font, renderer, geometry, opts.text, opts.pos_pixel,
                 static_cast<unsigned char>( opts.text_color ) );
}

void cata_tiles::draw_om( point dest, const tripoint_abs_omt &center_abs_omt, bool blink )
{
    if( !g ) {
        return;
    }

    // clear_frame_queues() was already called by redraw_invalidated() before
    // this callback runs. Only clear the tile sprite queue so that overmap
    // tiles don't accumulate across ticks; UI queues are owned by the
    // enclosing redraw_invalidated() cycle.
    if( auto *rs = &lighting::get_render_state(); rs->ready() ) {
        rs->clear_tile_queue();
    }

    int width = OVERMAP_WINDOW_TERM_WIDTH * font->width;
    int height = OVERMAP_WINDOW_TERM_HEIGHT * font->height;

    {
        // GPU scissor — clips overmap tile sprites to the overmap viewport.
        SDL_Rect clipRect = { dest.x, dest.y, width, height };
        lighting::get_render_state().set_tile_scissor( &clipRect );

        //fill render area with black to prevent artifacts where no new pixels are drawn
        geometry->rect( renderer, point{ clipRect.x, clipRect.y }, clipRect.w, clipRect.h, SDL_Color() );
    }

    point s;
    get_window_tile_counts( width, height, s.x, s.y );

    op = point( dest.x * fontwidth, dest.y * fontheight );
    // Rounding up to include incomplete tiles at the bottom/right edges
    screentile_width = divide_round_up( width, tile_width );
    screentile_height = divide_round_up( height, tile_height );

    const int min_col = 0;
    const int max_col = s.x;
    const int min_row = 0;
    const int max_row = s.y;
    int height_3d = 0;
    avatar &you = get_avatar();
    const tripoint_abs_omt avatar_pos = you.abs_omt_pos();
    const tripoint_abs_omt corner_NW = center_abs_omt - point( max_col / 2, max_row / 2 );
    const tripoint_abs_omt corner_SE = corner_NW + point( max_col - 1, max_row - 1 );
    const inclusive_cuboid<tripoint_abs_omt> overmap_area( corner_NW, corner_SE );
    // Debug vision allows seeing everything
    const bool has_debug_vision = you.has_trait( trait_id( "DEBUG_NIGHTVISION" ) );
    // sight_points is hoisted for speed reasons.
    const int sight_points = !has_debug_vision ?
                             you.overmap_sight_range( g->light_level( you.bub_pos().z() ) ) :
                             100;
    const bool showhordes = uistate.overmap_show_hordes;
    const bool viewing_weather = ( ( uistate.overmap_debug_weather || uistate.overmap_visible_weather )
                                   && center_abs_omt.z() >= 0 );
    o = corner_NW.xy().reinterpret_as<point_bub_ms>();

    const auto global_omt_to_draw_position = []( const tripoint_abs_omt & omp ) {
        // z position is hardcoded to 0 because the things this will be used to draw should not be skipped
        return tripoint_bub_ms( omp.x(), omp.y(), 0 );
    };
    const auto has_player_label = [&]( const tripoint_abs_omt & pos ) -> bool {
        const auto player_label = overmap_label_note::extract_label( ACTIVE_OVERMAP_BUFFER.note( pos ) );
        return player_label.has_value() && !player_label->empty();
    };
    const auto has_map_label = [&]( const tripoint_abs_omt & pos ) -> bool {
        if( const auto player_label = overmap_label_note::extract_label( ACTIVE_OVERMAP_BUFFER.note( pos ) );
            player_label.has_value() && !player_label->empty() )
        {
            return true;
        }

        const auto &terrain = ACTIVE_OVERMAP_BUFFER.ter( pos );
        if( const auto static_label = overmap_labels::get_label( terrain->get_type_id() );
            static_label.has_value() && !static_label->empty() )
        {
            return true;
        }

        return false;
    };

    // Cache display_oter substitution strings for the active region.
    const regional_settings &active_region_settings = ACTIVE_OVERMAP_BUFFER.get_settings(
                center_abs_omt );
    const bool om_has_display_oter = !active_region_settings.display_oter.is_empty();
    const std::string om_default_oter_str = active_region_settings.default_oter.str();
    const std::string om_display_oter_str = om_has_display_oter
                                            ? active_region_settings.display_oter.str()
                                            : std::string{};

    for( int row = min_row; row < max_row; row++ ) {
        for( int col = min_col; col < max_col; col++ ) {
            const tripoint_abs_omt omp = corner_NW + point( col, row );

            const bool see = has_debug_vision || ACTIVE_OVERMAP_BUFFER.seen( omp );
            const bool los = see && you.overmap_los( omp, sight_points );
            // the full string from the ter_id including _north etc.
            TILE_CATEGORY category = TILE_CATEGORY::C_OVERMAP_TERRAIN;
            std::string id;
            int rotation = 0;
            int subtile = -1;

            if( viewing_weather ) {
                const tripoint_abs_omt omp_sky( omp.xy(), OVERMAP_HEIGHT );
                if( uistate.overmap_debug_weather ||
                    you.overmap_los( omp_sky, sight_points * 2 ) ) {
                    id = overmap_ui::get_weather_at_point( omp_sky.xy() ).c_str();
                    category = TILE_CATEGORY::C_OVERMAP_WEATHER;
                }
            }
            if( id.empty() ) {
                if( see ) {
                    id = get_omt_id_rotation_and_subtile( omp, rotation, subtile );
                    if( om_has_display_oter && id == om_default_oter_str ) {
                        id = om_display_oter_str;
                    }
                } else {
                    id = "unknown_terrain";
                }
            }

            if( overmap_transparency && category != TILE_CATEGORY::C_OVERMAP_WEATHER ) {
                int z_offset = 0;
                while( id == "open_air" ) {
                    z_offset++;
                    const tripoint_abs_omt lower_omp = omp + tripoint( 0, 0, -z_offset );
                    const bool lower_see = has_debug_vision || ACTIVE_OVERMAP_BUFFER.seen( lower_omp );
                    if( !lower_see ) {
                        //actually really strange situation when above overmap is explored, but below one isn't
                        //so let's account for this just in case, drawing highest seen tile
                        z_offset--;
                        break;
                    }
                    id = get_omt_id_rotation_and_subtile( lower_omp, rotation, subtile );
                }
                draw_om_tile_recursively( omp + tripoint( 0, 0, -z_offset ), id, rotation, subtile, z_offset );
            } else {
                const lit_level ll = ACTIVE_OVERMAP_BUFFER.is_explored( omp ) ? lit_level::LOW : lit_level::LIT;

                auto [bgCol, fgCol] = get_overmap_color( ACTIVE_OVERMAP_BUFFER, omp );

                // light level is now used for choosing between grayscale filter and normal lit tiles.
                const tile_search_params tile { id, category, "overmap_terrain", subtile, rotation };
                draw_from_id_string( tile, omp.reinterpret_as<tripoint_bub_ms>(), bgCol, fgCol,
                                     ll, false, 0, false,
                                     height_3d );
            }

            if( blink && uistate.overmap_highlighted_omts.contains( omp ) ) {
                if( tile_iso ) {
                    const tile_search_params tile {"highlight", C_NONE, empty_string, 0, 0};
                    draw_from_id_string( tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt,
                                         lit_level::LIT, false, 0, false );
                } else {
                    SDL_Color c = curses_color_to_SDL( c_pink );
                    c.a = c.a >> 1;
                    auto p = player_to_screen( omp.reinterpret_as<tripoint_bub_ms>().xy() );
                    draw_color_at( c, point_bub_ms( p ), SDL_BLENDMODE_BLEND );
                }
            }

            if( see ) {
                if( blink && uistate.overmap_debug_mongroup ) {
                    const std::vector<mongroup *> mgroups = ACTIVE_OVERMAP_BUFFER.monsters_at( omp );
                    if( !mgroups.empty() ) {
                        const auto horde_it = std::ranges::find_if( mgroups, []( const mongroup * mgp ) {
                            return mgp != nullptr && mgp->horde;
                        } );
                        const mongroup *chosen = horde_it != mgroups.end() ? *horde_it : mgroups.front();
                        if( chosen != nullptr ) {
                            const tile_search_params tile { chosen->type->defaultMonster.str(), C_NONE, empty_string, 0, 0 };
                            draw_from_id_string( tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt,
                                                 lit_level::LIT, false, 0, false );
                        }
                    }
                }
                const auto fallback_horde_id = [&]( const tripoint_abs_omt & pos ) -> std::string {
                    const auto groups = ACTIVE_OVERMAP_BUFFER.monsters_at( pos );
                    const auto horde_it = std::ranges::find_if( groups, []( const mongroup * mgp )
                    {
                        return mgp != nullptr && mgp->horde && mgp->type.is_valid();
                    } );
                    if( horde_it == groups.end() )
                    {
                        return "mon_zombie";
                    }

                    const mongroup *mgp = *horde_it;
                    const MonsterGroup &group = mgp->type.obj();
                    const auto default_id = group.defaultMonster.is_valid()
                    ? group.defaultMonster.str()
                    : std::string( "mon_zombie" );
                    if( group.monsters.empty() )
                    {
                        return default_id;
                    }

                    const auto best_entry = std::ranges::max_element( group.monsters, []( const auto & lhs,
                            const auto & rhs )
                    {
                        return lhs.frequency < rhs.frequency;
                    } );
                    if( best_entry == group.monsters.end() )
                    {
                        return default_id;
                    }
                    return best_entry->name.is_valid() ? best_entry->name.str() : default_id;
                };

                const int horde_size = ACTIVE_OVERMAP_BUFFER.get_horde_size( omp );
                if( showhordes && los && horde_size >= HORDE_VISIBILITY_SIZE ) {
                    // Prefer overmap horde sprites; fall back to a zombie monster sprite if missing.
                    const int clamped_size = std::clamp( horde_size, 1, 90 );
                    const std::string horde_id = string_format( "overmap_horde_%d", clamped_size );
                    if( find_tile_with_season( horde_id ) ) {
                        const tile_search_params tile { horde_id, C_NONE, empty_string, 0, 0 };
                        draw_from_id_string(
                            tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt, lit_level::LIT, false, 0,
                            false );
                    } else {
                        auto fallback_id = fallback_horde_id( omp );
                        if( !find_tile_with_season( fallback_id ) ) {
                            const auto groups = ACTIVE_OVERMAP_BUFFER.monsters_at( omp );
                            const auto horde_it = std::ranges::find_if( groups, []( const mongroup * mgp ) {
                                return mgp != nullptr && mgp->horde && mgp->type.is_valid();
                            } );
                            if( horde_it != groups.end() && ( *horde_it ) != nullptr ) {
                                const MonsterGroup &group = ( *horde_it )->type.obj();
                                if( group.defaultMonster.is_valid() ) {
                                    fallback_id = group.defaultMonster.str();
                                }
                            }
                            if( !find_tile_with_season( fallback_id ) ) {
                                fallback_id = "mon_zombie";
                            }
                        }
                        const tile_search_params tile { fallback_id, C_NONE, empty_string, 0, 0 };
                        draw_from_id_string(
                            tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt, lit_level::LIT, false, 0,
                            false );
                    }
                }
            }

            if( uistate.place_terrain || uistate.place_special ) {
                // Highlight areas that already have been generated
                // TODO: fix point types
                if( ACTIVE_MAPBUFFER.lookup_submap( project_to<coords::sm>( omp ) ) ) {
                    const tile_search_params tile {"highlight", C_NONE, empty_string, 0, 0};
                    draw_from_id_string(
                        tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt,
                        lit_level::LIT, false, 0, false );
                }
            }

            if( blink && ACTIVE_OVERMAP_BUFFER.has_vehicle( omp ) ) {
                const std::string tile_id = find_tile_looks_like( "overmap_remembered_vehicle", C_OVERMAP_NOTE )
                                            ? "overmap_remembered_vehicle"
                                            : "note_c_cyan";
                const tile_search_params tile { tile_id, C_OVERMAP_NOTE, "overmap_note", 0, 0 };
                draw_from_id_string(
                    tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt,
                    lit_level::LIT, false, 0, false );
            }

            if( blink && uistate.overmap_show_map_notes && ACTIVE_OVERMAP_BUFFER.has_note( omp ) &&
                !has_map_label( omp ) ) {

                nc_color ter_color = c_black;
                std::string ter_sym = " ";
                // Display notes in all situations, even when not seen
                std::tie( ter_sym, ter_color, std::ignore ) =
                    overmap_ui::get_note_display_info( ACTIVE_OVERMAP_BUFFER.note( omp ) );

                bool drew_note_sprite = false;
                const std::optional<std::string> note_sprite =
                    overmap_ui::get_note_sprite_id( ACTIVE_OVERMAP_BUFFER.note( omp ) );
                if( note_sprite ) {
                    const tile_search_params sprite_tile { *note_sprite, C_NONE, empty_string, 0, 0 };
                    drew_note_sprite = draw_from_id_string(
                                           sprite_tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt,
                                           lit_level::LIT, false, 0, false );
                }
                if( !drew_note_sprite ) {
                    std::string note_name = "note_" + ter_sym + "_" + string_from_color( ter_color );
                    const tile_search_params tile { note_name, C_OVERMAP_NOTE, "overmap_note", 0, 0 };
                    draw_from_id_string(
                        tile, omp.reinterpret_as<tripoint_bub_ms>(), std::nullopt, std::nullopt,
                        lit_level::LIT, false, 0, false );
                }
            }
        }
    }

    if( uistate.place_terrain ) {
        const oter_str_id &terrain_id = uistate.place_terrain->id;
        const oter_t &terrain = *terrain_id;
        std::string id = terrain.get_type_id().str();
        int rotation;
        int subtile;
        terrain.get_rotation_and_subtile( rotation, subtile );
        const tile_search_params tile { id, C_NONE, empty_string, subtile, rotation };
        draw_from_id_string(
            tile, global_omt_to_draw_position( center_abs_omt ), std::nullopt, std::nullopt,
            lit_level::LOW, true, 0, false );
    }
    if( uistate.place_special ) {
        for( const overmap_special_terrain &s_ter : uistate.place_special->preview_terrains() ) {
            if( s_ter.p.z() == 0 ) {
                const point_rel_omt rp( om_direction::rotate( s_ter.p.xy(), uistate.omedit_rotation ) );
                oter_id rotated_id = s_ter.terrain->get_rotated( uistate.omedit_rotation );
                const oter_t &terrain = *rotated_id;
                std::string id = terrain.get_type_id().str();
                int rotation;
                int subtile;
                terrain.get_rotation_and_subtile( rotation, subtile );

                const tile_search_params tile { id, C_OVERMAP_TERRAIN, "overmap_terrain", 0, rotation };
                draw_from_id_string(
                    tile, global_omt_to_draw_position( center_abs_omt + rp ), std::nullopt, std::nullopt,
                    lit_level::LOW, true, 0, false );
            }
        }
    }

    auto npcs_near_player = ACTIVE_OVERMAP_BUFFER.get_npcs_near_player( sight_points );

    // draw nearby seen npcs
    for( const shared_ptr_fast<npc> &guy : npcs_near_player ) {
        const tripoint_abs_omt &guy_loc = guy->abs_omt_pos();
        if( guy_loc.z() == center_abs_omt.z() && ( has_debug_vision ||
                ACTIVE_OVERMAP_BUFFER.seen( guy_loc ) ) ) {
            draw_entity_with_overlays( *guy, global_omt_to_draw_position( guy_loc ), lit_level::LIT,
                                       height_3d );
        }
    }

    if( you.abs_omt_pos().z() == center_abs_omt.z() ) {
        draw_entity_with_overlays( you, global_omt_to_draw_position( avatar_pos ),
                                   lit_level::LIT, height_3d );
    }

    {
        const tile_search_params tile { "cursor", C_NONE, empty_string, 0, 0 };
        draw_from_id_string(
            tile, global_omt_to_draw_position( center_abs_omt ), std::nullopt, std::nullopt,
            lit_level::LIT, false, 0, false );
    }

    if( blink ) {
        // Draw path for auto-travel
        for( const tripoint_abs_omt &pos : you.omt_path ) {
            const char *id;
            if( pos.z() == center_abs_omt.z() ) {
                id = "overmap_path";
            } else if( pos.z() > center_abs_omt.z() ) {
                id = "overmap_path_above";
            } else {
                id = "overmap_path_below";
            }
            const tile_search_params tile { id, C_NONE, empty_string, 0, 0 };
            draw_from_id_string(
                tile, global_omt_to_draw_position( pos ), std::nullopt, std::nullopt,
                lit_level::LIT, false, 0, false );
        }

        // reduce the area where the map cursor is drawn so it doesn't get cut off
        inclusive_cuboid<tripoint_abs_omt> map_cursor_area = overmap_area;
        map_cursor_area.p_max.y()--;
        const std::optional<std::pair<tripoint_abs_omt, std::string>> mission_arrow =
                    get_mission_arrow( map_cursor_area, center_abs_omt );
        if( mission_arrow ) {
            const tile_search_params tile { mission_arrow->second, C_NONE, empty_string, 0, 0 };
            draw_from_id_string(
                tile, global_omt_to_draw_position( mission_arrow->first ), std::nullopt, std::nullopt,
                lit_level::LIT, false, 0, false );
        }
    }

    if( !viewing_weather && uistate.overmap_show_city_labels ) {
        const auto abs_sm_to_draw_label = [&]( const tripoint_abs_sm & city_pos, const int label_length ) {
            const auto tile_draw_pos = global_omt_to_draw_position( project_to<coords::omt>
                                       ( city_pos ) ) - o;
            point draw_point( tile_draw_pos.x() * tile_width + dest.x,
                              tile_draw_pos.y() * tile_height + dest.y );
            // center text on the tile
            draw_point += point( ( tile_width - label_length * fontwidth ) / 2,
                                 ( tile_height - fontheight ) / 2 );
            return draw_point;
        };

        // draws a black rectangle behind a label for visibility and legibility
        const auto label_bg = [&]( const tripoint_abs_sm & pos, const std::string & name ) {
            const int name_length = utf8_width( name );
            const point draw_pos = abs_sm_to_draw_label( pos, name_length );
            const SDL_Rect clipRect = { draw_pos.x, draw_pos.y, name_length * fontwidth, fontheight };

            geometry->rect( renderer, point{ clipRect.x, clipRect.y }, clipRect.w, clipRect.h, SDL_Color() );

            draw_string( *font, renderer, geometry, name, draw_pos, 11 );
        };

        const auto abs_omt_to_draw_label = [&]( const tripoint_abs_omt & omt_pos, const int label_length ) {
            const auto tile_draw_pos = global_omt_to_draw_position( omt_pos ) - o;
            auto draw_point = point( tile_draw_pos.x() * tile_width + dest.x,
                                     tile_draw_pos.y() * tile_height + dest.y );
            draw_point += point( ( tile_width - label_length * fontwidth ) / 2,
                                 ( tile_height - fontheight ) / 2 );
            return draw_point;
        };

        const auto label_bg_omt = [&]( const tripoint_abs_omt & pos, const std::string & name ) {
            const auto name_length = utf8_width( name );
            const auto draw_pos = abs_omt_to_draw_label( pos, name_length );
            const auto clip_rect = SDL_Rect{
                .x = draw_pos.x,
                .y = draw_pos.y,
                .w = name_length * fontwidth,
                .h = fontheight
            };

            geometry->rect( renderer, point{ clip_rect.x, clip_rect.y }, clip_rect.w, clip_rect.h,
                            SDL_Color() );

            draw_string( *font, renderer, geometry, name, draw_pos, 11 );
        };

        // the tiles on the overmap are overmap tiles, so we need to use
        // coordinate conversions to make sure we're in the right place.
        const int radius = coords::project_to<coords::sm>( tripoint_abs_omt( std::min( max_col, max_row ),
                           0, 0 ) ).x() / 2;

        for( const city_reference &city : ACTIVE_OVERMAP_BUFFER.get_cities_near(
                 coords::project_to<coords::sm>( center_abs_omt ), radius ) ) {
            const tripoint_abs_omt city_center = coords::project_to<coords::omt>( city.abs_sm_pos );
            if( ACTIVE_OVERMAP_BUFFER.seen( city_center ) && overmap_area.contains( city_center ) &&
                !has_player_label( city_center ) ) {
                label_bg( city.abs_sm_pos, city.city->name );
            }
        }

        for( int row = min_row; row < max_row; row++ ) {
            for( int col = min_col; col < max_col; col++ ) {
                const tripoint_abs_omt omt_pos = corner_NW + point( col, row );
                if( !ACTIVE_OVERMAP_BUFFER.seen( omt_pos ) ) {
                    continue;
                }
                auto label_text = std::optional<std::string> {};
                if( const auto player_label =
                        overmap_label_note::extract_label( ACTIVE_OVERMAP_BUFFER.note( omt_pos ) );
                    player_label.has_value() ) {
                    label_text = *player_label;
                } else {
                    const auto &terrain = ACTIVE_OVERMAP_BUFFER.ter( omt_pos );
                    if( const auto static_label = overmap_labels::get_label( terrain->get_type_id() );
                        static_label.has_value() ) {
                        label_text = _( *static_label );
                    }
                }
                if( !label_text.has_value() || label_text->empty() ) {
                    continue;
                }
                if( overmap_area.contains( omt_pos ) ) {
                    label_bg_omt( omt_pos, *label_text );
                }
            }
        }
    }

    std::vector<std::pair<nc_color, std::string>> notes_window_text;

    if( uistate.overmap_show_map_notes ) {
        const std::string &note_text = ACTIVE_OVERMAP_BUFFER.note( center_abs_omt );
        if( !note_text.empty() && !overmap_label_note::is_label_only( note_text ) ) {
            const std::tuple<char, nc_color, size_t> note_info = overmap_ui::get_note_display_info(
                        note_text );
            const size_t pos = std::get<2>( note_info );
            if( pos != std::string::npos ) {
                const auto display_note_text =
                    note_label_utils::strip_label_commands( note_text.substr( pos ) );
                if( !display_note_text.empty() ) {
                    notes_window_text.emplace_back( std::get<1>( note_info ), display_note_text );
                }
            }
            if( ACTIVE_OVERMAP_BUFFER.is_marked_dangerous( center_abs_omt ) ) {
                notes_window_text.emplace_back( c_red, _( "DANGEROUS AREA!" ) );
            }
        }
    }

    if( has_debug_vision || ACTIVE_OVERMAP_BUFFER.seen( center_abs_omt ) ) {
        for( const auto &npc : npcs_near_player ) {
            if( !npc->marked_for_death && npc->abs_omt_pos() == center_abs_omt ) {
                notes_window_text.emplace_back( npc->basic_symbol_color(), npc->name );
            }
        }
    }

    for( auto &v : ACTIVE_OVERMAP_BUFFER.get_vehicle( center_abs_omt ) ) {
        notes_window_text.emplace_back( c_white, v.name );
    }

    if( !notes_window_text.empty() ) {
        constexpr int padding = 2;

        const auto draw_note_text = [&]( point  draw_pos, const std::string & name,
        nc_color & color ) {
            char note_fg_color = color == c_yellow ? 11 :
                                 cata_cursesport::colorpairs[color.to_color_pair_index()].FG;
            return draw_string( *font, renderer, geometry, name, draw_pos, note_fg_color );
        };

        // Find screen coordinates to the right of the center tile
        auto center_sm = coords::project_to<coords::sm>( tripoint_abs_omt( center_abs_omt.x() + 1,
                         center_abs_omt.y(), center_abs_omt.z() ) );
        const auto tile_draw_pos = global_omt_to_draw_position( project_to<coords::omt>
                                   ( center_sm ) ) - o;
        point draw_point( tile_draw_pos.x() * tile_width + dest.x,
                          tile_draw_pos.y() * tile_height + dest.y );
        draw_point += point( padding, padding );

        // Draw notes header. Very simple label at the moment
        nc_color header_color = c_white;
        const std::string header_string = _( "-- Notes: --" );
        SDL_Rect header_background_rect = {
            draw_point.x - padding,
            draw_point.y - padding,
            fontwidth * utf8_width( header_string ) + padding * 2,
            fontheight + padding * 2
        };
        geometry->rect( renderer, point{ header_background_rect.x, header_background_rect.y },
                        header_background_rect.w, header_background_rect.h, SDL_Color{ 0, 0, 0, 175 } );
        draw_note_text( draw_point, header_string, header_color );
        draw_point.y += fontheight + padding * 2;

        const int starting_x = draw_point.x;

        for( auto &line : notes_window_text ) {
            const auto color_segments = split_by_color( line.second );
            std::stack<nc_color> color_stack;
            nc_color default_color = std::get<0>( line );
            color_stack.push( default_color );
            std::vector<std::tuple<nc_color, std::string>> colored_lines;

            draw_point.x = starting_x;

            int line_length = 0;
            for( auto seg : color_segments ) {
                if( seg.empty() ) {
                    continue;
                }

                if( seg[0] == '<' ) {
                    const color_tag_parse_result::tag_type type = update_color_stack(
                                color_stack, seg, report_color_error::no );
                    if( type != color_tag_parse_result::non_color_tag ) {
                        seg = rm_prefix( seg );
                    }
                }

                nc_color &color = color_stack.empty() ? default_color : color_stack.top();
                colored_lines.emplace_back( color, seg );
                line_length += utf8_width( seg );
            }

            // Draw background first for the whole line
            SDL_Rect background_rect = {
                draw_point.x - padding,
                draw_point.y - padding,
                fontwidth *line_length + padding * 2,
                fontheight + padding * 2
            };
            geometry->rect( renderer, point{ background_rect.x, background_rect.y },
                            background_rect.w, background_rect.h, SDL_Color{ 0, 0, 0, 175 } );

            // Draw colored text segments
            for( auto &colored_line : colored_lines ) {
                std::string &text = std::get<1>( colored_line );
                draw_point.x = draw_note_text( draw_point, text, std::get<0>( colored_line ) ).x;
            }

            draw_point.y += fontheight + padding;
        }
    }

    lighting::get_render_state().clear_tile_scissor();
}

static bool draw_window( Font_Ptr &font, const catacurses::window &w, point offset )
{
    if( scaling_factor > 1 ) {
        SDL_SetRenderLogicalPresentation( renderer.get(), WindowWidth / scaling_factor,
                                          WindowHeight / scaling_factor, SDL_LOGICAL_PRESENTATION_STRETCH );
    }

    cata_cursesport::WINDOW *const win = w.get<cata_cursesport::WINDOW>();
    //Keeping track of the last drawn window
    const cata_cursesport::WINDOW *winBuffer = static_cast<cata_cursesport::WINDOW *>
            ( ::winBuffer.lock().get() );
    if( !fontScaleBuffer ) {
        fontScaleBuffer = tilecontext->get_tile_width();
    }
    const int fontScale = tilecontext->get_tile_width();
    //This creates a problem when map_font is different from the regular font
    //Specifically when showing the overmap
    //And in some instances of screen change, i.e. inventory.
    bool oldWinCompatible = false;

    // clear the oversized buffer proportionally
    invalidate_framebuffer_proportion( win );

    // use the oversize buffer when dealing with windows that can have a different font than the main text font
    bool use_oversized_framebuffer = g && ( w == g->w_terrain || w == g->w_overmap );

    std::vector<curseline> &framebuffer = use_oversized_framebuffer ? oversized_framebuffer :
                                          terminal_framebuffer;

    // When this window is being drawn inside an ui_adaptor redraw_cb, its draws
    // route into the adaptor's retained GPU slice, which was just cleared. The
    // per-cell framebuffer dirty-cell skip below assumes a persistent backbuffer
    // (the removed display_buffer) keeps unchanged cells on screen — but the
    // slice has no such persistence, so unchanged cells would be dropped (e.g. a
    // navigated uilist collapsing to current+previous row). Force a FULL re-push
    // of this window's cells into the slice by bypassing the skip while routing.
    const bool slice_active = lighting::get_render_state().ready() &&
                              lighting::get_render_state().slice_routing_active();

    /*
    Let's try to keep track of different windows.
    A number of windows are coexisting on the screen, so don't have to interfere.

    g->w_terrain, g->w_minimap, g->w_HP, g->w_status, g->w_status2, g->w_messages,
     g->w_location, and g->w_minimap, can be buffered if either of them was
     the previous window.

    g->w_overmap and g->w_omlegend are likewise.

    Everything else works on strict equality because there aren't yet IDs for some of them.
    */
    if( g && ( w == g->w_terrain || w == g->w_minimap ) ) {
        if( winBuffer == g->w_terrain || winBuffer == g->w_minimap ) {
            oldWinCompatible = true;
        }
    } else if( g && ( w == g->w_overmap || w == g->w_omlegend ) ) {
        if( winBuffer == g->w_overmap || winBuffer == g->w_omlegend ) {
            oldWinCompatible = true;
        }
    } else {
        if( win == winBuffer ) {
            oldWinCompatible = true;
        }
    }

    // TODO: Get this from UTF system to make sure it is exactly the kind of space we need
    static const std::string space_string = " ";

    bool update = false;
    for( int j = 0; j < win->height; j++ ) {
        if( !win->line[j].touched ) {
            continue;
        }

        const int fby = win->pos.y + j;
        if( fby >= static_cast<int>( framebuffer.size() ) ) {
            // prevent indexing outside the frame buffer. This might happen for some parts of the window. FIX #28953.
            break;
        }

        update = true;
        win->line[j].touched = false;
        for( int i = 0; i < win->width; i++ ) {
            const int fbx = win->pos.x + i;
            if( fbx >= static_cast<int>( framebuffer[fby].chars.size() ) ) {
                // prevent indexing outside the frame buffer. This might happen for some parts of the window.
                break;
            }

            const cursecell &cell = win->line[j].chars[i];

            const int drawx = offset.x + i * font->width;
            const int drawy = offset.y + j * font->height;
            if( drawx + font->width > WindowWidth || drawy + font->height > WindowHeight ) {
                // Outside of the display area, would not render anyway
                continue;
            }

            // Avoid redrawing an unchanged tile by checking the framebuffer cache
            // TODO: handle caching when drawing normal windows over graphical tiles
            cursecell &oldcell = framebuffer[fby].chars[fbx];

            if( !slice_active && oldWinCompatible && cell == oldcell &&
                fontScale == fontScaleBuffer ) {
                continue;
            }
            oldcell = cell;

            if( cell.ch.empty() ) {
                continue; // second cell of a multi-cell character
            }

            // Spaces are used a lot, so this does help noticeably
            if( cell.ch == space_string ) {
                const SDL_Color bg_col = color_as_sdl( cell.BG );
                if( !suppress_cell_bg( win, bg_col ) ) {
                    geometry->rect( renderer, point( drawx, drawy ), font->width, font->height,
                                    bg_col );
                }
                continue;
            }
            const int codepoint = UTF8_getch( cell.ch );
            const catacurses::base_color FG = cell.FG;
            const catacurses::base_color BG = cell.BG;
            int cw = ( codepoint == UNKNOWN_UNICODE ) ? 1 : utf8_width( cell.ch );
            if( cw < 1 ) {
                // utf8_width() may return a negative width
                continue;
            }
            bool use_draw_ascii_lines_routine = get_option<bool>( "USE_DRAW_ASCII_LINES_ROUTINE" );
            unsigned char uc = static_cast<unsigned char>( cell.ch[0] );
            switch( codepoint ) {
                case LINE_XOXO_UNICODE:
                    uc = LINE_XOXO_C;
                    break;
                case LINE_OXOX_UNICODE:
                    uc = LINE_OXOX_C;
                    break;
                case LINE_XXOO_UNICODE:
                    uc = LINE_XXOO_C;
                    break;
                case LINE_OXXO_UNICODE:
                    uc = LINE_OXXO_C;
                    break;
                case LINE_OOXX_UNICODE:
                    uc = LINE_OOXX_C;
                    break;
                case LINE_XOOX_UNICODE:
                    uc = LINE_XOOX_C;
                    break;
                case LINE_XXXO_UNICODE:
                    uc = LINE_XXXO_C;
                    break;
                case LINE_XXOX_UNICODE:
                    uc = LINE_XXOX_C;
                    break;
                case LINE_XOXX_UNICODE:
                    uc = LINE_XOXX_C;
                    break;
                case LINE_OXXX_UNICODE:
                    uc = LINE_OXXX_C;
                    break;
                case LINE_XXXX_UNICODE:
                    uc = LINE_XXXX_C;
                    break;
                case LINE_XDXO_UNICODE:
                    uc = LINE_XDXO_C;
                    break;
                case LINE_DXOX_UNICODE:
                    uc = LINE_DXOX_C;
                    break;
                case LINE_XOXD_UNICODE:
                    uc = LINE_XOXD_C;
                    break;
                case LINE_OXDX_UNICODE:
                    uc = LINE_OXDX_C;
                    break;
                case UNKNOWN_UNICODE:
                    use_draw_ascii_lines_routine = true;
                    break;
                default:
                    use_draw_ascii_lines_routine = false;
                    break;
            }
            {
                const SDL_Color bg_col = color_as_sdl( BG );
                if( !suppress_cell_bg( win, bg_col ) ) {
                    geometry->rect( renderer, point( drawx, drawy ),
                                    font->width * cw, font->height, bg_col );
                }
            }
            if( use_draw_ascii_lines_routine ) {
                font->draw_ascii_lines( renderer, geometry, uc, point( drawx, drawy ), FG );
            } else {
                font->OutputChar( renderer, geometry, cell.ch, point( drawx, drawy ), FG );
            }
        }
    }
    win->draw = false; //We drew the window, mark it as so
    //Keeping track of last drawn window and tilemode zoom level
    ::winBuffer = w.weak_ptr();
    fontScaleBuffer = tilecontext->get_tile_width();

    return update;
}

static bool draw_window( Font_Ptr &font, const catacurses::window &w )
{
    cata_cursesport::WINDOW *const win = w.get<cata_cursesport::WINDOW>();
    // Use global font sizes here to make this independent of the
    // font used for this window.
    return draw_window( font, w, point( win->pos.x * ::fontwidth, win->pos.y * ::fontheight ) );
}

void cata_cursesport::curses_drawwindow( const catacurses::window &w )
{
    if( scaling_factor > 1 ) {
        SDL_SetRenderLogicalPresentation( renderer.get(), WindowWidth / scaling_factor,
                                          WindowHeight / scaling_factor, SDL_LOGICAL_PRESENTATION_STRETCH );
    }
    WINDOW *const win = w.get<WINDOW>();
    bool update = false;
    if( g && w == g->w_terrain && use_tiles ) {
        // color blocks overlay; drawn on top of tiles and on top of overlay strings (if any).
        color_block_overlay_container color_blocks;

        // Strings with colors do be drawn with map_font on top of tiles.
        std::multimap<point, formatted_text> overlay_strings;

        // game::w_terrain can be drawn by the tilecontext.
        // skip the normal drawing code for it.
        tilecontext->draw(
            point( win->pos.x * fontwidth, win->pos.y * fontheight ),
            g->ter_view_p,
            TERRAIN_WINDOW_TERM_WIDTH * font->width,
            TERRAIN_WINDOW_TERM_HEIGHT * font->height,
            overlay_strings,
            color_blocks );

        // color blocks overlay
        if( !color_blocks.second.empty() ) {
            SDL_BlendMode blend_mode;
            GetRenderDrawBlendMode( renderer, blend_mode ); // save the current blend mode
            SetRenderDrawBlendMode( renderer, color_blocks.first ); // set the new blend mode
            for( const auto &e : color_blocks.second ) {
                geometry->rect( renderer, e.first, tilecontext->get_tile_width(),
                                tilecontext->get_tile_height(), e.second );
            }
            SetRenderDrawBlendMode( renderer, blend_mode ); // set the old blend mode
        }

        // overlay strings
        point prev_coord;
        int x_offset = 0;
        int alignment_offset = 0;
        for( const auto &iter : overlay_strings ) {
            const point coord = iter.first;
            const formatted_text ft = iter.second;
            const utf8_wrapper text( ft.text );

            // Strings at equal coords are displayed sequentially.
            if( coord != prev_coord ) {
                x_offset = 0;
            }

            // Calculate length of all strings in sequence to align them.
            if( x_offset == 0 ) {
                int full_text_length = 0;
                const auto range = overlay_strings.equal_range( coord );
                for( auto ri = range.first; ri != range.second; ++ri ) {
                    utf8_wrapper rt( ri->second.text );
                    full_text_length += rt.display_width();
                }

                alignment_offset = 0;
                if( ft.alignment == text_alignment::center ) {
                    alignment_offset = full_text_length / 2;
                } else if( ft.alignment == text_alignment::right ) {
                    alignment_offset = full_text_length - 1;
                }
            }

            int width = 0;
            for( size_t i = 0; i < text.size(); ++i ) {
                const int x0 = win->pos.x * fontwidth;
                const int y0 = win->pos.y * fontheight;
                const int x = x0 + ( x_offset - alignment_offset + width ) * map_font->width + coord.x;
                const int y = y0 + coord.y;

                // Clip to window bounds.
                if( x < x0 || x > x0 + ( TERRAIN_WINDOW_TERM_WIDTH - 1 ) * font->width
                    || y < y0 || y > y0 + ( TERRAIN_WINDOW_TERM_HEIGHT - 1 ) * font->height ) {
                    continue;
                }

                const uint32_t ch = text.at( i );
                const auto glyph = utf32_to_utf8( ch );
                const bool outlined_white = ft.color == catacurses::white ||
                                            ft.color == catacurses::white + 8;

                if( outlined_white ) {
                    static constexpr std::array<point, 4> outline_offsets = {
                        point_east,
                        point_north,
                        point_west,
                        point_south,
                    };
                    for( const point &offset : outline_offsets ) {
                        map_font->OutputChar( renderer, geometry, glyph,
                                              point( x + offset.x, y + offset.y ),
                                              catacurses::black );
                    }
                }

                map_font->OutputChar( renderer, geometry, glyph, point( x, y ), ft.color );
                width += mk_wcwidth( ch );
            }

            prev_coord = coord;
            x_offset = width;
        }

        invalidate_framebuffer( terminal_framebuffer, win->pos,
                                TERRAIN_WINDOW_TERM_WIDTH, TERRAIN_WINDOW_TERM_HEIGHT );

        update = true;
    } else if( g && w == g->w_terrain && map_font ) {
        // When the terrain updates, predraw a black space around its edge
        // to keep various former interface elements from showing through the gaps

        //calculate width differences between map_font and font
        int partial_width = std::max( TERRAIN_WINDOW_TERM_WIDTH * fontwidth - TERRAIN_WINDOW_WIDTH *
                                      map_font->width, 0 );
        int partial_height = std::max( TERRAIN_WINDOW_TERM_HEIGHT * fontheight - TERRAIN_WINDOW_HEIGHT *
                                       map_font->height, 0 );
        //Gap between terrain and lower window edge
        if( partial_height > 0 ) {
            geometry->rect( renderer, point( win->pos.x * map_font->width,
                                             ( win->pos.y + TERRAIN_WINDOW_HEIGHT ) * map_font->height ),
                            TERRAIN_WINDOW_WIDTH * map_font->width + partial_width, partial_height,
                            color_as_sdl( catacurses::black ) );
        }
        //Gap between terrain and sidebar
        if( partial_width > 0 ) {
            geometry->rect( renderer, point( ( win->pos.x + TERRAIN_WINDOW_WIDTH ) * map_font->width,
                                             win->pos.y * map_font->height ),
                            partial_width,
                            TERRAIN_WINDOW_HEIGHT * map_font->height + partial_height,
                            color_as_sdl( catacurses::black ) );
        }
        // Special font for the terrain window
        update = draw_window( map_font, w );
    } else if( g && w == g->w_overmap && use_tiles && use_tiles_overmap ) {
        overmap_tilecontext->draw_om( win->pos, overmap_ui::redraw_info.center,
                                      overmap_ui::redraw_info.blink );
        update = true;
    } else if( g && w == g->w_overmap && overmap_font ) {
        // Special font for the terrain window
        update = draw_window( overmap_font, w );
    } else if( g && w == g->w_pixel_minimap && pixel_minimap_option ) {
        // ensure the space the minimap covers is "dirtied".
        // this is necessary when it's the only part of the sidebar being drawn
        // TODO: Figure out how to properly make the minimap code do whatever it is this does
        draw_window( font, w );

        // Make sure the entire minimap window is black before drawing.
        clear_window_area( w );
        tilecontext->draw_minimap(
            point( win->pos.x * fontwidth, win->pos.y * fontheight ),
            tripoint_bub_ms( g->u.bub_pos().xy(), g->ter_view_p.z() ),
            win->width * font->width, win->height * font->height );
        update = true;

    } else {
        // Either not using tiles (tilecontext) or not the w_terrain window.
        update = draw_window( font, w );
    }
    if( update ) {
        needupdate = true;
    }
}

static int alt_buffer = 0;
static bool alt_down = false;

static void begin_alt_code()
{
    alt_buffer = 0;
    alt_down = true;
}

static bool add_alt_code( char c )
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

static int end_alt_code()
{
    alt_down = false;
    return alt_buffer;
}

static int HandleDPad()
{
    // Check if we have a gamepad d-pad event.
    if( SDL_GetJoystickHat( joystick, 0 ) != SDL_HAT_CENTERED ) {
        // When someone tries to press a diagonal, they likely will
        // press a single direction first. Wait a few milliseconds to
        // give them time to press both of the buttons for the diagonal.
        int button = SDL_GetJoystickHat( joystick, 0 );
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

        if( delaydpad == std::numeric_limits<Uint64>::max() ) {
            delaydpad = SDL_GetTicks() + dpad_delay;
            queued_dpad = lc;
        }

        // Okay it seems we're ready to process.
        if( SDL_GetTicks() > delaydpad ) {

            if( lc != ERR ) {
                if( dpad_continuous && ( lc & lastdpad ) == 0 ) {
                    // Continuous movement should only work in the same or similar directions.
                    dpad_continuous = false;
                    lastdpad = lc;
                    return 0;
                }

                last_input = input_event( lc, input_event_t::gamepad );
                lastdpad = lc;
                queued_dpad = ERR;

                if( !dpad_continuous ) {
                    delaydpad = SDL_GetTicks() + 200;
                    dpad_continuous = true;
                } else {
                    delaydpad = SDL_GetTicks() + 60;
                }
                return 1;
            }
        }
    } else {
        dpad_continuous = false;
        delaydpad = std::numeric_limits<Uint64>::max();

        // If we didn't hold it down for a while, just
        // fire the last registered press.
        if( queued_dpad != ERR ) {
            last_input = input_event( queued_dpad, input_event_t::gamepad );
            queued_dpad = ERR;
            return 1;
        }
    }

    return 0;
}

static SDL_Keycode sdl_keycode_opposite_arrow( SDL_Keycode key )
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

static bool sdl_keycode_is_arrow( SDL_Keycode key )
{
    return static_cast<bool>( sdl_keycode_opposite_arrow( key ) );
}

static int arrow_combo_to_numpad( SDL_Keycode mod, SDL_Keycode key )
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

static int arrow_combo_modifier = 0;

static int handle_arrow_combo( SDL_Keycode key )
{
    if( !arrow_combo_modifier ) {
        arrow_combo_modifier = key;
        return 0;
    }
    return arrow_combo_to_numpad( arrow_combo_modifier, key );
}

static void end_arrow_combo()
{
    arrow_combo_modifier = 0;
}

/**
 * Translate SDL key codes to key identifiers used by ncurses, this
 * allows the input_manager to only consider those.
 * @return 0 if the input can not be translated (unknown key?),
 * -1 when a ALT+number sequence has been started,
 * or something that a call to ncurses getch would return.
 */
static int sdl_keysym_to_curses( const SDL_Keycode sym, const SDL_Keymod mod )
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

bool handle_resize( int w, int h )
{
    if( ( w != WindowWidth ) || ( h != WindowHeight ) ) {
        WindowWidth = w;
        WindowHeight = h;
        TERMINAL_WIDTH = WindowWidth / fontwidth / scaling_factor;
        TERMINAL_HEIGHT = WindowHeight / fontheight / scaling_factor;
        need_invalidate_framebuffers = true;
        catacurses::stdscr = catacurses::newwin( TERMINAL_HEIGHT, TERMINAL_WIDTH, point_zero );
        game_ui::init_ui();
        ui_manager::screen_resized();
        // Keep the UI compositor texture sized to the physical swapchain so
        // the composite blit stays 1:1 after a window resize.
        {
            auto &rs = lighting::get_render_state();
            if( rs.ready() ) {
                int pw = 0;
                int ph = 0;
                SDL_GetWindowSizeInPixels( window.get(), &pw, &ph );
                if( rs.ui_target() ) {
                    rs.ui_target()->resize( pw, ph );
                }
                if( rs.world_target() ) {
                    rs.world_target()->resize( pw, ph );
                }
                if( rs.world_ldr_target() ) {
                    rs.world_ldr_target()->resize( pw, ph );
                }
                // Font cell size may have changed; drop stale-size icon rasters
                // so the next request re-rasterizes crisp at the new size.
                widget_icon::clear();
            }
        }
        return true;
    }
    return false;
}

void resize_term( const int cell_w, const int cell_h )
{
    int w = cell_w * fontwidth * scaling_factor;
    int h = cell_h * fontheight * scaling_factor;
    SDL_SetWindowSize( window.get(), w, h );
    SDL_GetWindowSize( window.get(), &w, &h );
    handle_resize( w, h );
}

void toggle_fullscreen_window()
{
    static int restore_win_w = get_option<int>( "TERMINAL_X" ) * fontwidth * scaling_factor;
    static int restore_win_h = get_option<int>( "TERMINAL_Y" ) * fontheight * scaling_factor;

    if( fullscreen ) {
        if( printErrorIf( !SDL_SetWindowFullscreen( window.get(), false ),
                          "SDL_SetWindowFullscreen failed" ) ) {
            return;
        }
        SDL_RestoreWindow( window.get() );
        SDL_SetWindowSize( window.get(), restore_win_w, restore_win_h );
        SDL_SetWindowMinimumSize( window.get(), fontwidth * FULL_SCREEN_WIDTH * scaling_factor,
                                  fontheight * FULL_SCREEN_HEIGHT * scaling_factor );
    } else {
        restore_win_w = WindowWidth;
        restore_win_h = WindowHeight;
        SDL_SetWindowFullscreenMode( window.get(), nullptr );
        if( printErrorIf( !SDL_SetWindowFullscreen( window.get(), true ),
                          "SDL_SetWindowFullscreen failed" ) ) {
            return;
        }
    }
    int nw = 0;
    int nh = 0;
    SDL_GetWindowSize( window.get(), &nw, &nh );
    handle_resize( nw, nh );
    fullscreen = !fullscreen;
}

//Check for any window messages (keypress, paint, mousemove, etc)
static void CheckMessages()
{
    SDL_Event ev;
    bool quit = false;
    bool text_refresh = false;
    bool is_repeat = false;
    if( HandleDPad() ) {
        return;
    }

    last_input = input_event();

    std::optional<point> resize_dims;
    bool render_target_reset = false;

    while( SDL_PollEvent( &ev ) ) {
        // Dear ImGui dev UI sees every event first. While a tool is open keep
        // producing frames (this event-driven loop has no vsync tick) so
        // hover/drag stay responsive.
        const bool imgui_capture = imgui_layer::process_event( ev );
        // Open/close toggle (F4) — handled BEFORE the capture gate so the panel
        // can always be closed even while ImGui holds keyboard focus. P0 dev
        // key; revisit for collisions when promoting past P0.
        if( ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_F4 ) {
            imgui_layer::visible() = !imgui_layer::visible();
            needupdate = true;
            continue;
        }
        // ImGui consumed this mouse/keyboard event — keep it out of game input.
        if( imgui_capture ) {
            continue;
        }
        switch( ev.type ) {
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                break;
            case SDL_EVENT_WINDOW_EXPOSED:
                needupdate = true;
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
                const int lc = sdl_keysym_to_curses( ev.key.key, ev.key.mod );
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
                    // F7: cycle debug visualization mode (0-8). Mode 8 is the
                    // B/W emitter-only diagnostic — bypasses tint gating so it
                    // works on the main-menu blue backdrop.
                    g_current_dbg_mode = ( g_current_dbg_mode + 1 ) % 9u;
                    g_dbg_params.debug_mode = g_current_dbg_mode;
                    break;
                } else if( lc == KEY_F( 8 ) ) {
                    // F8: decrease emitter/sun/sky scales.
                    // Shift+F8: less dither.  Ctrl+F8: fewer dither bands.
                    if( ev.key.mod & SDL_KMOD_ALT ) {
                        g_dbg_params.gi_strength =
                            std::max( 0.0f, g_dbg_params.gi_strength - 0.05f );
                    } else if( ev.key.mod & SDL_KMOD_CTRL ) {
                        g_dbg_params.dither_bands =
                            std::max( 1.0f, g_dbg_params.dither_bands - 1.0f );
                    } else if( ev.key.mod & SDL_KMOD_SHIFT ) {
                        g_dbg_params.dither_amt =
                            std::max( 0.0f, g_dbg_params.dither_amt - 0.1f );
                    } else {
                        g_emitter_scale = std::max( 0.0f, g_emitter_scale - 0.1f );
                        g_sun_scale = std::max( 0.0f, g_sun_scale - 0.1f );
                        g_sky_scale = std::max( 0.0f, g_sky_scale - 0.1f );
                        g_dbg_params.emitter_scale = g_emitter_scale;
                        g_dbg_params.sun_scale = g_sun_scale;
                        g_dbg_params.sky_scale = g_sky_scale;
                    }
                    break;
                } else if( lc == KEY_F( 9 ) ) {
                    // F9: increase emitter/sun/sky scales.
                    // Shift+F9: more dither.  Ctrl+F9: more dither bands.
                    if( ev.key.mod & SDL_KMOD_ALT ) {
                        g_dbg_params.gi_strength =
                            std::min( 2.0f, g_dbg_params.gi_strength + 0.05f );
                    } else if( ev.key.mod & SDL_KMOD_CTRL ) {
                        g_dbg_params.dither_bands =
                            std::min( 16.0f, g_dbg_params.dither_bands + 1.0f );
                    } else if( ev.key.mod & SDL_KMOD_SHIFT ) {
                        g_dbg_params.dither_amt =
                            std::min( 1.0f, g_dbg_params.dither_amt + 0.1f );
                    } else {
                        g_emitter_scale = std::min( 10.0f, g_emitter_scale + 0.1f );
                        g_sun_scale = std::min( 10.0f, g_sun_scale + 0.1f );
                        g_sky_scale = std::min( 10.0f, g_sky_scale + 0.1f );
                        g_dbg_params.emitter_scale = g_emitter_scale;
                        g_dbg_params.sun_scale = g_sun_scale;
                        g_dbg_params.sky_scale = g_sky_scale;
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
                        case 0: menu_emitter_tuning::pos_x = 8.5f;
                                menu_emitter_tuning::pos_y = 4.5f;  break;
                        case 1: menu_emitter_tuning::pos_x = 40.0f;
                                menu_emitter_tuning::pos_y = 22.0f; break;
                        case 2: menu_emitter_tuning::pos_x = 70.0f;
                                menu_emitter_tuning::pos_y = 38.0f; break;
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
                        last_input = input_event( ev.key.key - SDLK_KP_1 + NUMPAD_1, input_event_t::keyboard );
                    } else {
                        // a key we don't know in curses and won't handle.
                        break;
                    }
                } else if( add_alt_code( lc ) ) {
                    // key was handled
                } else {
                    last_input = input_event( lc, input_event_t::keyboard );
                }
            }
            break;
            case SDL_EVENT_KEY_UP: {
                is_repeat = ev.key.repeat;
                if( ev.key.key == SDLK_LALT || ev.key.key == SDLK_RALT ) {
                    int code = end_alt_code();
                    if( code ) {
                        last_input = input_event( code, input_event_t::keyboard );
                        last_input.text = utf32_to_utf8( code );
                    }
                }
            }
            break;
            case SDL_EVENT_TEXT_INPUT:
                if( !add_alt_code( *ev.text.text ) ) {
                    if( strlen( ev.text.text ) > 0 ) {
                        const unsigned lc = UTF8_getch( ev.text.text );
                        last_input = input_event( lc, input_event_t::keyboard );
#if defined(SDL_PLATFORM_ANDROID)
                        if( !android_is_hardware_keyboard_available() ) {
                            if( !is_string_input( touch_input_context ) && !touch_input_context.allow_text_entry ) {
                                if( get_option<bool>( "ANDROID_AUTO_KEYBOARD" ) ) {
                                    SDL_StopTextInput( ::window.get() );
                                }

                                quick_shortcuts_t &qsl = quick_shortcuts_map[get_quick_shortcut_name(
                                                             touch_input_context.get_category() )];
                                qsl.remove( last_input );
                                add_quick_shortcut( qsl, last_input, false, true );
                                refresh_display();
                            } else if( lc == '\n' || lc == KEY_ESCAPE ) {
                                if( get_option<bool>( "ANDROID_AUTO_KEYBOARD" ) ) {
                                    SDL_StopTextInput( ::window.get() );
                                }
                            }
                        }
#endif
                    } else {
                        // no key pressed in this event
                        last_input = input_event();
                        last_input.type = input_event_t::keyboard;
                    }
                    last_input.text = ev.text.text;
                    text_refresh = true;
                }
                break;
            case SDL_EVENT_TEXT_EDITING: {
                if( strlen( ev.edit.text ) > 0 ) {
                    const unsigned lc = UTF8_getch( ev.edit.text );
                    last_input = input_event( lc, input_event_t::keyboard );
                } else {
                    // no key pressed in this event
                    last_input = input_event();
                    last_input.type = input_event_t::keyboard;
                }
                last_input.edit = ev.edit.text;
                last_input.edit_refresh = true;
                text_refresh = true;
            }
            break;
            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
                last_input = input_event( ev.jbutton.button, input_event_t::keyboard );
                break;
            case SDL_EVENT_JOYSTICK_AXIS_MOTION:
                // on gamepads, the axes are the analog sticks
                // TODO: somehow get the "digipad" values from the axes
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if( get_option<std::string>( "HIDE_CURSOR" ) == "show" ||
                    get_option<std::string>( "HIDE_CURSOR" ) == "hidekb" ) {
                    if( !SDL_CursorVisible() ) {
                        SDL_ShowCursor();
                    }

                    // Only monitor motion when cursor is visible
                    last_input = input_event( MOUSE_MOVE, input_event_t::mouse );
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                switch( ev.button.button ) {
                    case SDL_BUTTON_LEFT:
                        last_input = input_event( MOUSE_BUTTON_LEFT, input_event_t::mouse );
                        break;
                    case SDL_BUTTON_RIGHT:
                        last_input = input_event( MOUSE_BUTTON_RIGHT, input_event_t::mouse );
                        break;
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if( ev.wheel.y > 0 ) {
                    last_input = input_event( SCROLLWHEEL_UP, input_event_t::mouse );
                } else if( ev.wheel.y < 0 ) {
                    last_input = input_event( SCROLLWHEEL_DOWN, input_event_t::mouse );
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

    // While the ImGui dev panel is open, repaint every CheckMessages tick — not
    // only on input events — so it animates/updates continuously. get_input_event
    // spins CheckMessages ~1 kHz while waiting; vsync on submit_frame caps actual
    // redraws to the display rate. Without this an idle panel (no mouse motion)
    // looks frozen.
    if( imgui_layer::visible() ) {
        needupdate = true;
    }

    bool resized = false;
    if( resize_dims.has_value() ) {
        restore_on_out_of_scope<input_event> prev_last_input( last_input );
        needupdate = resized = handle_resize( resize_dims.value().x, resize_dims.value().y );
    }
    if( !resized && render_target_reset ) {
        reinitialize_framebuffer( true );
        needupdate = true;
        restore_on_out_of_scope<input_event> prev_last_input( last_input );
        // FIXME: SDL_RENDER_TARGETS_RESET only seems to be fired after the first redraw
        // when restoring the window after system sleep, rather than immediately
        // on focus gain. This seems to mess up the first redraw and
        // causes black screen that lasts ~0.5 seconds before the screen
        // contents are redrawn in the following code.
        ui_manager::invalidate( rectangle<point>( point_zero, point( WindowWidth, WindowHeight ) ), false );
        ui_manager::redraw_invalidated();
    }
    if( needupdate ) {
        try_sdl_update();
    }
    if( quit ) {
        exit_handler( 0 );
    }
}

//***********************************
//Pseudo-Curses Functions           *
//***********************************

// Calculates the new width of the window
int projected_window_width()
{
    return get_option<int>( "TERMINAL_X" ) * fontwidth;
}

// Calculates the new height of the window
int projected_window_height()
{
    return get_option<int>( "TERMINAL_Y" ) * fontheight;
}

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
    scaling_factor = 1;
    point terminal( get_option<int>( "TERMINAL_X" ), get_option<int>( "TERMINAL_Y" ) );

    if( get_option<std::string>( "SCALING_FACTOR" ) == "2" ) {
        scaling_factor = 2;
    } else if( get_option<std::string>( "SCALING_FACTOR" ) == "4" ) {
        scaling_factor = 4;
    }

    if( scaling_factor > 1 ) {

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
            FULL_SCREEN_WIDTH * fontwidth * scaling_factor > max_width ) {
            if( FULL_SCREEN_WIDTH * fontwidth * scaling_factor > max_width ) {
                dbg( DL::Warn ) << "SCALING_FACTOR set too high for display size, resetting to 1";
                scaling_factor = 1;
                terminal.x = max_width / fontwidth;
                terminal.y = max_height / fontheight;
                get_options().get_option( "SCALING_FACTOR" ).setValue( "1" );
            } else {
                terminal.x = max_width / fontwidth;
            }
        }

        if( terminal.y * fontheight > max_height ||
            FULL_SCREEN_HEIGHT * fontheight * scaling_factor > max_height ) {
            if( FULL_SCREEN_HEIGHT * fontheight * scaling_factor > max_height ) {
                dbg( DL::Warn ) << "SCALING_FACTOR set too high for display size, resetting to 1";
                scaling_factor = 1;
                terminal.x = max_width / fontwidth;
                terminal.y = max_height / fontheight;
                get_options().get_option( "SCALING_FACTOR" ).setValue( "1" );
            } else {
                terminal.y = max_height / fontheight;
            }
        }

        terminal.x -= terminal.x % scaling_factor;
        terminal.y -= terminal.y % scaling_factor;

        terminal.x = std::max( FULL_SCREEN_WIDTH * scaling_factor, terminal.x );
        terminal.y = std::max( FULL_SCREEN_HEIGHT * scaling_factor, terminal.y );

        get_options().get_option( "TERMINAL_X" ).setValue(
            std::max( FULL_SCREEN_WIDTH * scaling_factor, terminal.x ) );
        get_options().get_option( "TERMINAL_Y" ).setValue(
            std::max( FULL_SCREEN_HEIGHT * scaling_factor, terminal.y ) );

        get_options().save();
    }

    TERMINAL_WIDTH = terminal.x / scaling_factor;
    TERMINAL_HEIGHT = terminal.y / scaling_factor;
}

//Basic Init, create the font, backbuffer, etc
void catacurses::init_interface()
{
    last_input = input_event();
    inputdelay = -1;

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
    tilecontext = std::make_shared<cata_tiles>( renderer, geometry );
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
        dbg( DL::Error ) << "failed to check for tileset: " << err.what();
        // use_tiles is the cached value of the USE_TILES option.
        // most (all?) code refers to this to see if cata_tiles should be used.
        // Setting it to false disables this from getting used.
        use_tiles = false;
    }
    if( tilesName == omTilesName ) {
        overmap_tilecontext = tilecontext;
    } else {
        try {
            overmap_tilecontext = std::make_shared<cata_tiles>( renderer, geometry );
            std::vector<mod_id> dummy;
            overmap_tilecontext->load_tileset(
                omTilesName,
                dummy,
                /*precheck=*/true,
                /*force=*/false,
                /*pump_events=*/true
            );
        } catch( const std::exception &err ) {
            dbg( DL::Error ) << "failed to check for overmap tileset: " << err.what();
            // use_tiles is the cached value of the USE_TILES option.
            // most (all?) code refers to this to see if cata_tiles should be used.
            // Setting it to false disables this from getting used.
            use_tiles = false;
        }
    }
    color_loader<SDL_Color>().load( windowsPalette );
    init_colors();

    // initialize sound set
    load_soundset();

    try {
        font = std::make_unique<FontFallbackList>( renderer, format, fl.fontwidth, fl.fontheight,
                windowsPalette, fl.typeface, fl.fontsize, fl.fontblending );
        map_font = std::make_unique<FontFallbackList>( renderer, format, fl.map_fontwidth,
                   fl.map_fontheight,
                   windowsPalette, fl.map_typeface, fl.map_fontsize, fl.fontblending );
        overmap_font = std::make_unique<FontFallbackList>( renderer, format, fl.overmap_fontwidth,
                       fl.overmap_fontheight,
                       windowsPalette, fl.overmap_typeface, fl.overmap_fontsize, fl.fontblending );
    } catch( std::exception &e ) {
        font.reset();
        map_font.reset();
        overmap_font.reset();
        throw;
    }

    stdscr = newwin( get_terminal_height(), get_terminal_width(), point_zero );
    //newwin calls `new WINDOW`, and that will throw, but not return nullptr.

}

// This is supposed to be called from init.cpp, and only from there.
void load_tileset()
{
    if( !tilecontext || !use_tiles ) {
        return;
    }
    const auto tilesName = get_option<std::string>( "TILES" );
    const auto omTilesName = get_option<std::string>( "OVERMAP_TILES" );
    tilecontext->load_tileset(
        tilesName,
        world_generator->active_world->info->active_mod_order,
        /*precheck=*/false,
        /*force=*/false,
        /*pump_events=*/true
    );
    tilecontext->do_tile_loading_report( []( const std::string & str ) {
        DebugLog( DL::Info, DC::Main ) << str;
    } );

    if( tilesName == omTilesName ) {
        overmap_tilecontext = tilecontext;
    } else {
        if( overmap_tilecontext ) {
            overmap_tilecontext = std::make_shared<cata_tiles>( renderer, geometry );
            overmap_tilecontext->load_tileset(
                omTilesName,
                world_generator->active_world->info->active_mod_order,
                /*precheck=*/false,
                /*force=*/false,
                /*pump_events=*/true
            );
            overmap_tilecontext->do_tile_loading_report( []( const std::string & str ) {
                DebugLog( DL::Info, DC::Main ) << str;
            } );
        }
    }
}

//Ends the terminal, destroy everything
void catacurses::endwin()
{
    tilecontext.reset();
    overmap_tilecontext.reset();
    font.reset();
    map_font.reset();
    overmap_font.reset();
    WinDestroy();
}

template<>
SDL_Color color_loader<SDL_Color>::from_rgb( const int r, const int g, const int b )
{
    SDL_Color result;
    result.b = b;       //Blue
    result.g = g;       //Green
    result.r = r;       //Red
    result.a = 0xFF;    // Opaque
    return result;
}

void input_manager::set_timeout( const int t )
{
    input_timeout = t;
    inputdelay = t;
}

void input_manager::pump_events()
{
    if( test_mode ) {
        return;
    }

    // Handle all events, but ignore any keypress
    CheckMessages();

    last_input = input_event();
    previously_pressed_key = 0;
}

// This is how we're actually going to handle input events, SDL getch
// is simply a wrapper around this.
input_event input_manager::get_input_event()
{
    previously_pressed_key = 0;

    // standards note: getch is sometimes required to call refresh
    // see, e.g., http://linux.die.net/man/3/getch
    // so although it's non-obvious, that refresh() call (and maybe InvalidateRect?) IS supposed to be there
    // however, the refresh call has not effect when nothing has been drawn, so
    // we can skip it if `needupdate` is false to improve performance during mouse
    // move events.
    if( needupdate ) {
        wrefresh( catacurses::stdscr );
    }

    if( inputdelay < 0 ) {
        do {
            CheckMessages();
            if( last_input.type != input_event_t::error ) {
                break;
            }
            SDL_Delay( 1 );
        } while( last_input.type == input_event_t::error );
    } else if( inputdelay > 0 ) {
        Uint64 starttime = SDL_GetTicks();
        Uint64 endtime = 0;
        bool timedout = false;
        do {
            CheckMessages();
            endtime = SDL_GetTicks();
            if( last_input.type != input_event_t::error ) {
                break;
            }
            SDL_Delay( 1 );
            timedout = endtime >= starttime + inputdelay;
            if( timedout ) {
                last_input.type = input_event_t::timeout;
            }
        } while( !timedout );
    } else {
        CheckMessages();
    }

    if( last_input.type == input_event_t::mouse ) {
        float mx, my;
        SDL_GetMouseState( &mx, &my );
        last_input.mouse_pos.x = static_cast<int>( mx );
        last_input.mouse_pos.y = static_cast<int>( my );
    } else if( last_input.type == input_event_t::keyboard ) {
        previously_pressed_key = last_input.get_first_input();
    }

    return last_input;
}

bool gamepad_available()
{
    return joystick != nullptr;
}

void rescale_tileset( float size )
{
    tilecontext->set_draw_scale( size );
}

static window_dimensions get_window_dimensions( const catacurses::window &win,
        point pos, point size )
{
    window_dimensions dim;
    if( use_tiles && g && win == g->w_terrain ) {
        // tiles might have different dimensions than standard font
        dim.scaled_font_size.x = tilecontext->get_tile_width();
        dim.scaled_font_size.y = tilecontext->get_tile_height();
    } else if( map_font && g && win == g->w_terrain ) {
        // map font (if any) might differ from standard font
        dim.scaled_font_size.x = map_font->width;
        dim.scaled_font_size.y = map_font->height;
    } else if( overmap_font && g && win == g->w_overmap ) {
        if( use_tiles && use_tiles_overmap ) {
            // tiles might have different dimensions than standard font
            dim.scaled_font_size.x = overmap_tilecontext->get_tile_width();
            dim.scaled_font_size.y = overmap_tilecontext->get_tile_height();
        } else {
            dim.scaled_font_size.x = overmap_font->width;
            dim.scaled_font_size.y = overmap_font->height;
        }
    } else {
        dim.scaled_font_size.x = fontwidth;
        dim.scaled_font_size.y = fontheight;
    }

    // multiplied by the user's specified scaling factor regardless of whether tiles are in use
    dim.scaled_font_size *= get_scaling_factor();

    if( win ) {
        cata_cursesport::WINDOW *const pwin = win.get<cata_cursesport::WINDOW>();
        dim.window_pos_cell = pwin->pos;
        dim.window_size_cell.x = pwin->width;
        dim.window_size_cell.y = pwin->height;
    } else {
        dim.window_pos_cell = pos;
        dim.window_size_cell = size;
    }

    // the window position is *always* in standard font dimensions!
    dim.window_pos_pixel = point( dim.window_pos_cell.x * fontwidth,
                                  dim.window_pos_cell.y * fontheight );
    // But the size of the window is in the font dimensions of the window.
    dim.window_size_pixel.x = dim.window_size_cell.x * dim.scaled_font_size.x;
    dim.window_size_pixel.y = dim.window_size_cell.y * dim.scaled_font_size.y;

    return dim;
}

window_dimensions get_window_dimensions( const catacurses::window &win )
{
    return get_window_dimensions( win, point_zero, point_zero );
}

window_dimensions get_window_dimensions( point pos, point size )
{
    return get_window_dimensions( {}, pos, size );
}

auto get_sdl_window_size() -> point
{
    return point( std::max( 1, WindowWidth / scaling_factor ),
                  std::max( 1, WindowHeight / scaling_factor ) );
}

auto get_sdl_font_size() -> point
{
    return point( fontwidth, fontheight );
}

std::optional<tripoint_bub_ms> input_context::get_coordinates( const catacurses::window
        &capture_win_ )
{
    if( !coordinate_input_received ) {
        return std::nullopt;
    }

    const catacurses::window &capture_win = capture_win_ ? capture_win_ : g->w_terrain;
    const window_dimensions dim = get_window_dimensions( capture_win );

    const int &fw = dim.scaled_font_size.x;
    const int &fh = dim.scaled_font_size.y;
    point win_min = dim.window_pos_pixel;
    point win_size = dim.window_size_pixel;
    const point win_max = win_min + win_size;

    // Translate mouse coordinates to map coordinates based on tile size
    // Check if click is within bounds of the window we care about
    const inclusive_rectangle<point> win_bounds( win_min, win_max );
    if( !win_bounds.contains( coordinate ) ) {
        return std::nullopt;
    }

    point_bub_ms view_offset;
    if( capture_win == g->w_terrain ) {
        view_offset = g->ter_view_p.xy();
    }

    const point screen_pos = coordinate - win_min;
    point_bub_ms p;
    if( tile_iso && use_tiles ) {
        const float win_mid_x = win_min.x + win_size.x / 2.0f;
        const float win_mid_y = -win_min.y + win_size.y / 2.0f;
        const int screen_col = std::round( ( screen_pos.x - win_mid_x ) / ( fw / 2.0 ) );
        const int screen_row = std::round( ( screen_pos.y - win_mid_y ) / ( fw / 4.0 ) );
        const point_rel_ms selected( ( screen_col - screen_row ) / 2, ( screen_row + screen_col ) / 2 );
        p = view_offset + selected;
    } else {
        const point_rel_ms selected( screen_pos.x / fw, screen_pos.y / fh );
        p = view_offset + selected - dim.window_size_cell / 2;
    }

    return tripoint_bub_ms( p, g->get_levz() );
}

int get_terminal_width()
{
    return TERMINAL_WIDTH;
}

int get_terminal_height()
{
    return TERMINAL_HEIGHT;
}

int get_scaling_factor()
{
    return scaling_factor;
}

static int map_font_width()
{
    if( use_tiles && tilecontext ) {
        return tilecontext->get_tile_width();
    }
    return ( map_font ? map_font.get() : font.get() )->width;
}

static int map_font_height()
{
    if( use_tiles && tilecontext ) {
        return tilecontext->get_tile_height();
    }
    return ( map_font ? map_font.get() : font.get() )->height;
}

static int overmap_font_width()
{
    if( use_tiles && overmap_tilecontext && use_tiles_overmap ) {
        return overmap_tilecontext->get_tile_width();
    }
    return ( overmap_font ? overmap_font.get() : font.get() )->width;
}

static int overmap_font_height()
{
    if( use_tiles && overmap_tilecontext && use_tiles_overmap ) {
        return overmap_tilecontext->get_tile_height();
    }
    return ( overmap_font ? overmap_font.get() : font.get() )->height;
}

void to_map_font_dim_width( int &w )
{
    w = ( w * fontwidth ) / map_font_width();
}

void to_map_font_dim_height( int &h )
{
    h = ( h * fontheight ) / map_font_height();
}

void to_map_font_dimension( int &w, int &h )
{
    to_map_font_dim_width( w );
    to_map_font_dim_height( h );
}

void from_map_font_dimension( int &w, int &h )
{
    w = ( w * map_font_width() + fontwidth - 1 ) / fontwidth;
    h = ( h * map_font_height() + fontheight - 1 ) / fontheight;
}

void to_overmap_font_dimension( int &w, int &h )
{
    w = ( w * fontwidth ) / overmap_font_width();
    h = ( h * fontheight ) / overmap_font_height();
}

bool is_draw_tiles_mode()
{
    return use_tiles;
}

/** Saves a screenshot of the current viewport as a PNG file.
 * Re-renders the current queue state to a temporary offscreen GPU texture,
 * downloads it via SDL_GPU copy pass, then saves with IMG_SavePNG.
 * Bridge-free: does not use SDL_RenderReadPixels / display_buffer.
 * @param file_path: Full path where the PNG file should be saved.
 * @returns true on success.
 */
bool save_screenshot( const std::string &file_path )
{
    auto &rs = lighting::get_render_state();
    if( !rs.ready() ) {
        dbg( DL::Error ) << "save_screenshot: render state not ready";
        return false;
    }

    const int w = WindowWidth;
    const int h = WindowHeight;
    if( w <= 0 || h <= 0 ) {
        return false;
    }

    // Use the same format as the swapchain so the existing pipeline matches.
    SDL_GPUTextureCreateInfo tci{};
    tci.type                 = SDL_GPU_TEXTURETYPE_2D;
    tci.format               = rs.device().swapchain_format();
    tci.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width                = static_cast<Uint32>( w );
    tci.height               = static_cast<Uint32>( h );
    tci.layer_count_or_depth = 1;
    tci.num_levels           = 1;
    tci.sample_count         = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *offscreen = SDL_CreateGPUTexture( rs.device().raw(), &tci );
    if( printErrorIf( !offscreen, "save_screenshot: SDL_CreateGPUTexture failed" ) ) {
        return false;
    }

    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer( rs.device().raw() );
    if( !cb ) {
        SDL_ReleaseGPUTexture( rs.device().raw(), offscreen );
        return false;
    }

    // Re-render current queue state into the offscreen texture.
    rs.tile_batcher().begin_frame();
    constexpr float clear_black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    rs.tile_batcher().begin_pass( cb, offscreen,
                                  static_cast<Uint32>( w ), static_cast<Uint32>( h ),
                                  clear_black );
    if( !rs.tile_sprites_empty() && rs.gpu_sampler() ) {
        rs.flush_tile_sprites( rs.tile_batcher(), rs.gpu_sampler() );
    }
    if( !rs.ui_rects_empty() && rs.geometry().white_texture() ) {
        // UI rects are HUD: unlit segment.
        rs.tile_batcher().set_texture( rs.geometry().white_texture(),
                                       rs.gpu_sampler(), /*is_lit=*/false );
        rs.flush_ui_rects( rs.tile_batcher() );
    }
    if( !rs.font_glyphs_empty() && rs.gpu_sampler() ) {
        rs.flush_font_glyphs( rs.tile_batcher(), rs.gpu_sampler() );
    }
    rs.tile_batcher().end_pass();

    // Download the rendered pixels to a CPU-accessible transfer buffer.
    const Uint32 row_pitch = static_cast<Uint32>( w ) * 4;
    const Uint32 buf_size  = row_pitch * static_cast<Uint32>( h );

    SDL_GPUTransferBufferCreateInfo tbci{};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tbci.size  = buf_size;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer( rs.device().raw(), &tbci );
    if( !tb ) {
        SDL_CancelGPUCommandBuffer( cb );
        SDL_ReleaseGPUTexture( rs.device().raw(), offscreen );
        return false;
    }

    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
    SDL_GPUTextureTransferInfo dst_info{};
    dst_info.transfer_buffer = tb;
    dst_info.pixels_per_row  = static_cast<Uint32>( w );
    SDL_GPUTextureRegion region{};
    region.texture = offscreen;
    region.w       = static_cast<Uint32>( w );
    region.h       = static_cast<Uint32>( h );
    region.d       = 1;
    SDL_DownloadFromGPUTexture( cp, &region, &dst_info );
    SDL_EndGPUCopyPass( cp );

    SDL_SubmitGPUCommandBuffer( cb );
    SDL_WaitForGPUIdle( rs.device().raw() );

    bool ok = false;
    void *mapped = SDL_MapGPUTransferBuffer( rs.device().raw(), tb, false );
    if( mapped ) {
        // Swapchain is typically BGRA8 on D3D12; map to SDL_PIXELFORMAT_ARGB8888
        // (little-endian BGRA byte order) so IMG_SavePNG writes correct colours.
        SDL_Surface *surf = SDL_CreateSurfaceFrom(
            w, h, SDL_PIXELFORMAT_ARGB8888,
            mapped, static_cast<int>( row_pitch ) );
        if( surf ) {
            ok = !printErrorIf(
                     !IMG_SavePNG( surf, file_path.c_str() ),
                     ( std::string( "save_screenshot: cannot save file: " ) + file_path ).c_str() );
            SDL_DestroySurface( surf );
        }
        SDL_UnmapGPUTransferBuffer( rs.device().raw(), tb );
    }

    SDL_ReleaseGPUTransferBuffer( rs.device().raw(), tb );
    SDL_ReleaseGPUTexture( rs.device().raw(), offscreen );
    return ok;
}

void repoint_overmap_tilecontext()
{
    overmap_tilecontext = std::make_shared<cata_tiles>( renderer, geometry );
}
#ifdef _WIN32
HWND getWindowHandle()
{
    return static_cast<HWND>( SDL_GetPointerProperty(
                                  SDL_GetWindowProperties( ::window.get() ),
                                  SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                  nullptr ) );
}
#endif

const SDL_Renderer_Ptr &get_sdl_renderer()
{
    return renderer;
}

const SDL_Window_Ptr &get_sdl_window()
{
    return window;
}


