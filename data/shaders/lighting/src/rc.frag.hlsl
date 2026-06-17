// SHADER_BUILD_TAG: rc_field_tag_j
// Radiance-cascade GI gather — Step-3 Phase 2 (single cascade).
//
// One fragment = one probe = one map tile. For its tile centre, gather occluded
// emitter radiance (each emitter direction sphere-marches the SDF for shadow),
// and write the summed colour into the cascade texture (RGBA16F). The sprite
// shader then reads this as its IndirectTex GI field.
//
// Resources (space2), sampler-less ⇒ storage buffers start at t0:
//   t0  Emitters — StructuredBuffer<float>, 16 floats/emitter (see layout below)
//   t1  SdfBuf   — StructuredBuffer<float>, x-major sdf[x*map_h+y]
// (shadercross reflection must show samplers=0 storage_buffers=2.)
//
// D3D12 / SDL_shadercross gotchas (all empirically bisected — Metal tolerates,
// D3D12 pipeline creation rejects with E_INVALIDARG):
//   • Emitters is a SCALAR StructuredBuffer<float>, NOT StructuredBuffer<float4>.
//     A float4 (vector-element) structured buffer read at a DYNAMIC index (the
//     per-emitter loop) produced DXIL the D3D12 driver rejected; a constant
//     index was fine, and a scalar buffer (like vol.frag's) is fine. Emitter ei
//     occupies floats [ei*16 .. ei*16+15]; we use pos_radius (0..3) + color_
//     falloff (4..7).
//   • The K-strongest selection uses a scalar counter, not a dynamically-indexed
//     local array (indexable temp), which also tripped D3D12 pipeline creation.

StructuredBuffer<float> Emitters : register(t0, space2);
StructuredBuffer<float> SdfBuf   : register(t1, space2);

// P1: contribution epsilon — skip shadow march when atten is negligible.
static const float LIGHT_EPS = 0.004;
// P2: per-probe shadow-march budget — first RC_K in-range emitters get a full
// trace_shadow; the rest add unshadowed. Bounds the pass at probes*RC_K*steps.
static const uint RC_K = 8u;

cbuffer RcParams : register(b0, space3) {
    uint  emitter_count;
    uint  sdf_map_w;      // runtime SDF dims (probe grid extent)
    uint  sdf_map_h;
    float current_z;
    float shadow_k;       // sphere-trace cone hardness (sprite shadow_k knob)
    uint  shadow_steps;   // per-emitter march cap
    float rc_pad0;
    float rc_pad1;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// --- SDF helpers (verbatim from sprite.frag — shared occlusion march) ----------
float sdf_texel(int x, int y) {
    x = clamp(x, 0, (int)sdf_map_w - 1);
    y = clamp(y, 0, (int)sdf_map_h - 1);
    return SdfBuf[x * (int)sdf_map_h + y];
}
float sdf_bilinear(float2 p) {
    const float2 sp = p - 0.5;        // tile-centre alignment
    const float2 fp = floor(sp);
    const int   x0  = (int)fp.x;
    const int   y0  = (int)fp.y;
    const float2 w  = sp - fp;
    const float a = sdf_texel(x0,     y0    );
    const float b = sdf_texel(x0 + 1, y0    );
    const float c = sdf_texel(x0,     y0 + 1);
    const float d = sdf_texel(x0 + 1, y0 + 1);
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}
float trace_shadow(float2 origin, float2 dir, float dist_to_light, float k, int steps) {
    if(sdf_map_w == 0u || steps <= 0) {
        return 1.0;
    }
    float shadow = 1.0;
    float t = min(0.3, dist_to_light * 0.5);
    [loop] for(int ss = 0; ss < steps; ++ss) {
        if(t >= dist_to_light - 0.4) break;
        const float sd = sdf_bilinear(origin + dir * t);
        if(sd < 0.05) { shadow = 0.0; break; }
        shadow = min(shadow, k * sd / max(dist_to_light - t, 0.01));
        t += max(sd, 0.15);
    }
    return saturate(shadow);
}

float4 main(VS_OUT i) : SV_Target0 {
    // Cascade texture is transposed (drop-in for IndirectTex): fragment (px,py)
    // is the probe for tile (x=py, y=px).
    const int tileX = (int)floor(i.pos.y);
    const int tileY = (int)floor(i.pos.x);
    if(tileX >= (int)sdf_map_w || tileY >= (int)sdf_map_h) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const float2 probe = float2((float)tileX + 0.5, (float)tileY + 0.5);

    float3 gi = float3(0.0, 0.0, 0.0);
    const uint me = min(emitter_count, 8192u);

    uint traced = 0u;
    [loop] for(uint ei = 0u; ei < me; ++ei) {
        const uint   b         = ei * 16u; // 16 floats/emitter
        const float3 e_pos     = float3(Emitters[b + 0u], Emitters[b + 1u], Emitters[b + 2u]);
        const float  e_radius  = Emitters[b + 3u];
        const float3 e_color   = float3(Emitters[b + 4u], Emitters[b + 5u], Emitters[b + 6u]);
        const float  e_falloff = Emitters[b + 7u];
        if(abs(e_pos.z - current_z) > 0.5) continue;
        const float2 dv   = e_pos.xy - probe;
        const float  dist = length(dv);
        if(dist >= e_radius || dist < 0.01) continue;
        const float  atten = 1.0 - pow(saturate(dist / e_radius), e_falloff);
        if(atten <= LIGHT_EPS) continue;
        const float3 rgb = (e_color.x < 0.01 && e_color.y < 0.01 && e_color.z < 0.01)
                           ? float3(1.0, 1.0, 1.0) : e_color;
        // Unshadowed base (all in-range emitters).
        gi += rgb * atten;
        // First RC_K in-range emitters: refine with a shadow trace.
        if(traced < RC_K) {
            const float2 dir    = dv / max(dist, 0.001);
            const float  shadow = trace_shadow(probe, dir, dist, shadow_k, (int)shadow_steps);
            gi += rgb * atten * (shadow - 1.0);
            traced += 1u;
        }
    }
    return float4(gi, 1.0);
}
