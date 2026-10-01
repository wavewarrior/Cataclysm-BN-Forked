# Cataclysm-BN-Forked

A turn-based survival roguelike with a chunked tile map and a GPU lighting renderer. This glossary names the concepts used to talk about per-z-level map caches and their upkeep.

## Language

**Level cache**:
The per-z-level bundle of derived map data (transparency, floor, outside, seen, lightmap, visibility, suspension, vehicle sub-caches) built from terrain, furniture, vehicles and lights.
_Avoid_: map cache (ambiguous with the whole set across z-levels)

**Level cache freshness**:
Whether a Level cache reflects the current world and the current viewer, and which parts must be rebuilt when it does not.
_Avoid_: cache invalidation, dirty tracking

**Change kind**:
One of a small closed set of facts about the world or viewer that affects Level cache freshness, such as terrain changed, light changed, vehicle moved, player moved, z-level changed, or map shifted.
_Avoid_: dirty flag, invalidate call

**Structure rebuild**:
The recomputation of a lighting field because world structure changed, as opposed to a refresh caused by the viewer moving.
_Avoid_: dirty/invalidate (as nouns)

**View stale**:
The condition that the viewer's position, z-level or inputs have changed since the Level cache was last built for them, so a rebuild must run before visibility is read exactly.
_Avoid_: visibility dirty aggregate

**Geometry-only visibility**:
The optimistic answer visibility queries give while a level's visibility is stale: line of sight is checked, light level is not.
_Avoid_: dirty read
