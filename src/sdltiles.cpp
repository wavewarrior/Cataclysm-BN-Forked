// MUST precede any game header: debug.h defines a function-like `DebugLog`
// macro that otherwise mangles ImGui::DebugLog in imgui.h (same reason
// imgui_layer.cpp includes imgui.h before debug.h).
#include "imgui.h"

#include "cursesdef.h" // IWYU pragma: associated
#include "sdltiles.h" // IWYU pragma: associated
#include "sdl_input.h"
#include "sdl_fonts.h"
#include "sdl_framebuffer.h"

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
#include "overmap_ui.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "point.h"
#include "rng.h"
#include "sdl_wrappers.h"
#include "sdl_geometry.h"
#include "sdl_utils.h"
#include "sdl_font.h"
#include "sdl_lighting_devui.h"
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
#include "weather.h"
#include "weather_type.h"
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

display_context g_display;

//***********************************
//Globals                           *
//***********************************

std::shared_ptr<cata_tiles> tilecontext;
std::shared_ptr<cata_tiles> overmap_tilecontext;

palette_array windowsPalette;

int fontwidth;          //the width of the font, background is always this size
int fontheight;         //the height of the font, background is always this size

// Reference aliases — existing code reads/writes these names and they
// transparently reach g_display.member.  Each later extraction step
// batch-migrates its domain's refs into the extracted module and drops
// the alias.
static Uint64 &lastupdate = g_display.lastupdate;
static bool &needupdate = g_display.needupdate;
static bool &need_invalidate_framebuffers = g_display.need_invalidate_framebuffers;

static Font_Ptr &font = g_display.font;
static Font_Ptr &map_font = g_display.map_font;
static Font_Ptr &overmap_font = g_display.overmap_font;

static SDL_Window_Ptr &window = g_display.window;
// Phase 2i-B-1: SDL_Renderer no longer claims the visible window — that
// belongs to the SDL_GPU device now (lighting::render_state). The legacy
// renderer keeps running on a hidden mirror window so every call site that
// still talks SDL_Renderer (cata_tiles draw_sprite_at, sdl_font glyph
// cache, pixel_minimap, vehicle_preview …) compiles and executes
// unchanged; its output is just invisible. Subsequent 2i-B-N commits port
// those call sites to the GPU stack and drop the hidden window.
static SDL_Window_Ptr &legacy_window = g_display.legacy_window;
static SDL_Renderer_Ptr &renderer = g_display.renderer;
static SDL_PixelFormat &format = g_display.format;
static GeometryRenderer_Ptr &geometry = g_display.geometry;
static int &WindowWidth = g_display.WindowWidth;        //Width of the actual window, not the curses window
static int &WindowHeight = g_display.WindowHeight;       //Height of the actual window, not the curses window
// input from various input sources. Each input source sets the type and
// the actual input value (key pressed, mouse button clicked, ...)
// This value is finally returned by input_manager::get_input_event.
static input_event &last_input = g_display.last_input;

static int &inputdelay = g_display.inputdelay;         //How long getch will wait for a character to be typed

static int &TERMINAL_WIDTH = g_display.TERMINAL_WIDTH;
static int &TERMINAL_HEIGHT = g_display.TERMINAL_HEIGHT;
bool &fullscreen = g_display.fullscreen;
int &scaling_factor = g_display.scaling_factor;

static SDL_Joystick *&joystick = g_display.joystick; // Only one joystick for now.

//***********************************
//Non-curses, Window functions      *
//***********************************

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
    // Initialize framebuffer caches (self-validating per-family)
    cache_initialize_all(
        TERMINAL_HEIGHT, TERMINAL_WIDTH,
        /*overmap*/ OVERMAP_WINDOW_HEIGHT, OVERMAP_WINDOW_WIDTH,
        /*terrain*/ TERRAIN_WINDOW_HEIGHT, TERRAIN_WINDOW_WIDTH
    );

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
        imgui_layer::set_dev_ui( sdl_lighting_devui::draw );
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
        // Dev cursor light (F4): translate the mouse pixel into the shader's
        // world-tile space so the snapshot can pin an omni emitter under it.
        // Inverse of the sprite vertex's tile→screen map: world = (mouse_px -
        // draw_offset)/tile_dim + tile_map_origin. Mouse, draw_offset and tile
        // dims are all in LOGICAL window pixels (the projection space the tiles
        // draw in; see the proj_w/h note below), so no HiDPI density scaling.
        if( cursor_light_emitter::enabled && g && tilecontext
            && world_generator && world_generator->active_world ) {
            float msx = 0.0f, msy = 0.0f;
            SDL_GetMouseState( &msx, &msy );
            const point o  = tilecontext->get_tile_map_origin().raw();
            const point op = tilecontext->get_drawing_pixel_offset();
            const int   tw = std::max( 1, tilecontext->get_tile_width() );
            const int   th = std::max( 1, tilecontext->get_tile_height() );
            cursor_light_emitter::wx = ( msx - static_cast<float>( op.x ) )
                                       / static_cast<float>( tw ) + static_cast<float>( o.x );
            cursor_light_emitter::wy = ( msy - static_cast<float>( op.y ) )
                                       / static_cast<float>( th ) + static_cast<float>( o.y );
            cursor_light_emitter::wz = static_cast<float>( g->u.bub_pos().z() );
        }
        lighting::frame_lighting_result fr =
            lighting::build_and_submit_lighting( rs, rebuild_pertile, g_dbg_lighting,
                                                 g_skylight_bleed, g_vision_blur );
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

    // Dev oracle (one-shot, F4 button): read back the RC cascade and log stats.
    // Reads the last fully-submitted cascade (this frame's gather is still on the
    // unsubmitted render CB) — fine on a held-still scene, which is when it's used.
    if( g_rc_readback ) {
        g_rc_readback = false;
        if( rs.rc().ready() && rs.sdf().populated() ) {
            rs.rc().debug_log_stats( static_cast<std::uint32_t>( rs.sdf().map_w() ),
                                     static_cast<std::uint32_t>( rs.sdf().map_h() ) );
        }
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
        // Weather multiplier on sun + sky intensity (Bucket A / A3). Reuses the
        // sim's own light model: incident sunlight = clear-sky sunlight() plus the
        // weather type's light_modifier (negative for clouds/rain/storm; see
        // incident_sunlight() in weather.cpp). Normalised against the clear-sky
        // baseline so it is 1.0 in clear weather and dims under overcast, keeping
        // the GPU sun/sky in step with gameplay light. CPU-side multiply only —
        // no shader, wire, or uniform change.
        float weather_mult = 1.0f;
        if( g ) {
            const float base = sunlight( calendar::turn, false );
            // weather_id can be a stale/unregistered id on the main menu after
            // quit-to-menu (the world's weather_type JSON is unloaded but g and
            // the weather_manager survive). is_valid() guards the factory lookup
            // so the dangling "clear" id no longer triggers a debugmsg storm.
            const weather_type_id wid = get_weather().weather_id;
            if( base > 1.0f && wid.is_valid() ) {
                const int mod = wid->light_modifier;
                weather_mult = std::clamp( ( base + static_cast<float>( mod ) ) / base,
                                           0.0f, 1.0f );
            }
            in.sun.sun_intensity *= weather_mult;
            in.sun.sky_intensity *= weather_mult;
        }
        // Repurpose sun.sp_pad as the shader debug-heatmap sentinel.
        in.sun.sp_pad = g_dbg_lighting_shader ? 1.0f : 0.0f;

        // Stash the per-frame volumetric inputs (Step-6 / C2). Consumed at the
        // Pass-W record site, where `in` is out of scope. Sun dir/colour/intensity
        // already carry the A3 weather multiplier; cam_off/tile_px are the SAME
        // values the sprite vtx uses (single source of truth for the world_pos
        // reconstruction in vol.frag). shadow_k/steps reuse the sprite knobs so
        // shafts match the surface-shadow softness. The density/intensity/reach
        // knobs are merged in at record time.
        g_vol_params.tile_pixel_size = in.tile_pixel_size;
        g_vol_params.camera_off_x    = in.camera_off_x;
        g_vol_params.camera_off_y    = in.camera_off_y;
        g_vol_params.current_z       = in.z_level;
        g_vol_params.sun_dir_x       = in.sun.sun_dir_x;
        g_vol_params.sun_dir_y       = in.sun.sun_dir_y;
        g_vol_params.sun_intensity   = in.sun.sun_intensity;
        g_vol_params.sun_r           = in.sun.sun_r;
        g_vol_params.sun_g           = in.sun.sun_g;
        g_vol_params.sun_b           = in.sun.sun_b;
        g_vol_params.shadow_k        = in.debug.shadow_k;
        g_vol_params.shadow_steps    = in.debug.shadow_steps;
        g_vol_params.sdf_map_w       = static_cast<std::uint32_t>( rs.sdf().map_w() );
        g_vol_params.sdf_map_h       = static_cast<std::uint32_t>( rs.sdf().map_h() );

        // Runtime debug tuning: wire the globally controlled debug_params into
        // frame_light_inputs. shader uses these for debug visualization (F7 cycles
        // modes, F8/F9 adjust scales). Defaults are all zeroed (no-op).
        in.debug = g_dbg_params;
        // Feed a live wall-clock seconds (wrapped to keep float32 phase precision)
        // so the foliage sway shader has a non-zero anim_time → sin oscillates.
        in.debug.anim_time = std::fmod( static_cast<float>( SDL_GetTicks() ) / 1000.0f, 1000.0f );
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
        // Per-slice ordered flush: each ui_adaptor slice draws rects-then-glyphs
        // in z-order so overlapping windows occlude correctly. flush_ui binds the
        // white (rect) and per-glyph textures itself.
        rs.flush_ui( rs.tile_batcher(), rs.gpu_sampler() );
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
            // Silhouette sun-shadow mask (Phase 1). Render the TALL subset of
            // the tile queue as sheared coverage into shadow_mask_ BEFORE Pass W
            // (own batcher + pass; the queue is still populated and re-drained by
            // Pass W below). Phase 1 only debug-blits the mask — no sprite wiring
            // yet. Gated with Pass W so the mask is rebuilt on the same frames
            // the world is.
            rs.flush_shadow_casters( ctx.cmd_buffer,
                                     static_cast<std::uint32_t>( proj_w ),
                                     static_cast<std::uint32_t>( proj_h ) );

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

            // Step-6 C2 volumetric sun shafts: composite occlusion-gated lit-fog
            // additively into world_target after it is freshly drawn, BEFORE
            // bloom (so shafts bloom too). Same Pass-W-ran gate as bloom
            // (additive → must not re-add on a retained frame). Off by default
            // (legibility kill-gate); F4 knobs merged onto the stashed per-frame
            // params.
            if( g_vol_enable && rs.volumetric().ready() ) {
                lighting::vol_params vp = g_vol_params;
                vp.vol_density   = g_vol_density;
                vp.vol_intensity = g_vol_intensity;
                vp.vol_reach     = g_vol_reach;
                vp.vol_shadow    = g_vol_shadow;
                // proj-space (game-view) size: vol.frag reconstructs world_pos as
                // uv*proj_size/tile_px - camera_off, because the sprite pass
                // stretches proj-space into the full world_target texture.
                vp.proj_w = static_cast<float>( proj_w );
                vp.proj_h = static_cast<float>( proj_h );
                rs.volumetric().record( ctx.cmd_buffer, wt->texture(),
                                        wt->width(), wt->height(),
                                        rs.sdf().sdf_buffer(), rs.sdf().sky_vis_buffer(),
                                        vp );
            }

            // Step-4 bloom: add glow into world_target AFTER it is freshly drawn
            // this frame (only when Pass W ran — bloom mutates the target in
            // place, so running it on a retained frame would accumulate glow).
            // The tonemap (Pass T) then resolves the augmented HDR target.
            if( g_bloom_enable && rs.bloom().ready() ) {
                rs.bloom().record( ctx.cmd_buffer, wt->texture(),
                                   wt->width(), wt->height(),
                                   g_bloom_threshold, g_bloom_intensity );
            }
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
    if( g_shadow_debug && rs.shadow_mask() && rs.shadow_mask()->texture() ) {
        // Phase-1 kill-gate: show the silhouette-shadow mask instead of the
        // world. Opaque (mask alpha=1) → clean grey shadows on black.
        blit_layer( rs.shadow_mask() );
    } else {
        blit_layer( rs.world_ldr_target() ); // tonemapped world (opaque base)
    }
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
// No-op: display_buffer removed. Remains until atlas lookup uses GPU-native
// keys and SDL_Renderer can be removed entirely (2i-B-7f Part B).
void set_displaybuffer_rendertarget() {}

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

bool gamepad_available()
{
    return sdl_input::gamepad_available( g_display );
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
    // Per-slice ordered UI flush (matches the live compositor path).
    rs.flush_ui( rs.tile_batcher(), rs.gpu_sampler() );
    rs.tile_batcher().end_pass();

    // Submit the render CB, then read back through the reusable
    // capture_texture_to_rgba helper (DRY, byte-identical to the old
    // inline path — verified at extraction time).
    SDL_SubmitGPUCommandBuffer( cb );
    SDL_WaitForGPUIdle( rs.device().raw() );

    std::vector<uint8_t> pixels;
    bool ok = false;
    if( rs.capture_texture_to_rgba( offscreen, w, h, pixels ) ) {
        // Swapchain is typically BGRA8 on D3D12; map to SDL_PIXELFORMAT_ARGB8888
        // (little-endian BGRA byte order) so IMG_SavePNG writes correct colours.
        const Uint32 row_pitch = static_cast<Uint32>( w ) * 4;
        SDL_Surface *surf = SDL_CreateSurfaceFrom(
            w, h, SDL_PIXELFORMAT_ARGB8888,
            pixels.data(), static_cast<int>( row_pitch ) );
        if( surf ) {
            ok = !printErrorIf(
                     !IMG_SavePNG( surf, file_path.c_str() ),
                     ( std::string( "save_screenshot: cannot save file: " ) + file_path ).c_str() );
            SDL_DestroySurface( surf );
        }
    }

    SDL_ReleaseGPUTexture( rs.device().raw(), offscreen );
    return ok;
}

void rescale_tileset( float size )
{
    tilecontext->set_draw_scale( size );
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


