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
    float rotation;
    float pad0;
    float pad1;
    float pad2;
};

StructuredBuffer<SpriteInstance> Instances : register( t0, space0 );

cbuffer FrameParams : register( b0, space1 ) {
    float2 target_size; // (width, height) in pixels
    uint   instance_base; // start offset into Instances for this draw segment
    uint   pad;
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
    // SV_InstanceID behaviour varies across backends after first_instance
    // (D3D12 starts from 0; Vulkan starts from base). Add the explicit base
    // from the uniform so the indexing is portable across all SDL_GPU
    // backends.
    const SpriteInstance s = Instances[iid + instance_base];
    const float2 corner = quad_uv[vid];

    // Rotation around the destination rect's centre. corner ∈ [0,1] so
    // (corner - 0.5) is the offset from centre in normalised units; we
    // multiply by dst_size to convert to pixel-space, rotate, then add
    // the rect centre back to land in absolute pixels.
    const float c = cos( s.rotation );
    const float si = sin( s.rotation );
    const float2 dst_size = float2( s.dst_w, s.dst_h );
    const float2 local_pixel = ( corner - 0.5 ) * dst_size;
    const float2 rotated = float2(
                               local_pixel.x * c - local_pixel.y * si,
                               local_pixel.x * si + local_pixel.y * c );
    const float2 centre = float2( s.dst_x + s.dst_w * 0.5,
                                  s.dst_y + s.dst_h * 0.5 );
    const float2 pixel = centre + rotated;

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
