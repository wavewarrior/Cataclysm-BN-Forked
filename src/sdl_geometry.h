#pragma once

#include <memory>

#include "sdl_wrappers.h"
#include "point.h"

/// Interface to render geometry with SDL_Renderer.
class GeometryRenderer
{
    public:
        virtual ~GeometryRenderer() = default;

        /// Renders a SDL rectangle with given color.
        virtual void rect( const SDL_Renderer_Ptr &renderer, const SDL_FRect &rect,
                           const SDL_Color &color ) const = 0;

        /// Renders a point+width+height defined rectangle with given color.
        void rect( const SDL_Renderer_Ptr &renderer, point pos, int width, int height,
                   const SDL_Color &color ) const;

        /// Renders a straight horizontal line with given thickness and color.
        void horizontal_line( const SDL_Renderer_Ptr &renderer, point pos, int x2, int thickness,
                              const SDL_Color &color ) const;

        /// Renders a straight vertical line with given thickness and color.
        void vertical_line( const SDL_Renderer_Ptr &renderer, point pos, int y2, int thickness,
                            const SDL_Color &color ) const;
};
using GeometryRenderer_Ptr = std::unique_ptr<GeometryRenderer>;

/// Implementation of a GeometryRenderer using default RenderFillRect.
class DefaultGeometryRenderer : public GeometryRenderer
{
    public:
        void rect( const SDL_Renderer_Ptr &renderer, const SDL_FRect &rect,
                   const SDL_Color &color ) const override;
};

/// Legacy alias. The color-modulated-texture fallback only mattered for
/// SDL_Renderer back-ends that throttled SDL_RenderFillRect; SDL_GPU has no
/// such limitation, so this class delegates entirely to DefaultGeometryRenderer.
/// Kept as a distinct type so sdltiles.cpp's USE_COLOR_MODULATED_TEXTURES
/// branch keeps compiling — both subclasses now hit the same GPU queue.
class ColorModulatedGeometryRenderer: public DefaultGeometryRenderer
{
    public:
        ColorModulatedGeometryRenderer( const SDL_Renderer_Ptr &renderer );

        void rect( const SDL_Renderer_Ptr &renderer, const SDL_FRect &rect,
                   const SDL_Color &color ) const override;
};


