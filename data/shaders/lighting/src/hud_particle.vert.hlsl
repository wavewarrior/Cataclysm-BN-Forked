// HUD particle vertex shader — procedural quad from instance data.
// Each instance encodes (x, y, size, alpha, r, g, b, rotation) as a 64-byte struct.
// Generates a rotated square centered at (x, y) with the given size.

cbuffer ParticleInstance : register( t0 ) {
    float4 pos_size;    // xy = position, z = size, w = alpha
    float4 color;       // rgb = color, a = unused
    float rotation;     // degrees
    float3 padding;
};

struct VS_IN {
    uint vertex_id : VERTEXID;
};

struct VS_OUT {
    float4 pos : SV_POSITION;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

VS_OUT vertex_main( VS_IN i )
{
    // Procedural quad: 4 vertices per instance
    const float2 corner = float2( ( i.vertex_id & 1 ) - 0.5f,
                                   ( ( i.vertex_id >> 1 ) & 1 ) - 0.5f );
    
    // Rotate
    const float rad = radians( rotation );
    const float cos_a = cos( rad );
    const float sin_a = sin( rad );
    const float2 rotated = float2( corner.x * cos_a - corner.y * sin_a,
                                    corner.x * sin_a + corner.y * cos_a );
    
    // Scale and translate
    const float2 screen_pos = pos_size.xy + rotated * pos_size.z;
    
    // Convert to NDC (assuming viewport is passed via uniform or calculated)
    // For simplicity, we'll use a fixed viewport calculation
    const float2 ndc = screen_pos / float2( 1920.0, 1080.0 ) * 2.0 - 1.0;
    ndc.y = -ndc.y; // Flip Y
    
    VS_OUT o;
    o.pos = float4( ndc, 0.0, 1.0 );
    o.color = float4( color.rgb, pos_size.w );
    o.uv = corner + 0.5f;
    return o;
}