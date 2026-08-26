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
// ASC-CDL colour grade + post-processing controls (F4 sliders).
extern float g_grade_cdl_slope_r;
extern float g_grade_cdl_slope_g;
extern float g_grade_cdl_slope_b;
extern float g_grade_cdl_offset_r;
extern float g_grade_cdl_offset_g;
extern float g_grade_cdl_offset_b;
extern float g_grade_cdl_power_r;
extern float g_grade_cdl_power_g;
extern float g_grade_cdl_power_b;
extern float g_grade_temperature;
extern float g_grade_tint;
extern float g_grade_saturation;
extern float g_grade_contrast;
extern float g_grade_vignette;
extern float g_grade_grain;
extern float g_grade_ca;
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
// World-locked sub-tile decal splatmap. Off restores the grid-locked fd_blood
// tile sprite and the single-pass Pass W exactly as they were.
extern bool g_splatmap_enable;
// Blood channel strength for the splatmap composite. Deliberately strong: the
// composite lands in the HDR world target BEFORE the AgX tonemap + exposure
// 0.35, which crushes subtle darkening.
extern float g_splat_blood_strength;

// ── Atmospheric HUD particles (screen-space ambient layer, drawn over the HUD).
// The weather/time-of-day picker in sdl_render_frame chooses an emitter and its
// base rate/alpha; everything here overrides or scales that choice.
// Master kill-switch — off also drops the particles already on screen.
extern bool g_hud_part_enable;
// Ignore the weather picker and always emit g_hud_part_type. The point of the
// knob: every other emitter needs a specific season, hour, z-level or weather to
// appear, which is unusable for tuning.
extern bool g_hud_part_force;
// Emitter index when forcing — matches lighting::hud_emitter_type's order
// (0 ember, 1 dust, 2 pollen, 3 snow, 4 leaf).
extern int g_hud_part_type;
// Per-emitter kill-switches, applied to the WEATHER-PICKED type (a forced type
// always emits — you asked for it explicitly). Lets a single effect be silenced
// without disabling the layer.
extern bool g_hud_part_ember_enable;
extern bool g_hud_part_dust_enable;
extern bool g_hud_part_pollen_enable;
extern bool g_hud_part_snow_enable;
extern bool g_hud_part_leaf_enable;
// Emit during rain too. Off by default because the world-space rain_effect
// already fills the screen; on, rain picks the dust emitter as wind-blown spray.
extern bool g_hud_part_in_rain;
// Keep particles OFF the map viewport, so a drifting mote is never mistaken for
// an item or a creature. On by default — the layer is HUD dressing, and the one
// place it must not sit is the tiles the player is reading. Off draws over
// everything (useful for judging density).
extern bool g_hud_part_mask_play;
// Multipliers on the picked emitter's parameters. 1.0 = authored look.
// Spawn rate (particles/second) — the steady-state count is rate x lifetime.
extern float g_hud_part_rate_scale;
// Alpha. Values above 1 clip to opaque and are useful for spotting the layer.
extern float g_hud_part_alpha_scale;
// Spawn diameter.
extern float g_hud_part_size_scale;
// Velocity. Lifetimes are derived from travel distance, so this changes how fast
// a particle crosses the screen, not whether it makes it across.
extern float g_hud_part_speed_scale;
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
// Hover tile highlight and dotted line (CPU-side; see hover-tile-effect-plan.md).
extern bool   g_hover_highlight_enable;
extern float  g_hover_highlight_color[4];
extern float  g_hover_highlight_thickness;
extern float  g_hover_highlight_corner_len;
extern bool   g_hover_highlight_pulse;
extern float  g_hover_highlight_pulse_speed;
extern bool   g_hover_line_enable;
extern float  g_hover_line_color[4];
extern float  g_hover_line_dot_size;
extern float  g_hover_line_dot_spacing;
extern bool   g_hover_line_fade_ends;
// Depth extrude (DitW) global multipliers.
extern float g_depth_lean_str;
extern float g_depth_dark_str;
// Sound wavefront ring tuning.
extern float g_sound_wave_speed;       // tiles/sec expansion rate, default 12.0
extern float g_sound_wave_min_radius; // min tile radius, default 6.0
extern float g_sound_wave_max_radius;  // max tile radius, default 48.0

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

/// True when the animated sound-pulse wavefront VFX (dev_test_lights::sound_pulses
/// — footsteps/melee/gunfire/ballistics, or an F4-panel test spawn) should be
/// rendered. `player_in_stealth` is the caller's `movement_mode_is( CMM_STEALTH )`
/// result and is the only gameplay gate: outside stealth the rings stay hidden.
/// An open F4 dev panel bypasses it so debug-spawned test pulses stay visible;
/// the "spawn sounds on click" checkbox deliberately does not, because it is a
/// click-to-place debug toggle with no bearing on what the player should see.
auto sound_pulses_visible( bool player_in_stealth ) -> bool;
/// Mutable ref to the "spawn sounds on click" checkbox state (default off — it
/// swallows world left-clicks and queues real, monster-audible sounds).
auto sound_place_mode() -> bool &; // *NOPAD*
void rml_tick();

// Place a static dev test light at the hovered world tile. Returns true if it placed
// one (panel open + place-mode on) so the caller can consume the click. Was inline in
// the ImGui Effects tab; now driven by a world click from sdl_input.
bool place_test_light();


/// Place a test sound at the hovered world tile. Returns true if placed.
bool place_test_sound();

} // namespace sdl_lighting_devui
