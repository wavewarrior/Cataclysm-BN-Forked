#include "sprite_batcher.h"

#include "shader_compiler.h"
#include "debug.h"

#include <algorithm>
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
StructuredBuffer<SpriteInstance> Instances : register(t0, space0);
cbuffer FrameParams : register(b0, space1) {
    float2 target_size;
    uint   instance_base;
    uint   pad;
};
struct VS_OUT {
    float4 pos  : SV_Position;
    float2 uv   : TEXCOORD0;
    float4 tint : TEXCOORD1;
};
static const float2 quad_uv[6] = {
    float2(0.0,0.0), float2(1.0,0.0), float2(0.0,1.0),
    float2(1.0,0.0), float2(1.0,1.0), float2(0.0,1.0)
};
VS_OUT main(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    const SpriteInstance s = Instances[iid + instance_base];
    const float2 c = quad_uv[vid];
    // Rotate offset around quad centre. rotation is in radians, positive =
    // clockwise in screen space (Y-down), matching SDL_RenderTextureRotated.
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
    VS_OUT o;
    o.pos = float4(ndc, 0.0, 1.0);
    o.uv  = float2(s.src_u + c.x * s.src_uw, s.src_v + c.y * s.src_vh);
    o.tint = float4(s.tint_r, s.tint_g, s.tint_b, s.tint_a);
    return o;
}
)HLSL";

static const char *const SPRITE_FRAG_HLSL = R"HLSL(
Texture2D<float4> Atlas    : register(t0, space2);
SamplerState      AtlasSmp : register(s0, space2);
struct VS_OUT {
    float4 pos  : SV_Position;
    float2 uv   : TEXCOORD0;
    float4 tint : TEXCOORD1;
};
float4 main(VS_OUT i) : SV_Target0 {
    const float4 texel = Atlas.Sample(AtlasSmp, i.uv);
    return texel * i.tint;
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

// ---- PIMPL body --------------------------------------------------------

class sprite_batcher_impl
{
    public:
        // Sized so a 1080p screen worth of tiles + UI elements has comfortable
        // headroom (a tile screen at the densest zoom is ~3-4 k instances).
        static constexpr Uint32 MAX_INSTANCES = 65536;
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
        };
        std::vector<sprite_instance> pending;
        std::vector<segment>          segments;

        // Pending segment in progress (not yet pushed to `segments`).
        SDL_GPUTexture *bound_tex     = nullptr;
        SDL_GPUSampler *bound_sampler = nullptr;
        Uint32          seg_start     = 0;

        // Pass-scope state captured by begin_pass().
        SDL_GPUCommandBuffer *cur_cb = nullptr;
        SDL_GPUTexture       *cur_target = nullptr;
        Uint32 cur_target_w = 0;
        Uint32 cur_target_h = 0;
        bool   cur_clear    = false;
        float  cur_clear_color[4] = {};
        bool   pass_open = false;

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
            bound_tex = nullptr;
            bound_sampler = nullptr;
            seg_start = 0;
            pass_open = true;
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

                const SDL_GPUViewport vp{
                    0.0f, 0.0f,
                    static_cast<float>( cur_target_w ),
                    static_cast<float>( cur_target_h ),
                    0.0f, 1.0f
                };
                SDL_SetGPUViewport( rp, &vp );

                for( const segment &s : segments ) {
                    SDL_GPUTextureSamplerBinding tsb{};
                    tsb.texture = s.tex;
                    tsb.sampler = s.sampler;
                    SDL_BindGPUFragmentSamplers( rp, /*first_slot=*/0, &tsb, 1 );

                    frame_params fp{
                        static_cast<float>( cur_target_w ),
                        static_cast<float>( cur_target_h ),
                        s.start,
                        0u
                    };
                    SDL_PushGPUVertexUniformData( cur_cb, /*slot=*/0, &fp, sizeof( fp ) );

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
            segments.push_back( segment{ bound_tex, bound_sampler,
                                         seg_start, end - seg_start } );
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
