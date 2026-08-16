#pragma once
#ifndef CATA_SRC_HUD_RUNIC_H
#define CATA_SRC_HUD_RUNIC_H

#include <array>
#include <string>
#include <string_view>

/// Sidebar HUD in the character creator's register: ink ladder, dp geometry,
/// markup primitives.
///
/// The HUD it replaced was a character-cell terminal — one typeface at one size,
/// a single amber hue, and every frame stroke a real box-drawing glyph. This one
/// speaks the creator's language instead: flex boxes, gruvbox neutrals with a
/// gold accent, dotted `.nc-rule` separators, pip meters and tracked-caps heads,
/// all reusing the device classes in `data/gui/newchar_common.rcss` rather than
/// restating them. Source Code Pro stays as the typeface; the cell grid does not.
///
/// Two properties survive the change verbatim, because they are the reasons the
/// HUD works rather than decisions about how it looks:
///
///  1. **The regions tile the viewport exactly and never overlap.** `keys` is
///     anchored to `ctx_h` itself rather than to a row count, so the residual
///     sliver the cell grid had to round away is gone. `sidebar_hud_top_rows()`
///     carves the terrain viewport out of these numbers, so a stale one leaves a
///     strip painting over live map.
///
///  2. **Hierarchy is luminance, not hue.** Every distinction a producer makes is
///     a step on the `ink` ladder; every gold mark on screen comes from the
///     stylesheet and is redundant with a shape (a left edge, a filled pip, a
///     filled chip). Desaturating the document must leave every reading intact.
namespace hud_runic
{

// ── Palette ─────────────────────────────────────────────────────────────────

/// The luminance ladder. These six values are the ONLY colours a producer may
/// emit, and there is deliberately no gold rung: gold is the stylesheet's, spent
/// on panel heads, the crit cursor edge, filled pips and the alarm chip, never on
/// a value a producer chose. Resolved through the gruvbox tokens in
/// `data/gui/theme.json`, so the F4 Theme tab still edits them live.
enum class ink : int {
    ground,  ///< `bg-hard`, luma 31.5: panel ground, radar ground
    dead,    ///< `bg2`,     luma 74.2: present but unavailable (unbound key, empty ammo)
    rule,    ///< `bg4`,     luma 113.0: separators, `::`, oldest log rows
    label,   ///< `fg4`,     luma 154.6: chrome words, limbs at FULL health
    datum,   ///< `fg`,      luma 219.5: live data, limbs that are DAMAGED
    peak,    ///< `fg0`,     luma 240.2: newest log line, wielded weapon, the clock
};

/// `#rrggbbaa` for a ladder rung. Falls back to the built-in ladder if the theme
/// is missing the token, so a truncated theme.json degrades to correct colours
/// rather than to `substitute_tokens`'s magenta.
auto hex( ink i ) -> std::string;

/// The same rung as `hex`, as straight RGBA in 0..1 — for the GPU dot layer,
/// which cannot consume a CSS string. Falls back identically.
auto rgba( ink i ) -> std::array<float, 4>;

/// The RCSS rung class for `i`, for the rows that need it ALONGSIDE a layout
/// class (`<span class="hud-cell-name hud-i4">`), which `tint` cannot produce.
/// Promoted here on its second adopter: a producer-local copy of the class
/// table in each TU is exactly the drift the single ladder exists to prevent.
auto ink_class( ink i ) -> std::string_view;

/// Wrap already-escaped `content` in a span at ladder rung `i`.
auto tint( ink i, std::string_view content ) -> std::string;

// ── Geometry ────────────────────────────────────────────────────────────────

inline constexpr float row_h    = 20.0f;  ///< dp, one HUD list row
inline constexpr float head_h   =
    22.0f;  ///< dp, one .nc-colhead (3+3 padding, 12dp caps, 1dp rule)
inline constexpr float chrome_h = 8.0f;   ///< dp, a panel's 1dp border pair + .hud-body padding

/// A region in dp, ready for `Element::SetProperty`.
struct rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    auto operator<=>( const rect & ) const = default; // *NOPAD*
};

/// Where every region sits, in dp.
///
/// `status` and `keys` span the full width and are the two OPAQUE strips; the
/// rest are translucent and float over the terrain. All seven are pairwise
/// disjoint, so occlusion is their sum with no union arithmetic.
struct layout {
    float ctx_w = 0.0f;
    float ctx_h = 0.0f;
    rect status;   ///< full width, top edge: world and character state
    rect soma;     ///< left column: body parts, pools, effects
    rect radar;    ///< right column, top: the dot-matrix tactical minimap
    rect dock;     ///< right column under the radar: target, arms
    rect log;      ///< bottom-left, sized to its content, not to the column
    rect keys;     ///< full width, bottom edge: the function-key strip
    rect vehicle;  ///< right column under the dock; empty unless driving
};

/// Options for `layout_for`. `sidebar_right` mirrors `soma`/`radar`/`dock`/
/// `vehicle` horizontally so the dock stays on the player's configured edge.
struct layout_options {
    float ctx_w_dp = 0.0f;
    float ctx_h_dp = 0.0f;
    bool sidebar_right = true;
    int log_lines = 6;        ///< the log well is sized to this, and only this
    bool show_vehicle = false;
    /// Is SOMA's limb card expanded? Drives only that panel's HEIGHT — the
    /// producer reads the same flag for its content. Collapsing has to reach the
    /// layout, or the card still reserves its full column while showing three
    /// rows and the player has reclaimed nothing.
    bool soma_expanded = false;
};

auto layout_for( const layout_options &o ) -> layout;

/// dp height of a panel holding exactly `rows` list rows under a head.
constexpr auto panel_h( int rows ) -> float
{
    return head_h + static_cast<float>( rows ) * row_h + chrome_h;
}

// ── Markup primitives ───────────────────────────────────────────────────────
//
// These are the only way a producer builds markup. Every one returns a
// complete, escaped RML fragment; every caller-supplied string goes through
// `rml_escape` inside the helper.
//
// There is no `no_break` / U+00A0 conversion and there must not be. Under flex
// layout gaps come from `margin`/`padding`, never from padding spaces, so the
// parse-time whitespace trim that forced the old `pad` to fill with NO-BREAK
// SPACE can no longer corrupt a row. A producer reaching for a literal
// alignment space is a bug — `newcharprofession.rcss:201` records the same rule
// for the creator.

/// `<div class="{classes}"[ id="{id}"]>{inner}</div>`. Empty `id` → no attribute.
/// `classes` and `id` are emitted as authored; `inner` is already markup.
auto row( std::string_view classes, std::string_view id, std::string_view inner ) -> std::string;

/// `<div class="nc-rule"></div>` — the creator's dotted separator.
auto rule_div() -> std::string;

/// A `.nc-rule` followed by `<div class="hud-row hud-subhead"><span>{title}</span></div>`.
auto subhead( std::string_view title ) -> std::string;

struct pip_options {
    int cur = 0;
    int max = 0;
    int count = 12;
};

/// `count` `<div class="nc-pip">` elements, the first `round( count * cur / max )`
/// carrying ` on`. `max <= 0` → every pip off. The lit count is clamped to
/// `[0, count]`, so a value over its maximum fills the meter rather than
/// overflowing the row.
auto pips( const pip_options &o ) -> std::string;

struct fact_options {
    std::string label;   ///< emitted as-is; RCSS upper-cases it
    std::string value;
    std::string sub;     ///< omitted entirely when empty
    ink value_ink = ink::datum;
};

/// The creator's fact block: quiet tracked-caps label over a bright value, with
/// an optional subordinate line. An empty `.sub` emits no element at all — an
/// empty one would still take vertical space in a fixed-height panel.
auto fact( const fact_options &o ) -> std::string;

struct legend_options {
    std::string label;
    std::string value;
    ink label_ink = ink::label;
    ink value_ink = ink::datum;
    bool alarm = false;                   ///< value takes "hud-alarm" instead of a rung class
    std::string label_class = "hud-lab";  ///< extra class on the label span; "" for none
    /// Extra class on the value span. Defaults to `.hud-text`, which is only a
    /// `display: block` + `flex: 0 0 auto` — the minimum a span needs to be a
    /// real flex item, since RmlUi does NOT blockify flex children and an inline
    /// one silently ignores every size property put on it. Never pass "" unless
    /// the replacement class declares its own `display`.
    std::string value_class = "hud-text";
};

/// One `LABEL :: value` pair from the creator's sigil strip.
///
/// An empty `label` omits both the label span and the separator, which is how a
/// bare value — the season, the clock, the place name — joins a STATUS group
/// without inventing a caption for it.
///
/// The default `label_class` is the STATUS strip's: `.hud-lab` shrinks a caption
/// to the `.nc-fact-label` recipe (11dp, 2dp tracking, caps). The KEYS strip
/// passes `.label_class = "hud-key"` and `.value_class = "nc-legend-label"`
/// instead, because there the label position holds a key glyph and the value
/// position holds the action name — content, not a caption, and it must not
/// shrink.
auto legend_item( const legend_options &o ) -> std::string;

/// `<span class="nc-chip"><span class="nc-chip-label hud-iN">text</span></span>`.
auto chip( std::string_view text, ink i ) -> std::string;

/// One row of the creator's vertical tally list. An empty `val` omits its span.
auto tally_row( std::string_view name, std::string_view val, ink name_ink, ink val_ink )
-> std::string;

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
/// is not critical on its own, because the crit treatment is the register's only
/// shout and a fracture is a long-term condition the player cannot act on this
/// turn — mending gets its own ordinary row instead. It has to be an explicit
/// rule, not an omission: `is_limb_broken` means hp == 0, so a broken limb always
/// trips the ratio test, and simply not consulting the flag left every broken
/// limb permanently shouting. A broken limb that is ALSO bleeding or bitten is
/// still critical, since those tick damage between turns.
struct crit_options {
    int cur = 0;
    int max = 0;
    bool bleeding = false;
    bool bitten = false;
    bool broken = false;
};

auto is_critical( const crit_options &o ) -> bool;

} // namespace hud_runic

#endif // CATA_SRC_HUD_RUNIC_H
