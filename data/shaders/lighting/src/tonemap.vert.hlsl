// Fullscreen-triangle tonemap vertex shader.
//
// Emits a single oversized triangle covering the viewport from SV_VertexID
// alone — no vertex/instance buffer bound. UV runs 0..1 across the screen.
// Paired with tonemap.frag.hlsl; driven by tonemap_pass (3-vertex draw).

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VS_OUT main( uint vid : SV_VertexID )
{
    VS_OUT o;
    // vid 0->(0,0) 1->(2,0) 2->(0,2): a triangle whose intersection with the
    // [0,1] viewport is the full screen.
    float2 uv = float2( ( vid << 1 ) & 2, vid & 2 );
    o.uv  = uv;
    // Map UV [0,2] to clip space; flip Y (texture top-left origin).
    o.pos = float4( uv * float2( 2.0, -2.0 ) + float2( -1.0, 1.0 ), 0.0, 1.0 );
    return o;
}
