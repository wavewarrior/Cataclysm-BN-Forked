#include "font_engine.h"

#include "debug.h"
#include "shader_compiler.h"

#include <cstring>
#include <stdexcept>
#include <utility>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

// ---- shaders ----------------------------------------------------------

static const char* const FONT_VERT_HLSL = R"HLSL(
cbuffer FrameParams : register(b0, space1) {
    float2 target_size;
    float2 origin;     // pixel-space (x, y) where the text starts
};

struct VS_IN {
    float2 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VS_OUT main(VS_IN i) {
    // TTF reports vertices in pixel space — translate by origin and project
    // to clip. Y-flip matches the rest of the engine (top-left origin).
    const float2 pixel = i.pos + origin;
    const float2 ndc = float2(
        pixel.x / target_size.x *  2.0 - 1.0,
        pixel.y / target_size.y * -2.0 + 1.0);
    VS_OUT o;
    o.pos = float4(ndc, 0.0, 1.0);
    o.uv  = i.uv;
    return o;
}
)HLSL";

static const char* const FONT_FRAG_HLSL = R"HLSL(
Texture2D<float4> Atlas    : register(t0, space2);
SamplerState      AtlasSmp : register(s0, space2);

cbuffer Tint : register(b0, space3) {
    float4 color;
};

struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main(VS_OUT i) : SV_Target0 {
    // ALPHA glyphs: atlas R channel carries coverage. Final RGB = tint,
    // alpha = tint.a * coverage.
    const float coverage = Atlas.Sample(AtlasSmp, i.uv).r;
    return float4(color.rgb, color.a * coverage);
}
)HLSL";

// ---- PIMPL ------------------------------------------------------------

class font_engine_impl {
public:
    // Holds enough headroom for several hundred glyphs per draw — a long
    // message log row fits easily. Each glyph = 4 verts + 6 indices, so
    // 8192 verts ≈ 2048 glyphs.
    static constexpr std::uint32_t MAX_VERTICES = 8192;
    static constexpr std::uint32_t MAX_INDICES = MAX_VERTICES * 6 / 4;
    static constexpr std::uint32_t RING_SLOTS = 3;

    gpu_device* dev = nullptr;
    TTF_TextEngine* engine = nullptr;
    SDL_GPUSampler* sampler = nullptr;

    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUShader* vert_shader = nullptr;
    SDL_GPUShader* frag_shader = nullptr;

    // Per-frame ring of vertex + index buffers + transfer staging.
    SDL_GPUBuffer* vbufs[RING_SLOTS] = {};
    SDL_GPUBuffer* ibufs[RING_SLOTS] = {};
    SDL_GPUTransferBuffer* vxfer[RING_SLOTS] = {};
    SDL_GPUTransferBuffer* ixfer[RING_SLOTS] = {};
    std::uint32_t cur_slot = 0;

    void init(gpu_device& d, SDL_GPUTextureFormat fmt) {
        if (!d.ready()) { throw std::runtime_error("font_engine::init: gpu_device not ready"); }
        dev = &d;

        engine = TTF_CreateGPUTextEngine(d.raw());
        if (!engine) {
            throw std::runtime_error(std::string("TTF_CreateGPUTextEngine: ") + SDL_GetError());
        }
        // CCW to match our sprite pipeline's front_face.
        TTF_SetGPUTextEngineWinding(engine, TTF_GPU_TEXTENGINE_WINDING_COUNTER_CLOCKWISE);

        init_shader_compiler();

        auto v = compile_graphics_shader(
            d, FONT_VERT_HLSL, "main", SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "font.vert");
        auto f = compile_graphics_shader(
            d, FONT_FRAG_HLSL, "main", SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "font.frag");
        if (!v || !f) {
            if (v) { SDL_ReleaseGPUShader(d.raw(), v.shader); }
            if (f) { SDL_ReleaseGPUShader(d.raw(), f.shader); }
            throw std::runtime_error("font shader compile failed");
        }
        vert_shader = v.shader;
        frag_shader = f.shader;

        // Pipeline.
        SDL_GPUVertexBufferDescription vbuf_desc{};
        vbuf_desc.slot = 0;
        vbuf_desc.pitch = sizeof(float) * 4; // pos.xy + uv.xy
        vbuf_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        vbuf_desc.instance_step_rate = 0;

        SDL_GPUVertexAttribute attrs[2]{};
        attrs[0].location = 0;
        attrs[0].buffer_slot = 0;
        attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[0].offset = 0;
        attrs[1].location = 1;
        attrs[1].buffer_slot = 0;
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[1].offset = sizeof(float) * 2;

        SDL_GPUVertexInputState vis{};
        vis.vertex_buffer_descriptions = &vbuf_desc;
        vis.num_vertex_buffers = 1;
        vis.vertex_attributes = attrs;
        vis.num_vertex_attributes = 2;

        SDL_GPUColorTargetBlendState blend{};
        blend.enable_blend = true;
        // Premultiplied alpha source (we output `color.a * coverage`
        // in alpha and `color.rgb` in colour, classic ALPHA glyph mix).
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.color_write_mask =
            SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B
            | SDL_GPU_COLORCOMPONENT_A;

        SDL_GPUColorTargetDescription ctd{};
        ctd.format = fmt;
        ctd.blend_state = blend;

        SDL_GPUGraphicsPipelineCreateInfo pci{};
        pci.vertex_shader = vert_shader;
        pci.fragment_shader = frag_shader;
        pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pci.vertex_input_state = vis;
        pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pci.target_info.num_color_targets = 1;
        pci.target_info.color_target_descriptions = &ctd;

        pipeline = SDL_CreateGPUGraphicsPipeline(d.raw(), &pci);
        if (!pipeline) {
            throw std::runtime_error(std::string("font pipeline: ") + SDL_GetError());
        }

        SDL_GPUSamplerCreateInfo si{};
        si.min_filter = SDL_GPU_FILTER_LINEAR;
        si.mag_filter = SDL_GPU_FILTER_LINEAR;
        si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler = SDL_CreateGPUSampler(d.raw(), &si);
        if (!sampler) { throw std::runtime_error(std::string("font sampler: ") + SDL_GetError()); }

        const Uint32 vbytes = MAX_VERTICES * sizeof(float) * 4;
        const Uint32 ibytes = MAX_INDICES * sizeof(Uint32);
        for (std::uint32_t i = 0; i < RING_SLOTS; ++i) {
            SDL_GPUBufferCreateInfo vbci{};
            vbci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            vbci.size = vbytes;
            vbufs[i] = SDL_CreateGPUBuffer(d.raw(), &vbci);

            SDL_GPUBufferCreateInfo ibci{};
            ibci.usage = SDL_GPU_BUFFERUSAGE_INDEX;
            ibci.size = ibytes;
            ibufs[i] = SDL_CreateGPUBuffer(d.raw(), &ibci);

            SDL_GPUTransferBufferCreateInfo vtci{};
            vtci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            vtci.size = vbytes;
            vxfer[i] = SDL_CreateGPUTransferBuffer(d.raw(), &vtci);

            SDL_GPUTransferBufferCreateInfo itci{};
            itci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            itci.size = ibytes;
            ixfer[i] = SDL_CreateGPUTransferBuffer(d.raw(), &itci);

            if (!vbufs[i] || !ibufs[i] || !vxfer[i] || !ixfer[i]) {
                throw std::runtime_error(std::string("font buffers: ") + SDL_GetError());
            }
        }

        dbg(DL::Info) << "font_engine initialised.";
    }

    void shutdown() noexcept {
        if (!dev) { return; }
        SDL_GPUDevice* r = dev->raw();
        for (std::uint32_t i = 0; i < RING_SLOTS; ++i) {
            if (vbufs[i]) {
                SDL_ReleaseGPUBuffer(r, vbufs[i]);
                vbufs[i] = nullptr;
            }
            if (ibufs[i]) {
                SDL_ReleaseGPUBuffer(r, ibufs[i]);
                ibufs[i] = nullptr;
            }
            if (vxfer[i]) {
                SDL_ReleaseGPUTransferBuffer(r, vxfer[i]);
                vxfer[i] = nullptr;
            }
            if (ixfer[i]) {
                SDL_ReleaseGPUTransferBuffer(r, ixfer[i]);
                ixfer[i] = nullptr;
            }
        }
        if (sampler) {
            SDL_ReleaseGPUSampler(r, sampler);
            sampler = nullptr;
        }
        if (pipeline) {
            SDL_ReleaseGPUGraphicsPipeline(r, pipeline);
            pipeline = nullptr;
        }
        if (vert_shader) {
            SDL_ReleaseGPUShader(r, vert_shader);
            vert_shader = nullptr;
        }
        if (frag_shader) {
            SDL_ReleaseGPUShader(r, frag_shader);
            frag_shader = nullptr;
        }
        if (engine) {
            TTF_DestroyGPUTextEngine(engine);
            engine = nullptr;
        }
        dev = nullptr;
    }

    void begin_frame() noexcept { cur_slot = (cur_slot + 1) % RING_SLOTS; }

    void draw_text(
        SDL_GPUCommandBuffer* cb, SDL_GPUTexture* target, std::uint32_t target_w,
        std::uint32_t target_h, TTF_Text* text, float x, float y, const float rgba[4]) {
        if (!dev || !cb || !target || !text) { return; }

        TTF_GPUAtlasDrawSequence* seq = TTF_GetGPUTextDrawData(text);
        if (!seq) {
            return; // empty text
        }

        // Flatten sequences into one vertex+index pair. Multiple atlas
        // pages → multiple draw calls (one per sequence), but we batch
        // the upload into one map+memcpy each for v and i.
        std::vector<float> verts;
        std::vector<Uint32> idx;
        // Per-sequence record so end-of-loop knows where to slice the
        // big buffer for binding + indexed draws.
        struct draw_part {
            SDL_GPUTexture* tex;
            Uint32 first_index;
            Uint32 num_indices;
            Sint32 vertex_offset;
        };
        std::vector<draw_part> parts;
        verts.reserve(1024);
        idx.reserve(1536);
        parts.reserve(4);

        for (; seq; seq = seq->next) {
            if (seq->image_type != TTF_IMAGE_ALPHA) {
                // Phase 2f scope: alpha glyphs only.
                dbg(DL::Warn) << "font_engine: non-ALPHA glyph sequence ("
                              << static_cast<int>(seq->image_type) << ") skipped";
                continue;
            }
            const Sint32 base_vert = static_cast<Sint32>(verts.size() / 4);
            const Uint32 first_idx = static_cast<Uint32>(idx.size());

            verts.reserve(verts.size() + seq->num_vertices * 4);
            for (int vi = 0; vi < seq->num_vertices; ++vi) {
                verts.push_back(seq->xy[vi].x);
                verts.push_back(seq->xy[vi].y);
                verts.push_back(seq->uv[vi].x);
                verts.push_back(seq->uv[vi].y);
            }
            idx.reserve(idx.size() + seq->num_indices);
            for (int ii = 0; ii < seq->num_indices; ++ii) {
                idx.push_back(static_cast<Uint32>(seq->indices[ii]));
            }
            parts.push_back(draw_part{
                seq->atlas_texture, first_idx, static_cast<Uint32>(seq->num_indices), base_vert});
        }

        if (parts.empty()) { return; }
        if (verts.size() / 4 > MAX_VERTICES || idx.size() > MAX_INDICES) {
            dbg(DL::Warn) << "font_engine: text exceeds per-frame buffer cap, truncating";
            // Cheap truncate — drop overflow parts.
            while (!parts.empty()
                   && parts.back().first_index + parts.back().num_indices > MAX_INDICES) {
                parts.pop_back();
            }
            idx.resize(std::min<std::size_t>(idx.size(), MAX_INDICES));
            verts.resize(std::min<std::size_t>(verts.size(), MAX_VERTICES * 4));
            if (parts.empty()) { return; }
        }

        // Upload.
        const Uint32 slot = cur_slot;
        const Uint32 vbytes = static_cast<Uint32>(verts.size() * sizeof(float));
        const Uint32 ibytes = static_cast<Uint32>(idx.size() * sizeof(Uint32));

        void* vmap = SDL_MapGPUTransferBuffer(dev->raw(), vxfer[slot], true);
        void* imap = SDL_MapGPUTransferBuffer(dev->raw(), ixfer[slot], true);
        if (!vmap || !imap) {
            dbg(DL::Error) << "font_engine map failed: " << SDL_GetError();
            if (vmap) { SDL_UnmapGPUTransferBuffer(dev->raw(), vxfer[slot]); }
            if (imap) { SDL_UnmapGPUTransferBuffer(dev->raw(), ixfer[slot]); }
            return;
        }
        std::memcpy(vmap, verts.data(), vbytes);
        std::memcpy(imap, idx.data(), ibytes);
        SDL_UnmapGPUTransferBuffer(dev->raw(), vxfer[slot]);
        SDL_UnmapGPUTransferBuffer(dev->raw(), ixfer[slot]);

        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
        if (!cp) { return; }
        SDL_GPUTransferBufferLocation vsrc{vxfer[slot], 0};
        SDL_GPUBufferRegion vdst{vbufs[slot], 0, vbytes};
        SDL_UploadToGPUBuffer(cp, &vsrc, &vdst, false);

        SDL_GPUTransferBufferLocation isrc{ixfer[slot], 0};
        SDL_GPUBufferRegion idst{ibufs[slot], 0, ibytes};
        SDL_UploadToGPUBuffer(cp, &isrc, &idst, false);
        SDL_EndGPUCopyPass(cp);

        // Render.
        SDL_GPUColorTargetInfo ct{};
        ct.texture = target;
        ct.load_op = SDL_GPU_LOADOP_LOAD;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        ct.cycle = false;

        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cb, &ct, 1, nullptr);
        if (!rp) { return; }
        SDL_BindGPUGraphicsPipeline(rp, pipeline);

        SDL_GPUBufferBinding vb{vbufs[slot], 0};
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_GPUBufferBinding ib{ibufs[slot], 0};
        SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        const SDL_GPUViewport
            vp{0.0f, 0.0f, static_cast<float>(target_w), static_cast<float>(target_h), 0.0f, 1.0f};
        SDL_SetGPUViewport(rp, &vp);

        struct vert_uniform {
            float target_w;
            float target_h;
            float origin_x;
            float origin_y;
        } vu{static_cast<float>(target_w), static_cast<float>(target_h), x, y};
        SDL_PushGPUVertexUniformData(cb, 0, &vu, sizeof(vu));

        float tint[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
        SDL_PushGPUFragmentUniformData(cb, 0, tint, sizeof(tint));

        for (const draw_part& dp : parts) {
            SDL_GPUTextureSamplerBinding tsb{dp.tex, sampler};
            SDL_BindGPUFragmentSamplers(rp, 0, &tsb, 1);
            SDL_DrawGPUIndexedPrimitives(
                rp, dp.num_indices, /*num_instances=*/1, dp.first_index, dp.vertex_offset,
                /*first_instance=*/0);
        }

        SDL_EndGPURenderPass(rp);
    }
};

// ---- trampolines ------------------------------------------------------

font_engine::font_engine(): p(std::make_unique<font_engine_impl>()) {}
font_engine::font_engine(font_engine&&) noexcept = default;
font_engine& font_engine::operator=(font_engine&&) noexcept = default;
font_engine::~font_engine() {
    if (p) { p->shutdown(); }
}

void font_engine::init(gpu_device& d, SDL_GPUTextureFormat fmt) { p->init(d, fmt); }
void font_engine::shutdown() noexcept {
    if (p) { p->shutdown(); }
}
TTF_TextEngine* font_engine::raw() const noexcept { return p ? p->engine : nullptr; }
void font_engine::begin_frame() {
    if (p) { p->begin_frame(); }
}

void font_engine::draw_text(
    SDL_GPUCommandBuffer* cb, SDL_GPUTexture* target, std::uint32_t target_w,
    std::uint32_t target_h, TTF_Text* text, float x, float y, const float rgba[4]) {
    p->draw_text(cb, target, target_w, target_h, text, x, y, rgba);
}

} // namespace lighting
