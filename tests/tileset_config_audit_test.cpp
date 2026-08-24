// Regression guard for cross-sheet tile sprite anchoring.
//
// In this theme a tile entry may reference fg/bg sprites from a DIFFERENT sheet
// than the file section the entry is declared in: fg/bg are global atlas indices.
// The draw offset a layer uses must therefore match the sheet the sprite ACTUALLY
// lives in (the section it is declared in, or an explicit per-layer
// "fg_offset_x"/"fg_offset_y" / "bg_offset_x"/"bg_offset_y" override added in
// plans/fix-tree-sprite-anchoring.md). The stoneshard tree bug was 128x160 canopy
// sprites declared in a 64x80 section with no override: the trees rendered 1.5
// tiles right and 2.5 tiles low, clipped and overpainting their neighbours.

#include "catch/catch_amalgamated.hpp"
#include "filesystem.h"
#include "fstream_utils.h"
#include "json.h"
#include "path_info.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr const char* theme_dir = "MSX++UnDeadPeopleEdition/";

struct xy {
    int x = 0;
    int y = 0;
    bool operator==(const xy& o) const { return x == o.x && y == o.y; }
};

/// Global sprite-index range of one "tiles-new" file section.
struct sheet_span {
    std::string file;
    int first = 0;
    int last = 0; // exclusive
    int cell_w = 32;
    int cell_h = 32;
    int off_x = 0;
    int off_y = 0;
};

/// Cell count of a PNG, read straight from the IHDR header (no image decode).
auto png_cell_count(const std::string& path, int cell_w, int cell_h) -> int {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f);
    std::uint8_t hdr[24] = {};
    f.read(reinterpret_cast<char*>(hdr), 24);
    REQUIRE(f);
    REQUIRE((hdr[1] == 'P' && hdr[2] == 'N' && hdr[3] == 'G'));
    const int w = (int(hdr[16]) << 24) | (int(hdr[17]) << 16) | (int(hdr[18]) << 8) | int(hdr[19]);
    const int h = (int(hdr[20]) << 24) | (int(hdr[21]) << 16) | (int(hdr[22]) << 8) | int(hdr[23]);
    return (w / cell_w) * (h / cell_h);
}

/// Sprite indices inside an "fg"/"bg" array: int, {"sprite": int | [int...]},
/// or a nested array of those.
auto collect_sprite_indices(const JsonArray& arr, std::vector<int>& out) -> void {
    for (size_t i = 0; i < arr.size(); ++i) {
        if (arr.has_int(i)) {
            out.push_back(arr.get_int(i));
        } else if (arr.has_object(i)) {
            const JsonObject o = arr.get_object(i);
            // Loader-owned members such as "weight" are not ours to visit.
            o.allow_omitted_members();
            if (o.has_int("sprite")) {
                out.push_back(o.get_int("sprite"));
            } else if (o.has_array("sprite")) {
                collect_sprite_indices(o.get_array("sprite"), out);
            }
        } else if (arr.has_array(i)) {
            collect_sprite_indices(arr.get_array(i), out);
        }
    }
}

auto collect_layer_indices(const JsonObject& entry, const std::string& key, std::vector<int>& out)
    -> void {
    if (entry.has_int(key)) {
        out.push_back(entry.get_int(key));
    } else if (entry.has_array(key)) {
        collect_sprite_indices(entry.get_array(key), out);
    }
}

auto owner_of(const std::vector<sheet_span>& spans, int idx) -> const sheet_span* {
    for (const auto& s : spans) {
        if (idx >= s.first && idx < s.last) { return &s; }
    }
    return nullptr;
}

struct entry_anchor {
    xy fg_eff;
    xy bg_eff;
    bool has_fg_ov = false;
    bool has_bg_ov = false;
};

} // namespace

TEST_CASE(
    "MSX++UnDeadPeopleEdition cross-sheet tile sprites are anchored to their sheet", "[tileset]") {
    const std::string base = PATH_INFO::gfxdir() + theme_dir;
    const std::string cfg_path = base + "tile_config.json";
    std::ifstream cfg_probe(cfg_path);
    if (!cfg_probe) {
        // The bundled Catch2 has no conditional SKIP(); WARN + return is the
        // house idiom for a missing-asset bail (see tests/coop_net_test.cpp).
        WARN("theme tile_config.json not present in this checkout");
        return;
    }

    std::vector<sheet_span> spans;
    std::map<std::string, entry_anchor> anchors;
    std::vector<std::string> problems;
    int total_entries = 0;
    bool loaded = false;

    read_from_file_json(cfg_path, [&](JsonIn& jsin) {
        loaded = true;
        const JsonObject root = jsin.get_object();
        root.allow_omitted_members();
        REQUIRE(root.has_array("tiles-new"));
        const JsonArray sections = root.get_array("tiles-new");
        // Pass 1: build the full atlas span first. Entries may forward-reference
        // sprites on sheets declared LATER (bullets in normal_items use a bg cell
        // from normal_stuff), so no entry may be checked before every span exists.
        {
            int off = 0;
            for (size_t i = 0; i < sections.size(); ++i) {
                const JsonObject section = sections.get_object(i);
                section.allow_omitted_members();
                const std::string file = section.get_string("file", "");
                REQUIRE_FALSE(file.empty());
                const int cw = section.get_int("sprite_width", 32);
                const int ch = section.get_int("sprite_height", 32);
                const int ox = section.get_int("sprite_offset_x", 0);
                const int oy = section.get_int("sprite_offset_y", 0);
                const int count = png_cell_count(base + file, cw, ch);
                spans.push_back(sheet_span{file, off, off + count, cw, ch, ox, oy});
                off += count;
            }
            CHECK(off > 23000); // stoneshard sheets live at the top of the atlas
        }
        // Pass 2: check every entry's layer anchors against the owning sheet.
        for (size_t i = 0; i < sections.size(); ++i) {
            const sheet_span& self = spans[i];
            const JsonObject section = sections.get_object(i);
            section.allow_omitted_members();
            const int cw = self.cell_w;
            const int ch = self.cell_h;
            const int ox = self.off_x;
            const int oy = self.off_y;
            if (!section.has_array("tiles")) { continue; }
            for (const JsonObject entry : section.get_array("tiles")) {
                entry.allow_omitted_members();
                // "id" is a string, or an array of aliases sharing one sprite
                // (see the loader in cata_tiles_tileset.cpp).
                std::string id = "?";
                if (entry.has_string("id")) {
                    id = entry.get_string("id");
                } else if (entry.has_array("id")) {
                    id = entry.get_array("id").get_string(0);
                }
                ++total_entries;
                entry_anchor a;
                a.has_fg_ov = entry.has_int("fg_offset_x") || entry.has_int("fg_offset_y");
                a.has_bg_ov = entry.has_int("bg_offset_x") || entry.has_int("bg_offset_y");
                a.fg_eff = xy{entry.get_int("fg_offset_x", ox), entry.get_int("fg_offset_y", oy)};
                a.bg_eff = xy{entry.get_int("bg_offset_x", ox), entry.get_int("bg_offset_y", oy)};
                anchors.try_emplace(id, a);

                for (const std::string key : {"fg", "bg"}) {
                    const bool has_ov = key == "fg" ? a.has_fg_ov : a.has_bg_ov;
                    const xy eff = key == "fg" ? a.fg_eff : a.bg_eff;
                    std::vector<int> idxs;
                    collect_layer_indices(entry, key, idxs);
                    for (const int idx : idxs) {
                        // Index 0 is the conventional blank placeholder cell: it is
                        // transparent in every sheet, so its anchor is meaningless.
                        if (idx == 0) { continue; }
                        const sheet_span* own = owner_of(spans, idx);
                        if (own == nullptr) {
                            problems.push_back(
                                "out of range: " + id + " " + key + " " + std::to_string(idx)
                                + " in [" + self.file + "]");
                            continue;
                        }
                        if (xy{own->off_x, own->off_y} != eff) {
                            problems.push_back(
                                "offset mismatch: " + id + " " + key + " " + std::to_string(idx)
                                + ": entry eff (" + std::to_string(eff.x) + ", "
                                + std::to_string(eff.y) + ") vs owner [" + own->file + "] ("
                                + std::to_string(own->off_x) + ", " + std::to_string(own->off_y)
                                + ")");
                        } else if (!has_ov && (own->cell_w != cw || own->cell_h != ch)) {
                            problems.push_back(
                                "cell-size mismatch without override: " + id + " " + key + " "
                                + std::to_string(idx) + ": section " + std::to_string(cw) + "x"
                                + std::to_string(ch) + " vs owner [" + own->file + "] "
                                + std::to_string(own->cell_w) + "x" + std::to_string(own->cell_h));
                        }
                    }
                }
            }
        }
    });
    REQUIRE(loaded);
    CHECK(total_entries > 1000);

    for (const auto& p : problems) { INFO(p); }
    CHECK(problems.empty());

    // Spot-checks: the anchors the stoneshard trees/foliage and the vac ovens need.
    const auto t_tree = anchors.find("t_tree");
    REQUIRE(t_tree != anchors.end());
    CHECK(t_tree->second.fg_eff == xy{-48, -128});
    CHECK(t_tree->second.bg_eff == xy{-16, -48});
    const auto t_shrub = anchors.find("t_shrub");
    REQUIRE(t_shrub != anchors.end());
    CHECK(t_shrub->second.fg_eff == xy{-48, -128});
    CHECK(t_shrub->second.bg_eff == xy{-16, -48});
    const auto vac = anchors.find("vac_oven_small");
    REQUIRE(vac != anchors.end());
    CHECK(vac->second.fg_eff == xy{-16, -48});

    // The stoneshard tree cells (23880-23898) must resolve inside stoneshard_trees.png.
    const sheet_span* trees = owner_of(spans, 23889);
    REQUIRE(trees != nullptr);
    CHECK(trees->file == "stoneshard_trees.png");
    CHECK((trees->cell_w == 128 && trees->cell_h == 160));
    const sheet_span* foliage = owner_of(spans, 19752);
    REQUIRE(foliage != nullptr);
    CHECK(foliage->file == "stoneshard_foliage.png");
    CHECK((foliage->cell_w == 128 && foliage->cell_h == 160));
}
