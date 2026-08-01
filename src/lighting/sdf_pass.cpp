#include "lighting/sdf_pass.h"

#include "debug.h"
#include "lighting/gpu_device.h"
#include "sdl_wrappers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#define dbg(x) DebugLogFL((x), DC::SDL)

namespace lighting {

// ---------------------------------------------------------------------------
// GPU texture management
// ---------------------------------------------------------------------------

sdf_pass::~sdf_pass() {
    // Caller must call shutdown() with the device before destruction. GPU
    // resources can't be released here (no device pointer); a leak means
    // shutdown() was skipped.
}

void sdf_pass::init(gpu_device& dev, int map_w, int map_h) {
    if (!dev.ready() || map_w <= 0 || map_h <= 0) { return; }
    map_w_ = map_w;
    map_h_ = map_h;

    SDL_GPUDevice* d = dev.raw();

    // P3.3: transparency_tex_ (R8), sdf_tex_ (R32F) and sky_vis_tex_ deleted —
    // nothing samples them. The JFA seed reads trans_storage_ (float), SDF reads
    // sdf_storage_, sky_vis reads skyvis_storage_.

    // Transfer buffers (sky_vis R8 path retained for the byte source below).
    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = static_cast<Uint32>(map_w * map_h); // 1 byte per tile
        xfer_sky_vis_ = SDL_CreateGPUTransferBuffer(d, &tbci);
    }
    {
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = static_cast<Uint32>(map_w * map_h * 4); // 4 bytes per tile (float)
        xfer_skyvis_f_ = SDL_CreateGPUTransferBuffer(d, &tbci);
    }
    {
        // Stage 2b coverage occluder: tile-res, 2 floats/tile (height, roof).
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = static_cast<Uint32>(map_w * map_h * 2 * 4);
        xfer_occ_ = SDL_CreateGPUTransferBuffer(d, &tbci);
    }
    {
        // P3 JFA input: tile-res transparency as floats (0.0=opaque .. 1.0=open).
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = static_cast<Uint32>(map_w * map_h * 4);
        xfer_trans_f_ = SDL_CreateGPUTransferBuffer(d, &tbci);
    }

    // SDF + sky-vis as fragment-readable storage buffers (sampler-texture
    // Load returns 0 on Metal). Same data as the textures, as float arrays.
    {
        SDL_GPUBufferCreateInfo bci{};
        // GRAPHICS read (sprite.frag SdfBuf) + COMPUTE read (gi_field/gi_bounce
        // sphere-march the same SDF on the GPU compute GI path) + COMPUTE write
        // (P3.3: JFA resolve pass writes directly to sdf_storage_).
        bci.usage =
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ
            | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
        // SDF_SUPERSAMPLE² subcells per tile, 4 bytes each.
        bci.size = static_cast<Uint32>(map_w * map_h * SDF_SUPERSAMPLE * SDF_SUPERSAMPLE * 4);
        sdf_storage_ = SDL_CreateGPUBuffer(d, &bci);
        if (!sdf_storage_) { dbg(DL::Error) << "sdf_pass::init: failed to create sdf_storage"; }
    }
    {
        // GRAPHICS read (sprite.frag SkyVisBuf) + COMPUTE read (sky_sun.comp
        // portal-tests it to find open-sky directions — Stage 2a).
        SDL_GPUBufferCreateInfo bci{};
        bci.usage =
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
        bci.size = static_cast<Uint32>(map_w * map_h * 4);
        skyvis_storage_ = SDL_CreateGPUBuffer(d, &bci);
        if (!skyvis_storage_) {
            dbg(DL::Error) << "sdf_pass::init: failed to create skyvis_storage";
        }
    }
    {
        // Stage 2b unified coverage occluder field — tile-res, 2 floats/tile
        // (height, roof). COMPUTE read (sky_sun.comp marches it).
        SDL_GPUBufferCreateInfo bci{};
        bci.usage =
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
        bci.size = static_cast<Uint32>(map_w * map_h * 2 * 4);
        occ_storage_ = SDL_CreateGPUBuffer(d, &bci);
        if (!occ_storage_) { dbg(DL::Error) << "sdf_pass::init: failed to create occ_storage"; }
    }
    {
        // P3 JFA input: tile-res transparency as floats (0.0=opaque .. 1.0=open).
        // COMPUTE read so the seed shader can consume it directly.
        SDL_GPUBufferCreateInfo bci{};
        bci.usage =
            SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
        bci.size = static_cast<Uint32>(map_w * map_h * 4);
        trans_storage_ = SDL_CreateGPUBuffer(d, &bci);
        if (!trans_storage_) { dbg(DL::Error) << "sdf_pass::init: failed to create trans_storage"; }
    }
}

void sdf_pass::shutdown(gpu_device& dev) {
    SDL_GPUDevice* d = dev.raw();
    if (!d) { return; }
    if (xfer_sky_vis_) {
        SDL_ReleaseGPUTransferBuffer(d, xfer_sky_vis_);
        xfer_sky_vis_ = nullptr;
    }
    if (xfer_skyvis_f_) {
        SDL_ReleaseGPUTransferBuffer(d, xfer_skyvis_f_);
        xfer_skyvis_f_ = nullptr;
    }
    if (sdf_storage_) {
        SDL_ReleaseGPUBuffer(d, sdf_storage_);
        sdf_storage_ = nullptr;
    }
    if (skyvis_storage_) {
        SDL_ReleaseGPUBuffer(d, skyvis_storage_);
        skyvis_storage_ = nullptr;
    }
    if (xfer_occ_) {
        SDL_ReleaseGPUTransferBuffer(d, xfer_occ_);
        xfer_occ_ = nullptr;
    }
    if (occ_storage_) {
        SDL_ReleaseGPUBuffer(d, occ_storage_);
        occ_storage_ = nullptr;
    }
    if (xfer_trans_f_) {
        SDL_ReleaseGPUTransferBuffer(d, xfer_trans_f_);
        xfer_trans_f_ = nullptr;
    }
    if (trans_storage_) {
        SDL_ReleaseGPUBuffer(d, trans_storage_);
        trans_storage_ = nullptr;
    }
}

void sdf_pass::upload(
    SDL_GPUCopyPass* cp, SDL_GPUDevice* dev, int runtime_w, int runtime_h,
    const std::vector<uint8_t>& transparency, const std::vector<float>& sdf,
    const std::vector<uint8_t>& sky_vis,
    const std::vector<float>& occ) {
    if (!cp || !dev || !trans_storage_) { return; }
    if (runtime_w <= 0 || runtime_h <= 0) { return; }
    // Refuse runtime sizes that exceed the buffer allocation. Should never
    // happen if render_state::init sized for REALITY_BUBBLE_SIZE_MAX, but
    // clamp defensively rather than overrun the GPU storage buffers.
    if (runtime_w > map_w_ || runtime_h > map_h_) {
        dbg(DL::Error) << "sdf_pass::upload: runtime " << runtime_w << "x" << runtime_h
                       << " exceeds buf " << map_w_ << "x" << map_h_;
        return;
    }

    const Uint32 pixel_count = static_cast<Uint32>(runtime_w * runtime_h);
    // populated_ flips only when transparency actually lands on the GPU (the
    // trans_storage_ block below). Main-menu / pre-world frames call upload()
    // with empty vectors → guards skip every channel → populated_ stays false
    // so begin_lighting_frame keeps sdf_map_w/h=0 and the shader skips its
    // shadow march (which would read s=0 → shadow=0 → SDF debug all red + sun
    // killed).

    // All uploads write a runtime_w × runtime_h sub-rect at (0,0). The buffers
    // are sized for REALITY_BUBBLE_SIZE_MAX so they hold any legal mapsize; the
    // shader clamps with runtime_w/h (via map_w()/h()), and the x-major packing
    // matches arr[x * runtime_h + y].

    // P3.3: transparency is no longer uploaded to an R8 sampler texture (nothing
    // sampled it). It feeds the JFA seed via trans_storage_ (float) only — see
    // the trans_storage_ block below, which also flips populated_.

    // Stage 2b: unified coverage occluder field — tile-res, 2 floats/tile
    // (occ[(x*runtime_h+y)*2 + 0] = occluder height, +1 = roof bit). Marched by
    // sky_sun.comp for sun/moon/sky occlusion (single occlusion source).
    {
        const Uint32 occ_floats = pixel_count * 2u;
        if (xfer_occ_ && occ_storage_ && static_cast<Uint32>(occ.size()) >= occ_floats) {
            void* mapped = SDL_MapGPUTransferBuffer(dev, xfer_occ_, true);
            if (mapped) {
                std::memcpy(mapped, occ.data(), occ_floats * sizeof(float));
                SDL_UnmapGPUTransferBuffer(dev, xfer_occ_);

                SDL_GPUTransferBufferLocation tb_src{};
                tb_src.transfer_buffer = xfer_occ_;
                tb_src.offset = 0;

                SDL_GPUBufferRegion buf_dst{};
                buf_dst.buffer = occ_storage_;
                buf_dst.offset = 0;
                buf_dst.size = occ_floats * static_cast<Uint32>(sizeof(float));

                SDL_UploadToGPUBuffer(cp, &tb_src, &buf_dst, false);
            }
        }
    }

    // P3 JFA input: tile-res transparency as floats (0.0=opaque .. 1.0=open).
    // Convert uint8 bytes → float so the seed shader can read directly.
    if (trans_storage_ && xfer_trans_f_
        && static_cast<Uint32>(transparency.size()) >= pixel_count) {
        void* mapped = SDL_MapGPUTransferBuffer(dev, xfer_trans_f_, true);
        if (mapped) {
            float* fdst = static_cast<float*>(mapped);
            for (Uint32 i = 0; i < pixel_count; ++i) {
                fdst[i] = static_cast<float>(transparency[i]) / 255.0f;
            }
            SDL_UnmapGPUTransferBuffer(dev, xfer_trans_f_);

            SDL_GPUTransferBufferLocation tb_src{};
            tb_src.transfer_buffer = xfer_trans_f_;
            tb_src.offset = 0;

            SDL_GPUBufferRegion buf_dst{};
            buf_dst.buffer = trans_storage_;
            buf_dst.offset = 0;
            buf_dst.size = pixel_count * static_cast<Uint32>(sizeof(float));

            SDL_UploadToGPUBuffer(cp, &tb_src, &buf_dst, false);
        }
        // populated_ flips here: trans_storage_ is the JFA seed input, so once it
        // lands the GPU SDF pass has valid data to consume. begin_lighting_frame()
        // exposes sdf_map_w/h from this frame onward.
        populated_ = true;
        runtime_w_ = runtime_w;
        runtime_h_ = runtime_h;
    }

    // P3.3: sky_vis_tex_ deleted — shader reads from skyvis_storage_ buffer, not a texture.
    // The xfer_sky_vis_ transfer buffer is retained for the float conversion path below.

    // Sky-vis as a fragment storage buffer of floats (1.0=open, 0.0=roofed).
    // The shader reads SkyVisBuf, not the R8 texture, because sampler-texture
    // Load returns 0 on Metal. Convert the uint8 bytes (0/255) → float here.
    if (skyvis_storage_ && xfer_skyvis_f_ && static_cast<Uint32>(sky_vis.size()) >= pixel_count) {
        void* mapped = SDL_MapGPUTransferBuffer(dev, xfer_skyvis_f_, true);
        if (mapped) {
            float* fdst = static_cast<float*>(mapped);
            for (Uint32 i = 0; i < pixel_count; ++i) {
                fdst[i] = static_cast<float>(sky_vis[i]) / 255.0f;
            }
            SDL_UnmapGPUTransferBuffer(dev, xfer_skyvis_f_);

            SDL_GPUTransferBufferLocation tb_src{};
            tb_src.transfer_buffer = xfer_skyvis_f_;
            tb_src.offset = 0;

            SDL_GPUBufferRegion buf_dst{};
            buf_dst.buffer = skyvis_storage_;
            buf_dst.offset = 0;
            buf_dst.size = pixel_count * static_cast<Uint32>(sizeof(float));

            SDL_UploadToGPUBuffer(cp, &tb_src, &buf_dst, false);
        }
    }

}

} // namespace lighting
