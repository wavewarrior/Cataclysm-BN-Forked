#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>

#include "avatar.h"
#include "calendar.h"
#include "cached_options.h"
#include "cata_tiles.h"
#include "game_constants.h"
#include "map.h"
#include "debug.h"
#include "game.h"
#include "profile.h"
#include "output.h"
#include "point.h"
#include "sdl_display.h"
#include "sdl_font.h"
#include "sdl_fonts.h"
#include "sdl_geometry.h"
#include "sdl_lighting_devui.h"
#include "sdl_render_frame.h"
#include "sdl_wrappers.h"
#include "lighting/dev_test_lights.h"
#include "lighting/frame_build.h"
#include "lighting/rmlui_layer.h"
#include "lighting/render_state.h"
#include "sound_visualization.h"
#include "lighting/sdf_pass.h"
#include "weather.h"
#include "weather_type.h"
#include "worldfactory.h"

#define dbg(x) DebugLogFL((x),DC::Main)

using namespace std::literals;

// Rolling averages from frame_perf, published every frame by refresh_display.
// File-static: consumed only within this TU (the overlay in pass_b).
static float g_fps_avg = 0.0f;
static float g_body_ms_avg = 0.0f;
// External linkage: toggled from game::toggle_debug_fps() via sdl_render_frame.h.
bool g_show_fps = false;

// Per-phase render-body timing — pins which refresh_display stage produces the
// render_body spikes seen while walking. Indexed 0..9 in call order; sum+max
// accumulate over the SAME 120-frame window as the [render][perf] line and reset
// with it (logged + zeroed in the frame_perf destructor). Diagnostic; remove once
// the spike source is identified.
static double      g_phase_sum[10] = {};
static double      g_phase_max[10] = {};
static const char *g_phase_name[10] = {
    "begin", "build_light", "flush_gather", "assemble", "menu_bg",
    "overlays", "ui_a", "world_w", "tonemap", "swap_b"
};

// Per-frame volumetric inputs: written in assemble_light_inputs, consumed in
// render_world_pass_w. Both live in this TU, so file-local (was a dev-UI global).
static lighting::vol_params g_vol_params;

// Full-screen identity-blit quad (origin, full UV, white tint, no rotation).
// Callers override tint as needed (e.g. the menu backdrop).
static lighting::sprite_instance fullscreen_quad( float w, float h )
{
    lighting::sprite_instance q{};
    q.dst_w  = w;
    q.dst_h  = h;
    q.src_uw = 1.f;
    q.src_vh = 1.f;
    q.tint_r = 1.f;
    q.tint_g = 1.f;
    q.tint_b = 1.f;
    q.tint_a = 1.f;
    return q;
}

// Frame phase helpers — file-local, orchestrated by refresh_display (end of file).
// Forward-declared (static) so calls resolve regardless of definition order and
// the definitions below take internal linkage.
static auto begin_frame( lighting::render_state &rs ) -> std::optional<lighting::frame_context>;
static auto build_lighting( lighting::render_state &rs ) -> bool;
static auto flush_and_gather_rc( lighting::render_state &rs, lighting::frame_context &ctx,
                                 bool rc_rebuild ) -> void;
static auto assemble_light_inputs( lighting::render_state &rs,
                                   lighting::frame_context &ctx ) -> void;
static auto maybe_push_menu_background( lighting::render_state &rs,
                                        lighting::frame_context &ctx ) -> void;
static auto draw_lighting_overlays( lighting::render_state &rs,
                                    lighting::frame_context &ctx ) -> void;
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
    // RmlUi: lazy init; init() self-guards so this only truly attempts once.
    if( !rmlui_layer::ready() ) {
        rmlui_layer::init( rs.device() );
    }

    // F4 opens the RmlUi dev panel (devui_visible()), rendered via the normal RmlUi
    // composite pass.
    sdl_lighting_devui::rml_tick();

    rs.tile_batcher().begin_frame();
    rs.ui_batcher().begin_frame();
    rs.fonts().begin_frame();

    lighting::frame_context ctx = rs.device().begin_frame();
    if( !ctx.valid() ) {
        return std::nullopt;
    }
    if( !ctx.swapchain_tex ) {
    // Acquire succeeded but the drawable is transiently unavailable (window
    // occluded/minimised, or a timing gap during the rapid loading-screen
    // frames). Presenting a nil drawable aborts on Metal
    // (presentDrawable: "drawable must not be nil"), so CANCEL the command
    // buffer — submit_frame would present and crash.
    rs.device().cancel_frame( ctx );
        return std::nullopt;
    }
    return ctx;
}

auto build_lighting( lighting::render_state &rs ) -> bool
{
    ZoneScopedN( "render_build_lighting" );
    bool rc_rebuild = false;
    if( !rs.collector() ) {
    return rc_rebuild;
}

// P3: gate SDF rebuild on transparency_generation change, not turn.
// Creatures moving don't change the SDF (they're emitters only, not occluders).
// Only terrain/furniture/field/vehicle transparency changes matter.
//
// Origin term tracks the BUBBLE (abs-sub) origin, NOT the camera. The SDF/vis/
// skyvis are bubble-indexed (W=H=mapsize*SEEX, transparency_cache[x*H+y]), and
// light_pos reaches the shader in bubble-tile coords, so panning the camera one
// tile per step does NOT change their content — only the reality bubble shifting
// (map reload) does. The old camera-origin term forced a full 2x supersampled DT
// recompute every walk-step (the horde walk-lag); the bubble origin fires only on
// an actual shift. Emitters refresh every frame outside this gate, so decoupling
// from camera scroll does not freeze moving lights.
//
// P4: split rebuild_pertile into two independent gates so each buffer only
// rebuilds when its actual dependency changed:
//   rebuild_structure — SDF, sun_sdf, sky_vis (transparency_generation + z + origin)
//   rebuild_vis       — FOV visibility mask (player position change)
// When a door opens (structure++), vis does NOT need to rebuild. When the player
// walks in static terrain, only vis rebuilds — SDF/sun_sdf/sky_vis are skipped.
static std::uint64_t last_gen = 0;
static int           last_z = INT_MIN;
static point         last_origin{ INT_MIN, INT_MIN };
static int           last_player_x = INT_MIN;
static int           last_player_y = INT_MIN;

lighting::lighting_rebuild_flags rebuild{};
int px = 0, py = 0;
std::uint64_t gen = 0;
if( g && world_generator && world_generator->active_world ) {
    const int z = g->u.bub_pos().z();
        const point origin = g->m.get_abs_sub().raw().xy();
        // Read generation from the current level's cache.
        const auto &cache = g->m.get_cache_ref( z );
        gen = cache.transparency_generation;
        px = g->u.bub_pos().x();
        py = g->u.bub_pos().y();

        // Rebuild the SDF on transparency change (gen), z change, OR map shift
        // (origin): a shift moves the bubble's contents, so the map-local SDF must
        // realign immediately or shadows drift behind the camera for a frame.
        rebuild.structure = sdl_lighting_devui::devui_visible()
                            || gen != last_gen || z != last_z
                            || origin != last_origin;

        // vis depends on player position — the seen_cache shadowcast origin.
        // When the player moves, FOV changes even if terrain hasn't.
        // When terrain changes (structure rebuild), vis is already covered by
        // rebuild.structure because seen_cache is rebuilt alongside transparency_cache.
        rebuild.vis = sdl_lighting_devui::devui_visible()
                      || px != last_player_x || py != last_player_y
                      || z != last_z;

        if( rebuild.structure ) {
            last_gen = gen;
            last_z = z;
            last_origin = origin;
        }
        if( rebuild.vis ) {
            last_player_x = px;
            last_player_y = py;
            last_z = z;
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

    // Dev test lights: keep the hovered world-tile fresh while the F4 panel is
    // open (the panel drops a static light there on click); despawn all placed
    // lights the moment the panel closes. Pure debugging aid.
    if( !sdl_lighting_devui::devui_visible() ) {
    if( !dev_test_lights::lights.empty() ) {
            dev_test_lights::lights.clear();
        }
    } else if( g && tilecontext && world_generator && world_generator->active_world ) {
    float msx = 0.0f, msy = 0.0f;
    SDL_GetMouseState( &msx, &msy );
        const point o  = tilecontext->get_tile_map_origin().raw();
        const point op = tilecontext->get_drawing_pixel_offset();
        const int   tw = std::max( 1, tilecontext->get_tile_width() );
        const int   th = std::max( 1, tilecontext->get_tile_height() );
        dev_test_lights::hover_wx = ( msx - static_cast<float>( op.x ) )
                                    / static_cast<float>( tw ) + static_cast<float>( o.x );
        dev_test_lights::hover_wy = ( msy - static_cast<float>( op.y ) )
                                    / static_cast<float>( th ) + static_cast<float>( o.y );
        dev_test_lights::hover_wz = static_cast<float>( g->u.bub_pos().z() );
    }

    dbg( DL::Debug ) << "[render] build_and_submit_lighting START";
    // B1: bound the SDF rebuild to the on-screen tile rect. Origin + extent come
    // from cata_tiles (bubble-local tile coords, same space as the SDF grid).
    // No tilecontext (e.g. main menu) → cam_w=0 → whole-bubble fallback.
    int cam_x0 = -1, cam_y0 = -1, cam_w = 0, cam_h = 0;
    if( tilecontext ) {
    if( g ) {
            // Compute camera origin from current player position rather than reading
            // tilecontext->get_tile_map_origin(), which is only updated by
            // cata_tiles::draw() and can be stale on frames where refresh_display
            // fires without a preceding redraw (e.g. pump_events at end of turn).
            // Matches cata_tiles::draw() formula: o = floor(center + subtile) - POS.
            // Subtile offset omitted (<1 tile error, irrelevant for clip region).
            const float cx = static_cast<float>( g->u.bub_pos().x() + g->u.view_offset.x() );
            const float cy = static_cast<float>( g->u.bub_pos().y() + g->u.view_offset.y() );
            cam_x0 = static_cast<int>( std::floor( cx ) ) - POSX;
            cam_y0 = static_cast<int>( std::floor( cy ) ) - POSY;
        } else {
            const point cam_o = tilecontext->get_tile_map_origin().raw();
            cam_x0 = cam_o.x;
            cam_y0 = cam_o.y;
        }
        cam_w  = tilecontext->get_screentile_width();
        cam_h  = tilecontext->get_screentile_height();
    }
    lighting::frame_lighting_result fr =
        lighting::build_and_submit_lighting( rs, rebuild, g_dbg_lighting,
            g_skylight_bleed, g_vision_blur,
            cam_x0, cam_y0, cam_w, cam_h );
    rc_rebuild = fr.built_pertile;
    DebugLogFL( DL::Info, DC::Main )
            << "[flash][gpu] rebuild: struct=" << rebuild.structure
            << " vis=" << rebuild.vis
            << " rc=" << rc_rebuild
            << " origin=" << ( g ? g->m.get_abs_sub().raw().xy().to_string() : "?" )
            << " px=" << px << " py=" << py
            << " gen=" << ( g ? std::to_string( gen ) : "?" )
            << " cam_xy0=" << cam_x0 << "," << cam_y0
            << " cam_wh=" << cam_w << "x" << cam_h;
    if( fr.built_pertile ) {
    s_emo.trans_at_player    = fr.trans_at_player;
    s_emo.sdf_W_at_submit    = fr.sdf_W;
    s_emo.sdf_size_at_submit = fr.sdf_size;
}
if( g_dbg_lighting ) {
    s_emo.snap = std::move( fr.snapshot_copy );
    }
    return rc_rebuild;
}

// Stage 2b.2: the directional celestial light is the sun by day, the moon by
// night. The compute sky/sun march (direction + elevation) and the fragment
// Cloud dimming valid at any hour (unlike the daytime sunlight()-ratio mult):
// clear weather (light_modifier≈0) → 1.0; heavy overcast (≈-60..-100) → ~0.3.
static float weather_cloud_mult()
{
    if( !g ) { return 1.0f; }
    const weather_type_id wid = get_weather().weather_id;
    if( !wid.is_valid() ) { return 1.0f; }
    return std::clamp( 1.0f + static_cast<float>( wid->light_modifier ) / 100.0f,
                       0.3f, 1.0f );
}

// Weather-driven rain intensity (GPU rain). Returns 0 when no rain or no game
// session; dev slider g_rain_intensity serves as fallback for menu testing.
static auto weather_rain_intensity() -> float
{
    if( g ) {
    const weather_type_id wid = get_weather().weather_id;
        if( wid.is_valid() && wid->rains ) {
            switch( wid->precip ) {
                case precip_class::very_light:
                    return 0.1f;
                case precip_class::light:
                    return 0.3f;
                case precip_class::medium:
                    return 0.6f;
                case precip_class::heavy:
                    return 1.0f;
                default:
                    return 0.0f;
            }
        } else {
            return 0.0f;
        }
    }
    return std::clamp( g_rain_intensity, 0.f, 1.f );
}

// shading (colour + intensity) both want ONE light, so we fold the moon into the
// existing sun_params: the moon rides the opposite (12h-shifted) arc, cold and
// dim, scaled by phase ("full moon = sun with different params"). We pick
// whichever body is brighter — the sun overtakes the moon at dawn and vice-versa
// at dusk — for the DIRECTIONAL term, and keep the real-hour SKY ambient either
// way (the sky-dome glow is not the moon). No shader/struct change: the moon is
// just the sun term with swapped direction/colour/intensity, occluded by the
// same SkyBuf.a coverage march.
static lighting::sun_params make_celestial_params( const time_point &when, float hour )
{
    lighting::sun_params sp = lighting::make_sun_params( hour );
    float moon_hour = hour + 12.0f;
    if( moon_hour >= 24.0f ) {
        moon_hour -= 24.0f;
    }
    const lighting::sun_params mp = lighting::make_sun_params( moon_hour );
    // Phase illumination: NEW(0)→0 … FULL(4)→1 … WANING→0 (enum 0..7).
    float phase_d = 4.0f - static_cast<float>( get_moon_phase( when ) );
    phase_d = phase_d < 0.0f ? -phase_d : phase_d;
    const float illum = std::clamp( 1.0f - phase_d / 4.0f, 0.0f, 1.0f );
    constexpr float MOON_MAX = 0.18f;   // peak full-moon directional intensity
    const float moon_int = illum * MOON_MAX;
    if( moon_int > sp.sun_intensity ) {
        sp.sun_dir_x     = mp.sun_dir_x;
        sp.sun_dir_y     = mp.sun_dir_y;
        sp.sun_sin_elev  = mp.sun_sin_elev;
        sp.sun_intensity = moon_int * weather_cloud_mult(); // P6a: clouds dim the moon
        sp.sun_r = 0.55f;   // cold blue-white moonlight
        sp.sun_g = 0.65f;
        sp.sun_b = 0.95f;
    }
    return sp;
}

auto flush_and_gather_rc( lighting::render_state &rs,
                          lighting::frame_context &ctx, bool rc_rebuild ) -> void
{
    ZoneScopedN( "render_flush_gather_rc" );
    if( rs.collector() ) {
    rs.collector()->flush_to_render_cb( ctx.cmd_buffer );
    }

    // P3.3: GPU JFA SDF — the SOLE writer of sdf_storage_. Recorded INDEPENDENTLY
    // of GI/sky pipeline readiness: the CPU Euclidean DT it replaced never
    // depended on them, and a backend that fails to create the GI/sky compute
    // pipelines (e.g. a D3D12 reflection failure) must STILL get a valid SDF —
    // otherwise the fragment shadow march reads an uninitialised buffer and all
    // emitter/sun shadows break. Runs first so SDL_GPU sees the write before the
    // sky/GI reads below and inserts the write→read barrier on sdf_storage_.
    if( rc_rebuild && rs.sdf().populated() && rs.sdf().sdf_buffer()
        && rs.gpu_sdf().ready() && rs.sdf().trans_buffer() ) {
    rs.gpu_sdf().record( ctx.cmd_buffer, rs.sdf().trans_buffer(),
                             rs.sdf().sdf_buffer(),
                             static_cast<std::uint32_t>( rs.sdf().map_w() ),
                             static_cast<std::uint32_t>( rs.sdf().map_h() ) );
    }

    // Sky/sun + GI are the optional compute layers ON TOP of the SDF; they need
    // their own pipelines created (gi().ready()) and consume the SDF write above.
    if( rc_rebuild && rs.sdf().populated() && rs.gi().ready()
        && rs.collector() && rs.sdf().sdf_buffer() ) {
    const std::uint32_t map_w = static_cast<std::uint32_t>( rs.sdf().map_w() );
        const std::uint32_t map_h = static_cast<std::uint32_t>( rs.sdf().map_h() );

        // Celestial light params drive BOTH the sky/sun pass and the GI daylight
        // injection, so derive them once. Weather-independent (intensity/colour
        // applied fragment-side); cheap, so no wait on assemble_light_inputs.
        const float sun_hour = g ? hour_of_day<float>( calendar::turn ) : 12.f;
        const lighting::sun_params sp = make_celestial_params( calendar::turn, sun_hour );

        // Stage 2a/2b: directional sky/sun pass FIRST. Marches the unified coverage
        // occluder field (OccBuf: height + roof) in 3D toward the celestial light →
        // sky-access + sun occlusion in sky_buffer(), read by sprite.frag as SkyBuf.
        // Recorded BEFORE the GI pass: gi_field.comp also reads SkyBuf (P2 daylight
        // bounce), so SDL_GPU must see the write here before the read below to
        // insert the compute-write→compute-read barrier.
        if( rs.sky().ready() && rs.sdf().occ_buffer() ) {
            lighting::sky_sun_params kp{};
            kp.map_w        = map_w;
            kp.map_h        = map_h;
            kp.sun_dir_x    = sp.sun_dir_x;
            kp.sun_dir_y    = sp.sun_dir_y;
            kp.sun_sin_elev = sp.sun_sin_elev;
            kp.shadow_k     = g_dbg_params.shadow_k;
            kp.shadow_steps = g_dbg_params.shadow_steps;
            // P5b: F4-tunable sky/sun quality knobs.
            kp.sky_dirs     = static_cast<std::uint32_t>( std::max( 1.0f, g_dbg_params.sky_dirs ) );
            kp.sky_reach    = g_dbg_params.sky_reach;
            kp.sun_steps    = static_cast<std::uint32_t>( std::max( 1.0f, g_dbg_params.sun_steps ) );
            kp.sun_penumbra = static_cast<std::uint32_t>( std::max( 1.0f, g_dbg_params.sun_penumbra ) );
            rs.sky().record( ctx.cmd_buffer, rs.sdf().occ_buffer(),
                             map_w, map_h, kp );
        }

        lighting::gi_params rp{};
        rp.emitter_count = static_cast<std::uint32_t>( std::max( 0, rs.collector()->last_count() ) );
        rp.map_w         = map_w;
        rp.map_h         = map_h;
        rp.current_z     = g ? static_cast<float>( g->u.bub_pos().z() ) : 0.0f;
        rp.shadow_k      = g_dbg_params.shadow_k;
        rp.shadow_steps  = g_dbg_params.shadow_steps;
        // P2: sun/sky surface-radiance injection. gi_field.comp adds
        // sky_color*SkyBuf.rgb + sun_color*SkyBuf.a to each tile's field so the
        // bounce pass propagates daylight into shadowed/indoor neighbours. Colour
        // mirrors the sprite's direct sun/sky terms (no weather mult — matches the
        // weather-independent sky pass; bounce is a soft fill, exactness non-critical).
        rp.sun_r         = sp.sun_r;
        rp.sun_g         = sp.sun_g;
        rp.sun_b         = sp.sun_b;
        rp.sun_intensity = sp.sun_intensity;
        rp.sky_r         = sp.sky_r;
        rp.sky_g         = sp.sky_g;
        rp.sky_b         = sp.sky_b;
        rp.sky_intensity = sp.sky_intensity;
        // Two compute dispatches (field gather → ray-march bounce) on the render
        // CB. SDL_GPU inserts the compute→graphics barrier so the sprite pass
        // reads the finished gi_buffer().
        rs.gi().record( ctx.cmd_buffer,
                        rs.collector()->emitter_buffer(), rs.sdf().sdf_buffer(),
                        rs.sky().sky_buffer(),
                        map_w, map_h, rp );
    }

    if( g_rc_readback ) {
    g_rc_readback = false;
    if( rs.gi().ready() && rs.sdf().populated() ) {
            rs.gi().debug_log_stats( static_cast<std::uint32_t>( rs.sdf().map_w() ),
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
    in.sun = make_celestial_params( calendar::turn, sun_hour );
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
        // P6a: at night the sunlight()-ratio mult above is a no-op (base <= 1.0),
        // so apply cloud dimming directly to avoid double-dimming daytime sun.
        if( base <= 1.0f ) {
            in.sun.sun_intensity *= weather_cloud_mult();
        }
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
    // Wet specular: fold the user knob with rain intensity so the sheen only shows
    // while raining (mirrors the A3 weather-mult CPU fold). 0 = exact no-op.
    in.debug.spec_strength = g_rain_enable
                             ? g_spec_strength * weather_rain_intensity()
                             : 0.f;

    static int emit_dbg_frame = 0;
    if( ++emit_dbg_frame >= 120 ) {
    emit_dbg_frame = 0;
    dbg( DL::Debug ) << "lighting: n_emit=" << rs.collector()->last_count()
                         << " emitter_buf=" << ( rs.collector()->emitter_buffer() ? "ok" : "NULL" )
                         << " sdf_buf=" << ( rs.sdf().sdf_buffer() ? "ok" : "NULL" )
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
        ~transient_routing_guard() {
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

    }

    // ── Animated debug sound pulses (shader-rendered, per-tile) ───────────
    // Each reachable tile from the BFS flood-fill becomes one instanced circle.
    // Overlapping circles with additive blending trace the occlusion boundary
    // smoothly — preserving wall shadows while gaining pixel-level smoothness.
    {
        const double now = dev_test_lights::pulse_now_s();
        sfx::advance_all_pulses( now );
        auto &pulses = dev_test_lights::sound_pulses;
        if( pulses.empty() || !rs.sound_waves().ready() ) { return; }
        const float tp = s_emo.tile_px > 0.f ? s_emo.tile_px : 32.f;
        constexpr float speed = 9.0f; // wavefront expansion, tiles/sec

        // Collect per-tile instances across all active pulses.
        std::vector<lighting::sound_wave_instance> instances;
        instances.reserve( 2048 );

        for( const auto &p : pulses ) {
            if( p.z != s_emo.player_z ) { continue; }
            const float max_r = std::clamp( p.volume, 1.f, 24.f );
            const float radius = static_cast<float>( now - p.spawn_s ) * speed;
            const float life = std::clamp( 1.f - radius / max_r, 0.f, 1.f );
            if( life <= 0.f ) { continue; }

            for( const auto &t : p.field ) {
                if( t.dist > radius ) { continue; }
                const float sx = ( t.tx + s_emo.cam_off_x ) * tp + s_emo.op_x;
                const float sy = ( t.ty + s_emo.cam_off_y ) * tp + s_emo.op_y;
                instances.push_back( { sx, sy, t.dist, 0.f } );
            }

            if( !instances.empty() ) {
                auto *wt = rs.world_target();
                const float circle_r = tp * 0.55f;
                const lighting::snd_frag_params fp {
                    .camera_off_x = static_cast<float>( s_emo.cam_off_x ),
                    .camera_off_y = static_cast<float>( s_emo.cam_off_y ),
                    .op_x = s_emo.op_x,
                    .op_y = s_emo.op_y,
                    .tile_px_inv = tp > 0.f ? 1.f / tp : 0.f,
                    .pad0 = 0.f,
                    .sdf_map_w = static_cast<std::uint32_t>( rs.sdf().map_w() ),
                    .sdf_map_h = static_cast<std::uint32_t>( rs.sdf().map_h() ),
                };
                rs.sound_waves().record( {
                    .cb = ctx.cmd_buffer,
                    .target = wt ? wt->texture() : nullptr,
                    .proj_w = wt ? static_cast<std::uint32_t>( wt->width() ) : 0u,
                    .proj_h = wt ? static_cast<std::uint32_t>( wt->height() ) : 0u,
                    .instances = &instances,
                    .radius = radius,
                    .life = life,
                    .circle_radius_px = circle_r,
                    .sdf_buffer = rs.sdf().sdf_buffer(),
                    .snd_frag_params = fp,
                } );
                instances.clear();
            }
        }

        std::erase_if( pulses, [now]( const dev_test_lights::sound_pulse & p ) {
            const float max_r = std::clamp( p.volume, 1.f, 24.f );
            return static_cast<float>( now - p.spawn_s ) * speed > max_r;
        } );
        if( !pulses.empty() ) {
            g_display.needupdate = true;
            g_display.inputdelay = 25;
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

    // Rain: world-targeted falling drops → splash rings + dark impact decals.
    // Needs the map + camera for the sky-gated tile spawn loop.
    const bool want_rain = g_rain_enable && rs.rain().ready();
    if( want_rain ) {
        lighting::rain_params rp{};
        rp.active          = true;
        rp.intensity       = weather_rain_intensity();
        rp.wind_angle      = 270.f; // wind from west (left-to-right on screen)
        rp.camera_off_x    = g_vol_params.camera_off_x;
        rp.camera_off_y    = g_vol_params.camera_off_y;
        rp.tile_pixel_size = g_vol_params.tile_pixel_size;

        if( g && tilecontext && world_generator && world_generator->active_world
            && rp.tile_pixel_size > 0.f ) {
            map &m = get_map();
            const int z = g->u.bub_pos().z();
            const level_cache &mc = m.access_cache( z );
            const int mapsize = m.getmapsize();
            const int map_w = mapsize * SEEX;
            const int map_h = mapsize * SEEY;
            const point o = tilecontext->get_tile_map_origin().raw();
            const int cache_n = static_cast<int>( mc.outside_cache.size() );

            const int tiles_x = static_cast<int>( wt->width()  / rp.tile_pixel_size ) + 2;
            const int tiles_y = static_cast<int>( wt->height() / rp.tile_pixel_size ) + 2;

            // File-local RNG so spawn jitter never perturbs the game RNG stream.
            static std::mt19937 rain_rng{ 0x5A1Du };
            std::uniform_int_distribution<int> dx( 0, std::max( 0, tiles_x ) );
            std::uniform_int_distribution<int> dy( 0, std::max( 0, tiles_y ) );
            std::uniform_real_distribution<float> dj( 0.f, 0.35f );

            const int spawn = static_cast<int>( rp.intensity * 22.f );
            for( int i = 0; i < spawn; ++i ) {
                const int tx = o.x + dx( rain_rng );
                const int ty = o.y + dy( rain_rng );
                if( tx < 0 || ty < 0 || tx >= map_w || ty >= map_h ) {
                    continue;
                }
                const int idx = tx * map_h + ty; // x-major (matches frame_build)
                if( idx < cache_n && mc.outside_cache[idx] ) { // sky-exposed only
                    // Sub-tile landing position (not grid-locked). The drop dies
                    // here and spawns its splash + dark impact decal at this tile.
                    const float fx = static_cast<float>( tx ) + dj( rain_rng ) + 0.15f;
                    const float fy = static_cast<float>( ty ) + dj( rain_rng ) + 0.15f;
                    rs.rain().add_drop( fx, fy, 0.45f + dj( rain_rng ) );
                }
            }
        }

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

    // World text (§7, e.g. SCT) renders through the RmlUi layer even with no menu
    // open, so the overlay pass must run when either a document OR world text is
    // present. world_text_active() is kept OUT of rmlui_layer::active() so it does
    // not steal mouse input (active() gates input in sdl_input).
    // FPS overlay: set the persistent HUD line. set_hud_text lives OUTSIDE the
    // world_text begin/clear cycle, so the counter shows with no menu/SCT open and
    // never accumulates (set, not appended). It also makes world_text_active() true
    // on its own → the overlay pass below runs even on an otherwise-empty frame.
    // body_ms is render-body CPU wall-clock (steady_clock), NOT GPU time — labelled
    // accordingly. The label is cached and rebuilt only when the averages change.
    if( g_show_fps ) {
        static std::string fps_label;
        static float shown_fps = -1.0f;
        static float shown_body = -1.0f;
        if( g_fps_avg != shown_fps || g_body_ms_avg != shown_body ) {
            fps_label = string_format( "FPS: %.0f  body: %.1f ms", g_fps_avg, g_body_ms_avg );
            shown_fps = g_fps_avg;
            shown_body = g_body_ms_avg;
        }
        rmlui_layer::set_hud_text( 8.0f, 8.0f, fps_label, 0xCCFFFFFFu );
    } else {
        rmlui_layer::set_hud_text( 0.0f, 0.0f, std::string(), 0u );
    }

    const bool rmlui_active = rmlui_layer::active() || rmlui_layer::world_text_active();
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

    auto blit_layer = [&]( lighting::ui_composite_target * layer ) {
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

    // RmlUi (player menus + dev panel + world text) draws into the single swapchain
    // pass (D3D12 single-pass rule).
    rs.tile_batcher().end_pass(
          rmlui_active
          ? lighting::sprite_batcher::pass_overlay_fn(
    []( SDL_GPURenderPass * rp, SDL_GPUCommandBuffer * cb ) {
        rmlui_layer::render_in_pass( rp, cb );
    } )
    : lighting::sprite_batcher::pass_overlay_fn{} );

    rs.device().submit_frame( ctx );
}

void refresh_display()
{
    // perf probe: render-body time + wall-clock period between frames (the
    // period includes the sim/turn work between renders). Rolling avg every
    // 120 frames → tells render-bound vs sim-bound. RAII so all return paths log.
    struct frame_perf {
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        ~frame_perf() {
            using clk = std::chrono::steady_clock;
            const clk::time_point now = clk::now();
            const double body_ms = std::chrono::duration<double, std::milli>( now - t0 ).count();
            static clk::time_point last{};
            static double sum_body = 0.0, max_body = 0.0, sum_period = 0.0, max_period = 0.0;
            static int n = 0;
            if( last.time_since_epoch().count() != 0 ) {
                const double period_ms = std::chrono::duration<double, std::milli>( now - last ).count();
                sum_period += period_ms;
                max_period = std::max( max_period, period_ms );
            }
            last = now;
            sum_body += body_ms;
            max_body = std::max( max_body, body_ms );
            ++n;
            // Publish running averages EVERY frame (not just at the 120-frame
            // boundary) so the overlay isn't pinned at 0 during the first window
            // after launch/enable.
            {
                const double ap = sum_period / n;
                g_fps_avg = static_cast<float>( ap > 0.0 ? 1000.0 / ap : 0.0 );
                g_body_ms_avg = static_cast<float>( sum_body / n );
            }
            if( n >= 120 ) {
                const double ap = sum_period / n;
                DebugLogFL( DL::Info, DC::Main )
                        << "[render][perf] " << n << " frames: render_body avg=" << ( sum_body / n )
                        << "ms max=" << max_body << "ms | frame_period avg=" << ap << "ms (~"
                        << ( ap > 0.0 ? 1000.0 / ap : 0.0 ) << " fps) max=" << max_period << "ms";
                // Per-phase breakdown (avg/max ms) — which stage owns the spike.
                std::string ph;
                for( int i = 0; i < 10; ++i ) {
                    ph += string_format( " %s=%.2f/%.2f", g_phase_name[i],
                                         g_phase_sum[i] / n, g_phase_max[i] );
                    g_phase_sum[i] = 0.0;
                    g_phase_max[i] = 0.0;
                }
                DebugLogFL( DL::Info, DC::Main ) << "[render][perf][phase avg/max ms]" << ph;
                sum_body = max_body = sum_period = max_period = 0.0;
                n = 0;
            }
        }
    } _fp;

    dbg( DL::Debug ) << "[render] refresh_display START";
    g_display.needupdate = false;
    g_display.lastupdate = SDL_GetTicks();

    auto &rs = lighting::get_render_state();

    // Per-phase spike attribution (see g_phase_* above). lap(i) charges the time
    // since the previous lap to phase i; maxes are logged with the 120-frame window.
    std::chrono::steady_clock::time_point _pt = std::chrono::steady_clock::now();
    auto lap = [&]( int idx ) {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>( now - _pt ).count();
        g_phase_sum[idx] += ms;
        g_phase_max[idx] = std::max( g_phase_max[idx], ms );
        _pt = now;
    };

    dbg( DL::Debug ) << "[render] begin_frame";
    auto ctx = begin_frame( rs );
    lap( 0 );
    if( !ctx ) {
        return;
    }

    const bool rc_rebuild = build_lighting( rs );
    lap( 1 );
    dbg( DL::Debug ) << "[render] flush_and_gather_rc";
    flush_and_gather_rc( rs, *ctx, rc_rebuild );
    lap( 2 );
    dbg( DL::Debug ) << "[render] assemble_light_inputs";
    assemble_light_inputs( rs, *ctx );
    lap( 3 );
    dbg( DL::Debug ) << "[render] maybe_push_menu_background";
    maybe_push_menu_background( rs, *ctx );
    lap( 4 );

    int proj_w = 0;
    int proj_h = 0;
    SDL_GetWindowSize( g_display.window.get(), &proj_w, &proj_h );
    if( proj_w <= 0 || proj_h <= 0 ) {
        proj_w = static_cast<int>( ctx->swapchain_w );
        proj_h = static_cast<int>( ctx->swapchain_h );
    }

    dbg( DL::Debug ) << "[render] draw_lighting_overlays";
    draw_lighting_overlays( rs, *ctx );
    lap( 5 );
    dbg( DL::Debug ) << "[render] composite_ui_pass_a";
    composite_ui_pass_a( rs, *ctx, proj_w, proj_h );
    lap( 6 );
    dbg( DL::Debug ) << "[render] render_world_pass_w";
    render_world_pass_w( rs, *ctx, proj_w, proj_h );
    lap( 7 );
    dbg( DL::Debug ) << "[render] tonemap_pass_t";
    tonemap_pass_t( rs, *ctx );
    lap( 8 );
    dbg( DL::Debug ) << "[render] composite_swapchain_pass_b";
    composite_swapchain_pass_b( rs, *ctx, proj_w, proj_h );
    lap( 9 );
    dbg( DL::Debug ) << "[render] refresh_display COMPLETE";
}
