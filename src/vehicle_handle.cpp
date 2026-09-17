#include "vehicle_handle.h"

auto vehicle_registry::get() -> vehicle_registry &
{
    static vehicle_registry instance;
    return instance;
}

auto vehicle_registry::attach( vehicle &veh ) -> vehicle_handle
{
    if( !free_slots_.empty() ) {
        const std::uint32_t slot = free_slots_.back();
        free_slots_.pop_back();
        slots_[slot].veh = &veh;
        return vehicle_handle{ .slot = slot, .generation = slots_[slot].generation };
    }
    const std::uint32_t slot = static_cast<std::uint32_t>( slots_.size() );
    slots_.push_back( slot_entry{ .veh = &veh, .generation = 1 } );
    return vehicle_handle{ .slot = slot, .generation = 1 };
}

auto vehicle_registry::detach( vehicle_handle handle ) -> void
{
    if( handle.slot >= slots_.size() ) {
        return;
    }
    slot_entry &entry = slots_[handle.slot];
    if( entry.generation != handle.generation ) {
        // Already stale — a double-detach must not corrupt a live vehicle's slot.
        return;
    }
    entry.veh = nullptr;
    ++entry.generation;
    free_slots_.push_back( handle.slot );
}

auto vehicle_registry::resolve( vehicle_handle handle ) const -> vehicle *
{
    if( handle.generation == 0 || handle.slot >= slots_.size() ) {
        return nullptr;
    }
    const slot_entry &entry = slots_[handle.slot];
    if( entry.generation != handle.generation ) {
        return nullptr;
    }
    return entry.veh;
}

auto vehicle_registry::live_count() const -> std::size_t
{
    return static_cast<std::size_t>( slots_.size() - free_slots_.size() );
}
