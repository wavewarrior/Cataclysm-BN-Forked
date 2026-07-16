#include "menu_plexus.h"

#include "noise_utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace lighting
{

bool g_plexus_visible = false;

} // namespace lighting

namespace
{

std::vector<lighting::plexus_particle> g_particles;
std::vector<std::uint8_t> g_pixels;
int g_w = 0;
int g_h = 0;
unsigned g_gen = 0;
int g_frame = 0;
lighting::plexus_config g_cfg;

// Sand particle for the sandstorm effect — real position, velocity, lifetime.
struct sand_grain {
    float x, y;
    float vx, vy;
    float life;     // remaining life (frames), dies at 0
    float max_life; // initial lifetime for alpha ramp
    std::uint8_t size; // 1 or 2 px
};
constexpr int SAND_COUNT = 12000;
std::vector<sand_grain> g_sand;
std::mt19937 g_sand_rng( 0xDA57u );

// ---------------------------------------------------------------------------
// Rasterizer helpers — local to this TU, do NOT reuse draw_stroke/put from
// rmlui_proc_texture.cpp (they are in its anonymous namespace).
// ---------------------------------------------------------------------------

auto blend_px( int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b,
               std::uint8_t a ) -> void
{
    if( x < 0 || x >= g_w || y < 0 || y >= g_h || a == 0 ) {
        return;
    }
    const std::size_t idx = ( static_cast<std::size_t>( y ) * g_w + x ) * 4;
    const std::uint8_t inv = 255 - a;
    g_pixels[idx + 0] = static_cast<std::uint8_t>( ( r * a + g_pixels[idx + 0] * inv ) / 255 );
    g_pixels[idx + 1] = static_cast<std::uint8_t>( ( g * a + g_pixels[idx + 1] * inv ) / 255 );
    g_pixels[idx + 2] = static_cast<std::uint8_t>( ( b * a + g_pixels[idx + 2] * inv ) / 255 );
    // Leave alpha channel as-is (background is opaque).
}

auto draw_line( int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g,
                std::uint8_t b, std::uint8_t a ) -> void
{
    int dx = std::abs( x1 - x0 );
    int dy = -std::abs( y1 - y0 );
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for( ;; ) {
        blend_px( x0, y0, r, g, b, a );
        if( x0 == x1 && y0 == y1 ) {
            break;
        }
        int e2 = 2 * err;
        if( e2 >= dy ) {
            err += dy;
            x0 += sx;
        }
        if( e2 <= dx ) {
            err += dx;
            y0 += sy;
        }
    }
}

auto fill_circle( int cx, int cy, int radius, std::uint8_t r, std::uint8_t g,
                  std::uint8_t b, std::uint8_t a ) -> void
{
    const int r2 = radius * radius;
    for( int dy = -radius; dy <= radius; ++dy ) {
        for( int dx = -radius; dx <= radius; ++dx ) {
            if( dx * dx + dy * dy <= r2 ) {
                blend_px( cx + dx, cy + dy, r, g, b, a );
            }
        }
    }
}

// Elder Futhark rune stroke definitions.
// Each rune is an array of line segments { x0, y0, x1, y1 } relative to a
// ~16×24 local grid centred on (0, 0).  Coordinates span roughly [-8,8] × [-12,12].
struct stroke {
    int x0, y0, x1, y1;
};

// glyph 1 — Isa (ᛁ): single vertical line
static constexpr stroke isa_strokes[] = {
    { 0, -12, 0, 12 },
};

// glyph 2 — Algiz (ᛉ): vertical stem + two upper diagonals (protective fork)
static constexpr stroke algiz_strokes[] = {
    { 0, -12, 0, 12 },
    { 0, -12, -6, -4 },
    { 0, -12, 6, -4 },
};

// glyph 3 — Gebo (ᚷ): an X cross (gift / binding)
static constexpr stroke gebo_strokes[] = {
    { -6, -10, 6, 10 },
    { 6, -10, -6, 10 },
};

// glyph 4 — Thurisaz (ᚦ): vertical stem + right-side triangle (thorn)
static constexpr stroke thurisaz_strokes[] = {
    { 0, -12, 0, 12 },
    { 0, -8, 7, -2 },
    { 7, -2, 0, 4 },
};

auto draw_glyph( int cx, int cy, std::uint8_t glyph_id, std::uint8_t r,
                 std::uint8_t g, std::uint8_t b, std::uint8_t a ) -> void
{
    const stroke *strokes = nullptr;
    int count = 0;

    switch( glyph_id ) {
        case 1:
            strokes = isa_strokes;
            count = static_cast<int>( sizeof( isa_strokes ) / sizeof( isa_strokes[0] ) );
            break;
        case 2:
            strokes = algiz_strokes;
            count = static_cast<int>( sizeof( algiz_strokes ) / sizeof( algiz_strokes[0] ) );
            break;
        case 3:
            strokes = gebo_strokes;
            count = static_cast<int>( sizeof( gebo_strokes ) / sizeof( gebo_strokes[0] ) );
            break;
        case 4:
            strokes = thurisaz_strokes;
            count = static_cast<int>( sizeof( thurisaz_strokes ) / sizeof( thurisaz_strokes[0] ) );
            break;
        default:
            return;
    }

    for( int i = 0; i < count; ++i ) {
        draw_line( cx + strokes[i].x0, cy + strokes[i].y0,
                   cx + strokes[i].x1, cy + strokes[i].y1,
                   r, g, b, a );
    }
}

// Wrap value into [0, limit), handling negatives.
auto wrap_coord( float v, int limit ) -> float
{
    if( limit <= 0 ) {
        return 0.0f;
    }
    float r = std::fmod( v, static_cast<float>( limit ) );
    if( r < 0.0f ) {
        r += static_cast<float>( limit );
    }
    return r;
}

// Spawn or respawn a single sand grain.  Spawns preferentially near the bottom
// (quadratic y distribution), with slight rightward initial velocity (wind).
void sand_spawn( sand_grain &s )
{
    std::uniform_real_distribution<float> dx( 0.0f, static_cast<float>( g_w ) );
    // Bottom-biased: y = (1 - (1-r)^3) * h → very sparse at top, thick at bottom.
    // r=0→y=0 (top), r=1→y=h (bottom); 50% of particles below 87% of screen.
    std::uniform_real_distribution<float> dy01( 0.0f, 1.0f );
    s.x = dx( g_sand_rng );
    const float r = dy01( g_sand_rng );
    const float inv = 1.0f - r;
    s.y = ( 1.0f - inv * inv * inv ) * static_cast<float>( g_h );
    // Initial velocity: slight rightward wind + small random scatter.
    std::uniform_real_distribution<float> dvx( 0.1f, 0.6f );
    std::uniform_real_distribution<float> dvy( -0.3f, 0.1f );
    s.vx = dvx( g_sand_rng );
    s.vy = dvy( g_sand_rng );
    // Lifetime: 40-120 frames (~2-6 sec at 20fps).
    std::uniform_real_distribution<float> dl( 40.0f, 120.0f );
    s.max_life = dl( g_sand_rng );
    s.life = s.max_life;
    // Size: 1 or 2px. Probability of 2px scales with spawn depth —
    // top grains are almost always 1px, bottom grains ~50% are 2px.
    const float y_frac = s.y / std::max( 1.0f, static_cast<float>( g_h ) );
    s.size = ( dy01( g_sand_rng ) < y_frac * 0.5f ) ? 2 : 1;
}

void sand_init_all()
{
    g_sand.resize( SAND_COUNT );
    for( auto &s : g_sand ) {
        sand_spawn( s );
        // Stagger initial lifetimes so they don't all die at once.
        std::uniform_real_distribution<float> stagger( 0.0f, 1.0f );
        s.life = stagger( g_sand_rng ) * s.max_life;
    }
}

} // anonymous namespace

namespace lighting
{

auto plexus_init() -> void
{
    g_particles.clear();
    g_sand.clear();
    g_pixels.clear();
    g_w = 0;
    g_h = 0;
    g_gen = 0;
    g_frame = 0;
    g_cfg = plexus_config{};
}

auto plexus_finish() -> void
{
    g_particles.clear();
    g_sand.clear();
    g_pixels.clear();
    g_w = 0;
    g_h = 0;
}

auto plexus_resize( int width, int height ) -> void
{
    g_w = width;
    g_h = height;
    g_pixels.resize( static_cast<std::size_t>( g_w ) * g_h * 4 );

    // Fill with background colour.
    for( std::size_t i = 0; i < g_pixels.size(); i += 4 ) {
        g_pixels[i + 0] = g_cfg.bg_r;
        g_pixels[i + 1] = g_cfg.bg_g;
        g_pixels[i + 2] = g_cfg.bg_b;
        g_pixels[i + 3] = g_cfg.bg_a;
    }

    // Respawn particles with a deterministic seed.
    std::mt19937 rng( 0xB17E );
    std::uniform_real_distribution<float> dist_x( 0.0f, static_cast<float>( g_w ) );
    std::uniform_real_distribution<float> dist_y( 0.0f, static_cast<float>( g_h ) );
    std::uniform_real_distribution<float> dist_life( 0.0f, 1.0f );

    g_particles.resize( static_cast<std::size_t>( g_cfg.particle_count ) );
    for( int i = 0; i < g_cfg.particle_count; ++i ) {
        auto &p = g_particles[static_cast<std::size_t>( i )];
        p.x = dist_x( rng );
        p.y = dist_y( rng );
        p.vx = 0.0f;
        p.vy = 0.0f;
        p.life = dist_life( rng );
        p.glyph = ( i % 10 == 0 ) ? static_cast<std::uint8_t>( ( i % 4 ) + 1 ) : 0;
    }
    sand_init_all();
}

auto plexus_step() -> void
{
    if( g_w <= 0 || g_h <= 0 ) {
        return;
    }

    ++g_frame;

    // 1. Advance particles.
    const int n = static_cast<int>( g_particles.size() );
    for( int i = 0; i < n; ++i ) {
        auto &p = g_particles[static_cast<std::size_t>( i )];
        float noise = static_cast<float>(
                          corr_vnoise( static_cast<int>( p.x ), static_cast<int>( p.y ), 64, g_frame / 30 ) );
        float ang = noise * 3.14159265f * 4.0f;
        p.vx = ( p.vx + std::cos( ang ) * g_cfg.speed_scale ) * 0.98f;
        p.vy = ( p.vy + std::sin( ang ) * g_cfg.speed_scale ) * 0.98f;
        p.x += p.vx;
        p.y += p.vy;
        p.x = wrap_coord( p.x, g_w );
        p.y = wrap_coord( p.y, g_h );
        p.life = std::sin( g_frame * 0.015f + i * 0.7f ) * 0.5f + 0.5f;
    }

    // 2. Clear buffer to background.
    for( std::size_t i = 0; i < g_pixels.size(); i += 4 ) {
        g_pixels[i + 0] = g_cfg.bg_r;
        g_pixels[i + 1] = g_cfg.bg_g;
        g_pixels[i + 2] = g_cfg.bg_b;
        g_pixels[i + 3] = g_cfg.bg_a;
    }

    // Wind gust model: a slow sine-composite that modulates all wind speeds.
    const float gust = 0.6f
                        + 0.25f * std::sin( g_frame * 0.007f )
                        + 0.15f * std::sin( g_frame * 0.019f + 1.3f );

    // Accumulated wind phase for macro noise (monotonic).
    static float s_wind_phase = 0;
    s_wind_phase += 0.3f * gust;

    // 2b. Sand particles — real physics: wind, gravity, turbulence, lifetime.
    {
        const float wind_force = 0.15f * gust;  // horizontal push (rightward)
        const float gravity = 0.02f;             // gentle downward pull
        const float updraft_force = -0.08f * gust; // upward lift (negative y)
        const float turbulence = 0.05f;           // random scatter
        const float drag = 0.97f;                 // velocity damping

        for( auto &s : g_sand ) {
            // Decrease lifetime.
            s.life -= 1.0f;
            if( s.life <= 0.0f ) {
                sand_spawn( s );
                continue;
            }
            // Forces: wind + gravity + updraft (stronger near bottom) + turbulence.
            const float vy_norm = s.y / std::max( 1.0f, static_cast<float>( g_h ) );
            const float updraft_scale = vy_norm * vy_norm; // stronger near bottom
            s.vx += wind_force;
            s.vy += gravity + updraft_force * updraft_scale;
            // Turbulence from noise field.
            const float turb_angle = static_cast<float>(
                                         corr_vnoise( static_cast<int>( s.x ), static_cast<int>( s.y ),
                                                      32, 0xACEDu ) ) * 6.2832f;
            s.vx += std::cos( turb_angle ) * turbulence;
            s.vy += std::sin( turb_angle ) * turbulence;
            // Damping.
            s.vx *= drag;
            s.vy *= drag;
            // Integrate.
            s.x += s.vx;
            s.y += s.vy;
            // Wrap horizontally; respawn if off-screen vertically.
            s.x = wrap_coord( s.x, g_w );
            if( s.y < -5.0f || s.y > static_cast<float>( g_h ) + 5.0f ) {
                sand_spawn( s );
                continue;
            }
            // Alpha: fade in over first 10% of life, fade out over last 20%.
            const float life_frac = s.life / s.max_life;
            float alpha_mult = 1.0f;
            if( life_frac > 0.9f ) {
                alpha_mult = ( 1.0f - life_frac ) / 0.1f; // fade in
            } else if( life_frac < 0.2f ) {
                alpha_mult = life_frac / 0.2f; // fade out
            }
            // Base alpha: brighter near bottom.
            const auto a = static_cast<std::uint8_t>(
                               ( 20.0f + vy_norm * 60.0f ) * alpha_mult );
            if( a < 2 ) { continue; }
            const int px = static_cast<int>( s.x );
            const int py = static_cast<int>( s.y );
            if( s.size == 2 ) {
                blend_px( px, py, g_cfg.line_r, g_cfg.line_g, g_cfg.line_b, a );
                blend_px( px + 1, py, g_cfg.line_r, g_cfg.line_g, g_cfg.line_b, a );
                blend_px( px, py + 1, g_cfg.line_r, g_cfg.line_g, g_cfg.line_b, a );
                blend_px( px + 1, py + 1, g_cfg.line_r, g_cfg.line_g, g_cfg.line_b, a );
            } else {
                blend_px( px, py, g_cfg.line_r, g_cfg.line_g, g_cfg.line_b, a );
            }
        }
    }

    // 3. Connection lines (O(n²) — fine for 200 particles).
    const float max_dist = static_cast<float>( g_cfg.connection_dist );
    for( int i = 0; i < n; ++i ) {
        const auto &pi = g_particles[static_cast<std::size_t>( i )];
        for( int j = i + 1; j < n; ++j ) {
            const auto &pj = g_particles[static_cast<std::size_t>( j )];
            float dx = pi.x - pj.x;
            float dy = pi.y - pj.y;
            float dist = std::sqrt( dx * dx + dy * dy );
            if( dist < max_dist ) {
                float a = ( 1.0f - dist / max_dist ) * std::min( pi.life, pj.life );
                draw_line( static_cast<int>( pi.x ), static_cast<int>( pi.y ),
                           static_cast<int>( pj.x ), static_cast<int>( pj.y ),
                           g_cfg.line_r, g_cfg.line_g, g_cfg.line_b,
                           static_cast<std::uint8_t>( a * g_cfg.line_a ) );
            }
        }
    }

    // 4. Nodes.
    for( int i = 0; i < n; ++i ) {
        const auto &p = g_particles[static_cast<std::size_t>( i )];
        fill_circle( static_cast<int>( p.x ), static_cast<int>( p.y ),
                     g_cfg.node_radius,
                     g_cfg.node_r, g_cfg.node_g, g_cfg.node_b,
                     static_cast<std::uint8_t>( p.life * g_cfg.node_a ) );
    }

    // 5. Glyphs.
    for( int i = 0; i < n; ++i ) {
        const auto &p = g_particles[static_cast<std::size_t>( i )];
        if( p.glyph != 0 ) {
            draw_glyph( static_cast<int>( p.x ), static_cast<int>( p.y ),
                        p.glyph,
                        g_cfg.glyph_r, g_cfg.glyph_g, g_cfg.glyph_b,
                        static_cast<std::uint8_t>( p.life * g_cfg.glyph_a ) );
        }
    }

    // 6. Macro wind noise — slow-scrolling darkening, gust-modulated.
    {
        const float wind_x = s_wind_phase;
        constexpr int step = 4;
        constexpr unsigned wind_seed = 0x57494E44u;
        for( int sy = 0; sy < g_h; sy += step ) {
            for( int sx = 0; sx < g_w; sx += step ) {
                const double n = corr_vnoise(
                                     static_cast<int>( sx + wind_x ), sy, 120, wind_seed );
                if( n < 0.55 ) {
                    const float strength = ( 0.55f - static_cast<float>( n ) ) / 0.55f;
                    const auto a = static_cast<std::uint8_t>( strength * 180.0f );
                    for( int dy = 0; dy < step && sy + dy < g_h; ++dy ) {
                        for( int dx = 0; dx < step && sx + dx < g_w; ++dx ) {
                            blend_px( sx + dx, sy + dy, g_cfg.bg_r, g_cfg.bg_g, g_cfg.bg_b, a );
                        }
                    }
                }
            }
        }
    }

    ++g_gen;
}

auto plexus_pixels() -> const std::vector<std::uint8_t> &
{
    return g_pixels;
}

auto plexus_width() -> int
{
    return g_w;
}

auto plexus_height() -> int
{
    return g_h;
}

auto plexus_generation() -> unsigned
{
    return g_gen;
}

auto plexus_get_config() -> plexus_config &
{
    return g_cfg;
}

} // namespace lighting
