#pragma once

#include <climits>
#include <cstdint>
#include <string>
#include <vector>

namespace lighting
{

struct debug_params;
struct gpu_emitter;

} // namespace lighting

// Debug overlay state — saved from the previous frame, drawn this frame.
struct EmitterOverlayState {
    std::vector<lighting::gpu_emitter> snap;
    float cam_off_x = 0.f, cam_off_y = 0.f, tile_px = 32.f;
    float op_x = 0.f, op_y = 0.f;
    int player_x = 0, player_y = 0, player_z = 0;
    int screen_w = 0, screen_h = 0;
    int map_origin_x = 0, map_origin_y = 0;
    int draw_off_px_x = 0, draw_off_px_y = 0;
    uint32_t last_n_emit_pushed = 0;
    float trans_at_player = -1.f;
    int sdf_W_at_submit = 0;
    size_t sdf_size_at_submit = 0;
};

extern EmitterOverlayState s_emo;

// Clamp ranges + key-step sizes for the F8/F9 handlers and the F4 sliders.
// Single source so the two input paths can't drift.
namespace lighting_dbg_range
{
inline constexpr float SCALE_MIN = 0.0f, SCALE_MAX = 10.0f, SCALE_STEP = 0.1f;
inline constexpr float GI_MIN = 0.0f, GI_MAX = 2.0f, GI_STEP = 0.05f;
inline constexpr float DAMT_MIN = 0.0f, DAMT_MAX = 1.0f, DAMT_STEP = 0.1f;
inline constexpr float DBND_MIN = 1.0f, DBND_MAX = 16.0f, DBND_STEP = 1.0f;
} // namespace lighting_dbg_range

// Master toggle for the lighting debug HUD.
extern bool g_dbg_lighting;
// When true, the fragment shader replaces lighting output with a heatmap.
extern bool g_dbg_lighting_shader;
// Runtime tuning state for shader debug modes.
extern lighting::debug_params g_dbg_params;
// One-shot readback of the RC cascade texture (logs stats).
extern bool g_rc_readback;
// Tonemap pass controls (F4 sliders).
extern float g_tonemap_exposure;
extern float g_tonemap_min_ev;
extern float g_tonemap_max_ev;
// Bloom post controls (F4 sliders).
extern bool g_bloom_enable;
extern float g_bloom_threshold;
extern float g_bloom_intensity;
// Volumetric sun-shaft controls.
extern bool g_vol_enable;
extern float g_vol_density;
extern float g_vol_intensity;
extern float g_vol_shadow;
extern float g_vol_reach;
// High-fidelity rain effect controls.
extern bool g_rain_enable;
extern float g_rain_intensity;
// Wet specular glint strength (max; folded with rain intensity per-frame). 0 = off.
extern float g_spec_strength;
// Silhouette sun-shadow mask kill-gate.
extern bool g_shadow_debug;
// Current debug mode display (0-7, cycles through modes).
extern uint32_t g_current_dbg_mode;
// Per-contribution scales live in g_dbg_params.{emitter,sun,sky}_scale (single
// source of truth, consumed by the renderer); no standalone copies.
// Indoor daylight bleed strength.
extern float g_skylight_bleed;
// Hover-outline controls (CPU-side; see HOVER_OUTLINE_PLAN.md).
extern bool g_outline_enable;
extern float g_outline_thickness;
extern float g_outline_alpha;
extern float g_outline_alpha_cut;
extern float g_outline_col_hostile[4];
extern float g_outline_col_neutral[4];
extern float g_outline_col_friendly[4];
extern float g_outline_col_self[4];
// Vision-mask blur (tiles).
extern float g_vision_blur;
// Depth extrude (DitW) global multipliers.
extern float g_depth_lean_str;
extern float g_depth_dark_str;

// Main-menu decorative-emitter tuning.
namespace menu_emitter_tuning
{

extern float radius_input;
extern float pos_x;
extern float pos_y;
extern int pos_preset;
extern bool blue_backdrop;

} // namespace menu_emitter_tuning

// Dev cursor light (F4 panel).
namespace cursor_light_emitter
{

extern bool enabled;
extern float radius;
extern float intensity;
extern float wx, wy, wz;

} // namespace cursor_light_emitter

namespace sdl_lighting_devui
{

// F4 opens the RmlUi dev panel (devui.rml). devui_visible() is the F4 toggle
// (sdl_input writes it); rml_tick() opens/syncs/closes the doc to match it each
// frame (called from refresh_display).
bool &devui_visible();
void rml_tick();

// Place a static dev test light at the hovered world tile. Returns true if it placed
// one (panel open + place-mode on) so the caller can consume the click. Was inline in
// the ImGui Effects tab; now driven by a world click from sdl_input.
bool place_test_light();

} // namespace sdl_lighting_devui
