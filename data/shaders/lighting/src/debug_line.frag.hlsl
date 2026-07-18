// Debug-line fragment shader — solid colour pass-through.
// Outputs premultiplied alpha (matches the ONE / ONE_MINUS_SRC_ALPHA
// blend state in debug_line_pass.cpp).

struct PS_IN {
    float4 colour : TEXCOORD0;
};

float4 main( PS_IN i ) : SV_TARGET
{
    return float4( i.colour.rgb * i.colour.a, i.colour.a );
}
