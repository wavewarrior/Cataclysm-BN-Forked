// RmlUi vertex shader (spike). Transforms Rml::Vertex (pixel-space position +
// premultiplied RGBA8 colour + UV) into clip space, matching the canonical
// RmlUi backend shader. Pixel->NDC mirrors sprite.vert (Y-down screen space).
//
// Vertex attributes (see rmlui_render_interface pipeline): location 0 position
// (FLOAT2), 1 colour (UBYTE4_NORM -> float4 0..1), 2 tex_coord (FLOAT2).
// [[vk::location]] pins SPIR-V input locations so they match the C++ attributes.

// Vertex uniform slot 0 -> register(b0, space1) (SDL_PushGPUVertexUniformData).
// translation = per-RenderGeometry offset (used when no CSS transform);
// viewport = logical projection pixels; elem_transform = CSS transform matrix
// (identity when no transform is active).
cbuffer VertParams : register( b0, space1 ) {
    float2 translation;
    float2 viewport_size;
    column_major float4x4 elem_transform; // identity when no CSS transform
};

struct VS_IN {
    [[vk::location( 0 )]] float2 position : TEXCOORD0;
    [[vk::location( 1 )]] float4 colour   : TEXCOORD1;
    [[vk::location( 2 )]] float2 uv        : TEXCOORD2;
};

struct VS_OUT {
    float4 pos    : SV_Position;
    float4 colour : TEXCOORD0;
    float2 uv      : TEXCOORD1;
};

VS_OUT main( VS_IN i )
{
    // When elem_transform is identity, the mul reduces to (p + translation, 0, 1)
    // so the math is equivalent to the original path — zero perf regression.
    const float4 tp = mul( elem_transform, float4( i.position + translation, 0.0, 1.0 ) );
    const float2 ndc = float2( tp.x / viewport_size.x *  2.0 - 1.0,
                               tp.y / viewport_size.y * -2.0 + 1.0 );
    VS_OUT o;
    o.pos    = float4( ndc, 0.0, 1.0 );
    o.colour = i.colour;
    o.uv     = i.uv;
    return o;
}
