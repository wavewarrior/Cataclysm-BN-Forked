#include "rmlui_render_interface.h"

#include <cstddef>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "debug.h"
#include "gpu_device.h"
#include "shader_compiler.h"

// Lighting/ files must define dbg themselves (not globally available).
#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace lighting
{

namespace
{
// Keep released GPU resources alive this many frames before freeing, so the
// GPU is guaranteed done reading buffers/textures still in flight.
constexpr std::uint64_t KEEP_FRAMES = 4;

// Per-RenderGeometry vertex uniform (register b0, space1 in rmlui.vert.hlsl).
struct vert_params {
    float translation_x;
    float translation_y;
    float viewport_w;
    float viewport_h;
};
}  // namespace

struct rmlui_render_interface::impl {
    gpu_device *dev = nullptr;
    SDL_GPUDevice *raw = nullptr;

    SDL_GPUGraphicsPipeline *pipeline = nullptr;
    SDL_GPUShader *vert = nullptr;
    SDL_GPUShader *frag = nullptr;
    SDL_GPUSampler *sampler = nullptr;
    SDL_GPUTexture *white_tex = nullptr;

    // CPU-side compiled geometry; GPU buffers filled lazily in upload_pending.
    struct geom {
        std::vector<Rml::Vertex> verts;
        std::vector<int> idx;
        SDL_GPUBuffer *vbuf = nullptr;
        SDL_GPUBuffer *ibuf = nullptr;
        std::uint32_t idx_count = 0;
        bool uploaded = false;
    };
    std::unordered_map<std::uint64_t, geom> geoms;
    std::vector<std::uint64_t> pending;          // compiled, awaiting GPU upload
    std::uint64_t next_geom = 1;                 // 1-based: 0 is RmlUi's "invalid"

    std::unordered_map<std::uint64_t, SDL_GPUTexture *> textures;
    std::uint64_t next_tex = 1;

    // Deferred frees (run KEEP_FRAMES after the freeing frame).
    std::uint64_t frame = 0;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> deferred;

    // Active render-pass state (set by begin_render_pass).
    SDL_GPURenderPass *rp = nullptr;
    SDL_GPUCommandBuffer *cb = nullptr;
    std::uint32_t target_w = 0, target_h = 0;
    std::uint32_t proj_w = 0, proj_h = 0;

    bool scissor_enabled = false;
    Rml::Rectanglei scissor{};

    std::uint32_t compiles_in_pass = 0;
    std::uint32_t textures_in_pass = 0;

    // Create an RGBA8 SAMPLER texture and upload `pixels` on a dedicated command
    // buffer submitted immediately (safe outside any open render pass).
    SDL_GPUTexture *upload_rgba( const std::uint8_t *pixels, int w, int h ) {
        if( !raw || w <= 0 || h <= 0 ) {
            return nullptr;
        }
        SDL_GPUTextureCreateInfo tci{};
        tci.type = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        tci.width = static_cast<std::uint32_t>( w );
        tci.height = static_cast<std::uint32_t>( h );
        tci.layer_count_or_depth = 1;
        tci.num_levels = 1;
        tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture *tex = SDL_CreateGPUTexture( raw, &tci );
        if( !tex ) {
            dbg( DL::Error ) << "rmlui: CreateGPUTexture failed: " << SDL_GetError();
            return nullptr;
        }
        const std::uint32_t bytes = static_cast<std::uint32_t>( w ) *
                                    static_cast<std::uint32_t>( h ) * 4u;
        SDL_GPUTransferBufferCreateInfo tbi{};
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbi.size = bytes;
        SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer( raw, &tbi );
        if( !xfer ) {
            SDL_ReleaseGPUTexture( raw, tex );
            return nullptr;
        }
        void *mapped = SDL_MapGPUTransferBuffer( raw, xfer, false );
        if( !mapped ) {
            SDL_ReleaseGPUTransferBuffer( raw, xfer );
            SDL_ReleaseGPUTexture( raw, tex );
            return nullptr;
        }
        std::memcpy( mapped, pixels, bytes );
        SDL_UnmapGPUTransferBuffer( raw, xfer );

        SDL_GPUCommandBuffer *ucb = SDL_AcquireGPUCommandBuffer( raw );
        if( !ucb ) {
            SDL_ReleaseGPUTransferBuffer( raw, xfer );
            SDL_ReleaseGPUTexture( raw, tex );
            return nullptr;
        }
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( ucb );
        SDL_GPUTextureTransferInfo ti{};
        ti.transfer_buffer = xfer;
        ti.offset = 0;
        ti.pixels_per_row = static_cast<std::uint32_t>( w );
        ti.rows_per_layer = static_cast<std::uint32_t>( h );
        SDL_GPUTextureRegion region{};
        region.texture = tex;
        region.w = static_cast<std::uint32_t>( w );
        region.h = static_cast<std::uint32_t>( h );
        region.d = 1;
        SDL_UploadToGPUTexture( cp, &ti, &region, false );
        SDL_EndGPUCopyPass( cp );
        SDL_SubmitGPUCommandBuffer( ucb );
        SDL_ReleaseGPUTransferBuffer( raw, xfer );
        return tex;
    }
};

rmlui_render_interface::rmlui_render_interface() : p( std::make_unique<impl>() ) {}
rmlui_render_interface::~rmlui_render_interface() = default;

bool rmlui_render_interface::init( gpu_device &dev )
{
    if( !dev.ready() ) {
        return false;
    }
    p->dev = &dev;
    p->raw = dev.raw();

    init_shader_compiler();
    const std::string vsrc = load_lighting_shader_source( "rmlui.vert.hlsl" );
    const std::string fsrc = load_lighting_shader_source( "rmlui.frag.hlsl" );
    compiled_shader v = compile_graphics_shader( dev, vsrc, "main",
                        SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "rmlui.vert" );
    if( !v ) {
        dbg( DL::Error ) << "rmlui: vert shader compile failed";
        return false;
    }
    compiled_shader f = compile_graphics_shader( dev, fsrc, "main",
                        SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "rmlui.frag" );
    if( !f ) {
        SDL_ReleaseGPUShader( p->raw, v.shader );
        dbg( DL::Error ) << "rmlui: frag shader compile failed";
        return false;
    }
    p->vert = v.shader;
    p->frag = f.shader;
    // Gate log: expect samplers=1, storage_buffers=0 (low-risk reflection path).
    DebugLogFL( DL::Info, DC::Main )
            << "rmlui.frag reflection: samplers=" << f.resources.num_samplers
            << " storage_textures=" << f.resources.num_storage_textures
            << " storage_buffers=" << f.resources.num_storage_buffers
            << " uniform_buffers=" << f.resources.num_uniform_buffers
            << " (expect samplers=1 st=0 sb=0)";

    // Vertex layout = Rml::Vertex { Vector2f position; ColourbPremultiplied
    // colour; Vector2f tex_coord }.
    SDL_GPUVertexBufferDescription vbd{};
    vbd.slot = 0;
    vbd.pitch = static_cast<std::uint32_t>( sizeof( Rml::Vertex ) );
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbd.instance_step_rate = 0;
    SDL_GPUVertexAttribute attrs[3]{};
    attrs[0].location = 0;
    attrs[0].buffer_slot = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[0].offset = static_cast<std::uint32_t>( offsetof( Rml::Vertex, position ) );
    attrs[1].location = 1;
    attrs[1].buffer_slot = 0;
    attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    attrs[1].offset = static_cast<std::uint32_t>( offsetof( Rml::Vertex, colour ) );
    attrs[2].location = 2;
    attrs[2].buffer_slot = 0;
    attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[2].offset = static_cast<std::uint32_t>( offsetof( Rml::Vertex, tex_coord ) );

    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;  // premultiplied alpha
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                             SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = dev.swapchain_format();
    ctd.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = p->vert;
    pci.fragment_shader = p->frag;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.vertex_input_state.num_vertex_buffers = 1;
    pci.vertex_input_state.vertex_buffer_descriptions = &vbd;
    pci.vertex_input_state.num_vertex_attributes = 3;
    pci.vertex_input_state.vertex_attributes = attrs;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.target_info.num_color_targets = 1;
    pci.target_info.color_target_descriptions = &ctd;
    pci.target_info.has_depth_stencil_target = false;
    p->pipeline = SDL_CreateGPUGraphicsPipeline( p->raw, &pci );
    if( !p->pipeline ) {
        dbg( DL::Error ) << "rmlui: pipeline create failed: " << SDL_GetError();
        return false;
    }

    // Linear sampler with clamp — fonts/decorators want smooth scaling.
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    p->sampler = SDL_CreateGPUSampler( p->raw, &si );
    if( !p->sampler ) {
        dbg( DL::Error ) << "rmlui: sampler create failed: " << SDL_GetError();
        return false;
    }

    // 1x1 opaque white — bound for untextured geometry (TextureHandle 0) so the
    // fragment shader's texel*colour degenerates to the flat vertex colour.
    const std::uint8_t white[4] = { 255, 255, 255, 255 };
    p->white_tex = p->upload_rgba( white, 1, 1 );
    if( !p->white_tex ) {
        dbg( DL::Error ) << "rmlui: white texture create failed";
        return false;
    }

    dbg( DL::Info ) << "rmlui_render_interface: init ok";
    return true;
}

void rmlui_render_interface::shutdown()
{
    if( !p->raw ) {
        return;
    }
    // Run all deferred frees regardless of frame age.
    for( auto &d : p->deferred ) {
        d.second();
    }
    p->deferred.clear();
    for( auto &kv : p->geoms ) {
        if( kv.second.vbuf ) {
            SDL_ReleaseGPUBuffer( p->raw, kv.second.vbuf );
        }
        if( kv.second.ibuf ) {
            SDL_ReleaseGPUBuffer( p->raw, kv.second.ibuf );
        }
    }
    p->geoms.clear();
    p->pending.clear();
    for( auto &kv : p->textures ) {
        SDL_ReleaseGPUTexture( p->raw, kv.second );
    }
    p->textures.clear();
    if( p->white_tex ) {
        SDL_ReleaseGPUTexture( p->raw, p->white_tex );
        p->white_tex = nullptr;
    }
    if( p->sampler ) {
        SDL_ReleaseGPUSampler( p->raw, p->sampler );
        p->sampler = nullptr;
    }
    if( p->pipeline ) {
        SDL_ReleaseGPUGraphicsPipeline( p->raw, p->pipeline );
        p->pipeline = nullptr;
    }
    if( p->vert ) {
        SDL_ReleaseGPUShader( p->raw, p->vert );
        p->vert = nullptr;
    }
    if( p->frag ) {
        SDL_ReleaseGPUShader( p->raw, p->frag );
        p->frag = nullptr;
    }
    p->raw = nullptr;
    p->dev = nullptr;
}

void rmlui_render_interface::begin_frame()
{
    p->frame++;
    auto &d = p->deferred;
    for( std::size_t i = 0; i < d.size(); ) {
        if( d[i].first + KEEP_FRAMES <= p->frame ) {
            d[i].second();
            d[i] = std::move( d.back() );
            d.pop_back();
        } else {
            ++i;
        }
    }
}

void rmlui_render_interface::upload_pending( SDL_GPUCommandBuffer *cb )
{
    if( p->pending.empty() || !cb ) {
        return;
    }
    for( std::uint64_t h : p->pending ) {
        auto it = p->geoms.find( h );
        if( it == p->geoms.end() || it->second.uploaded ) {
            continue;
        }
        impl::geom &g = it->second;
        const std::uint32_t vbytes = static_cast<std::uint32_t>(
                                         g.verts.size() * sizeof( Rml::Vertex ) );
        const std::uint32_t ibytes = static_cast<std::uint32_t>(
                                         g.idx.size() * sizeof( int ) );
        if( vbytes == 0 || ibytes == 0 ) {
            g.uploaded = true;
            continue;
        }
        SDL_GPUBufferCreateInfo vci{};
        vci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        vci.size = vbytes;
        g.vbuf = SDL_CreateGPUBuffer( p->raw, &vci );
        SDL_GPUBufferCreateInfo ici{};
        ici.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        ici.size = ibytes;
        g.ibuf = SDL_CreateGPUBuffer( p->raw, &ici );

        SDL_GPUTransferBufferCreateInfo tvi{};
        tvi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tvi.size = vbytes;
        SDL_GPUTransferBuffer *vx = SDL_CreateGPUTransferBuffer( p->raw, &tvi );
        SDL_GPUTransferBufferCreateInfo tii{};
        tii.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tii.size = ibytes;
        SDL_GPUTransferBuffer *ix = SDL_CreateGPUTransferBuffer( p->raw, &tii );
        if( !g.vbuf || !g.ibuf || !vx || !ix ) {
            dbg( DL::Error ) << "rmlui: geometry buffer alloc failed: " << SDL_GetError();
            continue;
        }
        void *mv = SDL_MapGPUTransferBuffer( p->raw, vx, false );
        std::memcpy( mv, g.verts.data(), vbytes );
        SDL_UnmapGPUTransferBuffer( p->raw, vx );
        void *mi = SDL_MapGPUTransferBuffer( p->raw, ix, false );
        std::memcpy( mi, g.idx.data(), ibytes );
        SDL_UnmapGPUTransferBuffer( p->raw, ix );

        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
        SDL_GPUTransferBufferLocation vsrc{};
        vsrc.transfer_buffer = vx;
        SDL_GPUBufferRegion vdst{};
        vdst.buffer = g.vbuf;
        vdst.size = vbytes;
        SDL_UploadToGPUBuffer( cp, &vsrc, &vdst, false );
        SDL_GPUTransferBufferLocation isrc{};
        isrc.transfer_buffer = ix;
        SDL_GPUBufferRegion idst{};
        idst.buffer = g.ibuf;
        idst.size = ibytes;
        SDL_UploadToGPUBuffer( cp, &isrc, &idst, false );
        SDL_EndGPUCopyPass( cp );

        // Transfer buffers must outlive this frame's submit — defer their free.
        SDL_GPUDevice *raw = p->raw;
        p->deferred.emplace_back( p->frame, [raw, vx, ix]() {
            SDL_ReleaseGPUTransferBuffer( raw, vx );
            SDL_ReleaseGPUTransferBuffer( raw, ix );
        } );

        g.idx_count = static_cast<std::uint32_t>( g.idx.size() );
        g.uploaded = true;
    }
    p->pending.clear();
}

void rmlui_render_interface::begin_render_pass( SDL_GPURenderPass *rp,
        SDL_GPUCommandBuffer *cb, std::uint32_t target_w, std::uint32_t target_h,
        std::uint32_t proj_w, std::uint32_t proj_h )
{
    p->rp = rp;
    p->cb = cb;
    p->target_w = target_w;
    p->target_h = target_h;
    p->proj_w = proj_w == 0 ? target_w : proj_w;
    p->proj_h = proj_h == 0 ? target_h : proj_h;
    p->scissor_enabled = false;
}

void rmlui_render_interface::end_render_pass()
{
    p->rp = nullptr;
    p->cb = nullptr;
}

std::uint32_t rmlui_render_interface::compiles_in_pass() const noexcept
{
    return p->compiles_in_pass;
}

std::uint32_t rmlui_render_interface::textures_in_pass() const noexcept
{
    return p->textures_in_pass;
}

Rml::CompiledGeometryHandle rmlui_render_interface::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices )
{
    if( p->rp != nullptr ) {
        // Compiled mid-Render(): would force an in-pass upload on D3D12. We defer
        // to next frame's upload_pending instead — count it for the Phase-5 gate.
        p->compiles_in_pass++;
    }
    const std::uint64_t h = p->next_geom++;
    impl::geom g;
    g.verts.assign( vertices.begin(), vertices.end() );
    g.idx.assign( indices.begin(), indices.end() );
    p->geoms.emplace( h, std::move( g ) );
    p->pending.push_back( h );
    return static_cast<Rml::CompiledGeometryHandle>( h );
}

void rmlui_render_interface::RenderGeometry( Rml::CompiledGeometryHandle geometry,
        Rml::Vector2f translation, Rml::TextureHandle texture )
{
    if( !p->rp || !p->cb || !p->pipeline ) {
        return;
    }
    auto it = p->geoms.find( static_cast<std::uint64_t>( geometry ) );
    if( it == p->geoms.end() ) {
        return;
    }
    impl::geom &g = it->second;
    if( !g.uploaded || g.idx_count == 0 || !g.vbuf || !g.ibuf ) {
        return;  // not yet uploaded (compiled mid-render last frame) — 1-frame pop-in
    }

    SDL_BindGPUGraphicsPipeline( p->rp, p->pipeline );

    SDL_GPUBufferBinding vb{};
    vb.buffer = g.vbuf;
    SDL_BindGPUVertexBuffers( p->rp, 0, &vb, 1 );
    SDL_GPUBufferBinding ib{};
    ib.buffer = g.ibuf;
    SDL_BindGPUIndexBuffer( p->rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT );

    // Texture: TextureHandle 0 is the documented untextured sentinel -> white
    // (never bind null — D3D12 invariant #3).
    SDL_GPUTexture *tex = p->white_tex;
    if( texture != 0 ) {
        auto tit = p->textures.find( static_cast<std::uint64_t>( texture ) );
        if( tit != p->textures.end() ) {
            tex = tit->second;
        }
    }
    SDL_GPUTextureSamplerBinding tsb{};
    tsb.texture = tex;
    tsb.sampler = p->sampler;
    SDL_BindGPUFragmentSamplers( p->rp, 0, &tsb, 1 );

    // Scissor in logical coords -> physical target pixels (scale when proj!=target).
    const float sx = p->proj_w ? static_cast<float>( p->target_w ) / p->proj_w : 1.f;
    const float sy = p->proj_h ? static_cast<float>( p->target_h ) / p->proj_h : 1.f;
    SDL_Rect sc;
    if( p->scissor_enabled ) {
        sc.x = static_cast<int>( p->scissor.Left() * sx );
        sc.y = static_cast<int>( p->scissor.Top() * sy );
        sc.w = static_cast<int>( p->scissor.Width() * sx );
        sc.h = static_cast<int>( p->scissor.Height() * sy );
    } else {
        sc.x = 0;
        sc.y = 0;
        sc.w = static_cast<int>( p->target_w );
        sc.h = static_cast<int>( p->target_h );
    }
    SDL_SetGPUScissor( p->rp, &sc );

    const vert_params vp{ translation.x, translation.y,
                          static_cast<float>( p->proj_w ), static_cast<float>( p->proj_h ) };
    SDL_PushGPUVertexUniformData( p->cb, 0, &vp, sizeof( vp ) );

    SDL_DrawGPUIndexedPrimitives( p->rp, g.idx_count, 1, 0, 0, 0 );
}

void rmlui_render_interface::ReleaseGeometry( Rml::CompiledGeometryHandle geometry )
{
    auto it = p->geoms.find( static_cast<std::uint64_t>( geometry ) );
    if( it == p->geoms.end() ) {
        return;
    }
    SDL_GPUDevice *raw = p->raw;
    SDL_GPUBuffer *vbuf = it->second.vbuf;
    SDL_GPUBuffer *ibuf = it->second.ibuf;
    if( vbuf || ibuf ) {
        p->deferred.emplace_back( p->frame, [raw, vbuf, ibuf]() {
            if( vbuf ) {
                SDL_ReleaseGPUBuffer( raw, vbuf );
            }
            if( ibuf ) {
                SDL_ReleaseGPUBuffer( raw, ibuf );
            }
        } );
    }
    p->geoms.erase( it );
}

Rml::TextureHandle rmlui_render_interface::LoadTexture(
    Rml::Vector2i &texture_dimensions, const Rml::String &source )
{
    SDL_Surface *surf = IMG_Load( source.c_str() );
    if( !surf ) {
        dbg( DL::Warn ) << "rmlui: LoadTexture failed for " << source << ": " << SDL_GetError();
        return 0;
    }
    SDL_Surface *rgba = surf;
    if( surf->format != SDL_PIXELFORMAT_RGBA32 ) {
        rgba = SDL_ConvertSurface( surf, SDL_PIXELFORMAT_RGBA32 );
        SDL_DestroySurface( surf );
        if( !rgba ) {
            return 0;
        }
    }
    SDL_GPUTexture *tex = p->upload_rgba( static_cast<const std::uint8_t *>( rgba->pixels ),
                                          rgba->w, rgba->h );
    texture_dimensions = Rml::Vector2i( rgba->w, rgba->h );
    SDL_DestroySurface( rgba );
    if( !tex ) {
        return 0;
    }
    if( p->rp != nullptr ) {
        p->textures_in_pass++;
    }
    const std::uint64_t h = p->next_tex++;
    p->textures.emplace( h, tex );
    return static_cast<Rml::TextureHandle>( h );
}

Rml::TextureHandle rmlui_render_interface::GenerateTexture(
    Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions )
{
    SDL_GPUTexture *tex = p->upload_rgba(
                              reinterpret_cast<const std::uint8_t *>( source.data() ),
                              source_dimensions.x, source_dimensions.y );
    if( !tex ) {
        return 0;
    }
    if( p->rp != nullptr ) {
        p->textures_in_pass++;
    }
    const std::uint64_t h = p->next_tex++;
    p->textures.emplace( h, tex );
    return static_cast<Rml::TextureHandle>( h );
}

void rmlui_render_interface::ReleaseTexture( Rml::TextureHandle texture )
{
    auto it = p->textures.find( static_cast<std::uint64_t>( texture ) );
    if( it == p->textures.end() ) {
        return;
    }
    SDL_GPUDevice *raw = p->raw;
    SDL_GPUTexture *tex = it->second;
    p->deferred.emplace_back( p->frame, [raw, tex]() {
        SDL_ReleaseGPUTexture( raw, tex );
    } );
    p->textures.erase( it );
}

void rmlui_render_interface::EnableScissorRegion( bool enable )
{
    p->scissor_enabled = enable;
}

void rmlui_render_interface::SetScissorRegion( Rml::Rectanglei region )
{
    p->scissor = region;
    p->scissor_enabled = true;
}

}  // namespace lighting
