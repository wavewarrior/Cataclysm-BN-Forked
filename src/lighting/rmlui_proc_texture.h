#pragma once
#ifndef CATA_SRC_LIGHTING_RMLUI_PROC_TEXTURE_H
#define CATA_SRC_LIGHTING_RMLUI_PROC_TEXTURE_H

#include <cstdint>
#include <string>
#include <vector>

namespace lighting {
// Live, debug-tunable parameters for the runic frame generator. Every field that
// was once a hardcoded constant in rmlui_proc_texture.cpp now lives here, so the
// F4 "Runic Frame" tab can drive the look at runtime and persist it. The struct
// is read by gen_runic_frame() (draw params) and by apply_crt() (control params).
struct runic_params {
    // band colour (the lit rune ink); alpha is always 255
    int col_r = 161, col_g = 136, col_b = 95; // #a1885f
    // geometry (all px)
    int ring = 20;         // corner size & edge thickness
    int glyph_scale = 2;   // rune cell block size (glyph grid is fixed 5x3)
    int band_top = 4;      // depth where glyph motifs start
    int div_top = 3;       // band outer wall depth
    int div_bot = 14;      // band inner wall depth
    int wall = 1;          // box wall thickness
    int divw = 2;          // intra-group divider thickness
    int pad = 3;           // padding glyph<->wall inside a box
    int ggap = 2;          // gap between glyphs within a box
    int gapi = 1;          // gap glyph-box <-> divider inside a group
    int rgap = 3;          // gap between a rule line and a group
    int pitch = 220;       // fixed-interval template: group spacing
    int border_frac = 100; // % of usable span the edge decoration covers (centred)
    int unit = 256;        // default edge length when unspecified
    int fill_pct = 38;     // glyph cell lit threshold (0..99): higher = denser
    int frame_inset = 12;  // whole-frame inward shift on every side (F9/F10)
    // corrosion: knocks pixels out near the band walls / outer corner so the
    // frame edges read as worn rather than crisp. Weighted to the rim so glyphs
    // stay legible. corrode_pct=0 disables it entirely.
    int corrode_pct = 28;  // blob-hole density at the rim (0..99); 0 = off
    int corrode_grid = 5;  // value-noise cell size px (bigger = larger pits)
    int corrode_reach = 2; // px inward from each wall the corrosion fades over
    int corrode_grit = 10; // fine 1px speckle right at the rim (0..99)
    // taper: edge rule lines trail off into two dots toward the corner instead
    // of stopping flat. First gap is small, second ~2x → "tapering out" feel.
    int taper_dots = 1; // 0 = flat line ends; 1 = trail into 2 dots
    int taper_gap = 2;  // px gap before the first trailing dot (gap2 ~ 2x)
    // declutter small panels: an edge shorter than this (raw px) is "small" and
    // thins out — sides drop their runes, top/bottom keep one centred group.
    int rune_small_px = 170;
    // control (consumed by apply_crt, not by the pixel draw)
    int force_template = -1;     // -1 = Auto (per-edge); 0/1/2 forces all edges
    bool use_fixed_seed = false; // false = per-panel-size seed (shipping)
    unsigned seed = 12345u;      // used when use_fixed_seed
    unsigned regen = 0u;         // bump to bust RmlUi's by-string texture cache
};

// Shared live config. Non-const so the debug UI edits it in place. First call
// lazily loads the saved config from disk (once).
runic_params& runic_cfg();

// Persist / restore runic_cfg() to config/runic_frame.json.
void save_runic_cfg();
void load_runic_cfg();

// Procedurally generate an ornate "runic" 9-patch frame as RGBA8 pixels
// (row-major). Pixels are either fully opaque or fully transparent, so the
// buffer is already correct for the render interface's premultiplied-alpha
// upload (no colour fringing on the transparent center). `variant`
// deterministically seeds the pattern via an FNV-1a hash, so the same string
// always yields the same border across launches. Out params receive the
// texture dimensions.
std::vector<std::uint8_t> gen_runic_frame(const std::string& variant, int& out_w, int& out_h);
} // namespace lighting

#endif // CATA_SRC_LIGHTING_RMLUI_PROC_TEXTURE_H
