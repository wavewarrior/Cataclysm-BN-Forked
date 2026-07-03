#include "sdl_lighting_devui.h"

#include "avatar.h"
#include "camera_debug.h"
#include "cata_tiles.h"
#include "creature.h"
#include "game.h"
#include "lighting/dev_test_lights.h"
#include "lighting/gpu_emitter.h"
#include "lighting/render_state.h"
#include "lighting/rmlui_layer.h"
#include "lighting/rmlui_proc_texture.h"
#include "lighting/sdf_pass.h"
#include "lighting/sprite_batcher.h"
#include "map.h"
#include "mapdata.h"
#include "panels.h"
#include "path_info.h"
#include "rml_screen.h"
#include "rml_toggle_registry.h"
#include "rml_util.h"
#include "ui.h"
#include "ui_theme.h"
#include "weather.h"
#include "worldfactory.h"

#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Definitions for the shared globals declared in sdl_lighting_devui.h
// ---------------------------------------------------------------------------

EmitterOverlayState s_emo;

bool g_dbg_lighting = true;
bool g_dbg_lighting_shader = false;
lighting::debug_params g_dbg_params{};
bool g_rc_readback = false;
float g_tonemap_exposure = 0.35f;
float g_tonemap_min_ev = -12.47393f;
float g_tonemap_max_ev = 4.026069f;
bool g_bloom_enable = true;
float g_bloom_threshold = 1.0f;
float g_bloom_intensity = 0.5f;
bool g_vol_enable = false;
float g_vol_density = 0.3f;
float g_vol_intensity = 1.0f;
float g_vol_shadow = 0.0f;
float g_vol_reach = 8.0f;
bool g_rain_enable = true;
float g_rain_intensity = 0.5f;
float g_spec_strength = 0.0f; // wet specular glint (0=off); × rain intensity per-frame
bool g_shadow_debug = false;
uint32_t g_current_dbg_mode = 0u;
float g_skylight_bleed = 0.5f;
float g_vision_blur = 1.5f;
// Hover-outline (HOVER_OUTLINE_PLAN.md) — CPU-side, no shader cbuffer.
bool g_outline_enable = true;
float g_outline_thickness = 0.06f; // ring radius as fraction of tile width
float g_outline_alpha = 1.0f;
float g_outline_alpha_cut = 0.6f; // silhouette mask cutoff (drops baked shadows)
float g_outline_col_hostile[4] = {1.00f, 0.24f, 0.24f, 1.0f};
float g_outline_col_neutral[4] = {1.00f, 0.86f, 0.24f, 1.0f};
float g_outline_col_friendly[4] = {0.31f, 0.90f, 0.31f, 1.0f};
float g_outline_col_self[4] = {0.31f, 0.86f, 1.00f, 1.0f};

namespace menu_emitter_tuning {
float radius_input = 100.0f;
float pos_x = 8.5f;
float pos_y = 4.5f;
int pos_preset = 0;
bool blue_backdrop = true;
} // namespace menu_emitter_tuning

namespace cursor_light_emitter {
bool enabled = false;
float radius = 8.0f;
float intensity = 1.5f;
float color[3] = {1.0f, 1.0f, 1.0f}; // RGB tint (saturate one channel to see colored GI bounce)
float wx = 0.0f, wy = 0.0f, wz = 0.0f;
} // namespace cursor_light_emitter

// Storage for the click-to-place dev test lights (declared in dev_test_lights.h).
namespace dev_test_lights {
bool place_mode = false;
float hover_wx = 0.0f, hover_wy = 0.0f, hover_wz = 0.0f;
std::vector<light> lights;
} // namespace dev_test_lights

namespace sdl_lighting_devui {

// ── Tier 8 slice 1: parallel RmlUi dev-panel preview ────────────────────────
// A standalone RmlUi document that mirrors part of this ImGui panel, behind a
// preview toggle. ImGui stays primary + fully live; this proves form-control
// two-way data-binding (checkbox -> *_rmlui_enabled() bool, bound by pointer).
// Driven per-frame by rml_tick() from sdl_render_frame; opens when (F4 visible &&
// preview on), closes otherwise. Slice 2 expands it toward full parity.
namespace {
Rml::ElementDocument* g_devui_doc = nullptr;
Rml::DataModelHandle g_devui_model;
bool g_devui_visible = false; // §8 gate — the F4 toggle (RmlUi dev panel open/closed)
int g_devui_tab = 0;          // slice 7 — active tab (data-if/data-class-active in devui.rml)
// Slice 8 — proxies for controls whose backing globals aren't directly bindable
// (uint32 fields, <select> indices, size_t counts, read-only diagnostics text).
int g_devui_dbg_mode = 0;       // <select> proxy → g_current_dbg_mode (event-applied)
int g_devui_shadow_steps = 16;  // reconciled with uint g_dbg_params.shadow_steps each frame
int g_devui_placed = 0;         // mirrors dev_test_lights::lights.size() each frame
int g_runic_template = 0;       // <select> proxy → runic force_template+1 (event-applied)
bool g_runic_auto_regen = true; // bump regen when a runic field changes (cf. ImGui auto-regen)
Rml::String g_diag_text;        // diagnostics tab read-out, rebuilt each frame
Rml::Vector<Rml::String> g_dbg_mode_names;       // lighting debug-mode <select> options
Rml::Vector<Rml::String> g_runic_template_names; // runic template <select> options

// ── Tier 8 slice 4: composite colour picker (SV square + hue strip) ─────────
// RmlUi has no native colour picker, but the render interface implements gradient
// shaders (linear/horizontal/vertical-gradient), so we compose one: a saturation×value
// square (white→hue horizontal gradient under a transparent→black vertical overlay) +
// a rainbow hue strip, each draggable. HSV state is internal; on drag we map the mouse
// position to H or S/V, convert to RGB, write the target colour, and update the visuals
// (gradient/thumbs/swatch) via SetProperty. Slice 4 targets the cursor-light colour to
// prove the mechanism; later it generalises to the outline/theme colours.
float g_pk_h = 0.f; // hue 0..360
float g_pk_s = 0.f; // saturation 0..1
float g_pk_v = 1.f; // value 0..1
Rml::String g_pk_hex = "#ffffff";

// Colour targets the one picker can edit (selected by the swatch row). Each `rgb` points
// at the first of 3 contiguous RGB floats (cursor is [3]; the outline colours are [4]
// RGBA — we edit RGB and leave alpha). g_pk_idx is the active target.
struct pk_target_t {
    const char* elem_id; // its swatch element in devui.rml
    float* rgb;
};
const std::array<pk_target_t, 5> g_pk_targets = {{
    {"pkt-cursor", cursor_light_emitter::color},
    {"pkt-hostile", g_outline_col_hostile},
    {"pkt-neutral", g_outline_col_neutral},
    {"pkt-friendly", g_outline_col_friendly},
    {"pkt-self", g_outline_col_self},
}};
int g_pk_idx = 0;
float g_pk_orig[3] = {1.f, 1.f, 1.f}; // colour at the moment this target was selected

// Named theme/game colours (ui_theme) are an ALTERNATE target kind: accessed by name
// (RGBA, alpha preserved) with a post-write apply (reload_theme for RCSS, nc-cache clear
// for game colours), and there are many — so they're chosen via the <select> combo, not
// the fixed swatch row. When g_pk_named_active, the picker edits the named colour instead
// of g_pk_targets[g_pk_idx]. The combo list (built on open) maps option index → name+kind.
struct pk_named_t {
    Rml::String name;
    bool game; // false = RCSS panel colour, true = game text colour
};
std::vector<pk_named_t> g_pk_combo;         // index → name + kind
Rml::Vector<Rml::String> g_pk_combo_labels; // bound for the <option data-for>
int g_pk_combo_idx = 0;                     // bound <select> value
bool g_pk_named_active = false;             // true → edit the combo's named colour

void pk_read_target(float out[3]) {
    if (g_pk_named_active && g_pk_combo_idx >= 0
        && g_pk_combo_idx < static_cast<int>(g_pk_combo.size())) {
        const pk_named_t& n = g_pk_combo[g_pk_combo_idx];
        float tmp[4] = {0.f, 0.f, 0.f, 1.f};
        if (n.game) {
            ui_theme::get_game_rgba(n.name, tmp);
        } else {
            ui_theme::get_rcss_rgba(n.name, tmp);
        }
        out[0] = tmp[0];
        out[1] = tmp[1];
        out[2] = tmp[2];
        return;
    }
    const float* c = g_pk_targets[g_pk_idx].rgb;
    out[0] = c[0];
    out[1] = c[1];
    out[2] = c[2];
}

void pk_write_target(const float in[3]) {
    if (g_pk_named_active && g_pk_combo_idx >= 0
        && g_pk_combo_idx < static_cast<int>(g_pk_combo.size())) {
        const pk_named_t& n = g_pk_combo[g_pk_combo_idx];
        float tmp[4] = {0.f, 0.f, 0.f, 1.f};
        if (n.game) {
            ui_theme::get_game_rgba(n.name, tmp); // preserve alpha
            tmp[0] = in[0];
            tmp[1] = in[1];
            tmp[2] = in[2];
            ui_theme::set_game_rgba(n.name, tmp);
            clear_nc_color_cache(); // applies on next screen reopen
        } else {
            ui_theme::get_rcss_rgba(n.name, tmp);
            tmp[0] = in[0];
            tmp[1] = in[1];
            tmp[2] = in[2];
            ui_theme::set_rcss_rgba(n.name, tmp);
            rmlui_layer::reload_theme(); // RCSS applies instantly
        }
        return;
    }
    float* c = g_pk_targets[g_pk_idx].rgb;
    c[0] = in[0];
    c[1] = in[1];
    c[2] = in[2];
}

void pk_hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b) {
    h = std::fmod(std::fmod(h, 360.f) + 360.f, 360.f);
    const float c = v * s;
    const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    const float m = v - c;
    float rr = 0.f;
    float gg = 0.f;
    float bb = 0.f;
    if (h < 60.f) {
        rr = c;
        gg = x;
    } else if (h < 120.f) {
        rr = x;
        gg = c;
    } else if (h < 180.f) {
        gg = c;
        bb = x;
    } else if (h < 240.f) {
        gg = x;
        bb = c;
    } else if (h < 300.f) {
        rr = x;
        bb = c;
    } else {
        rr = c;
        bb = x;
    }
    r = rr + m;
    g = gg + m;
    b = bb + m;
}

void pk_rgb_to_hsv(float r, float g, float b, float& h, float& s, float& v) {
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float d = mx - mn;
    v = mx;
    s = mx <= 0.f ? 0.f : d / mx;
    if (d <= 0.f) {
        h = 0.f;
        return;
    }
    if (mx == r) {
        h = 60.f * std::fmod((g - b) / d, 6.f);
    } else if (mx == g) {
        h = 60.f * ((b - r) / d + 2.f);
    } else {
        h = 60.f * ((r - g) / d + 4.f);
    }
    if (h < 0.f) { h += 360.f; }
}

Rml::String pk_hex(float r, float g, float b) {
    const auto q = [](float f) {
        return static_cast<unsigned>(std::lround(std::clamp(f, 0.f, 1.f) * 255.f));
    };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", q(r), q(g), q(b));
    return Rml::String(buf);
}

Rml::String pk_pct(float frac) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f%%", std::clamp(frac, 0.f, 1.f) * 100.f);
    return Rml::String(buf);
}

// Recompute RGB from HSV, write the target colour, and refresh the picker visuals.
void picker_apply() {
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
    pk_hsv_to_rgb(g_pk_h, g_pk_s, g_pk_v, r, g, b);
    const float rgb[3] = {r, g, b};
    pk_write_target(rgb);
    g_pk_hex = pk_hex(r, g, b);
    if (g_devui_model) { g_devui_model.DirtyVariable("pk_hex"); }
    if (g_devui_doc == nullptr) { return; }
    // Repaint every target swatch from its live colour; outline the active one.
    for (int i = 0; i < static_cast<int>(g_pk_targets.size()); ++i) {
        if (Rml::Element* el = g_devui_doc->GetElementById(g_pk_targets[i].elem_id)) {
            const float* c = g_pk_targets[i].rgb;
            el->SetProperty("background-color", pk_hex(c[0], c[1], c[2]));
            el->SetProperty("border-color", i == g_pk_idx ? "#ffffff" : "#555555");
        }
    }
    float hr = 0.f;
    float hg = 0.f;
    float hb = 0.f;
    pk_hsv_to_rgb(g_pk_h, 1.f, 1.f, hr, hg, hb); // pure hue for the saturation gradient
    if (Rml::Element* sv = g_devui_doc->GetElementById("pk-sv")) {
        sv->SetProperty("decorator", "horizontal-gradient(#ffffff " + pk_hex(hr, hg, hb) + ")");
    }
    if (Rml::Element* t = g_devui_doc->GetElementById("pk-sv-thumb")) {
        t->SetProperty("left", pk_pct(g_pk_s));
        t->SetProperty("top", pk_pct(1.f - g_pk_v));
    }
    if (Rml::Element* t = g_devui_doc->GetElementById("pk-hue-thumb")) {
        t->SetProperty("top", pk_pct(g_pk_h / 360.f));
    }
    if (Rml::Element* sw = g_devui_doc->GetElementById("pk-swatch")) {
        sw->SetProperty("background-color", g_pk_hex);
    }
    if (Rml::Element* sw = g_devui_doc->GetElementById("pk-swatch-orig")) {
        sw->SetProperty("background-color", pk_hex(g_pk_orig[0], g_pk_orig[1], g_pk_orig[2]));
    }
}

// Seed HSV from the active target colour AND snapshot it as the revert/"orig" colour
// (call after the doc opens / on target switch — i.e. whenever a fresh target is chosen).
void picker_init() {
    float c[3];
    pk_read_target(c);
    g_pk_orig[0] = c[0];
    g_pk_orig[1] = c[1];
    g_pk_orig[2] = c[2];
    pk_rgb_to_hsv(c[0], c[1], c[2], g_pk_h, g_pk_s, g_pk_v);
}

void devui_rml_open() {
    if (g_devui_doc != nullptr || !rmlui_layer::ready()) { return; }
    Rml::Context* ctx = rmlui_layer::context();
    if (ctx == nullptr) { return; }
    Rml::DataModelConstructor c = ctx->CreateDataModel("devui");
    if (!c) { return; }
    // Bind the screen-toggle flags BY POINTER: data-checked two-way binds each
    // checkbox to the real static bool, so a click toggles the live screen and
    // ImGui-side changes reflect here (rml_tick DirtyAllVariables each frame). Mirrors
    // the ImGui "Route screens through RmlUi" registry (draw_rmlui_tab).
    c.Bind("tab", &g_devui_tab); // slice 7 — active tab page
    // Register the array type ONCE, before any Rml::Vector<Rml::String> Bind below
    // (dbg_mode_names / ru_template_names / pk_names) — binding before this fails with
    // "data type not registered".
    c.RegisterArray<Rml::Vector<Rml::String>>();
    c.Bind("ui_scale", &rmlui_layer::ui_scale()); // slider (float, two-way)
    // The per-screen toggle checkboxes — bound BY POINTER from the single-source-of-truth
    // registry so this list can never drift from the flip-all control (rml_toggle_registry).
    for (const rml_toggle& t : rml_toggle_registry()) { c.Bind(t.name, &t.accessor()); }
    // Flip-all controls (mirror the ImGui buttons). The checkbox vars are bound by
    // pointer + rml_tick DirtyAllVariables each frame, so they reflect the flip next frame.
    c.BindEventCallback(
        "rml_all_on", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            rml_toggles_set_all(true);
        });
    c.BindEventCallback(
        "rml_all_off", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            rml_toggles_set_all(false);
        });
    c.BindEventCallback("rml_reset", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        rml_toggles_reset_defaults();
    });
    // Slice 3 — Effects tab tuning params (live lighting). Floats two-way bound to the
    // same globals the ImGui sliders drive; the game render reads them each frame.
    c.Bind("nrm_amount", &g_dbg_params.nrm_amount);
    c.Bind("nrm_relief", &g_dbg_params.nrm_relief);
    c.Bind("nrm_elev", &g_dbg_params.nrm_elev);
    // Camera follow knobs (CPU-only; pushed into game::main_camera_ in draw_ter).
    c.Bind("cam_smooth", &camera_dbg::smooth_speed);
    c.Bind("cam_lookahead", &camera_dbg::look_ahead);
    c.Bind("cam_deadzone", &camera_dbg::dead_zone);
    c.Bind("vol_enable", &g_vol_enable);
    c.Bind("vol_density", &g_vol_density);
    c.Bind("vol_intensity", &g_vol_intensity);
    c.Bind("vol_reach", &g_vol_reach);
    c.Bind("vol_shadow", &g_vol_shadow);
    c.Bind("rain_enable", &g_rain_enable);
    c.Bind("rain_intensity", &g_rain_intensity);
    c.Bind("spec_strength", &g_spec_strength);
    c.Bind("shadow_debug", &g_shadow_debug);
    c.Bind("shadow_mask_str", &g_dbg_params.shadow_mask_str);
    c.Bind("mem_dim", &g_dbg_params.mem_dim);
    c.Bind("mem_radius", &g_dbg_params.mem_radius);
    c.Bind("sway_amp", &g_dbg_params.sway_amp);
    c.Bind("sway_freq", &g_dbg_params.sway_freq);
    c.Bind("outline_enable", &g_outline_enable);
    c.Bind("outline_thickness", &g_outline_thickness);
    c.Bind("outline_alpha", &g_outline_alpha);
    c.Bind("outline_alpha_cut", &g_outline_alpha_cut);
    // Slice 8 — Lighting tab. Floats two-way bound to the same globals the ImGui sliders
    // drive; debug_mode + shadow_steps are uint32 so they go through int proxies.
    c.Bind("emitter_scale", &g_dbg_params.emitter_scale);
    c.Bind("sun_scale", &g_dbg_params.sun_scale);
    c.Bind("sky_scale", &g_dbg_params.sky_scale);
    c.Bind("skylight_bleed", &g_skylight_bleed);
    c.Bind("vision_blur", &g_vision_blur);
    c.Bind("tonemap_exposure", &g_tonemap_exposure);
    c.Bind("tonemap_min_ev", &g_tonemap_min_ev);
    c.Bind("tonemap_max_ev", &g_tonemap_max_ev);
    c.Bind("bloom_enable", &g_bloom_enable);
    c.Bind("bloom_threshold", &g_bloom_threshold);
    c.Bind("bloom_intensity", &g_bloom_intensity);
    c.Bind("depth_lean_str", &g_depth_lean_str);
    c.Bind("depth_dark_str", &g_depth_dark_str);
    c.Bind("dither_amt", &g_dbg_params.dither_amt);
    c.Bind("dither_bands", &g_dbg_params.dither_bands);
    c.Bind("gi_strength", &g_dbg_params.gi_strength);
    c.Bind("shadow_k", &g_dbg_params.shadow_k);
    c.Bind("shadow_steps", &g_devui_shadow_steps); // int proxy → uint each frame
    // P5b: sky/sun quality knobs (sky_sun.comp cbuffer, read each frame)
    c.Bind("sky_dirs", &g_dbg_params.sky_dirs);
    c.Bind("sky_reach", &g_dbg_params.sky_reach);
    c.Bind("sun_steps", &g_dbg_params.sun_steps);
    c.Bind("sun_penumbra", &g_dbg_params.sun_penumbra);
    c.Bind("light_eps", &g_dbg_params.light_eps);
    c.Bind("max_shadow_k", &g_dbg_params.max_shadow_k);
    c.Bind("sdf_sharp", &g_dbg_params.sdf_sharp);
    c.Bind("ao_strength", &g_dbg_params.ao_strength);
    c.Bind("night_floor", &g_dbg_params.night_floor);
    c.Bind("day_floor", &g_dbg_params.day_floor);
    c.Bind("dbg_mode_idx", &g_devui_dbg_mode);
    c.Bind("dbg_mode_names", &g_dbg_mode_names);
    c.BindEventCallback("dbg_mode", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        g_current_dbg_mode = static_cast<uint32_t>(std::max(0, g_devui_dbg_mode));
        g_dbg_params.debug_mode = g_current_dbg_mode;
    });
    c.BindEventCallback(
        "rc_readback",
        [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { g_rc_readback = true; });
    // Slice 8 — Effects tab cursor-light controls (colour is on the Colour tab).
    c.Bind("cursor_enable", &cursor_light_emitter::enabled);
    c.Bind("cursor_radius", &cursor_light_emitter::radius);
    c.Bind("cursor_intensity", &cursor_light_emitter::intensity);
    c.Bind("place_mode", &dev_test_lights::place_mode);
    c.Bind("placed_count", &g_devui_placed);
    c.BindEventCallback(
        "clear_lights", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            dev_test_lights::lights.clear();
        });
    // Slice 8 — CRT post-effect + world-text (Screens tab).
    rmlui_layer::crt_params& crtp = rmlui_layer::crt();
    c.Bind("crt_enable", &crtp.enabled);
    c.Bind("crt_scanline_alpha", &crtp.scanline_alpha);
    c.Bind("crt_scanline_pitch", &crtp.scanline_pitch);
    c.Bind("crt_scanline_thickness", &crtp.scanline_thickness);
    c.Bind("crt_roll_speed", &crtp.roll_speed);
    c.Bind("crt_flicker", &crtp.flicker);
    c.Bind("crt_vignette", &crtp.vignette_alpha);
    c.Bind("wt_px", &rmlui_layer::world_text_px());
    c.Bind("wt_dx", &rmlui_layer::world_text_dx());
    c.Bind("wt_dy", &rmlui_layer::world_text_dy());
    // Slice 8 — Animation tab (struct fields are bindable; the singleton is persistent).
    c.Bind("anim_override", &debug_anim_override());
    animation_tuning& at = debug_anim_tuning();
    c.Bind("an_move_bob", &at.move_bob);
    c.Bind("an_breathing", &at.breathing);
    c.Bind("an_hit_reaction", &at.hit_reaction);
    c.Bind("an_attack_lunge", &at.attack_lunge);
    c.Bind("an_bob_amplitude", &at.bob_amplitude);
    c.Bind("an_bob_duration", &at.bob_duration);
    c.Bind("an_move_bob_freq", &at.move_bob_freq);
    c.Bind("an_move_tilt_deg", &at.move_tilt_deg);
    c.Bind("an_move_slide_dur", &at.move_slide_dur);
    c.Bind("an_idle_sway", &at.idle_sway);
    c.Bind("an_idle_freq", &at.idle_freq);
    c.Bind("an_idle_tilt_deg", &at.idle_tilt_deg);
    c.Bind("an_idle_vbob_mult", &at.idle_vbob_mult);
    c.Bind("an_hit_push", &at.hit_push);
    c.Bind("an_hit_duration", &at.hit_duration);
    c.Bind("an_hit_burst_total", &at.hit_burst_total);
    c.Bind("an_hit_flash_intensity", &at.hit_flash_intensity);
    c.Bind("an_hit_flash_frac", &at.hit_flash_frac);
    c.Bind("an_hit_freq", &at.hit_freq);
    c.Bind("an_hit_tilt_deg", &at.hit_tilt_deg);
    c.Bind("an_attack_amplitude", &at.attack_amplitude);
    c.Bind("an_attack_duration", &at.attack_duration);
    c.Bind("an_attack_freq", &at.attack_freq);
    c.Bind("an_attack_ranged_mult", &at.attack_ranged_mult);
    c.Bind("an_attack_tilt_melee_deg", &at.attack_tilt_melee_deg);
    c.Bind("an_attack_tilt_ranged_deg", &at.attack_tilt_ranged_deg);
    // Slice 8 — Runic frame tab (all int/bool fields bindable; template via <select>;
    // buttons via event-click; auto-regen handled in rml_tick by a change check).
    lighting::runic_params& rc = lighting::runic_cfg();
    c.Bind("ru_col_r", &rc.col_r);
    c.Bind("ru_col_g", &rc.col_g);
    c.Bind("ru_col_b", &rc.col_b);
    c.Bind("ru_ring", &rc.ring);
    c.Bind("ru_glyph_scale", &rc.glyph_scale);
    c.Bind("ru_band_top", &rc.band_top);
    c.Bind("ru_div_top", &rc.div_top);
    c.Bind("ru_div_bot", &rc.div_bot);
    c.Bind("ru_frame_inset", &rc.frame_inset);
    c.Bind("ru_fill_pct", &rc.fill_pct);
    c.Bind("ru_wall", &rc.wall);
    c.Bind("ru_divw", &rc.divw);
    c.Bind("ru_pad", &rc.pad);
    c.Bind("ru_ggap", &rc.ggap);
    c.Bind("ru_gapi", &rc.gapi);
    c.Bind("ru_border_frac", &rc.border_frac);
    c.Bind("ru_rgap", &rc.rgap);
    c.Bind("ru_pitch", &rc.pitch);
    c.Bind("ru_unit", &rc.unit);
    c.Bind("ru_corrode_pct", &rc.corrode_pct);
    c.Bind("ru_corrode_grid", &rc.corrode_grid);
    c.Bind("ru_corrode_reach", &rc.corrode_reach);
    c.Bind("ru_corrode_grit", &rc.corrode_grit);
    c.Bind("ru_taper_dots", &rc.taper_dots);
    c.Bind("ru_taper_gap", &rc.taper_gap);
    c.Bind("ru_rune_small_px", &rc.rune_small_px);
    c.Bind("ru_fixed_seed", &rc.use_fixed_seed);
    c.Bind("ru_auto_regen", &g_runic_auto_regen);
    c.Bind("ru_template_idx", &g_runic_template);
    c.Bind("ru_template_names", &g_runic_template_names);
    c.BindEventCallback(
        "ru_template", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            lighting::runic_params& r = lighting::runic_cfg();
            r.force_template = g_runic_template - 1; // 0 (Auto) -> -1
            r.regen++;
        });
    c.BindEventCallback(
        "ru_randomize", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            lighting::runic_params& r = lighting::runic_cfg();
            r.seed = r.seed * 1664525u + 1013904223u; // LCG step
            r.regen++;
        });
    c.BindEventCallback(
        "ru_generate", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
            lighting::runic_cfg().regen++;
        });
    c.BindEventCallback("ru_save", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        lighting::save_runic_cfg();
    });
    c.BindEventCallback("ru_reload", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        lighting::load_runic_cfg();
        lighting::runic_cfg().regen++;
    });
    c.BindEventCallback("ru_reset", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        lighting::runic_params& r = lighting::runic_cfg();
        const unsigned keep = r.regen;
        r = lighting::runic_params{};
        r.regen = keep + 1;
    });
    // Diagnostics read-out (rebuilt each frame in rml_tick).
    c.Bind("diag_text", &g_diag_text);
    // Slice 4 — colour picker. pk_hex is the readout; the SV square + hue strip emit
    // mousedown/drag → map mouse pos to S/V or H, then picker_apply() writes the colour.
    c.Bind("pk_hex", &g_pk_hex);
    c.BindEventCallback("pk_sv", [](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
        if (g_devui_doc == nullptr) { return; }
        Rml::Element* sv = g_devui_doc->GetElementById("pk-sv");
        if (sv == nullptr) { return; }
        const Rml::Vector2f off = sv->GetAbsoluteOffset(Rml::BoxArea::Border);
        const float w = sv->GetClientWidth();
        const float h = sv->GetClientHeight();
        if (w <= 0.f || h <= 0.f) { return; }
        const float mx = ev.GetParameter<float>("mouse_x", off.x);
        const float my = ev.GetParameter<float>("mouse_y", off.y);
        g_pk_s = std::clamp((mx - off.x) / w, 0.f, 1.f);
        g_pk_v = std::clamp(1.f - (my - off.y) / h, 0.f, 1.f);
        picker_apply();
    });
    c.BindEventCallback("pk_hue", [](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
        if (g_devui_doc == nullptr) { return; }
        Rml::Element* hue = g_devui_doc->GetElementById("pk-hue");
        if (hue == nullptr) { return; }
        const Rml::Vector2f off = hue->GetAbsoluteOffset(Rml::BoxArea::Border);
        const float h = hue->GetClientHeight();
        if (h <= 0.f) { return; }
        const float my = ev.GetParameter<float>("mouse_y", off.y);
        g_pk_h = std::clamp((my - off.y) / h, 0.f, 1.f) * 360.f;
        picker_apply();
    });
    c.BindEventCallback(
        "pk_target", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            int idx = -1;
            if (!args.empty()) { args[0].GetInto(idx); }
            if (idx >= 0 && idx < static_cast<int>(g_pk_targets.size())) {
                g_pk_named_active = false; // back to a direct (cursor/outline) target
                g_pk_idx = idx;
                picker_init(); // reseed HSV + snapshot orig from the newly-selected colour
                picker_apply();
            }
        });
    c.BindEventCallback("pk_combo", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        // The <select> two-way binds g_pk_combo_idx; switch to that named theme colour.
        if (g_pk_combo_idx >= 0 && g_pk_combo_idx < static_cast<int>(g_pk_combo.size())) {
            g_pk_named_active = true;
            picker_init();
            picker_apply();
        }
    });
    c.BindEventCallback("pk_revert", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        // Restore the snapshot taken when this target was selected; reseed HSV WITHOUT
        // re-snapshotting (picker_init would overwrite orig), then repaint.
        pk_write_target(g_pk_orig);
        pk_rgb_to_hsv(g_pk_orig[0], g_pk_orig[1], g_pk_orig[2], g_pk_h, g_pk_s, g_pk_v);
        picker_apply();
    });
    // Slice 7 — tab bar. data-event-click="devui_tab(N)" sets the active page; data-if /
    // data-class-active in devui.rml read `tab`. Dirty it so the switch is instant (rml_tick
    // also DirtyAllVariables each frame, but don't wait a frame on a click).
    c.BindEventCallback(
        "devui_tab", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
            int idx = 0;
            if (!args.empty()) { args[0].GetInto(idx); }
            g_devui_tab = idx;
            if (g_devui_model) { g_devui_model.DirtyVariable("tab"); }
        });
    // Slice 7 — bottom-right drag handle resizes the panel. Drag emits absolute mouse_x/y;
    // panel size = mouse - panel top-left. The panel is a fixed-height flex column, so the
    // body flexes to fill the new height (and scrolls when content exceeds it).
    c.BindEventCallback(
        "devui_resize", [](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
            if (g_devui_doc == nullptr) { return; }
            Rml::Element* panel = g_devui_doc->GetElementById("devui-panel");
            if (panel == nullptr) { return; }
            const Rml::Vector2f poff = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
            const float mx = ev.GetParameter<float>("mouse_x", poff.x);
            const float my = ev.GetParameter<float>("mouse_y", poff.y);
            const float w = std::clamp(mx - poff.x, 240.f, 900.f);
            const float h = std::clamp(my - poff.y, 160.f, 1200.f);
            panel->SetProperty("width", std::to_string(static_cast<int>(w)) + "px");
            panel->SetProperty("height", std::to_string(static_cast<int>(h)) + "px");
        });
    // Build + bind the theme/game colour combo (names + selected index).
    g_pk_combo.clear();
    g_pk_combo_labels.clear();
    for (const std::string& n : ui_theme::rcss_names()) {
        g_pk_combo.push_back({Rml::String(n), false});
        g_pk_combo_labels.push_back(Rml::String(n));
    }
    for (const std::string& n : ui_theme::game_color_names()) {
        g_pk_combo.push_back({Rml::String(n), true});
        g_pk_combo_labels.push_back(Rml::String(n));
    }
    c.Bind("pk_names", &g_pk_combo_labels);
    c.Bind("pk_combo_idx", &g_pk_combo_idx);
    // Slice 8 — <select> option lists + seed the int proxies from their live globals.
    g_dbg_mode_names =
        {"off",     "ambient", "emitter", "sun",         "sky", "total",      "SDF",    "sky_vis",
         "emit_bw", "normal",  "AO",      "shadow mask", "GI",  "sky access", "sun occ"};
    g_runic_template_names = {"Auto", "Centred", "Thirds", "Fixed-interval"};
    g_devui_dbg_mode = static_cast<int>(g_current_dbg_mode);
    g_devui_shadow_steps = static_cast<int>(g_dbg_params.shadow_steps);
    g_runic_template = lighting::runic_cfg().force_template + 1;
    g_devui_model = c.GetModelHandle();
    Rml::ElementDocument* doc =
        rmlui_layer::open_document(PATH_INFO::datadir() + "gui/devui.rml", false);
    if (doc == nullptr) {
        ctx->RemoveDataModel("devui");
        return;
    }
    g_devui_doc = doc;
    // Seed the picker from the current cursor-light colour + paint the initial visuals.
    picker_init();
    picker_apply();
}

void devui_rml_close() {
    if (g_devui_doc == nullptr) { return; }
    rmlui_layer::close_document(g_devui_doc);
    if (Rml::Context* ctx = rmlui_layer::context()) { ctx->RemoveDataModel("devui"); }
    g_devui_doc = nullptr;
    g_devui_model = Rml::DataModelHandle();
}
} // namespace

// Per-frame driver for the parallel RmlUi dev-panel preview (called from
// sdl_render_frame). Opens the doc when F4 is visible AND the preview checkbox is on,
// closes it otherwise; while open, dirty all bound vars so ImGui-side toggle changes
// reflect in the RmlUi checkboxes.
// Slice 8 — the Diagnostics tab as one multi-line string (RmlUi has no per-frame text
// widget; we rebuild + bind a string). Mirrors draw_diagnostics_tab's read-out.
static Rml::String build_diag_text() {
    if (!g_dbg_lighting) { return "enable Debug HUD (F5) for live readout"; }
    auto& rs = lighting::get_render_state();
    const float tp = s_emo.tile_px > 0.f ? s_emo.tile_px : 32.f;
    const float sw = static_cast<float>(s_emo.screen_w);
    const float sh = static_cast<float>(s_emo.screen_h);
    const size_t cache_sz =
        g ? get_map().access_cache(g->u.bub_pos().z()).transparency_cache.size() : 0;
    const int wanted = g ? (get_map().getmapsize() * SEEX) * (get_map().getmapsize() * SEEY) : 0;
    std::string out;
    char buf[256];
    snprintf(buf, sizeof(buf), "screen=%dx%d  tile_px=%.1f\n", s_emo.screen_w, s_emo.screen_h, tp);
    out += buf;
    snprintf(buf, sizeof(buf), "SDF pop=%d  rt=%dx%d  tex=%dx%d  cache=%zu/%d\n",
             rs.sdf().populated() ? 1 : 0, rs.sdf().map_w(), rs.sdf().map_h(), rs.sdf().tex_w(),
             rs.sdf().tex_h(), cache_sz, wanted);
    out += buf;
    if (g && cache_sz > 0) {
        map& mm = get_map();
        const int H = mm.getmapsize() * SEEY;
        const auto& tc = mm.access_cache(g->u.bub_pos().z()).transparency_cache;
        const int px = g->u.bub_pos().x();
        const int py = g->u.bub_pos().y();
        auto T = [&](int x, int y) -> float {
            const int i = x * H + y;
            return (i >= 0 && i < static_cast<int>(tc.size())) ? tc[i] : -1.f;
        };
        snprintf(buf, sizeof(buf), "trans@p=%.3f N=%.3f S=%.3f E=%.3f W=%.3f\n", T(px, py),
                 T(px, py - 1), T(px, py + 1), T(px + 1, py), T(px - 1, py));
        out += buf;
        snprintf(buf, sizeof(buf), "trans@p(submit)=%.3f sdfW=%d sz=%zu\n", s_emo.trans_at_player,
                 s_emo.sdf_W_at_submit, s_emo.sdf_size_at_submit);
        out += buf;
    }
    snprintf(buf, sizeof(buf), "map_origin=(%d,%d)  draw_off_px=(%d,%d)\n", s_emo.map_origin_x,
             s_emo.map_origin_y, s_emo.draw_off_px_x, s_emo.draw_off_px_y);
    out += buf;
    snprintf(buf, sizeof(buf), "cam_off=(%.2f,%.2f)  op=(%.0f,%.0f)\n", s_emo.cam_off_x,
             s_emo.cam_off_y, s_emo.op_x, s_emo.op_y);
    out += buf;
    snprintf(buf, sizeof(buf), "player=(%d,%d,%d)  emitters=%zu  pushed=%u\n", s_emo.player_x,
             s_emo.player_y, s_emo.player_z, s_emo.snap.size(), s_emo.last_n_emit_pushed);
    out += buf;
    const float pscr_x = (s_emo.player_x + s_emo.cam_off_x) * tp + s_emo.op_x;
    const float pscr_y = (s_emo.player_y + s_emo.cam_off_y) * tp + s_emo.op_y;
    snprintf(buf, sizeof(buf), "player_screen=(%.1f,%.1f)  center=(%.1f,%.1f)\n", pscr_x, pscr_y,
             sw * 0.5f, sh * 0.5f);
    out += buf;
    const float dx = pscr_x - sw * 0.5f;
    const float dy = pscr_y - sh * 0.5f;
    snprintf(buf, sizeof(buf), "delta_to_center=(%.1f,%.1f)px  =(%.2f,%.2f)tiles\n", dx, dy,
             dx / tp, dy / tp);
    out += buf;
    if (!s_emo.snap.empty()) {
        const lighting::gpu_emitter& e0 = s_emo.snap.front();
        const float ed_x = e0.pos_x - static_cast<float>(s_emo.player_x);
        const float ed_y = e0.pos_y - static_cast<float>(s_emo.player_y);
        const float ed = std::sqrt(ed_x * ed_x + ed_y * ed_y);
        snprintf(buf, sizeof(buf), "emit[0] pos=(%.1f,%.1f,%.1f) r=%.1f dist=%.2f %s\n", e0.pos_x,
                 e0.pos_y, e0.pos_z, e0.radius, ed, (ed < e0.radius) ? "INSIDE" : "outside");
        out += buf;
    }
    snprintf(buf, sizeof(buf), "menu  F10:r_in=%.0f  F11:pos=(%.1f,%.1f)  F12:bgBlue=%s",
             menu_emitter_tuning::radius_input, menu_emitter_tuning::pos_x,
             menu_emitter_tuning::pos_y, menu_emitter_tuning::blue_backdrop ? "ON" : "off");
    out += buf;
    return out;
}

bool& devui_visible() { return g_devui_visible; }

bool place_test_light() {
    // No-op unless the panel is open with place-mode on (mirrors the old ImGui guard).
    // Returns true when it placed one, so the caller can consume the click.
    if (!g_devui_visible || !dev_test_lights::place_mode) { return false; }
    dev_test_lights::lights.push_back(dev_test_lights::light{
        dev_test_lights::hover_wx, dev_test_lights::hover_wy, dev_test_lights::hover_wz,
        cursor_light_emitter::radius, cursor_light_emitter::intensity,
        cursor_light_emitter::color[0], cursor_light_emitter::color[1],
        cursor_light_emitter::color[2]});
    return true;
}

void rml_tick() {
    if (g_devui_visible) {
        devui_rml_open();
        if (g_devui_doc != nullptr) {
            // Slice 8 per-frame sync of the values that can't two-way bind directly.
            // shadow_steps (uint): reconcile with the int proxy — whichever side changed wins.
            static int last_ss = -1;
            if (g_devui_shadow_steps != last_ss) {
                g_dbg_params.shadow_steps = static_cast<uint32_t>(
                    std::max(1, g_devui_shadow_steps));
            } else {
                g_devui_shadow_steps = static_cast<int>(g_dbg_params.shadow_steps);
            }
            last_ss = g_devui_shadow_steps;
            // placed-light count (size_t → int readout).
            g_devui_placed = static_cast<int>(dev_test_lights::lights.size());
            // runic auto-regen: bump regen when any edited field changed (cheap field sum).
            if (g_runic_auto_regen) {
                lighting::runic_params& r = lighting::runic_cfg();
                const long sig =
                    r.col_r + r.col_g + r.col_b + r.ring + r.glyph_scale + r.band_top + r.div_top
                    + r.div_bot + r.wall + r.divw + r.pad + r.ggap + r.gapi + r.rgap + r.pitch
                    + r.border_frac + r.unit + r.fill_pct + r.frame_inset + r.corrode_pct
                    + r.corrode_grid + r.corrode_reach + r.corrode_grit + r.taper_dots + r.taper_gap
                    + r.rune_small_px + (r.use_fixed_seed ? 1 : 0) + r.force_template;
                static bool sig_init = false;
                static long last_sig = 0;
                if (!sig_init || sig != last_sig) {
                    if (sig_init) {
                        r.regen++; // skip the spurious first-frame bump
                    }
                    last_sig = sig;
                    sig_init = true;
                }
            }
            g_diag_text = build_diag_text();
            g_devui_model.DirtyAllVariables();
        }
    } else {
        devui_rml_close();
    }
}

} // namespace sdl_lighting_devui
