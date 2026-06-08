// MUST precede any game header: debug.h defines a function-like `DebugLog`
// macro that otherwise mangles ImGui::DebugLog in imgui.h (same reason
// imgui_layer.cpp includes imgui.h before debug.h).
#include "imgui.h"

#include "sdl_lighting_devui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "avatar.h"
#include "cata_tiles.h"
#include "game.h"
#include "map.h"
#include "lighting/gpu_emitter.h"
#include "lighting/imgui_layer.h"
#include "lighting/render_state.h"
#include "lighting/sdf_pass.h"
#include "lighting/sprite_batcher.h"
#include "mapdata.h"
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
bool   g_shadow_debug = false;
uint32_t g_current_dbg_mode = 0u;
float g_emitter_scale = 1.0f;
float g_sun_scale = 1.0f;
float g_sky_scale = 1.0f;
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

    static const char *mode_names[] = {
        "off", "ambient", "emitter", "sun", "sky", "total", "SDF", "sky_vis", "emit_bw",
        "normal", "AO", "shadow mask"
    };
    int mode = static_cast<int>( g_current_dbg_mode );
    if( ImGui::Combo( "mode (F7)", &mode, mode_names, static_cast<int>( std::size( mode_names ) ) ) ) {
        g_current_dbg_mode = static_cast<uint32_t>( mode );
        g_dbg_params.debug_mode = g_current_dbg_mode;
    }

    ImGui::SeparatorText( "Cursor light (dev)" );
    ImGui::Checkbox( "omni light follows cursor", &cursor_light_emitter::enabled );
    ImGui::SliderFloat( "cursor radius (tiles)", &cursor_light_emitter::radius, 1.0f, 40.0f );
    ImGui::SliderFloat( "cursor intensity", &cursor_light_emitter::intensity, 0.0f, 5.0f );

    ImGui::SeparatorText( "Surface normals (A1)" );
    ImGui::SliderFloat( "normal amount", &g_dbg_params.nrm_amount, 0.0f, 10.0f );
    ImGui::SliderFloat( "normal relief (signed)", &g_dbg_params.nrm_relief, -6.0f, 6.0f );
    ImGui::SliderFloat( "normal elev (grazing)", &g_dbg_params.nrm_elev, 0.05f, 1.5f );

    ImGui::SeparatorText( "Light scales" );
    if( ImGui::SliderFloat( "emitter", &g_emitter_scale, 0.0f, 10.0f ) ) {
        g_dbg_params.emitter_scale = g_emitter_scale;
    }
    if( ImGui::SliderFloat( "sun", &g_sun_scale, 0.0f, 10.0f ) ) {
        g_dbg_params.sun_scale = g_sun_scale;
    }
    if( ImGui::SliderFloat( "sky", &g_sky_scale, 0.0f, 10.0f ) ) {
        g_dbg_params.sky_scale = g_sky_scale;
    }
    ImGui::SliderFloat( "indoor sky bleed", &g_skylight_bleed, 0.0f, 1.0f );
    ImGui::SliderFloat( "vision blur (tiles)", &g_vision_blur, 0.0f, 6.0f );

    ImGui::SeparatorText( "Tonemap (AgX)" );
    ImGui::SliderFloat( "exposure", &g_tonemap_exposure, 0.0f, 2.0f );
    ImGui::SliderFloat( "min EV", &g_tonemap_min_ev, -20.0f, 0.0f );
    ImGui::SliderFloat( "max EV", &g_tonemap_max_ev, 0.0f, 12.0f );
    ImGui::Checkbox( "bloom", &g_bloom_enable );
    ImGui::SliderFloat( "bloom threshold", &g_bloom_threshold, 0.0f, 4.0f );
    ImGui::SliderFloat( "bloom intensity", &g_bloom_intensity, 0.0f, 2.0f );

    ImGui::SeparatorText( "Volumetric sun shafts (C2)" );
    ImGui::Checkbox( "volumetric", &g_vol_enable );
    ImGui::SliderFloat( "vol density", &g_vol_density, 0.0f, 2.0f );
    ImGui::SliderFloat( "vol intensity", &g_vol_intensity, 0.0f, 4.0f );
    ImGui::SliderFloat( "vol reach (tiles)", &g_vol_reach, 1.0f, 24.0f );
    ImGui::SliderFloat( "vol shadow (lanes)", &g_vol_shadow, 0.0f, 1.0f );

    ImGui::SeparatorText( "Silhouette sun shadows (Phase 1/2)" );
    ImGui::Checkbox( "show shadow mask (debug blit)", &g_shadow_debug );
    ImGui::SliderFloat( "shadow mask strength", &g_dbg_params.shadow_mask_str, 0.0f, 1.0f );

    ImGui::SeparatorText( "Dither / GI / shadow" );
    ImGui::SliderFloat( "dither amt", &g_dbg_params.dither_amt, 0.0f, 1.0f );
    ImGui::SliderFloat( "dither bands", &g_dbg_params.dither_bands, 1.0f, 16.0f, "%.0f" );
    ImGui::SliderFloat( "GI strength", &g_dbg_params.gi_strength, 0.0f, 2.0f );
    if( ImGui::Button( "RC cascade readback (log stats)" ) ) {
        g_rc_readback = true;
    }
    ImGui::SliderFloat( "shadow k", &g_dbg_params.shadow_k, 0.0f, 32.0f );
    int steps = static_cast<int>( g_dbg_params.shadow_steps );
    if( ImGui::SliderInt( "shadow steps", &steps, 1, 64 ) ) {
        g_dbg_params.shadow_steps = static_cast<uint32_t>( std::max( 1, steps ) );
    }
    ImGui::SliderFloat( "SDF sharpness", &g_dbg_params.sdf_sharp, 0.0f, 1.0f );
    ImGui::SliderFloat( "ambient occlusion", &g_dbg_params.ao_strength, 0.0f, 1.0f );

    ImGui::SeparatorText( "Vision (Stoneshard)" );
    ImGui::SliderFloat( "vis curve", &g_dbg_params.vis_curve, 0.0f, 4.0f );
    ImGui::SliderFloat( "vis radius", &g_dbg_params.vis_radius, 0.0f, 40.0f, "%.1f" );
    ImGui::SliderFloat( "night floor", &g_dbg_params.night_floor, 0.0f, 0.30f );
    ImGui::SliderFloat( "day floor", &g_dbg_params.day_floor, 0.0f, 0.30f );

    ImGui::SeparatorText( "Tone grade (Stoneshard wash)" );
    ImGui::SliderFloat( "desaturate", &g_dbg_params.grade_desat, 0.0f, 1.0f );
    ImGui::SliderFloat( "cool tint", &g_dbg_params.grade_cool, 0.0f, 1.0f );
    ImGui::SliderFloat( "brightness", &g_dbg_params.grade_bright, 0.0f, 1.5f );

    ImGui::SeparatorText( "Memory fade (effect 3)" );
    ImGui::SliderFloat( "mem dim", &g_dbg_params.mem_dim, 0.0f, 1.0f );
    ImGui::SliderFloat( "mem radius", &g_dbg_params.mem_radius, 1.0f, 60.0f, "%.0f" );

    ImGui::SeparatorText( "Foliage sway" );
    ImGui::SliderFloat( "sway amplitude", &g_dbg_params.sway_amp, 0.0f, 8.0f, "%.1f px" );
    ImGui::SliderFloat( "sway frequency", &g_dbg_params.sway_freq, 0.0f, 3.0f, "%.1f Hz" );

    // Diagnostics — the former top-left curses HUD, now read-only ImGui text.
    // Reads s_emo (file-scope, populated by the g_dbg_lighting overlay block in
    // refresh_display BEFORE new_frame() runs, so values are current this frame).
    ImGui::SeparatorText( "Diagnostics" );
    if( !g_dbg_lighting ) {
        ImGui::TextDisabled( "enable Debug HUD (F5) for live readout" );
    } else {
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

    ImGui::End();
}

}  // namespace sdl_lighting_devui
