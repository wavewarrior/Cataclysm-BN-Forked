# Procedural normal atlas for sprite lighting

## STATUS

Algorithm **validated offline against the real tileset** (see Evidence). No C++ landed yet.
Prototype: `C:\WORK\nrmproto.py` (generator), `nrmsheet.py` (contact sheet), `nrmcal*.py`
(calibration). Contact sheets in `C:\WORK\lighting_review\11..13-proto-*.png`.

## Problem

Sprite surfaces carry no relief, so light direction is invisible on them. Today
`surface_normal()` (sprite.frag.hlsl:352) derives normals from a 4-tap gradient of
**alpha**, and its own comment concedes the limit: "Interior pixels (a≈1) flatten". Every
BN terrain sprite is a fully-opaque tile, so `dx = dy = 0` and the normal is exactly
`(0,0,1)`. Measured in debug view 9: grass, asphalt, wall and building interior all read
`121.6, 121.6, 243.5` — identical. Relief survives only in a 1-2 texel rim, and `edge`
lerps even that back to flat. So `nrm_amount`, `nrm_relief` and `nrm_elev` all multiply
zero, and sun direction can only ever appear as *cast shadows*, never as shading.

## Technique selection

From the survey (arXiv:2212.09692, "Analysis and Compilation of Normal Map Generation
Techniques for Pixel Art") plus Laigter's implementation notes:

| technique | verdict |
|---|---|
| Hand painting | Best quality, reference standard. ~30k sprites — impossible here. |
| Sobel from colour map | **Rejected.** Survey: "uses information exclusively from edges, resulting in incoherent geometry" — inverted volumes and grooves, because it reads pre-baked shading as height. BN art is heavily pre-shaded. |
| Sobel from hand height map | Needs a hand-painted height map per sprite. Same cost problem. |
| **Beveling (dual-mask EDT)** | **Chosen.** Fully automatic, "can separate the internal shapes of the object, achieving better results with internal contours". |
| Four illumination angles | Sprite Lamp's method. Needs 4 hand-drawn lightings per sprite. |
| Deep generative model | "weak internal geometry on pixel art", trained on non-pixel-art data. |

Beveling, per the paper: two binary masks (alpha silhouette + internal contours) → euclidean
distance transform each → weighted merge → Gaussian → Sobel → normal.

## Three deviations, each forced by measurement on THIS tileset

1. **The external term must be gated off for full tiles.** For a fully-opaque 32x32 sprite
   the alpha EDT degenerates to distance-from-tile-border, stamping an *identical pyramid*
   on every wall and floor tile — a repeating diamond crease locked to the tile grid, and
   one pyramid per tile up a multi-tile wall instead of one gradient. All 12 tiles sampled
   hit this gate, so internal contours carry essentially everything for BN terrain.

2. **No median prefilter, no adaptive percentile.** Both were implemented and measured, and
   both made it *worse*:
   - 3x3 median erased the brick mortar and plank seams that are the entire signal
     (`t_rock_wall` ny std 0.211 → 0.116, visibly flat).
   - A percentile threshold fires on a fixed fraction of pixels *by construction*, even on
     a sprite with no contours, so the EDT invented large smooth domes out of scattered
     speckles — confident nonsense, worse than mush.

3. **A coherence gate replaces threshold tuning.** Dithered concrete saturates any
   per-pixel colour delta (density 0.45-0.60) and yields mush. The gate rejects it *after*
   masking, using a shape statistic instead of a colour constant, so it does not need
   retuning per tileset.

### Coherence statistic: mean non-edge (gap) run length

Measured over 12 UnDeadPeople tiles at threshold 0.14 — clean separation, nothing in the gap:

| class | tiles | gap run |
|---|---|---|
| noise | t_wall, t_wall_w, t_grass, t_door_c | 1.60 – 2.89 |
| structured | t_rock_wall, t_floor, t_sidewalk, t_pavement, t_metal_floor | 4.04 – 23.84 |

Two alternatives were implemented and **measured to fail**:

- **Edge run length** (runs of edge pixels): herringbone plank seams are *diagonal*, giving
  short runs on both axes — scored 2.39, *below* dithered concrete at 3.26. Axis-aligned
  edge runs cannot see diagonal structure.
- **Scale ratio** (edge density native vs 2x box downsample), which is orientation-free and
  looked more principled: structured 0.00-1.35 vs noise 0.02-0.63 — heavily overlapping and
  unusable. Planks scored 0.06, identical to grass noise. Box downsampling *averages*, so it
  erases 1px plank/pavement seams along with the dither; it discriminates thick-vs-thin, not
  structure-vs-noise.

Gap run length has one hole: it *rises* as edges get sparser, so a few isolated speckles
score high. Closed separately by `min_density = 0.05`, not by the statistic.

## Wire design — pinned decisions

### One sampler, double-height page

**Do not add a second sampler.** sprite.frag.hlsl:22-30 records that shadercross @6b06e55c
silently mis-binds sampler textures on Metal — readback proves the upload arrives but
Sample/Load returns all zeros for every fragment. Emitters, SDF and SkyVis were all moved
off textures into storage buffers for exactly this reason: "Atlas (slot 0) is the only
sampler texture that works." A second sampler would also renumber `ShadowMask` t1→t2 and
every storage buffer t2..t6 in both HLSL and C++.

Instead: allocate each atlas page **double height** — colour in the top half, the normal for
each sprite at the *same rect* in the bottom half. Then `normal_uv = uv + float2(0, 0.5)`.

- one sampler, zero binding renumbering, no Metal risk
- no per-instance lane, no packed-UV encoding, no `sprite_instance` growth
- no same-sheet pairing invariant for the packer to break when a sheet fills mid-load
- a uniform offset of `0.0` disables the whole feature

Rejected alternatives: per-sprite packed normal UV in the free `extrude_pad` lane (needs an
`allocate_pair` guarantee the packer must hold forever — silently breaks when a sheet fills);
Texture2DArray at slot 0 (changes the texture type at the one binding known to be fragile,
and touches every sample site).

**`atlas_h` MUST mean the full GPU texture height.** The sprite UV math divides `srcrect` by
the `atlas_w/atlas_h` returned from `find_gpu_texture_full` (dynamic_atlas.h:81). If the GPU
mirror doubles to 4096x8192 while that call still reports 4096, every colour V silently
halves. Report the real GPU height and colour UVs stay correct by construction. Derive
colour height as `total / 2` and keep `total` under the device max 2D size — never exceed the
limit to gain the extra half.

### Cached sidecar, not per-boot recompute

EDT + Gaussian + Sobel over every page every startup is a visible launch regression. Cache to
a sidecar keyed on `hash(source PNG + params)`, regenerated on mismatch. Also makes the
output inspectable while tuning.

## What this does and does NOT deliver

**Delivers** (proven, sheet `13-proto-bevel-gated.png`): real per-material relief on
structured art. `t_rock_wall` bricks read as rounded blocks, `t_floor` herringbone planks as
individual planks, both with clearly different response across lit N/S/E/W. This art renders
perfectly flat today.

**Refuses**: noise-dithered concrete comes out uniform flat blue (nx/ny std exactly 0.000)
rather than fabricating relief.

**Does NOT deliver macro facing.** Measured S-lit bottom−top delta ≈ 0.3/255 before the gate
and 0 after. The bevel gives *local* relief only, matching the survey's finding that its
geometry sits near contours "without a good distribution of information along the visible
surfaces". So a building lit from inside/outside still will not show its wall face
brightening with light direction.

That needs the orthogonal **per-sprite facing term** (see
`C:\WORK\lighting_review\FINDINGS-facing-normals.md`): a quad-local vertical fraction varying
from the vertex shader, plus a per-sprite "vertical face" amount driven from terrain flags,
because the shader's only proxy (`is_tall`, from sprite art height) is false for every wall —
`t_wall` is 32x32 in `normal_terrain.png` with no size override.

**The two compose and both are needed.** The atlas replaces the *source* of the normal; the
facing amount supplies the per-sprite "this is a vertical surface" signal. Neither is
redundant if the other lands.

## Evidence

- `11-proto-bevel-normals.png` — first pass, threshold 0.14. Brick and planks excellent;
  dithered walls mush.
- `12-proto-bevel-adaptive.png` — median + adaptive percentile. Worse: structure erased,
  invented blobs. Kept as the record of a refuted approach.
- `13-proto-bevel-gated.png` — final: coherence gate. Structure kept, dither flat.
