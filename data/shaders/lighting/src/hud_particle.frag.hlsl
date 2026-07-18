// HUD particle fragment shader — soft circle with radial falloff.
// No texture — purely procedural.

struct PS_IN {
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float4 main( PS_IN i ) : SV_TARGET
{
    // Soft circle: radial falloff from center
    const float2 center = i.uv - 0.5f;
    const float dist = length( center );
    const float alpha = smoothstep( 0.5, 0.0, dist ) * i.color.a;
    
    return float4( i.color.rgb * alpha, alpha );
}