#include "cursesdef.h" // IWYU pragma: associated
#include "sdltiles.h" // IWYU pragma: associated

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
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
#include "string_formatter.h"
#include "uistate.h"
#include "ui_manager.h"
#include "wcwidth.h"
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
static bool clear_display_buffer_before_redraw = false;
static const std::string empty_string;

palette_array windowsPalette;

static Font_Ptr font;
static Font_Ptr map_font;
static Font_Ptr overmap_font;

static SDL_Window_Ptr window;
static SDL_Renderer_Ptr renderer;
static SDL_PixelFormat format = SDL_PIXELFORMAT_UNKNOWN;
static SDL_Texture_Ptr display_buffer;
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

static bool SetupRenderTarget()
{
    SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_NONE );
    display_buffer.reset( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_TARGET, WindowWidth / scaling_factor, WindowHeight / scaling_factor ) );
    SDL_SetTextureScaleMode( display_buffer.get(), SDL_SCALEMODE_NEAREST );
    if( printErrorIf( !display_buffer, "Failed to create window buffer" ) ) {
        return false;
    }
    if( printErrorIf( !SDL_SetRenderTarget( renderer.get(), display_buffer.get() ),
                      "SDL_SetRenderTarget failed" ) ) {
        return false;
    }
    ClearScreen();

    return true;
}

//Registers, creates, and shows the Window!!
static void WinCreate()
{
    std::string version = string_format( "Cataclysm: Bright Nights - %s", getVersionString() );

    // Common flags used for fulscreen and for windowed
    int window_flags = 0;
    WindowWidth = TERMINAL_WIDTH * fontwidth * scaling_factor;
    WindowHeight = TERMINAL_HEIGHT * fontheight * scaling_factor;
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

    // On Android SDL seems janky in windowed mode so we're fullscreen all the time.
    // Fullscreen mode is now modified so it obeys terminal width/height, rather than
    // overwriting it with this calculation.
    if( fullscreen || ( window_flags & SDL_WINDOW_MAXIMIZED ) ) {
        SDL_GetWindowSize( ::window.get(), &WindowWidth, &WindowHeight );
        // Ignore previous values, use the whole window, but nothing more.
        TERMINAL_WIDTH = WindowWidth / fontwidth / scaling_factor;
        TERMINAL_HEIGHT = WindowHeight / fontheight / scaling_factor;
    }
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
        renderer.reset( SDL_CreateRenderer( ::window.get(), renderer_driver ) );
        if( printErrorIf( !renderer,
                          "Failed to initialize accelerated renderer, falling back to software rendering" ) ) {
            software_renderer = true;
        } else {
            if( get_option<bool>( "VSYNC" ) ) {
                SDL_SetRenderVSync( renderer.get(), 1 );
            }
            if( !SetupRenderTarget() ) {
                dbg( DL::Error ) << "Failed to initialize display buffer under accelerated rendering, "
                                 "falling back to software rendering.";
                software_renderer = true;
                display_buffer.reset();
                renderer.reset();
            }
        }
    }

    if( software_renderer ) {
        renderer.reset( SDL_CreateRenderer( ::window.get(), "software" ) );
        throwErrorIf( !renderer, "Failed to initialize software renderer" );
        throwErrorIf( !SetupRenderTarget(),
                      "Failed to initialize display buffer under software rendering, unable to continue." );
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
}

static void WinDestroy()
{

    shutdown_sound();
    tilecontext.reset();

    if( joystick ) {
        SDL_CloseJoystick( joystick );

        joystick = nullptr;
    }
    geometry.reset();
    format = SDL_PIXELFORMAT_UNKNOWN;
    display_buffer.reset();
    renderer.reset();
    ::window.reset();
}

/// Converts a color from colorscheme to SDL_Color.
inline const SDL_Color &color_as_sdl( const unsigned char color )
{
    return windowsPalette[color];
}

void refresh_display()
{
    needupdate = false;
    lastupdate = SDL_GetTicks();

    if( test_mode ) {
        return;
    }

    // Select default target (the window), copy rendered buffer
    // there, present it, select the buffer as target again.
    SetRenderTarget( renderer, nullptr );
    ClearScreen();
    RenderCopy( renderer, display_buffer, nullptr, nullptr );
    SDL_RenderPresent( renderer.get() );
    SetRenderTarget( renderer, display_buffer );
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

//for resetting the render target after updating texture caches in cata_tiles.cpp
void set_displaybuffer_rendertarget()
{
    SetRenderTarget( renderer, display_buffer );
}

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

static std::optional<std::pair<tripoint_abs_omt, std::string>> get_mission_arrow(
            const inclusive_cuboid<tripoint> &overmap_area, const tripoint_abs_omt &center )
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
    if( overmap_area.contains( mission_target.raw() ) ) {
        mission_arrow_variant = "mission_cursor";
        return std::make_pair( mission_target, mission_arrow_variant );
    }

    inclusive_rectangle<point> area_flat( overmap_area.p_min.xy(), overmap_area.p_max.xy() );
    if( area_flat.contains( mission_target.raw().xy() ) ) {
        int area_z = center.z();
        if( mission_target.z() > area_z ) {
            mission_arrow_variant = "mission_arrow_up";
        } else {
            mission_arrow_variant = "mission_arrow_down";
        }
        return std::make_pair( tripoint_abs_omt( mission_target.xy(), area_z ), mission_arrow_variant );
    }

    const std::vector<tripoint> traj = line_to( center.raw(),
                                       tripoint( mission_target.raw().xy(), center.raw().z ) );

    if( traj.empty() ) {
        debugmsg( "Failed to gen overmap mission trajectory %s %s",
                  center.to_string(), mission_target.to_string() );
        return std::nullopt;
    }

    tripoint arr_pos = traj[0];
    for( auto it = traj.rbegin(); it != traj.rend(); it++ ) {
        if( overmap_area.contains( *it ) ) {
            arr_pos = *it;
            break;
        }
    }

    const int north_border_y = ( overmap_area.p_max.y - overmap_area.p_min.y ) / 3;
    const int south_border_y = north_border_y * 2;
    const int west_border_x = ( overmap_area.p_max.x - overmap_area.p_min.x ) / 3;
    const int east_border_x = west_border_x * 2;

    tripoint north_pmax( overmap_area.p_max );
    north_pmax.y = overmap_area.p_min.y + north_border_y;
    tripoint south_pmin( overmap_area.p_min );
    south_pmin.y += south_border_y;
    tripoint west_pmax( overmap_area.p_max );
    west_pmax.x = overmap_area.p_min.x + west_border_x;
    tripoint east_pmin( overmap_area.p_min );
    east_pmin.x += east_border_x;

    const inclusive_cuboid<tripoint> north_sector( overmap_area.p_min, north_pmax );
    const inclusive_cuboid<tripoint> south_sector( south_pmin, overmap_area.p_max );
    const inclusive_cuboid<tripoint> west_sector( overmap_area.p_min, west_pmax );
    const inclusive_cuboid<tripoint> east_sector( east_pmin, overmap_area.p_max );

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

    int width = OVERMAP_WINDOW_TERM_WIDTH * font->width;
    int height = OVERMAP_WINDOW_TERM_HEIGHT * font->height;

    {
        //set clipping to prevent drawing over stuff we shouldn't
        SDL_Rect clipRect = { dest.x, dest.y, width, height };
        printErrorIf( !SDL_SetRenderClipRect( renderer.get(), &clipRect ),
                      "SDL_SetRenderClipRect failed" );

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
    const tripoint_abs_omt avatar_pos = you.global_omt_location();
    const tripoint_abs_omt corner_NW = center_abs_omt - point( max_col / 2, max_row / 2 );
    const tripoint_abs_omt corner_SE = corner_NW + point( max_col - 1, max_row - 1 );
    const inclusive_cuboid<tripoint> overmap_area( corner_NW.raw(), corner_SE.raw() );
    // Debug vision allows seeing everything
    const bool has_debug_vision = you.has_trait( trait_id( "DEBUG_NIGHTVISION" ) );
    // sight_points is hoisted for speed reasons.
    const int sight_points = !has_debug_vision ?
                             you.overmap_sight_range( g->light_level( you.posz() ) ) :
                             100;
    const bool showhordes = uistate.overmap_show_hordes;
    const bool viewing_weather = ( ( uistate.overmap_debug_weather || uistate.overmap_visible_weather )
                                   && center_abs_omt.z() >= 0 );
    o = corner_NW.raw().xy();

    const auto global_omt_to_draw_position = []( const tripoint_abs_omt & omp ) {
        // z position is hardcoded to 0 because the things this will be used to draw should not be skipped
        return tripoint( omp.raw().xy(), 0 );
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
                draw_from_id_string( tile, omp.raw(), bgCol, fgCol,
                                     ll, false, 0, false,
                                     height_3d );
            }

            if( blink && uistate.overmap_highlighted_omts.contains( omp ) ) {
                if( tile_iso ) {
                    const tile_search_params tile {"highlight", C_NONE, empty_string, 0, 0};
                    draw_from_id_string( tile, omp.raw(), std::nullopt, std::nullopt, lit_level::LIT, false, 0, false );
                } else {
                    SDL_Color c = curses_color_to_SDL( c_pink );
                    c.a = c.a >> 1;
                    auto p = player_to_screen( omp.raw().xy() );
                    draw_color_at( c, p, SDL_BLENDMODE_BLEND );
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
                            draw_from_id_string( tile, omp.raw(), std::nullopt, std::nullopt, lit_level::LIT, false, 0, false );
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
                            tile, omp.raw(), std::nullopt, std::nullopt, lit_level::LIT, false, 0, false );
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
                            tile, omp.raw(), std::nullopt, std::nullopt, lit_level::LIT, false, 0, false );
                    }
                }
            }

            if( uistate.place_terrain || uistate.place_special ) {
                // Highlight areas that already have been generated
                // TODO: fix point types
                if( ACTIVE_MAPBUFFER.lookup_submap( project_to<coords::sm>( omp ).raw() ) ) {
                    const tile_search_params tile {"highlight", C_NONE, empty_string, 0, 0};
                    draw_from_id_string(
                        tile, omp.raw(), std::nullopt, std::nullopt,
                        lit_level::LIT, false, 0, false );
                }
            }

            if( blink && ACTIVE_OVERMAP_BUFFER.has_vehicle( omp ) ) {
                const std::string tile_id = find_tile_looks_like( "overmap_remembered_vehicle", C_OVERMAP_NOTE )
                                            ? "overmap_remembered_vehicle"
                                            : "note_c_cyan";
                const tile_search_params tile { tile_id, C_OVERMAP_NOTE, "overmap_note", 0, 0 };
                draw_from_id_string(
                    tile, omp.raw(), std::nullopt, std::nullopt,
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
                                           sprite_tile, omp.raw(), std::nullopt, std::nullopt,
                                           lit_level::LIT, false, 0, false );
                }
                if( !drew_note_sprite ) {
                    std::string note_name = "note_" + ter_sym + "_" + string_from_color( ter_color );
                    const tile_search_params tile { note_name, C_OVERMAP_NOTE, "overmap_note", 0, 0 };
                    draw_from_id_string(
                        tile, omp.raw(), std::nullopt, std::nullopt,
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
            if( s_ter.p.z == 0 ) {
                // TODO: fix point types
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
        const tripoint_abs_omt &guy_loc = guy->global_omt_location();
        if( guy_loc.z() == center_abs_omt.z() && ( has_debug_vision ||
                ACTIVE_OVERMAP_BUFFER.seen( guy_loc ) ) ) {
            draw_entity_with_overlays( *guy, global_omt_to_draw_position( guy_loc ), lit_level::LIT,
                                       height_3d );
        }
    }

    if( you.global_omt_location().z() == center_abs_omt.z() ) {
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
        inclusive_cuboid<tripoint> map_cursor_area = overmap_area;
        map_cursor_area.p_max.y--;
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
            const tripoint tile_draw_pos = global_omt_to_draw_position( project_to<coords::omt>
                                           ( city_pos ) ) - o;
            point draw_point( tile_draw_pos.x * tile_width + dest.x,
                              tile_draw_pos.y * tile_height + dest.y );
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
            auto draw_point = point( tile_draw_pos.x * tile_width + dest.x,
                                     tile_draw_pos.y * tile_height + dest.y );
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
            if( ACTIVE_OVERMAP_BUFFER.seen( city_center ) && overmap_area.contains( city_center.raw() ) &&
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
                if( overmap_area.contains( omt_pos.raw() ) ) {
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
            if( !npc->marked_for_death && npc->global_omt_location() == center_abs_omt ) {
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
        const tripoint tile_draw_pos = global_omt_to_draw_position( project_to<coords::omt>
                                       ( center_sm ) ) - o;
        point draw_point( tile_draw_pos.x * tile_width + dest.x,
                          tile_draw_pos.y * tile_height + dest.y );
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

    printErrorIf( !SDL_SetRenderClipRect( renderer.get(), nullptr ),
                  "SDL_SetRenderClipRect failed" );
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

            if( oldWinCompatible && cell == oldcell && fontScale == fontScaleBuffer ) {
                continue;
            }
            oldcell = cell;

            if( cell.ch.empty() ) {
                continue; // second cell of a multi-cell character
            }

            // Spaces are used a lot, so this does help noticeably
            if( cell.ch == space_string ) {
                geometry->rect( renderer, point( drawx, drawy ), font->width, font->height,
                                color_as_sdl( cell.BG ) );
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
            geometry->rect( renderer, point( drawx, drawy ), font->width * cw, font->height,
                            color_as_sdl( BG ) );
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
    if( clear_display_buffer_before_redraw ) {
        clear_display_buffer_before_redraw = false;
        SetRenderTarget( renderer, display_buffer );
        ClearScreen();
    }

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
            tripoint( g->u.pos().xy(), g->ter_view_p.z ),
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
        throwErrorIf( !SetupRenderTarget(), "SetupRenderTarget failed" );
        game_ui::init_ui();
        ui_manager::screen_resized();
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
                if( lc <= 0 ) {
                    // a key we don't know in curses and won't handle.
                    break;
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
    bool resized = false;
    if( resize_dims.has_value() ) {
        restore_on_out_of_scope<input_event> prev_last_input( last_input );
        needupdate = resized = handle_resize( resize_dims.value().x, resize_dims.value().y );
    }
    // resizing already reinitializes the render target
    if( !resized && render_target_reset ) {
        throwErrorIf( !SetupRenderTarget(), "SetupRenderTarget failed" );
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

    font = std::make_unique<FontFallbackList>( renderer, format, fl.fontwidth, fl.fontheight,
            windowsPalette, fl.typeface, fl.fontsize, fl.fontblending );
    map_font = std::make_unique<FontFallbackList>( renderer, format, fl.map_fontwidth,
               fl.map_fontheight,
               windowsPalette, fl.map_typeface, fl.map_fontsize, fl.fontblending );
    overmap_font = std::make_unique<FontFallbackList>( renderer, format, fl.overmap_fontwidth,
                   fl.overmap_fontheight,
                   windowsPalette, fl.overmap_typeface, fl.overmap_fontsize, fl.fontblending );
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

auto get_sdl_display_buffer_size() -> point
{
    if( !display_buffer ) { return point_zero; }

    auto width = 0.0f;
    auto height = 0.0f;

    if( !SDL_GetTextureSize( display_buffer.get(), &width, &height ) ) {
        return point_zero;
    }
    return point( width, height );
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

void clear_sdl_display_buffer()
{
    if( !renderer || !display_buffer ) { return; }

    SetRenderTarget( renderer, display_buffer );
    ClearScreen();
}

void clear_sdl_display_buffer_before_redraw()
{
    clear_display_buffer_before_redraw = true;
    reinitialize_framebuffer( true );
}

std::optional<tripoint> input_context::get_coordinates( const catacurses::window &capture_win_ )
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

    point view_offset;
    if( capture_win == g->w_terrain ) {
        view_offset = g->ter_view_p.xy();
    }

    const point screen_pos = coordinate - win_min;
    point p;
    if( tile_iso && use_tiles ) {
        const float win_mid_x = win_min.x + win_size.x / 2.0f;
        const float win_mid_y = -win_min.y + win_size.y / 2.0f;
        const int screen_col = std::round( ( screen_pos.x - win_mid_x ) / ( fw / 2.0 ) );
        const int screen_row = std::round( ( screen_pos.y - win_mid_y ) / ( fw / 4.0 ) );
        const point selected( ( screen_col - screen_row ) / 2, ( screen_row + screen_col ) / 2 );
        p = view_offset + selected;
    } else {
        const point selected( screen_pos.x / fw, screen_pos.y / fh );
        p = view_offset + selected - dim.window_size_cell / 2;
    }

    return tripoint( p, g->get_levz() );
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

/** Saves a screenshot of the current viewport, as a PNG file, to the given location.
* @param file_path: A full path to the file where the screenshot should be saved.
* @returns `true` if the screenshot generation was successful, `false` otherwise.
*/
bool save_screenshot( const std::string &file_path )
{
    // SDL3: SDL_RenderReadPixels returns a new SDL_Surface* owned by caller.
    auto readback = SDL_Surface_Ptr( SDL_RenderReadPixels( renderer.get(), nullptr ) );
    if( printErrorIf( !readback, "save_screenshot: cannot read data from SDL_Renderer." ) ) {
        return false;
    }

    // Save screenshot as PNG file
    const bool ok = !printErrorIf( !IMG_SavePNG( readback.get(), file_path.c_str() ),
                                   std::string( "save_screenshot: cannot save screenshot file: " +
                                           file_path ).c_str() );
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


