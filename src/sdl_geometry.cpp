#include "sdl_geometry.h"
#include "sdl_utils.h"
#include "debug.h"
#include "lighting/render_state.h"

#define dbg(x) DebugLogFL((x),DC::SDL)

// Phase 2i-B-4: route every GeometryRenderer rect into the lighting
// render_state's deferred UI quad queue. The legacy display_buffer no longer
// receives fill-rect draws (it would re-appear in the bridge blit and cover
// the GPU draw). The SDL_Renderer_Ptr arg on the public interface is now
// vestigial — kept so call sites compile unchanged; will be stripped in
// 2i-B-6 when SDL_Renderer is deleted wholesale.
static void mirror_rect_to_gpu( const SDL_FRect &r, const SDL_Color &c ) noexcept
{
    auto &rs = lighting::get_render_state();
    if( !rs.ready() ) {
        return;
    }
    constexpr float inv255 = 1.0f / 255.0f;
    rs.queue_ui_rect( r.x, r.y, r.w, r.h,
                      static_cast<float>( c.r ) * inv255,
                      static_cast<float>( c.g ) * inv255,
                      static_cast<float>( c.b ) * inv255,
                      static_cast<float>( c.a ) * inv255 );
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


void DefaultGeometryRenderer::rect( const SDL_Renderer_Ptr & /*renderer*/, const SDL_FRect &rect,
                                    const SDL_Color &color ) const
{
    // No more RenderFillRect into display_buffer — the GPU queue is the
    // single source of truth for UI rects.
    mirror_rect_to_gpu( rect, color );
}

ColorModulatedGeometryRenderer::ColorModulatedGeometryRenderer( const SDL_Renderer_Ptr & /*renderer*/ )
{
    // The legacy 1×1 white SDL_Texture this class once held is unused now
    // that all rects go through render_state::queue_ui_rect (which samples
    // the lighting subsystem's own 1×1 white SDL_GPUTexture). Constructor
    // kept so existing call sites in sdltiles.cpp still compile.
    dbg( DL::Info ) << "ColorModulatedGeometryRenderer: GPU-backed (legacy SDL_Texture path removed).";
}

void ColorModulatedGeometryRenderer::rect( const SDL_Renderer_Ptr &renderer, const SDL_FRect &rect,
        const SDL_Color &color ) const
{
    // Same path as DefaultGeometryRenderer now — the "color-modulated
    // texture" trick was an SDL_Renderer fallback for blend-rate limited
    // back-ends and is irrelevant under SDL_GPU.
    DefaultGeometryRenderer::rect( renderer, rect, color );
}

