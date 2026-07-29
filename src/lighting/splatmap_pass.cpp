#include "splatmap_pass.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "debug.h"
#include "fstream_utils.h" // read_from_file_json
#include "json.h"          // JsonIn / JsonObject
#include "lighting/gpu_device.h"
#include "lighting/shader_compiler.h"
#include "path_info.h"
#include "sdl_utils.h"    // create_surface_32
#include "sdl_wrappers.h" // load_image

#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace lighting
{

namespace
{

// ---- Quad instance layout (matches splat_stamp.vert.hlsl SpriteInstance) ----
// Byte-identical to rain_effect's quad_instance so both procedural-quad vertex
// shaders share one wire format.
struct quad_instance {
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
    float light_mul;
    float pad1;
    float pad2;
};
static_assert( sizeof( quad_instance ) == 64,
               "quad_instance must be 64 bytes (wire-stable with vert shader)" );

// Vertex FrameParams push (splat_stamp.vert / rain_droplet.vert layout).
struct frame_params {
    float tw;
    float th;
    std::uint32_t base;
    std::uint32_t pad;
};

// Vertex CompositeParams push (splat_composite.vert).
struct composite_params {
    float rect_x, rect_y, rect_w, rect_h;
    float tw, th;
    float pad0, pad1;
};

// Fragment SplatColors push (splat_composite.frag).
struct splat_colors_push {
    float blood_r, blood_g, blood_b, blood_a;
    float blood_s, wet_s, snow_s, pad;
};

/// Build a graphics pipeline for a procedural quad draw.
auto make_pipeline(
    SDL_GPUDevice *dev, SDL_GPUShader *vert, SDL_GPUShader *frag, SDL_GPUTextureFormat fmt,
    const SDL_GPUColorTargetBlendState &blend ) -> SDL_GPUGraphicsPipeline *  // *NOPAD*
{
    SDL_GPUColorTargetDescription ctd{};
    ctd.format = fmt;
    ctd.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = vert;
    pi.fragment_shader = frag;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pi.target_info.num_color_targets = 1;
    pi.target_info.color_target_descriptions = &ctd;
    pi.target_info.has_depth_stencil_target = false;
    return SDL_CreateGPUGraphicsPipeline( dev, &pi );
}

/// Stamp blend: MAX is load-bearing. The fragment writes 0 into non-target
/// channels, so a MAX blend leaves them untouched — one pipeline serves all
/// three channels with no colour write masks — and overlapping stamps saturate
/// toward 1 (organic buildup) instead of washing out.
auto stamp_blend() -> SDL_GPUColorTargetBlendState
{
    SDL_GPUColorTargetBlendState b{};
    b.enable_blend = true;
    b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.color_blend_op = SDL_GPU_BLENDOP_MAX;
    b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.alpha_blend_op = SDL_GPU_BLENDOP_MAX;
    b.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
                         | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    return b;
}

/// Composite blend: the fragment emits PREMULTIPLIED alpha, so src is ONE
/// (SRC_ALPHA would apply alpha twice → dim). Identical to rain_effect's.
auto composite_blend() -> SDL_GPUColorTargetBlendState
{
    SDL_GPUColorTargetBlendState b{};
    b.enable_blend = true;
    b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.color_blend_op = SDL_GPU_BLENDOP_ADD;
    b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    b.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
                         | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    return b;
}

} // namespace

splatmap_pass::~splatmap_pass() { shutdown(); }

auto splatmap_pass::ready() const noexcept -> bool
{
    return dev_ != nullptr && stamp_pipeline_ != nullptr && composite_pipeline_ != nullptr
           && stamp_xfer_ != nullptr && stamp_storage_ != nullptr && atlas_ != nullptr
           && linear_ != nullptr && ( blood_cells_ + gibs_cells_ ) > 0;
}

auto splatmap_pass::cell_count( bool gibs ) const noexcept -> int
{
    return gibs ? gibs_cells_ : blood_cells_;
}

auto splatmap_pass::cell_base( bool gibs ) const noexcept -> int
{
    return gibs ? blood_cells_ : 0;
}

// ---- Atlas load ---------------------------------------------------------

auto splatmap_pass::load_atlas() -> bool
{
    const std::string dir = PATH_INFO::gfxdir() + "splatmap/";
    const std::string manifest = dir + "stamps.json";

    // Only files listed in the manifest are loaded, so enabling/disabling a
    // stamp is a one-line JSON edit with no rebuild.
    int cell_size = 32;
    std::vector<std::string> blood;
    std::vector<std::string> gibs;
    const bool parsed = read_from_file_json( manifest, [&]( JsonIn & jsin ) {
        JsonObject jo = jsin.get_object();
        jo.allow_omitted_members(); // "//" comments
        jo.read( "cell_size", cell_size );
        if( jo.has_object( "groups" ) ) {
            JsonObject groups = jo.get_object( "groups" );
            groups.allow_omitted_members();
            groups.read( "blood", blood );
            groups.read( "gibs", gibs );
        }
    }, /*optional=*/true );

    if( !parsed ) {
        dbg( DL::Error ) << "splatmap_pass: missing or unreadable " << manifest;
        return false;
    }
    if( cell_size <= 0 ) {
        dbg( DL::Error ) << "splatmap_pass: invalid cell_size " << cell_size;
        return false;
    }
    if( blood.empty() && gibs.empty() ) {
        dbg( DL::Error ) << "splatmap_pass: " << manifest << " lists no stamps";
        return false;
    }

    // Hard cap: an 8x8 atlas of cell_size cells. Overflow is dropped loudly
    // rather than silently growing the atlas.
    const std::size_t total = blood.size() + gibs.size();
    if( total > static_cast<std::size_t>( SPLAT_ATLAS_CELLS ) ) {
        dbg( DL::Error ) << "splatmap_pass: " << total << " stamps exceeds the "
                         << SPLAT_ATLAS_CELLS << "-cell atlas; dropping the overflow";
        if( blood.size() > static_cast<std::size_t>( SPLAT_ATLAS_CELLS ) ) {
            blood.resize( SPLAT_ATLAS_CELLS );
            gibs.clear();
        } else {
            gibs.resize( SPLAT_ATLAS_CELLS - blood.size() );
        }
    }

    const int dim = SPLAT_ATLAS_COLS * cell_size;
    atlas_dim_ = dim;
    SDL_Surface_Ptr sheet = create_surface_32( dim, dim );
    if( !sheet ) {
        dbg( DL::Error ) << "splatmap_pass: atlas surface alloc failed";
        return false;
    }
    // Transparent black everywhere a cell is missing, so an unloadable stamp
    // renders as nothing rather than as garbage.
    SDL_FillSurfaceRect( sheet.get(), nullptr,
                         SDL_MapRGBA( SDL_GetPixelFormatDetails( sheet->format ), nullptr,
                                      0, 0, 0, 0 ) );

    // Blood cells occupy [0, n_blood); gibs [n_blood, n_blood + n_gibs) — that
    // is exactly what cell_base()/cell_count() report.
    int placed = 0;
    const auto blit_group = [&]( const std::vector<std::string> &names ) -> int {
        int n = 0;
        for( const std::string &name : names ) {
            SDL_Surface_Ptr img = load_image( ( dir + "stamps/" + name ).c_str() );
            if( !img ) {
                dbg( DL::Error ) << "splatmap_pass: failed to load stamp " << name;
                continue;
            }
            SDL_Rect dst{ ( placed % SPLAT_ATLAS_COLS ) * cell_size,
                          ( placed / SPLAT_ATLAS_COLS ) * cell_size, cell_size, cell_size };
            if( !SDL_BlitSurfaceScaled( img.get(), nullptr, sheet.get(), &dst,
                                        SDL_SCALEMODE_NEAREST ) ) {
                dbg( DL::Error ) << "splatmap_pass: blit " << name << ": " << SDL_GetError();
                continue;
            }
            ++placed;
            ++n;
        }
        return n;
    };
    blood_cells_ = blit_group( blood );
    gibs_cells_ = blit_group( gibs );
    if( blood_cells_ + gibs_cells_ == 0 ) {
        dbg( DL::Error ) << "splatmap_pass: no stamp images loaded";
        return false;
    }

    // ---- Upload the assembled sheet -------------------------------------
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = static_cast<std::uint32_t>( dim );
    tci.height = static_cast<std::uint32_t>( dim );
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    atlas_ = SDL_CreateGPUTexture( dev_->raw(), &tci );
    if( !atlas_ ) {
        dbg( DL::Error ) << "splatmap_pass: atlas texture: " << SDL_GetError();
        return false;
    }

    SDL_Surface *src = sheet.get();
    SDL_Surface *converted = nullptr;
    if( src->format != SDL_PIXELFORMAT_RGBA32 ) {
        converted = SDL_ConvertSurface( src, SDL_PIXELFORMAT_RGBA32 );
        if( !converted ) {
            dbg( DL::Error ) << "splatmap_pass: atlas convert: " << SDL_GetError();
            return false;
        }
        src = converted;
    }

    const std::uint32_t row_bytes = static_cast<std::uint32_t>( dim ) * 4u;
    const std::uint32_t bytes = row_bytes * static_cast<std::uint32_t>( dim );
    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = bytes;
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer( dev_->raw(), &tbi );
    bool ok = false;
    if( xfer ) {
        if( void *mapped = SDL_MapGPUTransferBuffer( dev_->raw(), xfer, false ) ) {
            auto *out = static_cast<std::uint8_t *>( mapped );
            const auto *in = static_cast<const std::uint8_t *>( src->pixels );
            for( int y = 0; y < dim; ++y ) {
                std::memcpy( out + static_cast<std::size_t>( y ) * row_bytes,
                             in + static_cast<std::size_t>( y ) * src->pitch, row_bytes );
            }
            SDL_UnmapGPUTransferBuffer( dev_->raw(), xfer );

            if( SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer( dev_->raw() ) ) {
                if( SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb ) ) {
                    SDL_GPUTextureTransferInfo ti{};
                    ti.transfer_buffer = xfer;
                    ti.offset = 0;
                    ti.pixels_per_row = static_cast<std::uint32_t>( dim );
                    ti.rows_per_layer = static_cast<std::uint32_t>( dim );
                    SDL_GPUTextureRegion reg{};
                    reg.texture = atlas_;
                    reg.w = static_cast<std::uint32_t>( dim );
                    reg.h = static_cast<std::uint32_t>( dim );
                    reg.d = 1;
                    SDL_UploadToGPUTexture( cp, &ti, &reg, false );
                    SDL_EndGPUCopyPass( cp );
                    ok = true;
                }
                SDL_SubmitGPUCommandBuffer( cb );
            }
        }
        SDL_ReleaseGPUTransferBuffer( dev_->raw(), xfer );
    }
    if( converted ) {
        SDL_DestroySurface( converted );
    }
    if( !ok ) {
        dbg( DL::Error ) << "splatmap_pass: atlas upload failed";
        return false;
    }

    DebugLogFL( DL::Info, DC::Main )
            << "splatmap_pass: atlas loaded (" << blood_cells_ << " blood + " << gibs_cells_
            << " gibs cells, " << dim << "x" << dim << ")";
    return true;
}

// ---- Init / Shutdown ----------------------------------------------------

auto splatmap_pass::init( gpu_device &dev, SDL_GPUTextureFormat world_format ) -> bool
{
    shutdown();
    dev_ = &dev;
    world_format_ = world_format;

    if( !dev.ready() ) {
        dbg( DL::Error ) << "splatmap_pass::init: gpu_device not ready";
        return false;
    }
    init_shader_compiler();

    // Atlas first: an empty or missing manifest means the whole feature no-ops,
    // and there is no point compiling shaders for it.
    if( !load_atlas() ) {
        return false;
    }

    // One LINEAR + CLAMP_TO_EDGE sampler, owned by the pass. The composite uses
    // it so 8 px/tile does not read as hard blocks; the stamp pass reuses it for
    // the atlas read (stamp UVs are inset by half a texel so LINEAR cannot bleed
    // in a neighbouring cell).
    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    linear_ = SDL_CreateGPUSampler( dev.raw(), &sci );
    if( !linear_ ) {
        dbg( DL::Error ) << "splatmap_pass: sampler create failed: " << SDL_GetError();
        return false;
    }

    // ---- Shaders ---------------------------------------------------------
    const std::string sv = load_lighting_shader_source( "splat_stamp.vert.hlsl" );
    const std::string sf = load_lighting_shader_source( "splat_stamp.frag.hlsl" );
    const std::string cv = load_lighting_shader_source( "splat_composite.vert.hlsl" );
    const std::string cf = load_lighting_shader_source( "splat_composite.frag.hlsl" );
    if( sv.empty() || sf.empty() || cv.empty() || cf.empty() ) {
        dbg( DL::Error ) << "splatmap_pass: failed to load splatmap shader source";
        return false;
    }

    auto c_sv = compile_graphics_shader(
                    dev, sv, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "splat_stamp.vert" );
    auto c_sf = compile_graphics_shader(
                    dev, sf, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "splat_stamp.frag" );
    auto c_cv = compile_graphics_shader(
                    dev, cv, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "splat_composite.vert" );
    auto c_cf = compile_graphics_shader(
                    dev, cf, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "splat_composite.frag" );
    if( !c_sv || !c_sf || !c_cv || !c_cf ) {
        dbg( DL::Error ) << "splatmap_pass: shader compile failed";
        return false;
    }
    stamp_vert_ = c_sv.shader;
    stamp_frag_ = c_sf.shader;
    composite_vert_ = c_cv.shader;
    composite_frag_ = c_cf.shader;

    stamp_pipeline_ = make_pipeline( dev.raw(), stamp_vert_, stamp_frag_,
                                     SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, stamp_blend() );
    composite_pipeline_ = make_pipeline( dev.raw(), composite_vert_, composite_frag_,
                                        world_format_, composite_blend() );
    if( !stamp_pipeline_ || !composite_pipeline_ ) {
        dbg( DL::Error ) << "splatmap_pass: pipeline create failed";
        return false;
    }

    // ---- Persistent instance buffers (transfer + storage) ----------------
    const std::uint32_t inst_bytes =
        static_cast<std::uint32_t>( MAX_STAMPS_PER_FLUSH * sizeof( quad_instance ) );
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bci.size = inst_bytes;
    stamp_storage_ = SDL_CreateGPUBuffer( dev.raw(), &bci );

    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = inst_bytes;
    stamp_xfer_ = SDL_CreateGPUTransferBuffer( dev.raw(), &tbi );
    if( !stamp_storage_ || !stamp_xfer_ ) {
        dbg( DL::Error ) << "splatmap_pass: instance buffer create failed";
        return false;
    }

    DebugLogFL( DL::Info, DC::Main )
            << "splatmap_pass: initialised (" << SPLAT_TEX_DIM << "x" << SPLAT_TEX_DIM
            << " per submap, cache=" << MAX_SUBMAPS << ")";
    return true;
}

auto splatmap_pass::shutdown() noexcept -> void
{
    // Release the per-submap targets while the device is still live.
    index_.clear();
    cache_.clear();

    if( dev_ && dev_->ready() ) {
        SDL_ReleaseGPUGraphicsPipeline( dev_->raw(), stamp_pipeline_ );
        SDL_ReleaseGPUGraphicsPipeline( dev_->raw(), composite_pipeline_ );
        SDL_ReleaseGPUShader( dev_->raw(), stamp_vert_ );
        SDL_ReleaseGPUShader( dev_->raw(), stamp_frag_ );
        SDL_ReleaseGPUShader( dev_->raw(), composite_vert_ );
        SDL_ReleaseGPUShader( dev_->raw(), composite_frag_ );
        SDL_ReleaseGPUBuffer( dev_->raw(), stamp_storage_ );
        SDL_ReleaseGPUTransferBuffer( dev_->raw(), stamp_xfer_ );
        if( atlas_ ) {
            SDL_ReleaseGPUTexture( dev_->raw(), atlas_ );
        }
        if( linear_ ) {
            SDL_ReleaseGPUSampler( dev_->raw(), linear_ );
        }
    }

    stamp_pipeline_ = nullptr;
    composite_pipeline_ = nullptr;
    stamp_vert_ = nullptr;
    stamp_frag_ = nullptr;
    composite_vert_ = nullptr;
    composite_frag_ = nullptr;
    stamp_storage_ = nullptr;
    stamp_xfer_ = nullptr;
    atlas_ = nullptr;
    linear_ = nullptr;
    atlas_dim_ = 0;
    blood_cells_ = 0;
    gibs_cells_ = 0;
    dev_ = nullptr;
}

// ---- Submap cache -------------------------------------------------------

auto splatmap_pass::find( std::uint64_t key ) -> entry *
{
    const auto it = index_.find( key );
    if( it == index_.end() ) {
        return nullptr;
    }
    // Touch: relink to the front. splice moves list NODES, so the (non-movable)
    // ui_composite_target inside is never copied and iterators stay valid.
    cache_.splice( cache_.begin(), cache_, it->second );
    return &*it->second;
}

auto splatmap_pass::touch( std::uint64_t key ) -> bool
{
    if( !ready() ) {
        return true; // pretend "already present" so callers skip seeding
    }
    if( find( key ) != nullptr ) {
        return true;
    }

    if( cache_.size() >= MAX_SUBMAPS ) {
        // Evict the least-recently-used entry. Its stamps go with it; the submap
        // re-seeds from field data the next time it becomes visible.
        index_.erase( cache_.back().key );
        cache_.pop_back();
    }

    cache_.emplace_front();
    entry &e = cache_.front();
    e.key = key;
    if( !e.tex.init( *dev_, SPLAT_TEX_DIM, SPLAT_TEX_DIM, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                     0 ) ) {
        dbg( DL::Error ) << "splatmap_pass: submap target init failed";
        cache_.pop_front();
        return true; // no entry → caller must not bother seeding
    }
    index_.emplace( key, cache_.begin() );
    return false;
}

auto splatmap_pass::add_stamp( std::uint64_t key, const splat_stamp &s ) -> void
{
    if( !ready() ) {
        return;
    }
    entry *e = find( key );
    if( !e ) {
        return;
    }
    // Bound the backlog: a huge unflushed queue would only be drawn frames later
    // anyway, and MAX_STAMPS_PER_FLUSH is already a generous per-frame budget.
    if( e->pending.size() >= MAX_STAMPS_PER_FLUSH * 4 ) {
        return;
    }
    e->pending.push_back( s );
}

// ---- Stamp rendering ----------------------------------------------------

auto splatmap_pass::flush_stamps( SDL_GPUCommandBuffer *cb ) -> void
{
    if( !ready() || !cb ) {
        return;
    }

    // Half-texel inset so the LINEAR atlas read cannot bleed a neighbouring
    // cell in at the cell border.
    const float cell_uv = 1.0f / static_cast<float>( SPLAT_ATLAS_COLS );
    const float inset = 0.5f / static_cast<float>( std::max( 1, atlas_dim_ ) );

    std::vector<quad_instance> insts;
    for( entry &e : cache_ ) {
        if( e.pending.empty() || !e.tex.texture() ) {
            continue;
        }
        const std::size_t n = std::min( e.pending.size(), MAX_STAMPS_PER_FLUSH );
        insts.clear();
        insts.reserve( n );
        for( std::size_t i = 0; i < n; ++i ) {
            const splat_stamp &s = e.pending[i];
            const int cell = std::clamp( s.cell, 0, SPLAT_ATLAS_CELLS - 1 );
            quad_instance qi{};
            qi.dst_x = s.px - s.size * 0.5f;
            qi.dst_y = s.py - s.size * 0.5f;
            qi.dst_w = s.size;
            qi.dst_h = s.size;
            qi.src_u = static_cast<float>( cell % SPLAT_ATLAS_COLS ) * cell_uv + inset;
            qi.src_v = static_cast<float>( cell / SPLAT_ATLAS_COLS ) * cell_uv + inset;
            qi.src_uw = cell_uv - 2.0f * inset;
            qi.src_vh = cell_uv - 2.0f * inset;
            qi.tint_r = s.mask_r;
            qi.tint_g = s.mask_g;
            qi.tint_b = s.mask_b;
            qi.tint_a = s.alpha;
            qi.rotation = s.rotation;
            insts.push_back( qi );
        }

        // ---- Upload this entry's instances (copy pass, no render pass open) --
        const std::uint32_t bytes =
            static_cast<std::uint32_t>( insts.size() * sizeof( quad_instance ) );
        void *mapped = SDL_MapGPUTransferBuffer( dev_->raw(), stamp_xfer_, /*cycle=*/true );
        if( !mapped ) {
            dbg( DL::Error ) << "splatmap_pass: MapGPUTransferBuffer failed: " << SDL_GetError();
            return;
        }
        std::memcpy( mapped, insts.data(), bytes );
        SDL_UnmapGPUTransferBuffer( dev_->raw(), stamp_xfer_ );

        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
        if( !cp ) {
            dbg( DL::Error ) << "splatmap_pass: stamp copy pass begin failed";
            return;
        }
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = stamp_xfer_;
        src.offset = 0;
        SDL_GPUBufferRegion dst{};
        dst.buffer = stamp_storage_;
        dst.offset = 0;
        dst.size = bytes;
        SDL_UploadToGPUBuffer( cp, &src, &dst, /*cycle=*/true );
        SDL_EndGPUCopyPass( cp );

        // ---- Render into this submap's persistent texture ------------------
        // First pass on a fresh texture must CLEAR (reading an uninitialised
        // colour target is undefined); every later pass LOADs so decals persist.
        SDL_GPUColorTargetInfo ct{};
        ct.texture = e.tex.texture();
        ct.load_op = e.cleared ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        ct.clear_color = SDL_FColor{ 0.f, 0.f, 0.f, 0.f };
        ct.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &ct, 1, nullptr );
        if( !rp ) {
            dbg( DL::Error ) << "splatmap_pass: stamp render pass begin failed";
            return;
        }
        const frame_params fp{ static_cast<float>( SPLAT_TEX_DIM ),
                               static_cast<float>( SPLAT_TEX_DIM ), 0u, 0u };
        SDL_PushGPUVertexUniformData( cb, 0, &fp, sizeof( fp ) );

        SDL_GPUTextureSamplerBinding tsb{};
        tsb.texture = atlas_;
        tsb.sampler = linear_;
        SDL_BindGPUGraphicsPipeline( rp, stamp_pipeline_ );
        SDL_BindGPUVertexStorageBuffers( rp, 0, &stamp_storage_, 1 );
        SDL_BindGPUFragmentSamplers( rp, 0, &tsb, 1 );
        SDL_DrawGPUPrimitives( rp, 6, static_cast<Uint32>( insts.size() ), 0, 0 );
        SDL_EndGPURenderPass( rp );

        e.cleared = true;
        // Overflow (n < pending.size()) stays queued for the next frame.
        e.pending.erase( e.pending.begin(),
                         e.pending.begin() + static_cast<std::ptrdiff_t>( n ) );
    }
}

// ---- Composite ----------------------------------------------------------

auto splatmap_pass::composite(
    SDL_GPUCommandBuffer *cb, SDL_GPUTexture *world_tex, std::uint32_t proj_w,
    std::uint32_t proj_h, const std::vector<splat_quad> &quads, const splat_colors &colors ) -> void
{
    if( !ready() || !cb || !world_tex || quads.empty() || proj_w == 0 || proj_h == 0 ) {
        return;
    }

    SDL_GPUColorTargetInfo ct{};
    ct.texture = world_tex;
    ct.load_op = SDL_GPU_LOADOP_LOAD; // preserve the terrain half of Pass W
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &ct, 1, nullptr );
    if( !rp ) {
        dbg( DL::Error ) << "splatmap_pass: composite render pass begin failed";
        return;
    }
    SDL_BindGPUGraphicsPipeline( rp, composite_pipeline_ );

    const splat_colors_push cp_frag{ colors.blood_r, colors.blood_g, colors.blood_b, 1.0f,
                                     colors.blood_strength, colors.wet_strength,
                                     colors.snow_strength, 0.0f };
    SDL_PushGPUFragmentUniformData( cb, 0, &cp_frag, sizeof( cp_frag ) );

    for( const splat_quad &q : quads ) {
        // Do NOT touch() here: the composite must not reorder the LRU (that is
        // the visibility pass's job) and must skip submaps with no decal texture.
        const auto it = index_.find( q.key );
        if( it == index_.end() || !it->second->cleared || !it->second->tex.texture() ) {
            continue;
        }
        SDL_GPUTextureSamplerBinding tsb{};
        tsb.texture = it->second->tex.texture();
        tsb.sampler = linear_;
        SDL_BindGPUFragmentSamplers( rp, 0, &tsb, 1 );

        const composite_params cpv{ q.dst_x, q.dst_y, q.dst_w, q.dst_h,
                                    static_cast<float>( proj_w ), static_cast<float>( proj_h ),
                                    0.f, 0.f };
        SDL_PushGPUVertexUniformData( cb, 0, &cpv, sizeof( cpv ) );
        SDL_DrawGPUPrimitives( rp, 6, 1, 0, 0 );
    }
    SDL_EndGPURenderPass( rp );
}

} // namespace lighting
