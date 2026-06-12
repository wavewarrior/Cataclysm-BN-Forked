#pragma once

// High-fidelity rain effect — GPU particle system for falling droplets,
// impact splashes, and persistent wet-spot accumulation via a reusable
// ping-pong splat map texture.
//
// Driven from refresh_display() (sdl_render_frame.cpp) between the world
// pass and tonemap resolve. Reads weather state each frame to determine
// intensity, wind direction, and fade rate.

#include <cstdint>
#include <vector>

#include <SDL3/SDL_gpu.h>

namespace lighting
{

class gpu_device;

// One falling rain droplet (CPU-side particle).
struct rain_droplet {
    float screen_x = 0.f;   // pixel-space X position
    float screen_y = 0.f;   // pixel-space Y position
    float vel_x = 0.f;      // horizontal velocity (wind drift) px/frame
    float vel_y = 0.f;      // vertical velocity px/frame
    float opacity = 1.0f;   // alpha for rendering
    float length = 1.0f;    // streak length multiplier
};

// One impact splash particle (CPU-side, short-lived).
struct rain_splash {
    float x = 0.f;          // screen-space X at ground plane
    float y = 0.f;          // screen-space Y at ground plane
    float intensity = 1.0f; // brightness contribution to splat map
    uint32_t age = 0u;      // frames alive
    uint32_t max_age = 10u; // lifetime in frames before removal
};

// Per-frame rain parameters driven by weather state.
struct rain_params {
    bool   active = false;       // true when current weather has rains=true
    float  intensity = 0.f;      // 0..2+ scale (light_drizzle=0.25, heavy_rain=2.0)
    float  wind_angle = 0.f;     // degrees from north (0=north), clockwise positive
    float  fade_rate = 0.997f;   // per-frame splat decay multiplier (<1 = decay)
    uint32_t droplet_cap = 2048; // max concurrent droplets
};

class rain_effect
{
    public:
        rain_effect() = default;
        rain_effect( const rain_effect & ) = delete;
        rain_effect &operator=( const rain_effect & ) = delete;
        ~rain_effect();

        // Build pipelines + splat textures. hdr_format is the world_target
        // format (RGBA16F for HDR). screen_w/screen_h are the swapchain dims
        // used to size droplet spawn area and ground plane threshold.
        bool init( gpu_device &dev, SDL_GPUTextureFormat hdr_format,
                   std::uint32_t screen_w, std::uint32_t screen_h );

        void shutdown() noexcept;

        bool ready() const noexcept {
            return droplet_pipeline_ != nullptr && splat_pipeline_
                   && splat_a_ && splat_b_;
        }

        // Update internal particle state and record all rain draws.
        // `cb` is the current command buffer, `world_tex` is the HDR world
        // target that droplets are drawn onto, `params` carries weather data.
        void record( SDL_GPUCommandBuffer *cb, SDL_GPUTexture *world_tex,
                     std::uint32_t world_w, std::uint32_t world_h,
                     const rain_params &params );

    private:
        // Spawn new droplets at the top of screen based on intensity.
        void spawn_droplets( float intensity, float wind_angle,
                             std::uint32_t screen_w, std::uint32_t screen_h );

        // Update all droplet positions; remove off-screen ones.
        void update_droplets( std::uint32_t screen_w, std::uint32_t screen_h );

        // Spawn splash particles when droplets hit the ground plane.
        void spawn_splashes();

        // Update splash ages; remove expired ones.
        void update_splashes();

        // GPU state --------------------------------------------------------
        gpu_device              *dev_ = nullptr;
        SDL_GPUTextureFormat     hdr_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;

        // Droplet rendering pipeline (alpha-blended onto world_target).
        SDL_GPUShader           *droplet_vert_ = nullptr;
        SDL_GPUShader           *droplet_frag_ = nullptr;
        SDL_GPUGraphicsPipeline *droplet_pipeline_ = nullptr;

        // Splat fade + accumulation pipeline (fullscreen, ping-pong textures).
        SDL_GPUShader           *splat_vert_ = nullptr;
        SDL_GPUShader           *splat_frag_ = nullptr;
        SDL_GPUGraphicsPipeline *splat_pipeline_ = nullptr;

        // Ping-pong splat map textures (512x512 RGBA8).
        SDL_GPUTexture          *splat_a_ = nullptr;
        SDL_GPUTexture          *splat_b_ = nullptr;
        std::uint32_t            splat_w_ = 512u;
        std::uint32_t            splat_h_ = 512u;

        // Persistent GPU buffers for droplet instance upload (reused each frame).
        SDL_GPUTransferBuffer   *inst_transfer_buf_ = nullptr;
        SDL_GPUBuffer           *inst_storage_buf_  = nullptr;
        std::size_t              inst_capacity_      = 0u;

        // Linear sampler for splat texture reads.
        SDL_GPUSampler          *splat_sampler_ = nullptr;

        // Particle pools ---------------------------------------------------
        static constexpr int MAX_DROPLETS = 2048;
        static constexpr int MAX_SPLASHES = 512;
        std::vector<rain_droplet> droplets_;
        std::vector<rain_splash> splashes_;

        // Reusable splash data arrays for the fragment shader uniforms.
        float splash_x_[512];
        float splash_y_[512];
        float splash_intensity_[512];

        // Screen dimensions last recorded (for ground plane threshold).
        std::uint32_t screen_w_ = 0u;
        std::uint32_t screen_h_ = 0u;
};

} // namespace lighting
