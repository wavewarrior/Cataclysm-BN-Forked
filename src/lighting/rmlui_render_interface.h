#pragma once
#ifndef CATA_SRC_LIGHTING_RMLUI_RENDER_INTERFACE_H
#define CATA_SRC_LIGHTING_RMLUI_RENDER_INTERFACE_H

#include <RmlUi/Core/RenderInterface.h>

// RmlUi RenderInterface over SDL_GPU.
//
// PHASE 1 (current): stub bodies only — just enough to be a concrete class so
// Rml::Initialise() accepts it and the layer's init/shutdown can be proven.
// Nothing is uploaded or drawn yet.
//
// PHASE 2 will fill the GPU bodies, using the upload-timing-safe design forced
// by the D3D12 single-pass invariant: CompileGeometry copies vertices/indices
// to CPU-side storage only; the GPU upload happens in the layer's prepare()
// (OUTSIDE the render pass); RenderGeometry records draws INSIDE the open
// swapchain pass. See the plan + src/lighting/CLAUDE.md.

namespace lighting
{

class rmlui_render_interface : public Rml::RenderInterface
{
    public:
        // --- Required (pure virtual) ---
        Rml::CompiledGeometryHandle CompileGeometry( Rml::Span<const Rml::Vertex> vertices,
                Rml::Span<const int> indices ) override;
        void RenderGeometry( Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                             Rml::TextureHandle texture ) override;
        void ReleaseGeometry( Rml::CompiledGeometryHandle geometry ) override;

        Rml::TextureHandle LoadTexture( Rml::Vector2i &texture_dimensions,
                                        const Rml::String &source ) override;
        Rml::TextureHandle GenerateTexture( Rml::Span<const Rml::byte> source,
                                            Rml::Vector2i source_dimensions ) override;
        void ReleaseTexture( Rml::TextureHandle texture ) override;

        void EnableScissorRegion( bool enable ) override;
        void SetScissorRegion( Rml::Rectanglei region ) override;
};

}  // namespace lighting

#endif  // CATA_SRC_LIGHTING_RMLUI_RENDER_INTERFACE_H
