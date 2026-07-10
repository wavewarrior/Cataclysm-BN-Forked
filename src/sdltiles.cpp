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
#include "game.h"
#include "game_ui.h"
#include "get_version.h"
#include "input.h"
#include "runtime_handlers.h"
#include "json.h"
#include "options.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "rng.h"
#include "sdl_wrappers.h"
#include "sdl_geometry.h"
#include "sdl_utils.h"
#include "sdl_font.h"
#include "sdl_lighting_devui.h"
#include "sdl_window.h"
#include "sdlsound.h"
#include "lighting/emitter_collector.h"
#include "lighting/frame_build.h"
#include "lighting/render_state.h"
#include "lighting/snapshot.h"
#include "lighting/sdf_pass.h"
#include "map.h"
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
static SDL_Window_Ptr &window = g_display.window;
// Phase 2i-B-1: SDL_Renderer no longer claims the visible window — that
// belongs to the SDL_GPU device now (lighting::render_state). The legacy
// renderer keeps running on a hidden mirror window so every call site that
// still talks SDL_Renderer (cata_tiles draw_sprite_at, sdl_font glyph
// cache, pixel_minimap, vehicle_preview …) compiles and executes
// unchanged; its output is just invisible. Subsequent 2i-B-N commits port
// those call sites to the GPU stack and drop the hidden window.
static SDL_Renderer_Ptr &renderer = g_display.renderer;
static GeometryRenderer_Ptr &geometry = g_display.geometry;
static int &WindowWidth =
    g_display.WindowWidth;        //Width of the actual window, not the curses window
static int &WindowHeight =
    g_display.WindowHeight;       //Height of the actual window, not the curses window

// only update if the set interval has elapsed
// No-op: display_buffer removed. Remains until atlas lookup uses GPU-native
// keys and SDL_Renderer can be removed entirely (2i-B-7f Part B).
void set_displaybuffer_rendertarget() {}

// This is supposed to be called from init.cpp, and only from there.
void load_tileset()
{
    if( !tilecontext ) {
        return;
    }
    const auto tilesName = get_option<std::string>( "TILES" );
    const auto omTilesName = get_option<std::string>( "OVERMAP_TILES" );
    // active_world may be null during early init (core-data load before any world is selected).
    // Pass an empty mod list in that case; the idempotency guard will cause a proper reload
    // once active_world is set and load_tileset() is called again.
    std::vector<mod_id> mod_list;
    if( world_generator && world_generator->active_world ) {
        mod_list = world_generator->active_world->info->active_mod_order;
    }
    tilecontext->load_tileset(
        tilesName,
        mod_list,
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
            std::vector<mod_id> om_mod_list;
            if( world_generator && world_generator->active_world ) {
                om_mod_list = world_generator->active_world->info->active_mod_order;
            }
            overmap_tilecontext->load_tileset(
                omTilesName,
                om_mod_list,
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
    return true;
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


