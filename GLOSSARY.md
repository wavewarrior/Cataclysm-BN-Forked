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
One of a small closed set of facts about the world or viewer that affects Level cache freshness, such as terrain changed, light changed, vehicle moved, player moved, z-level changed, or map shifted. A kind carries the fact, not its consequences; deriving consequences is the module's job.
_Avoid_: dirty flag, invalidate call

**Structure rebuild**:
The recomputation of a lighting field because world structure changed, as opposed to a refresh caused by the viewer moving.
_Avoid_: dirty/invalidate (as nouns)

**View stale**:
The condition that the viewer's position, z-level or inputs have changed since the Level cache was last built for them.
_Avoid_: visibility dirty aggregate, needs-rebuild

**Property diff**:
The set of freshness-relevant terrain and furniture property changes that the module computes internally from an old/new state pair.
_Avoid_: change flags, diff fields

**Stamp door**:
A named entry point through which a completed builder reports the freshness state it established for its own cache content.
_Avoid_: raw bit write, builder stamp (as a verb)

**Rebuild plan**:
The immutable value the freshness module derives once per consumption point, saying which parts of which levels to rebuild and how the lightmap participates in this refresh.
_Avoid_: refresh decision, dirty list

**The single door**:
The invariant that every influence on Level cache freshness enters the module through a public door: a change kind, a stamp door, or the declared escape hatch.
_Avoid_: closed surface, invalidation discipline

**Geometry-only visibility**:
The optimistic answer visibility queries give while a level's visibility is stale: line of sight is checked, light level is not.
_Avoid_: dirty read

## Rendering & lighting

**Reality bubble**:
The square region of submaps around the avatar that is loaded into the local map and simulated;
lighting fields (SDF, sky access, GI) are built over it in bubble coordinates.
_Avoid_: view, bubble (bare), loaded area

**Emitter**:
A world-position light source gathered each frame from the reality bubble and uploaded to the
emitter buffer that sprite shading and GI read.
_Avoid_: light source, lamp

**Shade position**:
The world point at which a sprite pixel's lighting is evaluated: the pixel's own map position for
ordinary sprites, the centre of the base tile for tall sprites (trees, walls), snapped to the
art's texel grid.
_Avoid_: sample point, light position

**Vision carve**:
The per-pixel dimming applied inside tiles the player can see, following the line of sight from
the player through the sub-tile distance field, so a corner cutting sight reads as a smooth curve
instead of a tile staircase.
_Avoid_: shadow ray, LOS fade

## Vehicles

**Heading**:
The direction a vehicle currently points.
_Avoid_: facing, face, direction

**Steer target**:
The heading a vehicle will turn to once it moves.
_Avoid_: turn_dir, intended heading, turn target

**Pre-steer**:
A steer target set while the vehicle is stopped; it changes nothing until the vehicle moves.
_Avoid_: stationary turn

**Driven vehicle**:
The vehicle whose controls the avatar is operating, in person or by remote.
_Avoid_: controlled vehicle, player vehicle

**Partner-driven vehicle**:
In co-op, the vehicle whose controls the other player is operating.
_Avoid_: remote vehicle, proxy vehicle

**Rolling**:
A vehicle with any nonzero speed.
_Avoid_: moving (ambiguous with grab/drag), in motion

**Coasting**:
A rolling vehicle that no one is driving.
_Avoid_: freewheeling, drifting

## Co-op

**World event**:
A replicated edit to the shared world in co-op: a terrain or furniture replacement, or a field appearing, changing intensity, or expiring.
_Avoid_: delta, mutation, sync entry

**World event interpreter**:
The single owner of what each world event means: how it is applied to a map, how it is undone, and how it is written to and read from the wire.
_Avoid_: event handler, delta codec

**Rollback**:
Undoing recent world events in reverse order to return a map to an earlier tick after the two players' worlds disagree.
_Avoid_: rewind, revert
