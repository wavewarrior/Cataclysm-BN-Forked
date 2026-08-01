#pragma once
#ifndef CATA_SRC_HUD_PHOSPHOR_H
#define CATA_SRC_HUD_PHOSPHOR_H

#include <array>
#include <string>
#include <string_view>
#include <vector>

/// Terminal-phosphor sidebar HUD: geometry and text primitives.
///
/// The HUD is drawn as a character-cell terminal. Every region is a whole number
/// of cells wide and tall, every frame stroke is a real box-drawing glyph, and
/// every colour is one rung of a single-hue luminance ladder. Two consequences
/// worth stating up front, because they are the reason this header exists:
///
///  1. **The grid must tile the viewport exactly.** The HUD it replaces laid
///     regions out in percentages while the terminal itself was sized in cells,
///     so `TERMY * FONT_HEIGHT = 67 * 16 = 1072 != 1080` and the hotbar rendered
///     6.34 dp off the bottom of the screen with a 6.36 dp overlap onto the bar
///     above it. Here the cell size is derived from the context dimensions first
///     and every rect is snapped to it, so a region can never straddle a cell.
///
///  2. **Hierarchy is luminance, not hue.** Nothing in the HUD may introduce a
///     second hue. Desaturating the whole document must leave every distinction
///     intact — that property is what makes this register immune to protanopia,
///     deuteranopia, tritanopia and a washed-out panel alike, and it is only
///     preserved if callers go through `ink` instead of writing colours.
namespace hud_phosphor
{

// ── Palette ─────────────────────────────────────────────────────────────────

/// The luminance ladder. These seven values are the ONLY colours the phosphor
/// HUD may emit; there is deliberately no eighth. Resolved through the `ph-*`
/// tokens in `data/gui/theme.json`, so the F4 Theme tab still edits them live.
///
/// Adjacent rungs are separated by 35-53 8-bit greyscale levels, three to five
/// times the ~10-level just-noticeable difference, so no pair can collapse.
enum class ink : int {
    ground,   ///< `ph-0` panel ground
    dead,     ///< `ph-1` present but unavailable (a disabled key's number)
    rule,     ///< `ph-2` box frames, bar troughs, oldest log rows
    label,    ///< `ph-3` chrome words, and limbs at FULL health (intact recedes)
    datum,    ///< `ph-4` live data, and limbs that are DAMAGED (damage advances)
    peak,     ///< `ph-5` critical, newest, wielded, the player glyph
    inverse,  ///< `ph-k` glyphs drawn *inside* an inverted cell
};

/// `#rrggbbaa` for a ladder rung. Falls back to the built-in ladder if the theme
/// is missing the token, so a truncated theme.json degrades to correct colours
/// rather than to magenta.
auto hex( ink i ) -> std::string;

/// The same rung as `hex`, as straight RGBA in 0..1 — for the GPU dot layer,
/// which cannot consume a CSS string. Falls back to the built-in ladder exactly
/// as `hex` does.
auto rgba( ink i ) -> std::array<float, 4>;

// ── Geometry ────────────────────────────────────────────────────────────────

/// The cell grid a given context resolves to.
///
/// `cell_w` is `0.6 * font_size` exactly, because that is Source Code Pro's
/// advance width — which is why this register can promise zero horizontal drift
/// across a full row where a Consolas mock-up drifts 0.19 dp per 192 cells.
struct metrics {
    float cell_w = 0.0f;    ///< dp
    float cell_h = 0.0f;    ///< dp; also the pinned line-height, so box stems abut
    float font_size = 0.0f; ///< dp
    int cols = 0;
    int rows = 0;
};

/// Choose a cell size for a context measured in dp.
///
/// Picks the largest cell from a legibility-ordered candidate set that still
/// yields the design's authored **192-column** grid, and only when no candidate
/// can reach 192 falls back to the largest that clears the narrow-window
/// minimum. The target is a column count and not a cell size because every
/// internal grid the producers emit — SOMA 34, DOCK 35, LOG 92, KEYS 192 — is
/// quoted against 192 columns; the cell then falls out of it, and at 1920 dp
/// that is exactly the mockup's 10 x 20 dp at font-size 16.667 dp.
///
/// Never returns a zero cell: a degenerate context clamps to the smallest
/// candidate so callers can lay out unconditionally.
auto metrics_for( float ctx_w_dp, float ctx_h_dp ) -> metrics;

/// A region in cells. Half-open: covers columns `[col, col + cols)`.
struct cell_rect {
    int col = 0;
    int row = 0;
    int cols = 0;
    int rows = 0;

    auto operator<=>( const cell_rect & ) const = default; // *NOPAD*
};

/// A region in dp, ready for `Element::SetProperty`.
struct rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

/// Where every region sits, in cells.
///
/// `status` and `keys` span the full width and are the two OPAQUE regions; the
/// rest are translucent and float over the terrain. The five gameplay regions
/// are pairwise disjoint, so occlusion is their sum with no union arithmetic.
struct layout {
    metrics m;
    cell_rect status;   ///< full width, top edge: world and character state
    cell_rect soma;     ///< left column: body parts, pools, effects
    cell_rect radar;    ///< right column, top: the dot-matrix tactical minimap
    cell_rect dock;     ///< right column under the radar: target, arms
    cell_rect log;      ///< bottom-left, sized to its content, not to the column
    cell_rect keys;     ///< full width, bottom edge: the function-key row
    cell_rect vehicle;  ///< right column under the dock; empty unless driving
};

/// Options for `layout_for`. `sidebar_right` mirrors `soma`/`dock`/`vehicle`
/// horizontally so the dock stays on the player's configured sidebar edge.
struct layout_options {
    metrics m;
    bool sidebar_right = true;
    int log_lines = 6;       ///< the log well is sized to this, and only this
    bool show_vehicle = false;
};

auto layout_for( const layout_options &o ) -> layout;

/// Cells to dp. The returned rect is exact: no rounding, because the cell size
/// was chosen so the grid tiles the context.
auto to_dp( const metrics &m, const cell_rect &c ) -> rect;

// ── Cell-grid text ──────────────────────────────────────────────────────────

/// Display width of UTF-8 `text` in cells. Box-drawing and block-element glyphs
/// are single-width; this is not a general wcwidth, it is the subset the HUD
/// emits, and it exists so `pad` cannot be fooled by a multi-byte frame glyph.
/// U+00A0 NO-BREAK SPACE counts as one cell, so this agrees with `pad`'s fill.
auto display_width( std::string_view utf8 ) -> int;

/// Pad with U+00A0 NO-BREAK SPACE, or truncate, to exactly `cols` display cells.
///
/// Truncation is why this is a function and not a format spec: cutting UTF-8 by
/// bytes would emit a partial code point, and cutting by code points would still
/// let a producer silently overrun its region — which is the class of bug that
/// put `[Unbound globally!]` (1554 dp of text in a 1520 dp box) on screen.
///
/// The fill is U+00A0 and must stay one; the rationale is on the definition.
auto pad( std::string_view utf8, int cols ) -> std::string;

/// Right-align within `cols`, truncating from the left if needed. Same fill.
auto pad_left( std::string_view utf8, int cols ) -> std::string;

// ── Frames ──────────────────────────────────────────────────────────────────

/// A title that interrupts a rule, DOS-style: `──┤ SOMA ├──`. Costs zero rows,
/// which is the whole reason section headers are affordable in this register.
struct rule_title {
    int col = 0;          ///< cell where the interrupting `┤` goes
    std::string text;     ///< already translated; will be upper-cased and spaced
};

/// A horizontal rule spanning `cols` cells.
///
/// `left`/`right` are the junction glyphs for the ends — pass the member that is
/// actually correct for the corner (`┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼`), or empty for a rule
/// that runs to the region edge. `crossings` places `┼`/`┬`/`┴` where an
/// internal vertical meets this rule.
struct rule_options {
    int cols = 0;
    std::vector<rule_title> titles;
    std::vector<int> crossings;
    std::string left;
    std::string right;
    std::string crossing_glyph = "\u252c"; ///< `┬` by default
};

auto rule( const rule_options &o ) -> std::string;

// ── Bars ────────────────────────────────────────────────────────────────────

/// A horizontal bar `cells` wide, drawn in block-element glyphs.
///
/// Fill is U+2584 LOWER HALF BLOCK, quantised to whole cells. The half-height
/// glyph is the point: at 50% cell ink a bar is about 10.5 px tall in a 20 px
/// cell, so a stack of them reads as separate bars with dark gaps between,
/// where a column of full blocks merges into one solid slab. The trough is
/// U+2591 LIGHT SHADE at `ink::rule`, so the unlit remainder stays dotted.
///
/// There is no sub-cell precision, and there cannot be: the partial blocks
/// divide a cell horizontally and this one divides it vertically. Quantisation
/// is +-1/2 cell; the exact figure is printed beside every bar, so the number
/// carries the truth and the bar carries the gestalt.
///
/// `intact_recedes` implements the register's severity rule: a part at full
/// health draws in `ink::label` and a damaged one in `ink::datum`, so the eye is
/// drawn to damage rather than to a wall of identical healthy bars.
struct bar_options {
    int cur = 0;
    int max = 0;
    int cells = 15;
    bool intact_recedes = true;
};

auto bar( const bar_options &o ) -> std::string;

// ── Emphasis ────────────────────────────────────────────────────────────────

/// Wrap already-escaped `content` in a span at ladder rung `i`.
auto tint( ink i, std::string_view content ) -> std::string;

/// Reverse video: `ink::inverse` glyphs on an `ink::peak` ground.
///
/// This is the register's only "shout", and it is deliberately not a colour. A
/// critical limb does not get a hotter hue — it gets its entire row inverted,
/// which survives greyscale, survives a colourblind player, and survives a
/// player who has turned saturation off.
auto invert( std::string_view content ) -> std::string;

// ── Severity ────────────────────────────────────────────────────────────────

/// Is this body part critical?
///
/// Replaces the shipping predicate, which was `o.cur * 100 / o.max < 25` in
/// INTEGER arithmetic (`panels.cpp`). A left arm at 8/30 truncates to 26, so a
/// limb at 26.7% health that was both bleeding and bitten never once rendered as
/// critical — and `hud_anim::is_critical` missed identically at 0.267 vs 0.25.
/// Here the ratio is float, the threshold is a third rather than a quarter, and
/// an actively bleeding or bitten limb is critical at any health, because those
/// are the states that kill you between turns.
///
/// `broken` SUPPRESSES criticality rather than contributing to it: a broken limb
/// is not critical on its own, because inversion is the register's only shout and
/// a fracture is a long-term condition the player cannot act on this turn —
/// mending gets its own non-inverted row instead. It has to be an explicit rule,
/// not an omission: `is_limb_broken` means hp == 0, so a broken limb always trips
/// the ratio test, and simply not consulting the flag left every broken limb
/// permanently inverted. A broken limb that is ALSO bleeding or bitten is still
/// critical, since those tick damage between turns.
struct crit_options {
    int cur = 0;
    int max = 0;
    bool bleeding = false;
    bool bitten = false;
    bool broken = false;
};

auto is_critical( const crit_options &o ) -> bool;

} // namespace hud_phosphor

#endif // CATA_SRC_HUD_PHOSPHOR_H
