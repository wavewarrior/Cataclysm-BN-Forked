// Phase 0 SDL-tiles decomposition — render regression harness.
//
// Numeric/oracle-based render regression tests — no golden PNGs, no eyeball
// approval. All sections share one process-wide render_state singleton (tagged
// [.gpu][render] to keep them off non-GPU CI lanes).
//
// Run with: cata_test-tiles "[render]"
//
// ** ONE TEST_CASE only ** — the singleton g_display / render_state can only
// be initialised and torn down once per file. Each SECTION re-enters the test
// function from the top; the harness is initialised ONCE via a static flag so
// we don't create/destroy the GPU device on every section path.
//
// Fixtures:
//   A — compositor: UI rects → offscreen → stats
//   B — world-lit:  checker tile grid → offscreen
//   D — ui-over-world: UI rects + tile sprites composite
//   E — retention:  cleared-queue frame is darker

#include "catch/catch_amalgamated.hpp"
#include "lighting/render_state.h"
#include "lighting/gpu_geometry.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

struct sdl_video_session {
    bool ok = false;
    sdl_video_session() noexcept {
        ok = SDL_InitSubSystem( SDL_INIT_VIDEO );
    }
    ~sdl_video_session() noexcept {
        if( ok ) {
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
        }
    }
};

struct render_state_guard {
    ~render_state_guard() {
        lighting::shutdown_render_state();
    }
};

auto pixel_luma( const uint8_t *rgba ) -> float
{
    return 0.299f * rgba[0] + 0.587f * rgba[1] + 0.114f * rgba[2];
}

struct region_stats {
    double sum = 0.0;
    float  max_val = 0.0f;
    long   nonzero = 0;
    double mean = 0.0;
};

auto compute_region_stats( const std::vector<uint8_t> &pixels, int w, int h,
                           int rx, int ry, int rw, int rh ) -> region_stats
{
    region_stats s;
    const int x1 = std::max( 0, rx );
    const int y1 = std::max( 0, ry );
    const int x2 = std::min( w, rx + rw );
    const int y2 = std::min( h, ry + rh );
    long count = 0;
    for( int y = y1; y < y2; ++y ) {
        for( int x = x1; x < x2; ++x ) {
            const int idx = ( y * w + x ) * 4;
            if( idx + 3 >= static_cast<int>( pixels.size() ) ) {
                continue;
            }
            const float l = pixel_luma( &pixels[idx] );
            s.sum += l;
            if( l > s.max_val ) {
                s.max_val = l;
            }
            if( l > 0.001f ) {
                ++s.nonzero;
            }
            ++count;
        }
    }
    s.mean = count > 0 ? s.sum / static_cast<double>( count ) : 0.0;
    return s;
}

auto make_white_sprite( float x, float y, float w, float h ) -> lighting::sprite_instance
{
    lighting::sprite_instance s{};
    s.dst_x  = x;
    s.dst_y  = y;
    s.dst_w  = w;
    s.dst_h  = h;
    s.src_u  = 0.0f;
    s.src_v  = 0.0f;
    s.src_uw = 1.0f;
    s.src_vh = 1.0f;
    s.tint_r = 1.0f;
    s.tint_g = 1.0f;
    s.tint_b = 1.0f;
    s.tint_a = 1.0f;
    s.rotation = 0.0f;
    return s;
}

// Singleton test harness — initialised once on first access, torn down
// automatically at process exit.
struct test_harness {
    SDL_Window            *window = nullptr;
    render_state_guard     guard;
    lighting::render_state *rs = nullptr;

    auto init() -> bool {
        if( !sdl.ok ) { return false; }
    window = SDL_CreateWindow( "render_test", 320, 240,
                               SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY );
    if( !window ) { return false; }
    if( !lighting::init_render_state_on( window ) ) {
            SDL_DestroyWindow( window );
            window = nullptr;
            return false;
        }
        rs = &lighting::get_render_state();
        return rs->ready();
    }

    ~test_harness() {
        if( window ) { SDL_DestroyWindow( window ); }
    }

    sdl_video_session sdl;
};

auto get_harness() -> test_harness &
{
    static test_harness h{};
    static bool once = false;
    if( !once ) {
        once = h.init();
    }
    return h;
}

auto create_offscreen( lighting::render_state &rs, int w, int h ) -> SDL_GPUTexture *
{
    SDL_GPUTextureCreateInfo tci{};
    tci.type                 = SDL_GPU_TEXTURETYPE_2D;
    tci.format               = rs.device().swapchain_format();
    tci.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                               SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width                = static_cast<Uint32>( w );
    tci.height               = static_cast<Uint32>( h );
    tci.layer_count_or_depth = 1;
    tci.num_levels           = 1;
    tci.sample_count         = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture( rs.device().raw(), &tci );
}

auto render_and_capture( lighting::render_state &rs, SDL_GPUTexture *target,
                         int w, int h ) -> std::vector<uint8_t>
{
    lighting::frame_context ctx = rs.device().begin_frame();
    REQUIRE( ctx.valid() );

    constexpr float clear_black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    rs.tile_batcher().begin_pass( ctx.cmd_buffer, target,
                                  static_cast<Uint32>( w ), static_cast<Uint32>( h ),
                                  clear_black,
                                  static_cast<Uint32>( w ), static_cast<Uint32>( h ),
                                  rs.device().swapchain_format() );

    rs.flush_tile_sprites( rs.tile_batcher(), rs.gpu_sampler() );
    rs.flush_ui( rs.tile_batcher(), rs.gpu_sampler() );

    rs.tile_batcher().end_pass();
    rs.device().submit_frame( ctx );

    std::vector<uint8_t> pixels;
    REQUIRE( rs.capture_texture_to_rgba( target, w, h, pixels ) );
    REQUIRE( pixels.size() == static_cast<std::size_t>( w * h * 4 ) );
    return pixels;
}

} // namespace

TEST_CASE( "render_regression", "[.gpu][render]" )
{
    auto &h = get_harness();
    if( !h.rs ) {
        WARN( "test_harness init failed (no GPU or hidden window)" );
        return;
    }

    auto &rs = *h.rs;
    constexpr int W = 80;
    constexpr int H = 60;
    constexpr float TILE_PX = 8.0f;

    // -----------------------------------------------------------------------
    // Fixture A — Compositor: UI rects → offscreen → stats
    // -----------------------------------------------------------------------
    SECTION( "compositor_ui_rects" ) {
        SDL_GPUTexture *offscreen = create_offscreen( rs, W, H );
        REQUIRE( offscreen != nullptr );

        lighting::render_state::frame_light_inputs base_in{};
        base_in.tile_pixel_size = TILE_PX;
        rs.begin_lighting_frame( base_in );

        rs.queue_ui_rect( 0.0f, 0.0f,
                          static_cast<float>( W ),
                          static_cast<float>( H ),
                          1.0f, 1.0f, 1.0f, 1.0f );

        const auto pixels = render_and_capture( rs, offscreen, W, H );
        const region_stats full = compute_region_stats( pixels, W, H, 0, 0, W, H );
        CHECK( full.nonzero > 0 );
        CHECK( full.max_val > 0.001f );
        CHECK( full.mean > 0.0 );

        SDL_ReleaseGPUTexture( rs.device().raw(), offscreen );
    }

    // -----------------------------------------------------------------------
    // Fixture B — World-lit: checker tile grid → offscreen
    // -----------------------------------------------------------------------
    SECTION( "world_lit_checker" ) {
        auto *white = rs.geometry().white_texture();
        REQUIRE( white != nullptr );

        SDL_GPUTexture *offscreen = create_offscreen( rs, W, H );
        REQUIRE( offscreen != nullptr );

        lighting::render_state::frame_light_inputs base_in{};
        base_in.tile_pixel_size = TILE_PX;
        rs.begin_lighting_frame( base_in );

        const int cols = W / static_cast<int>( TILE_PX );
        const int rows = H / static_cast<int>( TILE_PX );
        for( int r = 0; r < rows; ++r ) {
            for( int c = 0; c < cols; ++c ) {
                const float x = static_cast<float>( c ) * TILE_PX;
                const float y = static_cast<float>( r ) * TILE_PX;
                const float tint = ( r + c ) % 2 == 0 ? 1.0f : 0.3f;
                lighting::sprite_instance s = make_white_sprite( x, y, TILE_PX, TILE_PX );
                s.tint_r = tint;
                s.tint_g = tint;
                s.tint_b = tint;
                rs.queue_tile_sprite( white, s );
            }
        }

        const auto pixels = render_and_capture( rs, offscreen, W, H );
        const region_stats full = compute_region_stats( pixels, W, H, 0, 0, W, H );
        CHECK( full.nonzero > 0 );
        CHECK( full.max_val > 0.1f );

        SDL_ReleaseGPUTexture( rs.device().raw(), offscreen );
    }

    // -----------------------------------------------------------------------
    // Fixture D — UI over world: composite world + UI layers
    // -----------------------------------------------------------------------
    SECTION( "ui_over_world" ) {
        auto *white = rs.geometry().white_texture();
        REQUIRE( white != nullptr );

        SDL_GPUTexture *offscreen = create_offscreen( rs, W, H );
        REQUIRE( offscreen != nullptr );

        lighting::render_state::frame_light_inputs base_in{};
        base_in.tile_pixel_size = TILE_PX;
        rs.begin_lighting_frame( base_in );

        const int cols = W / static_cast<int>( TILE_PX );
        const int rows = H / static_cast<int>( TILE_PX );
        for( int r = 0; r < rows; ++r ) {
            for( int c = 0; c < cols; ++c ) {
                const float x = static_cast<float>( c ) * TILE_PX;
                const float y = static_cast<float>( r ) * TILE_PX;
                const float tint = ( r + c ) % 2 == 0 ? 0.5f : 0.2f;
                lighting::sprite_instance s = make_white_sprite( x, y, TILE_PX, TILE_PX );
                s.tint_r = tint;
                s.tint_g = tint;
                s.tint_b = tint;
                rs.queue_tile_sprite( white, s );
            }
        }

        rs.queue_ui_rect( static_cast<float>( W ) / 4.0f,
                          static_cast<float>( H ) / 4.0f,
                          static_cast<float>( W ) / 2.0f,
                          static_cast<float>( H ) / 2.0f,
                          1.0f, 0.0f, 0.0f, 0.8f );

        const auto pixels = render_and_capture( rs, offscreen, W, H );
        const region_stats centre = compute_region_stats( pixels, W, H,
                                    W / 4, H / 4, W / 2, H / 2 );
        const region_stats corner = compute_region_stats( pixels, W, H,
                                    0, 0, W / 4, H / 4 );
        CHECK( centre.nonzero > 0 );
        CHECK( corner.nonzero > 0 );
        CHECK( centre.max_val > 0.01f );

        SDL_ReleaseGPUTexture( rs.device().raw(), offscreen );
    }

    // -----------------------------------------------------------------------
    // Fixture E — Retention: cleared-queue frame is darker
    // -----------------------------------------------------------------------
    SECTION( "retention_cleared_queue_darker" ) {
        auto *white = rs.geometry().white_texture();
        REQUIRE( white != nullptr );

        SDL_GPUTexture *offscreen = create_offscreen( rs, W, H );
        REQUIRE( offscreen != nullptr );

        lighting::render_state::frame_light_inputs base_in{};
        base_in.tile_pixel_size = TILE_PX;
        rs.begin_lighting_frame( base_in );

        const int cols = W / static_cast<int>( TILE_PX );
        const int rows = H / static_cast<int>( TILE_PX );
        for( int r = 0; r < rows; ++r ) {
            for( int c = 0; c < cols; ++c ) {
                const float x = static_cast<float>( c ) * TILE_PX;
                const float y = static_cast<float>( r ) * TILE_PX;
                lighting::sprite_instance s = make_white_sprite( x, y, TILE_PX, TILE_PX );
                s.tint_r = 0.5f;
                s.tint_g = 0.5f;
                s.tint_b = 0.5f;
                rs.queue_tile_sprite( white, s );
            }
        }

        const auto frame1 = render_and_capture( rs, offscreen, W, H );

        rs.clear_tile_queue();
        rs.clear_ui_queues();
        const auto frame2 = render_and_capture( rs, offscreen, W, H );

        const region_stats f1s = compute_region_stats( frame1, W, H, 0, 0, W, H );
        const region_stats f2s = compute_region_stats( frame2, W, H, 0, 0, W, H );
        CHECK( f1s.nonzero > 0 );
        CHECK( f1s.mean > 0.0 );
        CHECK( f2s.mean < f1s.mean );

        SDL_ReleaseGPUTexture( rs.device().raw(), offscreen );
    }
}
