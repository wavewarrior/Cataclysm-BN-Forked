#include "rmlui_render_interface.h"

#include "debug.h"
#include "gpu_device.h"
#include "rmlui_proc_texture.h"
#include "shader_compiler.h"

#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Variant.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Lighting/ files must define dbg themselves (not globally available).
#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

namespace {
// Keep released GPU resources alive this many frames before freeing, so the
// GPU is guaranteed done reading buffers/textures still in flight.
constexpr std::uint64_t KEEP_FRAMES = 4;
// A cropped-sprite source (see LoadTexture "?sprite:") keeps its decoded sheet alive this
// many frames after the last crop. Long enough to cover a burst of crops from one sheet,
// short enough that leaving the screen gives the memory back.
constexpr std::uint64_t SHEET_KEEP_FRAMES = 240;

// Staging ring length for upload_pending. The device allows 2 frames in flight,
// so a slot written now was last touched 3 frames ago and is provably retired.
constexpr std::uint32_t UPLOAD_RING_SLOTS = 3;

// Smallest GPU-buffer pool bucket. Buckets are powers of two, so a handful of
// them covers every RmlUi batch size while letting one buffer be reused by any
// batch that rounds to the same bucket.
constexpr std::uint32_t POOL_MIN_BYTES = 256;

// Alignment of each region inside the shared staging buffer. Buffer-to-buffer
// copies have no documented alignment requirement on D3D12/Vulkan, but Metal's
// blit encoder needs a 4-byte-aligned source offset; 16 is free here and safe
// everywhere.
constexpr std::uint32_t STAGE_ALIGN = 16;

auto pool_bucket(std::uint32_t bytes) -> std::uint32_t {
    return std::bit_ceil(std::max(bytes, POOL_MIN_BYTES));
}

auto align_up(std::uint32_t v, std::uint32_t a) -> std::uint32_t {
    return (v + a - 1) & ~(a - 1);
}

// Per-RenderGeometry vertex uniform (register b0, space1 in rmlui.vert.hlsl).
// 80 bytes: 16-byte translation/viewport + 64-byte transform matrix.
struct vert_params {
    float translation_x;
    float translation_y;
    float viewport_w;
    float viewport_h;
    float transform[16]; // column-major 4x4; identity when no CSS transform
};

// Gradient fragment uniform (register b0, space3 in rmlui_gradient.frag.hlsl).
// Doubles as the CompiledShaderHandle backing store (heap-allocated in
// CompileShader, pushed verbatim in RenderShader). Layout MUST match the HLSL
// cbuffer's 16-byte packing exactly — see the static_assert.
constexpr int RMLUI_MAX_STOPS = 16;
struct grad_params {
    std::int32_t func = 0; // 0 lin,1 rad,2 conic,3 rep-lin,4 rep-rad,5 rep-conic
    std::int32_t num_stops = 0;
    float p0 = 0.f, p1 = 0.f; // g_p
    float v0 = 0.f, v1 = 0.f; // g_v
    float pad0 = 0.f, pad1 = 0.f;
    float colors[RMLUI_MAX_STOPS * 4] = {}; // float4[16], premultiplied 0..1
    float positions[RMLUI_MAX_STOPS] = {};  // packed into float4[4] in the shader
};
static_assert(sizeof(grad_params) == 352, "grad_params must match the HLSL cbuffer layout");
} // namespace

struct rmlui_render_interface::impl {
    gpu_device* dev = nullptr;
    SDL_GPUDevice* raw = nullptr;

    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUShader* vert = nullptr;
    SDL_GPUShader* frag = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    SDL_GPUTexture* white_tex = nullptr;

    // Gradient decorator pipeline: same vertex shader + layout, gradient frag.
    SDL_GPUGraphicsPipeline* grad_pipeline = nullptr;
    SDL_GPUShader* grad_frag = nullptr;

    // CPU-side compiled geometry; GPU buffers filled lazily in upload_pending.
    struct geom {
        std::vector<Rml::Vertex> verts;
        std::vector<int> idx;
        SDL_GPUBuffer* vbuf = nullptr;
        SDL_GPUBuffer* ibuf = nullptr;
        // Pool bucket each buffer came from, so release returns it to the right
        // free-list (pooled buffers are >= the batch size, not equal to it).
        std::uint32_t vbuf_cap = 0;
        std::uint32_t ibuf_cap = 0;
        std::uint32_t idx_count = 0;
        bool uploaded = false;
    };
    std::unordered_map<std::uint64_t, geom> geoms;
    std::vector<std::uint64_t> pending; // compiled, awaiting GPU upload
    std::uint64_t next_geom = 1;        // 1-based: 0 is RmlUi's "invalid"

    // Recycled geometry buffers, keyed by pool_bucket() capacity. RmlUi releases
    // and recompiles geometry HANDLES every frame, so a buffer cached on `geom`
    // dies with its handle — the pool is what survives that churn. MEASURED
    // 2026-08-02: creating buffers per batch cost 109 GPU resource creations per
    // frame (~0.1 ms each on D3D12), the bulk of a 4.9 -> 12.8 ms prepare().
    std::unordered_map<std::uint32_t, std::vector<SDL_GPUBuffer*>> vbuf_pool;
    std::unordered_map<std::uint32_t, std::vector<SDL_GPUBuffer*>> ibuf_pool;

    // Persistent staging ring: one mapped transfer buffer per frame instead of
    // two freshly created ones per batch (was 54 creations/frame for ~6 kB).
    SDL_GPUTransferBuffer* stage[UPLOAD_RING_SLOTS] = {};
    std::uint32_t stage_cap[UPLOAD_RING_SLOTS] = {};
    std::uint32_t stage_slot = 0;

    // Scratch for upload_pending: this frame's copy-pass regions. A member so
    // its capacity persists across frames (no per-frame heap traffic).
    struct upload_region {
        SDL_GPUBuffer* dst;
        std::uint32_t src_offset;
        std::uint32_t size;
    };
    std::vector<upload_region> upload_regions;

    std::unordered_map<std::uint64_t, SDL_GPUTexture*> textures;
    std::uint64_t next_tex = 1;

    // Borrowed textures: handles whose SDL_GPUTexture is owned elsewhere (currently
    // render_state's character-portrait target). ReleaseTexture drops the mapping but
    // must NOT free the texture — see ReleaseTexture.
    std::unordered_set<std::uint64_t> borrowed;
    // Resolves the borrowed texture on demand. A callback rather than a stored pointer
    // because the target can be reallocated (window resize) and a cached pointer would
    // dangle; the "?avatar:<generation>" source busts RmlUi's cache so this is re-run.
    std::function<SDL_GPUTexture*()> borrowed_lookup;
    int borrowed_w = 0;
    int borrowed_h = 0;

    // Most recently decoded tileset sheet, for the "?sprite:" crop source. Tileset sheets
    // are multi-megabyte PNGs and a UI that browses locations asks for a different sprite
    // out of the SAME sheet over and over — without this, each distinct location paid a
    // full sheet decode and the hitch was visible while moving the cursor.
    //
    // Exactly one entry: consecutive requests almost always share a sheet, and holding
    // more would pin tens of megabytes to save a decode that is already rare.
    std::string sheet_path;
    SDL_Surface* sheet_rgba = nullptr;
    std::uint64_t sheet_frame = 0;   //< frame of the last crop, for the idle release

    // Deferred frees (run KEEP_FRAMES after the freeing frame).
    std::uint64_t frame = 0;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> deferred;

    // Active render-pass state (set by begin_render_pass).
    SDL_GPURenderPass* rp = nullptr;
    SDL_GPUCommandBuffer* cb = nullptr;
    std::uint32_t target_w = 0, target_h = 0;
    std::uint32_t proj_w = 0, proj_h = 0;

    bool scissor_enabled = false;
    Rml::Rectanglei scissor{};

    // CSS transform (Phase 1). Set by SetTransform, applied in RenderGeometry/RenderShader.
    std::optional<Rml::Matrix4f> active_transform;

    std::uint32_t compiles_in_pass = 0;
    std::uint32_t textures_in_pass = 0;

    // Create an RGBA8 SAMPLER texture and upload `pixels` on a dedicated command
    // buffer submitted immediately (safe outside any open render pass).
    SDL_GPUTexture* upload_rgba(const std::uint8_t* pixels, int w, int h) {
        if (!raw || w <= 0 || h <= 0) { return nullptr; }
        SDL_GPUTextureCreateInfo tci{};
        tci.type = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        tci.width = static_cast<std::uint32_t>(w);
        tci.height = static_cast<std::uint32_t>(h);
        tci.layer_count_or_depth = 1;
        tci.num_levels = 1;
        tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture* tex = SDL_CreateGPUTexture(raw, &tci);
        if (!tex) {
            dbg(DL::Error) << "rmlui: CreateGPUTexture failed: " << SDL_GetError();
            return nullptr;
        }
        const std::uint32_t bytes =
            static_cast<std::uint32_t>(w) * static_cast<std::uint32_t>(h) * 4u;
        SDL_GPUTransferBufferCreateInfo tbi{};
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbi.size = bytes;
        SDL_GPUTransferBuffer* xfer = SDL_CreateGPUTransferBuffer(raw, &tbi);
        if (!xfer) {
            SDL_ReleaseGPUTexture(raw, tex);
            return nullptr;
        }
        void* mapped = SDL_MapGPUTransferBuffer(raw, xfer, false);
        if (!mapped) {
            SDL_ReleaseGPUTransferBuffer(raw, xfer);
            SDL_ReleaseGPUTexture(raw, tex);
            return nullptr;
        }
        std::memcpy(mapped, pixels, bytes);
        SDL_UnmapGPUTransferBuffer(raw, xfer);

        SDL_GPUCommandBuffer* ucb = SDL_AcquireGPUCommandBuffer(raw);
        if (!ucb) {
            SDL_ReleaseGPUTransferBuffer(raw, xfer);
            SDL_ReleaseGPUTexture(raw, tex);
            return nullptr;
        }
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(ucb);
        SDL_GPUTextureTransferInfo ti{};
        ti.transfer_buffer = xfer;
        ti.offset = 0;
        ti.pixels_per_row = static_cast<std::uint32_t>(w);
        ti.rows_per_layer = static_cast<std::uint32_t>(h);
        SDL_GPUTextureRegion region{};
        region.texture = tex;
        region.w = static_cast<std::uint32_t>(w);
        region.h = static_cast<std::uint32_t>(h);
        region.d = 1;
        SDL_UploadToGPUTexture(cp, &ti, &region, false);
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(ucb);
        SDL_ReleaseGPUTransferBuffer(raw, xfer);
        return tex;
    }

    // Take a buffer of at least `bytes` from `pool`, creating one only when the
    // matching bucket is empty. `out_cap` receives the bucket capacity so the
    // buffer can be returned to the same bucket on release.
    auto acquire_buffer(
        std::unordered_map<std::uint32_t, std::vector<SDL_GPUBuffer*>>& pool,
        SDL_GPUBufferUsageFlags usage, std::uint32_t bytes,
        std::uint32_t& out_cap) -> SDL_GPUBuffer* { // *NOPAD*
        const std::uint32_t cap = pool_bucket(bytes);
        out_cap = cap;
        auto it = pool.find(cap);
        if (it != pool.end() && !it->second.empty()) {
            SDL_GPUBuffer* buf = it->second.back();
            it->second.pop_back();
            return buf;
        }
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = usage;
        bci.size = cap;
        return SDL_CreateGPUBuffer(raw, &bci);
    }

    // The current ring slot's staging buffer, grown only when this frame needs
    // more than it already holds. Steady state creates nothing.
    auto ensure_stage(std::uint32_t bytes) -> SDL_GPUTransferBuffer* { // *NOPAD*
        const std::uint32_t slot = stage_slot;
        if (stage[slot] != nullptr && stage_cap[slot] >= bytes) { return stage[slot]; }
        const std::uint32_t cap = pool_bucket(bytes);
        SDL_GPUTransferBufferCreateInfo tci{};
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size = cap;
        SDL_GPUTransferBuffer* grown = SDL_CreateGPUTransferBuffer(raw, &tci);
        if (!grown) { return nullptr; }
        if (stage[slot] != nullptr) {
            // The outgoing buffer may still be referenced by an in-flight submit,
            // so free it on the same KEEP_FRAMES delay as every other resource.
            SDL_GPUDevice* r = raw;
            SDL_GPUTransferBuffer* old = stage[slot];
            deferred.emplace_back(frame, [r, old]() { SDL_ReleaseGPUTransferBuffer(r, old); });
        }
        stage[slot] = grown;
        stage_cap[slot] = cap;
        return grown;
    }
};

rmlui_render_interface::rmlui_render_interface(): p(std::make_unique<impl>()) {}
rmlui_render_interface::~rmlui_render_interface() = default;

bool rmlui_render_interface::init(gpu_device& dev) {
    if (!dev.ready()) { return false; }
    p->dev = &dev;
    p->raw = dev.raw();

    init_shader_compiler();
    const std::string vsrc = load_lighting_shader_source("rmlui.vert.hlsl");
    const std::string fsrc = load_lighting_shader_source("rmlui.frag.hlsl");
    compiled_shader v = compile_graphics_shader(
        dev, vsrc, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "rmlui.vert");
    if (!v) {
        dbg(DL::Error) << "rmlui: vert shader compile failed";
        return false;
    }
    compiled_shader f = compile_graphics_shader(
        dev, fsrc, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "rmlui.frag");
    if (!f) {
        SDL_ReleaseGPUShader(p->raw, v.shader);
        dbg(DL::Error) << "rmlui: frag shader compile failed";
        return false;
    }
    p->vert = v.shader;
    p->frag = f.shader;
    // Gate log: expect samplers=1, storage_buffers=0 (low-risk reflection path).
    DebugLogFL(DL::Info, DC::Main)
        << "rmlui.frag reflection: samplers=" << f.resources.num_samplers
        << " storage_textures=" << f.resources.num_storage_textures << " storage_buffers="
        << f.resources.num_storage_buffers << " uniform_buffers=" << f.resources.num_uniform_buffers
        << " (expect samplers=1 st=0 sb=0)";

    // Vertex layout = Rml::Vertex { Vector2f position; ColourbPremultiplied
    // colour; Vector2f tex_coord }.
    SDL_GPUVertexBufferDescription vbd{};
    vbd.slot = 0;
    vbd.pitch = static_cast<std::uint32_t>(sizeof(Rml::Vertex));
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vbd.instance_step_rate = 0;
    SDL_GPUVertexAttribute attrs[3]{};
    attrs[0].location = 0;
    attrs[0].buffer_slot = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[0].offset = static_cast<std::uint32_t>(offsetof(Rml::Vertex, position));
    attrs[1].location = 1;
    attrs[1].buffer_slot = 0;
    attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    attrs[1].offset = static_cast<std::uint32_t>(offsetof(Rml::Vertex, colour));
    attrs[2].location = 2;
    attrs[2].buffer_slot = 0;
    attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[2].offset = static_cast<std::uint32_t>(offsetof(Rml::Vertex, tex_coord));

    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; // premultiplied alpha
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.color_write_mask =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B
        | SDL_GPU_COLORCOMPONENT_A;

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
    p->pipeline = SDL_CreateGPUGraphicsPipeline(p->raw, &pci);
    if (!p->pipeline) {
        dbg(DL::Error) << "rmlui: pipeline create failed: " << SDL_GetError();
        return false;
    }

    // Gradient pipeline: identical vertex layout/blend/target, gradient fragment
    // shader (enables RmlUi gradient decorators via CompileShader/RenderShader).
    // NON-FATAL: if this fails, gradient decorators just no-op (RenderShader guards
    // on grad_pipeline); the rest of the UI still renders, so don't abort init.
    const std::string gsrc = load_lighting_shader_source("rmlui_gradient.frag.hlsl");
    compiled_shader gf = compile_graphics_shader(
        dev, gsrc, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "rmlui_gradient.frag");
    if (!gf) {
        dbg(DL::Error) << "rmlui: gradient frag compile failed (gradients disabled)";
    } else {
        p->grad_frag = gf.shader;
        DebugLogFL(DL::Info, DC::Main)
            << "rmlui_gradient.frag reflection: samplers=" << gf.resources.num_samplers
            << " storage_buffers=" << gf.resources.num_storage_buffers
            << " (expect samplers=0 sb=0; 1 uniform buffer at b0/space3)";
        SDL_GPUGraphicsPipelineCreateInfo gpci = pci; // same layout/blend/target
        gpci.fragment_shader = p->grad_frag;
        p->grad_pipeline = SDL_CreateGPUGraphicsPipeline(p->raw, &gpci);
        if (!p->grad_pipeline) {
            dbg(DL::Error) << "rmlui: gradient pipeline create failed (gradients disabled): "
                           << SDL_GetError();
        } else {
            DebugLogFL(DL::Info, DC::Main) << "rmlui: gradient pipeline ready";
        }
    }

    // Linear sampler, REPEAT wrap on U/V. Repeat is required for RmlUi's tiled
    // `repeat` image decorator (it emits UVs > 1 and relies on sampler wrap to
    // tile, e.g. the CRT scanline overlay). Safe for everything else: glyph-atlas
    // sub-rects and `fill`/`scale-none` decorators keep UVs within [0,1], where
    // REPEAT and CLAMP are identical.
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    p->sampler = SDL_CreateGPUSampler(p->raw, &si);
    if (!p->sampler) {
        dbg(DL::Error) << "rmlui: sampler create failed: " << SDL_GetError();
        return false;
    }

    // 1x1 opaque white — bound for untextured geometry (TextureHandle 0) so the
    // fragment shader's texel*colour degenerates to the flat vertex colour.
    const std::uint8_t white[4] = {255, 255, 255, 255};
    p->white_tex = p->upload_rgba(white, 1, 1);
    if (!p->white_tex) {
        dbg(DL::Error) << "rmlui: white texture create failed";
        return false;
    }

    dbg(DL::Info) << "rmlui_render_interface: init ok";
    return true;
}

void rmlui_render_interface::shutdown() {
    // Not gated on p->raw: the cached sheet is a CPU surface and must be released even when
    // the GPU device is already gone.
    if (p->sheet_rgba != nullptr) {
        SDL_DestroySurface(p->sheet_rgba);
        p->sheet_rgba = nullptr;
        p->sheet_path.clear();
    }
    if (!p->raw) { return; }
    // Run all deferred frees regardless of frame age.
    for (auto& d : p->deferred) { d.second(); }
    p->deferred.clear();
    for (auto& kv : p->geoms) {
        if (kv.second.vbuf) { SDL_ReleaseGPUBuffer(p->raw, kv.second.vbuf); }
        if (kv.second.ibuf) { SDL_ReleaseGPUBuffer(p->raw, kv.second.ibuf); }
    }
    p->geoms.clear();
    p->pending.clear();
    // Pooled buffers are not in `geoms` — the deferred frees above are what push
    // released buffers into the pools, so drain the pools after running them.
    for (auto& kv : p->vbuf_pool) {
        for (SDL_GPUBuffer* buf : kv.second) { SDL_ReleaseGPUBuffer(p->raw, buf); }
    }
    p->vbuf_pool.clear();
    for (auto& kv : p->ibuf_pool) {
        for (SDL_GPUBuffer* buf : kv.second) { SDL_ReleaseGPUBuffer(p->raw, buf); }
    }
    p->ibuf_pool.clear();
    for (std::uint32_t i = 0; i < UPLOAD_RING_SLOTS; ++i) {
        if (p->stage[i]) {
            SDL_ReleaseGPUTransferBuffer(p->raw, p->stage[i]);
            p->stage[i] = nullptr;
            p->stage_cap[i] = 0;
        }
    }
    p->upload_regions.clear();
    for (auto& kv : p->textures) { SDL_ReleaseGPUTexture(p->raw, kv.second); }
    p->textures.clear();
    if (p->white_tex) {
        SDL_ReleaseGPUTexture(p->raw, p->white_tex);
        p->white_tex = nullptr;
    }
    if (p->sampler) {
        SDL_ReleaseGPUSampler(p->raw, p->sampler);
        p->sampler = nullptr;
    }
    if (p->pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(p->raw, p->pipeline);
        p->pipeline = nullptr;
    }
    if (p->vert) {
        SDL_ReleaseGPUShader(p->raw, p->vert);
        p->vert = nullptr;
    }
    if (p->frag) {
        SDL_ReleaseGPUShader(p->raw, p->frag);
        p->frag = nullptr;
    }
    if (p->grad_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(p->raw, p->grad_pipeline);
        p->grad_pipeline = nullptr;
    }
    if (p->grad_frag) {
        SDL_ReleaseGPUShader(p->raw, p->grad_frag);
        p->grad_frag = nullptr;
    }
    p->raw = nullptr;
    p->dev = nullptr;
}

void rmlui_render_interface::begin_frame() {
    p->frame++;
    // Advance the staging ring (impl::stage): with 2 frames in flight, 3 slots
    // mean the slot this frame writes was last written 3 frames ago.
    p->stage_slot = (p->stage_slot + 1) % UPLOAD_RING_SLOTS;
    auto& d = p->deferred;
    for (std::size_t i = 0; i < d.size();) {
        if (d[i].first + KEEP_FRAMES <= p->frame) {
            d[i].second();
            d[i] = std::move(d.back());
            d.pop_back();
        } else {
            ++i;
        }
    }
    // Drop the cached tileset sheet once nothing has cropped from it for a while. Without
    // this, visiting character creation once pins a multi-megabyte surface for the rest of
    // the session; the cache only needs to survive a burst of crops from one sheet.
    if (p->sheet_rgba != nullptr && p->sheet_frame + SHEET_KEEP_FRAMES <= p->frame) {
        SDL_DestroySurface(p->sheet_rgba);
        p->sheet_rgba = nullptr;
        p->sheet_path.clear();
    }
}

void rmlui_render_interface::upload_pending(SDL_GPUCommandBuffer* cb) {
    if (p->pending.empty() || !cb) { return; }

    // MEASURED 2026-08-02 (1920x1080, i9-14900K, D3D12): this function used to
    // create a vertex buffer, an index buffer and two transfer buffers, and open
    // its own copy pass, PER BATCH. 27 batches/frame = 109 GPU resource
    // creations + 27 copy passes to move ~618 vertices (a few kB), and D3D12
    // committed-resource creation is ~0.1 ms each — that was the entire
    // 4.9 -> 12.8 ms/frame this function cost, i.e. nearly all of render_body.
    // Now destination buffers come from a recycling pool and staging is one
    // persistent ring buffer, so a steady-state frame creates nothing and issues
    // exactly one map + one copy pass.

    // Pass 1: size this frame's staging need. Empty geometry is retired here (as
    // before) so it never reaches the GPU work below.
    std::uint32_t total = 0;
    for (std::uint64_t h : p->pending) {
        auto it = p->geoms.find(h);
        if (it == p->geoms.end() || it->second.uploaded) { continue; }
        impl::geom& g = it->second;
        const std::uint32_t vbytes = static_cast<std::uint32_t>(
            g.verts.size() * sizeof(Rml::Vertex));
        const std::uint32_t ibytes = static_cast<std::uint32_t>(g.idx.size() * sizeof(int));
        if (vbytes == 0 || ibytes == 0) {
            g.uploaded = true;
            continue;
        }
        total += align_up(vbytes, STAGE_ALIGN) + align_up(ibytes, STAGE_ALIGN);
    }
    if (total == 0) {
        p->pending.clear();
        return;
    }

    SDL_GPUTransferBuffer* xfer = p->ensure_stage(total);
    if (!xfer) {
        dbg(DL::Error) << "rmlui: geometry staging alloc failed: " << SDL_GetError();
        p->pending.clear();
        return;
    }

    // The copy pass opens before the staging map so every early-out below can end
    // it exactly once; mapping is device-scope and legal inside an open pass.
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    if (!cp) {
        dbg(DL::Error) << "rmlui: geometry copy pass failed: " << SDL_GetError();
        p->pending.clear();
        return;
    }
    // cycle=true matches sprite_batcher's storage upload: the ring should already
    // guarantee this slot is retired, but cycling is free when it is and removes
    // the write-while-in-flight race if a driver ever queues deeper.
    void* mapped = SDL_MapGPUTransferBuffer(p->raw, xfer, true);
    if (!mapped) {
        dbg(DL::Error) << "rmlui: geometry staging map failed: " << SDL_GetError();
        SDL_EndGPUCopyPass(cp);
        p->pending.clear();
        return;
    }

    // Pass 2: acquire pooled buffers and pack every batch into the one staging
    // buffer at increasing offsets. Uploads are encoded after the unmap, which
    // SDL requires ("You must unmap the transfer buffer before encoding upload
    // commands").
    auto& regions = p->upload_regions;
    regions.clear();
    std::uint8_t* base = static_cast<std::uint8_t*>(mapped);
    std::uint32_t off = 0;
    for (std::uint64_t h : p->pending) {
        auto it = p->geoms.find(h);
        if (it == p->geoms.end() || it->second.uploaded) { continue; }
        impl::geom& g = it->second;
        const std::uint32_t vbytes = static_cast<std::uint32_t>(
            g.verts.size() * sizeof(Rml::Vertex));
        const std::uint32_t ibytes = static_cast<std::uint32_t>(g.idx.size() * sizeof(int));
        g.vbuf = p->acquire_buffer(
            p->vbuf_pool, SDL_GPU_BUFFERUSAGE_VERTEX, vbytes, g.vbuf_cap);
        g.ibuf = p->acquire_buffer(
            p->ibuf_pool, SDL_GPU_BUFFERUSAGE_INDEX, ibytes, g.ibuf_cap);
        if (!g.vbuf || !g.ibuf) {
            dbg(DL::Error) << "rmlui: geometry buffer alloc failed: " << SDL_GetError();
            // Whichever half succeeded never reached the GPU — recycle it now.
            if (g.vbuf) { p->vbuf_pool[g.vbuf_cap].push_back(g.vbuf); }
            if (g.ibuf) { p->ibuf_pool[g.ibuf_cap].push_back(g.ibuf); }
            g.vbuf = nullptr;
            g.ibuf = nullptr;
            g.vbuf_cap = 0;
            g.ibuf_cap = 0;
            continue;
        }
        std::memcpy(base + off, g.verts.data(), vbytes);
        regions.push_back({.dst = g.vbuf, .src_offset = off, .size = vbytes});
        off += align_up(vbytes, STAGE_ALIGN);
        std::memcpy(base + off, g.idx.data(), ibytes);
        regions.push_back({.dst = g.ibuf, .src_offset = off, .size = ibytes});
        off += align_up(ibytes, STAGE_ALIGN);

        g.idx_count = static_cast<std::uint32_t>(g.idx.size());
        g.uploaded = true;
    }
    SDL_UnmapGPUTransferBuffer(p->raw, xfer);

    for (const impl::upload_region& r : regions) {
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = xfer;
        src.offset = r.src_offset;
        SDL_GPUBufferRegion dst{};
        dst.buffer = r.dst;
        dst.size = r.size;
        SDL_UploadToGPUBuffer(cp, &src, &dst, false);
    }
    SDL_EndGPUCopyPass(cp);
    regions.clear();
    p->pending.clear();
}

void rmlui_render_interface::begin_render_pass(
    SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cb, std::uint32_t target_w, std::uint32_t target_h,
    std::uint32_t proj_w, std::uint32_t proj_h) {
    p->rp = rp;
    p->cb = cb;
    p->target_w = target_w;
    p->target_h = target_h;
    p->proj_w = proj_w == 0 ? target_w : proj_w;
    p->proj_h = proj_h == 0 ? target_h : proj_h;
    p->scissor_enabled = false;
}

void rmlui_render_interface::end_render_pass() {
    p->rp = nullptr;
    p->cb = nullptr;
}

std::uint32_t rmlui_render_interface::compiles_in_pass() const noexcept {
    return p->compiles_in_pass;
}

std::uint32_t rmlui_render_interface::textures_in_pass() const noexcept {
    return p->textures_in_pass;
}

void rmlui_render_interface::set_borrowed_texture_source(
    std::function<SDL_GPUTexture*()> resolver, int width, int height) {
    p->borrowed_lookup = std::move(resolver);
    p->borrowed_w = width;
    p->borrowed_h = height;
}

Rml::CompiledGeometryHandle rmlui_render_interface::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
    if (p->rp != nullptr) {
        // Compiled mid-Render(): would force an in-pass upload on D3D12. We defer
        // to next frame's upload_pending instead — count it for the Phase-5 gate.
        p->compiles_in_pass++;
    }
    const std::uint64_t h = p->next_geom++;
    impl::geom g;
    g.verts.assign(vertices.begin(), vertices.end());
    g.idx.assign(indices.begin(), indices.end());
    p->geoms.emplace(h, std::move(g));
    p->pending.push_back(h);
    return static_cast<Rml::CompiledGeometryHandle>(h);
}

void rmlui_render_interface::RenderGeometry(
    Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) {
    if (!p->rp || !p->cb || !p->pipeline) { return; }
    auto it = p->geoms.find(static_cast<std::uint64_t>(geometry));
    if (it == p->geoms.end()) { return; }
    impl::geom& g = it->second;
    if (!g.uploaded || g.idx_count == 0 || !g.vbuf || !g.ibuf) {
        return; // not yet uploaded (compiled mid-render last frame) — 1-frame pop-in
    }

    SDL_BindGPUGraphicsPipeline(p->rp, p->pipeline);

    SDL_GPUBufferBinding vb{};
    vb.buffer = g.vbuf;
    SDL_BindGPUVertexBuffers(p->rp, 0, &vb, 1);
    SDL_GPUBufferBinding ib{};
    ib.buffer = g.ibuf;
    SDL_BindGPUIndexBuffer(p->rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    // Texture: TextureHandle 0 is the documented untextured sentinel -> white
    // (never bind null — D3D12 invariant #3).
    SDL_GPUTexture* tex = p->white_tex;
    if (texture != 0) {
        auto tit = p->textures.find(static_cast<std::uint64_t>(texture));
        if (tit != p->textures.end()) { tex = tit->second; }
    }
    SDL_GPUTextureSamplerBinding tsb{};
    tsb.texture = tex;
    tsb.sampler = p->sampler;
    SDL_BindGPUFragmentSamplers(p->rp, 0, &tsb, 1);

    // Scissor in logical coords -> physical target pixels (scale when proj!=target).
    const float sx = p->proj_w ? static_cast<float>(p->target_w) / p->proj_w : 1.f;
    const float sy = p->proj_h ? static_cast<float>(p->target_h) / p->proj_h : 1.f;
    SDL_Rect sc;
    if (p->scissor_enabled) {
        sc.x = static_cast<int>(p->scissor.Left() * sx);
        sc.y = static_cast<int>(p->scissor.Top() * sy);
        sc.w = static_cast<int>(p->scissor.Width() * sx);
        sc.h = static_cast<int>(p->scissor.Height() * sy);
    } else {
        sc.x = 0;
        sc.y = 0;
        sc.w = static_cast<int>(p->target_w);
        sc.h = static_cast<int>(p->target_h);
    }
    SDL_SetGPUScissor(p->rp, &sc);

    vert_params vp{};
    if( p->active_transform.has_value() ) {
        // Bake translation into the transform matrix to avoid double-applying.
        auto tf = p->active_transform.value();
        auto *raw = tf.data();
        raw[12] += translation.x;
        raw[13] += translation.y;
        std::memcpy( vp.transform, raw, sizeof( vp.transform ) );
    } else {
        vp.translation_x = translation.x;
        vp.translation_y = translation.y;
        std::memset( vp.transform, 0, sizeof( vp.transform ) );
        vp.transform[0] = 1.f, vp.transform[5] = 1.f,
        vp.transform[10] = 1.f, vp.transform[15] = 1.f;
    }
    vp.viewport_w = static_cast<float>( p->proj_w );
    vp.viewport_h = static_cast<float>( p->proj_h );
    SDL_PushGPUVertexUniformData( p->cb, 0, &vp, sizeof( vp ) );

    SDL_DrawGPUIndexedPrimitives(p->rp, g.idx_count, 1, 0, 0, 0);
}

void rmlui_render_interface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    auto it = p->geoms.find(static_cast<std::uint64_t>(geometry));
    if (it == p->geoms.end()) { return; }
    SDL_GPUBuffer* vbuf = it->second.vbuf;
    SDL_GPUBuffer* ibuf = it->second.ibuf;
    const std::uint32_t vcap = it->second.vbuf_cap;
    const std::uint32_t icap = it->second.ibuf_cap;
    if (vbuf || ibuf) {
        // Recycle instead of destroy: RmlUi churns handles constantly, so
        // destroying here is what forced ~109 SDL_CreateGPUBuffer calls per frame
        // (~12 ms). The KEEP_FRAMES delay is unchanged, so a buffer only re-enters
        // the pool once the GPU is provably done reading it.
        impl* ip = p.get();
        p->deferred.emplace_back(p->frame, [ip, vbuf, ibuf, vcap, icap]() {
            if (vbuf) { ip->vbuf_pool[vcap].push_back(vbuf); }
            if (ibuf) { ip->ibuf_pool[icap].push_back(ibuf); }
        });
    }
    p->geoms.erase(it);
}

Rml::CompiledShaderHandle rmlui_render_interface::CompileShader(
    const Rml::String& name, const Rml::Dictionary& parameters) {
    const auto get_v2 = [&](const char* key, Rml::Vector2f def) -> Rml::Vector2f {
        auto i = parameters.find(key);
        if (i != parameters.end()) { i->second.GetInto(def); }
        return def;
    };
    const auto get_bool = [&](const char* key, bool def) -> bool {
        auto i = parameters.find(key);
        if (i != parameters.end()) { i->second.GetInto(def); }
        return def;
    };
    const auto get_float = [&](const char* key, float def) -> float {
        auto i = parameters.find(key);
        if (i != parameters.end()) { i->second.GetInto(def); }
        return def;
    };

    grad_params gp;
    if (name == "linear-gradient") {
        gp.func = get_bool("repeating", false) ? 3 : 0;
        const Rml::Vector2f a = get_v2("p0", Rml::Vector2f(0.f));
        const Rml::Vector2f b = get_v2("p1", Rml::Vector2f(0.f));
        gp.p0 = a.x;
        gp.p1 = a.y;
        gp.v0 = b.x - a.x;
        gp.v1 = b.y - a.y;
    } else if (name == "radial-gradient") {
        gp.func = get_bool("repeating", false) ? 4 : 1;
        const Rml::Vector2f c = get_v2("center", Rml::Vector2f(0.f));
        const Rml::Vector2f r = get_v2("radius", Rml::Vector2f(1.f));
        gp.p0 = c.x;
        gp.p1 = c.y;
        gp.v0 = r.x != 0.f ? 1.f / r.x : 0.f;
        gp.v1 = r.y != 0.f ? 1.f / r.y : 0.f;
    } else if (name == "conic-gradient") {
        gp.func = get_bool("repeating", false) ? 5 : 2;
        const Rml::Vector2f c = get_v2("center", Rml::Vector2f(0.f));
        const float ang = get_float("angle", 0.f);
        gp.p0 = c.x;
        gp.p1 = c.y;
        gp.v0 = std::cos(ang);
        gp.v1 = std::sin(ang);
    } else {
        dbg(DL::Warn) << "rmlui: unsupported shader '" << name << "'";
        return {};
    }

    auto sit = parameters.find("color_stop_list");
    if (sit != parameters.end() && sit->second.GetType() == Rml::Variant::COLORSTOPLIST) {
        const Rml::ColorStopList& stops = sit->second.GetReference<Rml::ColorStopList>();
        const int n = std::min(static_cast<int>(stops.size()), RMLUI_MAX_STOPS);
        gp.num_stops = n;
        for (int i = 0; i < n; i++) {
            const auto c = stops[i].color; // ColourbPremultiplied, 0..255
            gp.colors[i * 4 + 0] = c.red / 255.f;
            gp.colors[i * 4 + 1] = c.green / 255.f;
            gp.colors[i * 4 + 2] = c.blue / 255.f;
            gp.colors[i * 4 + 3] = c.alpha / 255.f;
            gp.positions[i] = stops[i].position.number;
        }
    }
    return reinterpret_cast<Rml::CompiledShaderHandle>(new grad_params(gp));
}

void rmlui_render_interface::RenderShader(
    Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry,
    Rml::Vector2f translation, Rml::TextureHandle /*texture*/) {
    if (!p->rp || !p->cb || !p->grad_pipeline || shader == 0) { return; }
    auto it = p->geoms.find(static_cast<std::uint64_t>(geometry));
    if (it == p->geoms.end()) { return; }
    impl::geom& g = it->second;
    if (!g.uploaded || g.idx_count == 0 || !g.vbuf || !g.ibuf) {
        return; // 1-frame pop-in (same as RenderGeometry)
    }
    const grad_params& gp = *reinterpret_cast<const grad_params*>(shader);

    SDL_BindGPUGraphicsPipeline(p->rp, p->grad_pipeline);

    SDL_GPUBufferBinding vb{};
    vb.buffer = g.vbuf;
    SDL_BindGPUVertexBuffers(p->rp, 0, &vb, 1);
    SDL_GPUBufferBinding ib{};
    ib.buffer = g.ibuf;
    SDL_BindGPUIndexBuffer(p->rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    // No fragment sampler: the gradient shader declares none.
    const float sx = p->proj_w ? static_cast<float>(p->target_w) / p->proj_w : 1.f;
    const float sy = p->proj_h ? static_cast<float>(p->target_h) / p->proj_h : 1.f;
    SDL_Rect sc;
    if (p->scissor_enabled) {
        sc.x = static_cast<int>(p->scissor.Left() * sx);
        sc.y = static_cast<int>(p->scissor.Top() * sy);
        sc.w = static_cast<int>(p->scissor.Width() * sx);
        sc.h = static_cast<int>(p->scissor.Height() * sy);
    } else {
        sc.x = 0;
        sc.y = 0;
        sc.w = static_cast<int>(p->target_w);
        sc.h = static_cast<int>(p->target_h);
    }
    SDL_SetGPUScissor(p->rp, &sc);

    vert_params vp{};
    if( p->active_transform.has_value() ) {
        auto tf = p->active_transform.value();
        auto *raw = tf.data();
        raw[12] += translation.x;
        raw[13] += translation.y;
        std::memcpy( vp.transform, raw, sizeof( vp.transform ) );
    } else {
        vp.translation_x = translation.x;
        vp.translation_y = translation.y;
        std::memset( vp.transform, 0, sizeof( vp.transform ) );
        vp.transform[0] = 1.f, vp.transform[5] = 1.f,
        vp.transform[10] = 1.f, vp.transform[15] = 1.f;
    }
    vp.viewport_w = static_cast<float>( p->proj_w );
    vp.viewport_h = static_cast<float>( p->proj_h );
    SDL_PushGPUVertexUniformData( p->cb, 0, &vp, sizeof( vp ) );
    SDL_PushGPUFragmentUniformData(p->cb, 0, &gp, sizeof(gp));

    SDL_DrawGPUIndexedPrimitives(p->rp, g.idx_count, 1, 0, 0, 0);
}

void rmlui_render_interface::ReleaseShader(Rml::CompiledShaderHandle shader) {
    delete reinterpret_cast<grad_params*>(shader);
}

Rml::TextureHandle rmlui_render_interface::LoadTexture(
    Rml::Vector2i& texture_dimensions, const Rml::String& source) {
    // Procedural textures: "?proc:<variant>" is generated in C++ rather than
    // loaded from disk. The source can arrive bare ("?proc:runic") or still
    // wrapped in the RCSS url(...) form ("url(?proc:runic)") — e.g. an
    // @spritesheet src keeps the wrapper — so accept either.
    Rml::String proc = source;
    if (proc.rfind("url(", 0) == 0 && !proc.empty() && proc.back() == ')') {
        proc = proc.substr(4, proc.size() - 5);
    }
    if (proc.rfind("?proc:", 0) == 0) {
        const std::string variant = proc.substr(6); // after "?proc:"
        int w = 0, h = 0;
        const std::vector<std::uint8_t> px = gen_runic_frame(variant, w, h);
        SDL_GPUTexture* ptex = p->upload_rgba(px.data(), w, h);
        if (!ptex) { return 0; }
        if (p->rp != nullptr) { p->textures_in_pass++; }
        texture_dimensions = Rml::Vector2i(w, h);
        const std::uint64_t ph = p->next_tex++;
        p->textures.emplace(ph, ptex);
        dbg(DL::Info) << "rmlui_proc: generated \"" << source << "\" " << w << "x" << h
                      << " handle=" << ph;
        return static_cast<Rml::TextureHandle>(ph);
    }
    // Atlas crop: "?sprite:<x>:<y>:<w>:<h>:<path>" lifts ONE sprite out of a tileset sheet
    // on disk and uploads it as its own small texture. The path comes last because it is the
    // only field that may itself contain ':'.
    //
    // Why decode from file rather than sample the live atlas: the GPU atlas has no sub-rect
    // support on this interface (a borrowed texture is handed over whole), and cropping on
    // the GPU would mean a copy pass inside RmlUi's render — the D3D12 hazard this file
    // already goes out of its way to avoid. The decode happens once per distinct sprite
    // because RmlUi caches by source string.
    if (proc.rfind("?sprite:", 0) == 0) {
        const std::string spec = proc.substr(8);
        int sx = 0, sy = 0, sw = 0, sh = 0;
        std::size_t pos = 0;
        int *const fields[4] = {&sx, &sy, &sw, &sh};
        bool parsed = true;
        for (int *f : fields) {
            const std::size_t colon = spec.find(':', pos);
            if (colon == std::string::npos) { parsed = false; break; }
            *f = std::atoi(spec.substr(pos, colon - pos).c_str());
            pos = colon + 1;
        }
        const std::string path = parsed ? spec.substr(pos) : std::string();
        if (!parsed || path.empty() || sw <= 0 || sh <= 0) {
            dbg(DL::Error) << "rmlui_sprite: malformed source \"" << source << "\"";
            return 0;
        }
        // Decode only when the sheet changed; see impl::sheet_rgba.
        if (p->sheet_rgba == nullptr || p->sheet_path != path) {
            SDL_Surface* sheet = IMG_Load(path.c_str());
            if (sheet == nullptr) {
                dbg(DL::Error) << "rmlui_sprite: cannot load \"" << path << "\": " << SDL_GetError();
                return 0;
            }
            // Force a known layout so the row copy below needs no per-format branching.
            SDL_Surface* conv = SDL_ConvertSurface(sheet, SDL_PIXELFORMAT_ABGR8888);
            SDL_DestroySurface(sheet);
            if (conv == nullptr) { return 0; }
            if (p->sheet_rgba != nullptr) { SDL_DestroySurface(p->sheet_rgba); }
            p->sheet_rgba = conv;
            p->sheet_path = path;
            dbg(DL::Info) << "rmlui_sprite: decoded sheet " << path;
        }
        p->sheet_frame = p->frame;
        SDL_Surface* const rgba = p->sheet_rgba;
        std::vector<std::uint8_t> px(static_cast<std::size_t>(sw) * sh * 4, 0);
        for (int row = 0; row < sh; ++row) {
            const int src_y = sy + row;
            if (src_y < 0 || src_y >= rgba->h) { continue; }
            const int copy_w = std::min(sw, rgba->w - sx);
            if (copy_w <= 0) { continue; }
            const auto *src = static_cast<const std::uint8_t*>(rgba->pixels) +
                              static_cast<std::size_t>(src_y) * rgba->pitch +
                              static_cast<std::size_t>(sx) * 4;
            std::memcpy(px.data() + static_cast<std::size_t>(row) * sw * 4, src,
                        static_cast<std::size_t>(copy_w) * 4);
        }
        // rgba is the cached sheet — owned by impl, NOT freed here.
        SDL_GPUTexture* stex = p->upload_rgba(px.data(), sw, sh);
        if (!stex) { return 0; }
        if (p->rp != nullptr) { p->textures_in_pass++; }
        texture_dimensions = Rml::Vector2i(sw, sh);
        const std::uint64_t sph = p->next_tex++;
        p->textures.emplace(sph, stex);
        dbg(DL::Info) << "rmlui_sprite: cropped " << sw << "x" << sh << " from " << path
                      << " at " << sx << "," << sy << " handle=" << sph;
        return static_cast<Rml::TextureHandle>(sph);
    }
    // Borrowed textures: "?avatar:<generation>" hands back a texture the RENDERER owns
    // (render_state's portrait target) rather than one this interface allocated. Two
    // consequences, both load-bearing:
    //   * it must be recorded in `borrowed` so ReleaseTexture does NOT free it — RmlUi
    //     drops textures whenever a document reloads, which would destroy a target
    //     render_state is still drawing into;
    //   * the <generation> suffix exists only to bust RmlUi's own source-string cache,
    //     so a reallocated target is re-resolved instead of leaving the document
    //     sampling a dead texture.
    if (proc.rfind("?avatar:", 0) == 0) {
        SDL_GPUTexture* borrowed_tex = p->borrowed_lookup ? p->borrowed_lookup() : nullptr;
        if (!borrowed_tex) { return 0; }
        if (p->rp != nullptr) { p->textures_in_pass++; }
        texture_dimensions = Rml::Vector2i(p->borrowed_w, p->borrowed_h);
        const std::uint64_t bh = p->next_tex++;
        p->textures.emplace(bh, borrowed_tex);
        p->borrowed.insert(bh);
        dbg(DL::Info) << "rmlui_borrowed: bound \"" << source << "\" "
                      << p->borrowed_w << "x" << p->borrowed_h << " handle=" << bh;
        return static_cast<Rml::TextureHandle>(bh);
    }
    Rml::String path = source;
    int svg_px = 0;
    const std::size_t query_pos = path.rfind("?px=");
    if (query_pos != Rml::String::npos) {
        const std::string digits = path.substr(query_pos + 4);
        path.erase(query_pos); // strip "?px=..." — IMG_Load must never see the query
        // Sized rasterization only makes sense for an .svg source; a non-svg
        // path with a stray "?px=" marker just loses the (meaningless) suffix.
        static const std::string svg_ext = ".svg";
        const bool is_svg = query_pos >= svg_ext.size() &&
            path.compare(query_pos - svg_ext.size(), svg_ext.size(), svg_ext) == 0;
        if (is_svg && !digits.empty() &&
            std::all_of(digits.begin(), digits.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; })) {
            svg_px = std::atoi(digits.c_str());
        }
        if (is_svg && svg_px <= 0) {
            dbg(DL::Warn) << "rmlui: malformed ?px= size in " << source
                          << ", loading at native size";
        }
    }
    SDL_Surface* surf = nullptr;
    if (svg_px > 0) {
        SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
        if (io == nullptr) {
            dbg(DL::Warn) << "rmlui: cannot open " << path << ": " << SDL_GetError();
        } else {
            surf = IMG_LoadSizedSVG_IO(io, svg_px, svg_px);
            SDL_CloseIO(io);
            if (!surf) {
                dbg(DL::Warn) << "rmlui: sized SVG rasterize failed for " << path << ": "
                              << SDL_GetError();
            }
        }
    }
    if (!surf) {
        surf = IMG_Load(path.c_str());
    }
    if (!surf) {
        dbg(DL::Warn) << "rmlui: LoadTexture failed for " << source << ": " << SDL_GetError();
        return 0;
    }
    SDL_Surface* rgba = surf;
    if (surf->format != SDL_PIXELFORMAT_RGBA32) {
        rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surf);
        if (!rgba) { return 0; }
    }
    SDL_GPUTexture* tex =
        p->upload_rgba(static_cast<const std::uint8_t*>(rgba->pixels), rgba->w, rgba->h);
    texture_dimensions = Rml::Vector2i(rgba->w, rgba->h);
    SDL_DestroySurface(rgba);
    if (!tex) { return 0; }
    if (p->rp != nullptr) { p->textures_in_pass++; }
    const std::uint64_t h = p->next_tex++;
    p->textures.emplace(h, tex);
    return static_cast<Rml::TextureHandle>(h);
}

Rml::TextureHandle rmlui_render_interface::GenerateTexture(
    Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) {
    SDL_GPUTexture* tex = p->upload_rgba(
        reinterpret_cast<const std::uint8_t*>(source.data()), source_dimensions.x,
        source_dimensions.y);
    if (!tex) { return 0; }
    if (p->rp != nullptr) { p->textures_in_pass++; }
    const std::uint64_t h = p->next_tex++;
    p->textures.emplace(h, tex);
    return static_cast<Rml::TextureHandle>(h);
}

void rmlui_render_interface::ReleaseTexture(Rml::TextureHandle texture) {
    const std::uint64_t h = static_cast<std::uint64_t>(texture);
    auto it = p->textures.find(h);
    if (it == p->textures.end()) { return; }
    // Borrowed textures belong to render_state, not to us. RmlUi releases textures
    // whenever a document reloads, so freeing one here would destroy a live render
    // target mid-frame. Drop the mapping only.
    if (p->borrowed.erase(h) != 0) {
        p->textures.erase(it);
        return;
    }
    SDL_GPUDevice* raw = p->raw;
    SDL_GPUTexture* tex = it->second;
    p->deferred.emplace_back(p->frame, [raw, tex]() { SDL_ReleaseGPUTexture(raw, tex); });
    p->textures.erase(it);
}

void rmlui_render_interface::EnableScissorRegion(bool enable) { p->scissor_enabled = enable; }

void rmlui_render_interface::SetScissorRegion(Rml::Rectanglei region) {
    p->scissor = region;
    p->scissor_enabled = true;
}
void rmlui_render_interface::SetTransform(const Rml::Matrix4f* transform) {
    if (transform) {
        p->active_transform = *transform;
    } else {
        p->active_transform.reset();
    }
}

} // namespace lighting
