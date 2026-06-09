#include "rmlui_render_interface.h"

// PHASE 1 stubs. These compile to a concrete RenderInterface so the layer can
// initialise RmlUi, but they upload/draw nothing. PHASE 2 replaces every body
// with the real SDL_GPU implementation (CPU-copy compile, ring-buffer upload in
// prepare(), in-pass draw with the 1x1 white texture for untextured geometry).

namespace lighting
{

Rml::CompiledGeometryHandle rmlui_render_interface::CompileGeometry(
    Rml::Span<const Rml::Vertex> /*vertices*/, Rml::Span<const int> /*indices*/ )
{
    return 0;
}

void rmlui_render_interface::RenderGeometry( Rml::CompiledGeometryHandle /*geometry*/,
        Rml::Vector2f /*translation*/, Rml::TextureHandle /*texture*/ )
{
}

void rmlui_render_interface::ReleaseGeometry( Rml::CompiledGeometryHandle /*geometry*/ )
{
}

Rml::TextureHandle rmlui_render_interface::LoadTexture( Rml::Vector2i &texture_dimensions,
        const Rml::String & /*source*/ )
{
    texture_dimensions = Rml::Vector2i( 0, 0 );
    return 0;
}

Rml::TextureHandle rmlui_render_interface::GenerateTexture(
    Rml::Span<const Rml::byte> /*source*/, Rml::Vector2i /*source_dimensions*/ )
{
    return 0;
}

void rmlui_render_interface::ReleaseTexture( Rml::TextureHandle /*texture*/ )
{
}

void rmlui_render_interface::EnableScissorRegion( bool /*enable*/ )
{
}

void rmlui_render_interface::SetScissorRegion( Rml::Rectanglei /*region*/ )
{
}

}  // namespace lighting
