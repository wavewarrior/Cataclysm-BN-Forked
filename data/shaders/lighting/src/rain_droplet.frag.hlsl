// Rain droplet fragment shader — procedural vertical streak.
//
// No texture sampling needed. Uses the local quad Y coordinate (uv.y, 0=top)
// to create a bright-at-top / transparent-at-bottom gradient that looks like
// a falling rain streak. Per-instance tint provides the light-blue colour.

struct PS_IN {
    float4 pos  : SV_Position;
    float2 uv   : TEXCOORD0; // local quad coord (0..1), y=0 is top of streak
    float4 tint : TEXCOORD1; // per-instance color tint from sprite_instance
};

float4 main( PS_IN i ) : SV_Target
{
    // Vertical gradient: bright at top (uv.y=0), transparent at bottom.
    // The streak extends slightly beyond the quad by using a 2x multiplier,
    // giving elongated drops that look like motion-blurred rain.
    const float v = i.uv.y * 2.0f;
    const float alpha = 1.0f - smoothstep( 0.0f, 1.0f, v );

    // Skip fully-transparent fragments early for culling.
    if( alpha < 0.01f ) {
        return float4( 0.0f, 0.0f, 0.0f, 0.0f );
    }

    // Per-instance tint (light blue for rain).
    const float3 col = i.tint.rgb;

    // Premultiplied alpha output for proper blending.
    return float4( col * alpha, alpha );
}
