#pragma once

#include "coop_mutation_log.h"
#include "coop_proto.h"
#include "coordinates.h"

#include <vector>

class JsonIn;
class JsonOut;
class map;

/// A World event as captured by the interpreter at the moment it was applied:
/// every value needed to undo it, stored under an explicit name.  One record
/// type covers all five replicated kinds; the meaning of a slot never depends
/// on the event type, so inverting a record needs no type-driven reinterpretation.
struct coop_recorded_event {
    coop_event_type type = coop_event_type::terrain_changed;
    tripoint_abs_ms pos;
    /// Terrain / furniture events: the id index written and the one it replaced.
    int terrain_or_furniture = 0;
    int old_terrain_or_furniture = 0;
    /// Field events: the field type involved, plus the intensities either side
    /// of the mutation.  `old_intensity` is 0 when no field was present before
    /// (creation) or when an older sender carried no intensity (expiry).
    int field = 0;
    int old_intensity = 0;
    int new_intensity = 0;
};

/// The operation that undoes a recorded event.  Named per operation, not per
/// wire event type: undoing a creation is "remove this field", which is not the
/// same thing as the wire's `field_expired` even though the effect overlaps.
enum class coop_inverse_op : uint8_t {
    none, ///< nothing to undo (non-replicated event types)
    set_terrain,
    set_furniture,
    remove_field,
    add_field,
    set_field_intensity,
};

/// The inverse of a recorded event, ready to be applied to a map.
struct coop_inverted_event {
    coop_inverse_op op = coop_inverse_op::none;
    tripoint_abs_ms pos;
    int terrain_or_furniture = 0;
    int field = 0;
    int intensity = 0;
};

/// The World event interpreter: the single place that knows what each replicated
/// World event means, how to apply it, how to record it for reversal, how to
/// invert the record, and how the delta event array looks on the wire.
///
/// Owns the delta event array of the A4 sync packet only; every other sync key is
/// handled by `coop_client::apply_sync` itself.
struct coop_world_event_interpreter {
        /// Is this event type part of the replicated (delta-streamed) set?
        /// Single source of truth for the five kinds; `coop_collect_streamable`
        /// filters through this.
        static auto is_replicated( coop_event_type type ) -> bool;

        /// Reads the field intensity off a wire event: it travels in `creature_id`
        /// (new intensity for created/changed, pre-expiry intensity for expired).
        /// Readers go through here instead of touching `creature_id` directly.
        static auto field_intensity_carrier( const coop_world_event& ev ) -> int;

        /// Apply a World event to `target`, capturing the pre-mutation state.
        /// Non-replicated event types are a no-op on the map; callers gate on
        /// `is_replicated` before recording the result for rollback.
        static auto apply( map& target, const coop_world_event& ev ) -> coop_recorded_event;

        /// The inverse of a recorded event.  Pure.
        static auto invert( const coop_recorded_event& recorded ) -> coop_inverted_event;

        /// Apply an inverted event, restoring the state `recorded` captured.
        static auto apply_inverse( map& target, const coop_inverted_event& inv ) -> void;

        /// Write the delta event array: `"events": [ { "ev", "x", "y", "z", "v",
        /// ( "cid" only when non-zero ) }, … ]`.  Byte-compatible with the
        /// hand-rolled server serialiser it replaces.
        static auto write_events( JsonOut& out, const std::vector<coop_world_event>& events )
        -> void;

        /// Read one event object of that array (the caller owns the enclosing
        /// array iteration).  Absent members default to 0, mirroring the
        /// hand-rolled client parser it replaces.
        static auto read_event( JsonIn& in ) -> coop_world_event;
};
