// HUD particle fragment shader — soft circle with radial falloff.
// No texture — purely procedural.

struct PS_IN {
    float4 pos : SV_POSITION;   // pixel coords in the render target
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

// Gameplay cutout. The ambient particle layer draws over the whole screen, but
// the map is the one thing it must never sit on top of — a mote drifting across
// a tile reads as an item or a creature. play_rect is that viewport in TARGET
// PIXELS (x0, y0, x1, y1); fragments inside it are discarded, so particles are
// left only on the HUD chrome around it. mask_enable = 0 draws everywhere
// (dev panel toggle, and the main menu, where there is no map).
cbuffer MaskParams : register( b0, space3 ) {
    float4 play_rect;
    float  mask_enable;
    float3 mask_pad;
};

float4 main( PS_IN i ) : SV_TARGET
{
    if( mask_enable > 0.5f
        && i.pos.x >= play_rect.x && i.pos.x <= play_rect.z
        && i.pos.y >= play_rect.y && i.pos.y <= play_rect.w ) {
        discard;
    }

    // Soft circle: radial falloff from center
    const float2 center = i.uv - 0.5f;
    const float dist = length( center );
    const float alpha = smoothstep( 0.5, 0.0, dist ) * i.color.a;

    return float4( i.color.rgb * alpha, alpha );
}
