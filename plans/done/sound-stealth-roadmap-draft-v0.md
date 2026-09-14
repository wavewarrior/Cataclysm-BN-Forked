# Sound & Stealth System Roadmap

## Status
Draft — awaiting scout research on current codebase capabilities.

## Reference Games
- **Mark of the Ninja** — silhouette-based stealth, sound visualization as expanding rings, enemy cone-of-vision, noise masking
- **Dishonored** — chaos system, sound propagation through walls, enemy alert states with visual feedback
- **Splinter Cell** — light/shadow stealth, third-person awareness meters, sound-based detection
- **Metal Gear Solid** — binocular view, noise meter, patrol route disruption

---

## Current CBN Capabilities (Known)

### Sound Engine
- Tile-based intensity heatmap (`_snapshot_sound_visualization`)
- Ray-casted occlusion along source→listener paths
- Sound categories: `movement`, `alert`, `music`, `ambient`, `destructive_activity`, etc.
- Sound markers placed on minimap for unseen sources
- Volume attenuation by distance + material occlusion (dB-based)
- Weather attenuation factor
- Deafness/hearing loss mechanics
- Monster AI hears sounds and investigates

### Sound Visualization (Debug-Only)
- Animated wavefront pulses (F4 panel, sound spawner)
- Flood-fill propagation respecting wall occlusion
- Cyan wavefront band + dim trail interior
- Auto-cleanup after animation lifetime expires
- **Moved outside F5 gate** (session fix)

### Stealth Mechanics (Partial)
- Player stealth stat
- Noise generation from footsteps (terrain-dependent)
- Clothing affects noise (barefoot, footwear types)
- Vehicle noise
- Combat noise (gunshots, melee)
- Activity noise (crafting, digging, etc.)

---

## Feature Categories

### Tier 1: Foundation (Must Have)
Things that make the system playable and useful.

### Tier 2: Polish (Should Have)
Things that make the system feel professional and competitive.

### Tier 3: Innovation (Nice to Have)
Things that push beyond reference games.

---

## Pending Research
- [ ] Full inventory of sound-related files and symbols
- [ ] Monster AI perception pipeline (detection → investigation → alarm)
- [ ] Existing stealth meters/indicators in UI
- [ ] Material sound properties (floor types, clothing modifiers)
- [ ] Height/elevation sound propagation
- [ ] Any TODO/incomplete features in sound code

---

## Notes
- Sound pulse rendering fixed: now renders without F5 debug HUD
- Click-through fixed: F4 panel no longer blocks world clicks
- `sounds::sound()` only records events; `sfx::play_variant_sound()` needed for audio
- `debugmsg()` is modal — use `DebugLog()` for diagnostics