// Splatmap composite vertex shader — one quad per visible submap.
//
// Emits the submap's splatmap quad in LOGICAL projection pixels (the same
// space cata_tiles' player_to_screen produces, which is what guarantees
// pixel-exact alignment with the tile sprites). No storage buffer: the quad
// rect arrives as a cbuffer push, one draw per submap.

cbuffer CompositeParams : register(b0, space1) {
    float4 rect;        // dst_x, dst_y, dst_w, dst_h (logical projection px)
    float2 target_size; // projection extent
    float2 cp_pad;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0; // 0..1 across the submap splatmap
};

static const float2 quad_uv[6] = {
    float2(0.0,0.0), float2(1.0,0.0), float2(0.0,1.0),
    float2(1.0,0.0), float2(1.0,1.0), float2(0.0,1.0)
};

VS_OUT main(uint vid : SV_VertexID) {
    const float2 c = quad_uv[vid];
    const float2 pixel = float2(rect.x + c.x * rect.z,
                                rect.y + c.y * rect.w);

    // NDC conversion (Y-flipped for texture coords) — copied from sprite.vert.
    const float2 ndc = float2(
        pixel.x / target_size.x *  2.0 - 1.0,
        pixel.y / target_size.y * -2.0 + 1.0);

    VS_OUT o;
    o.pos = float4(ndc, 0.0, 1.0);
    o.uv  = c;
    return o;
}
