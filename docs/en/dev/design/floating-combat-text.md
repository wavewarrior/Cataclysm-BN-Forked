# Floating Combat Text Design Plan

## Overview

This document covers the design and implementation plan for in-world floating damage numbers, hit/miss indicators, and critical hit indicators. These are transient visual feedback elements that appear over creatures during combat to communicate outcomes instantly without relying on the message log.

The project already has a **Scrolling Combat Text (SCT)** system (`scrollingcombattext` class in `output.h`, rendered via `cata_tiles::draw_sct_frame()`). This plan builds on and extends that existing infrastructure rather than replacing it.

---

## Current State Analysis

### What Exists Today

- **SCT System** (`src/output.h:886`, `src/output.cpp:1771+`):
  - Each SCT entry has a position, direction, text, color type, and step counter (max 8 steps).
  - Entries scroll in a direction from their spawn point over ~8 frames.
  - Supports two text fields (`sText`, `sText2`) with separate colors — currently used for damage + HP bar.
  - Direction handling supports both isometric and orthogonal tile modes.
  - Controlled by options: `ANIMATION_SCT` (toggle) and `ANIMATION_SCT_USE_FONT` (Unicode font vs ASCII).

- **SCT Spawn Points** (`src/melee.cpp:2512`, `src/character.cpp:9165,9272,9586`, `src/creature.cpp:887,893`):
  - Melee hits spawn SCT at the target's position with direction from attacker to target.
  - Damage values and remaining HP are shown.
  - Critical hits get a "Critical!" label in yellow (`m_critical`).

- **Color Mapping** (`src/cata_tiles.cpp:202`):
  - `m_good` -> green, `m_bad` -> red, `m_warning`/`m_critical` -> yellow, `m_neutral` -> white, `m_info`/`m_grazing` -> blue, `m_headshot` -> magenta.

- **Where Damage is Processed** (`src/creature.cpp:1357`):
  - `Creature::deal_damage()` is the central point where all damage flows through.
  - Returns a `dealt_damage_instance` with per-type damage breakdown.
  - Currently no floating text is spawned here — it's handled at higher-level call sites (melee, ranged).

### What's Missing

1. **Miss indicators** — No visual "MISS" appears when an attack misses. The message log says so, but there's no in-world feedback.
2. **Graze indicators** — Grazing is a distinct combat outcome with reduced damage, but it has no floating indicator (only `m_grazing` color exists).
3. **Block/Parry/Dodge/Evade indicators** — Defensive outcomes have no floating text at all.
4. **Damage type coloring** — All damage uses the same color regardless of type (bash/cut/stab/etc.). Players can't visually distinguish damage types from SCT alone.
5. **Critical hit visual distinction** — Criticals only get a "Critical!" label; there's no size/animation differentiation for the number itself.
6. **Triple criticals** — The code supports triple crits (`item.cpp:1531`) but they look identical to regular crits in SCT.
7. **Position jitter** — Multiple hits on the same creature stack directly on top of each other, making them hard to read.
8. **Damage type labels** — No indication of whether damage was bash/cut/stab/heat/etc.

---

## Design Goals

1. **Instant readability**: A player should understand what happened in <200ms without reading the message log.
2. **Non-obstructive**: Floating text must not cover creature sprites or important UI elements.
3. **Scalable**: The system should handle 5-10 simultaneous SCT entries during heavy combat (turrets, AoE).
4. **Configurable**: Players should be able to toggle individual types on/off and customize colors/sizes.
5. **Consistent with Cataclysm's aesthetic**: Should feel at home in the tile-based world, not like a modern action game overlay.

---

## Feature 1: Floating Damage Numbers

### Option A: Enhanced Existing SCT (Recommended)

Extend the current `cSCT` class to support damage-specific features while maintaining backward compatibility.

**Changes:**

- Add a `damage_type` field to `cSCT` for color-coding by type.
- Add a `size_multiplier` field so critical hits render larger.
- Add position jitter: each new SCT at the same position gets a small random offset (±2 tiles).
- Use damage-type-specific colors from the existing palette rather than generic good/bad.

**Damage Type Color Mapping:**

| Damage Type | Color (Tile)     | Rationale             |
| ----------- | ---------------- | --------------------- |
| DT_BASH     | White/Gray       | Blunt, neutral impact |
| DT_CUT      | Cyan/Light Blue  | Sharp, slicing        |
| DT_STAB     | Red-Orange       | Piercing, aggressive  |
| DT_ACID     | Green-Yellow     | Corrosive, sickly     |
| DT_HEAT     | Orange/Red       | Fire, heat            |
| DT_COLD     | Light Blue/Cyan  | Ice, cold             |
| DT_DARK     | Purple/Magenta   | Eldritch, dark        |
| DT_LIGHT    | Yellow/Gold      | Holy, radiant         |
| DT_PSI      | Blue/Violet      | Mental, psychic       |
| DT_ELECTRIC | Bright Yellow    | Lightning             |
| DT_BULLET   | White with trail | Fast projectile       |

**Size Scaling:**

- Normal damage: 1x (current behavior)
- Critical hit: 1.5x font size, gold/yellow color overlay on the number
- Triple critical: 2x font size, bright red/gold color, brief "flash" effect (first frame rendered twice at full opacity before fading)
- Graze: 0.75x size + dimmed color

**Implementation approach:**

```cpp
// In cSCT struct additions:
enum class sct_damage_type { none, bash, cut, stab, acid, heat, cold, dark, light, psi, electric, bullet };
sct_damage_type damage_type = sct_damage_type::none;
float size_multiplier = 1.0f;
bool is_critical = false;
bool is_triple_crit = false;
```

**Pros:** Leverages existing infrastructure, minimal new code paths, backward compatible.
**Cons:** The `cSCT` class was designed for simple text scrolling — adding more fields increases its complexity.

> **Decision (grill session):** Scale-up animation (numbers growing from 0.5x to full size over 2 frames) is deferred to Phase 2 as polish. No particle effects or screen shake in this iteration.

### Option B: New Dedicated FloatingText System

Create a separate system specifically for floating combat text, decoupled from the general SCT animation system.

**Architecture:**

- New `floating_text_manager` singleton that manages a pool of transient text objects.
- Each entry has: position (screen-space), velocity (drift upward), lifetime, font size, color, text content, and optional sprite effect.
- Renders in a dedicated overlay pass after the main frame but before UI elements.
- Uses object pooling to avoid allocation during combat.

**Pros:** Full control over rendering, animation, positioning, and effects. Can add particle effects (sparks on crits, etc.).
**Cons:** Significant new code (~500+ lines), duplicates some SCT functionality, requires a new render pass.

### Option C: Hybrid — SCT as Data, New Renderer

Keep `cSCT` as the data structure for spawning combat text but replace the rendering in `draw_sct_frame()` with a more sophisticated renderer that supports size scaling, per-type colors, and position jitter.

**Changes:**

- Extend `cSCT` with new fields (damage type, size multiplier, jitter offset).
- Rewrite `cata_tiles::draw_sct_frame()` to use the font rendering path for all entries (drop ASCII fallback), applying size scaling and color mapping.
- Add a small random offset at spawn time based on entry count at that position.

**Pros:** Best of both worlds — minimal data structure changes, powerful new renderer. No separate system to maintain.
**Cons:** Requires rewriting the SCT render path which currently has ASCII fallback logic.

### Recommendation: Option C (Hybrid) — LOCKED IN

Extend `cSCT` data structure with new fields but replace the rendering in `draw_sct_frame()` with a font-only renderer that supports size scaling, per-type colors, and screen-space radial jitter. Drop ASCII fallback for SCT — tiles-focused scope; accept edge cases with non-Latin scripts or custom tilesets.

---

## Feature 2: Hit/Miss Indicators

### Option A: Simple Text Labels

Spawn short text labels like "MISS", "DODGE", "PARRY", "BLOCK" at the target's position when these outcomes occur.

**Text + Color Mapping:**

| Outcome                    | Text    | Color      | Size |
| -------------------------- | ------- | ---------- | ---- |
| Miss (attack missed)       | MISS    | White/Gray | 1x   |
| Dodge (target dodged)      | DODGE   | Light Blue | 1x   |
| Parry (weapon parried)     | PARRY   | Cyan       | 1.1x |
| Block (shield blocked)     | BLOCK   | Brown/Tan  | 1x   |
| Evade (movement dodge)     | EVADE   | Light Gray | 0.9x |
| Deflect (reflected attack) | DEFLECT | Gold       | 1.2x |

**Implementation:**

- Spawn SCT entries with `sType` set to the outcome type ("miss", "dodge", etc.).
- Use existing color mapping: neutral for miss, blue-ish for dodge/evade, cyan for parry, brown for block.
- Direction: upward from target position (same as damage numbers).

**Pros:** Simple, consistent with existing SCT patterns, easy to configure.
**Cons:** Text-only may not be distinctive enough in fast combat.

### Option B: Text + Icon/Sprite Overlay

Combine text labels with small sprite indicators drawn alongside the text.

**Examples:**

- MISS: "MISS" text with a small X symbol
- DODGE: "DODGE" text with a zigzag/dash symbol
- PARRY: "PARRY" text with a sword/shield icon
- BLOCK: "BLOCK" text with a shield icon

**Implementation:**

- Extend `cSCT` to support an optional sprite ID alongside the text.
- In `draw_sct_frame()`, render the sprite first, then the text beside it.
- Use existing tileset glyphs (e.g., 'X' for miss, '~' for dodge, sword glyph for parry).

**Pros:** More visually distinctive at a glance. Icons are language-independent.
**Cons:** Requires sprite rendering support in SCT path; may look inconsistent with different tilesets.

### Option C: Text + Color Pulse Animation

Text labels with a brief color pulse animation on spawn — the text starts bright and saturated, then fades to its final color while drifting upward.

**Examples:**

- MISS: White text that pulses once before settling
- DODGE: Blue text with a quick blue flash
- PARRY: Cyan text with a metallic shimmer effect (brief opacity change)
- BLOCK: Brown/tan text with a solid, heavy appearance (slower fade)

**Implementation:**

- Add a `pulse_color` field to `cSCT` that overrides the base color for the first 2-3 frames.
- In the render loop, interpolate between pulse_color and final color based on step count.

**Pros:** Adds "game feel" without requiring new sprites. Subtle but effective.
**Cons:** Requires changes to the render timing; may be too subtle for some players.

### Recommendation: Option A + C (Text Labels with Pulse) — LOCKED IN

All five outcome types confirmed: MISS, DODGE, PARRY, BLOCK, GRAZE. Simple text labels in distinct colors with a brief color pulse on spawn for extra visual pop. All spawning goes through the centralized `spawn_combat_feedback()` helper.

---

## Feature 3: Critical Hit Indicators

### Option A: Size + Color Enhancement (Recommended)

Make critical hits visually distinct through size scaling and color changes, without adding new text labels.

**Behavior:**

- Regular damage: normal font size, standard damage-type color.
- Critical hit: 1.5x font size, gold/yellow color overlay on the number.
- Triple critical: 2x font size, bright red/gold color, brief "flash" effect (first frame rendered twice at full opacity before fading).

**Implementation:**

- Add `size_multiplier` and `is_critical` fields to `cSCT`.
- In `draw_sct_frame()`, scale the font rendering based on `size_multiplier`.
- For triple crits, render the text twice with slight offset on frame 0 for a "double-strike" effect.

**Pros:** Clean, no new text clutter, immediately recognizable.
**Cons:** Requires font size scaling support in the renderer (may need to pre-render glyphs at different sizes).

### Option B: Text Label + Size

Add a "CRIT!" or "!!!" label alongside the damage number for critical hits.

**Behavior:**

- Regular hit: just the damage number.
- Critical hit: large damage number + small "CRIT!" text above it in gold.
- Triple critical: extra-large damage number + "TRIPLE CRIT!" in red/gold.

**Implementation:**

- Use the existing two-text-field system (`sText` for damage, `sText2` for label).
- Position `sText2` above and centered on `sText`.

**Pros:** Unambiguous — even colorblind players can read "CRIT!".
**Cons:** Adds text clutter; may feel redundant since the message log already says "Critical!".

### Option C: Animation + Effects

Add a brief animation to critical hits beyond just size/color.

**Examples:**

- Critical hit: damage number scales up from 0.5x to 1.5x over 2 frames, then begins scrolling.
- Triple critical: same scale-up plus a brief screen shake on the target's tile (subtle, 1-pixel offset).
- Add spark/particle effects around the text for triple crits.

**Implementation:**

- Add `animation_phase` field to `cSCT`.
- In `draw_sct_frame()`, apply scale factor based on animation phase before rendering.
- For screen shake, add a temporary offset to the target's position during the critical hit frame.

**Pros:** Maximum "game feel" and satisfaction for landing crits.
**Cons:** More complex animation logic; screen shake may be disorienting in fast combat.

### Recommendation: Option A (Size + Color) — LOCKED IN

Start with size scaling and color changes — this is the most universally recognized critical hit indicator across all games. Scale-up animation (numbers growing from 0.5x to full size over 2 frames) is deferred to Phase 2 as polish. No particle effects or screen shake in this iteration.

---

## Feature 4: Position Jitter and Overlap Prevention

### Problem

When multiple attacks hit the same creature in quick succession, SCT entries spawn at exactly the same position and overlap, making them unreadable. The current system has some direction-based offsetting but no per-entry jitter.

### Option A: Simple Random Offset

Add a small random offset (±2-3 tiles) to each new SCT entry's spawn position. Entries that land on the same tile get pushed apart.

**Implementation:**

```cpp
// In scrollingcombattext::add():
const int jitter_range = 2;
point jitter{ rng(-jitter_range, jitter_range), rng(-jitter_range, jitter_range) };
pos += jitter;
```

**Pros:** Simple to implement, effective for most cases.
**Cons:** May push text too far from the creature in edge cases.

### Option B: Radial Distribution

Distribute SCT entries radially around the target's position based on how many entries already exist at that location.

**Implementation:**

```cpp
// Count existing entries near this position
int count = 0;
for (const auto& entry : vSCT) {
    if ((entry.pos - pos).manhattan_distance() < 3) ++count;
}
if (count > 0) {
    // Place new entry at a radial offset from center
    float angle = (count * M_PI / 4); // 45-degree increments
    pos.x += static_cast<int>(cos(angle) * 2);
    pos.y += static_cast<int>(sin(angle) * 2);
}
```

**Pros:** More predictable positioning; entries form a natural arc around the target.
**Cons:** Slightly more complex logic; may still overlap in extreme cases (5+ hits on same tile).

### Option C: Staggered Vertical Positioning

Keep horizontal position fixed but stagger vertical positions based on entry count. Newer entries spawn slightly higher, older ones drift down.

**Pros:** Cleanest visual result — entries form a vertical stack above the target.
**Cons:** Less natural for isometric view; may conflict with existing direction-based scrolling logic.

### Recommendation: Option B — LOCKED IN (Radial Distribution, Screen-Space Pixels)

Use radial distribution in **screen-space pixel offsets** rather than tile-space. When N entries already exist near a position, place the new one at angle `(N * 45 degrees)` with radius ~2 pixels from center. This creates a predictable arc around the target instead of scattered noise. Pure random can push text off-screen or too far from the creature in edge cases.

Screen-space offsets are chosen over tile-space because they allow for nicer effects later down the pipeline (e.g., curved trails, orbital animations) and don't require isometric coordinate transformations.

---

## Feature 5: Damage Type Indication

### Problem

Players cannot visually distinguish between damage types from SCT alone. A "10" could be bash, cut, stab, or heat — and the type matters for armor interactions.

### Option A: Color-Only (Recommended)

Use distinct colors for each damage type as described in Feature 1's color mapping table. No additional text needed.

**Pros:** Cleanest visual result; players learn to associate colors with types quickly.
**Cons:** Colorblind players may have difficulty distinguishing some types (e.g., heat vs electric).

### Option B: Color + Type Abbreviation

Add a small type abbreviation next to the damage number: "B" for bash, "C" for cut, "S" for stab, etc.

**Pros:** Unambiguous even for colorblind players.
**Cons:** Adds text clutter; abbreviations may not be intuitive (what does "S" mean — stab or slash?).

### Option C: Color + Damage Type Icon

Add a small icon/symbol next to the damage number representing the type:

- Bash: circle/hammer symbol
- Cut: diagonal line/slash
- Stab: triangle/point
- Heat: flame symbol
- Cold: snowflake/diamond
- Electric: zigzag bolt
- Acid: drop/bubble

**Pros:** Most visually distinctive; icons are language-independent.
**Cons:** Requires sprite rendering support in SCT path; may not match all tilesets.

### Recommendation: Option A (Color-Only) with Configurable Colorblind Mode

Default to color-only for clean visuals, but add an option `SCT_COLORBLIND_MODE` that adds small type abbreviations alongside the numbers. This gives both groups a good experience without forcing one approach on everyone.

---

## Feature 6: Configuration Options

Add new game options to let players customize floating combat text behavior:

| Option                      | Type | Default | Description                                               |
| --------------------------- | ---- | ------- | --------------------------------------------------------- |
| `ANIMATION_SCT_DAMAGE`      | bool | true    | Show damage numbers                                       |
| `ANIMATION_SCT_OUTCOMES`    | bool | true    | Show miss/dodge/parry/block text                          |
| `ANIMATION_SCT_CRITICALS`   | bool | true    | Highlight critical hits with size/color                   |
| `ANIMATION_SCT_TYPE_COLORS` | bool | true    | Color damage by type                                      |
| `ANIMATION_SCT_COLORBLIND`  | bool | false   | Add type abbreviations to numbers                         |
| `ANIMATION_SCT_JITTER`      | int  | 2       | Position jitter range in tiles (0 = none)                 |
| `ANIMATION_SCT_MAX_ENTRIES` | int  | 15      | Maximum simultaneous SCT entries (old ones removed first) |
| `ANIMATION_SCT_SPEED`       | int  | 10      | Animation speed multiplier (5-20, default 10)             |

---

## Implementation Plan

### Phase 1: Foundation (Core Data Structure Changes)

**Files to modify:**

- `src/output.h` — Extend `cSCT` struct with new fields
- `src/output.cpp` — Update constructor and `add()` method

**Changes:**

1. Add fields to `cSCT`:
   - `sct_damage_type damage_type = sct_damage_type::none;`
   - `float size_multiplier = 1.0f;`
   - `bool is_critical = false;`
   - `bool is_triple_crit = false;`
   - `point jitter_offset{0, 0};`

2. Update `scrollingcombattext::add()` to accept new parameters and apply **radial position jitter** in screen-space pixels (angle = entry_count * 45°, radius ~2px).

3. Add helper function: `sct_damage_type from_game_dt(damage_type dt);`

4. Implement FIFO eviction + off-screen cleanup in the main render loop.

### Phase 2: Renderer Enhancement

**Files to modify:**

- `src/cata_tiles.cpp` — Rewrite `draw_sct_frame()`

**Changes:**

1. **Font-only rendering path** for SCT (drop ASCII fallback). This is tiles-focused; accept edge cases with non-Latin scripts or custom tilesets.
2. Apply size scaling based on `size_multiplier`.
3. Map damage type to color using new mapping table.
4. Handle critical/triple-crit visual effects (scale-up animation: numbers grow from 0.5x to full size over 2 frames, then begin scrolling).
5. Apply jitter offset at spawn time via screen-space radial distribution.

### Phase 3: Spawn Point Integration — Centralized

**Files to modify:**

- `src/creature.cpp` — Add SCT spawn in `deal_damage_handle_type()` (primary)
- `src/melee.cpp`, `src/ballistics.cpp` — Thread `is_crit`/`is_graze` flags down to damage handler; add SCT for miss/dodge/parry/block outcomes at attack resolution sites

**Changes:**

1. **Centralized spawn**: All damage-related SCT spawned in `Creature::deal_damage_handle_type()`. Pass `bool is_crit, bool is_graze` from callers through the call chain. This ensures all damage types — melee, ranged, environmental, magic — get consistent floating text feedback.
2. **Distributed spawn for outcomes only**: Miss/dodge/parry/block happen _before_ damage is calculated, so they must be spawned at attack resolution sites (`melee.cpp`, `ballistics.cpp`). These are "outcome" SCTs with no damage value.
3. Create centralized `spawn_combat_feedback()` helper function that encapsulates all SCT spawn logic (position jitter calculation, color mapping, size scaling, flag setting). Callers pass high-level info and the helper handles everything else.

### Phase 4: Defensive Outcome Indicators — All Five Types

**Files to modify:**

- `src/melee.cpp` — Dodge/parry detection and SCT spawn
- `src/ballistics.cpp` — Ranged dodge/block detection
- New file: `src/combat_feedback.h/cpp` — Centralized combat feedback spawning (`spawn_combat_feedback()`)

**Changes:**

1. Identify where dodge, parry, block, and evade outcomes are determined in the attack flow.
2. Spawn SCT entries for all five outcome types with text labels + distinct colors:
   - MISS → white/gray, brief flash
   - DODGE → light blue
   - PARRY → cyan (slightly larger)
   - BLOCK → brown/tan
   - GRAZE → dimmed damage-type color (`m_grazing` type, currently unused for SCT — first to use it)
3. All outcomes use the centralized `spawn_combat_feedback()` helper for consistency.

### Phase 5: Configuration and Polish

**Files to modify:**

- `src/options.cpp` — Add new options
- `src/game_options.h` — Declare option keys (if needed)

**Changes:**

1. Add all configuration options from Feature 6 table.
2. Wire options into SCT spawn logic (gate damage numbers, outcomes, type colors).
3. Add colorblind mode support (`SCT_COLORBLIND_MODE` adds type abbreviations alongside numbers).
4. Test with various tilesets and font configurations. Accept edge cases — tiles-focused scope.

---

## Integration Points Reference

### Where Attacks Are Resolved (SCT Spawn Opportunities)

|| Attack Type | File | Function/Line | Outcome Available ||
|---|---|---|---|---|
| Melee | `src/melee.cpp` | `Character::melee_attack()` ~line 479+ | hit, miss, crit, graze (all implemented) |
| Ranged | `src/ballistics.cpp` | `projectile_attack()` ~line 330+ | hit, miss, dodge, graze (all threaded through deal_projectile_attack) |
| Martial Arts | `src/melee.cpp` | `perform_technique()` | technique-specific outcomes |
| Monster Melee | `src/monster.cpp` | `monster::melee_attack()` ~line 2012+ | hit, crit (computed from hitspread), graze (via deal_melee_hit) |

### Where Damage is Applied (SCT Spawn Opportunity)

|| Location | File | Function | Status ||
|---|---|---|---|---|
| Central damage handler | `src/creature.cpp` | `Creature::deal_damage()` ~line 1368 | All damage flows here — unified SCT spawning | **IMPLEMENTED** |
| Character-specific | `src/character.cpp` | `Character::deal_damage()` ~line 9562+ | Calls through to Creature::deal_damage() | **SCATTERED SPAWN REMOVED** |

### Spawn Location Architecture — IMPLEMENTED: Centralized Damage + Distributed Outcomes

**Primary spawn point (damage): `Creature::deal_damage()` (line 1380)**

All damage-related SCT spawned here. The `is_crit`/`is_graze` flags are threaded through all call chains:

- Melee: `Character::melee_attack()` → `deal_melee_hit()` → `deal_damage()`
- Ranged: `Creature::deal_projectile_attack()` → `deal_damage()` (graze auto-detected from goodhit)
- Monster melee: `monster::melee_attack()` → `deal_melee_hit()` → `deal_damage()`

This ensures all damage types — melee, ranged, environmental, magic — get consistent floating text feedback via a single code path.

**Secondary spawn points (outcomes only): Attack resolution functions**

Miss/dodge/parry/block happen _before_ damage is calculated, so they must be spawned at attack resolution sites:

- Melee miss: `melee.cpp` line 541 — player/NPC misses in `Character::melee_attack()`
- Melee dodge: `creature.cpp` line 826 — active dodges in `Creature::deal_melee_hit()` (hit_spread < 0)
- Melee block: `melee.cpp` line 2091 — shield/limb blocks in `Character::block_hit()`
- Ranged miss: `creature.cpp` ~line 950 — total misses in `Creature::deal_projectile_attack()` (missed_by >= 1.0)
- Ranged dodge: `creature.cpp` ~line 1012 — projectile dodges in `Creature::deal_projectile_attack()` (goodhit >= 1.0)

All spawning goes through the centralized `spawn_combat_feedback()` helper.

---

## Testing Checklist

- [x] Damage numbers appear over targets on hit (melee and ranged) — centralized in deal_damage()
- [ ] Numbers are color-coded by damage type — sct_damage_type_to_color() implemented, needs verification
- [x] Critical hits render larger (1.5x) and in gold/yellow — is_crit threaded from player_melee_attack() and monster::melee_attack()
- [ ] Triple criticals render extra-large (2x) with flash effect — not yet tested
- [x] Graze renders smaller (0.75x) and dimmed — is_graze threaded from melee.cpp (hit_spread <= 5) and deal_projectile_attack() (goodhit > accuracy_standard)
- [ ] Scale-up animation works for crits (Phase 2: numbers grow from 0.5x to full size over 2 frames)
- [x] Miss/dodge/parry/block/graze text appears on defensive outcomes with correct colors — all five outcome types implemented:
  - MISS: melee.cpp line 541, ranged creature.cpp ~line 950
  - DODGE: creature.cpp line 826 (melee), creature.cpp ~line 1012 (ranged)
  - BLOCK: melee.cpp line 2091 (Character::block_hit())
  - GRAZE: handled via size_multiplier in centralized deal_damage() spawn
- [ ] Multiple simultaneous SCT entries don't overlap — radial jitter distributes them in screen-space pixels
- [x] FIFO eviction works: oldest entries removed first when max count exceeded (default 15) — implemented in scrollingcombattext::add()
- [ ] Off-screen entries are immediately removed rather than waiting for step counter
- [ ] SCT entries scroll upward and fade out cleanly
- [x] `ANIMATION_SCT` option properly toggles all floating text — checked in spawn_combat_feedback() and deal_damage()
- [ ] Font-only rendering works with Unicode fonts (no ASCII fallback in tile mode)
- [ ] Colorblind mode (`SCT_COLORBLIND_MODE`) adds type abbreviations alongside numbers
- [ ] Works in both isometric and orthogonal tile modes
- [ ] No performance regression during heavy combat (10+ SCT entries)

---

## Implementation Status Summary

### Completed (Phases 1-3 Foundation)

**Phase 1 - Data Structure Extensions:**

- cSCT struct in output.h extended with new fields:
  - sct_damage_type damage_type
  - float size_multiplier
  - bool is_critical, is_triple_crit
  - point jitter_offset
  - sct_feedback_type feedback_type

**Phase 1 - Radial Jitter:**

- scrollingcombattext::add() in output.cpp implements radial distribution for position jitter (angle = count * 45°, radius ~2px)
- FIFO eviction with configurable max entries via ANIMATION_SCT_MAX_ENTRIES option

**Phase 2 - Centralized Spawning Helper:**

- New file: combat_feedback.h/cpp with:
  - spawn_combat_feedback() - main helper function
  - spawn_damage_number() - convenience for damage numbers
  - spawn_armor_feedback() - for armor damage/destruction feedback

**Phase 2 - Color Mapping:**

- sct_damage_type_to_color() in output.cpp maps damage types to colors:
  - bash→neutral, cut→info, stab→bad, acid→good, heat→warning, etc.

**Phase 3 - Centralized Damage Spawning (IMPLEMENTED):**

- SCT spawn added in Creature::deal_damage() (line 1370) with is_crit/is_graze flags
- Optional parameters added to all three deal_damage() overloads in creature.h/creature.cpp
- Scattered SCT spawns removed from:
  - melee.cpp player_hit_message() — was spawning for player hits only
  - character.cpp Character::deal_damage() — was spawning for monster→player melee
  - creature.cpp ranged::print_dmg_msg() — was spawning crit/graze SCT for ranged attacks
- Added #include "options.h" to creature.cpp for get_option access

**Phase 3 - Defensive Outcomes (COMPLETE):**

- All five outcome types implemented:
  - MISS: `melee.cpp` line 541 — player/NPC misses in `Character::melee_attack()`
  - DODGE: `creature.cpp` line 826 — active dodges in `Creature::deal_melee_hit()` (hit_spread < 0)
  - BLOCK: `melee.cpp` line 2091 — shield/limb blocks in `Character::block_hit()`
  - Ranged MISS: `creature.cpp` ~line 950 — total misses in `Creature::deal_projectile_attack()` (missed_by >= 1.0)
  - Ranged DODGE: `creature.cpp` ~line 1012 — projectile dodges in `Creature::deal_projectile_attack()` (goodhit >= 1.0)

**Phase 4 - Graze Detection (COMPLETE):**

- Melee graze: `melee.cpp` line 600 — detected when hit_spread > 0 && <= 5, threaded through deal_melee_hit() → deal_damage()
- Ranged graze: `creature.cpp` ~line 1008 — auto-detected from goodhit > accuracy_standard in deal_projectile_attack(), threaded to deal_damage()

**Phase 5 - Monster Melee Crits (COMPLETE):**

- `monster.cpp` line 2067 — crit chance computed from hitspread: base 5%, scaling up to ~15% at hitspread >= 20
- Threaded through deal_melee_hit() → deal_damage()

**Phase 6 - Ballistics Integration (COMPLETE):**

- Ranged MISS/DODGE SCT spawns added in `Creature::deal_projectile_attack()`
- Graze flag auto-detected and threaded to deal_damage() for proper size/color rendering

**Phase 6 - Configuration & Polish:**

- Wire up remaining options: ANIMATION_SCT_SPEED, off-screen cleanup, colorblind mode
- Final testing and integration

---

## References

### Games Studied

| Game                                        | Approach                                                                                                                                                                                                                                       | Notes                                                                                                                     |
| ------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| **StoneShard**                              | Floating numbers over body parts, color-coded by damage type (cut=red, bash=brown, etc.), criticals are larger and gold. Misses show as "Miss" in gray. Body-part targeting means numbers appear at specific locations on the creature sprite. | Very detailed per-bodypart feedback. Our tile-based approach is simpler but the color-coding principle applies.           |
| **Brotato**                                 | Large floating damage numbers, criticals are bigger and gold/yellow. Misses show as "MISS" in gray. Numbers drift upward and fade. Simple but effective.                                                                                       | Good reference for size-scaling approach.                                                                                 |
| **Enter the Gungeon / Binding of Isaac**    | Damage numbers pop up with a scale-in animation (start small, grow to full size). Criticals are gold and larger. Misses show as "MISS" in gray. Numbers have slight horizontal jitter so they don't stack perfectly.                           | Scale-up animation is a good pattern for crit satisfaction.                                                               |
| **World of Warcraft**                       | SCT addon ecosystem: configurable colors per type, position offsets to prevent overlap, separate frames for damage/healing/miss/block. Colorblind-friendly with text labels.                                                                   | The addon system shows what players want — extensive customization. Our options should cover the most-requested features. |
| **Katawa Shoujo / Ren'Py combat minigames** | Simple floating numbers with basic color coding.                                                                                                                                                                                               | Not relevant for our tile-based approach but shows minimal viable implementation.                                         |

### Codebase References

- SCT data structure: `src/output.h:886-962`
- SCT rendering: `src/cata_tiles.cpp:6955-7040` (`draw_sct_frame()`)
- Centralized spawn helper: `src/combat_feedback.h/cpp` — `spawn_combat_feedback()`, `spawn_damage_number()`
- Central damage handler (SCT): `src/creature.cpp:1380` — `Creature::deal_damage()` with is_crit/is_graze flags
- Melee miss SCT: `src/melee.cpp:541, 544` — `Character::melee_attack()` for player/NPC misses
- Melee dodge SCT: `src/creature.cpp:826` — `Creature::deal_melee_hit()` (hit_spread < 0)
- Melee graze detection: `src/melee.cpp:600` — hit_spread <= 5 threshold
- Melee block SCT: `src/melee.cpp:2091` — `Character::block_hit()`
- Monster melee crits: `src/monster.cpp:2067` — hitspread-based crit chance
- Ranged miss/dodge SCT: `src/creature.cpp:~950, ~1012` — `Creature::deal_projectile_attack()`
- Ranged graze detection: `src/creature.cpp:~1008` — goodhit > accuracy_standard
- Armor feedback SCT: `src/character.cpp:9166, 9271` — armor destruction/damage events
- Color mapping: `src/output.cpp:sct_damage_type_to_color()` — damage type to color conversion
- Options: `src/options.cpp:1935-1947` (ANIMATION_SCT*), `src/combat_feedback.cpp:65-82` (option checks)

---

## Decisions Log (Grill Session)

All decisions locked in during the grill-me session. Each question was answered with a recommendation, and the user confirmed or modified it.

| # | Decision Point              | Choice                                                                                                                                                                                          | Rationale                                                                                                                                           |
| - | --------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1 | Damage type indication      | Color-only default + `SCT_COLORBLIND_MODE` toggle for abbreviations                                                                                                                             | Clean visuals for most players; colorblind users get unambiguous fallback. Icons reserved for the message log where they have room to breathe.      |
| 2 | Critical hit treatment      | Size scaling (1.5x) + gold color; triple crits at 2x + flash. Scale-up animation in Phase 2 as polish. No particles or screen shake.                                                            | Universally recognized pattern across games. Defer extra polish to keep Phase 1 focused.                                                            |
| 3 | Spawn location architecture | Centralized for damage (`deal_damage_handle_type()`), distributed only for miss/dodge/parry/block outcomes (happen before damage calc). `is_crit`/`is_graze` flags threaded through call chain. | Single source of truth for damage feedback; defensive outcomes must spawn where they're determined.                                                 |
| 4 | Position jitter strategy    | Radial distribution in screen-space pixels (angle = entry_count * 45°, radius ~2px). Not tile-space.                                                                                            | Predictable arc around target; screen-space allows nicer effects later (curved trails, orbital animations) without isometric coordinate transforms. |
| 5 | Defensive outcome types     | All five: MISS, DODGE, PARRY, BLOCK, GRAZE — text labels with distinct colors + brief color pulse on spawn.                                                                                     | Complete combat feedback coverage. `m_grazing` type will be first to use it for SCT (previously unused).                                            |
| 6 | Max entries & cleanup       | FIFO eviction when exceeding max count; immediate removal for off-screen entries.                                                                                                               | Simple, predictable, matches player expectations (newest info most relevant). Off-screen check prevents wasted render cycles.                       |
| 7 | Font rendering approach     | Font-only path in `draw_sct_frame()`, drop ASCII fallback entirely for SCT. Accept edge cases (non-Latin scripts, custom tilesets).                                                             | Tiles-focused scope; ASCII fallback was designed for curses display. Edge cases handled iteratively.                                                |
| 8 | Spawn helper function       | Centralized `spawn_combat_feedback()` encapsulating jitter calculation, color mapping, size scaling, and cleanup logic. Callers pass high-level info only.                                      | Keeps all 15+ spawn sites clean; ensures consistent behavior across melee/ranged/magic attack paths.                                                |
