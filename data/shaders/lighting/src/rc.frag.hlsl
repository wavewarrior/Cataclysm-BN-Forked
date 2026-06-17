// SHADER_BUILD_TAG: rc_field_tag_b
// Radiance-cascade GI gather — Step-3 Phase 2 (single cascade).
//
// One fragment = one probe = one map tile. For its tile centre, gather occluded
// emitter radiance (each emitter direction sphere-marches the SDF for shadow),
// and write the summed colour into the cascade texture (RGBA16F). The sprite
// shader then reads this as its IndirectTex GI field. This is the per-probe
// gather that proves the GPU path + storage-buffer binding + transpose; the
// directional cascade hierarchy + bilinear-fix merge is Phase 3.
//
// Resources (space2). No sampler / no storage texture in this pass, so the two
// storage buffers start at t0:
//   t0  Emitters — StructuredBuffer<GpuEmitter>
//   t1  SdfBuf   — StructuredBuffer<float>, x-major sdf[x*map_h+y]
// (shadercross reflection must show storage_buffers=2; see radiance_cascade_pass.)

struct GpuEmitter {
    float4 pos_radius;
    float4 color_falloff;
    float4 cone_shape;
    float4 misc;
};

StructuredBuffer<GpuEmitter> Emitters : register(t0, space2);
StructuredBuffer<float>      SdfBuf   : register(t1, space2);
// P1: contribution epsilon — skip shadow march when atten is negligible.
static const float LIGHT_EPS = 0.004;
// P2: per-probe shadow-march budget. Only the RC_K strongest in-range emitters
// (by atten) get a full trace_shadow; weaker ones add unshadowed. Bounds this
// pass at probes*RC_K*steps regardless of horde size — the horde-TDR fix. GI is
// indirect, so fewer shadow-casters than the direct (sprite) K is invisible.
static const int RC_K = 8;

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
// Clamped raw SDF fetch. SdfBuf is x-major (sdf[x*H+y]).
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
// Soft-shadow sphere trace (Inigo Quilez cone ratio), as in sprite.frag.
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
    // The cascade texture is stored transposed (drop-in for IndirectTex): the
    // sprite reads Load(int3(y, x, 0)), so the fragment at pixel (px, py) is the
    // probe for tile (x = py, y = px).
    const int tileX = (int)floor(i.pos.y);
    const int tileY = (int)floor(i.pos.x);
    if(tileX >= (int)sdf_map_w || tileY >= (int)sdf_map_h) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const float2 probe = float2((float)tileX + 0.5, (float)tileY + 0.5);

    float3 gi = float3(0.0, 0.0, 0.0);
    const uint me = min(emitter_count, 8192u);

    // P2: K-strongest budget — track the RC_K strongest in-range emitters by
    // atten. Sized exactly RC_K (top_n never exceeds it): an oversized [64] here
    // forced a large dynamically-indexed indexable-temp array whose DXIL the
    // D3D12 driver rejected at pipeline creation (E_INVALIDARG) — Metal tolerated
    // it. RC_K slots keep the array small enough to build on all backends.
    uint  top_idx[RC_K];
    float top_val[RC_K];
    int   top_n = 0;

    // --- PASS 1: accumulate unshadowed GI + track K-max candidates ---
    [loop] for(uint ei = 0u; ei < me; ++ei) {
        const GpuEmitter e = Emitters[ei];
        const float3 e_pos     = e.pos_radius.xyz;
        const float  e_radius  = e.pos_radius.w;
        const float3 e_color   = e.color_falloff.xyz;
        const float  e_falloff = e.color_falloff.w;
        if(abs(e_pos.z - current_z) > 0.5) continue;
        const float2 dv   = e_pos.xy - probe;
        const float  dist = length(dv);
        if(dist >= e_radius || dist < 0.01) continue;
        const float  atten  = 1.0 - pow(saturate(dist / e_radius), e_falloff);
        // P1: skip negligible contributions entirely — no shadow march needed.
        if(atten <= LIGHT_EPS) continue;
        const float3 rgb = (e_color.x < 0.01 && e_color.y < 0.01 && e_color.z < 0.01)
                           ? float3(1.0, 1.0, 1.0) : e_color;
        // Unshadowed contribution (always added — weaker lights still fill GI).
        gi += rgb * atten;

        // Track K-strongest for full shadow trace (inline selection, no sort).
        if(top_n < RC_K) {
            top_idx[top_n] = ei;
            top_val[top_n] = atten;
            ++top_n;
        } else if(atten > top_val[0]) {
            int min_i = 0;
            for(int mi = 1; mi < top_n; ++mi) {
                if(top_val[mi] < top_val[min_i]) min_i = mi;
            }
            top_idx[min_i] = ei;
            top_val[min_i] = atten;
        }
    }

    // --- PASS 2: full shadow trace for the K strongest candidates ---
    [loop] for(int ti = 0; ti < top_n; ++ti) {
        const uint ei = top_idx[ti];
        const GpuEmitter e = Emitters[ei];
        const float3 e_pos     = e.pos_radius.xyz;
        const float  e_radius  = e.pos_radius.w;
        const float3 e_color   = e.color_falloff.xyz;
        const float  e_falloff = e.color_falloff.w;
        const float2 dv   = e_pos.xy - probe;
        const float  dist = length(dv);
        const float  atten  = 1.0 - pow(saturate(dist / e_radius), e_falloff);
        const float3 rgb = (e_color.x < 0.01 && e_color.y < 0.01 && e_color.z < 0.01)
                           ? float3(1.0, 1.0, 1.0) : e_color;
        // Subtract unshadowed (added in pass 1) and re-add with shadow.
        gi -= rgb * atten;
        const float2 dir    = dv / max(dist, 0.001);
        const float  shadow = trace_shadow(probe, dir, dist, shadow_k, (int)shadow_steps);
        gi += rgb * atten * shadow;
    }
    return float4(gi, 1.0);
}
