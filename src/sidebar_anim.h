#pragma once
#ifndef CATA_SRC_SIDEBAR_ANIM_H
#define CATA_SRC_SIDEBAR_ANIM_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "color.h"
#include "ui_tween.h"

// Sidebar animation state registry: bridges widget state -> tweens -> a per-draw
// transform. State lives here (a singleton), NOT in the value-widget draw
// closures — those are rebuilt on layout reload, so capturing animation state in
// them would lose it. Keyed by a stable string (widget id, or "moon"/"wind" for
// the native panels). `sidebar_requires_animation()` queries this same store so
// the idle redraw loop only repaints while something is animating.
//
// What animates is data-driven: each icon in gfx/widgets/icons.json may carry an
// "animations" array of specs (trigger + property + curve/params). The config
// says WHAT and HOW; this code decides WHEN (value changed / entered a danger
// band / always). An icon with no specs never animates — so effects are opt-in.
namespace sidebar_anim
{

// The transform an icon/row applies on top of its base draw, resolved each frame.
struct icon_transform {
    float scale = 1.0f;        // uniform, about the cell centre
    float scale_y = 1.0f;      // extra vertical scale, anchored at pivot_y
    float pivot_y = 0.5f;      // vertical anchor for scale_y (0 = top, 1 = bottom)
    float alpha = 1.0f;        // multiplies the tint alpha
    float offset_y = 0.0f;     // additive, pixels
    float rotation = 0.0f;     // degrees, clockwise (spin)
    nc_color blend_color = c_white; // target for `blend`
    float blend = 0.0f;        // 0 = base tint, 1 = fully blend_color
};

enum class anim_prop { scale, scale_y, alpha, offset_y, rotation, color_blend };

// When a spec fires: on any value change, on an increase / decrease specifically
// (for directional effects), while in a critical/danger band, or continuously
// (ambient loop, started once when the widget is first seen).
enum class anim_trigger { on_change, on_increase, on_decrease, critical, ambient };

// One parsed animation directive from icons.json (per icon).
struct anim_spec {
    anim_trigger trigger = anim_trigger::on_change;
    anim_prop prop = anim_prop::scale;
    float from = 1.0f;
    float to = 1.0f;
    std::uint32_t duration_ms = 300;
    ui_tween::ease_curve ease = ui_tween::ease_curve::linear;
    ui_tween::tween_loop loop = ui_tween::tween_loop::once;
    int repeats = 0;
    nc_color blend_color = c_white; // only used when prop == color_blend
    float pivot_y = 0.5f;           // only used when prop == scale_y (0 top, 1 bottom)
};

class registry
{
    public:
        // (Re)read gfx/widgets/icons.json and bind each icon's "animations" specs.
        // Safe to call repeatedly (reload). Does not touch live channel state.
        void load_specs();
        // Replace the bound specs directly (used by load_specs and by tests).
        void bind_specs( std::map<std::string, std::vector<anim_spec>> specs );

        // Feed a widget's current state. `key` is the stable per-widget state key;
        // `icon` is the spec-lookup id (the widget's icon, or "moon"/"wind"). The
        // FIRST call for a key records the value and starts ambient specs only (no
        // change-flash on open). Later: a value change fires on_change specs
        // (retargeted from the current sampled value); entering the critical band
        // fires critical specs; leaving it eases those props back to identity.
        void update( const std::string &key, const std::string &icon, double value,
                     bool is_critical, std::uint32_t now );
        // Resolved transform for `key` at `now` (identity if nothing is running).
        icon_transform sample( const std::string &key, std::uint32_t now ) const;
        // True if any key has a non-settled tween — drives the idle redraw.
        bool any_active( std::uint32_t now ) const;
        // Forget all live animation state (call on game load — see game::setup).
        // Forget a single key: remove state and stop active tweens.
        auto forget( const std::string &key ) -> void;
        void clear();

    private:
        struct channel_state {
            double last_value = 0.0;
            bool primed = false;
            bool was_critical = false;
            bool ambient_started = false;
            nc_color blend_color = c_white;
            float pivot_y = 0.5f;
            std::map<anim_prop, ui_tween::tween> active;
            // Spring-damper tweens (Phase 2): mutable so sample() can step them.
            mutable std::map<anim_prop, ui_tween::spring_state> springs;
            // Wall-clock ms of the last sample() call (for spring dt).
            mutable std::uint32_t last_sample_ms = 0;
        };
        std::map<std::string, channel_state> states_;
        std::map<std::string, std::vector<anim_spec>> specs_;
};

registry &get();

// Current animation clock (ms), the SAME timeline tweens and the idle predicate
// use (SDL_GetTicks). Callers in panels.cpp use this so they need no SDL include.
std::uint32_t now_ms();

} // namespace sidebar_anim

// Global-scope predicate for handle_action.cpp's animation invalidation `||`.
// Wraps sidebar_anim::get().any_active( SDL_GetTicks() ); cheap when idle.
bool sidebar_requires_animation();

#endif // CATA_SRC_SIDEBAR_ANIM_H
