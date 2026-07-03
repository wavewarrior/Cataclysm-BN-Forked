// ---- Sprite instance (80 bytes, wire-stable) ----
struct SpriteInstance {
    float dst_x, dst_y, dst_w, dst_h;
    float src_u, src_v, src_uw, src_vh;
    float tint_r, tint_g, tint_b, tint_a;
    float rotation, light_mul, pad1, pad2;
    float extrude_px, extrude_dark, extrude_lean, extrude_pad;
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
// 48 bytes — wire-stable with C++ light_params. The trailing sun_* row is used
// only by shadow.vert (silhouette-shadow shear); this shader ignores it but
// declares the same layout so the shared per-frame vertex push lines up.
cbuffer LightParams : register(b1, space1) {
    float tile_pixel_size;
    float current_z;
    uint  emitter_count;
    float ambient;
    float camera_off_x;
    float camera_off_y;
    uint  sdf_map_w;
    uint  sdf_map_h;
    float sun_dir_x;
    float sun_dir_y;
    float sun_cot_elev;
    float lp_sun_pad;
};

// Cbuffer slot 2: DebugParams (152 bytes — wire-stable with C++ debug_params).
// Pushed to the vertex stage so foliage sway can read sway_amp/sway_freq/anim_time.
// The full field list is declared so those three land at the correct byte offset;
// every other field is ignored by this shader.
cbuffer DebugParams : register(b2, space1) {
    uint  debug_mode;
    float debug_opacity;
    float emitter_scale;
    float sun_scale;
    float sky_scale;
    float shadow_k;
    uint  shadow_steps;
    float dither_amt;
    float dither_bands;
    float gi_strength;
    float vis_curve;
    float mem_dim;
    float mem_desat;
    float night_floor;
    float day_floor;
    float grade_desat;
    float grade_cool;
    float grade_bright;
    float vis_radius;
    float player_x;
    float player_y;
    float mem_radius;
    float nrm_amount;
    float nrm_relief;
    float nrm_elev;
    float sdf_sharp;
    float ao_strength;
    float shadow_mask_str;
    float sway_amp;     // wind displacement amplitude (pixels); 0 = sway off
    float sway_freq;    // wind oscillation frequency
    float anim_time;    // wrapped render seconds (per-frame data)
    float spec_strength; // fragment-stage only (declared here for cbuffer layout parity)
    float light_eps;     // fragment-stage only (declared here for cbuffer layout parity)
    float max_shadow_k;  // fragment-stage only (declared here for cbuffer layout parity)
    // P5b: fragment + compute-stage only (sky_sun.comp). Declared for cbuffer layout parity.
    float sky_dirs;
    float sky_reach;
    float sun_steps;
    float sun_penumbra;
};

struct VS_OUT {
    float4 pos      : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 tint     : TEXCOORD1; // Phase 5 CPU lightmap tint (ambient floor)
    float2 world_pos: TEXCOORD2; // map tile coords for fragment per-pixel lighting
    float  light_mul: TEXCOORD3; // memory-fade marker (<0 = -(dist); else no-op)
    float2 light_pos: TEXCOORD4; // lighting sample pos: base-tile centre for tall
                                 // sprites (uniform), else == world_pos (per-pixel)
    float  outline  : TEXCOORD5; // >0.5 = silhouette mask mode (hover outline)
    float  dark_frac: TEXCOORD6; // 0 at sprite base → extrude_dark at canopy; applied in frag
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

    // ---- Foliage sway (cosmetic breeze: UV offset) ----
    // Offsets UVs horizontally instead of displacing vertices, so the quad
    // geometry stays solid (no black gaps from sheared edges). The atlas
    // sampler uses CLAMP_TO_EDGE, so out-of-range UVs clamp to the sprite's
    // edge texels rather than wrapping to neighbouring atlas content.
    // swayw (s.pad1) is the per-sprite foliage weight set at enqueue (0 = no
    // sway). Phase keys off the world-locked base tile so neighbouring plants
    // desync and the motion sticks to terrain on scroll. The (1.0 - c.y)
    // gradient pins the base and peaks at the canopy.
    float2      c_uv = c;
    const float swayw = s.pad1;
    if( swayw > 0.0 ) {
        const float ph   = base_tile.x * 0.7 + base_tile.y * 1.3;
        const float wind = sin( anim_time * sway_freq + ph );
        // Pin the base: the lower BASE_PIN fraction of the sprite stays planted (zero sway)
        // and the bend ramps in toward the top, so the canopy sways while the trunk/base of
        // a single un-split sprite (shrubs, etc.) stays put. Sharper than the old linear
        // (1.0 - c.y), which still swayed the lower half.
        const float BASE_PIN = 0.55;
        const float bend = 1.0 - smoothstep( 0.0, BASE_PIN, c.y );
        c_uv.x += bend * swayw * sway_amp * wind / max( s.dst_w, 1.0 );
    }

    // ---- Height depth pillar (DitW) ----
    // UV mapping is unchanged: s.src_v + c.y * s.src_vh spans the full taller quad,
    // so the sprite artwork stretches vertically — no UV remap or atlas-bleed risk.
    // This block only adds lean (horizontal shear) and computes the dark_frac gradient.
    float2 pixel_out = pixel;
    if( s.extrude_px > 0.0 ) {
        // vertical_pos: 0 at base (c.y=1), 1 at canopy top (c.y=0).
        const float vertical_pos  = 1.0 - c.y;
        const float2 viewport_ctr = target_size * 0.5;
        // Lean direction: away from viewport centre — tops fan outward for parallax depth.
        const float2 lean_dir     = centre - viewport_ctr;
        pixel_out = pixel + lean_dir * ( s.extrude_lean * vertical_pos );
    }
    // Dark gradient: 0 at base, extrude_dark at top. extrude_dark=0 on non-opted tiles → no-op.
    const float dark_frac_out = s.extrude_dark * ( 1.0 - c.y );

    const float2 ndc = float2(
        pixel_out.x / target_size.x *  2.0 - 1.0,
        pixel_out.y / target_size.y * -2.0 + 1.0 );

    VS_OUT o;
    o.pos       = float4(ndc, 0.0, 1.0);
    o.dark_frac = dark_frac_out;
    o.uv        = float2(s.src_u + c_uv.x * s.src_uw, s.src_v + c_uv.y * s.src_vh);
    o.tint      = float4(s.tint_r, s.tint_g, s.tint_b, s.tint_a);
    o.world_pos = map_pos;
    o.light_mul = s.light_mul;
    o.light_pos = is_tall ? base_tile : map_pos;
    o.outline   = s.pad2;
    return o;
}
