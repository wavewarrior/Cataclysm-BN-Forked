#include "widget_icon.h"

#include <map>
#include <utility>

#include <string>

#include "debug.h"
#include "fstream_utils.h" // read_from_file_json
#include "json.h"          // JsonIn / JsonObject
#include "lighting/render_state.h"
#include "path_info.h"
#include "sdl_wrappers.h" // SDL3 + SDL3_image (IMG_LoadSizedSVG_IO)

#define dbg(x) DebugLogFL((x),DC::SDL)

namespace
{
// Keyed by (icon name, pixel size) — a font-size change requests a new size and
// re-rasterizes rather than scaling a stale bitmap.
std::map<std::pair<std::string, int>, lighting::gpu_texture_unique_ptr> g_cache;

// Logical icon id -> SVG filename, parsed from gfx/widgets/icons.json. Empty
// until load_config() runs; an absent id resolves to "<id>.svg" (back-compat).
std::map<std::string, std::string> g_id_to_file;
bool g_config_loaded = false;

// Resolve a logical icon name to its SVG filename via the registry, falling back
// to "<name>.svg" so unlisted icons keep working.
std::string resolve_icon_file( const std::string &name )
{
    if( !g_config_loaded ) {
        widget_icon::load_config();
    }
    const auto it = g_id_to_file.find( name );
    if( it != g_id_to_file.end() && !it->second.empty() ) {
        return it->second;
    }
    return name + ".svg";
}
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

    const std::string path = PATH_INFO::gfxdir() + "widgets/" + resolve_icon_file( name );
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

void load_config()
{
    g_config_loaded = true; // set first: get() inside the read path must not recurse
    g_id_to_file.clear();
    const std::string path = PATH_INFO::gfxdir() + "widgets/icons.json";
    // optional=true: no registry is fine — every icon falls back to "<id>.svg".
    read_from_file_json( path, []( JsonIn & jsin ) {
        JsonObject jo = jsin.get_object();
        jo.allow_omitted_members(); // "//" comments + "animations" (read by sidebar_anim)
        for( const JsonObject icon : jo.get_array( "icons" ) ) {
            icon.allow_omitted_members(); // ignore "animations" here
            const std::string id = icon.get_string( "id", std::string() );
            if( id.empty() ) {
                continue;
            }
            g_id_to_file[id] = icon.get_string( "svg", id + ".svg" );
        }
    }, true );
}

void reload()
{
    clear();         // drop cached rasters so an edited SVG re-rasterizes
    load_config();   // re-read id -> file mappings
}

} // namespace widget_icon
