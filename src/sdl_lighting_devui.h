#pragma once

#include <climits>
#include <cstdint>
#include <string>
#include <vector>

namespace lighting
{

struct debug_params;
struct gpu_emitter;
struct vol_params;

}  // namespace lighting

// Debug overlay state — saved from the previous frame, drawn this frame.
struct TileCoordGlyph {
    float x, y;
    std::string text;
};

struct EmitterOverlayState {
    std::vector<lighting::gpu_emitter> snap;
    float cam_off_x = 0.f, cam_off_y = 0.f, tile_px = 32.f;
    float op_x = 0.f, op_y = 0.f;
    int player_x = 0, player_y = 0, player_z = 0;
    int screen_w = 0, screen_h = 0;
    int map_origin_x = 0, map_origin_y = 0;
    int draw_off_px_x = 0, draw_off_px_y = 0;
    uint32_t last_n_emit_pushed = 0;
    float sdf_at_player = -1.f;
    float trans_at_player = -1.f;
    int   sdf_W_at_submit = 0;
    size_t sdf_size_at_submit = 0;
    std::vector<TileCoordGlyph> tile_labels;
    int cached_player_x = INT_MIN, cached_player_y = INT_MIN;
    float cached_cam_off_x = 0.f, cached_cam_off_y = 0.f;
    float cached_tile_px = 0.f;
    int cached_screen_w = 0, cached_screen_h = 0;
};

extern EmitterOverlayState s_emo;

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
extern bool   g_bloom_enable;
extern float  g_bloom_threshold;
extern float  g_bloom_intensity;
// Volumetric sun-shaft controls.
extern bool   g_vol_enable;
extern float  g_vol_density;
extern float  g_vol_intensity;
extern float  g_vol_shadow;
extern float  g_vol_reach;
extern lighting::vol_params g_vol_params;
// Silhouette sun-shadow mask kill-gate.
extern bool g_shadow_debug;
// Current debug mode display (0-7, cycles through modes).
extern uint32_t g_current_dbg_mode;
// Scale factors for individual light contributions.
extern float g_emitter_scale;
extern float g_sun_scale;
extern float g_sky_scale;
// Indoor daylight bleed strength.
extern float g_skylight_bleed;
// Vision-mask blur (tiles).
extern float g_vision_blur;

// Main-menu decorative-emitter tuning.
namespace menu_emitter_tuning
{

extern float radius_input;
extern float pos_x;
extern float pos_y;
extern int   pos_preset;
extern bool  blue_backdrop;

}  // namespace menu_emitter_tuning

// Dev cursor light (F4 panel).
namespace cursor_light_emitter
{

extern bool  enabled;
extern float radius;
extern float intensity;
extern float wx, wy, wz;

}  // namespace cursor_light_emitter

namespace sdl_lighting_devui
{

// Draw the "Lighting Debug (F4)" Dear ImGui panel. Registered as the
// imgui_layer dev-UI callback; reads the shared globals declared above.
void draw();

}  // namespace sdl_lighting_devui
