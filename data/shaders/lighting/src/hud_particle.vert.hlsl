// HUD particle vertex shader — procedural quad from instance data.
// Generates a rotated square centered at (x, y) with the given size.
// Follows rain_droplet.vert.hlsl pattern: StructuredBuffer + FrameParams cbuffer.
// All fields are scalar floats to avoid HLSL float3/float4 alignment pitfalls.

struct ParticleInstance {
    float x, y, size, alpha;     // position (screen px), diameter, opacity
    float r, g, b, rotation;     // colour RGB, rotation (radians)
    float pad0, pad1, pad2, pad3; // pad to 48 bytes (12 floats)
};

StructuredBuffer<ParticleInstance> Instances : register( t0, space0 );

cbuffer FrameParams : register( b0, space1 ) {
    float2 target_size;
    uint instance_base;
    uint fp_pad;
};

struct VS_OUT {
    float4 pos : SV_POSITION;
    float4 colour : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

// 6-vertex triangle list (2 triangles = 1 quad), matching rain's pattern.
static const float2 quad_uv[6] = {
    float2( 0, 0 ), float2( 1, 0 ), float2( 0, 1 ),
    float2( 1, 0 ), float2( 1, 1 ), float2( 0, 1 )
};

VS_OUT main( uint vid : SV_VertexID, uint iid : SV_InstanceID )
{
    const ParticleInstance p = Instances[iid + instance_base];
    const float2 uv = quad_uv[vid];

    // Rotated quad corners: center at origin, scale by size, rotate.
    const float2 local = ( uv - 0.5 ) * p.size;
    const float c = cos( p.rotation );
    const float s = sin( p.rotation );
    const float2 rotated = float2(
        local.x * c - local.y * s,
        local.x * s + local.y * c
    );
    const float2 screen_pos = float2( p.x, p.y ) + rotated;

    // Convert to NDC.
    const float2 ndc = float2(
        screen_pos.x / target_size.x * 2.0 - 1.0,
        -( screen_pos.y / target_size.y * 2.0 - 1.0 )
    );

    VS_OUT o;
    o.pos = float4( ndc, 0.0, 1.0 );
    o.colour = float4( p.r, p.g, p.b, p.alpha );
    o.uv = uv;
    return o;
}
