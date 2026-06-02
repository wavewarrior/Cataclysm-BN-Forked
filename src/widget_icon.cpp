#include "widget_icon.h"

#include <map>
#include <utility>

#include "debug.h"
#include "lighting/render_state.h"
#include "path_info.h"
#include "sdl_wrappers.h" // SDL3 + SDL3_image (IMG_LoadSizedSVG_IO)

#define dbg(x) DebugLogFL((x),DC::SDL)

namespace
{
// Keyed by (icon name, pixel size) — a font-size change requests a new size and
// re-rasterizes rather than scaling a stale bitmap.
std::map<std::pair<std::string, int>, lighting::gpu_texture_unique_ptr> g_cache;
} // namespace

namespace widget_icon
{

SDL_GPUTexture *get( const std::string &name, int px )
{
    if( name.empty() || px <= 0 ) {
        return nullptr;
    }
    const auto key = std::make_pair( name, px );
    const auto it = g_cache.find( key );
    if( it != g_cache.end() ) {
        return it->second.get();
    }

    const std::string path = PATH_INFO::gfxdir() + "widgets/" + name + ".svg";
    SDL_IOStream *io = SDL_IOFromFile( path.c_str(), "rb" );
    if( io == nullptr ) {
        dbg( DL::Warn ) << "widget_icon: cannot open " << path << ": " << SDL_GetError();
        return nullptr;
    }
    // Rasterize the SVG at exactly px*px (crisp at the current cell size).
    SDL_Surface *surf = IMG_LoadSizedSVG_IO( io, px, px );
    SDL_CloseIO( io );
    if( surf == nullptr ) {
        dbg( DL::Warn ) << "widget_icon: rasterize failed for " << name << ": " << SDL_GetError();
        return nullptr;
    }

    // Upload ONCE and cache the handle — re-uploading and sampling the same
    // frame is the D3D12 barrier race that disabled the loading image.
    SDL_GPUTexture *tex = lighting::get_render_state().upload_surface_to_gpu_texture( surf );
    SDL_DestroySurface( surf );
    if( tex == nullptr ) {
        dbg( DL::Warn ) << "widget_icon: GPU upload failed for " << name;
        return nullptr;
    }
    g_cache.emplace( key, lighting::gpu_texture_unique_ptr( tex ) );
    return tex;
}

void clear()
{
    g_cache.clear();
}

} // namespace widget_icon
