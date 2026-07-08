#include "rmlui_proc_texture.h"

#include "fstream_utils.h"
#include "json.h"
#include "path_info.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <random>
#include <string>
#include <vector>

namespace lighting {
namespace {
struct rgba {
    std::uint8_t r, g, b, a;
};

// Live, debug-tunable config (declared in rmlui_proc_texture.h). Lazily loaded
// from disk on first access so in-game frames adopt the saved look without any
// explicit init call.
runic_params g_runic;
bool g_runic_loaded = false;

// The lit rune-ink colour, built from the live config (alpha always opaque).
inline rgba light_col(const runic_params& c) {
    return rgba{static_cast<std::uint8_t>(c.col_r), static_cast<std::uint8_t>(c.col_g),
                static_cast<std::uint8_t>(c.col_b), 255};
}

inline void put(std::vector<std::uint8_t>& px, int W, int H, int x, int y, rgba c) {
    if (x < 0 || y < 0 || x >= W || y >= H) { return; }
    const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 4;
    px[i + 0] = c.r;
    px[i + 1] = c.g;
    px[i + 2] = c.b;
    px[i + 3] = c.a;
}

// --- Corrosion -------------------------------------------------------------
// A cheap integer hash (3 ints -> uint32) and bilinearly-interpolated value
// noise, both seeded so a given panel corrodes identically across launches and
// stays cache-correct. corrode_keep() decides whether a pixel survives: it eats
// blobby patches (low-freq noise) plus a little fine grit, both gated by an
// envelope that peaks at the rim (rim_dist 0) and fades to nothing by
// corrode_reach px inward, so band glyphs sitting deeper in stay intact.
inline std::uint32_t corr_hash(int x, int y, unsigned seed) {
    std::uint32_t h = seed * 2166136261u;
    h = (h ^ static_cast<std::uint32_t>(x)) * 16777619u;
    h = (h ^ static_cast<std::uint32_t>(y)) * 16777619u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}
inline double corr_hash01(int gx, int gy, unsigned seed) {
    return (corr_hash(gx, gy, seed) & 0xffffffu) / static_cast<double>(0x1000000);
}
inline double corr_vnoise(int x, int y, int grid, unsigned seed) {
    const int G = grid < 1 ? 1 : grid;
    const int gx = (x >= 0 ? x : x - G + 1) / G; // floor division
    const int gy = (y >= 0 ? y : y - G + 1) / G;
    double fx = (x - gx * G) / static_cast<double>(G);
    double fy = (y - gy * G) / static_cast<double>(G);
    fx = fx * fx * (3.0 - 2.0 * fx); // smoothstep
    fy = fy * fy * (3.0 - 2.0 * fy);
    const double a = corr_hash01(gx, gy, seed);
    const double b = corr_hash01(gx + 1, gy, seed);
    const double c = corr_hash01(gx, gy + 1, seed);
    const double d = corr_hash01(gx + 1, gy + 1, seed);
    const double top = a + (b - a) * fx;
    const double bot = c + (d - c) * fx;
    return top + (bot - top) * fy;
}
// true = keep the pixel; false = corroded away. `rim_dist` is the pixel's
// distance (px) from the nearest frame edge it should rot from (0 at the wall).
inline bool corrode_keep(int x, int y, int rim_dist, unsigned seed) {
    const runic_params& cfg = runic_cfg();
    if (cfg.corrode_pct <= 0) { return true; }
    const double reach = cfg.corrode_reach < 1 ? 1.0 : cfg.corrode_reach;
    const double env = 1.0 - std::min(1.0, rim_dist / reach); // 1 at rim -> 0
    if (env <= 0.0) { return true; }
    const double thr = (cfg.corrode_pct / 100.0) * env;
    if (corr_vnoise(x, y, cfg.corrode_grid, seed) < thr) {
        return false; // eaten blob patch
    }
    if (rim_dist <= 1 && cfg.corrode_grit > 0
        && (corr_hash(x, y, seed ^ 0x9e3779b9u) % 100u) < static_cast<unsigned>(cfg.corrode_grit)) {
        return false; // fine rim grit
    }
    return true;
}

// A band strip in orientation-agnostic coordinates: `along` runs down the edge,
// `depth` is the perpendicular distance from the outer edge. Horizontal edges
// map (along,depth)->(x,y); vertical edges map ->(x=depth, y=along).
struct strip {
    std::vector<std::uint8_t>* px;
    int w;
    int h;
    bool horizontal;
    unsigned seed = 0; // per-edge corrosion seed
    int along_len() const { return horizontal ? w : h; }
    void plot(int along, int depth, rgba c) const {
        const int tx = horizontal ? along : depth;
        const int ty = horizontal ? depth : along;
        // rim_dist: 0 at either band wall (where the edge reads cleanest), rising
        // toward the band centre, so corrosion eats the walls and spares glyphs.
        const runic_params& cfg = runic_cfg();
        const int rim = std::max(0, std::min(depth - cfg.div_top, cfg.div_bot - depth));
        if (!corrode_keep(tx, ty, rim, seed)) { return; }
        put(*px, w, h, tx, ty, c);
    }
};

// Corner piece: a distinct ornament rather than bare brackets. The rune band's
// outer wall (DIV_TOP) and inner wall (DIV_BOT) each bend 90° (vertex at the
// OUTER corner, arms running inward along both edges), the band is CLOSED by a
// 45° miter joining the two wall vertices, a solid gusset block anchors the
// extreme corner, and a nested-square medallion sits at the band centre where
// the two edges' centre rules converge. Base = top-left; the decorator mirrors/
// rotates it into the other three corners, so the ornament is symmetric.
void draw_corner_rules(std::vector<std::uint8_t>& px, int W, int H) {
    const runic_params& cfg = runic_cfg();
    const rgba LIGHT = light_col(cfg);
    const int RING = cfg.ring, DIV_TOP = cfg.div_top, DIV_BOT = cfg.div_bot;
    const int c = (DIV_TOP + DIV_BOT) / 2; // band centre

    // Corner corrosion rots from the extreme outer corner inward (rim = min(x,y)),
    // so the gusset/miter near (0,0) wear while the medallion at the centre stays
    // whole. Fixed seed: the corner texture is shared/mirrored across all four.
    auto cput = [&](int x, int y) {
        if (corrode_keep(x, y, std::min(x, y), 0xC0FFEEu)) { put(px, W, H, x, y, LIGHT); }
    };

    auto fill = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) { cput(x, y); }
        }
    };
    auto outline = [&](int x0, int y0, int x1, int y1) {
        for (int x = x0; x <= x1; ++x) {
            cput(x, y0);
            cput(x, y1);
        }
        for (int y = y0; y <= y1; ++y) {
            cput(x0, y);
            cput(x1, y);
        }
    };

    // Inner band wall (DIV_BOT) turning the corner; arms run inward to RING so
    // they abut the edge band. (The outer DIV_TOP L was removed by request.)
    for (int i = DIV_BOT; i < RING; ++i) {
        cput(i, DIV_BOT);
        cput(DIV_BOT, i);
    }
    // 45° miter closing the band channel at the corner.
    for (int d = DIV_TOP; d <= DIV_BOT; ++d) { cput(d, d); }
    // Solid gusset anchoring the extreme outer corner (outside the band).
    if (DIV_TOP > 0) { fill(0, 0, DIV_TOP - 1, DIV_TOP - 1); }
    // Nested-square medallion at the band centre (where the edge centre rules
    // meet). Sized to sit inside the band; skipped if the band is too shallow.
    const int r2 = std::min(3, (DIV_BOT - DIV_TOP) / 2);
    if (r2 >= 1) {
        outline(c - r2, c - r2, c + r2, c + r2);
        fill(c - 1, c - 1, c + 1, c + 1);
    }
}

// Close-button glyph: a bold X ENCASED in a square box, transparent elsewhere.
// This is the whole top-right corner piece (it replaces the corner ornament, so
// nothing else sits beneath it). Symmetric, so no orientation flip is needed.
// `invert` is the hover state: the box fills solid ink and the X is knocked out
// (transparent), flipping foreground/background so the button reads as active.
void draw_close_x(std::vector<std::uint8_t>& px, int W, int H, bool invert) {
    const runic_params& cfg = runic_cfg();
    const rgba LIGHT = light_col(cfg);
    const rgba CLEAR{0, 0, 0, 0};
    const int RING = cfg.ring;
    const int b0 = 1; // encasement box, 1px in from the corner edges
    const int b1 = RING - 2;
    if (b1 - b0 < 6) { return; }
    const int p = b0 + 3; // X extent, inset from the box walls
    const int q = b1 - 3;
    const int sz = q - p;
    // The X marks, drawn in `col` at the given thickness on both diagonals.
    auto xmark = [&](rgba col, int th) {
        for (int i = 0; i <= sz; ++i) {
            for (int k = 0; k < th; ++k) {
                put(px, W, H, p + i + k, p + i, col); // ╲
                put(px, W, H, p + i + k, q - i, col); // ╱
            }
        }
    };
    if (invert) {
        // Hover: solid ink box with the X carved back out (transparent).
        for (int y = b0; y <= b1; ++y) {
            for (int x = b0; x <= b1; ++x) { put(px, W, H, x, y, LIGHT); }
        }
        xmark(CLEAR, 3);
    } else {
        // Idle: 2px box walls + 2px X.
        for (int x = b0; x <= b1; ++x) {
            put(px, W, H, x, b0, LIGHT);
            put(px, W, H, x, b0 + 1, LIGHT);
            put(px, W, H, x, b1, LIGHT);
            put(px, W, H, x, b1 - 1, LIGHT);
        }
        for (int y = b0; y <= b1; ++y) {
            put(px, W, H, b0, y, LIGHT);
            put(px, W, H, b0 + 1, y, LIGHT);
            put(px, W, H, b1, y, LIGHT);
            put(px, W, H, b1 - 1, y, LIGHT);
        }
        xmark(LIGHT, 2);
    }
}

// Symmetrical 5x5 glyph (mirrored on X), each lit cell a GLYPH_SCALE block.
void glyph(const strip& s, int a0, int d0, std::mt19937& gen) {
    const runic_params& cfg = runic_cfg();
    const rgba LIGHT = light_col(cfg);
    const int GLYPH_SCALE = cfg.glyph_scale;
    // fill_pct% lit → sparse, rune-like marks rather than solid noise blocks.
    std::uniform_int_distribution<int> bit(0, 99);
    int grid[5][3];
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 3; ++x) { grid[y][x] = (bit(gen) < cfg.fill_pct) ? 1 : 0; }
    }
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            const int sxg = (x < 3) ? x : 4 - x;
            if (grid[y][sxg] == 1) {
                for (int dy = 0; dy < GLYPH_SCALE; ++dy) {
                    for (int dx = 0; dx < GLYPH_SCALE; ++dx) {
                        s.plot(a0 + x * GLYPH_SCALE + dx, d0 + y * GLYPH_SCALE + dy, LIGHT);
                    }
                }
            }
        }
    }
}

// A vertical line `w` px wide (along the band depth), starting at `along`.
void vline(const strip& s, int along, int w, int d0, int d1, rgba c) {
    for (int dx = 0; dx < w; ++dx) {
        for (int d = d0; d <= d1; ++d) { s.plot(along + dx, d, c); }
    }
}

// A horizontal line at depth `d`, from `a0` to `a1` inclusive.
void hline(const strip& s, int a0, int a1, int d, rgba c) {
    for (int a = a0; a <= a1; ++a) { s.plot(a, d, c); }
}

// Edge composition (panel-relative, NOT tiled): the edge is drawn at the panel's
// length and laid out symmetrically about its centre — plain rule lines fill the
// span, with compact rune groups stamped at fixed fractions. One of three
// templates (chosen by the panel seed) decides where the groups sit. Top/bottom
// edges use a DOUBLE rule, side edges a SINGLE rule.

// Glyph count for box `i` of an `n`-box group: a symmetric palindrome that swells
// toward the centre (e.g. n=3 -> 1,2,1; n=5 -> 1,2,3,2,1), capped at 3.
int box_glyphs(int i, int n) {
    const int d = std::min(i, n - 1 - i);
    return std::min(1 + d, 3);
}

int box_width(int g) {
    const runic_params& cfg = runic_cfg();
    const int WALL = cfg.wall, PAD = cfg.pad, GGAP = cfg.ggap;
    const int GW = 5 * cfg.glyph_scale;
    return 2 * WALL + 2 * PAD + g * GW + (g - 1) * GGAP;
}

int group_width(int nel) {
    const runic_params& cfg = runic_cfg();
    const int DSLOT = cfg.gapi + cfg.divw + cfg.gapi;
    int w = 0;
    for (int i = 0; i < nel; ++i) { w += box_width(box_glyphs(i, nel)); }
    return w + (nel - 1) * DSLOT;
}

// A compact rune group: `nel` encased boxes, single dividers between them.
void draw_group(const strip& s, int a0, int nel, std::mt19937& gen) {
    const runic_params& cfg = runic_cfg();
    const rgba LIGHT = light_col(cfg);
    const int WALL = cfg.wall, PAD = cfg.pad, GGAP = cfg.ggap, GAPI = cfg.gapi;
    const int DIVW = cfg.divw, DIV_TOP = cfg.div_top, DIV_BOT = cfg.div_bot;
    const int BAND_TOP = cfg.band_top;
    const int GW = 5 * cfg.glyph_scale;
    const int DSLOT = cfg.gapi + cfg.divw + cfg.gapi;
    int a = a0;
    for (int i = 0; i < nel; ++i) {
        const int g = box_glyphs(i, nel);
        const int bw = box_width(g);
        vline(s, a, WALL, DIV_TOP, DIV_BOT, LIGHT);
        vline(s, a + bw - WALL, WALL, DIV_TOP, DIV_BOT, LIGHT);
        hline(s, a, a + bw - 1, DIV_TOP, LIGHT);
        hline(s, a, a + bw - 1, DIV_BOT, LIGHT);
        int gx = a + WALL + PAD;
        for (int k = 0; k < g; ++k) {
            glyph(s, gx, BAND_TOP, gen);
            gx += GW + GGAP;
        }
        a += bw;
        if (i < nel - 1) {
            vline(s, a + GAPI, DIVW, DIV_TOP, DIV_BOT, LIGHT);
            a += DSLOT;
        }
    }
}

// A connecting rule between groups, centred on the band. Double = a tight `=`
// (two lines 2px apart) for horizontal edges; single = one centre line for
// vertical edges.
// `taper_lo`/`taper_hi` mark an end as a FREE terminus (the outermost rule of a
// non-fullspan edge): instead of stopping flat it trails off into two dots
// marching toward the corner — small gap then a wider one, so it reads as
// tapering out. Interior ends (butting groups) and fullspan ends (tucked under
// the corner) stay flat.
void draw_rule(
    const strip& s, int a0, int a1, bool dbl, bool taper_lo = false, bool taper_hi = false) {
    if (a1 < a0) { return; }
    const runic_params& cfg = runic_cfg();
    const rgba LIGHT = light_col(cfg);
    const int DIV_TOP = cfg.div_top, DIV_BOT = cfg.div_bot;
    const int c = (DIV_TOP + DIV_BOT) / 2; // band centre
    if (dbl) {
        hline(s, a0, a1, c - 1, LIGHT);
        hline(s, a0, a1, c + 1, LIGHT);
    } else {
        hline(s, a0, a1, c, LIGHT);
    }
    if (cfg.taper_dots <= 0) { return; }
    const int n = s.along_len();
    const int RING = cfg.ring;
    const int g1 = std::max(1, cfg.taper_gap); // first (small) gap
    const int g2 = g1 * 2 + 1;                 // second (wide) gap -> trails out
    auto trail = [&](int from, int dir) {
        const int lim_lo = RING, lim_hi = n - 1 - RING;
        auto dot = [&](int start, int len) {
            for (int k = 0; k < len; ++k) {
                const int a = start + dir * k;
                if (a >= lim_lo && a <= lim_hi) { s.plot(a, c, LIGHT); }
            }
        };
        if (dbl) {
            s.plot(from, c, LIGHT); // pinch the two lines to a centre point
        }
        int p = from + dir * (g1 + 1); // after first gap
        dot(p, 2);                     // first dot (2px)
        p += dir * (2 + g2);           // past the dot + second gap
        dot(p, 1);                     // second dot (1px, fades)
    };
    if (taper_lo) { trail(a0, -1); }
    if (taper_hi) { trail(a1, +1); }
}

// Lay out one edge symmetrically: rune groups at template-defined fractions of
// the usable span (inside the corner margins), joined by rule lines.
void draw_edge(const strip& s, unsigned seed, bool dbl, int tmpl) {
    const int n = s.along_len();
    std::mt19937 gen(seed);

    const runic_params& cfg = runic_cfg();
    const int RING = cfg.ring;
    const int RGAP = cfg.rgap; // gap between a rule and a group
    const int edge_lo = RING;  // keep groups clear of the corners
    const int edge_hi = n - 1 - RING;
    const int full = edge_hi - edge_lo;
    if (full <= 0) { return; }
    // border_frac (0..100): the decoration (groups + connecting rules) covers
    // this fraction of the usable span, CENTRED, so the edge no longer stretches
    // a thin rule the whole way into the corners. Only at 100% do the rules tuck
    // under the corners (0..n-1); below that they stop inside the active span and
    // the corner-side remainder stays empty.
    const int bf = std::max(0, std::min(100, cfg.border_frac));
    const int usable = std::max(1, full * bf / 100);
    const int lo = edge_lo + (full - usable) / 2; // active-span start
    const int hi = lo + usable;                   // active-span end
    const bool fullspan = bf >= 100;

    // One of three layout templates. `tmpl` (0/1/2) forces a specific one; -1
    // falls back to the panel seed. Groups sit at fractions of the usable span;
    // rule lines fill the rest. Centre/thirds are deliberately sparse (fixed
    // group count, so they thin out on big panels); the fixed-interval template
    // scales its group COUNT to the span length, so it stays dense at any size.
    const int which = (tmpl >= 0 && tmpl <= 2) ? tmpl : static_cast<int>(seed % 3);
    struct pos {
        double frac;
        int nel;
    };
    std::vector<pos> ps;
    switch (which) {
        case 0: // centred: a single group at the middle
            ps = {{0.5, 4}};
            break;
        case 1: // thirds: groups at 1/3, centre, 2/3
            ps = {{1.0 / 3, 2}, {0.5, 3}, {2.0 / 3, 2}};
            break;
        default: { // fixed interval: a group every ~PITCH px, run centred in span
            const int PITCH = cfg.pitch;
            const int count = std::max(1, usable / PITCH);
            for (int i = 0; i < count; ++i) {
                ps.push_back({(i + 0.5) / count, (i % 2 == 0) ? 2 : 3});
            }
            break;
        }
    }

    // Declutter small panels: a full rune layout looks busy on a short edge. When
    // this edge is "small", thin it out — sides (vertical) drop their runes
    // entirely; top/bottom (horizontal) keep a single centred group.
    if (n < cfg.rune_small_px) {
        if (s.horizontal) {
            ps = {{0.5, 3}};
        } else {
            ps.clear();
        }
    }

    std::vector<std::pair<int, int>> spans; // {a0, a1} of each placed group
    int last_end = lo - 1;
    for (const pos& p : ps) {
        const int gw = group_width(p.nel);
        if (gw <= 0 || gw > usable) { continue; }
        const int cx = lo + static_cast<int>(p.frac * usable);
        int a0 = cx - gw / 2;
        a0 = std::max(a0, lo);
        a0 = std::min(a0, hi - gw + 1);
        if (a0 <= last_end + RGAP) { // would collide with previous group
            continue;
        }
        draw_group(s, a0, p.nel, gen);
        spans.emplace_back(a0, a0 + gw - 1);
        last_end = a0 + gw - 1;
    }

    // Rule lines fill the gaps within the active span. At full span they run to
    // 0 / n-1 so they tuck under the corners; otherwise they stop at the active
    // span edges, leaving the corner-side remainder empty.
    const int rule_lo = fullspan ? 0 : lo;
    const int rule_hi = fullspan ? n - 1 : hi;
    // Only the outermost free ends of a non-fullspan edge taper into dots.
    const bool taper = !fullspan;
    int prev = rule_lo;
    bool first = true;
    for (const std::pair<int, int>& sp : spans) {
        draw_rule(s, prev, sp.first - 1 - RGAP, dbl, taper && first, false);
        prev = sp.second + 1 + RGAP;
        first = false;
    }
    // Final rule's hi end is a free terminus; its lo end taper only if it is also
    // the first rule (no groups were placed, so it spans the whole edge).
    draw_rule(s, prev, rule_hi, dbl, taper && first, taper);
}

std::uint32_t fnv1a(const std::string& s) {
    std::uint32_t hsh = 2166136261u;
    for (const char ch : s) {
        hsh ^= static_cast<std::uint8_t>(ch);
        hsh *= 16777619u;
    }
    return hsh;
}

// A solid line of arbitrary angle, `thick` px wide, written straight into the
// pixel buffer (the band `strip` helpers are axis-aligned only; bindrune staves
// run diagonally). Marches the segment in ~1px steps stamping a filled square so
// the stroke stays connected at any angle — hard-edged, matching the frame art.
void draw_stroke(
    std::vector<std::uint8_t>& px, int W, int H, float x0, float y0, float x1, float y1, int thick,
    rgba col) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    const int steps = std::max(1, static_cast<int>(std::ceil(len)));
    const int half = std::max(0, thick / 2);
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        const int cx = static_cast<int>(std::lround(x0 + dx * t));
        const int cy = static_cast<int>(std::lround(y0 + dy * t));
        for (int yy = -half; yy <= half; ++yy) {
            for (int xx = -half; xx <= half; ++xx) { put(px, W, H, cx + xx, cy + yy, col); }
        }
    }
}

// A self-framed bindrune sigil in a square `S`x`S` buffer: a central vertical
// stave (Isa) with a seeded subset of Elder Futhark strokes merged onto it
// (Algiz protective fork, Gebo binding-cross, Dagaz/Othala foot accents, an
// optional side branch), enclosed by a diamond frame with corner accents. The
// stroke set + jitter are chosen from `seed`, so each character name yields a
// distinct-but-coherent protective sigil. `ink` is the caller-supplied colour
// (the consumer passes the UI text colour so the sigil matches its label).
void draw_bindrune(std::vector<std::uint8_t>& px, int S, unsigned seed, rgba ink) {
    const rgba LIGHT = ink;
    const float cx = S * 0.5f;
    const float cy = S * 0.5f;
    const int thick = std::max(2, S / 32);
    const float r = S * 0.40f;
    std::mt19937 gen(seed);
    std::uniform_int_distribution<int> bit(0, 99);

    // Enclosing diamond frame (the "self-frame").
    const float top = cy - r, bot = cy + r, lft = cx - r, rgt = cx + r;
    draw_stroke(px, S, S, cx, top, rgt, cy, thick, LIGHT);
    draw_stroke(px, S, S, rgt, cy, cx, bot, thick, LIGHT);
    draw_stroke(px, S, S, cx, bot, lft, cy, thick, LIGHT);
    draw_stroke(px, S, S, lft, cy, cx, top, thick, LIGHT);
    // Corner accents: short crossbars just inside the top/bottom vertices.
    const float acc = r * 0.16f;
    draw_stroke(px, S, S, cx - acc, top + acc, cx + acc, top + acc, thick, LIGHT);
    draw_stroke(px, S, S, cx - acc, bot - acc, cx + acc, bot - acc, thick, LIGHT);

    // Central stave (Isa) — always present.
    const float sv = r * 0.72f;
    draw_stroke(px, S, S, cx, cy - sv, cx, cy + sv, thick, LIGHT);
    // Algiz: protective upper fork.
    if (bit(gen) < 85) {
        draw_stroke(px, S, S, cx, cy - sv * 0.45f, cx - r * 0.34f, cy - sv * 0.95f, thick, LIGHT);
        draw_stroke(px, S, S, cx, cy - sv * 0.45f, cx + r * 0.34f, cy - sv * 0.95f, thick, LIGHT);
    }
    // Gebo: binding X-cross through the centre.
    if (bit(gen) < 70) {
        draw_stroke(px, S, S, cx - r * 0.32f, cy - r * 0.32f, cx + r * 0.32f, cy + r * 0.32f, thick,
                    LIGHT);
        draw_stroke(px, S, S, cx + r * 0.32f, cy - r * 0.32f, cx - r * 0.32f, cy + r * 0.32f, thick,
                    LIGHT);
    }
    // Dagaz/Othala: lower foot branches.
    if (bit(gen) < 60) {
        draw_stroke(px, S, S, cx, cy + sv * 0.5f, cx - r * 0.30f, cy + sv * 0.92f, thick, LIGHT);
        draw_stroke(px, S, S, cx, cy + sv * 0.5f, cx + r * 0.30f, cy + sv * 0.92f, thick, LIGHT);
    }
    // Optional side branch for extra variety (mirrored by seed).
    if (bit(gen) < 50) {
        const float dir = (gen() & 1u) ? 1.0f : -1.0f;
        draw_stroke(px, S, S, cx, cy - sv * 0.10f, cx + dir * r * 0.30f, cy - sv * 0.45f, thick,
                    LIGHT);
    }
}
} // namespace

runic_params& runic_cfg() {
    if (!g_runic_loaded) {
        g_runic_loaded = true; // set first so load_runic_cfg() won't recurse
        load_runic_cfg();
    }
    return g_runic;
}

void save_runic_cfg() {
    const std::string path = PATH_INFO::config_dir() + "runic_frame.json";
    const runic_params& c = g_runic;
    write_to_file(
        path,
        [&](std::ostream& o) {
            JsonOut j(o, true);
            j.start_object();
            j.member("col_r", c.col_r);
            j.member("col_g", c.col_g);
            j.member("col_b", c.col_b);
            j.member("ring", c.ring);
            j.member("glyph_scale", c.glyph_scale);
            j.member("band_top", c.band_top);
            j.member("div_top", c.div_top);
            j.member("div_bot", c.div_bot);
            j.member("wall", c.wall);
            j.member("divw", c.divw);
            j.member("pad", c.pad);
            j.member("ggap", c.ggap);
            j.member("gapi", c.gapi);
            j.member("rgap", c.rgap);
            j.member("pitch", c.pitch);
            j.member("border_frac", c.border_frac);
            j.member("unit", c.unit);
            j.member("fill_pct", c.fill_pct);
            j.member("frame_inset", c.frame_inset);
            j.member("corrode_pct", c.corrode_pct);
            j.member("corrode_grid", c.corrode_grid);
            j.member("corrode_reach", c.corrode_reach);
            j.member("corrode_grit", c.corrode_grit);
            j.member("taper_dots", c.taper_dots);
            j.member("taper_gap", c.taper_gap);
            j.member("rune_small_px", c.rune_small_px);
            j.member("force_template", c.force_template);
            j.member("use_fixed_seed", c.use_fixed_seed);
            // stored as int (bit-preserving round-trip via static_cast on load)
            j.member("seed", static_cast<int>(c.seed));
            j.end_object();
        },
        "runic frame config");
}

void load_runic_cfg() {
    const std::string path = PATH_INFO::config_dir() + "runic_frame.json";
    runic_params& c = g_runic;
    // optional=true → silent when the file does not yet exist (first launch).
    read_from_file_json(
        path,
        [&](JsonIn& in) {
            JsonObject jo = in.get_object();
            jo.allow_omitted_members();
            c.col_r = jo.get_int("col_r", c.col_r);
            c.col_g = jo.get_int("col_g", c.col_g);
            c.col_b = jo.get_int("col_b", c.col_b);
            c.ring = jo.get_int("ring", c.ring);
            c.glyph_scale = jo.get_int("glyph_scale", c.glyph_scale);
            c.band_top = jo.get_int("band_top", c.band_top);
            c.div_top = jo.get_int("div_top", c.div_top);
            c.div_bot = jo.get_int("div_bot", c.div_bot);
            c.wall = jo.get_int("wall", c.wall);
            c.divw = jo.get_int("divw", c.divw);
            c.pad = jo.get_int("pad", c.pad);
            c.ggap = jo.get_int("ggap", c.ggap);
            c.gapi = jo.get_int("gapi", c.gapi);
            c.rgap = jo.get_int("rgap", c.rgap);
            c.pitch = jo.get_int("pitch", c.pitch);
            c.border_frac = jo.get_int("border_frac", c.border_frac);
            c.unit = jo.get_int("unit", c.unit);
            c.fill_pct = jo.get_int("fill_pct", c.fill_pct);
            c.frame_inset = jo.get_int("frame_inset", c.frame_inset);
            c.corrode_pct = jo.get_int("corrode_pct", c.corrode_pct);
            c.corrode_grid = jo.get_int("corrode_grid", c.corrode_grid);
            c.corrode_reach = jo.get_int("corrode_reach", c.corrode_reach);
            c.corrode_grit = jo.get_int("corrode_grit", c.corrode_grit);
            c.taper_dots = jo.get_int("taper_dots", c.taper_dots);
            c.taper_gap = jo.get_int("taper_gap", c.taper_gap);
            c.rune_small_px = jo.get_int("rune_small_px", c.rune_small_px);
            c.force_template = jo.get_int("force_template", c.force_template);
            c.use_fixed_seed = jo.get_bool("use_fixed_seed", c.use_fixed_seed);
            c.seed = static_cast<unsigned>(jo.get_int("seed", static_cast<int>(c.seed)));
        },
        true);
}

std::vector<std::uint8_t> gen_runic_frame(const std::string& variant, int& out_w, int& out_h) {
    // Variants: "runic-corner" (fixed 20x20 nested-square anchor); edges are
    // panel-relative, "runic-hedge:<len>:<seed>" (horizontal) and
    // "runic-vedge:<len>:<seed>" (vertical). <len> is the panel border-box length
    // in px; <seed> selects the symmetric layout template and the rune glyphs, so
    // a given panel size is deterministic across launches. apply_crt mirrors the
    // base regions into all four corners and both edge pairs.
    const runic_params& cfg = runic_cfg();
    const int RING = cfg.ring;
    const int UNIT = cfg.unit;

    auto alloc = [&](int w, int h) {
        out_w = w;
        out_h = h;
        return std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4, 0);
    };

    // rfind(...,0)==0 (prefix match) so the cache-bust suffix "runic-corner:G<n>"
    // still routes here; the corner art ignores any trailing params.
    if (variant.rfind("runic-corner", 0) == 0) {
        std::vector<std::uint8_t> px = alloc(RING, RING);
        draw_corner_rules(px, out_w, out_h);
        return px;
    }

    // Close-button piece (top-right corner): an encased X at corner size. The
    // "-inv" variant is the hover state (solid box, X knocked out). Check it
    // first — it shares the "runic-x" prefix. Both ignore trailing ":G<n>".
    if (variant.rfind("runic-x-inv", 0) == 0) {
        std::vector<std::uint8_t> px = alloc(RING, RING);
        draw_close_x(px, out_w, out_h, /*invert=*/true);
        return px;
    }
    if (variant.rfind("runic-x", 0) == 0) {
        std::vector<std::uint8_t> px = alloc(RING, RING);
        draw_close_x(px, out_w, out_h, /*invert=*/false);
        return px;
    }

    // Parse "<prefix>:<len>:<seed>" (both optional). Defaults: len=UNIT, seed from
    // the variant hash (so the bare "runic-hedge"/"runic-vedge" still work).
    // Format: "<prefix>:<len>:<seed>:<tmpl>" (len/seed/tmpl all optional). tmpl
    // (0/1/2) forces a layout template; -1 leaves it seed-driven.
    auto parse_edge = [&](const std::string& prefix, int& len, unsigned& seed, int& tmpl) -> bool {
        if (variant.rfind(prefix, 0) != 0) { return false; }
        len = UNIT;
        seed = fnv1a(variant);
        tmpl = -1;
        const std::size_t c1 = variant.find(':');
        if (c1 != std::string::npos) {
            const std::size_t c2 = variant.find(':', c1 + 1);
            try {
                len = std::stoi(variant.substr(c1 + 1, c2 - c1 - 1));
                if (c2 != std::string::npos) {
                    // stoul reads the leading number, stopping at any ":<tmpl>".
                    seed = static_cast<unsigned>(std::stoul(variant.substr(c2 + 1)));
                    const std::size_t c3 = variant.find(':', c2 + 1);
                    if (c3 != std::string::npos) { tmpl = std::stoi(variant.substr(c3 + 1)); }
                }
            } catch (...) {
                // Malformed params: fall back to the defaults.
                len = UNIT;
                seed = fnv1a(variant);
                tmpl = -1;
            }
        }
        len = std::max(len, 2 * RING + 1); // must fit the corner margins
        return true;
    };

    int len = UNIT;
    unsigned seed = 0;
    int tmpl = -1;
    if (parse_edge("runic-hedge", len, seed, tmpl)) {
        std::vector<std::uint8_t> px = alloc(len, RING);
        const strip s{&px, out_w, out_h, true, seed};
        draw_edge(s, seed, /*dbl=*/true, tmpl); // horizontal: double rule
        return px;
    }
    if (parse_edge("runic-vedge", len, seed, tmpl)) {
        std::vector<std::uint8_t> px = alloc(RING, len);
        const strip s{&px, out_w, out_h, false, seed};
        draw_edge(s, seed, /*dbl=*/false, tmpl); // vertical: single rule
        return px;
    }

    // Self-framed bindrune save sigil: "bindrune:<size>:<seed>:<rrggbb>" (size,
    // seed, colour all optional). <size> = square texture px (default 96); <seed>
    // = the per-character hash so each save file gets a unique sigil; <rrggbb> =
    // ink colour (default = the frame ink). A trailing ":G<n>" cache-bust is
    // ignored (std::stoul reads only the leading number of each field).
    if (variant.rfind("bindrune", 0) == 0) {
        int sz = 96;
        unsigned bseed = fnv1a(variant);
        rgba ink = light_col(cfg);
        const std::size_t c1 = variant.find(':');
        if (c1 != std::string::npos) {
            const std::size_t c2 = variant.find(':', c1 + 1);
            try {
                sz = std::stoi(variant.substr(c1 + 1, c2 - c1 - 1));
                if (c2 != std::string::npos) {
                    bseed = static_cast<unsigned>(std::stoul(variant.substr(c2 + 1)));
                    const std::size_t c3 = variant.find(':', c2 + 1);
                    if (c3 != std::string::npos) {
                        const unsigned long rgb = std::stoul(variant.substr(c3 + 1), nullptr, 16);
                        ink = rgba{static_cast<std::uint8_t>((rgb >> 16) & 0xffu),
                                   static_cast<std::uint8_t>((rgb >> 8) & 0xffu),
                                   static_cast<std::uint8_t>(rgb & 0xffu), 255};
                    }
                }
            } catch (...) {
                sz = 96;
                bseed = fnv1a(variant);
                ink = light_col(cfg);
            }
        }
        sz = std::max(16, sz);
        std::vector<std::uint8_t> px = alloc(sz, sz);
        draw_bindrune(px, sz, bseed, ink);
        return px;
    }

    // Debug marker bars (F12 panel-centre cross): "dbg-v:<h>" = 3xh, "dbg-h:<w>"
    // = wx3, solid magenta. Length parsed after the colon; default 64.
    const bool dvert = variant.rfind("dbg-v", 0) == 0;
    if (dvert || variant.rfind("dbg-h", 0) == 0) {
        int n = 64;
        const std::size_t c = variant.find(':');
        if (c != std::string::npos) {
            try {
                n = std::max(1, std::stoi(variant.substr(c + 1)));
            } catch (...) { n = 64; }
        }
        std::vector<std::uint8_t> px = dvert ? alloc(3, n) : alloc(n, 3);
        for (int i = 0; i < out_w * out_h; ++i) {
            px[i * 4 + 0] = 255;
            px[i * 4 + 1] = 0;
            px[i * 4 + 2] = 255;
            px[i * 4 + 3] = 255;
        }
        return px;
    }

    // Unknown variant: a 1x1 transparent texture (harmless).
    return alloc(1, 1);
}
} // namespace lighting
