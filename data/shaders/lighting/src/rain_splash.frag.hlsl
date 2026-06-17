// Rain splash fragment shader — expanding bright ring + dark wet impact spot.
//
// One quad per impact, world-positioned + projected to screen on the CPU and
// grown over the splash's lifetime (the quad gets bigger each frame). Two
// visuals share the quad, keyed off the quad-local coord:
//   - a thin bright RING hugging the quad edge (the expanding ripple), and
//   - a soft DARK central DISC: the wet mark the drop leaves on the ground
//     (replaces the old persistent low-res splat-map grid with a high-res,
//     transient, per-impact decal).
// No texture sampling.
//
// The per-instance tint carries the ring colour (cool blue) and the lifetime
// fade in tint.a (→0 as the splash ages out). Output is PREMULTIPLIED alpha to
// match the droplet blend (src = ONE, dst = ONE_MINUS_SRC_ALPHA), so:
//   out.rgb = sheen added,  dst scaled by (1 - out.a).
// The ring contributes bright rgb + a little darken; the dark disc contributes
// ONLY darken (rgb = 0, a = darken). They occupy different radii so they don't
// fight: disc in the centre, ring at the edge.

struct PS_IN {
    float4 pos  : SV_Position;
    float2 uv   : TEXCOORD0; // quad-local coord (0..1)
    float4 tint : TEXCOORD1; // rgb ring colour, a = lifetime fade
};

// Dark spot tuning. The splash composites into the HDR world_target BEFORE the
// AgX tonemap + exposure 0.35, which crushes a subtle darken — so the boost is
// deliberately strong (see RAIN_EFFECT_LEARNINGS). CORE_R is the disc radius in
// quad-local units (0 = centre, 1 = quad edge); kept inside the ring band.
static const float CORE_R     = 0.70f;
static const float DARK_BOOST = 3.0f;
static const float DARK_MAX   = 0.7f;

float4 main( PS_IN i ) : SV_Target
{
    // Distance from quad centre, 0 at centre → 1 at the quad edge.
    const float2 d = i.uv - 0.5f;
    const float  r = length( d ) * 2.0f;

    // Bright ring band hugging the quad edge (expands as the CPU grows the quad).
    const float band   = smoothstep( 0.72f, 0.9f, r ) * ( 1.0f - smoothstep( 0.9f, 1.0f, r ) );
    const float ring_a = band * i.tint.a;

    // Soft dark wet disc in the centre: 1 at centre → 0 at CORE_R. Pure darken.
    const float core_mask = 1.0f - smoothstep( 0.0f, CORE_R, r );
    const float core_a    = min( DARK_MAX, core_mask * i.tint.a * DARK_BOOST );

    const float3 rgb = i.tint.rgb * ring_a;          // bright only in the ring band
    const float  a   = saturate( ring_a + core_a );  // ring darken + disc darken

    if( a < 0.01f ) {
        return float4( 0.0f, 0.0f, 0.0f, 0.0f );
    }
    return float4( rgb, a );
}
