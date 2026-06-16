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
#include "game.h"
#include "map.h"
#include "lighting/gpu_emitter.h"
#include "lighting/imgui_layer.h"
#include "lighting/render_state.h"
#include "lighting/rmlui_layer.h"
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
float wx = 0.0f, wy = 0.0f, wz = 0.0f;
}  // namespace cursor_light_emitter

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
        "normal", "AO", "shadow mask"
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
    }
    if( ImGui::CollapsingHeader( "System menus" ) ) {
        ImGui::Checkbox( "options", &options_rmlui_enabled() );
        ImGui::Checkbox( "worldfactory", &worldfactory_rmlui_enabled() );
        ImGui::Checkbox( "main menu", &main_menu_rmlui_enabled() );
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
    if( ImGui::BeginTabItem( "RmlUi" ) ) {
        draw_rmlui_tab();
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
