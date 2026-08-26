// ---- Silhouette sun-shadow vertex shader (Phase 1) ----
// Clones sprite.vert's quad/centre/rotation, then SHEARS the top edge of each
// caster quad along the sun's shadow-fall direction so a tall sprite (tree)
// projects a sheared copy of its own silhouette onto the ground. The fragment
// shader (shadow.frag) samples the atlas alpha to write coverage into the
// screen-space shadow mask; the shear here is what makes that coverage land
// where the shadow should fall instead of on top of the caster.
//
// Drawn by render_state::flush_shadow_casters on a SECOND sprite_batcher
// instance (pipeline_desc.vert_name = "shadow.vert.hlsl"). It shares the
// batcher's instance/cbuffer wire layout, so SpriteInstance + FrameParams +
// LightParams below MUST match sprite.vert exactly (LightParams was grown to
// 48 bytes with the sun fields this shader needs in the VERTEX stage — the
// fragment-only sun_params (b1/space3) is not visible here).
//
// STRIDE FIX: SpriteInstance below used to declare only the first 16 floats
// (64 bytes) even though the shared buffer records were 80. Because
// StructuredBuffer indexing is stride-based, `Instances[iid + instance_base]`
// therefore stepped 64 bytes into an 80-byte record and every caster past the
// first read a progressively more misaligned mixture of its neighbours'
// fields. The resulting garbage mask went unnoticed only because
// debug_params::shadow_mask_str ships at 0.0, so the mask is multiplied out
// before it reaches the screen. The struct is now declared IN FULL (24 floats,
// 96 bytes) and must stay byte-identical to sprite.vert's.

struct SpriteInstance {
    float dst_x, dst_y, dst_w, dst_h;
    float src_u, src_v, src_uw, src_vh;
    float tint_r, tint_g, tint_b, tint_a;
    float rotation, light_mul, pad1, pad2;
    float extrude_px, extrude_dark, extrude_lean, face_amt;
    // light_mode + flash_* + cutout are unread here; declared for stride parity.
    float light_mode, flash_r, flash_g, flash_b;
    float cutout;
    // Reserved pads: keep the struct a multiple of 16 bytes (112 B = 28 floats)
    // for the GPU StructuredBuffer stride. Unread.
    float cutout_pad0;
    float cutout_pad1;
    float cutout_pad2;
};

StructuredBuffer<SpriteInstance> Instances: register(t0, space0);

cbuffer FrameParams: register(b0, space1) {
    float2 target_size;
    uint instance_base;
    uint fp_pad;
};

// Per-frame lighting params (48 bytes, wire-stable with light_params / the
// sprite.vert LightParams cbuffer). Only target geometry + the sun fields are
// used here; the rest ride along so the layout matches the shared push.
cbuffer LightParams: register(b1, space1) {
    float tile_pixel_size;
    float current_z;
    uint emitter_count;
    float ambient;
    float camera_off_x;
    float camera_off_y;
    uint sdf_map_w;
    uint sdf_map_h;
    float sun_dir_x;    // shadow-fall direction (= +light travel, away from sun)
    float sun_dir_y;    // world y-down convention; flip sign if shadows invert
    float sun_cot_elev; // cot(sun elevation): low sun → long shadow
    float lp_sun_pad;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

static const float2 quad_uv[6] =
    {float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
     float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)};

// Tunable shear length factor (× cot(elev)). Hot-tune here without a C++
// rebuild — shaders are loaded from disk at init.
static const float SHADOW_LEN_FACTOR = 1.0;

VS_OUT main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    const SpriteInstance s = Instances[iid + instance_base];
    const float2 c = quad_uv[vid];

    const float2 centre = float2(s.dst_x + 0.5 * s.dst_w, s.dst_y + 0.5 * s.dst_h);
    const float2 off = float2((c.x - 0.5) * s.dst_w, (c.y - 0.5) * s.dst_h);
    const float cs = cos(s.rotation);
    const float sn = sin(s.rotation);
    float2 pixel = centre + float2(off.x * cs - off.y * sn, off.x * sn + off.y * cs);

    // Shear the TOP of the quad (quad_uv.y == 0, the canopy) along the
    // shadow-fall direction; the base (y == 1, the tile the caster stands on)
    // stays pinned. shadow_len in pixels = sprite height × cot(elevation).
    // NOTE: sun_dir_y is negated — the in-game sun shadow falls DOWN-screen
    // (+y, verified vs the SDF column shadow) but raw +sun_dir_y pointed the
    // sheared silhouette UP-screen (the flagged world-y-down vs sprite-up sign).
    const float shadow_len = s.dst_h * sun_cot_elev * SHADOW_LEN_FACTOR;
    const float2 shear_px = float2(sun_dir_x, -sun_dir_y) * shadow_len;
    pixel += (1.0 - c.y) * shear_px;

    const float2 ndc =
        float2(pixel.x / target_size.x * 2.0 - 1.0, pixel.y / target_size.y * -2.0 + 1.0);

    VS_OUT o;
    o.pos = float4(ndc, 0.0, 1.0);
    o.uv = float2(s.src_u + c.x * s.src_uw, s.src_v + c.y * s.src_vh);
    return o;
}
