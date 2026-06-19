// MUST precede any game header: debug.h defines a function-like `DebugLog`
// macro that otherwise mangles ImGui::DebugLog in imgui.h (same reason
// imgui_layer.cpp includes imgui.h before debug.h).
#include "imgui.h"

#include "sdl_lighting_devui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include <string>
#include <unordered_map>

#include "avatar.h"
#include "cata_tiles.h"
#include "creature.h"
#include "game.h"
#include "map.h"
#include "lighting/dev_test_lights.h"
#include "lighting/gpu_emitter.h"
#include "lighting/imgui_layer.h"
#include "lighting/render_state.h"
#include "lighting/rmlui_layer.h"
#include "lighting/rmlui_proc_texture.h"
#include "rml_util.h"
#include "ui_theme.h"
#include "lighting/sdf_pass.h"
#include "lighting/sprite_batcher.h"
#include "mapdata.h"
#include "ui.h"
#include "rml_screen.h"
#include "weather.h"
#include "worldfactory.h"

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
bool   g_bloom_enable    = true;
float  g_bloom_threshold = 1.0f;
float  g_bloom_intensity = 0.5f;
bool   g_vol_enable    = false;
float  g_vol_density   = 0.3f;
float  g_vol_intensity = 1.0f;
float  g_vol_shadow    = 0.0f;
float  g_vol_reach     = 8.0f;
bool   g_rain_enable   = true;
float  g_rain_intensity = 0.5f;
float  g_spec_strength = 0.0f;     // wet specular glint (0=off); × rain intensity per-frame
bool   g_shadow_debug = false;
uint32_t g_current_dbg_mode = 0u;
float g_skylight_bleed = 0.5f;
float g_vision_blur = 1.5f;
// Hover-outline (HOVER_OUTLINE_PLAN.md) — CPU-side, no shader cbuffer.
bool  g_outline_enable    = true;
float g_outline_thickness = 0.06f;   // ring radius as fraction of tile width
float g_outline_alpha     = 1.0f;
float g_outline_alpha_cut = 0.6f;    // silhouette mask cutoff (drops baked shadows)
float g_outline_col_hostile[4]  = { 1.00f, 0.24f, 0.24f, 1.0f };
float g_outline_col_neutral[4]  = { 1.00f, 0.86f, 0.24f, 1.0f };
float g_outline_col_friendly[4] = { 0.31f, 0.90f, 0.31f, 1.0f };
float g_outline_col_self[4]     = { 0.31f, 0.86f, 1.00f, 1.0f };

namespace menu_emitter_tuning
{
float radius_input = 100.0f;
float pos_x        = 8.5f;
float pos_y        = 4.5f;
int   pos_preset   = 0;
bool  blue_backdrop = true;
}  // namespace menu_emitter_tuning

namespace cursor_light_emitter
{
bool  enabled   = false;
float radius    = 8.0f;
float intensity = 1.5f;
float color[3]  = { 1.0f, 1.0f, 1.0f }; // RGB tint (saturate one channel to see colored GI bounce)
float wx = 0.0f, wy = 0.0f, wz = 0.0f;
}  // namespace cursor_light_emitter

// Storage for the click-to-place dev test lights (declared in dev_test_lights.h).
namespace dev_test_lights
{
bool  place_mode = false;
float hover_wx = 0.0f, hover_wy = 0.0f, hover_wz = 0.0f;
std::vector<light> lights;
}  // namespace dev_test_lights

namespace sdl_lighting_devui
{

// Slider + inline "R" revert button. The first time each label is drawn we
// snapshot its starting value; the button restores that snapshot ("original").
// Snapshots are keyed by label, so every wrapped slider reverts independently.
static bool dbg_slider( const char *label, float *v, float lo, float hi,
                        const char *fmt = "%.3f" )
{
    static std::unordered_map<std::string, float> orig;
    auto it = orig.find( label );
    if( it == orig.end() ) {
        it = orig.emplace( label, *v ).first;
    }
    ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - 30.0f );
    bool changed = ImGui::SliderFloat( label, v, lo, hi, fmt );
    ImGui::SameLine();
    ImGui::PushID( label );
    if( ImGui::SmallButton( "R" ) ) {
        *v = it->second;
        changed = true;
    }
    if( ImGui::IsItemHovered() ) {
        ImGui::SetTooltip( "revert to %g", it->second );
    }
    ImGui::PopID();
    return changed;
}

static bool dbg_slider_int( const char *label, int *v, int lo, int hi )
{
    static std::unordered_map<std::string, int> orig;
    auto it = orig.find( label );
    if( it == orig.end() ) {
        it = orig.emplace( label, *v ).first;
    }
    ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - 30.0f );
    bool changed = ImGui::SliderInt( label, v, lo, hi );
    ImGui::SameLine();
    ImGui::PushID( label );
    if( ImGui::SmallButton( "R" ) ) {
        *v = it->second;
        changed = true;
    }
    if( ImGui::IsItemHovered() ) {
        ImGui::SetTooltip( "revert to %d", it->second );
    }
    ImGui::PopID();
    return changed;
}

// ColorEdit4 + inline "R" revert button, mirroring dbg_slider. Snapshots the
// colour the first time each name is drawn; the button restores that snapshot.
static bool dbg_color( const char *name, float rgba[4], ImGuiColorEditFlags flags )
{
    static std::unordered_map<std::string, std::array<float, 4>> orig;
    auto it = orig.find( name );
    if( it == orig.end() ) {
        it = orig.emplace( name, std::array<float, 4>{ rgba[0], rgba[1], rgba[2], rgba[3] } ).first;
    }
    bool changed = ImGui::ColorEdit4( name, rgba, flags );
    ImGui::SameLine();
    ImGui::PushID( name );
    if( ImGui::SmallButton( "R" ) ) {
        rgba[0] = it->second[0];
        rgba[1] = it->second[1];
        rgba[2] = it->second[2];
        rgba[3] = it->second[3];
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

// F4 "Theme" tab: live colour-wheel editor for data/gui/theme.json. RCSS-token
// edits re-apply immediately (reload_theme); game-colour edits apply on the next
// screen open (cache cleared). Save persists back to theme.json.
static void draw_theme_tab()
{
    ImGui::TextWrapped( "Live theme editor (data/gui/theme.json). RCSS colours apply "
                        "instantly; game text colours apply when a screen is reopened." );
    if( ImGui::Button( "Save to theme.json" ) ) {
        ui_theme::save();
    }
    ImGui::SameLine();
    if( ImGui::Button( "Reload from file" ) ) {
        ui_theme::load();
        clear_nc_color_cache();
        rmlui_layer::reload_theme();
    }

    const ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar |
                                      ImGuiColorEditFlags_AlphaPreviewHalf |
                                      ImGuiColorEditFlags_PickerHueWheel;

    if( ImGui::CollapsingHeader( "Panel / RCSS colours", ImGuiTreeNodeFlags_DefaultOpen ) ) {
        bool changed = false;
        for( const std::string &name : ui_theme::rcss_names() ) {
            float rgba[4];
            if( !ui_theme::get_rcss_rgba( name, rgba ) ) {
                continue;
            }
            if( dbg_color( name.c_str(), rgba, flags ) ) {
                ui_theme::set_rcss_rgba( name, rgba );
                changed = true;
            }
        }
        if( changed ) {
            rmlui_layer::reload_theme();
        }
    }

    if( ImGui::CollapsingHeader( "Game text colours" ) ) {
        bool changed = false;
        for( const std::string &name : ui_theme::game_color_names() ) {
            float rgba[4];
            if( !ui_theme::get_game_rgba( name, rgba ) ) {
                continue;
            }
            if( dbg_color( name.c_str(), rgba, flags ) ) {
                ui_theme::set_game_rgba( name, rgba );
                changed = true;
            }
        }
        if( changed ) {
            clear_nc_color_cache();  // takes effect when a screen is reopened
        }
    }
}

// F4 "Lighting" tab: the core GPU-lighting parameters (debug visualisation,
// light scales, tonemap/bloom, GI/shadow, vision, colour grade).
static void draw_lighting_tab()
{
    using namespace lighting_dbg_range;

    static const char *mode_names[] = {
        "off", "ambient", "emitter", "sun", "sky", "total", "SDF", "sky_vis", "emit_bw",
        "normal", "AO", "shadow mask", "GI", "sky access", "sun occ"
    };
    int mode = static_cast<int>( g_current_dbg_mode );
    if( ImGui::Combo( "mode (F7)", &mode, mode_names, static_cast<int>( std::size( mode_names ) ) ) ) {
        g_current_dbg_mode = static_cast<uint32_t>( mode );
        g_dbg_params.debug_mode = g_current_dbg_mode;
    }

    ImGui::SeparatorText( "Light scales" );
    dbg_slider( "emitter", &g_dbg_params.emitter_scale, SCALE_MIN, SCALE_MAX );
    dbg_slider( "sun", &g_dbg_params.sun_scale, SCALE_MIN, SCALE_MAX );
    dbg_slider( "sky", &g_dbg_params.sky_scale, SCALE_MIN, SCALE_MAX );
    dbg_slider( "indoor sky bleed", &g_skylight_bleed, 0.0f, 1.0f );
    dbg_slider( "vision blur (tiles)", &g_vision_blur, 0.0f, 6.0f );

    ImGui::SeparatorText( "Tonemap (AgX)" );
    dbg_slider( "exposure", &g_tonemap_exposure, 0.0f, 2.0f );
    dbg_slider( "min EV", &g_tonemap_min_ev, -20.0f, 0.0f );
    dbg_slider( "max EV", &g_tonemap_max_ev, 0.0f, 12.0f );
    ImGui::Checkbox( "bloom", &g_bloom_enable );
    dbg_slider( "bloom threshold", &g_bloom_threshold, 0.0f, 4.0f );
    dbg_slider( "bloom intensity", &g_bloom_intensity, 0.0f, 2.0f );

    ImGui::SeparatorText( "Dither / GI / shadow" );
    dbg_slider( "dither amt", &g_dbg_params.dither_amt, DAMT_MIN, DAMT_MAX );
    dbg_slider( "dither bands", &g_dbg_params.dither_bands, DBND_MIN, DBND_MAX, "%.0f" );
    dbg_slider( "GI strength", &g_dbg_params.gi_strength, GI_MIN, GI_MAX );
    if( ImGui::Button( "RC cascade readback (log stats)" ) ) {
        g_rc_readback = true;
    }
    dbg_slider( "shadow k", &g_dbg_params.shadow_k, 0.0f, 32.0f );
    int steps = static_cast<int>( g_dbg_params.shadow_steps );
    if( dbg_slider_int( "shadow steps", &steps, 1, 64 ) ) {
        g_dbg_params.shadow_steps = static_cast<uint32_t>( std::max( 1, steps ) );
    }
    // P1/P2: emitter density optimization knobs.
    dbg_slider( "light eps (march gate)", &g_dbg_params.light_eps, 0.0f, 0.05f, "%.4f" );
    int max_k = static_cast<int>( g_dbg_params.max_shadow_k );
    if( dbg_slider_int( "max shadow K/pixel", &max_k, 1, 64 ) ) {
        g_dbg_params.max_shadow_k = static_cast<float>( std::max( 1, max_k ) );
    }
    dbg_slider( "SDF sharpness", &g_dbg_params.sdf_sharp, 0.0f, 1.0f );
    dbg_slider( "ambient occlusion", &g_dbg_params.ao_strength, 0.0f, 1.0f );

    ImGui::SeparatorText( "Vision (Stoneshard)" );
    dbg_slider( "vis curve", &g_dbg_params.vis_curve, 0.0f, 4.0f );
    dbg_slider( "vis radius", &g_dbg_params.vis_radius, 0.0f, 40.0f, "%.1f" );
    dbg_slider( "night floor", &g_dbg_params.night_floor, 0.0f, 0.30f );
    dbg_slider( "day floor", &g_dbg_params.day_floor, 0.0f, 0.30f );

    ImGui::SeparatorText( "Tone grade (Stoneshard wash)" );
    dbg_slider( "desaturate", &g_dbg_params.grade_desat, 0.0f, 1.0f );
    dbg_slider( "cool tint", &g_dbg_params.grade_cool, 0.0f, 1.0f );
    dbg_slider( "brightness", &g_dbg_params.grade_bright, 0.0f, 1.5f );
}

// F4 "Effects" tab: atmosphere and surface effects layered on top of the core
// lighting (normals, volumetrics, rain, shadow mask, memory fade, sway) plus the
// dev cursor-light tool.
static void draw_effects_tab()
{
    ImGui::SeparatorText( "Cursor light (dev)" );
    ImGui::Checkbox( "omni light follows cursor", &cursor_light_emitter::enabled );
    dbg_slider( "cursor radius (tiles)", &cursor_light_emitter::radius, 1.0f, 40.0f );
    dbg_slider( "cursor intensity", &cursor_light_emitter::intensity, 0.0f, 5.0f );
    ImGui::ColorEdit3( "cursor color", cursor_light_emitter::color );

    // Click-to-place STATIC test lights (despawn when this panel closes). Unlike
    // the cursor light, these stay put — pin one beside a wall to study the
    // occluded side (direct shadow vs GI bounce around the corner). They reuse
    // the cursor brush's radius/intensity/colour. hover_* is updated each frame
    // by sdl_render_frame; we place on a world click (one that ImGui didn't eat).
    ImGui::Checkbox( "place test lights on click", &dev_test_lights::place_mode );
    if( dev_test_lights::place_mode
        && ImGui::IsMouseClicked( ImGuiMouseButton_Left )
        && !ImGui::GetIO().WantCaptureMouse ) {
        dev_test_lights::lights.push_back( dev_test_lights::light{
            dev_test_lights::hover_wx, dev_test_lights::hover_wy, dev_test_lights::hover_wz,
            cursor_light_emitter::radius, cursor_light_emitter::intensity,
            cursor_light_emitter::color[0], cursor_light_emitter::color[1],
            cursor_light_emitter::color[2] } );
    }
    ImGui::Text( "placed: %d", static_cast<int>( dev_test_lights::lights.size() ) );
    ImGui::SameLine();
    if( ImGui::Button( "clear placed" ) ) {
        dev_test_lights::lights.clear();
    }

    ImGui::SeparatorText( "Surface normals (A1)" );
    dbg_slider( "normal amount", &g_dbg_params.nrm_amount, 0.0f, 10.0f );
    dbg_slider( "normal relief (signed)", &g_dbg_params.nrm_relief, -6.0f, 6.0f );
    dbg_slider( "normal elev (grazing)", &g_dbg_params.nrm_elev, 0.05f, 1.5f );

    ImGui::SeparatorText( "Volumetric sun shafts (C2)" );
    ImGui::Checkbox( "volumetric", &g_vol_enable );
    dbg_slider( "vol density", &g_vol_density, 0.0f, 2.0f );
    dbg_slider( "vol intensity", &g_vol_intensity, 0.0f, 4.0f );
    dbg_slider( "vol reach (tiles)", &g_vol_reach, 1.0f, 24.0f );
    dbg_slider( "vol shadow (lanes)", &g_vol_shadow, 0.0f, 1.0f );

    ImGui::SeparatorText( "High-fidelity rain" );
    ImGui::Checkbox( "rain", &g_rain_enable );
    dbg_slider( "rain intensity", &g_rain_intensity, 0.0f, 1.0f );
    dbg_slider( "wet specular", &g_spec_strength, 0.0f, 3.0f );

    ImGui::SeparatorText( "Silhouette sun shadows (Phase 1/2)" );
    ImGui::Checkbox( "show shadow mask (debug blit)", &g_shadow_debug );
    dbg_slider( "shadow mask strength", &g_dbg_params.shadow_mask_str, 0.0f, 1.0f );

    ImGui::SeparatorText( "Memory fade (effect 3)" );
    dbg_slider( "mem dim", &g_dbg_params.mem_dim, 0.0f, 1.0f );
    dbg_slider( "mem radius", &g_dbg_params.mem_radius, 1.0f, 60.0f, "%.0f" );

    ImGui::SeparatorText( "Foliage sway" );
    dbg_slider( "sway amplitude", &g_dbg_params.sway_amp, 0.0f, 8.0f, "%.1f px" );
    dbg_slider( "sway frequency", &g_dbg_params.sway_freq, 0.0f, 3.0f, "%.1f Hz" );

    ImGui::SeparatorText( "Hover outline" );
    ImGui::Checkbox( "outline enable", &g_outline_enable );
    dbg_slider( "outline thickness", &g_outline_thickness, 0.0f, 0.25f, "%.3f" );
    dbg_slider( "outline alpha", &g_outline_alpha, 0.0f, 1.0f );
    dbg_slider( "outline mask cut", &g_outline_alpha_cut, 0.0f, 1.0f );
    const ImGuiColorEditFlags ocf = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha;
    dbg_color( "hostile", g_outline_col_hostile, ocf );
    dbg_color( "neutral", g_outline_col_neutral, ocf );
    dbg_color( "friendly", g_outline_col_friendly, ocf );
    dbg_color( "self (player)", g_outline_col_self, ocf );
}

// F4 "Animation" tab: live sprite-animation tuning (movement bob/slide, idle
// sway, hit reaction, attack lunge). "Live override" makes the panel own the
// tuning so edits aren't overwritten by the in-game options each frame.
static void draw_animation_tab()
{
    ImGui::TextWrapped( "Live override lets these knobs win over the in-game options "
                        "(and forces animations on). With it off the panel mirrors the "
                        "options, but edits get overwritten each frame." );
    ImGui::Checkbox( "live override (ignore options)", &debug_anim_override() );

    animation_tuning &t = debug_anim_tuning();

    ImGui::SeparatorText( "Effects on/off" );
    ImGui::Checkbox( "move bob/slide", &t.move_bob );
    ImGui::SameLine();
    ImGui::Checkbox( "idle sway", &t.breathing );
    ImGui::Checkbox( "hit reaction", &t.hit_reaction );
    ImGui::SameLine();
    ImGui::Checkbox( "attack lunge", &t.attack_lunge );

    ImGui::SeparatorText( "Movement" );
    dbg_slider( "bob amplitude (px)", &t.bob_amplitude, 0.f, 16.f );
    dbg_slider( "bob duration (s)", &t.bob_duration, 0.05f, 1.0f );
    dbg_slider( "bob frequency", &t.move_bob_freq, 1.f, 60.f );
    dbg_slider( "move tilt (deg)", &t.move_tilt_deg, 0.f, 15.f );
    dbg_slider( "slide duration (s)", &t.move_slide_dur, 0.02f, 0.5f );

    ImGui::SeparatorText( "Idle sway" );
    dbg_slider( "idle sway (px)", &t.idle_sway, 0.f, 6.f );
    dbg_slider( "idle frequency", &t.idle_freq, 0.f, 6.f );
    dbg_slider( "idle tilt (deg)", &t.idle_tilt_deg, 0.f, 6.f );
    dbg_slider( "idle vbob mult", &t.idle_vbob_mult, 0.f, 2.f );

    ImGui::SeparatorText( "Hit reaction" );
    dbg_slider( "hit push (px)", &t.hit_push, 0.f, 24.f );
    dbg_slider( "hit duration (s)", &t.hit_duration, 0.05f, 1.0f );
    dbg_slider( "hit burst total (s)", &t.hit_burst_total, 0.1f, 1.5f );
    dbg_slider( "hit flash intensity", &t.hit_flash_intensity, 0.f, 2.f );
    dbg_slider( "hit flash frac", &t.hit_flash_frac, 0.f, 1.f );
    dbg_slider( "hit frequency", &t.hit_freq, 1.f, 60.f );
    dbg_slider( "hit tilt (deg)", &t.hit_tilt_deg, 0.f, 20.f );

    ImGui::SeparatorText( "Attack lunge" );
    dbg_slider( "attack amplitude (px)", &t.attack_amplitude, 0.f, 16.f );
    dbg_slider( "attack duration (s)", &t.attack_duration, 0.05f, 1.0f );
    dbg_slider( "attack frequency", &t.attack_freq, 1.f, 60.f );
    dbg_slider( "ranged amp mult (signed)", &t.attack_ranged_mult, -2.f, 2.f );
    dbg_slider( "melee tilt (deg)", &t.attack_tilt_melee_deg, -15.f, 15.f );
    dbg_slider( "ranged tilt (deg)", &t.attack_tilt_ranged_deg, -15.f, 15.f );
}

// F4 "RmlUi" tab: global UI scale, CRT post-effects, and the per-screen
// migration toggles (grouped by screen category in collapsing headers).
static void draw_rmlui_tab()
{
    // Global UI scale for ALL RmlUi panels (font + dp spacing). <1 shrinks the UI.
    dbg_slider( "RmlUi UI scale", &rmlui_layer::ui_scale(), 0.5f, 1.5f, "%.2f" );

    // CRT post-effects (scanlines + vignette), applied live to any open RmlUi doc
    // that has a #crt-overlay element (options, worldfinalize). Tune here, then
    // bake the final values into theme.rcss.
    if( ImGui::CollapsingHeader( "CRT post-effect", ImGuiTreeNodeFlags_DefaultOpen ) ) {
        rmlui_layer::crt_params &c = rmlui_layer::crt();
        ImGui::Checkbox( "CRT effect (scanlines + vignette)", &c.enabled );
        dbg_slider( "scanline darkness", &c.scanline_alpha, 0.0f, 1.0f, "%.2f" );
        dbg_slider( "scanline pitch (px)", &c.scanline_pitch, 2.0f, 64.0f, "%.1f" );
        dbg_slider( "scanline thickness (px)", &c.scanline_thickness, 0.5f, 32.0f, "%.1f" );
        dbg_slider( "roll speed (px/s)", &c.roll_speed, 0.0f, 300.0f, "%.0f" );
        dbg_slider( "flicker", &c.flicker, 0.0f, 1.0f, "%.2f" );
        dbg_slider( "vignette", &c.vignette_alpha, 0.0f, 1.0f, "%.2f" );
    }

    // Per-screen migration toggles. Off = existing ImGui/curses menus; flip to
    // route a screen through the new RmlUi renderer in-game.
    ImGui::SeparatorText( "Route screens through RmlUi" );
    if( ImGui::CollapsingHeader( "Core widgets", ImGuiTreeNodeFlags_DefaultOpen ) ) {
        ImGui::Checkbox( "uilist", &uilist_rmlui_enabled() );
        ImGui::Checkbox( "query_popup", &query_popup_rmlui_enabled() );
        ImGui::Checkbox( "string_input", &string_input_rmlui_enabled() );
    }
    if( ImGui::CollapsingHeader( "Info screens" ) ) {
        ImGui::Checkbox( "missions", &missions_rmlui_enabled() );
        ImGui::Checkbox( "scores", &scores_rmlui_enabled() );
        ImGui::Checkbox( "help", &help_rmlui_enabled() );
        ImGui::Checkbox( "distraction mgr", &distraction_rmlui_enabled() );
        ImGui::Checkbox( "auto notes", &auto_note_rmlui_enabled() );
        ImGui::Checkbox( "diary", &diary_rmlui_enabled() );
    }
    if( ImGui::CollapsingHeader( "Character" ) ) {
        ImGui::Checkbox( "mutations", &mutations_rmlui_enabled() );
        ImGui::Checkbox( "bionics", &bionics_rmlui_enabled() );
    }
    if( ImGui::CollapsingHeader( "Items & inventory" ) ) {
        ImGui::Checkbox( "inventory (all selectors)", &inventory_rmlui_enabled() );
        ImGui::Checkbox( "advanced inventory", &advanced_inv_rmlui_enabled() );
        ImGui::Checkbox( "compare items", &compare_items_rmlui_enabled() );
        ImGui::Checkbox( "examine item", &examine_item_rmlui_enabled() );
        ImGui::Checkbox( "armor layers", &armor_layers_rmlui_enabled() );
        ImGui::Checkbox( "auto pickup", &autopickup_rmlui_enabled() );
    }
    if( ImGui::CollapsingHeader( "World interaction" ) ) {
        ImGui::Checkbox( "computer terminal", &computer_rmlui_enabled() );
        ImGui::Checkbox( "construction", &construction_rmlui_enabled() );
        ImGui::Checkbox( "crafting", &crafting_rmlui_enabled() );
        ImGui::Checkbox( "safemode", &safemode_rmlui_enabled() );
        ImGui::Checkbox( "trade", &trade_rmlui_enabled() );
        ImGui::Checkbox( "vending", &vending_rmlui_enabled() );
        ImGui::Checkbox( "npc dialogue", &dialogue_rmlui_enabled() );
        ImGui::Checkbox( "examine description", &description_view_rmlui_enabled() );
        ImGui::Checkbox( "faction manager", &faction_rmlui_enabled() );
        ImGui::Checkbox( "ranged targeting (2a)", &ranged_rmlui_enabled() );
    }
    if( ImGui::CollapsingHeader( "System menus" ) ) {
        ImGui::Checkbox( "options", &options_rmlui_enabled() );
        ImGui::Checkbox( "worldfactory", &worldfactory_rmlui_enabled() );
        ImGui::Checkbox( "main menu", &main_menu_rmlui_enabled() );
        ImGui::Checkbox( "load character (sigils)", &loadchar_rmlui_enabled() );
        ImGui::Checkbox( "new character", &newcharacter_rmlui_enabled() );
        ImGui::Checkbox( "overmap legend", &overmap_rmlui_enabled() );
        ImGui::Checkbox( "world text (SCT)", &world_text_rmlui_enabled() );
        if( world_text_rmlui_enabled() ) {
            ImGui::SliderInt( "wt font px", &rmlui_layer::world_text_px(), 8, 64 );
            ImGui::SliderFloat( "wt x offset", &rmlui_layer::world_text_dx(), -64.f, 64.f );
            ImGui::SliderFloat( "wt y offset", &rmlui_layer::world_text_dy(), -64.f, 64.f );
        }
        // Tier 7: the continuous sidebar HUD (NOT a modal screen). Slice 1 owns the
        // Stats panel only; with this ON, Stats renders via RmlUi while the rest of the
        // sidebar stays curses.
        ImGui::Checkbox( "sidebar HUD (stats)", &sidebar_hud_rmlui_enabled() );
    }
}

// F4 "Diagnostics" tab: the former top-left curses HUD, now read-only ImGui text.
// Reads s_emo (file-scope, populated by the g_dbg_lighting overlay block in
// refresh_display BEFORE new_frame() runs, so values are current this frame).
static void draw_diagnostics_tab()
{
    if( !g_dbg_lighting ) {
        ImGui::TextDisabled( "enable Debug HUD (F5) for live readout" );
        return;
    }
    auto &rs = lighting::get_render_state();
    const float tp = s_emo.tile_px > 0.f ? s_emo.tile_px : 32.f;
    const float sw = static_cast<float>( s_emo.screen_w );
    const float sh = static_cast<float>( s_emo.screen_h );

    ImGui::Text( "screen=%dx%d  tile_px=%.1f",
                 s_emo.screen_w, s_emo.screen_h, tp );

    const size_t cache_sz = g
                            ? get_map().access_cache( g->u.bub_pos().z() ).transparency_cache.size()
                            : 0;
    const int wanted = g
                       ? ( get_map().getmapsize() * SEEX )
                       * ( get_map().getmapsize() * SEEY )
                       : 0;
    ImGui::Text( "SDF pop=%d  rt=%dx%d  tex=%dx%d  cache=%zu/%d",
                 rs.sdf().populated() ? 1 : 0,
                 rs.sdf().map_w(), rs.sdf().map_h(),
                 rs.sdf().tex_w(), rs.sdf().tex_h(),
                 cache_sz, wanted );

    if( g && cache_sz > 0 ) {
        map &mm = get_map();
        const int H = mm.getmapsize() * SEEY;
        const auto &tc = mm.access_cache( g->u.bub_pos().z() ).transparency_cache;
        const int px = g->u.bub_pos().x();
        const int py = g->u.bub_pos().y();
        auto T = [&]( int x, int y ) -> float {
            const int i = x * H + y;
            return ( i >= 0 && i < static_cast<int>( tc.size() ) ) ? tc[i] : -1.f;
        };
        ImGui::Text( "trans@p=%.3f N=%.3f S=%.3f E=%.3f W=%.3f",
                     T( px, py ), T( px, py - 1 ), T( px, py + 1 ),
                     T( px + 1, py ), T( px - 1, py ) );
        ImGui::Text( "sdf@p=%.3f trans@p(submit)=%.3f sdfW=%d sz=%zu",
                     s_emo.sdf_at_player, s_emo.trans_at_player,
                     s_emo.sdf_W_at_submit, s_emo.sdf_size_at_submit );
    }

    ImGui::Text( "map_origin=(%d,%d)  draw_off_px=(%d,%d)",
                 s_emo.map_origin_x, s_emo.map_origin_y,
                 s_emo.draw_off_px_x, s_emo.draw_off_px_y );
    ImGui::Text( "cam_off=(%.2f,%.2f)  op=(%.0f,%.0f)",
                 s_emo.cam_off_x, s_emo.cam_off_y, s_emo.op_x, s_emo.op_y );
    ImGui::Text( "player=(%d,%d,%d)  emitters=%zu  pushed=%u",
                 s_emo.player_x, s_emo.player_y, s_emo.player_z,
                 s_emo.snap.size(), s_emo.last_n_emit_pushed );

    const float pscr_x = ( s_emo.player_x + s_emo.cam_off_x ) * tp + s_emo.op_x;
    const float pscr_y = ( s_emo.player_y + s_emo.cam_off_y ) * tp + s_emo.op_y;
    ImGui::Text( "player_screen=(%.1f,%.1f)  center=(%.1f,%.1f)",
                 pscr_x, pscr_y, sw * 0.5f, sh * 0.5f );
    const float dx = pscr_x - sw * 0.5f;
    const float dy = pscr_y - sh * 0.5f;
    ImGui::Text( "delta_to_center=(%.1f,%.1f)px  =(%.2f,%.2f)tiles",
                 dx, dy, dx / tp, dy / tp );

    if( !s_emo.snap.empty() ) {
        const lighting::gpu_emitter &e0 = s_emo.snap.front();
        const float ed_x = e0.pos_x - static_cast<float>( s_emo.player_x );
        const float ed_y = e0.pos_y - static_cast<float>( s_emo.player_y );
        const float ed   = std::sqrt( ed_x * ed_x + ed_y * ed_y );
        const char *in_r = ( ed < e0.radius ) ? "INSIDE" : "outside";
        ImGui::Text( "emit[0] pos=(%.1f,%.1f,%.1f) r=%.1f dist=%.2f %s",
                     e0.pos_x, e0.pos_y, e0.pos_z, e0.radius, ed, in_r );
    } else {
        ImGui::TextDisabled( "emit[0] (none)" );
    }
    ImGui::Text( "menu  F10:r_in=%.0f  F11:pos=(%.1f,%.1f)  F12:bgBlue=%s",
                 menu_emitter_tuning::radius_input,
                 menu_emitter_tuning::pos_x, menu_emitter_tuning::pos_y,
                 menu_emitter_tuning::blue_backdrop ? "ON" : "off" );
}

static void draw_runic_tab()
{
    lighting::runic_params &c = lighting::runic_cfg();
    bool changed = false;

    ImGui::TextWrapped( "Full control of the procedural runic frame. Edits apply live to every "
                        "panel border. Save persists as the game-wide default." );

    ImGui::SeparatorText( "Template" );
    {
        const char *names[] = { "Auto (top/bottom centred, sides thirds)",
                                "Centred", "Thirds", "Fixed-interval"
                              };
        int ti = c.force_template + 1;   // -1 (Auto) -> 0
        if( ImGui::Combo( "template", &ti, names, static_cast<int>( std::size( names ) ) ) ) {
            c.force_template = ti - 1;
            changed = true;
        }
    }

    ImGui::SeparatorText( "Seed" );
    {
        const char *names[] = { "Per-panel size (shipping)", "Fixed" };
        int si = c.use_fixed_seed ? 1 : 0;
        if( ImGui::Combo( "seed mode", &si, names, 2 ) ) {
            c.use_fixed_seed = ( si == 1 );
            changed = true;
        }
        if( c.use_fixed_seed ) {
            int sv = static_cast<int>( c.seed );
            if( ImGui::InputInt( "seed value", &sv ) ) {
                c.seed = static_cast<unsigned>( sv );
                changed = true;
            }
            ImGui::SameLine();
            if( ImGui::Button( "Randomize" ) ) {
                c.seed = c.seed * 1664525u + 1013904223u;   // LCG step
                changed = true;
            }
        }
    }

    ImGui::SeparatorText( "Colour" );
    {
        float col[3] = { c.col_r / 255.f, c.col_g / 255.f, c.col_b / 255.f };
        if( ImGui::ColorEdit3( "rune ink", col ) ) {
            c.col_r = static_cast<int>( std::lround( col[0] * 255.f ) );
            c.col_g = static_cast<int>( std::lround( col[1] * 255.f ) );
            c.col_b = static_cast<int>( std::lround( col[2] * 255.f ) );
            changed = true;
        }
    }

    ImGui::SeparatorText( "Geometry" );
    changed |= dbg_slider_int( "ring (corner/edge depth)", &c.ring, 4, 48 );
    changed |= dbg_slider_int( "glyph scale", &c.glyph_scale, 1, 4 );
    changed |= dbg_slider_int( "band top", &c.band_top, 0, 20 );
    changed |= dbg_slider_int( "div top", &c.div_top, 0, 20 );
    changed |= dbg_slider_int( "div bot", &c.div_bot, 2, 40 );
    changed |= dbg_slider_int( "frame inset (F9/F10)", &c.frame_inset, 0, 40 );
    changed |= dbg_slider_int( "fill %", &c.fill_pct, 0, 99 );

    ImGui::SeparatorText( "Boxes & dividers" );
    changed |= dbg_slider_int( "wall", &c.wall, 1, 4 );
    changed |= dbg_slider_int( "divider width", &c.divw, 1, 6 );
    changed |= dbg_slider_int( "pad", &c.pad, 0, 10 );
    changed |= dbg_slider_int( "glyph gap", &c.ggap, 0, 10 );
    changed |= dbg_slider_int( "inner gap", &c.gapi, 0, 8 );

    ImGui::SeparatorText( "Layout" );
    changed |= dbg_slider_int( "edge length % (lower = shorter)", &c.border_frac, 0, 100 );
    changed |= dbg_slider_int( "rule gap", &c.rgap, 0, 16 );
    changed |= dbg_slider_int( "pitch (fixed-interval)", &c.pitch, 60, 512 );
    changed |= dbg_slider_int( "unit (default edge len)", &c.unit, 64, 512 );

    ImGui::SeparatorText( "Corrosion (worn edges)" );
    changed |= dbg_slider_int( "corrode % (0 = off)", &c.corrode_pct, 0, 99 );
    changed |= dbg_slider_int( "pit size (noise grid)", &c.corrode_grid, 1, 16 );
    changed |= dbg_slider_int( "reach into band", &c.corrode_reach, 1, 10 );
    changed |= dbg_slider_int( "rim grit %", &c.corrode_grit, 0, 99 );

    ImGui::SeparatorText( "Edge taper (trailing dots)" );
    changed |= dbg_slider_int( "taper dots (0 = flat)", &c.taper_dots, 0, 1 );
    changed |= dbg_slider_int( "taper gap (1st; 2nd ~2x)", &c.taper_gap, 1, 12 );

    ImGui::SeparatorText( "Declutter small panels" );
    changed |= dbg_slider_int( "small edge px (sides drop runes)", &c.rune_small_px, 0, 500 );

    ImGui::SeparatorText( "Regenerate / persist" );
    static bool auto_regen = true;
    ImGui::Checkbox( "auto-regenerate on edit", &auto_regen );
    if( changed && auto_regen ) {
        c.regen++;
    }
    if( ImGui::Button( "Generate" ) ) {
        c.regen++;
    }
    ImGui::SameLine();
    if( ImGui::Button( "Save (config/runic_frame.json)" ) ) {
        lighting::save_runic_cfg();
    }
    ImGui::SameLine();
    if( ImGui::Button( "Reload" ) ) {
        lighting::load_runic_cfg();
        c.regen++;
    }
    ImGui::SameLine();
    if( ImGui::Button( "Reset to defaults" ) ) {
        const unsigned keep = c.regen;
        c = lighting::runic_params{};
        c.regen = keep + 1;   // preserve+bump so the cache still busts
    }
}

void draw()
{
    // Closing via the title-bar X flips visible() too (same flag as F4).
    if( !ImGui::Begin( "Lighting Debug (F4)", &imgui_layer::visible() ) ) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox( "Debug HUD active (F5)", &g_dbg_lighting );
    ImGui::SameLine();
    ImGui::Checkbox( "Shader heatmap (F6)", &g_dbg_lighting_shader );

    if( !ImGui::BeginTabBar( "f4_tabs" ) ) {
        ImGui::End();
        return;
    }
    if( ImGui::BeginTabItem( "Lighting" ) ) {
        draw_lighting_tab();
        ImGui::EndTabItem();
    }
    if( ImGui::BeginTabItem( "Effects" ) ) {
        draw_effects_tab();
        ImGui::EndTabItem();
    }
    if( ImGui::BeginTabItem( "Animation" ) ) {
        draw_animation_tab();
        ImGui::EndTabItem();
    }
    if( ImGui::BeginTabItem( "RmlUi" ) ) {
        draw_rmlui_tab();
        ImGui::EndTabItem();
    }
    if( ImGui::BeginTabItem( "Runic Frame" ) ) {
        draw_runic_tab();
        ImGui::EndTabItem();
    }
    if( ImGui::BeginTabItem( "Theme" ) ) {
        draw_theme_tab();
        ImGui::EndTabItem();
    }
    if( ImGui::BeginTabItem( "Diagnostics" ) ) {
        draw_diagnostics_tab();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();

    ImGui::End();
}

}  // namespace sdl_lighting_devui
