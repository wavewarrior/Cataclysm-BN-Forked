#include "sdl_geometry.h"
#include "sdl_utils.h"
#include "debug.h"
#include "lighting/render_state.h"

#define dbg(x) DebugLogFL((x),DC::SDL)

// Phase 2i-B-6 cont. — disabled again until cata_tiles moves off
// display_buffer in 2i-B-5.
//
// Repeated attempt history (4 commits worth):
//   * Enable queue → opaque-black bg rects cover legacy text on bridge.
//   * Migrate fonts to GPU → text problem fixed, but full-screen
//     window-bg rects still cover sprites that are on the bridge.
//   * Dual-path (rect both legacy + GPU) → still covers sprites because
//     the GPU pass runs *after* the bridge blit; sprites are on the
//     bridge but rects redraw on top.
//
// Root cause: as long as sprites live in display_buffer (i.e. cata_tiles
// still uses SDL_Renderer), the only frame-z-order that works is
// `bridge → text-only-on-top`. Anything more than text in the GPU pass
// covers sprites that should be visible.
//
// Text gets a pass because cata_tiles never lays text into display_buffer
// — only sprites. Rects do go on top of sprites in some legacy windows
// (color block overlays), so once cata_tiles is ported we re-enable
// rects on GPU.
static void mirror_rect_to_gpu( const SDL_FRect & /*r*/, const SDL_Color & /*c*/ ) noexcept
{
    // intentionally empty — see comment above. Re-enables with 2i-B-5.
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
    // Dual-path while cata_tiles still draws sprites into display_buffer.
    // Reason: legacy z-order inside a frame is `clear → bg rects → sprites
    // → text`. The bridge blit happens once per frame and copies whatever
    // was last written to display_buffer onto the GPU swapchain. If we
    // dropped the SDL_RenderFillRect path, sprites would land in
    // display_buffer (still legacy) but bg rects would only exist in the
    // GPU ui_batcher queue, which flushes ON TOP of the bridge blit —
    // every UI window's bg rect would then cover the sprites it used to
    // sit under. Symptom on Win11: w_terrain map area renders solid
    // black, mouse-move briefly shows the map between covers.
    //
    // Dual-path keeps the legacy bg rect in display_buffer so the bridge
    // ships it underneath the sprites, while the GPU queue copy still
    // draws on top of the bridge for the post-cata_tiles cutover work.
    // Translucent rects double-blend by α(1-α) — minor saturation, the
    // dominant opaque case is unchanged. Once 2i-B-5 ports cata_tiles
    // off display_buffer, the legacy path here gets dropped for real.
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
    // Same dual-path reasoning as DefaultGeometryRenderer::rect above.
    if( tex ) {
        SetTextureColorMod( tex, color.r, color.g, color.b );
        RenderCopy( renderer, tex, nullptr, &rect );
        mirror_rect_to_gpu( rect, color );
    } else {
        DefaultGeometryRenderer::rect( renderer, rect, color );
    }
}

