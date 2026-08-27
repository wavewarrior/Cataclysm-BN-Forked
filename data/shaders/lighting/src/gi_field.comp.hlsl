// GI compute — pass 1 of 2: per-tile direct radiance gather (was rc.frag).
//
// One compute thread = one map tile. For its tile centre, gather occluded
// emitter radiance (each emitter direction sphere-marches the SDF for shadow)
// and write the summed colour into FieldBuf. Pass 2 (gi_bounce.comp) then
// marches rays through this field for the single-bounce term.
//
// This is the GPU-compute home of the radiance gather. The fragment-stage RC
// (rc.frag) created fine on Metal but failed SDL_CreateGPUGraphicsPipeline
// root-signature construction on D3D12; compute uses a distinct binding model
// (SDL_BindGPUComputeStorageBuffers / RW bindings) that dodges it.
//
// Compute HLSL register spaces (SDL_GPU): readonly storage = (tN, space0),
// read-write storage = (uN, space1), uniforms = (bN, space2). Readonly buffers
// are declared StructuredBuffer (NOT RW) so D3D12 does not default them to
// read-write (Vulkan defaults readonly, D3D12 readwrite — mark explicitly).
//
//   t0 space0  Emitters — StructuredBuffer<float>, 16 floats/emitter (scalar:
//              a float4 structured buffer read at a dynamic index produced DXIL
//              D3D12 rejected; scalar is safe — same rule the spike proved).
//   t1 space0  SdfBuf   — StructuredBuffer<float>, SS-finer Euclidean grid,
//              x-major sdf[x*(map_h*SDF_SS)+y], distances already in tile units.
//   t2 space0  SkyBuf   — StructuredBuffer<float>, sky_sun.comp output, tile-res,
//              x-major sky[(x*map_h+y)*4 + c]: rgb = directional sky-access,
//              a = celestial (sun/moon) occlusion. P2: its surface radiance is
//              injected into the field so the bounce pass propagates daylight.
//   u0 space1  FieldBuf — RWStructuredBuffer<float>, 4 floats/tile (rgb + pad),
//              tile-res, x-major field[(x*map_h+y)*4 + c].
//   t3 space0  AlbedoBuf — StructuredBuffer<float>, 4 floats/tile (rgb+pad),
//              tile-res, x-major (terrain albedo; 1,1,1 = neutral). Albedo
//              bleed: the field is multiplied by lerp(1, albedo, gi_albedo)
//              so a red carpet tints the bounced light.
//   b0 space2  GiParams.

StructuredBuffer<float>   Emitters : register(t0, space0);
StructuredBuffer<float>   SdfBuf   : register(t1, space0);
StructuredBuffer<float>   SkyBuf   : register(t2, space0);
StructuredBuffer<float>   AlbedoBuf : register(t3, space0);
RWStructuredBuffer<float> FieldBuf : register(u0, space1);
#include "attenuation.hlsl"
cbuffer GiParams : register(b0, space2) {
    uint  emitter_count;
    uint  sdf_map_w;      // runtime tile dims (probe grid extent)
    uint  sdf_map_h;
    float current_z;      // probe z-plane (skip off-plane emitters)
    float shadow_k;       // sphere-trace cone hardness (sprite shadow_k knob)
    uint  shadow_steps;   // per-emitter march cap
    float gi_temporal;    // (unused here — shared gi_params push)
    float gi_bounce2;     // (unused here — shared gi_params push)
    // P2 sun/sky surface-radiance injection (matches sprite.frag sun/sky colour).
    float sun_r, sun_g, sun_b, sun_intensity;
    float sky_r, sky_g, sky_b, sky_intensity;
    float gi_albedo;      // albedo-bleed mix (0=off): field *= lerp(1, albedo, k)
};

// P1: contribution epsilon — skip shadow march when atten is negligible.
static const float LIGHT_EPS = 0.004;
// P2: per-tile shadow-march budget — first RC_K in-range emitters get a full
// trace_shadow; the rest add unshadowed. Bounds the pass at tiles*RC_K*steps.
static const uint  RC_K = 8u;
// SDF supersample factor — MUST match lighting::SDF_SUPERSAMPLE (sdf_pass.h)
// and the SDF_SS in sprite.frag.hlsl. SdfBuf is the SS-finer grid. Declared
// BEFORE sdf_texel below (HLSL has no forward declarations).
static const int   SDF_SS = 8;

// --- SDF helpers (SS-finer grid, identical math to sprite.frag) --------------
float sdf_texel( int x, int y )
{
    const int gw = (int)sdf_map_w * SDF_SS;
    const int gh = (int)sdf_map_h * SDF_SS;
    x = clamp( x, 0, gw - 1 );
    y = clamp( y, 0, gh - 1 );
    return SdfBuf[x * gh + y];
}
float sdf_bilinear( float2 p )
{
    const float2 g  = p * (float)SDF_SS - 0.5;
    const float2 fp = floor( g );
    const int   x0  = (int)fp.x;
    const int   y0  = (int)fp.y;
    const float2 w  = g - fp;
    const float a = sdf_texel( x0,     y0     );
    const float b = sdf_texel( x0 + 1, y0     );
    const float c = sdf_texel( x0,     y0 + 1 );
    const float d = sdf_texel( x0 + 1, y0 + 1 );
    return lerp( lerp( a, b, w.x ), lerp( c, d, w.x ), w.y );
}
// Soft-shadow sphere trace, shared with sprite.frag and vol.frag. Included here
// (not at the top) because it calls sdf_bilinear, defined above. GI probes pass
// self_eps = 0: a probe sits in open space, not on the occluder it is lit by.
#include "shadow_trace.hlsl"

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    const int tileX = (int)tid.x;
    const int tileY = (int)tid.y;
    if( tileX >= (int)sdf_map_w || tileY >= (int)sdf_map_h ) {
        return;
    }
    const float2 probe = float2( (float)tileX + 0.5, (float)tileY + 0.5 );

    float3 gi = float3( 0.0, 0.0, 0.0 );
    const uint me = min( emitter_count, 8192u );

    uint traced = 0u;
    [loop] for( uint ei = 0u; ei < me; ++ei ) {
        const uint   b         = ei * 16u; // 16 floats/emitter
        const float3 e_pos     = float3( Emitters[b + 0u], Emitters[b + 1u], Emitters[b + 2u] );
        const float  e_radius  = Emitters[b + 3u];
        const float3 e_color   = float3( Emitters[b + 4u], Emitters[b + 5u], Emitters[b + 6u] );
        const float  e_falloff = Emitters[b + 7u];
        if( abs( e_pos.z - current_z ) > 0.5 ) {
            continue;
        }
        const float2 dv   = e_pos.xy - probe;
        const float  dist = length( dv );
        if( dist < 0.01 ) {
            continue;
        }
        const float atten = point_light_atten( dist, e_radius, e_falloff );
        if( atten <= LIGHT_EPS ) {
            continue;
        }
        const float3 rgb = ( e_color.x < 0.01 && e_color.y < 0.01 && e_color.z < 0.01 )
                           ? float3( 1.0, 1.0, 1.0 ) : e_color;
        // Unshadowed base (all in-range emitters).
        gi += rgb * atten;
        // First RC_K in-range emitters: refine with a shadow trace.
        if( traced < RC_K ) {
            const float2 dir    = dv / max( dist, 0.001 );
            const float  shadow = soft_shadow_march( probe, dir, dist, shadow_k,
                                                     (int)shadow_steps, /*self_eps=*/0.0,
                                                     /*ref_receiver=*/false );
            gi += rgb * atten * ( shadow - 1.0 );
            traced += 1u;
        }
    }

    const uint o = ( (uint)tileX * sdf_map_h + (uint)tileY ) * 4u;

    // P2 — sun/sky surface radiance. SkyBuf is tile-res with the SAME x-major
    // layout as the field (sky[(x*map_h+y)*4 + c]): rgb = directional sky-access,
    // a = celestial occlusion. Add the daylight a tile's floor receives so the
    // bounce pass spreads it into shadowed/indoor neighbours (gi otherwise sees
    // emitters only). SkyBuf already bakes in roof/wall occlusion, so no extra
    // sky_vis gate is needed. Matches sprite.frag's direct sky_contrib/sun_contrib
    // colour; lambert/spec/mask are view-dependent and intentionally dropped for
    // the diffuse bounce source.
    if( sdf_map_w > 0u ) {
        const float3 sky_access = float3( SkyBuf[o + 0u], SkyBuf[o + 1u], SkyBuf[o + 2u] );
        const float  sun_occ    = SkyBuf[o + 3u];
        gi += float3( sky_r, sky_g, sky_b ) * sky_intensity * sky_access;
        gi += float3( sun_r, sun_g, sun_b ) * sun_intensity * sun_occ;
    }
    // Albedo bleed: tint the field by the tile's surface colour so the bounce
    // pass propagates coloured light (a red carpet tints the room). Neutral
    // (1,1,1) albedo = no change; gi_albedo=0 disables the mix entirely.
    if( gi_albedo > 0.001 ) {
        const float3 alb = float3( AlbedoBuf[o + 0u], AlbedoBuf[o + 1u], AlbedoBuf[o + 2u] );
        gi *= lerp( float3( 1.0, 1.0, 1.0 ), alb, gi_albedo );
    }

    FieldBuf[o + 0u] = gi.x;
    FieldBuf[o + 1u] = gi.y;
    FieldBuf[o + 2u] = gi.z;
    FieldBuf[o + 3u] = 0.0;
}
