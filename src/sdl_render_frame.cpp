// MUST precede any game header: debug.h defines a function-like `DebugLog`
// macro that otherwise mangles ImGui::DebugLog in imgui.h (same reason
// imgui_layer.cpp includes imgui.h before debug.h).
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "avatar.h"
#include "calendar.h"
#include "cached_options.h"
#include "cata_tiles.h"
#include "debug.h"
#include "game.h"
#include "output.h"
#include "point.h"
#include "sdl_display.h"
#include "sdl_font.h"
#include "sdl_fonts.h"
#include "sdl_geometry.h"
#include "sdl_lighting_devui.h"
#include "sdl_wrappers.h"
#include "lighting/frame_build.h"
#include "lighting/imgui_layer.h"
#include "lighting/rmlui_layer.h"
#include "lighting/render_state.h"
#include "lighting/sdf_pass.h"
#include "weather.h"
#include "weather_type.h"
#include "worldfactory.h"

#define dbg(x) DebugLogFL((x),DC::SDL)

using namespace std::literals;

// Per-frame volumetric inputs: written in assemble_light_inputs, consumed in
// render_world_pass_w. Both live in this TU, so file-local (was a dev-UI global).
static lighting::vol_params g_vol_params;

// Full-screen identity-blit quad (origin, full UV, white tint, no rotation).
// Callers override tint as needed (e.g. the menu backdrop).
static lighting::sprite_instance fullscreen_quad( float w, float h )
{
    lighting::sprite_instance q{};
    q.dst_w  = w;     q.dst_h  = h;
    q.src_uw = 1.f;   q.src_vh = 1.f;
    q.tint_r = 1.f;   q.tint_g = 1.f;   q.tint_b = 1.f;   q.tint_a = 1.f;
    return q;
}

// Frame phase helpers — file-local, orchestrated by refresh_display (end of file).
// Forward-declared (static) so calls resolve regardless of definition order and
// the definitions below take internal linkage.
static auto begin_frame( lighting::render_state &rs ) -> std::optional<lighting::frame_context>;
static auto build_lighting( lighting::render_state &rs ) -> bool;
static auto flush_and_gather_rc( lighting::render_state &rs, lighting::frame_context &ctx,
                                 bool rc_rebuild ) -> void;
static auto assemble_light_inputs( lighting::render_state &rs, lighting::frame_context &ctx ) -> void;
static auto maybe_push_menu_background( lighting::render_state &rs,
                                        lighting::frame_context &ctx ) -> void;
static auto draw_lighting_overlays( lighting::render_state &rs, lighting::frame_context &ctx ) -> void;
static auto composite_ui_pass_a( lighting::render_state &rs, lighting::frame_context &ctx,
                                 int proj_w, int proj_h ) -> void;
static auto render_world_pass_w( lighting::render_state &rs, lighting::frame_context &ctx,
                                 int proj_w, int proj_h ) -> void;
static auto tonemap_pass_t( lighting::render_state &rs, lighting::frame_context &ctx ) -> void;
static auto composite_swapchain_pass_b( lighting::render_state &rs, lighting::frame_context &ctx,
                                        int proj_w, int proj_h ) -> void;

auto begin_frame( lighting::render_state &rs ) -> std::optional<lighting::frame_context>
{
    if( test_mode ) {
        return std::nullopt;
    }
    if( !rs.ready() ) {
        return std::nullopt;
    }
    if( !imgui_layer::ready() ) {
        imgui_layer::init( rs.device().window_ptr(), rs.device().raw() );
        imgui_layer::set_dev_ui( sdl_lighting_devui::draw );
    }
    // RmlUi spike (Phase 1): lazy init beside ImGui; init() self-guards so this
    // only truly attempts once. Render/input not wired yet.
    if( !rmlui_layer::ready() ) {
        rmlui_layer::init( rs.device() );
    }

    rs.tile_batcher().begin_frame();
    rs.ui_batcher().begin_frame();
    rs.fonts().begin_frame();

    lighting::frame_context ctx = rs.device().begin_frame();
    if( !ctx.valid() ) {
        return std::nullopt;
    }
    if( !ctx.swapchain_tex ) {
        rs.device().submit_frame( ctx );
        return std::nullopt;
    }
    return ctx;
}

auto build_lighting( lighting::render_state &rs ) -> bool
{
    bool rc_rebuild = false;
    if( !rs.collector() ) {
        return rc_rebuild;
    }

    static time_point last_turn = calendar::before_time_starts;
    static int        last_z = INT_MIN;
    static point      last_origin{ INT_MIN, INT_MIN };
    bool rebuild_pertile = true;
    if( g && world_generator && world_generator->active_world ) {
        const time_point now = calendar::turn;
        const int z = g->u.bub_pos().z();
        const point origin = tilecontext
                             ? tilecontext->get_tile_map_origin().raw()
                             : point{ INT_MIN, INT_MIN };
        rebuild_pertile = imgui_layer::visible()
                          || now != last_turn || z != last_z
                          || origin != last_origin;
        if( rebuild_pertile ) {
            last_turn = now;
            last_z = z;
            last_origin = origin;
        }
    }

    if( cursor_light_emitter::enabled && g && tilecontext
        && world_generator && world_generator->active_world ) {
        float msx = 0.0f, msy = 0.0f;
        SDL_GetMouseState( &msx, &msy );
        const point o  = tilecontext->get_tile_map_origin().raw();
        const point op = tilecontext->get_drawing_pixel_offset();
        const int   tw = std::max( 1, tilecontext->get_tile_width() );
        const int   th = std::max( 1, tilecontext->get_tile_height() );
        cursor_light_emitter::wx = ( msx - static_cast<float>( op.x ) )
                                   / static_cast<float>( tw ) + static_cast<float>( o.x );
        cursor_light_emitter::wy = ( msy - static_cast<float>( op.y ) )
                                   / static_cast<float>( th ) + static_cast<float>( o.y );
        cursor_light_emitter::wz = static_cast<float>( g->u.bub_pos().z() );
    }

    lighting::frame_lighting_result fr =
        lighting::build_and_submit_lighting( rs, rebuild_pertile, g_dbg_lighting,
                                             g_skylight_bleed, g_vision_blur );
    rc_rebuild = fr.built_pertile;
    if( fr.built_pertile ) {
        s_emo.sdf_at_player      = fr.sdf_at_player;
        s_emo.trans_at_player    = fr.trans_at_player;
        s_emo.sdf_W_at_submit    = fr.sdf_W;
        s_emo.sdf_size_at_submit = fr.sdf_size;
    }
    if( g_dbg_lighting ) {
        s_emo.snap = std::move( fr.snapshot_copy );
    }
    return rc_rebuild;
}

auto flush_and_gather_rc( lighting::render_state &rs,
                          lighting::frame_context &ctx, bool rc_rebuild ) -> void
{
    if( rs.collector() ) {
        rs.collector()->flush_to_render_cb( ctx.cmd_buffer );
    }

    if( rc_rebuild && rs.sdf().populated() && rs.rc().ready()
        && rs.collector() && rs.sdf().sdf_buffer() ) {
        lighting::rc_params rp{};
        rp.emitter_count = static_cast<std::uint32_t>( std::max( 0, rs.collector()->last_count() ) );
        rp.map_w         = static_cast<std::uint32_t>( rs.sdf().map_w() );
        rp.map_h         = static_cast<std::uint32_t>( rs.sdf().map_h() );
        rp.current_z     = g ? static_cast<float>( g->u.bub_pos().z() ) : 0.0f;
        rp.shadow_k      = g_dbg_params.shadow_k;
        rp.shadow_steps  = g_dbg_params.shadow_steps;
        rs.rc().record( ctx.cmd_buffer,
                        rs.collector()->emitter_buffer(), rs.sdf().sdf_buffer(),
                        rp.map_w, rp.map_h, rp );
    }

    if( g_rc_readback ) {
        g_rc_readback = false;
        if( rs.rc().ready() && rs.sdf().populated() ) {
            rs.rc().debug_log_stats( static_cast<std::uint32_t>( rs.sdf().map_w() ),
                                     static_cast<std::uint32_t>( rs.sdf().map_h() ) );
        }
    }
}

auto assemble_light_inputs( lighting::render_state &rs,
                            lighting::frame_context &ctx ) -> void
{
    if( !rs.collector() ) {
        return;
    }

    lighting::render_state::frame_light_inputs in{};
    in.tile_pixel_size = tilecontext
                         ? static_cast<float>( tilecontext->get_tile_width() )
                         : 32.0f;
    in.z_level         = g ? static_cast<float>( g->u.bub_pos().z() ) : 0.0f;
    in.ambient         = 0.05f;

    if( g && tilecontext && in.tile_pixel_size > 0.0f ) {
        const point map_origin  = tilecontext->get_tile_map_origin().raw();
        const point draw_offset = tilecontext->get_drawing_pixel_offset();
        in.camera_off_x = static_cast<float>( draw_offset.x ) / in.tile_pixel_size
                          - static_cast<float>( map_origin.x );
        in.camera_off_y = static_cast<float>( draw_offset.y ) / in.tile_pixel_size
                          - static_cast<float>( map_origin.y );
        if( g_dbg_lighting ) {
            s_emo.cam_off_x = in.camera_off_x;
            s_emo.cam_off_y = in.camera_off_y;
            s_emo.tile_px   = in.tile_pixel_size;
            s_emo.op_x      = static_cast<float>( draw_offset.x );
            s_emo.op_y      = static_cast<float>( draw_offset.y );
            s_emo.player_x  = g->u.bub_pos().x();
            s_emo.player_y  = g->u.bub_pos().y();
            s_emo.player_z  = g->u.bub_pos().z();
            s_emo.screen_w  = static_cast<int>( ctx.swapchain_w );
            s_emo.screen_h  = static_cast<int>( ctx.swapchain_h );
            s_emo.map_origin_x = map_origin.x;
            s_emo.map_origin_y = map_origin.y;
            s_emo.draw_off_px_x = draw_offset.x;
            s_emo.draw_off_px_y = draw_offset.y;
        }
    } else if( g_dbg_lighting ) {
        s_emo.cam_off_x = 0.f;
        s_emo.cam_off_y = 0.f;
        s_emo.tile_px   = in.tile_pixel_size;
        s_emo.op_x      = 0.f;
        s_emo.op_y      = 0.f;
        s_emo.player_x  = 0;
        s_emo.player_y  = 0;
        s_emo.player_z  = 0;
        s_emo.screen_w  = static_cast<int>( ctx.swapchain_w );
        s_emo.screen_h  = static_cast<int>( ctx.swapchain_h );
        s_emo.map_origin_x = 0;
        s_emo.map_origin_y = 0;
        s_emo.draw_off_px_x = 0;
        s_emo.draw_off_px_y = 0;
    }

    const float sun_hour = g ? hour_of_day<float>( calendar::turn ) : 12.f;
    in.sun = lighting::make_sun_params( sun_hour );
    float weather_mult = 1.0f;
    if( g ) {
        const float base = sunlight( calendar::turn, false );
        const weather_type_id wid = get_weather().weather_id;
        if( base > 1.0f && wid.is_valid() ) {
            const int mod = wid->light_modifier;
            weather_mult = std::clamp( ( base + static_cast<float>( mod ) ) / base,
                                       0.0f, 1.0f );
        }
        in.sun.sun_intensity *= weather_mult;
        in.sun.sky_intensity *= weather_mult;
    }
    in.sun.sp_pad = g_dbg_lighting_shader ? 1.0f : 0.0f;

    g_vol_params.tile_pixel_size = in.tile_pixel_size;
    g_vol_params.camera_off_x    = in.camera_off_x;
    g_vol_params.camera_off_y    = in.camera_off_y;
    g_vol_params.current_z       = in.z_level;
    g_vol_params.sun_dir_x       = in.sun.sun_dir_x;
    g_vol_params.sun_dir_y       = in.sun.sun_dir_y;
    g_vol_params.sun_intensity   = in.sun.sun_intensity;
    g_vol_params.sun_r           = in.sun.sun_r;
    g_vol_params.sun_g           = in.sun.sun_g;
    g_vol_params.sun_b           = in.sun.sun_b;
    g_vol_params.shadow_k        = in.debug.shadow_k;
    g_vol_params.shadow_steps    = in.debug.shadow_steps;
    g_vol_params.sdf_map_w       = static_cast<std::uint32_t>( rs.sdf().map_w() );
    g_vol_params.sdf_map_h       = static_cast<std::uint32_t>( rs.sdf().map_h() );

    in.debug = g_dbg_params;
    in.debug.anim_time = std::fmod( static_cast<float>( SDL_GetTicks() ) / 1000.0f, 1000.0f );
    if( g ) {
        in.debug.player_x = static_cast<float>( g->u.bub_pos().x() ) + 0.5f;
        in.debug.player_y = static_cast<float>( g->u.bub_pos().y() ) + 0.5f;
    }

    static int emit_dbg_frame = 0;
    if( ++emit_dbg_frame >= 120 ) {
        emit_dbg_frame = 0;
        dbg( DL::Debug ) << "lighting: n_emit=" << rs.collector()->last_count()
                         << " emitter_buf=" << ( rs.collector()->emitter_buffer() ? "ok" : "NULL" )
                         << " sdf_tex=" << ( rs.sdf().sdf_texture() ? "ok" : "NULL" )
                         << " sampler=" << ( rs.gpu_sampler() ? "ok" : "NULL" )
                         << " cam_off=(" << in.camera_off_x << "," << in.camera_off_y << ")"
                         << " sdf=" << rs.sdf().map_w() << "x" << rs.sdf().map_h()
                         << " z=" << in.z_level
                         << " ambient=" << in.ambient
                         << " tile_px=" << in.tile_pixel_size;
    }
    s_emo.last_n_emit_pushed = static_cast<Uint32>( rs.collector()->last_count() );

    rs.begin_lighting_frame( in );
}

auto maybe_push_menu_background( lighting::render_state &rs,
                                 lighting::frame_context &ctx ) -> void
{
    const bool no_world = !g || !world_generator || !world_generator->active_world;
    if( no_world && rs.tile_sprites_empty() && rs.geometry().white_texture() ) {
        lighting::sprite_instance bg = fullscreen_quad(
                static_cast<float>( ctx.swapchain_w ),
                static_cast<float>( ctx.swapchain_h ) );
        bg.tint_r = 0.0f;
        bg.tint_g = 0.0f;
        bg.tint_b = menu_emitter_tuning::blue_backdrop ? 0.3f : 0.0f;
        rs.queue_tile_sprite( rs.geometry().white_texture(), bg );
    }
}

auto draw_lighting_overlays( lighting::render_state &rs,
                             lighting::frame_context &ctx ) -> void
{
    struct transient_routing_guard {
        lighting::render_state &rs;
        ~transient_routing_guard()
        {
            rs.set_transient_routing( false );
        }
    } _t_route{ rs };
    rs.set_transient_routing( true );

    if( g_dbg_lighting ) {
        constexpr float OL_PI = 3.14159265358979323846f;
        const float tp  = s_emo.tile_px > 0.f ? s_emo.tile_px : 32.f;
        const float sw  = static_cast<float>( ctx.swapchain_w );
        const float sh  = static_cast<float>( ctx.swapchain_h );

        // ── Tier 4: grid lines ─────────────────────────────────────────────
        {
            const float anchor_x = std::fmod( s_emo.op_x, tp );
            const float anchor_y = std::fmod( s_emo.op_y, tp );
            for( float x = anchor_x; x < sw; x += tp ) {
                rs.queue_ui_rect( x, 0.f, 1.f, sh, 0.25f, 0.25f, 0.30f, 0.35f );
            }
            for( float y = anchor_y; y < sh; y += tp ) {
                rs.queue_ui_rect( 0.f, y, sw, 1.f, 0.25f, 0.25f, 0.30f, 0.35f );
            }
        }

        // ── Tier 2: emitter markers ────────────────────────────────────────
        static bool emo_cam_logged = false;
        if( !emo_cam_logged ) {
            emo_cam_logged = true;
            dbg( DL::Debug ) << "overlay: cam=(" << s_emo.cam_off_x << ","
                             << s_emo.cam_off_y << ") tile_px=" << s_emo.tile_px
                             << " op=(" << s_emo.op_x << "," << s_emo.op_y
                             << ") snap=" << s_emo.snap.size();
        }
        for( const auto &e : s_emo.snap ) {
            const float sx  = ( e.pos_x + s_emo.cam_off_x ) * tp + s_emo.op_x;
            const float sy  = ( e.pos_y + s_emo.cam_off_y ) * tp + s_emo.op_y;
            const float rpx = e.radius * tp;
            const float cr  = e.r > 0.01f ? e.r : 1.0f;
            const float cg  = e.g > 0.01f ? e.g : 1.0f;
            const float cb  = e.b > 0.01f ? e.b : 1.0f;
            rs.queue_ui_rect( sx - 4.f, sy - 4.f, 8.f, 8.f, cr, cg, cb, 1.0f );
            for( int i = 0; i < 48; ++i ) {
                const float a = 2.0f * OL_PI * static_cast<float>( i ) / 48.0f;
                rs.queue_ui_rect( sx + std::cos( a ) * rpx - 2.f,
                                  sy + std::sin( a ) * rpx - 2.f,
                                  4.f, 4.f, cr, cg, cb, 0.75f );
            }
        }

        // Player cross (bright green) at map-coord player pos.
        {
            const float px = ( s_emo.player_x + s_emo.cam_off_x ) * tp + s_emo.op_x;
            const float py = ( s_emo.player_y + s_emo.cam_off_y ) * tp + s_emo.op_y;
            rs.queue_ui_rect( px - 12.f, py - 1.f, 24.f, 2.f, 0.f, 1.f, 0.f, 1.f );
            rs.queue_ui_rect( px - 1.f, py - 12.f, 2.f, 24.f, 0.f, 1.f, 0.f, 1.f );
        }
        // Screen-center cross (cyan).
        {
            const float cx = sw * 0.5f;
            const float cy = sh * 0.5f;
            rs.queue_ui_rect( cx - 10.f, cy - 1.f, 20.f, 2.f, 0.f, 1.f, 1.f, 0.9f );
            rs.queue_ui_rect( cx - 1.f, cy - 10.f, 2.f, 20.f, 0.f, 1.f, 1.f, 0.9f );
        }

        // ── Tier 3: per-tile (x,y) coord labels ────────────────────────────
        constexpr int TIER3_RADIUS = 6;
        constexpr int TIER3_STEP   = 2;
        const bool cache_stale =
            s_emo.player_x != s_emo.cached_player_x ||
            s_emo.player_y != s_emo.cached_player_y ||
            s_emo.cam_off_x != s_emo.cached_cam_off_x ||
            s_emo.cam_off_y != s_emo.cached_cam_off_y ||
            s_emo.tile_px != s_emo.cached_tile_px ||
            s_emo.screen_w != s_emo.cached_screen_w ||
            s_emo.screen_h != s_emo.cached_screen_h;
        if( cache_stale && tp >= 16.f ) {
            s_emo.tile_labels.clear();
            const int mx0 = s_emo.player_x - TIER3_RADIUS;
            const int my0 = s_emo.player_y - TIER3_RADIUS;
            const int mx1 = s_emo.player_x + TIER3_RADIUS;
            const int my1 = s_emo.player_y + TIER3_RADIUS;
            for( int my = my0; my <= my1; my += TIER3_STEP ) {
                for( int mx = mx0; mx <= mx1; mx += TIER3_STEP ) {
                    const float tx = ( mx + s_emo.cam_off_x ) * tp + s_emo.op_x + 1.f;
                    const float ty = ( my + s_emo.cam_off_y ) * tp + s_emo.op_y + 1.f;
                    s_emo.tile_labels.push_back( {
                        tx, ty,
                        std::to_string( mx ) + "," + std::to_string( my )
                    } );
                }
            }
            s_emo.cached_player_x  = s_emo.player_x;
            s_emo.cached_player_y  = s_emo.player_y;
            s_emo.cached_cam_off_x = s_emo.cam_off_x;
            s_emo.cached_cam_off_y = s_emo.cam_off_y;
            s_emo.cached_tile_px   = tp;
            s_emo.cached_screen_w  = s_emo.screen_w;
            s_emo.cached_screen_h  = s_emo.screen_h;
        }
        if( g_display.font ) {
            for( const TileCoordGlyph &g_lbl : s_emo.tile_labels ) {
                const float lw = static_cast<float>( g_lbl.text.size() ) *
                                 static_cast<float>( g_display.font->width );
                rs.queue_ui_rect( g_lbl.x - 1.f, g_lbl.y - 1.f,
                                  lw + 2.f, static_cast<float>( g_display.font->height ) + 2.f,
                                  0.f, 0.f, 0.f, 0.7f );
                draw_string( *g_display.font, g_display.renderer, g_display.geometry,
                             g_lbl.text,
                             point( static_cast<int>( g_lbl.x ),
                                    static_cast<int>( g_lbl.y ) ),
                             14 );
            }
        }
    }
}

auto composite_ui_pass_a( lighting::render_state &rs,
                          lighting::frame_context &ctx, int proj_w, int proj_h ) -> void
{
    constexpr float clear_transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    lighting::ui_composite_target *uct = rs.ui_target();
    if( uct && g_dbg_lighting ) {
        uct->invalidate();
    }

    const bool any_ui = !rs.ui_rects_empty() || !rs.font_glyphs_empty();
    if( uct && uct->texture() && any_ui && uct->consume_dirty() ) {
        rs.tile_batcher().begin_pass( ctx.cmd_buffer, uct->texture(),
                                      uct->width(), uct->height(),
                                      clear_transparent,
                                      static_cast<std::uint32_t>( proj_w ),
                                      static_cast<std::uint32_t>( proj_h ) );
        rs.flush_ui( rs.tile_batcher(), rs.gpu_sampler() );
        rs.tile_batcher().end_pass();
    }
}

auto render_world_pass_w( lighting::render_state &rs,
                          lighting::frame_context &ctx, int proj_w, int proj_h ) -> void
{
    constexpr float clear_black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    lighting::ui_composite_target *wt = rs.world_target();
    if( !wt || !wt->texture() ) {
        return;
    }

    const bool needs_clear = wt->consume_dirty();
    const bool have_tiles  = !rs.tile_sprites_empty() && rs.gpu_sampler();
    if( !needs_clear && !have_tiles ) {
        return;
    }

    rs.flush_shadow_casters( ctx.cmd_buffer,
                             static_cast<std::uint32_t>( proj_w ),
                             static_cast<std::uint32_t>( proj_h ) );

    rs.tile_batcher().begin_pass( ctx.cmd_buffer, wt->texture(),
                                  wt->width(), wt->height(),
                                  clear_black,
                                  static_cast<std::uint32_t>( proj_w ),
                                  static_cast<std::uint32_t>( proj_h ),
                                  wt->format() );
    if( have_tiles ) {
        rs.flush_tile_sprites( rs.tile_batcher(), rs.gpu_sampler() );
    }
    rs.tile_batcher().end_pass();

    if( g_vol_enable && rs.volumetric().ready() ) {
        lighting::vol_params vp = g_vol_params;
        vp.vol_density   = g_vol_density;
        vp.vol_intensity = g_vol_intensity;
        vp.vol_reach     = g_vol_reach;
        vp.vol_shadow    = g_vol_shadow;
        vp.proj_w = static_cast<float>( proj_w );
        vp.proj_h = static_cast<float>( proj_h );
        rs.volumetric().record( ctx.cmd_buffer, wt->texture(),
                                wt->width(), wt->height(),
                                rs.sdf().sdf_buffer(), rs.sdf().sky_vis_buffer(),
                                vp );
    }

    if( g_bloom_enable && rs.bloom().ready() ) {
        rs.bloom().record( ctx.cmd_buffer, wt->texture(),
                           wt->width(), wt->height(),
                           g_bloom_threshold, g_bloom_intensity );
    }

    // High-fidelity rain effect: droplets + splat map fade/accumulate.
    if( g_rain_enable && rs.rain().ready() ) {
        lighting::rain_params rp{};
        rp.active     = true;
        rp.intensity  = std::clamp( g_rain_intensity, 0.f, 1.f );
        rp.wind_angle = 270.f; // default: wind from west (left-to-right on screen)
        rp.fade_rate  = 0.98f; // wetness decay per frame (~50% in ~35 frames)
        rs.rain().record( ctx.cmd_buffer, wt->texture(),
                          wt->width(), wt->height(), rp );
    }
}

auto tonemap_pass_t( lighting::render_state &rs,
                     lighting::frame_context &ctx ) -> void
{
    lighting::ui_composite_target *wt   = rs.world_target();
    lighting::ui_composite_target *wldr = rs.world_ldr_target();
    if( wt && wt->texture() && wldr && wldr->texture() && rs.gpu_sampler()
        && rs.tonemap().ready() ) {
        rs.tonemap().record( ctx.cmd_buffer, wt->texture(), rs.gpu_sampler(),
                             wldr->texture(), wldr->width(), wldr->height(),
                             g_tonemap_exposure, g_tonemap_min_ev, g_tonemap_max_ev );
    }
}

auto composite_swapchain_pass_b( lighting::render_state &rs,
                                 lighting::frame_context &ctx, int proj_w, int proj_h ) -> void
{
    constexpr float clear_black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    const bool imgui_active = imgui_layer::active();
    const bool rmlui_active = rmlui_layer::active();
    if( imgui_active ) {
        imgui_layer::new_frame();
        imgui_layer::prepare( ctx.cmd_buffer );
    }
    if( rmlui_active ) {
        // new_frame()=Update() + prepare()=geometry upload, both BEFORE begin_pass
        // (D3D12: uploads must not land inside the open render pass).
        rmlui_layer::new_frame();
        rmlui_layer::prepare( ctx.cmd_buffer );
    }

    rs.tile_batcher().begin_pass( ctx.cmd_buffer, ctx.swapchain_tex,
                                  ctx.swapchain_w, ctx.swapchain_h,
                                  clear_black,
                                  static_cast<std::uint32_t>( proj_w ),
                                  static_cast<std::uint32_t>( proj_h ) );

    auto blit_layer = [&]( lighting::ui_composite_target *layer ) {
        if( !layer || !layer->texture() || !rs.gpu_sampler() ) {
            return;
        }
        const lighting::sprite_instance quad = fullscreen_quad(
                static_cast<float>( proj_w ), static_cast<float>( proj_h ) );
        rs.tile_batcher().set_texture( layer->texture(), rs.gpu_sampler(),
                                       /*is_lit=*/false );
        rs.tile_batcher().draw( quad );
    };
    if( g_shadow_debug && rs.shadow_mask() && rs.shadow_mask()->texture() ) {
        blit_layer( rs.shadow_mask() );
    } else {
        blit_layer( rs.world_ldr_target() );
    }
    blit_layer( rs.ui_target() );

    // Both overlays share the single swapchain pass (D3D12 single-pass rule).
    // RmlUi (player menus) draws first, ImGui (dev UI) on top.
    rs.tile_batcher().end_pass(
        ( imgui_active || rmlui_active )
        ? lighting::sprite_batcher::pass_overlay_fn(
    [imgui_active, rmlui_active]( SDL_GPURenderPass * rp, SDL_GPUCommandBuffer * cb ) {
        if( rmlui_active ) {
            rmlui_layer::render_in_pass( rp, cb );
        }
        if( imgui_active ) {
            imgui_layer::render_in_pass( rp, cb );
        }
    } )
        : lighting::sprite_batcher::pass_overlay_fn{} );

    rs.device().submit_frame( ctx );
}

void refresh_display()
{
    g_display.needupdate = false;
    g_display.lastupdate = SDL_GetTicks();

    auto &rs = lighting::get_render_state();

    auto ctx = begin_frame( rs );
    if( !ctx ) {
        return;
    }

    const bool rc_rebuild = build_lighting( rs );
    flush_and_gather_rc( rs, *ctx, rc_rebuild );
    assemble_light_inputs( rs, *ctx );
    maybe_push_menu_background( rs, *ctx );

    int proj_w = 0;
    int proj_h = 0;
    SDL_GetWindowSize( g_display.window.get(), &proj_w, &proj_h );
    if( proj_w <= 0 || proj_h <= 0 ) {
        proj_w = static_cast<int>( ctx->swapchain_w );
        proj_h = static_cast<int>( ctx->swapchain_h );
    }

    draw_lighting_overlays( rs, *ctx );
    composite_ui_pass_a( rs, *ctx, proj_w, proj_h );
    render_world_pass_w( rs, *ctx, proj_w, proj_h );
    tonemap_pass_t( rs, *ctx );
    composite_swapchain_pass_b( rs, *ctx, proj_w, proj_h );
}
