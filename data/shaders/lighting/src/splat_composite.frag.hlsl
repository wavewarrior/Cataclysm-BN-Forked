// Splatmap composite fragment shader — decals over terrain, under entities.
//
// Samples one submap's accumulated splatmap and resolves its three coverage
// channels into a single premultiplied-alpha contribution:
//   R — blood: tints toward blood_color AND darkens (a > 0 scales dst down).
//   G — wetness: pure darken (rgb 0, alpha only).
//   B — snow: white overlay.
// Only R is ever non-zero today; G/B are wired for future wetness/snow and
// cost nothing.
//
// Output is PREMULTIPLIED alpha, matching the composite pass blend
// (src = ONE, dst = ONE_MINUS_SRC_ALPHA).
//
// The composite lands in the HDR world_target BEFORE the AgX tonemap +
// exposure 0.35, which crushes subtle darkening — hence the deliberately
// strong default blood strength (same reason rain_splash.frag uses
// DARK_BOOST = 3.0).

struct PS_IN {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

Texture2D<float4> Splat    : register(t0, space2);
SamplerState      SplatSmp : register(s0, space2);

cbuffer SplatColors : register(b0, space3) {
    float4 blood_color;
    float4 strengths; // (blood, wet, snow, pad)
};

float4 main( PS_IN i ) : SV_Target
{
    const float4 s = Splat.Sample( SplatSmp, i.uv );
    const float  b = s.r * strengths.x;   // blood: tint + darken
    const float  w = s.g * strengths.y;   // wetness: pure darken
    const float  n = s.b * strengths.z;   // snow: white overlay
    const float3 rgb = blood_color.rgb * b + float3( 1.0, 1.0, 1.0 ) * n;
    const float  a   = saturate( b + w + n );
    if( a < 0.004 ) { return float4( 0.0, 0.0, 0.0, 0.0 ); }
    return float4( rgb, a );
}
