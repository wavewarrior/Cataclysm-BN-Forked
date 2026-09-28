// Dust mote fragment shader — a tiny soft dot (Step 6c, atmospheric-lighting-
// coherence plan).
//
// Colour and alpha are already premultiplied by the fade envelope on the CPU
// (dust_mote_effect.cpp); this only applies the radial soft-dot mask so a
// mote reads as a small round speck instead of a visible quad edge. Output is
// premultiplied (colour*mask, alpha*mask) to match the pipeline's
// ONE / ONE_MINUS_SRC_ALPHA blend (same convention as rain_effect's droplets).

struct VS_OUT {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float3 color : TEXCOORD1;
    float  alpha : TEXCOORD2;
};

float4 main(VS_OUT i) : SV_Target0 {
    const float d = length(i.uv); // 0 at centre, 1 at the quad edge
    const float mask = saturate(1.0 - d * d); // soft round falloff, no hard edge
    return float4(i.color * mask, i.alpha * mask);
}
