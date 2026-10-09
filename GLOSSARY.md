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

## Frame assembly

**Frame assembly**:
The module that produces one rendered frame: it derives the frame's inputs once, plans which steps run and why, executes them in order, and reports what happened.
_Avoid_: render loop, frame orchestration

**Frame inputs**:
The by-value snapshot of everything one frame reads (camera, time and weather values, the knobs that decide which steps run, which passes are ready, the rebuild plan, the previous frame's history), built once after the frame's drawable is acquired.
_Avoid_: frame state, render context

**Frame camera**:
The camera of one frame, derived once: the integer tile rectangle lighting is built over, the float offset and tile size passes draw with, and whether the frame is cropped to the screen or covers the whole reality bubble.
_Avoid_: viewport, camera offset (that is one part of it)

**Frame plan**:
The ordered, named steps of one frame, each marked run or skip with a reason, computed as a pure function of the frame inputs. Not the Rebuild plan, which says which level caches to rebuild.
_Avoid_: pass list, phase list, frame graph

**Frame report**:
What a frame actually did: each step's status, reason and duration, the gates that could only be resolved mid-frame, any capture written, and why no frame was produced when none was.
_Avoid_: frame log, perf stats

**Frame history**:
What the previous frames established that this frame's rebuild decision compares against: which world state the lighting fields were last built for.
_Avoid_: cache state, last-frame statics

**Plan law**:
An ordering or dependency rule about a frame plan that a test asserts without a GPU, such as the step that stamps lighting inputs preceding every step that begins a pass.
_Avoid_: invariant (too general)

**Capture request**:
The ask that a frame's final image and the matching map state be written to files, however it was made: by the driver, a key, a trigger file or an environment setting.
_Avoid_: screenshot (the in-game screenshot is the state view, a different image)

**Lighting settings**:
The single owner of every lighting knob's value, with its range and the channels allowed to write it.
_Avoid_: debug params (only the part uploaded to the GPU), knob globals

**Knob table**:
The list of every lighting knob with its name, type, default, range and kind, from which the runtime channels and tests read.
_Avoid_: knob registry, settings map

**Knob channel**:
One of the three ways a knob is set while the game runs: the F4 panel, the function keys, or the knob file.
_Avoid_: input path, writer

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

## Agent playtesting

**Trial**:
The declarative definition of a playtest: the save fixture to load, the pinned seed, start date and time-of-day, mod set, an optional scene, an optional scripted action prefix, and the end conditions.
_Avoid_: scenario (collides with the game's start scenario, `class scenario`), test case, script (ambiguous with `.vv` files and Lua)

**Episode**:
One execution of a Trial, with its own private user directory and its own report.
_Avoid_: run, session

**Scene**:
A Lua setup fixture that a Trial may reference, used to place terrain, monsters and items before or during an Episode.
_Avoid_: map setup, fixture (a fixture here means a save)

**Driver**:
The mode of the game binary (`--driver-fd`) that serves one JSON request line in, one observation line out, over an inherited file descriptor, so a supervisor can drive an Episode without a window or a keyboard.
_Avoid_: server, harness (the Windows `vv.py` tool is the harness)

**Supervisor**:
`bnplay`, the external Deno tool that boots an Episode's driver in a private clone, watches it from outside the game, evaluates oracles and writes the report. Isolation between Episodes is enforced here, not by the game. Its handle for a running Episode is a session id.
_Avoid_: runner, daemon (the daemon is only the supervisor's resident process)

**Fixture**:
A world save kept in the fixture library (a gitignored directory) that a Trial names and every Episode clones into its own user directory.
_Avoid_: save (the user's own saves are never touched), scene (a Scene is Lua)

**Fixture baseline**:
The game-log lines a fixture produces on a clean boot-and-idle after the driver reports ready, recorded once so an Episode fails only on error lines that are new. Refreshed whenever the fixture or its mod set changes.
_Avoid_: golden log, snapshot

**Observation**:
The compact JSON a driver answers each request with: turn, whether time passed, new messages, vitals and an `outcome`.
_Avoid_: state (that is one command), response (it may be a protocol error)

**Oracle**:
A check the supervisor evaluates over an Episode's observations or captured frames and reports as pass, fail or warn: the built-in checks, a declared predicate, or a capture oracle (`paired_null`, `diff_vs_null`, `triplet`).
_Avoid_: assertion, test

**Transcript**:
The JSONL record of every request and response of an Episode; the report points at a failing request in it, and it is the repro because same-seed Episodes are not guaranteed to replay.
_Avoid_: log (that is the game's `debug.log`)

## Dev loop

**Leaf edit**:
A change to one source file that no other translation unit depends on, so the rebuild is that unit plus the link.
_Avoid_: small change, incremental build

**Hub header**:
A project header reached, directly or through other headers, by a large share of translation units, so touching it rebuilds much of the tree. Its cost is its reach weighted by how often it changes.
_Avoid_: god header, core header

**Fast preset**:
A macOS build preset tuned for the edit loop, separate from the performance-representative `osx-arm-slim`.
_Avoid_: debug build (collides with the Debug build type)

**Authoritative binary**:
The executable the most recent build of a preset produced, as opposed to older copies of the same program lying around.
_Avoid_: repo-root binary, installed binary

**Build queue**:
The machine-wide ordering that admits builds so concurrent sessions do not starve each other.
_Avoid_: build lock, job pool
