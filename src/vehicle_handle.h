#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class vehicle;

/// Generation-checked reference to a vehicle. Cheap to copy and safe to store:
/// resolving a handle whose vehicle has been freed yields nullptr instead of a
/// dangling pointer. generation == 0 is the never-valid sentinel.
struct vehicle_handle {
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;

    auto operator<=>( const vehicle_handle & ) const = default; // *NOPAD*
    auto is_set() const -> bool {
        return generation != 0;
    }
};

/// Process-wide slot table mapping handles to live vehicles. A vehicle enters in
/// its constructor and leaves in its destructor, so every free path — including
/// MAPBUFFER submap eviction — invalidates every handle to it.
class vehicle_registry
{
    public:
        static auto get() -> vehicle_registry &; // *NOPAD*

        auto attach( vehicle &veh ) -> vehicle_handle;
        auto detach( vehicle_handle handle ) -> void;
        auto resolve( vehicle_handle handle ) const -> vehicle *; // *NOPAD*
        auto live_count() const -> std::size_t;

    private:
        struct slot_entry {
            vehicle *veh = nullptr;
            std::uint32_t generation = 0;
        };
        std::vector<slot_entry> slots_;
        std::vector<std::uint32_t> free_slots_;
};

/// Convenience wrapper used by the index resolvers.
inline auto resolve_vehicle( vehicle_handle handle ) -> vehicle * // *NOPAD*
{
    return vehicle_registry::get().resolve( handle );
}

template<>
struct std::hash<vehicle_handle> {
    auto operator()( const vehicle_handle &h ) const noexcept -> std::size_t {
        return ( static_cast<std::size_t>( h.generation ) << 32 ) ^ h.slot;
    }
};
