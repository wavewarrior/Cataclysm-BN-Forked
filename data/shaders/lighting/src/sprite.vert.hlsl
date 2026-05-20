// Sprite batcher vertex shader — phase 2c placeholder.
//
// Translated at runtime via SDL_shadercross. The matching C++ instance struct
// is `lighting::sprite_instance` (48 bytes, see src/lighting/sprite_batcher.h).
//
// SDL_GPU + shadercross resource-binding convention (HLSL space mapping):
//   space0 = vertex storage buffers (read-only)        — register t0..
//   space1 = vertex uniform buffers                    — register b0..
//   space2 = fragment samplers/textures                — register t0..s0..
//   space3 = fragment storage textures (read-only)
//   space4 = fragment storage buffers
//   space5 = fragment uniform buffers                  — register b0..
//
// We use:
//   - one read-only storage buffer of sprite_instance at space0 t0 (one
//     entry per instance, indexed by SV_InstanceID).
//   - one uniform buffer at space1 b0 carrying the orthographic projection
//     parameters (target_w, target_h) so we can map pixel coords to clip.

struct SpriteInstance {
    float dst_x;
    float dst_y;
    float dst_w;
    float dst_h;
    float src_u;
    float src_v;
    float src_uw;
    float src_vh;
    float tint_r;
    float tint_g;
    float tint_b;
    float tint_a;
};

StructuredBuffer<SpriteInstance> Instances : register( t0, space0 );

cbuffer FrameParams : register( b0, space1 ) {
    float2 target_size; // (width, height) in pixels
    float2 pad;
};

struct VS_OUT {
    float4 pos    : SV_Position;
    float2 uv     : TEXCOORD0;
    float4 tint   : TEXCOORD1;
};

// Unit quad expressed via SV_VertexID; six verts = two triangles.
//   tri 0: (0,0) (1,0) (0,1)
//   tri 1: (1,0) (1,1) (0,1)
static const float2 quad_uv[6] = {
    float2( 0.0, 0.0 ),
    float2( 1.0, 0.0 ),
    float2( 0.0, 1.0 ),
    float2( 1.0, 0.0 ),
    float2( 1.0, 1.0 ),
    float2( 0.0, 1.0 ),
};

VS_OUT main( uint vid : SV_VertexID, uint iid : SV_InstanceID )
{
    const SpriteInstance s = Instances[iid];
    const float2 corner = quad_uv[vid];

    // Pixel-space position on the target.
    const float2 pixel = float2(
                             s.dst_x + corner.x * s.dst_w,
                             s.dst_y + corner.y * s.dst_h );

    // Orthographic projection: pixel (0..target) → clip (-1..+1).
    // Y is flipped so (0,0) is top-left, matching SDL_Renderer legacy
    // coordinates and the rest of the engine's per-tile arithmetic.
    const float2 ndc = float2(
                           pixel.x / target_size.x *  2.0 - 1.0,
                           pixel.y / target_size.y * -2.0 + 1.0 );

    VS_OUT o;
    o.pos = float4( ndc, 0.0, 1.0 );
    o.uv  = float2( s.src_u + corner.x * s.src_uw,
                    s.src_v + corner.y * s.src_vh );
    o.tint = float4( s.tint_r, s.tint_g, s.tint_b, s.tint_a );
    return o;
}
