#include "sdl_geometry.h"
#include "sdl_utils.h"
#include "debug.h"
#include "lighting/render_state.h"

#define dbg(x) DebugLogFL((x),DC::SDL)

// Phase 2i-B-5 cutover: cata_tiles' unrotated draws now land on the
// GPU swapchain via the tile_batcher pass (see draw_sprite_at). Rects
// can finally migrate too: they queue into render_state and flush in
// the ui_batcher pass *after* the tile_batcher pass, so colour-block
// overlays render on top of GPU sprites and UI bgs render on top of
// (mostly empty) bridge.
//
// Legacy SDL_RenderFillRect path dropped in the rect impl below;
// display_buffer no longer receives rect fills.
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
    // Single GPU path now that cata_tiles' draws are also off
    // display_buffer (2i-B-5 cutover). Rects queue into the GPU
    // ui_batcher pass; flush runs after the tile_batcher pass so
    // colour-block overlays land on top of GPU sprites and UI bgs
    // land on top of (mostly empty) bridge.
    mirror_rect_to_gpu( rect, color );
}

ColorModulatedGeometryRenderer::ColorModulatedGeometryRenderer( const SDL_Renderer_Ptr
        & /*renderer*/ )
{
    // The colour-modulated SDL_Texture trick was an SDL_Renderer
    // back-end workaround for blend-rate-limited drivers; SDL_GPU has
    // no such limitation. Constructor kept so sdltiles.cpp's
    // USE_COLOR_MODULATED_TEXTURES branch keeps compiling; the rect
    // impl below delegates to DefaultGeometryRenderer.
    dbg( DL::Info ) << "ColorModulatedGeometryRenderer: GPU-backed (legacy SDL_Texture path removed).";
}

void ColorModulatedGeometryRenderer::rect( const SDL_Renderer_Ptr &renderer, const SDL_FRect &rect,
        const SDL_Color &color ) const
{
    DefaultGeometryRenderer::rect( renderer, rect, color );
}

