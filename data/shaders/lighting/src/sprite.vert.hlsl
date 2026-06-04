// ---- Sprite instance (64 bytes, wire-stable) ----
struct SpriteInstance {
    float dst_x, dst_y, dst_w, dst_h;
    float src_u, src_v, src_uw, src_vh;
    float tint_r, tint_g, tint_b, tint_a;
    float rotation, light_mul, pad1, pad2;
};

// Vertex storage slot 0: sprite instances
StructuredBuffer<SpriteInstance> Instances : register(t0, space0);

// Cbuffer slot 0: per-segment viewport + instance base (wire-stable)
cbuffer FrameParams : register(b0, space1) {
    float2 target_size;
    uint   instance_base;
    uint   fp_pad;
};

// Cbuffer slot 1: per-frame lighting params (world_pos computation)
cbuffer LightParams : register(b1, space1) {
    float tile_pixel_size;
    float current_z;
    uint  emitter_count;
    float ambient;
    float camera_off_x;
    float camera_off_y;
    uint  sdf_map_w;
    uint  sdf_map_h;
};

struct VS_OUT {
    float4 pos      : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 tint     : TEXCOORD1; // Phase 5 CPU lightmap tint (ambient floor)
    float2 world_pos: TEXCOORD2; // map tile coords for fragment per-pixel lighting
    float  light_mul: TEXCOORD3; // memory-fade marker (<0 = -(dist); else no-op)
    float2 light_pos: TEXCOORD4; // lighting sample pos: base-tile centre for tall
                                 // sprites (uniform), else == world_pos (per-pixel)
};
static const float2 quad_uv[6] = {
    float2(0.0,0.0), float2(1.0,0.0), float2(0.0,1.0),
    float2(1.0,0.0), float2(1.0,1.0), float2(0.0,1.0)
};

VS_OUT main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    const SpriteInstance s = Instances[iid + instance_base];
    const float2 c = quad_uv[vid];

    const float2 centre = float2(s.dst_x + 0.5 * s.dst_w,
                                 s.dst_y + 0.5 * s.dst_h);
    const float2 off    = float2((c.x - 0.5) * s.dst_w,
                                 (c.y - 0.5) * s.dst_h);
    const float  cs     = cos(s.rotation);
    const float  sn     = sin(s.rotation);
    const float2 pixel  = centre + float2(off.x * cs - off.y * sn,
                                          off.x * sn + off.y * cs);
    const float2 ndc = float2(
        pixel.x / target_size.x *  2.0 - 1.0,
        pixel.y / target_size.y * -2.0 + 1.0);

    // Use per-VERTEX pixel (not sprite centre) so world_pos interpolates across
    // the quad.  For small tiles (32px) the difference is < 0.5 tile — negligible.
    // For a fullscreen background quad this gives the lighting gradient we want.
    const float2 tile_tu = pixel / max(tile_pixel_size, 1.0);
    const float2 map_pos = tile_tu - float2(camera_off_x, camera_off_y);

    // Tall sprites (trees, tall furniture) are drawn extending UP from the tile
    // they stand on — straight into the region their own cast shadow falls — so
    // per-pixel map_pos samples shadowed/occluded tiles and the sprite self-
    // darkens ("shadow on top of the tree"). Light a tall sprite by the CENTRE of
    // its base tile instead (constant across all its fragments → uniform), so the
    // object renders lit ON TOP of the ground shadow. The base-tile centre is the
    // bottom edge minus half a tile; for a 1-tile sprite that is just the sprite
    // centre, so light_pos == map_pos and small sprites keep per-pixel ground
    // lighting (preserving the 4x-SDF shadow smoothness). Threshold at 1.5 tiles.
    const float2 base_px   = float2(centre.x,
                                    s.dst_y + s.dst_h - 0.5 * tile_pixel_size);
    const float2 base_tile = base_px / max(tile_pixel_size, 1.0)
                             - float2(camera_off_x, camera_off_y);
    const bool   is_tall   = s.dst_h > tile_pixel_size * 1.5;

    VS_OUT o;
    o.pos       = float4(ndc, 0.0, 1.0);
    o.uv        = float2(s.src_u + c.x * s.src_uw, s.src_v + c.y * s.src_vh);
    o.tint      = float4(s.tint_r, s.tint_g, s.tint_b, s.tint_a);
    o.world_pos = map_pos;
    o.light_mul = s.light_mul;
    o.light_pos = is_tall ? base_tile : map_pos;
    return o;
}
