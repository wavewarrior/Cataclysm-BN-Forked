#pragma once
#ifndef CATA_SRC_LIGHTING_LIGHTING_SETTINGS_H
#define CATA_SRC_LIGHTING_LIGHTING_SETTINGS_H

// One owner for the lighting knobs (issue 116 of the frame-assembly module).
//
// `lighting_settings` is a header-only value that owns lighting knob storage: the
// verbatim `debug_params` cbuffer member plus the knobs this ticket moves. It has
// no SDL and no RmlUi includes, so `cata_test-tiles` constructs one directly
// (ADR-0003's seam: the frame's inputs are assertable without a GPU).
//
// The knob table below replaces two private lists that encoded the same facts
// twice: the 39-name `/tmp/cata_knob` if-chain in `sdl_input.cpp` and the
// `lighting_dbg_range` constants behind the F8/F9 handlers. Channels: the file
// channel stays RAW (it is the measurement escape hatch, so no clamp); keys and
// the panel honour their ranges. Panel limits live in `data/gui/devui.rml`; a
// test compares the table against that markup so the two cannot drift.
//
// Scope of THIS ticket (later tickets move the per-group globals): the
// `debug_params` core, the debug mode (three copies and two env seeds collapse
// into one), the rebuild mode (two bools collapse into one `force_rebuild`), the
// `rc_readback` pulse, and `shadow_steps`' panel proxy.

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include "lighting/debug_params.h"

namespace lighting
{

/// How the structure rebuild is forced. Collapses the old
/// `g_force_rc_rebuild` / `g_rebuild_once` pair; the file channel's
/// `force_rc_rebuild 1` is `every_frame` and `force_rc_rebuild 2` is `once`,
/// exactly as the old two-bool decode did.
enum class force_rebuild_mode : std::uint8_t {
    none,
    every_frame,
    once,
};

/// Take-once knobs. `take()` returns the armed value and disarms, so a pulse can
/// only be consumed once; the frame's commit step takes the force-once and
/// `flush_and_gather_rc` takes the readback, at today's points. Kept as a member
/// of `lighting_settings` so the reader of the gate mode and the clearer of the
/// pulse are the same object: a test that injects its own settings reads and
/// clears the same one.
struct lighting_pulses {
    force_rebuild_mode force_rebuild = force_rebuild_mode::none;
    bool rc_readback = false;

    /// True iff a one-shot structure rebuild was armed; disarms it.
    auto take_force_once() -> bool {
        const bool armed = force_rebuild == force_rebuild_mode::once;
        if( armed ) {
            force_rebuild = force_rebuild_mode::none;
        }
        return armed;
    }
    /// True iff an RC cascade readback was requested; clears the request.
    auto take_rc_readback() -> bool {
        const bool armed = rc_readback;
        rc_readback = false;
        return armed;
    }
};

/// The lighting settings one frame reads. Copyable, no external references.
struct lighting_settings {
    /// Verbatim DebugParams cbuffer member (272 bytes, wire-stable; the
    /// static assertion travels with the struct in `lighting/debug_params.h`).
    debug_params debug;
    /// Take-once knobs (see `lighting_pulses`).
    lighting_pulses pulses;

    /// The debug mode's single home. Used to be stored three times
    /// (`g_current_dbg_mode`, `g_dbg_params.debug_mode`, `g_devui_dbg_mode`)
    /// and seeded twice from `CATA_DBG_MODE`.
    auto debug_mode() const -> std::uint32_t {
        return debug.debug_mode;
    }
    void set_debug_mode( std::uint32_t m ) {
        debug.debug_mode = m;
    }
    /// The frame's diagnostic-view predicate: modes 6 and up take the
    /// full-screen identity quad instead of the lit world.
    auto diagnostic_view_active() const -> bool {
        return debug.debug_mode >= 6u;
    }
};

/// What a knob is, deciding how a channel writes it.
enum class knob_kind : std::uint8_t {
    value,  /// continuous number
    toggle, /// boolean, written as `value > 0.5` by the file channel
    pulse,  /// take-once request
    mode,   /// integer selection (the debug mode)
};

/// Which channels may write a knob. The file channel is the scripted escape
/// hatch; `keys` is F8/F9; `panel` is the F4 dev UI.
enum knob_channel : unsigned {
    knob_file  = 1u << 0,
    knob_keys  = 1u << 1,
    knob_panel = 1u << 2,
};

/// Destinations that are not a plain scalar. `force_rc_rebuild` decodes into the
/// `force_rebuild` mode, `rc_readback` arms a pulse, `crt_world` lives in the
/// RmlUi layer's post-effect (whose header includes SDL, so it cannot appear in
/// this one).
struct knob_special {
    enum which_t {
        force_rc_rebuild,
        rc_readback,
        crt_world,
    } which;
    bool operator==( const knob_special &o ) const {
        return which == o.which;
    }
};
/// Where a knob's value lives. Pointer-to-member forms cover the storage this
/// ticket owns; plain addresses cover globals a later ticket moves (so the table
/// is complete from the start and the drift test covers every old-chain name
/// today); the tags cover the three destinations above.
using knob_dest = std::variant<
    float debug_params::*, std::uint32_t debug_params::*,
    float *, bool *, knob_special >;

/// Optional per-channel range. The panel's limits live in the markup; the keys'
/// limits are the old `lighting_dbg_range` constants. Several knobs have
/// DIFFERENT limits per channel (emitter/sun/sky scale: panel 0..4 step 0.02,
/// keys 0..10 step 0.1), which is why one pair of bounds cannot serve both.
struct knob_range {
    float min;
    float max;
    float step;
};

/// One row of the knob table.
struct knob_entry {
    std::string_view name;
    knob_kind kind;
    unsigned channels;
    knob_dest dest;
    /// Panel limits, cross-checked against `data/gui/devui.rml` by a test.
    /// Absent when the knob has no ranged widget in the markup.
    std::optional<knob_range> panel;
    /// Key limits, cross-checked against the old `lighting_dbg_range`
    /// constants by a test. Absent when F8/F9 do not write the knob.
    std::optional<knob_range> keys;
};

/// The knob table: every name the `/tmp/cata_knob` chain accepted, plus the
/// knobs only the keys or only the panel write. Defined next to the legacy
/// globals it points at (`sdl_lighting_devui.cpp`), so the addresses are the
/// live ones until the owning ticket moves them.
auto lighting_knob_table() -> std::span<const knob_entry>;

/// Look up a knob by file-channel name. Null when the name is unknown.
auto knob_find( std::string_view name ) -> const knob_entry *; // *NOPAD*

/// The file channel: raw write, no clamp (the measurement escape hatch), bools
/// from `value > 0.5`. Returns false for an unknown name, exactly as the old
/// if-chain reported `(unknown)`.
auto knob_apply_file( lighting_settings &s, std::string_view name, float value ) -> bool;

/// A key press: move `cur` one `step` toward `delta`'s sign, clamped to `range`.
/// Reproduces the old `std::max( MIN, x - STEP )` / `std::min( MAX, x + STEP )`
/// pair, which is the same arithmetic.
auto knob_key_step( const knob_range &range, float cur, float delta ) -> float;

/// The F4 panel's int-proxy reconciliation, as a pure function. Two knobs
/// (shadow_steps, debug_mode) are uint storage behind an int widget, so each
/// frame one side must win: the panel when its proxy moved, the storage when
/// something else wrote it.
struct int_proxy_state {
    /// Current proxy value as the widget holds it.
    int proxy;
    /// Proxy value at the END of the previous tick (-1 = never synced).
    int last;
};
struct int_proxy_result {
    /// Value the storage takes.
    int store;
    /// Value `last` takes. Always the final proxy: a stale `last` makes the next
    /// external write look like a panel change, which is the `shadow_steps` bug.
    int last;
    /// True when the panel won this tick.
    bool panel_won;
};
/// `store_clamped` is what the storage becomes when the panel wins (shadow_steps
/// floors at 1, the debug mode at 0).
auto knob_reconcile_int_proxy( const int_proxy_state &s, int store_now, int store_clamped )
-> int_proxy_result;

} // namespace lighting

#endif // CATA_SRC_LIGHTING_LIGHTING_SETTINGS_H
