#include "sound_visualization.h"

#include "debug.h"
#include "game.h"
#include "lighting/dev_test_lights.h"

// NOLINTNEXTLINE(cata-text-style)
#define dbg( x ) DebugLogFL( ( x ), DC::Main )

namespace sfx
{

namespace
{

constexpr auto MAX_ACTIVE_PULSES = std::size_t{ 32 };

} // namespace

auto emit_sound_pulse( const tripoint_bub_ms &source, float volume ) -> void
{
    if( g == nullptr ) {
        return;
    }

    auto &pulses = dev_test_lights::sound_pulses;
    if( pulses.size() >= MAX_ACTIVE_PULSES ) {
        pulses.erase( pulses.begin() );
    }

    pulses.push_back( dev_test_lights::sound_pulse{
        .z = source.z(),
        .volume = volume,
        .spawn_s = dev_test_lights::pulse_now_s(),
        .source = source,
    } );

    dbg( DL::Info ) << "[sound_vis] emit_sound_pulse src=(" << source.x() << "," << source.y()
                    << ") vol=" << volume
                    << " total=" << pulses.size();
}

auto sound_pulses_active() -> bool
{
    return !dev_test_lights::sound_pulses.empty();
}

} // namespace sfx
