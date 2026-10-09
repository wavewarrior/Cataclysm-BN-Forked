#include "frame_plan.h"

namespace
{

/// Flags per step: which begin a graphics render pass, which belong to the fused
/// lighting group. Derived from the call sites in `sdl_render_frame.cpp`: the
/// composite, world, tonemap and swapchain steps each reach `SDL_BeginGPURenderPass`
/// (directly or through the sprite batcher); the lighting steps reach compute passes
/// only, which the assemble law deliberately does not cover.
constexpr auto step_flags( frame_step_kind kind ) -> unsigned
{
    const unsigned render = static_cast<unsigned>( frame_step_flag::begins_render_pass );
    const unsigned fused  = static_cast<unsigned>( frame_step_flag::fused_lighting );
    switch( kind ) {
        case frame_step_kind::collector_flush:
        case frame_step_kind::gpu_sdf:
        case frame_step_kind::sky_sun:
        case frame_step_kind::gi:
        case frame_step_kind::gi_feedback:
        case frame_step_kind::rc_readback:
            return fused;
        case frame_step_kind::ui_composite:
        case frame_step_kind::avatar_composite:
        case frame_step_kind::vehicle_composite:
        case frame_step_kind::world_pass:
        case frame_step_kind::tonemap:
        case frame_step_kind::swapchain_composite:
            return render;
        case frame_step_kind::build_lighting:
        case frame_step_kind::assemble:
        case frame_step_kind::menu_background:
        case frame_step_kind::overlays:
            return 0u;
    }
    return 0u;
}

/// Today's phase order, as step kinds. `begin_frame` is a precondition outside the plan.
constexpr std::array<frame_step_kind, 16> default_order {{
        frame_step_kind::build_lighting,
        frame_step_kind::collector_flush,
        frame_step_kind::gpu_sdf,
        frame_step_kind::sky_sun,
        frame_step_kind::gi,
        frame_step_kind::gi_feedback,
        frame_step_kind::rc_readback,
        frame_step_kind::assemble,
        frame_step_kind::menu_background,
        frame_step_kind::overlays,
        frame_step_kind::ui_composite,
        frame_step_kind::avatar_composite,
        frame_step_kind::vehicle_composite,
        frame_step_kind::world_pass,
        frame_step_kind::tonemap,
        frame_step_kind::swapchain_composite,
    }};

/// The ten legacy phase names, in lap order.
constexpr std::array<std::string_view, 10> lap_names {{
        "begin", "build_light", "flush_gather", "assemble", "menu_bg",
        "overlays", "ui_a", "world_w", "tonemap", "swap_b"
    }};

} // namespace

auto frame_lap_names() -> std::span<const std::string_view>
{
    return lap_names;
}

auto frame_lap_count() -> std::size_t
{
    return lap_names.size();
}

auto frame_lap_of( frame_step_kind kind ) -> std::size_t
{
    switch( kind ) {
        case frame_step_kind::build_lighting:
            return 1;
        case frame_step_kind::collector_flush:
        case frame_step_kind::gpu_sdf:
        case frame_step_kind::sky_sun:
        case frame_step_kind::gi:
        case frame_step_kind::gi_feedback:
        case frame_step_kind::rc_readback:
            return 2;
        case frame_step_kind::assemble:
            return 3;
        case frame_step_kind::menu_background:
            return 4;
        case frame_step_kind::overlays:
            return 5;
        case frame_step_kind::ui_composite:
        case frame_step_kind::avatar_composite:
        case frame_step_kind::vehicle_composite:
            return 6;
        case frame_step_kind::world_pass:
            return 7;
        case frame_step_kind::tonemap:
            return 8;
        case frame_step_kind::swapchain_composite:
            return 9;
    }
    return 0;
}

auto frame_lap_name( std::size_t lap ) -> std::string_view
{
    return lap < lap_names.size() ? lap_names[lap] : std::string_view{};
}

auto frame_plan::index_of( frame_step_kind kind ) const -> std::size_t
{
    for( std::size_t i = 0; i < size_; ++i ) {
        if( steps_[i].kind == kind ) {
            return i;
        }
    }
    return size_;
}

auto frame_plan::step( frame_step_kind kind ) const -> const frame_step *   // *NOPAD*
{
    const std::size_t i = index_of( kind );
    return i < size_ ? &steps_[i] : nullptr;
}

auto frame_plan::push_back( frame_step step ) -> frame_plan &   // *NOPAD*
{
    steps_[size_++] = step;
    return *this;
}

auto frame_plan::fused_lighting_members() const -> std::array<frame_step_kind, 6>
{
    std::array<frame_step_kind, 6> members{};
    std::size_t n = 0;
    for( const frame_step & s : steps() ) {
        if( s.has( frame_step_flag::fused_lighting ) && n < members.size() ) {
            members[n++] = s.kind;
        }
    }
    return members;
}

auto default_frame_plan() -> frame_plan
{
    frame_plan plan;
    for( const frame_step_kind kind : default_order ) {
        plan.push_back( {
            .kind = kind,
            .status = frame_step_status::run,
            .flags = step_flags( kind ),
            .reason = {},
        } );
    }
    return plan;
}
