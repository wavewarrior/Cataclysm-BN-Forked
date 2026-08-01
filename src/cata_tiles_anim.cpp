#include "cata_tiles.h"
#include "cata_tiles_internal.h"
#include "sdl_lighting_devui.h"
#include "lighting/solid_overlay.h"

#include "cata_utility.h"
#include "avatar.h"
#include "catacharset.h"
#include "clzones.h"
#include "cursesdef.h"
#include "field.h"
#include "game.h"
#include "json.h"
#include "line.h"
#include "map.h"
#include "options.h"
#include "sdl_wrappers.h"
#include "sounds.h"
#include "string_utils.h"
#include "translations.h"
#include "weather.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

using namespace cata_tiles_internal;

// Tuning knobs for the sprite-animation system, refreshed once per frame from options
// (avoids re-reading 14 options per creature). File-scope keeps creature.h's
// animation_tuning type out of cata_tiles.h.
static animation_tuning s_anim_tuning;
// When set by the F4 "Animation" tab, the panel owns s_anim_tuning and we stop
// clobbering it with the option values each frame (and force animations on).
static bool s_anim_override = false;

animation_tuning &debug_anim_tuning() { return s_anim_tuning; }

bool &debug_anim_override() { return s_anim_override; }

static int msgtype_to_tilecolor( const game_message_type type, const bool bOldMsg )
{
    const int iBold = bOldMsg ? 0 : 8;

    switch( type ) {
        case m_good:
            return iBold + catacurses::green;
        case m_bad:
            return iBold + catacurses::red;
        case m_mixed:
        case m_headshot:
            return iBold + catacurses::magenta;
        case m_neutral:
            return iBold + catacurses::white;
        case m_warning:
        case m_critical:
            return iBold + catacurses::yellow;
        case m_info:
        case m_grazing:
            return iBold + catacurses::blue;
        default:
            break;
    }

    return -1;
}

namespace
{

struct draw_zone_overlay_options {
    SDL_Rect rect;
    SDL_Color color;
    std::multimap<point, formatted_text> &overlay_strings;
    std::string name = empty_string;
    int alpha = 64;
    bool draw_label = true;
};

void draw_zone_overlay( const draw_zone_overlay_options& opt )
{
    lighting::overlay_rect(
    { static_cast<float>( opt.rect.x ), static_cast<float>( opt.rect.y ),
      static_cast<float>( opt.rect.w ), static_cast<float>( opt.rect.h ) },
    lighting::overlay_color_from_bytes( opt.color.r, opt.color.g, opt.color.b, opt.alpha ) );

    if( opt.draw_label && !opt.name.empty() ) {
        const point center( opt.rect.x + opt.rect.w / 2, opt.rect.y + opt.rect.h / 2 );
        opt.overlay_strings
        .emplace( center, formatted_text( opt.name, catacurses::white, text_alignment::center ) );
    }
}

} // namespace

void cata_tiles::refresh_anim_frame()
{
    creatures_anim_active_ = false; // re-evaluated as creatures/tiles draw this frame
    anim_wall_now_ = static_cast<double>( SDL_GetTicks() ) / 1000.0;
    if( s_anim_override ) { // F4 live-tuning owns the struct; don't re-read options
        anim_enabled_ = true;
        return;
    }
    anim_enabled_ = get_option<bool>( "SPRITE_ANIMATIONS" );
    if( !anim_enabled_ ) { return; }
    animation_tuning& t = s_anim_tuning;
    t.move_bob = get_option<bool>( "SPRITE_MOVE_BOB" );
    t.breathing = get_option<bool>( "SPRITE_BREATHING" );
    t.hit_reaction = get_option<bool>( "SPRITE_HIT_REACTION" );
    t.attack_lunge = get_option<bool>( "SPRITE_ATTACK_LUNGE" );
    t.bob_amplitude = static_cast<float>( get_option<float>( "SPRITE_BOB_AMPLITUDE" ) );
    t.bob_duration = static_cast<float>( get_option<float>( "SPRITE_BOB_DURATION" ) );
    t.idle_sway = static_cast<float>( get_option<float>( "SPRITE_IDLE_SWAY" ) );
    t.hit_push = static_cast<float>( get_option<float>( "SPRITE_HIT_PUSH" ) );
    t.hit_duration = static_cast<float>( get_option<float>( "SPRITE_HIT_DURATION" ) );
    t.hit_flash_intensity = static_cast<float>( get_option<float>( "SPRITE_HIT_FLASH_INTENSITY" ) );
    t.attack_amplitude = static_cast<float>( get_option<float>( "SPRITE_ATTACK_AMPLITUDE" ) );
    t.attack_duration = static_cast<float>( get_option<float>( "SPRITE_ATTACK_DURATION" ) );
}

sprite_xform cata_tiles::compute_anim_xform( const Creature& c ) const
{
    sprite_xform x;
    if( !anim_enabled_ ) { return x; }
    // Per-creature idle phase (from tile position) so a horde doesn't sway in unison.
    const tripoint_bub_ms cp = c.bub_pos();
    const float idle_phase = static_cast<float>( ( cp.x() * 7 + cp.y() * 13 ) & 15 ) * 0.3927f;
    update_animation_state( c.anim_state, s_anim_tuning, anim_wall_now_, idle_phase );
    const animation_state& a = c.anim_state;
    x.off_x = a.hit_offset_x + a.attack_offset_x + a.idle_offset_x
              + a.slide_offset_x * static_cast<float>( tile_width );
    x.off_y = a.bob_offset_y + a.hit_offset_y + a.attack_offset_y + a.idle_offset_y
              + a.slide_offset_y * static_cast<float>( tile_height );
    x.tilt_deg = a.tilt_degrees + a.hit_tilt + a.attack_tilt + a.idle_tilt;
    if( a.hit_flash > 0.f ) {
        if( c.is_avatar() ) {
            x.flash_r = x.flash_g = x.flash_b = a.hit_flash * 0.5f; // white
        } else {
            x.flash_r = a.hit_flash * 0.6f; // red
            x.flash_g = x.flash_b = -a.hit_flash * 0.5f;
        }
    }
    if( x.active() ) {
        creatures_anim_active_ = true; // keep the redraw pump alive
    }
    return x;
}

bool cata_tiles::creatures_require_animation() const { return creatures_anim_active_; }

void cata_tiles::register_tile_hit( const tripoint_bub_ms& p, float dir_x, float dir_y )
{
    if( !get_option<bool>( "SPRITE_ANIMATIONS" ) || !get_option<bool>( "SPRITE_TILE_HIT" ) ) { return; }
    tile_hits_[p] = tile_hit_state{static_cast<double>( SDL_GetTicks() ) / 1000.0, dir_x, dir_y};
}

sprite_xform cata_tiles::tile_hit_xform( const tripoint_bub_ms& p )
{
    sprite_xform x;
    if( tile_hits_.empty() ) {
        return x; // fast path: no bashes pending
    }
    const auto it = tile_hits_.find( p );
    if( it == tile_hits_.end() ) { return x; }
    constexpr double dur = 0.25;
    const double t = anim_wall_now_ - it->second.wall;
    if( t < 0.0 || t >= dur ) {
        tile_hits_.erase( it ); // self-cleaning when the recoil decays
        return x;
    }
    // Directional recoil away from the basher + decaying wobble (like a creature hit reaction,
    // a bit more extreme). dir is normalized; falls back to a vertical jolt if unknown.
    const float decay = 1.0f - static_cast<float>( t / dur );
    const float kick = std::cos( static_cast<float>( t ) * 28.0f ) * 9.0f * decay; // px
    float dx = it->second.dir_x;
    float dy = it->second.dir_y;
    if( dx == 0.f && dy == 0.f ) {
        dy = 1.f; // unknown source: jolt downward
    }
    x.off_x = kick * dx;
    x.off_y = kick * dy;
    x.fg_only = true;              // recoil the smashed feature, not the ground beneath it
    creatures_anim_active_ = true; // keep the redraw pump alive while shaking
    return x;
}

/* Animation Functions */
/* -- Inits */
void cata_tiles::init_explosion( const tripoint_bub_ms& p, int radius, const std::string& name )
{
    do_draw_explosion = true;
    exp_pos = p;
    exp_rad = radius;
    exp_name = name;
}
void cata_tiles::init_custom_explosion_layer(
    const std::map<tripoint_bub_ms, explosion_tile> &layer, const std::string& name )
{
    do_draw_custom_explosion = true;
    custom_explosion_layer = layer;
    exp_name = name;
}
void cata_tiles::init_draw_cone_aoe( const tripoint_bub_ms& origin, const one_bucket& layer )
{
    do_draw_cone_aoe = true;
    cone_aoe_origin = origin;
    cone_aoe_layer = layer;
}
void cata_tiles::init_draw_line(
    const tripoint_bub_ms& p, std::vector<tripoint_bub_ms> trajectory, std::string name,
    bool target_line )
{
    do_draw_line = true;
    is_target_line = target_line;
    line_pos = p;
    line_endpoint_id = std::move( name );
    line_trajectory = std::move( trajectory );
}
void cata_tiles::init_draw_cursor( const tripoint_bub_ms& p )
{
    do_draw_cursor = true;
    cursors.emplace_back( p );
}
void cata_tiles::init_draw_highlight( const tripoint_bub_ms& p )
{
    do_draw_highlight = true;
    highlights.emplace_back( p );
}
void cata_tiles::init_draw_weather( weather_printable weather, std::string name )
{
    do_draw_weather = true;
    weather_name = std::move( name );
    anim_weather = std::move( weather );
}
void cata_tiles::init_draw_sct() { do_draw_sct = true; }
void cata_tiles::init_draw_zones( const zone_draw_options& options )
{
    do_draw_zones = true;
    zone_start = options.start;
    zone_end = options.end;
    zone_offset = options.offset;
    zone_points = options.points;
    zone_point_lookup.clear();
    if( !zone_points.empty() ) {
        std::ranges::for_each( zone_points, [&]( const tripoint_bub_ms & point ) {
            zone_point_lookup.insert( point );
        } );
    }
}

/* -- Void Animators */
void cata_tiles::void_explosion()
{
    do_draw_explosion = false;
    exp_pos = {-1, -1, -1};
    exp_rad = -1;
}
void cata_tiles::void_custom_explosion()
{
    do_draw_custom_explosion = false;
    custom_explosion_layer.clear();
}
void cata_tiles::void_line()
{
    do_draw_line = false;
    is_target_line = false;
    line_pos = {-1, -1, -1};
    line_endpoint_id.clear();
    line_trajectory.clear();
}
void cata_tiles::void_cursor()
{
    do_draw_cursor = false;
    cursors.clear();
}
namespace
{

/// Intravenous 2's aim readout, reproduced from the shipped store screenshots
/// (e.g. ss_54ecefbf on the Steam page for app 2608270): a thin desaturated-khaki
/// sight line, a white gapped-cross reticle with an open centre, and a small
/// solid endpoint marker that flips red → orange where line of sight breaks.
///
/// The look is deliberately flat. Every channel here stays below 1.0 so the
/// RGBA16F bloom pass (thresholded at 1.0, see sdl_lighting_devui.cpp) leaves the
/// overlay alone — the previous fiery, deliberately-over-1.0 impact splash was
/// the single loudest thing on screen and read nothing like the reference.
constexpr auto aim_sight_col =
    lighting::overlay_color{ .r = 0.784f, .g = 0.745f, .b = 0.510f, .a = 0.90f };
constexpr auto aim_edge_col =
    lighting::overlay_color{ .r = 0.784f, .g = 0.745f, .b = 0.510f, .a = 0.30f };
constexpr auto aim_fill_col =
    lighting::overlay_color{ .r = 0.784f, .g = 0.745f, .b = 0.510f, .a = 0.055f };
constexpr auto aim_hit_col =
    lighting::overlay_color{ .r = 1.000f, .g = 0.200f, .b = 0.200f, .a = 0.95f };
constexpr auto aim_blocked_col =
    lighting::overlay_color{ .r = 1.000f, .g = 0.467f, .b = 0.133f, .a = 0.95f };
constexpr auto aim_reticle_col =
    lighting::overlay_color{ .r = 0.902f, .g = 0.902f, .b = 0.902f, .a = 0.95f };
/// One-pixel dark surround. The overlay has to stay legible over a lit floor and
/// over a black wall, and this game shows plenty of both in the same frame.
constexpr auto aim_shade_col =
    lighting::overlay_color{ .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 0.55f };

} // namespace

auto cata_tiles::init_draw_aim_crosshair( point pixel ) -> void
{
    aim_crosshair_pixel_ = pixel;
    do_draw_aim_crosshair = true;
}
auto cata_tiles::void_aim_crosshair() -> void
{
    do_draw_aim_crosshair = false;
    aim_crosshair_pixel_ = std::nullopt;
}
auto cata_tiles::draw_aim_crosshair() -> void
{
    if( !do_draw_aim_crosshair || !aim_crosshair_pixel_.has_value() ) { return; }
    // Read the position BEFORE voiding: void_aim_crosshair() disengages the
    // optional, so dereferencing it afterwards was undefined behaviour.
    const auto c = *aim_crosshair_pixel_;
    void_aim_crosshair();
    // A genuine (0, 0) would stamp a reticle in the map's top-left corner, which
    // is never what the player means.
    if( c == point_zero ) { return; }

    // Four detached bars around an open centre plus a single centre pip, so the
    // tile you are aiming at stays readable through the reticle. Sized off
    // tile_width to hold its proportions across zoom levels.
    const auto tw = static_cast<float>( tile_width );
    const auto gap = std::max( 3.0f, tw * 0.14f );
    const auto arm = std::max( 5.0f, tw * 0.30f );
    constexpr auto thick = 2.0f;
    const auto cx = static_cast<float>( c.x );
    const auto cy = static_cast<float>( c.y );
    const auto bar = []( float x, float y, float w, float h ) {
        lighting::overlay_rect( { x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f }, aim_shade_col );
        lighting::overlay_rect( { x, y, w, h }, aim_reticle_col );
    };
    const auto hx = thick * 0.5f;
    bar( cx - gap - arm, cy - hx, arm, thick );
    bar( cx + gap, cy - hx, arm, thick );
    bar( cx - hx, cy - gap - arm, thick, arm );
    bar( cx - hx, cy + gap, thick, arm );
    bar( cx - hx, cy - hx, thick, thick );
}
auto cata_tiles::init_draw_aim_cone( const point_bub_ms &src, float aim_rad,
                                     float spread_half_rad, int max_range, int z ) -> void
{
    aim_cone_src_    = src;
    aim_cone_angle_  = aim_rad;
    aim_cone_spread_ = spread_half_rad;
    aim_cone_range_  = max_range;
    aim_cone_z_      = z;
    do_draw_aim_cone = true;
}
auto cata_tiles::void_aim_cone() -> void { do_draw_aim_cone = false; }
auto cata_tiles::draw_aim_cone() -> void
{
    if( !do_draw_aim_cone ) { return; }
    do_draw_aim_cone = false;
    // Screen-space overlay quads queued into the world pass (lighting::solid_overlay)
    // rather than a dedicated triangle pipeline: the wedge fill is a stack of
    // rotated quads, which at these alpha levels is indistinguishable from a
    // triangle fan once the sector count is high enough to hide the chords.
    const auto origin = player_to_screen( aim_cone_src_ );
    const auto xf = compute_anim_xform( get_avatar() );
    const auto tw = static_cast<float>( tile_width );
    const auto th = static_cast<float>( tile_height );
    // Apex on the tile CENTRE. player_to_screen returns the tile's top-left, but
    // every ray below starts from the tile centre, so using the corner put the
    // whole cone half a tile up-left of the muzzle.
    const auto ox = static_cast<float>( origin.x ) + xf.off_x + tw * 0.5f;
    const auto oy = static_cast<float>( origin.y ) + xf.off_y + th * 0.5f;
    const auto half = aim_cone_spread_;

    const auto src3d = tripoint_bub_ms( aim_cone_src_, aim_cone_z_ );
    const auto &here = get_map();

    // One ray's stop distance, in tiles from the source tile's centre.
    struct cone_ray {
        float stop = 0.0f;  //< where the wedge ends: the wall's NEAR face, or max range
        bool blocked = false;
    };
    struct slab_range {
        float lo = 0.0f;
        float hi = 0.0f;
    };
    // Ray/AABB slab overlap along one axis of a unit tile whose low edge is `lo`.
    const auto axis_slab = []( float p, float d, float lo ) -> slab_range {
        constexpr auto eps = 1e-6f;
        const auto inf = std::numeric_limits<float>::infinity();
        if( std::abs( d ) < eps ) {
            // Parallel to this axis: either always within the slab, or never.
            return ( p >= lo && p <= lo + 1.0f ) ? slab_range{ -inf, inf } : slab_range{ inf, -inf };
        }
        const auto t1 = ( lo - p ) / d;
        const auto t2 = ( lo + 1.0f - p ) / d;
        return { std::min( t1, t2 ), std::max( t1, t2 ) };
    };
    const auto cast_ray = [&]( float angle ) -> cone_ray {
        const auto max_len = static_cast<float>( aim_cone_range_ );
        const auto dx = std::cos( angle );
        const auto dy = std::sin( angle );
        const auto p0x = static_cast<float>( src3d.x() ) + 0.5f;
        const auto p0y = static_cast<float>( src3d.y() ) + 0.5f;
        for( const tripoint_bub_ms &t : here.ray_cast_angle( src3d, angle, aim_cone_range_ ) ) {
            // The shooter's own tile can be impassable (firing from inside a
            // vehicle, mid-bash), and stopping on it would collapse the cone.
            if( t.xy() == src3d.xy() || !here.impassable( t ) ) { continue; }
            // Stop on the wall's NEAR face: the point where the ray first crosses
            // into the blocking tile, which is the intersection the player reads
            // as "the shot stops here". `exit_at` is computed only to validate
            // that the intersection is real - a ray parallel to one axis can miss
            // the slab entirely, which the slab_range sentinels encode as an
            // empty (lo > hi) range.
            const auto sx = axis_slab( p0x, dx, static_cast<float>( t.x() ) );
            const auto sy = axis_slab( p0y, dy, static_cast<float>( t.y() ) );
            const auto entry = std::max( { sx.lo, sy.lo, 0.0f } );
            const auto exit_at = std::min( sx.hi, sy.hi );
            if( !std::isfinite( exit_at ) || exit_at <= 0.0f || entry > exit_at ) { break; }
            return { .stop = std::min( entry, max_len ), .blocked = entry <= max_len };
        }
        return { .stop = max_len, .blocked = false };
    };

    const auto left_angle  = aim_cone_angle_ - half;
    const auto right_angle = aim_cone_angle_ + half;

    // Sector count drives how closely the fill's outer boundary follows the arc:
    // each sector contributes one flat chord, so too few reads as a sawtooth.
    constexpr auto fan_segs = 24;
    std::array<cone_ray, fan_segs + 1> rays{};
    for( auto i = 0; i <= fan_segs; ++i ) {
        const auto t = static_cast<float>( i ) / fan_segs;
        rays[i] = cast_ray( left_angle + ( right_angle - left_angle ) * t );
    }
    const auto center_len = rays[fan_segs / 2].stop * tw;
    const auto sector = ( right_angle - left_angle ) / fan_segs;

    // Barely-there wash inside the spread. Intravenous 2 draws no accuracy cone at
    // all; keeping one is a concession to a game whose dispersion genuinely varies
    // by an order of magnitude, so it sits at the edge of perception and shares
    // the sight line's hue instead of shouting in orange.
    if( half > 0.005f ) {
        for( auto i = 0; i < fan_segs; ++i ) {
            // Sectors are disjoint in angle, so the translucent fill never
            // double-blends. Each spans the SHORTER of its two bounding rays so
            // the fill cannot leak past a wall that only one edge sees.
            lighting::overlay_wedge( {
                .apex = { ox, oy },
                .angle = left_angle + sector * ( static_cast<float>( i ) + 0.5f ),
                .half_angle = sector * 0.5f,
                .radius = std::min( rays[i].stop, rays[i + 1].stop ) * tw,
                .slabs = 14,
                .color = aim_fill_col } );
        }
    }

    // Diagonal strokes are rotated quads, so they use a 2px thickness: a 1px
    // quad at an arbitrary angle straddles two pixel rows and blends to half
    // brightness instead of reading as a crisp line.
    constexpr auto stroke_px = 2.0f;

    if( half > 0.005f ) {
        const auto edge = [&]( float angle, float len ) {
            lighting::overlay_line( {
                .from = { ox, oy },
                .to = { ox + len * std::cos( angle ), oy + len * std::sin( angle ) },
                .thickness = stroke_px,
                .color = aim_edge_col } );
        };
        edge( left_angle, rays[0].stop * tw );
        edge( right_angle, rays[fan_segs].stop * tw );
    }

    // The sight line is the one element Intravenous 2 always shows: thin, flat and
    // desaturated, running from the muzzle to wherever the shot actually stops. It
    // firms up as the spread tightens, which is the overlay's only animation.
    auto sight_col = aim_sight_col;
    sight_col.a *= 0.65f + 0.35f * ( 1.0f - std::min( half / 0.3f, 1.0f ) );
    const auto tip_x = ox + center_len * std::cos( aim_cone_angle_ );
    const auto tip_y = oy + center_len * std::sin( aim_cone_angle_ );
    lighting::overlay_line( {
        .from = { ox, oy },
        .to = { tip_x, tip_y },
        .thickness = stroke_px,
        .color = sight_col } );

    // Endpoint marker: red where the shot reaches its aim point, orange where
    // terrain cuts the line short. That red/orange flip is Intravenous 2's
    // line-of-sight tell, and the cheapest way to say "you are shooting a wall".
    const auto pip = std::max( 4.0f, tw * 0.20f );
    lighting::overlay_rect( { tip_x - pip * 0.5f - 1.0f, tip_y - pip * 0.5f - 1.0f,
                              pip + 2.0f, pip + 2.0f }, aim_shade_col );
    lighting::overlay_rect( { tip_x - pip * 0.5f, tip_y - pip * 0.5f, pip, pip },
                            rays[fan_segs / 2].blocked ? aim_blocked_col : aim_hit_col );
}
auto cata_tiles::draw_particle_overlay( const particle &p ) -> void
{
    const auto tw = static_cast<float>( tile_width );
    const auto th = static_cast<float>( tile_height );

    // Both endpoints go through player_to_screen so the segment is correct under
    // the iso shear too; the sprite path has to special-case tile_iso precisely
    // because it cannot do this. player_to_screen returns the tile's TOP-LEFT, so
    // half a tile is added to ride the tile centre.
    const auto a = player_to_screen( p.tile_prev.xy() );
    const auto b = player_to_screen( p.tile.xy() );
    const auto ax = static_cast<float>( a.x ) + tw * 0.5f;
    const auto ay = static_cast<float>( a.y ) + th * 0.5f;
    const auto bx = static_cast<float>( b.x ) + tw * 0.5f;
    const auto by = static_cast<float>( b.y ) + th * 0.5f;
    const auto cx = ax + ( bx - ax ) * p.frac;
    const auto cy = ay + ( by - ay ) * p.frac;

    const auto alpha = std::clamp( p.alpha, 0.0f, 1.0f );
    // Emissive: the tint is deliberately allowed above 1.0 so the RGBA16F world
    // target hands the head to the bloom pass (threshold 1.0) as a real glow.
    const auto tinted = [&]( float gain, float a ) {
        return lighting::overlay_color{ .r = p.tint_r * gain,
                                        .g = p.tint_g * gain,
                                        .b = p.tint_b * gain,
                                        .a = a * alpha };
    };

    if( p.style == particle_style::debris ) {
        // Thrown object. A tumbling one spins on its own wall clock so the rotation
        // reads even across a single-tile flight; a FLY_STRAIGHT one (arrow, spear)
        // holds its long axis along travel so it flies point-first.
        const auto s = std::max( 2.0f, p.size_px );
        const auto spin = p.tumble
                          ? static_cast<float>( anim_wall_now_ ) * 9.0f
                          : std::atan2( by - ay, bx - ax );
        lighting::overlay_quad( { .centre = { cx, cy },
                                  .w = s * 1.9f,
                                  .h = s * 0.7f,
                                  .rotation = spin,
                                  .color = tinted( 1.0f, 0.85f ) } );
        lighting::overlay_quad( { .centre = { cx, cy },
                                  .w = p.tumble ? s * 0.7f : s * 0.5f,
                                  .h = p.tumble ? s * 1.9f : s * 0.5f,
                                  .rotation = spin,
                                  .color = tinted( 1.4f, 0.55f ) } );
        return;
    }

    // Tracer: a dim tail, a brighter core stub and a hot head, all along travel.
    const auto dx = bx - ax;
    const auto dy = by - ay;
    const auto len = std::hypot( dx, dy );
    const auto head = std::max( 2.0f, p.size_px );
    if( len >= 0.001f ) {
        const auto ux = dx / len;
        const auto uy = dy / len;
        // Clamp the tail to the distance actually flown, so a tracer never trails
        // out behind the muzzle on the first frame of flight.
        const auto tail = std::min( p.length_px, len * p.frac + tw * 0.5f );
        lighting::overlay_line( { .from = { cx - ux * tail, cy - uy * tail },
                                  .to = { cx, cy },
                                  .thickness = std::max( 1.0f, head * 0.45f ),
                                  .color = tinted( 0.55f, 0.40f ) } );
        lighting::overlay_line( { .from = { cx - ux * tail * 0.35f, cy - uy * tail * 0.35f },
                                  .to = { cx, cy },
                                  .thickness = std::max( 1.0f, head * 0.9f ),
                                  .color = tinted( 1.3f, 0.85f ) } );
    }
    // Hot head: two concentric rects, the inner one well over the bloom threshold.
    lighting::overlay_rect( { cx - head, cy - head, head * 2.0f, head * 2.0f },
                            tinted( 1.5f, 0.30f ) );
    lighting::overlay_rect( { cx - head * 0.5f, cy - head * 0.5f, head, head },
                            tinted( 3.0f, 0.95f ) );
}
auto cata_tiles::init_draw_throw_arc( const tripoint_bub_ms &src,
                                      const tripoint_bub_ms &dst,
                                      float charge ) -> void
{
    throw_arc_src    = src;
    throw_arc_dst    = dst;
    throw_arc_charge = charge;
    do_draw_throw_arc = true;
}
auto cata_tiles::void_throw_arc() -> void { do_draw_throw_arc = false; }
auto cata_tiles::draw_throw_arc() -> void
{
    if( !do_draw_throw_arc ) { return; }
do_draw_throw_arc = false;
const auto p1   = player_to_screen( throw_arc_src.xy() );
    const auto p2   = player_to_screen( throw_arc_dst.xy() );
    const auto dist = std::hypot( float( p2.x - p1.x ), float( p2.y - p1.y ) );
    const auto arc_h = std::max( 8.0f, dist / 3.0f );
    const SDL_FPoint mid{
        0.5f * ( float( p1.x ) + float( p2.x ) ),
        0.5f * ( float( p1.y ) + float( p2.y ) ) - arc_h
    };
    constexpr auto N = 24;
    std::array<SDL_FPoint, N> pts;
    for( auto i = 0; i < N; ++i ) {
    const auto t = float( i ) / float( N - 1 );
        const auto u = 1.0f - t;
        pts[i] = SDL_FPoint{
            u *u * float( p1.x ) + 2.0f * u * t * mid.x + t * t * float( p2.x ),
            u *u * float( p1.y ) + 2.0f * u * t * mid.y + t * t * float( p2.y )
        };
    }
    const auto alpha = 120 + static_cast<int>( 135.0f * throw_arc_charge );
    lighting::overlay_polyline( {
        .points = pts,
        .color = lighting::overlay_color_from_bytes( 255, 140, 0, alpha ) } );
}
auto cata_tiles::init_draw_throw_impact( const tripoint_bub_ms &dst,
        float max_radius_tiles ) -> void
{
    throw_impact_dst         = dst;
    throw_impact_max_r_tiles = max_radius_tiles;
    do_draw_throw_impact     = true;
}
auto cata_tiles::void_throw_impact() -> void { do_draw_throw_impact = false; }
auto cata_tiles::draw_throw_impact() -> void
{
    if( !do_draw_throw_impact ) { return; }
const auto c      = player_to_screen( throw_impact_dst.xy() );
    const auto tile_w = static_cast<float>( tile_width );
    const auto max_r  = throw_impact_max_r_tiles * tile_w;
    const bool explosive = ( throw_impact_max_r_tiles > 0.6f );
    const auto r_ch = explosive ? 60 : 130;
    const auto draw_ring = [&]( float radius, int alpha ) {
        lighting::overlay_ring( {
            .centre = { static_cast<float>( c.x ), static_cast<float>( c.y ) },
            .radius = radius,
            .segments = 28,
            .color = lighting::overlay_color_from_bytes( 255, r_ch, 0, alpha ) } );
    };
    draw_ring( 4.0f, 230 );
    constexpr auto period_ms = 1200.0f;
    constexpr auto n_rings   = 3;
    const auto t_now = static_cast<float>( SDL_GetTicks() );
    for( auto i = 0; i < n_rings; ++i ) {
    const auto offset = float( i ) / float( n_rings );
        const auto t = std::fmod( t_now / period_ms + offset, 1.0f );
        draw_ring( t * max_r, static_cast<int>( ( 1.0f - t ) * 200.0f ) );
    }
}
auto cata_tiles::draw_hover_effect() -> void
{
    if( !hover_tile_.has_value() ) { return; }

    // Shared pulse: recomputed from wall-clock animation time, so both the
    // brackets and the dot trail breathe together.
    const auto pulse_mult = static_cast<float>(
                                g_hover_highlight_pulse
                                ? 0.7f + 0.3f * std::sin( anim_wall_now_ * g_hover_highlight_pulse_speed * 2.0f *
                                        std::numbers::pi_v<float> )
                                : 1.0f );

    const auto tw = static_cast<float>( tile_width );
    const auto th = static_cast<float>( tile_height );

    // --- Tile highlight: corner brackets ---
    if( g_hover_highlight_enable ) {
        const auto screen = player_to_screen( hover_tile_->xy() );
        const auto x = static_cast<float>( screen.x );
        const auto y = static_cast<float>( screen.y );
        const auto arm = std::min( g_hover_highlight_corner_len * tw, tw * 0.5f );
        const auto t = std::max( 1.0f, g_hover_highlight_thickness );
        const auto col = lighting::overlay_color{
            .r = g_hover_highlight_color[0],
            .g = g_hover_highlight_color[1],
            .b = g_hover_highlight_color[2],
            .a = g_hover_highlight_color[3] * pulse_mult };

        // Two rects per corner (one along each edge), inset by the line
        // thickness so the arms meet flush instead of overhanging the tile.
        const std::array<SDL_FRect, 8> arms = {
            SDL_FRect{ x, y, arm, t },                     // top-left, horizontal
            SDL_FRect{ x, y, t, arm },                     // top-left, vertical
            SDL_FRect{ x + tw - arm, y, arm, t },          // top-right, horizontal
            SDL_FRect{ x + tw - t, y, t, arm },            // top-right, vertical
            SDL_FRect{ x, y + th - t, arm, t },            // bottom-left, horizontal
            SDL_FRect{ x, y + th - arm, t, arm },          // bottom-left, vertical
            SDL_FRect{ x + tw - arm, y + th - t, arm, t }, // bottom-right, horizontal
            SDL_FRect{ x + tw - t, y + th - arm, t, arm }  // bottom-right, vertical
        };
        for( const SDL_FRect &arm_rect : arms ) {
            lighting::overlay_rect( arm_rect, col );
        }
    }

    // --- Dotted line: player centre to hover tile centre ---
    if( !g_hover_line_enable || g->u.bub_pos().xy() == hover_tile_->xy() ) { return; }

    const auto player_screen = player_to_screen( g->u.bub_pos().xy() );
    const auto xf = compute_anim_xform( get_avatar() );
    const auto px = static_cast<float>( player_screen.x ) + xf.off_x + tw * 0.5f;
    const auto py = static_cast<float>( player_screen.y ) + xf.off_y + th * 0.5f;

    const auto hover_screen = player_to_screen( hover_tile_->xy() );
    const auto hx = static_cast<float>( hover_screen.x ) + tw * 0.5f;
    const auto hy = static_cast<float>( hover_screen.y ) + th * 0.5f;

    const auto dx = hx - px;
    const auto dy = hy - py;
    const auto dist = std::sqrt( dx * dx + dy * dy );
    const auto spacing = std::max( 1.0f, g_hover_line_dot_spacing );
    if( dist < spacing ) { return; }

    const auto nx = dx / dist;
    const auto ny = dy / dist;
    const auto size = std::max( 1.0f, g_hover_line_dot_size );
    const auto half_size = size * 0.5f;
    const auto half_spacing = spacing * 0.5f;

    for( auto step = half_spacing; step < dist - half_spacing; step += spacing ) {
        const auto t = step / dist; // progress along the line, 0..1
        // Ramp up over the first quarter, down over the last quarter.
        const auto fade =
            g_hover_line_fade_ends
            ? std::min( t * 4.0f, 1.0f ) * std::min( ( 1.0f - t ) * 4.0f, 1.0f )
            : 1.0f;
        lighting::overlay_rect(
        { px + nx * step - half_size, py + ny * step - half_size, size, size },
        { .r = g_hover_line_color[0],
          .g = g_hover_line_color[1],
          .b = g_hover_line_color[2],
          .a = g_hover_line_color[3] * fade * pulse_mult } );
    }
}
void cata_tiles::void_highlight()
{
    do_draw_highlight = false;
    highlights.clear();
}
void cata_tiles::void_weather()
{
    do_draw_weather = false;
    weather_name.clear();
    anim_weather.vdrops.clear();
}
void cata_tiles::void_sct() { do_draw_sct = false; }
void cata_tiles::void_zones()
{
    do_draw_zones = false;
    zone_points.clear();
    zone_point_lookup.clear();
}
void cata_tiles::void_cone_aoe()
{
    do_draw_cone_aoe = true;
    cone_aoe_origin = {-1, -1, -1};
    cone_aoe_layer.clear();
}

/* -- Animation Renders */
void cata_tiles::draw_explosion_frame()
{
    for( int i = 1; i < exp_rad; ++i ) {
        draw_from_id_string(
        {exp_name, C_NONE, empty_string, corner, 0}, exp_pos + point( -i, -i ), std::nullopt,
        std::nullopt, lit_level::LIT, true, 0, false );
        draw_from_id_string(
        {exp_name, C_NONE, empty_string, corner, 1}, exp_pos + point( -i, i ), std::nullopt,
        std::nullopt, lit_level::LIT, true, 0, false );
        draw_from_id_string(
        {exp_name, C_NONE, empty_string, corner, 2}, exp_pos + point( i, i ), std::nullopt,
        std::nullopt, lit_level::LIT, true, 0, false );
        draw_from_id_string(
        {exp_name, C_NONE, empty_string, corner, 3}, exp_pos + point( i, -i ), std::nullopt,
        std::nullopt, lit_level::LIT, true, 0, false );

        for( int j = 1 - i; j < 0 + i; j++ ) {
            draw_from_id_string(
            {exp_name, C_NONE, empty_string, edge, 0}, exp_pos + point( j, -i ), std::nullopt,
            std::nullopt, lit_level::LIT, true, 0, false );
            draw_from_id_string(
            {exp_name, C_NONE, empty_string, edge, 0}, exp_pos + point( j, i ), std::nullopt,
            std::nullopt, lit_level::LIT, true, 0, false );

            draw_from_id_string(
            {exp_name, C_NONE, empty_string, edge, 1}, exp_pos + point( -i, j ), std::nullopt,
            std::nullopt, lit_level::LIT, true, 0, false );
            draw_from_id_string(
            {exp_name, C_NONE, empty_string, edge, 1}, exp_pos + point( i, j ), std::nullopt,
            std::nullopt, lit_level::LIT, true, 0, false );
        }
    }
}

void cata_tiles::draw_custom_explosion_frame()
{
    // TODO: Make the drawing code handle all the missing tiles: <^>v and *
    // TODO: Add more explosion tiles, like "strong explosion", so that it displays more info

    // explosion_weak/explosion_medium/explosion removed from tiles in favor of allowing custom
    // explosion sprites.

    int subtile = 0;
    int rotation = 0;

    for( const auto& pr : custom_explosion_layer ) {
        const explosion_neighbors ngh = pr.second.neighborhood;

        switch( ngh ) {
            case N_NORTH:
            case N_SOUTH:
                subtile = edge;
                rotation = 1;
                break;
            case N_WEST:
            case N_EAST:
                subtile = edge;
                rotation = 0;
                break;
            case N_NORTH | N_SOUTH:
            case N_NORTH | N_SOUTH | N_WEST:
            case N_NORTH | N_SOUTH | N_EAST:
                subtile = edge;
                rotation = 1;
                break;
            case N_WEST | N_EAST:
            case N_WEST | N_EAST | N_NORTH:
            case N_WEST | N_EAST | N_SOUTH:
                subtile = edge;
                rotation = 0;
                break;
            case N_SOUTH | N_EAST:
                subtile = corner;
                rotation = 0;
                break;
            case N_NORTH | N_EAST:
                subtile = corner;
                rotation = 1;
                break;
            case N_NORTH | N_WEST:
                subtile = corner;
                rotation = 2;
                break;
            case N_SOUTH | N_WEST:
                subtile = corner;
                rotation = 3;
                break;
            case N_NO_NEIGHBORS:
                subtile = edge;
                break;
            case N_WEST | N_EAST | N_NORTH | N_SOUTH:
                // Needs some special tile
                subtile = edge;
                break;
        }

        const tripoint_bub_ms& p = pr.first;
        const tile_search_params tile{exp_name, C_NONE, empty_string, subtile, rotation};
        draw_from_id_string( tile, p, std::nullopt, std::nullopt, lit_level::LIT, true, 0, false );
        // Used to be divided into explosion_weak/explosion_medium/explosion.
    }
}

void cata_tiles::draw_cone_aoe_frame()
{
    // Should probably jsonize for flamethrower, dragon breath etc.
    static const std::array<std::string, 3> sprite_ids =
    {"shot_cone_weak", "shot_cone_medium", "shot_cone_strong"};

    for( const point_with_value& pv : cone_aoe_layer ) {
        const tripoint diff = pv.pt - cone_aoe_origin.raw();
        int rotation = ( sgn( diff.x ) == sgn( diff.y ) ? 1 : 0 );

        size_t intensity = ( pv.val >= 1.0 ) + ( pv.val >= 0.5 );
        const tile_search_params tile{sprite_ids[intensity], C_NONE, empty_string, 0, rotation};
        draw_from_id_string(
            tile, tripoint_bub_ms( pv.pt ), std::nullopt, std::nullopt, lit_level::LIT, false, 0,
            false );
    }
}
void cata_tiles::draw_line()
{
    if( line_trajectory.empty() ) { return; }
    static std::string line_overlay = "animation_line";
    if( !is_target_line || g->u.sees( tripoint_bub_ms( line_pos ) ) ) {
        for( auto it = line_trajectory.begin(); it != line_trajectory.end() - 1; ++it ) {
            draw_from_id_string(
            {line_overlay, C_NONE, empty_string, 0, 0}, *it, std::nullopt, std::nullopt,
            lit_level::LIT, false, 0, false );
        }
    }

    draw_from_id_string(
    {line_endpoint_id, C_NONE, empty_string, 0, 0}, line_trajectory.back(), std::nullopt,
            std::nullopt, lit_level::LIT, false, 0, false );
}
void cata_tiles::draw_cursor()
{
    for( const tripoint_bub_ms& p : cursors ) {
        draw_from_id_string(
        {"cursor", C_NONE, empty_string, 0, 0}, p, std::nullopt, std::nullopt, lit_level::LIT,
        false, 0, false );
    }
}
void cata_tiles::draw_highlight()
{
    for( const tripoint_bub_ms& p : highlights ) {
        draw_from_id_string(
        {"highlight", C_NONE, empty_string, 0, 0}, p, std::nullopt, std::nullopt,
        lit_level::LIT, false, 0, false );
    }
}
void cata_tiles::draw_weather_frame()
{

    for( auto& vdrop : anim_weather.vdrops ) {
        // TODO: Z-level awareness if weather ever happens on anything but z-level 0.
        tripoint_bub_ms p( vdrop.first, vdrop.second, 0 );
        if( !tile_iso ) {
            // currently in ASCII screen coordinates
            p = p + o.raw();
        }
        draw_from_id_string(
        {weather_name, C_WEATHER, empty_string, 0, 0}, p, std::nullopt, std::nullopt,
        lit_level::LIT, true, 0, false );
    }
}

// §7 world-text layer (Tier 6 slice 4): route SCT through RmlUi's own font engine
// instead of the curses overlay_strings path. OFF by default; A/B via the F4
// "world text (SCT)" checkbox.
bool &world_text_rmlui_enabled()
{
    static bool enabled = true;
    return enabled;
}

void cata_tiles::draw_sct_frame( std::multimap<point, formatted_text> &overlay_strings )
{
    // Phase 2: Font-only rendering path for SCT.
    // Drop ASCII fallback — tiles-focused scope; accept edge cases with non-Latin scripts or custom
    // tilesets. Apply damage-type color mapping, size scaling hints, and crit effects.

    const bool show_damage = get_option<bool>( "ANIMATION_SCT_DAMAGE" );
    const bool type_colors = get_option<bool>( "ANIMATION_SCT_TYPE_COLORS" );
    const bool show_criticals = get_option<bool>( "ANIMATION_SCT_CRITICALS" );

    for( auto iter = SCT.vSCT.begin(); iter != SCT.vSCT.end(); ++iter ) {
        // Gate damage numbers by option, but always show outcome indicators
        // (MISS/DODGE/PARRY/BLOCK/GRAZE).
        if( !show_damage && iter->getFeedbackType() == sct_feedback_type::damage ) { continue; }

        const point_bub_ms iD( iter->getPosX(), iter->getPosY() );
        const int full_text_length = utf8_width( iter->getText() );

        // Apply jitter offset (screen-space pixel offsets from radial distribution).
        const auto jitter = iter->getJitterOffset();

        for( int j = 0; j < 2; ++j ) {
            std::string sText = iter->getText( ( j == 0 ) ? "first" : "second" );
            if( sText.empty() ) { continue; }

            // Determine color: use damage-type-specific mapping when enabled.
            int FG;
            if( type_colors && !iter->getText().empty()
                && iter->getDamageType() != sct_damage_type::none ) {
                // Map SCT damage type to a game_message_type, then to tile color.
                static const std::map<sct_damage_type, game_message_type> dt_color_map{
                    {sct_damage_type::bash, m_neutral},     // white/gray - blunt impact
                    {sct_damage_type::cut, m_info},         // cyan/light blue - sharp slicing
                    {sct_damage_type::stab, m_bad},         // red-orange - piercing
                    {sct_damage_type::acid, m_good},        // green-yellow - corrosive
                    {sct_damage_type::heat, m_warning},     // orange/red - fire
                    {sct_damage_type::cold, m_info},        // light blue/cyan - ice
                    {sct_damage_type::dark, m_mixed},       // purple/magenta - eldritch
                    {sct_damage_type::light, m_critical},   // yellow/gold - holy radiant
                    {sct_damage_type::psi, m_info},         // blue/violet - psychic
                    {sct_damage_type::electric, m_warning}, // bright yellow - lightning
                    {sct_damage_type::bullet, m_neutral},   // white - fast projectile
                };
                const auto color_it = dt_color_map.find( iter->getDamageType() );
                game_message_type gmt = ( color_it != dt_color_map.end() ) ? color_it->second : m_bad;
                FG = msgtype_to_tilecolor(
                         gmt, iter->getStep() >= scrollingcombattext::iMaxSteps / 2 );
            } else {
                // Fall back to original behavior: use the stored message type.
                FG = msgtype_to_tilecolor(
                         iter->getMsgType( ( j == 0 ) ? "first" : "second" ),
                         iter->getStep() >= scrollingcombattext::iMaxSteps / 2 );
            }

            // Critical hit visual effects: gold/yellow overlay for crits.
            // Gate behind ANIMATION_SCT_CRITICALS option.
            if( show_criticals ) {
                if( iter->isCrit() && !iter->isTripleCrit() ) {
                    FG = msgtype_to_tilecolor( m_critical, false ); // bright yellow/gold
                } else if( iter->isTripleCrit() ) {
                    // Triple crit: extra-bright magenta/red for maximum impact.
                    FG = msgtype_to_tilecolor( m_headshot, false ); // bright pink/magenta
                }
            }

            const auto direction = iter->getDirecton();
            // Compensate for string length offset added at SCT creation
            // (it will be readded using font size and proper encoding later).
            const int direction_offset = ( -displace_XY( direction ).x + 1 ) * full_text_length / 2;

            // Apply jitter offset to the screen position.
            const point screen_pos = player_to_screen( iD + point( direction_offset, 0 ) ) + jitter;

            overlay_strings.emplace( screen_pos, formatted_text( sText, FG, direction ) );

            // Triple crit flash effect: render text twice with slight offset on frame 0.
            if( iter->isTripleCrit() && iter->getStep() == 0 ) {
                const point flash_offset = jitter + point( 1, -1 );
                overlay_strings.emplace(
                    player_to_screen( iD + point( direction_offset, 0 ) ) + flash_offset,
                    formatted_text( sText, FG, direction ) );
            }
        }
    }
}

void cata_tiles::draw_zones_frame( std::multimap<point, formatted_text> &overlay_strings )
{
    const bool has_custom_points = !zone_points.empty();
    const auto min_local =
        has_custom_points
        ? point_bub_ms(
    std::ranges::min( zone_points, {}, []( const tripoint_bub_ms & p ) { return p.x(); } )
    .x(),
    std::ranges::min( zone_points, {}, []( const tripoint_bub_ms & p ) { return p.y(); } )
    .y() )
        : zone_offset.xy() + zone_start.xy();
    const auto max_local =
        has_custom_points
        ? point_bub_ms(
    std::ranges::max( zone_points, {}, []( const tripoint_bub_ms & p ) { return p.x(); } )
    .x(),
    std::ranges::max( zone_points, {}, []( const tripoint_bub_ms & p ) { return p.y(); } )
    .y() )
        : zone_offset.xy() + zone_end.xy();
    const tripoint_bub_ms center_local(
        ( min_local.x() + max_local.x() ) / 2, ( min_local.y() + max_local.y() ) / 2,
        get_avatar().bub_pos().z() );
    const auto lookup_local =
        has_custom_points
        ? tripoint_bub_ms( zone_points.front().xy(), zone_points.front().z() )
        : center_local;

    // get_zone_at expects absolute coordinates
    const zone_data* zone = zone_manager::get_manager().get_zone_at(
                                get_map().bub_to_abs( lookup_local ) );

    if( has_custom_points ) {
        if( zone ) {
            overlay_strings.emplace(
                player_to_screen( center_local.xy() ),
                formatted_text( zone->get_name(), catacurses::white, direction::NORTH ) );
        }
        return;
    }

    const point screen_tl = player_to_screen( min_local );
    const point screen_br = player_to_screen( max_local ) + point( tile_width, tile_height );

    draw_zone_overlay( {
        .rect = {screen_tl.x, screen_tl.y, screen_br.x - screen_tl.x, screen_br.y - screen_tl.y},
        .color = zone ? curses_color_to_SDL( zone->get_type().obj().color() )
        : curses_color_to_SDL( c_light_green ),
        .overlay_strings = overlay_strings,
        .name = zone ? zone->get_name() : "",
    } );
}

void cata_tiles::draw_footsteps_frame( const tripoint_bub_ms& center )
{
    static const std::string id_footstep = "footstep";
    static const std::string id_footstep_above = "footstep_above";
    static const std::string id_footstep_below = "footstep_below";

    for( const tripoint_bub_ms& pos : sounds::get_footstep_markers() ) {
        if( pos.z() == center.z() ) {
            draw_from_id_string(
            {id_footstep, C_NONE, empty_string, 0, 0}, pos, std::nullopt, std::nullopt,
            lit_level::LIT, false, 0, false );
        } else if( pos.z() > center.z() ) {
            draw_from_id_string(
            {id_footstep_above, C_NONE, empty_string, 0, 0}, pos, std::nullopt, std::nullopt,
            lit_level::LIT, false, 0, false );
        } else {
            draw_from_id_string(
            {id_footstep_below, C_NONE, empty_string, 0, 0}, pos, std::nullopt, std::nullopt,
            lit_level::LIT, false, 0, false );
        }
    }
}
/* END OF ANIMATION FUNCTIONS */
