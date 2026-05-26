#include "sprite_batcher.h"

#include "shader_compiler.h"
#include "debug.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace lighting
{

// --- HLSL sources embedded as string literals so phase 2d has no runtime
// data-file dependency. When we add disk-loaded shaders later (phases 5+),
// this is the template for how the load path looks. -----------------------

static const char *const SPRITE_VERT_HLSL = R"HLSL(
// ---- Sprite instance (64 bytes, wire-stable) ----
struct SpriteInstance {
    float dst_x, dst_y, dst_w, dst_h;
    float src_u, src_v, src_uw, src_vh;
    float tint_r, tint_g, tint_b, tint_a;
    float rotation, pad0, pad1, pad2;
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

    VS_OUT o;
    o.pos       = float4(ndc, 0.0, 1.0);
    o.uv        = float2(s.src_u + c.x * s.src_uw, s.src_v + c.y * s.src_vh);
    o.tint      = float4(s.tint_r, s.tint_g, s.tint_b, s.tint_a);
    o.world_pos = map_pos;
    return o;
}
)HLSL";

static const char *const SPRITE_FRAG_HLSL = R"HLSL(
// Phase 7: per-pixel Lambert shading + SDF soft shadows.
// Emitter data and SDF passed as Texture2D samplers (space2) — StructuredBuffer
// space4 fails D3D12 pipeline creation with SDL_shadercross @ 6b06e55c.
// EmitterTex: 4×64 RGBA32F; row=emitter index, col=data slot (0=pos+radius, 1=rgb+falloff).
// SdfTex: W×H R32_FLOAT matching sdf_pass dimensions.
Texture2D<float4> Atlas      : register(t0, space2);
SamplerState      AtlasSmp   : register(s0, space2);
Texture2D<float4> EmitterTex : register(t1, space2);
SamplerState      EmitterSmp : register(s1, space2);
Texture2D<float>  SdfTex     : register(t2, space2);
SamplerState      SdfSmp     : register(s2, space2);
// Phase 8: sky visibility (R8_UNORM, 255=open sky).
Texture2D<float>  SkyVisTex  : register(t3, space2);
SamplerState      SkyVisSmp  : register(s3, space2);
cbuffer LightParams : register(b0, space3) {
    float tile_pixel_size; float current_z;
    uint  emitter_count;   float ambient;
    float camera_off_x;    float camera_off_y;
    uint  sdf_map_w;       uint  sdf_map_h;
};
// Phase 8: sun + skylight params.
cbuffer SunParams : register(b1, space3) {
    float sun_dir_x, sun_dir_y, sun_sin_elev, sun_intensity;
    float sun_r,     sun_g,     sun_b,        sky_r;
    float sky_g,     sky_b,     sky_intensity, sp_pad;
};
struct VS_OUT {
    float4 pos      : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 tint     : TEXCOORD1;
    float2 world_pos: TEXCOORD2;
};
float4 main(VS_OUT i) : SV_Target0 {
    const float4 texel = Atlas.Sample(AtlasSmp, i.uv);
    if(texel.a < 0.01) discard;
    // Flat surface normal — Phase 7b will sample a normal atlas texture.
    const float3 normal = float3(0.0, 0.0, 1.0);
    // emitter_light accumulates GPU point-light contributions (starts at zero).
    // Combined with CPU tint ADDITIVELY so colored emitter glow is visible on
    // top of the CPU-shadowcasting result, not suppressed by max().
    float3 emitter_light = float3(0.0, 0.0, 0.0);
    const uint me = min(emitter_count, 64u);
    for(uint ei = 0u; ei < me; ++ei) {
        // row=emitter index, col 0=pos+radius, col 1=rgb+falloff
        const float4 d0 = EmitterTex.Load(int3(0, ei, 0));
        const float4 d1 = EmitterTex.Load(int3(1, ei, 0));
        if(abs(d0.z - current_z) > 0.5) continue;
        const float2 dv   = d0.xy - i.world_pos;
        const float  dist = length(dv);
        if(dist >= d0.w || dist < 0.01) continue;
        const float  atten   = 1.0 - pow(saturate(dist / d0.w), d1.w);
        // Lambert = 1.0 for omnidirectional point lights with flat normal.
        // Directional shading from real surface normals comes in Phase 7b.
        const float  lambert = 1.0;
        // DEBUG: bypass shadow to confirm emitter_light pipeline.
        // Remove this once lighting is confirmed working and re-enable SDF shadow.
        const float shadow = 1.0;
        const float3 rgb = (d1.x < 0.01 && d1.y < 0.01 && d1.z < 0.01)
                           ? float3(1, 1, 1) : d1.xyz;
        emitter_light += rgb * atten * lambert * shadow;
    }
    // Phase 8: sky ambient + directional sun contribution.
    // Swap x/y: sky_vis also stored x-major like SDF (iy=row→col, ix=col→row).
    const int sky_ix = clamp((int)i.world_pos.x, 0, (int)sdf_map_w - 1);
    const int sky_iy = clamp((int)i.world_pos.y, 0, (int)sdf_map_h - 1);
    float sky_vis = SkyVisTex.Load(int3(sky_iy, sky_ix, 0));
    // Sky ambient: soft, no shadowing needed.
    float3 sky_contrib = float3(sky_r, sky_g, sky_b) * sky_intensity * sky_vis;
    // Sun direct: ray-march SDF for soft shadow (16 steps, light-to-fragment direction).
    float3 sun_contrib = float3(0.0, 0.0, 0.0);
    if(sun_intensity > 0.001 && sky_vis > 0.01 && sdf_map_w > 0u) {
        const float2 toward_sun = -float2(sun_dir_x, sun_dir_y);
        float t = 0.3, sh = 1.0, k = 4.0;
        [loop] for(int ss = 0; ss < 16; ++ss) {
            if(t > 8.0) break;
            const float2 p  = i.world_pos + toward_sun * t;
            const int    ix = clamp((int)p.x, 0, (int)sdf_map_w - 1);
            const int    iy = clamp((int)p.y, 0, (int)sdf_map_h - 1);
            const float  s  = SdfTex.Load(int3(iy, ix, 0)); // swap: x-major SDF storage
            if(s < 0.05) { sh = 0.0; break; }
            sh = min(sh, k * s / max(8.0 - t, 0.01));
            t += max(s, 0.15);
        }
        const float sun_shadow = saturate(sh);
        // Lambert with flat normal: dot((0,0,1), normalize(sun_dir_xy, sin_elev))
        const float sun_lambert = sun_sin_elev / sqrt(1.0 + sun_sin_elev * sun_sin_elev);
        sun_contrib = float3(sun_r, sun_g, sun_b) * sun_intensity * sun_lambert
                      * sun_shadow * sky_vis;
    }

    // DEBUG VIZ: additive heatmap over normal lighting. Sentinel: sun_params
    // sp_pad > 0.5 (set by sdltiles.cpp::g_dbg_lighting_shader). Keeps UI/text
    // legible because we layer the heatmap into the final colour AFTER the
    // normal lighting path runs. Computed up-front so the value is available
    // when we return.
    // DIAGNOSTIC: paint EVERY visible tile with raw EmitterTex slot 0 read.
    // R = emit[0].pos_x / 200, G = emit[0].pos_y / 200, B = emit[0].radius/20.
    // Expected for real emit[0]=(86.5, 89.5, *, 13.4) → R≈0.43 G≈0.45 B≈0.67
    // (a yellowish tint). All zero → texture is empty / unbound.
    float dbg_r = 0.0, dbg_g = 0.0, dbg_b = 0.0;
    float dbg_alpha = 0.0;
    if(sp_pad > 0.5) {
        const float4 d0 = EmitterTex.Load(int3(0, 0, 0));
        dbg_r = saturate(d0.x / 200.0);
        dbg_g = saturate(d0.y / 200.0);
        dbg_b = saturate(d0.w / 20.0);
        dbg_alpha = 0.5;
    }
    // GPU total light (emitters + sky + sun + ambient floor).
    const float3 gpu_total = min(float3(ambient, ambient, ambient)
                                 + emitter_light + sky_contrib + sun_contrib,
                                 float3(2.0, 2.0, 2.0));
    // max(tint, gpu_total):
    //   Game tiles:  tint = 0 (set by CPU side) → gpu_total drives brightness (Stoneshard)
    //   UI / fonts:  tint = element color (1.0 for white) → stays fully visible
    //   Main menu:   tint = 1.0 (no game state) → all elements bright; emitters add glow
    const float3 combined = max(i.tint.rgb, gpu_total);
    float3 final_rgb = texel.rgb * combined;
    // Debug overlay — REPLACE final colour with raw d0 read so even very dim
    // tiles light up brightly when the upload is delivering data.
    if(sp_pad > 0.5) {
        final_rgb = float3(dbg_r, dbg_g, dbg_b);
    }
    return float4(final_rgb, texel.a * i.tint.a);
}
)HLSL";

// FrameParams cbuffer payload pushed as a vertex uniform per draw segment.
// Layout must match the cbuffer in sprite.vert.hlsl exactly.
struct frame_params {
    float    target_w;
    float    target_h;
    Uint32   instance_base;
    Uint32   pad;
};
static_assert( sizeof( frame_params ) == 16, "frame_params is wire-stable with vert shader" );

// Phase 6/6b: per-frame lighting params (cbuffer slot 1, vertex shader).
// 32 bytes, wire-stable with the LightParams cbuffer in SPRITE_VERT_HLSL.
struct light_params {
    float  tile_pixel_size; // screen pixels per tile (e.g. 32.0)
    float  current_z;       // player z-level
    Uint32 emitter_count;   // live entries in the emitter SSBO
    float  ambient;         // base ambient (0=dark dungeon, 0.05-0.3=outdoors)
    // Phase 6b: camera offset (screen tile → map tile: map_pos = tile_tu - offset)
    float  camera_off_x;    // = tile_map_origin_px.x / tile_px + 0.5
    float  camera_off_y;
    Uint32 sdf_map_w;       // SDF/map width  in tiles
    Uint32 sdf_map_h;       // SDF/map height in tiles (was lp_pad; same type, sizeof unchanged)
};
static_assert( sizeof( light_params ) == 32, "light_params wire-stable with LightParams cbuffer" );

static_assert( sizeof( sun_params ) == 48, "sun_params wire-stable with SunParams cbuffer" );

// ---- 24h sun LUT -------------------------------------------------------
// Defined at file scope so MSVC won't complain about static-local in nested block.
namespace {
struct sun_lut_key { float hr, si, sr, sg, sb, sky_i, sky_r, sky_g, sky_b, elev; };
static const sun_lut_key k_sun[] = {
  //  hr    si     sr     sg     sb    sky_i  sky_r  sky_g  sky_b  elev
    {  0, 0.00f, 0.f,  0.f,  0.f,  0.03f, 0.05f, 0.05f, 0.15f, 0.f   },
    {  5, 0.00f, 0.f,  0.f,  0.f,  0.05f, 0.05f, 0.10f, 0.25f, 0.f   },
    {  6, 0.10f, 0.90f,0.50f,0.20f,0.15f, 0.60f, 0.40f, 0.30f, 0.15f },
    {  8, 0.60f, 1.00f,0.80f,0.50f,0.50f, 0.55f, 0.65f, 0.85f, 0.50f },
    { 12, 1.00f, 1.00f,0.95f,0.80f,0.80f, 0.50f, 0.60f, 0.90f, 0.87f },
    { 16, 0.80f, 1.00f,0.90f,0.60f,0.60f, 0.50f, 0.60f, 0.85f, 0.70f },
    { 19, 0.20f, 1.00f,0.40f,0.10f,0.20f, 0.70f, 0.40f, 0.30f, 0.20f },
    { 21, 0.00f, 0.f,  0.f,  0.f,  0.05f, 0.10f, 0.08f, 0.15f, 0.f   },
    { 24, 0.00f, 0.f,  0.f,  0.f,  0.03f, 0.05f, 0.05f, 0.15f, 0.f   },
};
static constexpr int k_sun_n = static_cast<int>( sizeof( k_sun ) / sizeof( k_sun[0] ) );
} // anonymous namespace

sun_params make_sun_params( float sun_hour ) noexcept
{
    int ki = 0;
    while( ki < k_sun_n - 2 && k_sun[ki + 1].hr <= sun_hour ) {
        ++ki;
    }
    const auto &a = k_sun[ki], &b = k_sun[ki + 1];
    const float dt = ( b.hr > a.hr ) ? ( sun_hour - a.hr ) / ( b.hr - a.hr ) : 0.f;
    auto lp = [dt]( float x, float y ) { return x + dt * ( y - x ); };
    sun_params sp{};
    sp.sun_intensity = lp( a.si,    b.si );
    sp.sun_r         = lp( a.sr,    b.sr );
    sp.sun_g         = lp( a.sg,    b.sg );
    sp.sun_b         = lp( a.sb,    b.sb );
    sp.sky_intensity = lp( a.sky_i, b.sky_i );
    sp.sky_r         = lp( a.sky_r, b.sky_r );
    sp.sky_g         = lp( a.sky_g, b.sky_g );
    sp.sky_b         = lp( a.sky_b, b.sky_b );
    sp.sun_sin_elev  = lp( a.elev,  b.elev );
    // Sun direction rotates E→W (noon = overhead, dawn from east, dusk from west).
    const float angle = ( sun_hour - 12.f ) * 3.14159f / 12.f;
    sp.sun_dir_x = static_cast<float>( cos( static_cast<double>( angle ) ) );
    sp.sun_dir_y = 0.f;
    sp.sp_pad = 0.f;
    return sp;
}

// ---- PIMPL body --------------------------------------------------------

class sprite_batcher_impl
{
    public:
        // Sized so a 1080p screen worth of tiles + UI elements has comfortable
        // headroom (a tile screen at the densest zoom is ~3-4 k instances).
        // 262144 × 64 bytes = 16 MB per ring slot (×3 = 48 MB).
// Needed for 4K displays: minimap (~17K) + large terminal sidebar (~27K) +
// tile sprites (~10K) easily exceed 65536 on high-res setups.
static constexpr Uint32 MAX_INSTANCES = 262144;
        // SDL_GPU defaults to 2-3 frames in flight; 3 ring slots is enough to
        // avoid waiting for the GPU to finish reading the previous frame's
        // storage buffer before we overwrite it.
        static constexpr Uint32 RING_SLOTS   = 3;

        gpu_device *dev = nullptr;
        pipeline_desc desc{};

        SDL_GPUGraphicsPipeline *pipeline = nullptr;
        SDL_GPUShader *vert_shader = nullptr;
        SDL_GPUShader *frag_shader = nullptr;
        SDL_GPUSampler *default_sampler = nullptr;

        SDL_GPUBuffer         *storage_bufs[RING_SLOTS] = {};
        SDL_GPUTransferBuffer *xfer_bufs[RING_SLOTS]    = {};
        Uint32 cur_slot = 0;

        struct segment {
            SDL_GPUTexture *tex;
            SDL_GPUSampler *sampler;
            Uint32 start;
            Uint32 count;
            SDL_Rect scissor     = {};
            bool     has_scissor = false;
        };
        std::vector<sprite_instance> pending;
        std::vector<segment>          segments;

        // Pending segment in progress (not yet pushed to `segments`).
        SDL_GPUTexture *bound_tex      = nullptr;
        SDL_GPUSampler *bound_sampler  = nullptr;
        Uint32          seg_start      = 0;
        SDL_Rect        bound_scissor  = {};
        bool            bound_has_scissor = false;

        // Pass-scope state captured by begin_pass().
        SDL_GPUCommandBuffer *cur_cb = nullptr;
        SDL_GPUTexture       *cur_target = nullptr;
        Uint32 cur_target_w = 0;
        Uint32 cur_target_h = 0;
        bool   cur_clear    = false;
        float  cur_clear_color[4] = {};
        bool   pass_open = false;

        // Phase 7/8: per-frame lighting resources.
        SDL_GPUTexture *lp_emitter_tex  = nullptr;  // fragment sampler 1
        SDL_GPUTexture *lp_sdf_tex      = nullptr;  // fragment sampler 2
        SDL_GPUTexture *lp_sky_vis_tex  = nullptr;  // fragment sampler 3 (Phase 8)
        SDL_GPUSampler *lp_data_sampler = nullptr;
        light_params    lp              = {};  // defaults: all zero
        sun_params      lp_sun          = {};  // Phase 8: sun/sky params

        void set_lighting_resources( float           tile_pixel_size,
                                     float           z_level,
                                     Uint32          count,
                                     float           ambient,
                                     float           cam_off_x    = 0.0f,
                                     float           cam_off_y    = 0.0f,
                                     Uint32          sdf_map_w    = 0u,
                                     Uint32          sdf_map_h    = 0u,
                                     SDL_GPUTexture *emitter_tex  = nullptr,
                                     SDL_GPUTexture *sdf_tex      = nullptr,
                                     SDL_GPUSampler *data_sampler = nullptr,
                                     SDL_GPUTexture *sky_vis_tex  = nullptr,
                                     const sun_params *sp         = nullptr ) noexcept {
            // Guard: if textures exist but sampler is missing, disable GPU lighting.
            if( ( emitter_tex || sdf_tex || sky_vis_tex ) && !data_sampler ) {
                emitter_tex  = nullptr;
                sdf_tex      = nullptr;
                sky_vis_tex  = nullptr;
            }
            lp_emitter_tex  = emitter_tex;
            lp_sdf_tex      = sdf_tex;
            lp_sky_vis_tex  = sky_vis_tex;
            lp_data_sampler = data_sampler;
            lp.tile_pixel_size = tile_pixel_size;
            lp.current_z       = z_level;
            lp.emitter_count   = emitter_tex ? count : 0u;
            lp.ambient         = ambient;
            lp.camera_off_x    = cam_off_x;
            lp.camera_off_y    = cam_off_y;
            lp.sdf_map_w       = sdf_tex ? sdf_map_w : 0u;
            lp.sdf_map_h       = sdf_tex ? sdf_map_h : 0u;
            if( sp ) { lp_sun = *sp; } else { lp_sun = {}; }
        }

        // ---- lifecycle -------------------------------------------------

        void init( gpu_device &d, const pipeline_desc &pd, const char *label ) {
            if( !d.ready() ) {
                throw std::runtime_error( "sprite_batcher::init: gpu_device not ready" );
            }
            dev = &d;
            desc = pd;

            init_shader_compiler();

            // Compile shaders.
            auto v = compile_graphics_shader( d, SPRITE_VERT_HLSL, "main",
                                              SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
                                              "sprite_batcher.vert" );
            if( !v ) {
                throw std::runtime_error( "sprite_batcher vert shader compile failed" );
            }
            auto f = compile_graphics_shader( d, SPRITE_FRAG_HLSL, "main",
                                              SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
                                              "sprite_batcher.frag" );
            if( !f ) {
                SDL_ReleaseGPUShader( d.raw(), v.shader );
                throw std::runtime_error( "sprite_batcher frag shader compile failed" );
            }
            vert_shader = v.shader;
            frag_shader = f.shader;

            // Pipeline.
            SDL_GPUColorTargetBlendState blend{};
            blend.enable_blend = desc.enable_blend;
            blend.src_color_blendfactor = desc.src_color_blend;
            blend.dst_color_blendfactor = desc.dst_color_blend;
            blend.color_blend_op        = desc.color_blend_op;
            blend.src_alpha_blendfactor = desc.src_alpha_blend;
            blend.dst_alpha_blendfactor = desc.dst_alpha_blend;
            blend.alpha_blend_op        = desc.alpha_blend_op;
            blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
                                     SDL_GPU_COLORCOMPONENT_G |
                                     SDL_GPU_COLORCOMPONENT_B |
                                     SDL_GPU_COLORCOMPONENT_A;
            blend.enable_color_write_mask = false;

            SDL_GPUColorTargetDescription color_target{};
            color_target.format = desc.color_target_format;
            color_target.blend_state = blend;

            SDL_GPUGraphicsPipelineCreateInfo pci{};
            pci.vertex_shader   = vert_shader;
            pci.fragment_shader = frag_shader;
            pci.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            pci.target_info.num_color_targets = 1;
            pci.target_info.color_target_descriptions = &color_target;
            pci.target_info.has_depth_stencil_target  = false;
            pci.props = 0;

            pipeline = SDL_CreateGPUGraphicsPipeline( d.raw(), &pci );
            if( !pipeline ) {
                throw std::runtime_error( std::string( "sprite_batcher pipeline: " ) +
                                          SDL_GetError() );
            }

            // Default sampler: NEAREST to keep pixel-art parity with the
            // legacy SDL_Renderer SCALEMODE_NEAREST atlas.
            SDL_GPUSamplerCreateInfo si{};
            si.min_filter     = SDL_GPU_FILTER_NEAREST;
            si.mag_filter     = SDL_GPU_FILTER_NEAREST;
            si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            default_sampler = SDL_CreateGPUSampler( d.raw(), &si );
            if( !default_sampler ) {
                throw std::runtime_error( std::string( "sprite_batcher sampler: " ) +
                                          SDL_GetError() );
            }

            // Ring of (transfer, storage) buffer pairs.
            const Uint32 byte_size = MAX_INSTANCES * sizeof( sprite_instance );
            for( Uint32 i = 0; i < RING_SLOTS; ++i ) {
                SDL_GPUBufferCreateInfo bci{};
                bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
                bci.size  = byte_size;
                bci.props = 0;
                storage_bufs[i] = SDL_CreateGPUBuffer( d.raw(), &bci );

                SDL_GPUTransferBufferCreateInfo tci{};
                tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                tci.size  = byte_size;
                tci.props = 0;
                xfer_bufs[i] = SDL_CreateGPUTransferBuffer( d.raw(), &tci );

                if( !storage_bufs[i] || !xfer_bufs[i] ) {
                    throw std::runtime_error( std::string( "sprite_batcher buffers: " ) +
                                              SDL_GetError() );
                }
            }

            pending.reserve( MAX_INSTANCES );
            segments.reserve( 64 );

            dbg( DL::Info ) << "sprite_batcher initialised (" << ( label ? label : "?" )
                            << ", max_instances=" << MAX_INSTANCES << ")";
        }

        void shutdown() noexcept {
            if( !dev ) {
                return;
            }
            SDL_GPUDevice *r = dev->raw();
            for( Uint32 i = 0; i < RING_SLOTS; ++i ) {
                if( storage_bufs[i] ) {
                    SDL_ReleaseGPUBuffer( r, storage_bufs[i] );
                    storage_bufs[i] = nullptr;
                }
                if( xfer_bufs[i] ) {
                    SDL_ReleaseGPUTransferBuffer( r, xfer_bufs[i] );
                    xfer_bufs[i] = nullptr;
                }
            }
            if( default_sampler ) {
                SDL_ReleaseGPUSampler( r, default_sampler );
                default_sampler = nullptr;
            }
            if( pipeline ) {
                SDL_ReleaseGPUGraphicsPipeline( r, pipeline );
                pipeline = nullptr;
            }
            if( vert_shader ) {
                SDL_ReleaseGPUShader( r, vert_shader );
                vert_shader = nullptr;
            }
            if( frag_shader ) {
                SDL_ReleaseGPUShader( r, frag_shader );
                frag_shader = nullptr;
            }
            pending.clear();
            segments.clear();
            dev = nullptr;
        }

        // ---- per-frame ------------------------------------------------

        void begin_frame() noexcept {
            cur_slot = ( cur_slot + 1 ) % RING_SLOTS;
        }

        // ---- pass scope -----------------------------------------------

        void begin_pass( SDL_GPUCommandBuffer *cb, SDL_GPUTexture *target,
                         Uint32 w, Uint32 h, const float *clear ) {
            if( pass_open ) {
                throw std::runtime_error( "sprite_batcher: begin_pass without end_pass" );
            }
            cur_cb = cb;
            cur_target = target;
            cur_target_w = w;
            cur_target_h = h;
            cur_clear = clear != nullptr;
            if( clear ) {
                std::memcpy( cur_clear_color, clear, sizeof( cur_clear_color ) );
            }
            pending.clear();
            segments.clear();
            bound_tex         = nullptr;
            bound_sampler     = nullptr;
            seg_start         = 0;
            bound_has_scissor = false;
            bound_scissor     = {};
            pass_open         = true;
        }

        void set_texture( SDL_GPUTexture *atlas, SDL_GPUSampler *sampler ) {
            if( !sampler ) {
                sampler = default_sampler;
            }
            if( atlas == bound_tex && sampler == bound_sampler ) {
                return;
            }
            close_segment();
            bound_tex = atlas;
            bound_sampler = sampler;
            seg_start = static_cast<Uint32>( pending.size() );
        }

        void set_scissor( const SDL_Rect *rect ) {
            const bool want = ( rect != nullptr );
            const SDL_Rect r = want ? *rect : SDL_Rect{};
            if( want == bound_has_scissor
                && ( !want || ( r.x == bound_scissor.x && r.y == bound_scissor.y
                                && r.w == bound_scissor.w && r.h == bound_scissor.h ) ) ) {
                return;
            }
            close_segment();
            bound_has_scissor = want;
            bound_scissor     = r;
        }

        void draw( const sprite_instance &inst ) {
            if( pending.size() >= MAX_INSTANCES ) {
                dbg( DL::Warn ) << "sprite_batcher: per-pass instance cap reached, dropping";
                return;
            }
            pending.push_back( inst );
        }

        void draw_many( const sprite_instance *insts, std::size_t count ) {
            const std::size_t headroom = MAX_INSTANCES - pending.size();
            const std::size_t to_add = std::min( headroom, count );
            if( to_add < count ) {
                dbg( DL::Warn ) << "sprite_batcher: dropped " << ( count - to_add )
                                << " instances over per-pass cap";
            }
            pending.insert( pending.end(), insts, insts + to_add );
        }

        void flush() {
            close_segment();
        }

        void end_pass() {
            if( !pass_open ) {
                return;
            }
            close_segment();

            // No work and no clear → trivially nothing to do.
            if( segments.empty() && !cur_clear ) {
                pass_open = false;
                return;
            }

            // Upload pending instances if any.
            if( !pending.empty() ) {
                const Uint32 slot = cur_slot;
                const Uint32 byte_size = static_cast<Uint32>(
                                             pending.size() * sizeof( sprite_instance ) );

                void *mapped = SDL_MapGPUTransferBuffer( dev->raw(), xfer_bufs[slot],
                                                        /*cycle=*/true );
                if( !mapped ) {
                    dbg( DL::Error ) << "MapGPUTransferBuffer failed: " << SDL_GetError();
                    pass_open = false;
                    return;
                }
                std::memcpy( mapped, pending.data(), byte_size );
                SDL_UnmapGPUTransferBuffer( dev->raw(), xfer_bufs[slot] );

                SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cur_cb );
                if( !cp ) {
                    dbg( DL::Error ) << "BeginGPUCopyPass failed: " << SDL_GetError();
                    pass_open = false;
                    return;
                }
                SDL_GPUTransferBufferLocation src{};
                src.transfer_buffer = xfer_bufs[slot];
                src.offset = 0;
                SDL_GPUBufferRegion dst{};
                dst.buffer = storage_bufs[slot];
                dst.offset = 0;
                dst.size = byte_size;
                // 2026-05-22: cycle=true on the storage buffer upload.
                // RING_SLOTS=3 lets the application rotate buffers across
                // frames, but on Win11 D3D12 with heavy frames (5000+
                // instances) the GPU still reads the previous frame's
                // contents of this slot when the CPU starts overwriting,
                // because the SDL_GPU device queues more than RING_SLOTS
                // frames in flight under load. Forcing cycle here makes
                // SDL discard the in-use allocation and provide a fresh
                // one, removing the race. Bridge (1 inst) and per-glyph
                // segments (1 inst each) never showed the symptom; the
                // tile-sprite white-tex segment with thousands of
                // instances did.
                SDL_UploadToGPUBuffer( cp, &src, &dst, /*cycle=*/true );
                SDL_EndGPUCopyPass( cp );
            }

            // Render pass.
            SDL_GPUColorTargetInfo ct{};
            ct.texture = cur_target;
            ct.load_op = cur_clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            ct.store_op = SDL_GPU_STOREOP_STORE;
            if( cur_clear ) {
                ct.clear_color.r = cur_clear_color[0];
                ct.clear_color.g = cur_clear_color[1];
                ct.clear_color.b = cur_clear_color[2];
                ct.clear_color.a = cur_clear_color[3];
            }
            ct.cycle = false;

            SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cur_cb, &ct, 1, nullptr );
            if( !rp ) {
                dbg( DL::Error ) << "BeginGPURenderPass failed: " << SDL_GetError();
                pass_open = false;
                return;
            }

            if( !segments.empty() ) {
                SDL_BindGPUGraphicsPipeline( rp, pipeline );

                SDL_GPUBufferBinding storage_binding{};
                storage_binding.buffer = storage_bufs[cur_slot];
                storage_binding.offset = 0;
                SDL_BindGPUVertexStorageBuffers( rp, /*first_slot=*/0,
                                                 &storage_binding.buffer, 1 );

                // Phase 7: emitter + SDF bound as fragment samplers (slots 1/2).
                // StructuredBuffer space4 → E_INVALIDARG in D3D12 pipeline creation
                // with SDL_shadercross @ 6b06e55c; Texture2D space2 works.
                if( lp_emitter_tex && lp_data_sampler ) {
                    SDL_GPUTextureSamplerBinding emitter_bind{ lp_emitter_tex,
                                                               lp_data_sampler };
                    SDL_BindGPUFragmentSamplers( rp, /*first_slot=*/1, &emitter_bind, 1 );
                }
                if( lp_sdf_tex && lp_data_sampler ) {
                    SDL_GPUTextureSamplerBinding sdf_bind{ lp_sdf_tex, lp_data_sampler };
                    SDL_BindGPUFragmentSamplers( rp, /*first_slot=*/2, &sdf_bind, 1 );
                }
                if( lp_sky_vis_tex && lp_data_sampler ) {
                    SDL_GPUTextureSamplerBinding sky_bind{ lp_sky_vis_tex, lp_data_sampler };
                    SDL_BindGPUFragmentSamplers( rp, /*first_slot=*/3, &sky_bind, 1 );
                }

                const SDL_GPUViewport vp{
                    0.0f, 0.0f,
                    static_cast<float>( cur_target_w ),
                    static_cast<float>( cur_target_h ),
                    0.0f, 1.0f
                };
                SDL_SetGPUViewport( rp, &vp );

                bool     last_has_scissor = false;
                SDL_Rect last_scissor     = {};
                for( const segment &s : segments ) {
                    const bool changed = ( s.has_scissor != last_has_scissor )
                                         || ( s.has_scissor
                                              && ( s.scissor.x != last_scissor.x
                                                   || s.scissor.y != last_scissor.y
                                                   || s.scissor.w != last_scissor.w
                                                   || s.scissor.h != last_scissor.h ) );
                    if( changed ) {
                        if( s.has_scissor ) {
                            SDL_SetGPUScissor( rp, &s.scissor );
                        } else {
                            const SDL_Rect full{ 0, 0,
                                                 static_cast<int>( cur_target_w ),
                                                 static_cast<int>( cur_target_h ) };
                            SDL_SetGPUScissor( rp, &full );
                        }
                        last_has_scissor = s.has_scissor;
                        last_scissor     = s.scissor;
                    }

                    SDL_GPUTextureSamplerBinding tsb{};
                    tsb.texture = s.tex;
                    tsb.sampler = s.sampler;
                    SDL_BindGPUFragmentSamplers( rp, /*first_slot=*/0, &tsb, 1 );

                    // Re-bind emitter/SDF/sky-vis each segment. On D3D12 a
                    // SDL_BindGPUFragmentSamplers call may rebuild the descriptor
                    // table and zero any slot not included in that call.
                    if( lp_emitter_tex && lp_data_sampler ) {
                        SDL_GPUTextureSamplerBinding eb{ lp_emitter_tex, lp_data_sampler };
                        SDL_BindGPUFragmentSamplers( rp, /*first_slot=*/1, &eb, 1 );
                    }
                    if( lp_sdf_tex && lp_data_sampler ) {
                        SDL_GPUTextureSamplerBinding sb{ lp_sdf_tex, lp_data_sampler };
                        SDL_BindGPUFragmentSamplers( rp, /*first_slot=*/2, &sb, 1 );
                    }
                    if( lp_sky_vis_tex && lp_data_sampler ) {
                        SDL_GPUTextureSamplerBinding skb{ lp_sky_vis_tex, lp_data_sampler };
                        SDL_BindGPUFragmentSamplers( rp, /*first_slot=*/3, &skb, 1 );
                    }

                    frame_params fp{
                        static_cast<float>( cur_target_w ),
                        static_cast<float>( cur_target_h ),
                        s.start,
                        0u
                    };
                    SDL_PushGPUVertexUniformData( cur_cb, /*slot=*/0, &fp, sizeof( fp ) );

                    // Vertex slot 1: LightParams (world_pos computation)
                    SDL_PushGPUVertexUniformData( cur_cb, /*slot=*/1, &lp, sizeof( lp ) );
                    // Fragment slot 0: LightParams (ambient, emitter_count, sdf_map_w)
                    SDL_PushGPUFragmentUniformData( cur_cb, /*slot=*/0, &lp, sizeof( lp ) );
                    // Fragment slot 1: SunParams (sun/sky direction + color)
                    SDL_PushGPUFragmentUniformData( cur_cb, /*slot=*/1, &lp_sun, sizeof( lp_sun ) );

                    SDL_DrawGPUPrimitives( rp, /*num_vertices=*/6, /*num_instances=*/s.count,
                                           /*first_vertex=*/0, /*first_instance=*/0 );
                }
            }

            SDL_EndGPURenderPass( rp );
            pass_open = false;
        }

    private:
        // Move the currently bound (tex, sampler) + accumulated count into
        // `segments`. Safe to call when no instances are pending — no-ops.
        void close_segment() {
            const Uint32 end = static_cast<Uint32>( pending.size() );
            if( !bound_tex || end <= seg_start ) {
                return;
            }
            segment s{ bound_tex, bound_sampler, seg_start, end - seg_start };
            s.scissor     = bound_scissor;
            s.has_scissor = bound_has_scissor;
            segments.push_back( s );
            seg_start = end;
        }
};

// ---- sprite_batcher trampolines ---------------------------------------

sprite_batcher::sprite_batcher() : p( std::make_unique<sprite_batcher_impl>() ) {}
sprite_batcher::sprite_batcher( sprite_batcher && ) noexcept = default;
sprite_batcher &sprite_batcher::operator=( sprite_batcher && ) noexcept = default;
sprite_batcher::~sprite_batcher()
{
    if( p ) {
        p->shutdown();
    }
}

void sprite_batcher::init( gpu_device &dev, const pipeline_desc &desc,
                           const char *debug_label )
{
    p->init( dev, desc, debug_label );
}

void sprite_batcher::shutdown() noexcept
{
    if( p ) {
        p->shutdown();
    }
}

void sprite_batcher::begin_pass( SDL_GPUCommandBuffer *cb,
                                 SDL_GPUTexture *target,
                                 std::uint32_t target_w,
                                 std::uint32_t target_h,
                                 const float *clear_color_rgba )
{
    p->begin_pass( cb, target, target_w, target_h, clear_color_rgba );
}

void sprite_batcher::set_texture( SDL_GPUTexture *atlas, SDL_GPUSampler *sampler )
{
    p->set_texture( atlas, sampler );
}

void sprite_batcher::set_scissor( const SDL_Rect *rect )
{
    p->set_scissor( rect );
}

void sprite_batcher::set_lighting_resources( float            tile_pixel_size,
                                              float            z_level,
                                              Uint32           emitter_count,
                                              float            ambient,
                                              float            cam_off_x,
                                              float            cam_off_y,
                                              Uint32           sdf_map_w,
                                              Uint32           sdf_map_h,
                                              SDL_GPUTexture  *emitter_tex,
                                              SDL_GPUTexture  *sdf_tex,
                                              SDL_GPUSampler  *data_sampler,
                                              SDL_GPUTexture  *sky_vis_tex,
                                              const sun_params *sp )
{
    p->set_lighting_resources( tile_pixel_size, z_level,
                                emitter_count, ambient,
                                cam_off_x, cam_off_y, sdf_map_w, sdf_map_h,
                                emitter_tex, sdf_tex, data_sampler,
                                sky_vis_tex, sp );
}

void sprite_batcher::draw( const sprite_instance &inst )
{
    p->draw( inst );
}

void sprite_batcher::draw( const sprite_instance *insts, std::size_t count )
{
    p->draw_many( insts, count );
}

void sprite_batcher::flush()
{
    p->flush();
}

void sprite_batcher::end_pass()
{
    p->end_pass();
}

void sprite_batcher::begin_frame()
{
    p->begin_frame();
}

} // namespace lighting
