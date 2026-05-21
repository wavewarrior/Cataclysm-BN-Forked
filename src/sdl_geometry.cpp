#include "sdl_geometry.h"
#include "sdl_utils.h"
#include "debug.h"
#include "lighting/render_state.h"

#define dbg(x) DebugLogFL((x),DC::SDL)

// Phase 2i-B-4 plumbing — currently deactivated.
//
// Earlier attempt enqueued every legacy rect into render_state::queue_ui_rect
// so the ui_batcher could drain them on the swapchain. Problem surfaced on
// Win11: legacy text glyphs still draw through SDL_Renderer into
// display_buffer, then via the bridge blit. Queued GPU rects then draw ON
// TOP of the bridge — and opaque-black rects (window separators, status bar
// chrome, …) covered the text underneath. Symptom: "UI / fonts sometimes
// render black."
//
// The queue infrastructure (render_state::queue_ui_rect, flush_ui_rects,
// the ui_batcher pass in refresh_display) stays in tree but unused. It
// re-activates with the font-cutover commit, which also drops the legacy
// RenderFillRect path so rects + text both render via ui_batcher in a
// single z-coherent layer.
static void mirror_rect_to_gpu( const SDL_FRect & /*r*/, const SDL_Color & /*c*/ ) noexcept
{
    // intentionally empty — see comment above.
}

void GeometryRenderer::horizontal_line( const SDL_Renderer_Ptr &renderer, point pos, int x2,
                                        int thickness, const SDL_Color &color ) const
{
    SDL_FRect rect { static_cast<float>( pos.x ), static_cast<float>( pos.y ),
                     static_cast<float>( x2 - pos.x ), static_cast<float>( thickness ) };
    this->rect( renderer, rect, color );
}

void GeometryRenderer::vertical_line( const SDL_Renderer_Ptr &renderer, point pos, int y2,
                                      int thickness, const SDL_Color &color ) const
{
    SDL_FRect rect { static_cast<float>( pos.x ), static_cast<float>( pos.y ),
                     static_cast<float>( thickness ), static_cast<float>( y2 - pos.y ) };
    this->rect( renderer, rect, color );
}

void GeometryRenderer::rect( const SDL_Renderer_Ptr &renderer, point pos, int width,
                             int height, const SDL_Color &color ) const
{
    SDL_FRect rect { static_cast<float>( pos.x ), static_cast<float>( pos.y ),
                     static_cast<float>( width ), static_cast<float>( height ) };
    this->rect( renderer, rect, color );
}


void DefaultGeometryRenderer::rect( const SDL_Renderer_Ptr &renderer, const SDL_FRect &rect,
                                    const SDL_Color &color ) const
{
    // Phase 2i-B-4 transitional: still draw into display_buffer via the
    // legacy SDL_Renderer (so the bridge keeps the rect-under-text z-order
    // correct) AND queue the same rect into the GPU ui_batcher. The GPU
    // copy draws on top of the bridge blit, doubling intensity for any
    // translucent rect by α*α — visually negligible for opaque UI fills
    // (the dominant case). Once Font::OutputChar moves to a GPU queue,
    // we drop the RenderFillRect path entirely.
    SetRenderDrawColor( renderer, color.r, color.g, color.b, color.a );
    RenderFillRect( renderer, &rect );
    mirror_rect_to_gpu( rect, color );
}

ColorModulatedGeometryRenderer::ColorModulatedGeometryRenderer( const SDL_Renderer_Ptr &renderer )
{
    SDL_Surface_Ptr alt_surf = CreateSurface( sdl_color_pixel_format, 1, 1 );
    if( alt_surf ) {
        FillSurfaceRect( alt_surf, nullptr,
                         SDL_MapRGB( SDL_GetPixelFormatDetails( alt_surf->format ), nullptr,
                                     255, 255, 255 ) );

        tex.reset( SDL_CreateTextureFromSurface( renderer.get(), alt_surf.get() ) );
        alt_surf.reset();

        // Test to make sure color modulation is supported by renderer
        bool tex_enable = !SetTextureColorMod( tex, 0, 0, 0 );
        if( !tex_enable ) {
            tex.reset();
        }
        dbg( DL::Info ) << "ColorModulatedGeometryRenderer constructor() = " <<
                        ( tex_enable ? "FAIL" : "SUCCESS" ) << ". tex_enable = " << tex_enable;
    } else {
        dbg( DL::Error ) << "CreateRGBSurface failed: " << SDL_GetError();
    }
}

void ColorModulatedGeometryRenderer::rect( const SDL_Renderer_Ptr &renderer, const SDL_FRect &rect,
        const SDL_Color &color ) const
{
    if( tex ) {
        SetTextureColorMod( tex, color.r, color.g, color.b );
        RenderCopy( renderer, tex, nullptr, &rect );
        mirror_rect_to_gpu( rect, color );
    } else {
        DefaultGeometryRenderer::rect( renderer, rect, color );
    }
}

