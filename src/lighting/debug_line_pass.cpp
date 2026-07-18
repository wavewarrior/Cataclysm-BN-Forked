#include "debug_line_pass.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"

#include <algorithm>
#include <cstring>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

// ---- Destructor ----------------------------------------------------------

debug_line_pass::~debug_line_pass()
{
    shutdown();
}

// ---- Init ----------------------------------------------------------------

auto debug_line_pass::init( gpu_device &dev,
                            SDL_GPUTextureFormat target_format ) -> bool
{
    shutdown();
    dev_ = &dev;

    if( !dev.ready() ) {
        dbg( DL::Error ) << "debug_line_pass::init: gpu_device not ready";
        return false;
    }

    init_shader_compiler();

    const auto vert_src = load_lighting_shader_source( "debug_line.vert.hlsl" );
    const auto frag_src = load_lighting_shader_source( "debug_line.frag.hlsl" );
    if( vert_src.empty() || frag_src.empty() ) {
        dbg( DL::Error ) << "debug_line_pass: failed to load shader source";
        return false;
    }

    auto v = compile_graphics_shader(
        dev, vert_src, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "debug_line.vert" );
    auto f = compile_graphics_shader(
        dev, frag_src, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "debug_line.frag" );
    if( !v || !f ) {
        if( v ) { SDL_ReleaseGPUShader( dev.raw(), v.shader ); }
        if( f ) { SDL_ReleaseGPUShader( dev.raw(), f.shader ); }
        dbg( DL::Error ) << "debug_line_pass: shader compile failed";
        return false;
    }
    vert_ = v.shader;
    frag_ = f.shader;

    // Pipeline: standard alpha blend over the world target.
    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
                           | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = target_format;
    ctd.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vert_;
    pci.fragment_shader = frag_;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pci.target_info.num_color_targets = 1;
    pci.target_info.color_target_descriptions = &ctd;
    pci.target_info.has_depth_stencil_target = false;

    pipeline_ = SDL_CreateGPUGraphicsPipeline( dev.raw(), &pci );
    if( !pipeline_ ) {
        dbg( DL::Error ) << "debug_line_pass: pipeline creation failed";
        return false;
    }

    // Transfer + storage buffers for instanced draw.
    constexpr auto buf_size =
        static_cast<Uint32>( MAX_LINES * sizeof( debug_line_vertex ) );

    SDL_GPUTransferBufferCreateInfo xfer_ci{};
    xfer_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    xfer_ci.size = buf_size;
    xfer_ = SDL_CreateGPUTransferBuffer( dev.raw(), &xfer_ci );

    SDL_GPUBufferCreateInfo stor_ci{};
    stor_ci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    stor_ci.size = buf_size;
    storage_ = SDL_CreateGPUBuffer( dev.raw(), &stor_ci );

    if( !xfer_ || !storage_ ) {
        dbg( DL::Error ) << "debug_line_pass: buffer creation failed";
        return false;
    }

    lines_.reserve( 1024 );
    return true;
}

// ---- Shutdown ------------------------------------------------------------

auto debug_line_pass::shutdown() noexcept -> void
{
    if( !dev_ ) { return; }
    SDL_GPUDevice *d = dev_->raw();
    if( pipeline_ ) { SDL_ReleaseGPUGraphicsPipeline( d, pipeline_ ); pipeline_ = nullptr; }
    if( vert_ ) { SDL_ReleaseGPUShader( d, vert_ ); vert_ = nullptr; }
    if( frag_ ) { SDL_ReleaseGPUShader( d, frag_ ); frag_ = nullptr; }
    if( storage_ ) { SDL_ReleaseGPUBuffer( d, storage_ ); storage_ = nullptr; }
    if( xfer_ ) { SDL_ReleaseGPUTransferBuffer( d, xfer_ ); xfer_ = nullptr; }
    dev_ = nullptr;
    lines_.clear();
}

// ---- Line buffering ------------------------------------------------------

auto debug_line_pass::add_line( float x0, float y0, float x1, float y1,
                                float r, float g, float b, float a ) -> void
{
    if( lines_.size() < static_cast<std::size_t>( MAX_LINES ) ) {
        lines_.push_back( { x0, y0, x1, y1, r, g, b, a } );
    }
}

auto debug_line_pass::add_point( float x, float y,
                                 float r, float g, float b, float a ) -> void
{
    constexpr float arm = 0.15f; // tile units — small cross
    add_line( x - arm, y, x + arm, y, r, g, b, a );
    add_line( x, y - arm, x, y + arm, r, g, b, a );
}

auto debug_line_pass::clear() noexcept -> void
{
    lines_.clear();
}

// ---- Upload instances to GPU ---------------------------------------------

auto debug_line_pass::upload( SDL_GPUCommandBuffer *cb ) -> bool
{
    if( lines_.empty() ) {
        return false;
    }

    const auto count = static_cast<Uint32>(
        std::min( lines_.size(), static_cast<std::size_t>( MAX_LINES ) ) );
    const auto bytes = count * static_cast<Uint32>( sizeof( debug_line_vertex ) );

    void *mapped = SDL_MapGPUTransferBuffer( dev_->raw(), xfer_, true );
    if( !mapped ) {
        return false;
    }
    std::memcpy( mapped, lines_.data(), bytes );
    SDL_UnmapGPUTransferBuffer( dev_->raw(), xfer_ );

    // Copy pass: transfer → storage.
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = xfer_;
    src.offset = 0;
    SDL_GPUBufferRegion dst_region{};
    dst_region.buffer = storage_;
    dst_region.offset = 0;
    dst_region.size = bytes;
    SDL_UploadToGPUBuffer( cp, &src, &dst_region, true );
    SDL_EndGPUCopyPass( cp );
    return true;
}

// ---- Per-frame record ----------------------------------------------------

auto debug_line_pass::record(
    SDL_GPUCommandBuffer *cb, SDL_GPUTexture *target,
    std::uint32_t target_w, std::uint32_t target_h,
    float cam_x, float cam_y,
    float tile_w, float tile_h ) -> void
{
    if( !ready() || !cb || !target || lines_.empty() ) {
        return;
    }

    if( !upload( cb ) ) {
        return;
    }

    const auto count = static_cast<Uint32>(
        std::min( lines_.size(), static_cast<std::size_t>( MAX_LINES ) ) );

    // Begin render pass on world target — LOAD to preserve tiles, STORE to keep.
    SDL_GPUColorTargetInfo ct{};
    ct.texture = target;
    ct.load_op = SDL_GPU_LOADOP_LOAD;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &ct, 1, nullptr );
    if( !rp ) {
        return;
    }

    // Push camera uniform (vertex slot 0).
    struct FrameParams {
        float cam_x, cam_y;
        float tile_w, tile_h;
        float target_w, target_h;
        float pad0, pad1;
    };
    const FrameParams fp {
        .cam_x = cam_x,         .cam_y = cam_y,
        .tile_w = tile_w,       .tile_h = tile_h,
        .target_w = static_cast<float>( target_w ),
        .target_h = static_cast<float>( target_h ),
        .pad0 = 0.f,            .pad1 = 0.f,
    };

    SDL_BindGPUGraphicsPipeline( rp, pipeline_ );
    SDL_PushGPUVertexUniformData( cb, 0, &fp, sizeof( fp ) );

    const SDL_GPUViewport vp { 0.0f, 0.0f,
                               static_cast<float>( target_w ),
                               static_cast<float>( target_h ),
                               0.0f, 1.0f };
    SDL_SetGPUViewport( rp, &vp );

    // Bind instance storage buffer.
    SDL_BindGPUVertexStorageBuffers( rp, 0, &storage_, 1 );

    // Draw: 2 vertices per instance (line list), count instances.
    SDL_DrawGPUPrimitives( rp, 2, count, 0, 0 );
    SDL_EndGPURenderPass( rp );
}

} // namespace lighting
