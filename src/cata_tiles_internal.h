#pragma once

/// @file
/// TU-local symbols from cata_tiles.cpp that are genuinely shared across
/// multiple decomposition target files.  Declared `inline` so every
/// translation unit that #includes this header gets its own copy (C++17
/// ODR rules) — no separate definition or out-of-line instantiation needed.
///
/// All symbols live in `namespace cata_tiles_internal` to avoid
/// global namespace pollution.  Each decomposition .cpp that uses these
/// symbols adds a local `using namespace cata_tiles_internal;` directive.

/* ------------------------------------------------------------------ */
/* Includes                                                            */
/* ------------------------------------------------------------------ */

#include "enums.h"
#include "filesystem.h"
#include "fstream_utils.h"
#include "hash_utils.h"
#include "int_id.h"
#include "output.h"
#include "path_info.h"
#include "sdl_lighting_devui.h"
#include "sdl_utils.h"

#include <SDL3/SDL.h>

#include <array>
#include <string>

/* ------------------------------------------------------------------ */
/* Namespace                                                           */
/* ------------------------------------------------------------------ */

namespace cata_tiles_internal {

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

/// Empty string constant used across lookup / anim / layers / core for
/// default subcategory arguments and similar.
inline const std::string empty_string;

/// Item highlight tile ID used by ensure_default_item_highlight (tileset) and
/// draw_item_highlight (layers).
inline const std::string ITEM_HIGHLIGHT("highlight_item");

/// Corpse item type used by draw_field_or_item (layers) and
/// find_tile_looks_like / tile_type_search (lookup).
inline const itype_id itype_corpse("corpse");

/// Multitile subtile key names used by draw_from_id_string (core) and
/// tile_type_search (lookup).
inline const std::array<std::string, 8> multitile_keys = {
    {"center", "corner", "edge", "t_connection", "end_piece", "unconnected", "open", "broken"}};

/// Human-readable tile category names indexed by TILE_CATEGORY enum values.
/// Used by lr_generic (tileset) and tile_type_search (lookup).
inline const std::array<std::string, 14> TILE_CATEGORY_IDS = {
    {"",             // C_NONE,
     "vehicle_part", // C_VEHICLE_PART,
     "terrain",      // C_TERRAIN,
     "item",         // C_ITEM,
     "furniture",    // C_FURNITURE,
     "trap",         // C_TRAP,
     "field",        // C_FIELD,
     "lighting",     // C_LIGHTING,
     "monster",      // C_MONSTER,
     "bullet",       // C_BULLET,
     "hit_entity",   // C_HIT_ENTITY,
     "weather",      // C_WEATHER,
     "overmap_terrain", "overmap_weather"}};

/* ------------------------------------------------------------------ */
/* Math utilities                                                      */
/* ------------------------------------------------------------------ */

/// Integer division that rounds toward negative infinity (floor division).
/// Used by find_tile_with_season (lookup) and tile_hit_xform (anim).
inline int divide_round_down(int a, int b) {
    if (b < 0) {
        a = -a;
        b = -b;
    }
    if (a >= 0) {
        return a / b;
    } else {
        return -((-a + b - 1) / b);
    }
}

/* ------------------------------------------------------------------ */
/* SDL surface helpers                                                 */
/* ------------------------------------------------------------------ */

/// Hash a surface (or a sub-region) for dynamic atlas deduplication.
/// Used by tileset::register_warp_surface (core) and
/// tileset_loader::copy_surface_to_dynamic_atlas (tileset).
inline size_t get_surface_hash(SDL_Surface* surf, const SDL_Rect* rect) {
    if (SDL_MUSTLOCK(surf)) { SDL_LockSurface(surf); }

    SDL_Rect rr;
    if (rect == nullptr) {
        rr = {0, 0, surf->w, surf->h};
        rect = &rr;
    }

    size_t hash = 0;
    cata::hash_combine(hash, rect->w);
    cata::hash_combine(hash, rect->h);

    const int dx = rect->w;
    const int dy = rect->h;

    for (int y = 0; y < dy; ++y) {
        const auto offset = static_cast<uint32_t>(((y + rect->y) * surf->w) + rect->x);
        auto pData = static_cast<uint32_t*>(surf->pixels) + offset;
        for (int x = 0; x < dx; ++x, ++pData) { cata::hash_combine(hash, *pData); }
    }

    if (SDL_MUSTLOCK(surf)) { SDL_UnlockSurface(surf); }

    return hash;
}

/* ------------------------------------------------------------------ */
/* ASCII tile helpers                                                  */
/* ------------------------------------------------------------------ */

/// Generate ASCII tile ID from symbol and color pair.
/// Used by tile_type_search (lookup) and ensure_ascii_tileset (tileset).
inline std::string get_ascii_tile_id(const uint32_t sym, const int FG, const int BG) {
    return std::string(
        {'A', 'S', 'C', 'I', 'I', '_', static_cast<char>(sym), static_cast<char>(FG),
         static_cast<char>(BG)});
}

/* ------------------------------------------------------------------ */
/* Drawing helpers                                                     */
/* ------------------------------------------------------------------ */

/// Hover-outline colour by attitude (reads the live F4 knobs). Self = the avatar.
/// Used by draw_creature (layers).
inline SDL_Color outline_color_for(Attitude att, bool is_self) {
    const float* c = g_outline_col_neutral;
    if (is_self) {
        c = g_outline_col_self;
    } else {
        switch (att) {
            case Attitude::A_HOSTILE:
                c = g_outline_col_hostile;
                break;
            case Attitude::A_FRIENDLY:
                c = g_outline_col_friendly;
                break;
            case Attitude::A_NEUTRAL:
            default:
                c = g_outline_col_neutral;
                break;
        }
    }
    return SDL_Color{static_cast<Uint8>(c[0] * 255.0f), static_cast<Uint8>(c[1] * 255.0f),
                     static_cast<Uint8>(c[2] * 255.0f), 255};
}

/// Creates an identity UV surface for the given dimensions.
/// Used by draw_terrain (layers) and tileset_loader (tileset).
inline SDL_Surface_Ptr create_identity_uv_surface(int w, int h, bool offset_mode) {
    SDL_Surface_Ptr surf = create_surface_32(w, h);
    if (!surf) { return nullptr; }

    if (SDL_MUSTLOCK(surf.get())) { SDL_LockSurface(surf.get()); }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Uint8 r, g;
            if (offset_mode) {
                r = 127;
                g = 127;
            } else {
                r = static_cast<Uint8>(w > 1 ? x * 255 / (w - 1) : 0);
                g = static_cast<Uint8>(h > 1 ? 255 - y * 255 / (h - 1) : 255);
            }
            set_pixel_rgba(surf.get(), x, y, r, g, 0, 255);
        }
    }

    if (SDL_MUSTLOCK(surf.get())) { SDL_UnlockSurface(surf.get()); }

    return surf;
}

/// Chains a UV modifier onto composite at a specific position.
/// Used by draw_terrain (layers) and tileset_loader (tileset).
inline void chain_uv_modifier_at(
    SDL_Surface* composite, SDL_Surface* modifier, bool offset_mode, int dst_x, int dst_y) {
    if (!composite || !modifier) { return; }

    if (SDL_MUSTLOCK(composite)) { SDL_LockSurface(composite); }
    if (SDL_MUSTLOCK(modifier)) { SDL_LockSurface(modifier); }

    const int start_x = std::max(0, dst_x);
    const int start_y = std::max(0, dst_y);
    const int end_x = std::min(composite->w, dst_x + modifier->w);
    const int end_y = std::min(composite->h, dst_y + modifier->h);

    if (start_x >= end_x || start_y >= end_y) {
        if (SDL_MUSTLOCK(composite)) { SDL_UnlockSurface(composite); }
        if (SDL_MUSTLOCK(modifier)) { SDL_UnlockSurface(modifier); }
        return;
    }

    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            const int src_x = x - dst_x;
            const int src_y = y - dst_y;

            Uint8 comp_r, comp_g, comp_b, comp_a;
            get_pixel_rgba(composite, x, y, comp_r, comp_g, comp_b, comp_a);

            Uint8 mod_r, mod_g, mod_b, mod_a;
            get_pixel_rgba(modifier, src_x, src_y, mod_r, mod_g, mod_b, mod_a);

            Uint8 new_r, new_g;
            if (offset_mode) {
                int nr = static_cast<int>(comp_r) + (static_cast<int>(mod_r) - 127);
                int ng = static_cast<int>(comp_g) + (static_cast<int>(mod_g) - 127);
                new_r = static_cast<Uint8>(std::clamp(nr, 0, 255));
                new_g = static_cast<Uint8>(std::clamp(ng, 0, 255));
            } else {
                int sample_x = modifier->w > 1 ? (mod_r * (modifier->w - 1) + 127) / 255 : 0;
                int sample_y =
                    modifier->h > 1 ? ((255 - mod_g) * (modifier->h - 1) + 127) / 255 : 0;
                sample_x = std::clamp(sample_x, 0, composite->w - 1);
                sample_y = std::clamp(sample_y, 0, composite->h - 1);

                Uint8 sampled_r, sampled_g, sampled_b, sampled_a;
                get_pixel_rgba(
                    composite, sample_x, sample_y, sampled_r, sampled_g, sampled_b, sampled_a);
                new_r = sampled_r;
                new_g = sampled_g;
            }
            set_pixel_rgba(composite, x, y, new_r, new_g, 0, 255);
        }
    }

    if (SDL_MUSTLOCK(composite)) { SDL_UnlockSurface(composite); }
    if (SDL_MUSTLOCK(modifier)) { SDL_UnlockSurface(modifier); }
}

/* ------------------------------------------------------------------ */
/* Tileset config                                                      */
/* ------------------------------------------------------------------ */

/// Read tileset configuration file to extract JSON and tileset paths.
/// Used by tileset_loader::load (tileset).
inline void get_tile_information(
    const std::string& config_path, std::string& json_path, std::string& tileset_path) {
    const std::string default_json = PATH_INFO::defaulttilejson();
    const std::string default_tileset = PATH_INFO::defaulttilepng();

    const auto reader = [&](std::istream& fin) {
        while (!fin.eof()) {
            std::string sOption;
            fin >> sOption;

            if (sOption.empty()) {
                std::getline(fin, sOption);
            } else if (sOption[0] == '#') {
                std::getline(fin, sOption);
            } else if (sOption.find("JSON") != std::string::npos) {
                fin >> json_path;
            } else if (sOption.find("TILESET") != std::string::npos) {
                fin >> tileset_path;
            } else {
                std::getline(fin, sOption);
            }
        }
    };

    if (!read_from_file(config_path, reader)) {
        json_path = default_json;
        tileset_path = default_tileset;
    }

    if (json_path.empty()) {
        json_path = default_json;
    }
    if (tileset_path.empty()) {
        tileset_path = default_tileset;
    }
}

} // namespace cata_tiles_internal
