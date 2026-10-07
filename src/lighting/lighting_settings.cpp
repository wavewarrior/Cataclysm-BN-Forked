#include "lighting/lighting_settings.h"

// The knob table and the pure helpers of `lighting/lighting_settings.h`.
//
// This TU includes `sdl_lighting_devui.h` on purpose: the table's plain-address
// destinations point at the legacy knob globals that later tickets move group by
// group, so the addresses are always the live ones. The header stays SDL-free.

#include "lighting/rmlui_layer.h"
#include "sdl_lighting_devui.h"

#include <algorithm>
#include <type_traits>

namespace lighting
{

auto lighting_knob_table() -> std::span<const knob_entry>
{
    // clang-format off
    static const knob_entry table[] = {
        // ---- debug_params core, owned by lighting_settings::debug ----
        { "vis_curve", knob_kind::value, knob_file | knob_panel, &debug_params::vis_curve, knob_range{ 0.f, 4.f, 0.05f }, std::nullopt },
        { "vis_radius", knob_kind::value, knob_file | knob_panel, &debug_params::vis_radius, knob_range{ 0.f, 40.f, 0.5f }, std::nullopt },
        { "ao_strength", knob_kind::value, knob_file | knob_panel, &debug_params::ao_strength, knob_range{ 0.f, 1.f, 0.01f }, std::nullopt },
        { "ramp_enable", knob_kind::value, knob_file | knob_panel, &debug_params::ramp_enable, knob_range{ 0.f, 1.f, 0.01f }, std::nullopt },
        { "shadow_mask_str", knob_kind::value, knob_file | knob_panel, &debug_params::shadow_mask_str, knob_range{ 0.f, 1.f, 0.01f }, std::nullopt },
        { "sun_scale", knob_kind::value, knob_file | knob_keys | knob_panel, &debug_params::sun_scale, knob_range{ 0.f, 4.f, 0.02f }, knob_range{ 0.f, 10.f, 0.1f } },
        { "sky_scale", knob_kind::value, knob_file | knob_keys | knob_panel, &debug_params::sky_scale, knob_range{ 0.f, 4.f, 0.02f }, knob_range{ 0.f, 10.f, 0.1f } },
        { "emitter_scale", knob_kind::value, knob_keys | knob_panel, &debug_params::emitter_scale, knob_range{ 0.f, 4.f, 0.02f }, knob_range{ 0.f, 10.f, 0.1f } },
        { "gi_strength", knob_kind::value, knob_file | knob_keys | knob_panel, &debug_params::gi_strength, knob_range{ 0.f, 3.f, 0.02f }, knob_range{ 0.f, 2.f, 0.05f } },
        { "cloud_strength", knob_kind::value, knob_file | knob_panel, &debug_params::cloud_strength, knob_range{ 0.f, 1.f, 0.01f }, std::nullopt },
        { "nrm_amount", knob_kind::value, knob_file | knob_panel, &debug_params::nrm_amount, knob_range{ 0.f, 10.f, 0.1f }, std::nullopt },
        { "guard_amount", knob_kind::value, knob_file | knob_panel, &debug_params::guard_amount, knob_range{ 0.f, 1.f, 0.01f }, std::nullopt },
        { "portal_dirs", knob_kind::value, knob_file | knob_panel, &debug_params::portal_dirs, knob_range{ 1.f, 32.f, 1.f }, std::nullopt },
        { "portal_reach", knob_kind::value, knob_file | knob_panel, &debug_params::portal_reach, knob_range{ 1.f, 20.f, 0.5f }, std::nullopt },
        { "flicker_gain", knob_kind::value, knob_file | knob_panel, &debug_params::flicker_gain, knob_range{ 0.f, 1.f, 0.01f }, std::nullopt },
        { "shadow_steps", knob_kind::value, knob_file | knob_panel, &debug_params::shadow_steps, knob_range{ 1.f, 64.f, 1.f }, std::nullopt },
        { "max_shadow_k", knob_kind::value, knob_file | knob_panel, &debug_params::max_shadow_k, knob_range{ 1.f, 16.f, 1.f }, std::nullopt },
        { "gi_bilat", knob_kind::value, knob_file | knob_panel, &debug_params::gi_bilat, knob_range{ 0.f, 1.f, 0.01f }, std::nullopt },
        { "light_eps", knob_kind::value, knob_file | knob_panel, &debug_params::light_eps, knob_range{ 0.f, 0.05f, 0.001f }, std::nullopt },
        { "dither_amt", knob_kind::value, knob_keys | knob_panel, &debug_params::dither_amt, knob_range{ 0.f, 2.f, 0.01f }, knob_range{ 0.f, 1.f, 0.1f } },
        { "dither_bands", knob_kind::value, knob_keys | knob_panel, &debug_params::dither_bands, knob_range{ 1.f, 32.f, 1.f }, knob_range{ 1.f, 16.f, 1.f } },
        // The key channel for the debug mode is F7's cycle, not F8/F9 stepping.
        { "debug_mode", knob_kind::mode, knob_keys | knob_panel, &debug_params::debug_mode, std::nullopt, std::nullopt, "dbg_mode_idx" },
        // ---- legacy globals; the owning ticket moves them into settings ----
        { "gi_albedo", knob_kind::value, knob_file | knob_panel, &g_gi_albedo, knob_range{ 0.f, 1.f, 0.01f }, std::nullopt },
        { "gi_feedback", knob_kind::value, knob_file | knob_panel, &g_gi_feedback, knob_range{ 0.f, 0.9f, 0.01f }, std::nullopt },
        { "shaft_intensity", knob_kind::value, knob_file | knob_panel, &g_shaft_intensity, knob_range{ 0.f, 2.f, 0.02f }, std::nullopt },
        { "shaft_length_scale", knob_kind::value, knob_file | knob_panel, &g_shaft_length_scale, knob_range{ 0.f, 3.f, 0.05f }, std::nullopt },
        { "shaft_width", knob_kind::value, knob_file | knob_panel, &g_shaft_width, knob_range{ 0.05f, 2.f, 0.05f }, std::nullopt },
        { "dust_density", knob_kind::value, knob_file | knob_panel, &g_dust_density, knob_range{ 0.f, 2.f, 0.02f }, std::nullopt },
        { "dust_size", knob_kind::value, knob_file | knob_panel, &g_dust_size, knob_range{ 0.f, 2.f, 0.02f }, std::nullopt },
        { "dust_drift", knob_kind::value, knob_file | knob_panel, &g_dust_drift, knob_range{ 0.f, 1.f, 0.01f }, std::nullopt },
        { "glow_intensity", knob_kind::value, knob_file | knob_panel, &g_glow_intensity, knob_range{ 0.f, 2.f, 0.02f }, std::nullopt },
        { "glow_radius", knob_kind::value, knob_file | knob_panel, &g_glow_radius, knob_range{ 0.1f, 3.f, 0.05f }, std::nullopt },
        { "glow_saturation", knob_kind::value, knob_file | knob_panel, &g_glow_saturation, knob_range{ 0.f, 3.f, 0.05f }, std::nullopt },
        { "sun_arrow", knob_kind::toggle, knob_file | knob_panel, &g_sun_arrow, std::nullopt, std::nullopt },
        { "sky_sun_enable", knob_kind::toggle, knob_file | knob_panel, &g_sky_sun_enable, std::nullopt, std::nullopt },
        { "shaft_enable", knob_kind::toggle, knob_file | knob_panel, &g_shaft_enable, std::nullopt, std::nullopt },
        { "dust_enable", knob_kind::toggle, knob_file | knob_panel, &g_dust_enable, std::nullopt, std::nullopt },
        { "glow_enable", knob_kind::toggle, knob_file | knob_panel, &g_glow_enable, std::nullopt, std::nullopt },
        { "gi_enable", knob_kind::toggle, knob_file | knob_panel, &g_gi_enable, std::nullopt, std::nullopt },
        { "force_world_redraw", knob_kind::toggle, knob_file, &g_force_world_redraw, std::nullopt, std::nullopt },
        // ---- destinations that are not a plain scalar ----
        { "rc_readback", knob_kind::pulse, knob_file | knob_panel, knob_special{ knob_special::rc_readback }, std::nullopt, std::nullopt },
        { "force_rc_rebuild", knob_kind::mode, knob_file, knob_special{ knob_special::force_rc_rebuild }, std::nullopt, std::nullopt },
        { "crt_world", knob_kind::toggle, knob_file | knob_panel, knob_special{ knob_special::crt_world }, std::nullopt, std::nullopt },
    };
    // clang-format on
    return std::span<const knob_entry>( table );
}

auto knob_find( std::string_view name ) -> const knob_entry *
{
    const auto tbl = lighting_knob_table();
    const auto it = std::find_if( tbl.begin(), tbl.end(),
    [&]( const knob_entry &e ) {
        return e.name == name;
    } );
    return it == tbl.end() ? nullptr : &( *it );
}

auto knob_apply_file( lighting_settings &s, std::string_view name, float value ) -> bool
{
    const knob_entry *e = knob_find( name );
    if( e == nullptr || ( e->channels & knob_file ) == 0u ) {
        return false;
    }
    // Raw by contract: the file channel is the measurement escape hatch and
    // never clamps; bools come from `value > 0.5`, as the old chain did.
    std::visit( [&s, value]( const auto &d ) {
        using T = std::decay_t< decltype( d ) >;
        if constexpr( std::is_same_v<T, float debug_params::*> ) {
            s.debug.*d = value;
        } else if constexpr( std::is_same_v<T, std::uint32_t debug_params::*> ) {
            // Domain guard, not a range clamp: the lane is a uint and the old
            // chain wrote `static_cast<uint32_t>( std::max( 1.0f, kv ) )`.
            s.debug.*d = static_cast<std::uint32_t>( std::max( 1.0f, value ) );
        } else if constexpr( std::is_same_v<T, float *> ) {
            *d = value;
        } else if constexpr( std::is_same_v<T, bool *> ) {
            *d = value > 0.5f;
        } else if constexpr( std::is_same_v<T, knob_special> ) {
            switch( d.which ) {
                case knob_special::force_rc_rebuild:
                    // 2 = exactly one rebuild on the next frame (leaves
                    // continuous forcing off), as the old two-bool decode did.
                    s.pulses.force_rebuild = value > 1.5f
                                             ? force_rebuild_mode::once
                                             : ( value > 0.5f ? force_rebuild_mode::every_frame
                                                               : force_rebuild_mode::none );
                    break;
                case knob_special::rc_readback:
                    s.pulses.rc_readback = value > 0.5f;
                    break;
                case knob_special::crt_world:
                    // Lives in the RmlUi layer, whose header pulls in SDL; the
                    // table names it, the adapter writes it.
                    rmlui_layer::crt().crt_world = value > 0.5f;
                    break;
            }
        }
    }, e->dest );
    return true;
}

auto knob_key_step( const knob_range &range, float cur, float delta ) -> float
{
    return delta < 0.0f
           ? std::max( range.min, cur - range.step )
           : std::min( range.max, cur + range.step );
}

auto knob_reconcile_int_proxy( const int_proxy_state &s, int store_now, int store_clamped )
-> int_proxy_result
{
    // The panel wins only when ITS widget moved. Otherwise the storage won
    // (something outside the panel wrote it) and the proxy follows it — which is
    // the fix for `shadow_steps`, whose `last` was never assigned and so let the
    // proxy overwrite a file write every frame.
    if( s.proxy != s.last ) {
        return { store_clamped, store_clamped, true };
    }
    return { store_now, store_now, false };
}

} // namespace lighting