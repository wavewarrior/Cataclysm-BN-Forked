#include "cata_tiles.h"
#include "cata_tiles_internal.h"

#include "calendar.h"
#include "catacharset.h"
#include "cursesport.h"
#include "string_utils.h"
#include "field_type.h"
#include "omdata.h"
#include "veh_type.h"
#include "item.h"
#include "item_factory.h"
#include "map.h"
#include "monster.h"
#include "type_id.h"
#include "player.h"
#include "mod_tileset.h"
#include "trap.h"
#include "vehicle.h"
#include "vehicle_part.h"
#include "weather_type.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace cata_tiles_internal;

std::optional<tile_search_result> cata_tiles::tile_type_search(const tile_search_params& tile) {
    auto [id, category, subcategory, subtile, rota] = tile;
    std::optional<tile_lookup_res> res = find_tile_looks_like(id, category);
    const tile_type* tt = nullptr;
    if (res) { tt = &(res->tile()); }
    const std::string& found_id = res ? (res->id()) : id;

    if (!tt) {
        uint32_t sym = UNKNOWN_UNICODE;
        nc_color col = c_white;
        if (category == C_FURNITURE) {
            const furn_str_id fid(found_id);
            if (fid.is_valid()) {
                const furn_t& f = fid.obj();
                sym = f.symbol();
                col = f.color();
            }
        } else if (category == C_TERRAIN) {
            const ter_str_id tid(found_id);
            if (tid.is_valid()) {
                const ter_t& t = tid.obj();
                sym = t.symbol();
                col = t.color();
            }
        } else if (category == C_MONSTER) {
            const mtype_id mid(found_id);
            if (mid.is_valid()) {
                const mtype& mt = mid.obj();
                sym = UTF8_getch(mt.sym);
                col = mt.color;
            }
        } else if (category == C_VEHICLE_PART) {
            const vpart_id vpid(found_id.substr(3));
            if (vpid.is_valid()) {
                const vpart_info& v = vpid.obj();

                if (subtile == open_) {
                    sym = '\'';
                } else if (subtile == broken) {
                    sym = v.sym_broken;
                } else {
                    sym = v.sym;
                }
                subtile = -1;

                tileray face = tileray(units::from_degrees(rota));
                sym = special_symbol(face.dir_symbol(sym));
                rota = 0;

                col = v.color;
            }
        } else if (category == C_FIELD) {
            const field_type_id fid = field_type_id(found_id);
            sym = fid.obj().get_codepoint();
            // TODO: field intensity?
            col = fid.obj().get_color();
        } else if (category == C_TRAP) {
            const trap_str_id tmp(found_id);
            if (tmp.is_valid()) {
                const trap& t = tmp.obj();
                sym = t.sym;
                col = t.color;
            }
        } else if (category == C_ITEM) {
            // TODO!: push this up, it's a bad one
            item* tmp;
            if (found_id.starts_with("corpse_")) {
                tmp = item::spawn_temporary(itype_corpse, calendar::start_of_cataclysm);
            } else {
                tmp = item::spawn_temporary(found_id, calendar::start_of_cataclysm);
            }
            sym = tmp->symbol().empty() ? ' ' : tmp->symbol().front();
            col = tmp->color();
        } else if (category == C_OVERMAP_TERRAIN) {
            const oter_type_str_id tmp(id);
            if (tmp.is_valid()) {
                sym = tmp->symbol;
                col = tmp->color;
            }
        } else if (category == C_OVERMAP_WEATHER) {
            const weather_type_id weather_type_id(id);
            if (weather_type_id.is_valid()) {
                sym = weather_type_id->symbol;
                col = weather_type_id->color;
            }
        } else if (category == C_OVERMAP_NOTE) {
            sym = id[5];
            col = color_from_string(id.substr(7, id.length() - 1));
        } else if (category == C_BULLET) {
            static const auto default_bullet = std::string{"animation_bullet_normal_0deg"};
            auto res = find_tile_with_season(default_bullet);
            if (res) { return tile_search_result{.tt = &res->tile(), .found_id = res->id()}; }
        }
        // Special cases for walls
        switch (sym) {
            case LINE_XOXO:
            case LINE_XOXO_UNICODE:
                sym = LINE_XOXO_C;
                break;
            case LINE_OXOX:
            case LINE_OXOX_UNICODE:
                sym = LINE_OXOX_C;
                break;
            case LINE_XXOO:
            case LINE_XXOO_UNICODE:
                sym = LINE_XXOO_C;
                break;
            case LINE_OXXO:
            case LINE_OXXO_UNICODE:
                sym = LINE_OXXO_C;
                break;
            case LINE_OOXX:
            case LINE_OOXX_UNICODE:
                sym = LINE_OOXX_C;
                break;
            case LINE_XOOX:
            case LINE_XOOX_UNICODE:
                sym = LINE_XOOX_C;
                break;
            case LINE_XXXO:
            case LINE_XXXO_UNICODE:
                sym = LINE_XXXO_C;
                break;
            case LINE_XXOX:
            case LINE_XXOX_UNICODE:
                sym = LINE_XXOX_C;
                break;
            case LINE_XOXX:
            case LINE_XOXX_UNICODE:
                sym = LINE_XOXX_C;
                break;
            case LINE_OXXX:
            case LINE_OXXX_UNICODE:
                sym = LINE_OXXX_C;
                break;
            case LINE_XXXX:
            case LINE_XXXX_UNICODE:
                sym = LINE_XXXX_C;
                break;
            default:
                // sym goes unchanged
                break;
        }

        if (sym != 0 && sym < 256) {
            // see cursesport.cpp, function wattron
            const int pairNumber = col.to_color_pair_index();
            const cata_cursesport::pairs& colorpair = cata_cursesport::colorpairs[pairNumber];
            // What about isBlink?
            const bool isBold = col.is_bold();
            const int FG = colorpair.FG + (isBold ? 8 : 0);
            std::string generic_id = get_ascii_tile_id(sym, FG, -1);

            // do not rotate fallback tiles!
            if (sym != LINE_XOXO_C && sym != LINE_OXOX_C) { rota = 0; }
            if (tileset_ptr->find_tile_type(generic_id)) {
                return tile_type_search({generic_id, C_NONE, subcategory, subtile, rota});
            }
            // Try again without color this time (using default color).
            generic_id = get_ascii_tile_id(sym, -1, -1);
            if (tileset_ptr->find_tile_type(generic_id)) {
                return tile_type_search({generic_id, C_NONE, subcategory, subtile, rota});
            }
        }
    }

    // if id is not found, try to find a tile for the category+subcategory combination
    if (!tt) {
        const std::string& category_id = TILE_CATEGORY_IDS[category];
        if (!category_id.empty() && tileset_ptr->find_tile_type(category_id)) {
            return tile_type_search({category_id, C_NONE, subcategory, subtile, rota});
        }
    }

    if (tt) {
        return tile_search_result{.tt = tt, .found_id = found_id};
    }

    // if nothing else works, try to find a "generic" tile
    for (const std::string& key : multitile_keys) {
        if (!tileset_ptr->find_tile_type(key)) { continue; }
        const tile_search_params generic{key, C_NONE, subcategory, subtile, rota};
        return tile_type_search(generic);
    }

    return std::nullopt;
}

std::optional<tile_lookup_res> cata_tiles::find_tile_with_season(const std::string& id) const {
    const season_type season = season_of_year(calendar::turn);
    return tileset_ptr->find_tile_type_by_season(id, season);
}

template <typename T>
std::optional<tile_lookup_res> cata_tiles::find_tile_looks_like_by_string_id(
    const std::string& id, TILE_CATEGORY category, const int looks_like_jumps_limit) const {
    const string_id<T> s_id(id);
    if (!s_id.is_valid()) { return std::nullopt; }
    const T& obj = s_id.obj();
    return find_tile_looks_like(obj.looks_like, category, looks_like_jumps_limit - 1);
}

auto cata_tiles::find_tile_looks_like(
    const std::string& id, TILE_CATEGORY category, const int looks_like_jumps_limit) const
    -> std::optional<tile_lookup_res> {
    if (id.empty() || looks_like_jumps_limit <= 0) { return std::nullopt; }

    // Note on memory management:
    // This method must returns pointers to the objects (std::string *id  and tile_type * tile)
    // that are valid when this metod returns. Ideally they should have the lifetime
    // that is equal or exceeds lifetime of `this` or `this::tileset_ptr`.
    // For example, `id` argument may have shorter lifetime and thus should not be returned!
    // The result of `find_tile_with_season` is OK to be returned, because it's guaranteed to
    // return pointers to the keys and values that are stored inside the `tileset_ptr`.
    const auto tile_with_season = find_tile_with_season(id);
    if (tile_with_season) { return tile_with_season; }

    switch (category) {
        case C_FURNITURE:
            return find_tile_looks_like_by_string_id<furn_t>(id, category, looks_like_jumps_limit);
        case C_TERRAIN:
            return find_tile_looks_like_by_string_id<ter_t>(id, category, looks_like_jumps_limit);
        case C_TRAP:
            return find_tile_looks_like_by_string_id<trap>(id, category, looks_like_jumps_limit);
        case C_FIELD:
            return find_tile_looks_like_by_string_id<
                field_type>(id, category, looks_like_jumps_limit);
        case C_MONSTER:
            return find_tile_looks_like_by_string_id<mtype>(id, category, looks_like_jumps_limit);
        case C_OVERMAP_TERRAIN: {
            std::optional<tile_lookup_res> ret;
            const oter_type_str_id type_tmp(id);
            if (!type_tmp.is_valid()) { return ret; }

            int jump_limit = looks_like_jumps_limit;
            for (const std::string& looks_like : type_tmp.obj().looks_like) {

                ret = find_tile_looks_like(looks_like, category, jump_limit - 1);
                if (ret.has_value()) { return ret; }

                jump_limit--;
                if (jump_limit <= 0) { return ret; }
            }

            return ret;
        }

        case C_VEHICLE_PART: {
            // vehicle parts start with vp_ for their tiles, but not their IDs
            auto base_id = id.substr(3);
            const vpart_id base_vpid(base_id);
            if (!base_vpid.is_valid()) { // Fixed Fallback
                return find_tile_looks_like(base_id, C_FURNITURE, looks_like_jumps_limit - 1)
                    .or_else([&, this] {
                        return find_tile_looks_like(base_id, C_TERRAIN, looks_like_jumps_limit - 1);
                    });
            }
            return find_tile_looks_like(
                "vp_" + base_vpid.obj().looks_like, category, looks_like_jumps_limit - 1);
        }
        case C_ITEM: {
            itype_id iid = itype_id(id);
            if (!iid.is_valid()) {
                if (id.starts_with("corpse_")) {
                    return find_tile_looks_like(
                        itype_corpse.str(), category, looks_like_jumps_limit - 1);
                }
                return std::nullopt;
            }
            return find_tile_looks_like(iid->looks_like.str(), category, looks_like_jumps_limit - 1);
        }

        case C_BULLET: {
            auto ammo_name = id;
            replace_first(ammo_name, "animation_bullet_", "");
            auto iid = itype_id(ammo_name);
            if (!iid.is_valid()) { return std::nullopt; }
            if (!iid->looks_like.is_empty()) {
                return find_tile_looks_like(
                    "animation_bullet_" + iid->looks_like.str(), category,
                    looks_like_jumps_limit - 1);
            }
            return std::nullopt;
        }

        default:
            return std::nullopt;
    }
}

bool cata_tiles::find_overlay_looks_like(
    const bool male, const std::string& overlay, std::string& draw_id) {
    bool exists = false;

    std::string looks_like;
    std::string over_type;

    if (overlay.starts_with("worn_")) {
        looks_like = overlay.substr(5);
        over_type = "worn_";
    } else if (overlay.starts_with("wielded_")) {
        looks_like = overlay.substr(8);
        over_type = "wielded_";
    } else {
        looks_like = overlay;
    }

    for (int cnt = 0; cnt < 10 && !looks_like.empty(); cnt++) {
        draw_id = (male ? "overlay_male_" : "overlay_female_") + over_type + looks_like;
        if (tileset_ptr->find_tile_type(draw_id)) {
            exists = true;
            break;
        }
        draw_id = "overlay_" + over_type + looks_like;
        if (tileset_ptr->find_tile_type(draw_id)) {
            exists = true;
            break;
        }
        if (looks_like.starts_with("mutation_active_")) {
            looks_like = "mutation_" + looks_like.substr(16);
            continue;
        }
        itype_id iid = itype_id(looks_like);
        if (!iid.is_valid()) { break; }
        looks_like = iid->looks_like.str();
    }
    return exists;
}

void cata_tiles::get_terrain_orientation(
    const tripoint_bub_ms& p, int& rota, int& subtile,
    const std::map<tripoint_bub_ms, ter_id>& ter_override, const bool (&invisible)[5]) {
    map& here = get_map();
    const bool overridden = ter_override.contains(p);
    const auto ter = [&](const tripoint_bub_ms& q, const bool invis) -> ter_id {
        const auto override = ter_override.find(q);
        return override != ter_override.end() ? override->second
             : (!overridden || !invis)
                 ? here.ter(q)
                 : t_null;
    };

    // get terrain at x,y
    const ter_id tid = ter(p, invisible[0]);
    if (tid == t_null) {
        subtile = 0;
        rota = 0;
        return;
    }

    // get terrain neighborhood
    const ter_id neighborhood[4] =
        {ter(p + point_south, invisible[1]), ter(p + point_east, invisible[2]),
         ter(p + point_west, invisible[3]), ter(p + point_north, invisible[4])};

    char val = 0;

    // populate connection information
    for (int i = 0; i < 4; ++i) {
        if (neighborhood[i] == tid) { val += 1 << i; }
    }

    get_rotation_and_subtile(val, rota, subtile);
}

void cata_tiles::get_rotation_and_subtile(const char val, int& rotation, int& subtile) {
    switch (val) {
        // no connections
        case 0:
            subtile = unconnected;
            rotation = 0;
            break;
        // all connections
        case 15:
            subtile = center;
            rotation = 0;
            break;
        // end pieces
        case 8:
            subtile = end_piece;
            rotation = 2;
            break;
        case 4:
            subtile = end_piece;
            rotation = 3;
            break;
        case 2:
            subtile = end_piece;
            rotation = 1;
            break;
        case 1:
            subtile = end_piece;
            rotation = 0;
            break;
        // edges
        case 9:
            subtile = edge;
            rotation = 0;
            break;
        case 6:
            subtile = edge;
            rotation = 1;
            break;
        // corners
        case 12:
            subtile = corner;
            rotation = 2;
            break;
        case 10:
            subtile = corner;
            rotation = 1;
            break;
        case 3:
            subtile = corner;
            rotation = 0;
            break;
        case 5:
            subtile = corner;
            rotation = 3;
            break;
        // all t_connections
        case 14:
            subtile = t_connection;
            rotation = 2;
            break;
        case 11:
            subtile = t_connection;
            rotation = 1;
            break;
        case 7:
            subtile = t_connection;
            rotation = 0;
            break;
        case 13:
            subtile = t_connection;
            rotation = 3;
            break;
    }
}

void cata_tiles::get_connect_values(
    const tripoint_bub_ms& p, int& subtile, int& rotation, const int connect_group,
    const std::map<tripoint_bub_ms, ter_id>& ter_override) {
    uint8_t connections = get_map().get_known_connections(p, connect_group, ter_override);
    get_rotation_and_subtile(connections, rotation, subtile);
}

void cata_tiles::get_furn_connect_values(
    const tripoint_bub_ms& p, int& subtile, int& rotation, const int connect_group,
    const std::map<tripoint_bub_ms, furn_id>& furn_override) {
    uint8_t connections = get_map().get_known_connections_f(p, connect_group, furn_override);
    get_rotation_and_subtile(connections, rotation, subtile);
}

void cata_tiles::get_tile_values(const int t, const int* tn, int& subtile, int& rotation) {
    bool connects[4];
    char val = 0;
    for (int i = 0; i < 4; ++i) {
        connects[i] = (tn[i] == t);
        if (connects[i]) { val += 1 << i; }
    }
    get_rotation_and_subtile(val, rotation, subtile);
}

void cata_tiles::get_tile_values_with_ter(
    const tripoint_bub_ms& p, const int t, const int* tn, int& subtile, int& rotation) {
    get_tile_values(t, tn, subtile, rotation);
    // calculate rotation for unconnected tiles based on surrounding walls
    if (subtile == unconnected) {
        int val = 0;
        bool use_furniture = false;
        map& here = get_map();

        if (here.has_flag("ALIGN_WORKBENCH", p)) {
            for (int i = 0; i < 4; ++i) {
                // align to furniture that has the workbench quality
                const auto& pt = p + four_adjacent_offsets[i];
                if (here.has_furn(pt) && here.furn(pt).obj().workbench) {
                    val += 1 << i;
                    use_furniture = true;
                }
            }
        }
        // if still unaligned, try aligning to walls
        if (val == 0) {
            for (int i = 0; i < 4; ++i) {
                const auto& pt = p + four_adjacent_offsets[i];
                if (here.impassable(pt) && !here.is_transparent(pt)) {
                    val += 1 << i;
                }
            }
        }
        if (val == 0) {
            // if still unaligned, try aligning to fences and rails
            for (int i = 0; i < 4; ++i) {
                const auto& pt = p + four_adjacent_offsets[i];
                const furn_id& furn = here.furn(pt);
                if (furn && (furn.obj().has_flag("FENCE") || furn.obj().has_flag("RAIL"))) {
                    val += 1 << i;
                }
            }
        }
        if (val != 0) {
            get_rotation_and_subtile(val, rotation, subtile);
        }
    }
}
