# Glossary

## Rendering & lighting

**Reality bubble**
The square region of submaps around the avatar that is loaded into the local map and simulated;
lighting fields (SDF, sky access, GI) are built over it in bubble coordinates.
_Avoid_: view, bubble (bare), loaded area

**Emitter**
A world-position light source gathered each frame from the reality bubble and uploaded to the
emitter buffer that sprite shading and GI read.
_Avoid_: light source, lamp

**Shade position**
The world point at which a sprite pixel's lighting is evaluated: the pixel's own map position for
ordinary sprites, the centre of the base tile for tall sprites (trees, walls), snapped to the
art's texel grid.
_Avoid_: sample point, light position

**Vision carve**
The per-pixel dimming applied inside tiles the player can see, following the line of sight from
the player through the sub-tile distance field, so a corner cutting sight reads as a smooth curve
instead of a tile staircase.
_Avoid_: shadow ray, LOS fade

**Structure rebuild**
A frame on which the cached lighting fields (distance field, sky access, and GI when enabled) are
recomputed because the world's structure changed: terrain, z-level, a bubble shift, or camera
drift. Frames that reuse those caches are not rebuilds.
_Avoid_: dirty, invalidate (as nouns)

## Vehicles

**Heading**
The direction a vehicle currently points.
_Avoid_: facing, face, direction

**Steer target**
The heading a vehicle will turn to once it moves.
_Avoid_: turn_dir, intended heading, turn target

**Pre-steer**
A steer target set while the vehicle is stopped; it changes nothing until the vehicle moves.
_Avoid_: stationary turn

**Driven vehicle**
The vehicle whose controls the avatar is operating, in person or by remote.
_Avoid_: controlled vehicle, player vehicle

**Partner-driven vehicle**
In co-op, the vehicle whose controls the other player is operating.
_Avoid_: remote vehicle, proxy vehicle

**Rolling**
A vehicle with any nonzero speed.
_Avoid_: moving (ambiguous with grab/drag), in motion

**Coasting**
A rolling vehicle that no one is driving.
_Avoid_: freewheeling, drifting
