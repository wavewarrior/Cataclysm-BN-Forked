#include "sdl_geometry.h"
#include "sdl_utils.h"
#include "debug.h"
#include "lighting/render_state.h"

#define dbg(x) DebugLogFL((x),DC::SDL)

// Phase 2i-B-2 bridge: every GeometryRenderer::rect that hits the hidden
// SDL_Renderer also pushes a coloured sprite_instance into the deferred UI
// queue maintained by lighting::render_state. refresh_display() drains the
// queue through ui_batcher each frame, so UI rectangles re-appear on the
// visible window without touching any caller. Sub-pixel rects (w or h ≤ 0)
// are skipped — they'd otherwise upload zero-area instances and the
// fragment shader would still allocate per-tile bin slots.
static void mirror_rect_to_gpu( const SDL_FRect &r, const SDL_Color &c ) noexcept
{
    if( r.w <= 0.0f || r.h <= 0.0f ) {
        return;
    }
    lighting::get_render_state().queue_ui_rect(
        r.x, r.y, r.w, r.h,
        static_cast<float>( c.r ) / 255.0f,
        static_cast<float>( c.g ) / 255.0f,
        static_cast<float>( c.b ) / 255.0f,
        static_cast<float>( c.a ) / 255.0f );
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

