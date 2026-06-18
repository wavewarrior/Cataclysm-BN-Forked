// Sky/sun directional skylight — Stage 2a of GI_COMPUTE_AND_PERF_PLAN.md.
//
// One compute thread = one map tile. Computes a per-tile DIRECTIONAL skylight
// integral (the sky as an occluded dome) + the sun shadow occlusion, replacing
// the fragment shader's flat `sky_color·sky_vis` ambient and its single inline
// sun `trace_shadow` call. Writes SkyBuf: rgb = directional sky-access (0..1,
// white weight — the fragment multiplies sky_color), a = sun occlusion (0..1).
//
// Sky-access is a PORTAL march: per hemisphere direction, march the wall SDF;
// a direction "admits sky" iff it reaches an open-sky tile (SkyVis>0.5) before a
// wall blocks it. This unifies indoor + outdoor in one model:
//   - open tile  → adjacent tiles already open → most dirs admit → full sky,
//                  minus directions where a wall sits immediately alongside
//                  (alcove / against-wall self-shading — the visible win);
//   - roofed tile near a window → only the window's directions reach open sky
//                  → directional indoor daylight FROM the opening;
//   - deep interior → no direction reaches sky → dark. Correct, no flood-fill.
// (This is why Stage 2a lets frame_build delete the CPU window-bleed flood-fill.)
//
// Why compute (not fragment): mirrors the Stage-1 GI pass — distinct binding
// model (SDL_BindGPUComputeStorageBuffers / RW), dodges the fragment
// sampler-order root-sig that killed rc.frag on D3D12, and runs off the main
// thread. Inputs are SS-grid SunSdf (wall-only, trees excluded — same field the
// fragment sun march used) + tile-res raw SkyVis (open-sky 0/1).
//
// Compute HLSL register spaces (SDL_GPU): readonly storage = (tN, space0),
// read-write = (uN, space1), uniforms = (bN, space2). Readonly buffers are
// declared StructuredBuffer (NOT RW) so D3D12 does not default them read-write.
//
//   t0 space0  SunSdf  — StructuredBuffer<float>, SS-finer Euclidean grid,
//              x-major sunsdf[x*(map_h*SDF_SS)+y], distances in tile units.
//   t1 space0  SkyVis  — StructuredBuffer<float>, tile-res sky[x*map_h+y],
//              raw open-sky (1=open overhead, 0=roofed).
//   u0 space1  SkyBuf  — RWStructuredBuffer<float>, 4 floats/tile (rgb sky-access
//              + a sun-occ), tile-res, x-major sky[(x*map_h+y)*4 + c].
//   b0 space2  SkySunParams.

StructuredBuffer<float>   SunSdf : register(t0, space0);
StructuredBuffer<float>   SkyVis : register(t1, space0);
RWStructuredBuffer<float> SkyBuf : register(u0, space1);

cbuffer SkySunParams : register(b0, space2) {
    uint  map_w;        // runtime tile dims (thread/tile grid extent)
    uint  map_h;
    float sun_dir_x;    // sun travel direction (toward_sun = -sun_dir)
    float sun_dir_y;
    float sun_sin_elev; // sun elevation sine (2b heightfield; unused in 2a)
    float shadow_k;     // sphere-trace cone hardness (sprite shadow_k knob)
    uint  shadow_steps; // sun march iteration cap
    float ss_pad;
};

// Sky-dome sampling (constants; promote to F4 knobs once eyeballed).
static const int   SKY_DIRS    = 8;     // hemisphere directions per tile
static const int   SKY_STEPS   = 16;    // max march steps per direction
static const float SKY_STEP    = 0.70;  // tile units per step
static const float SKY_START   = 0.60;  // skip the tile's own cell
static const float SKY_REACH   = 10.0;  // give up looking for sky past this
static const float WALL        = 0.05;  // SDF distance treated as "blocked"
static const int   SUN_PENUMBRA = 1;    // angular samples across the sun (1=hard)
static const float SUN_REACH   = 8.0;   // directional sun march reach
static const float SUN_SPREAD  = 0.06;  // radians half-spread for penumbra
static const int   SDF_SS      = 4;     // MUST match sdf_pass.h / sprite.frag

// --- SDF helpers (SS-finer grid, identical math to sprite.frag / gi_field) ----
float sdf_texel( int x, int y )
{
    const int gw = (int)map_w * SDF_SS;
    const int gh = (int)map_h * SDF_SS;
    x = clamp( x, 0, gw - 1 );
    y = clamp( y, 0, gh - 1 );
    return SunSdf[x * gh + y];
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
// SkyVis is tile-res, x-major sky[x*map_h+y]. 1=open overhead, 0=roofed.
float skyvis_at( int x, int y )
{
    x = clamp( x, 0, (int)map_w - 1 );
    y = clamp( y, 0, (int)map_h - 1 );
    return SkyVis[x * (int)map_h + y];
}
// Soft directional sun shadow — identical model to gi_field's trace_shadow.
float trace_shadow( float2 origin, float2 dir, float dist_to_light, float k, int steps )
{
    if( map_w == 0u || steps <= 0 ) {
        return 1.0;
    }
    float shadow = 1.0;
    float t = min( 0.3, dist_to_light * 0.5 );
    [loop] for( int s = 0; s < steps; ++s ) {
        if( t >= dist_to_light - 0.4 ) {
            break;
        }
        const float sd = sdf_bilinear( origin + dir * t );
        if( sd < WALL ) {
            shadow = 0.0;
            break;
        }
        shadow = min( shadow, k * sd / max( dist_to_light - t, 0.01 ) );
        t += max( sd, 0.15 );
    }
    return saturate( shadow );
}

// Does direction `dir` from `origin` reach open sky before a wall? Returns the
// reached tile's openness (1) or 0 if wall-blocked / no sky within SKY_REACH.
float sky_admit( float2 origin, float2 dir )
{
    float t = SKY_START;
    [loop] for( int s = 0; s < SKY_STEPS; ++s ) {
        const float2 pos = origin + dir * t;
        if( sdf_bilinear( pos ) < WALL ) {
            return 0.0;                 // wall blocks this direction
        }
        if( skyvis_at( (int)floor( pos.x ), (int)floor( pos.y ) ) > 0.5 ) {
            return 1.0;                 // reached open sky
        }
        t += SKY_STEP;
        if( t > SKY_REACH ) {
            break;
        }
    }
    return 0.0;                          // no sky reachable in this direction
}

[numthreads(8, 8, 1)]
void main( uint3 tid : SV_DispatchThreadID )
{
    const int tileX = (int)tid.x;
    const int tileY = (int)tid.y;
    if( tileX >= (int)map_w || tileY >= (int)map_h ) {
        return;
    }
    const float2 probe = float2( (float)tileX + 0.5, (float)tileY + 0.5 );

    // Directional sky-access: fraction of the hemisphere that reaches open sky.
    float sky = 0.0;
    [loop] for( int d = 0; d < SKY_DIRS; ++d ) {
        const float  ang = 6.2831853 * ( (float)d + 0.5 ) / (float)SKY_DIRS;
        const float2 dir = float2( cos( ang ), sin( ang ) );
        sky += sky_admit( probe, dir );
    }
    sky /= (float)SKY_DIRS;

    // Sun occlusion: march toward the sun, optionally averaging a few angular
    // offsets for a soft penumbra (SUN_PENUMBRA=1 → hard edge, matches today).
    const float2 toward_sun = -float2( sun_dir_x, sun_dir_y );
    const float  base_ang   = atan2( toward_sun.y, toward_sun.x );
    float sun_occ = 0.0;
    [loop] for( int p = 0; p < SUN_PENUMBRA; ++p ) {
        const float off = ( SUN_PENUMBRA > 1 )
                          ? ( ( (float)p / (float)( SUN_PENUMBRA - 1 ) ) - 0.5 ) * 2.0 * SUN_SPREAD
                          : 0.0;
        const float  a   = base_ang + off;
        const float2 dir = float2( cos( a ), sin( a ) );
        sun_occ += trace_shadow( probe, dir, SUN_REACH, shadow_k, (int)shadow_steps );
    }
    sun_occ /= (float)SUN_PENUMBRA;

    const uint o = ( (uint)tileX * map_h + (uint)tileY ) * 4u;
    SkyBuf[o + 0u] = sky;
    SkyBuf[o + 1u] = sky;
    SkyBuf[o + 2u] = sky;
    SkyBuf[o + 3u] = sun_occ;
}
