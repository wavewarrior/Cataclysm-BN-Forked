# Co-op Sublime Modernization Plan

**Status:** Active — major features delivered, polish items remain.
**Goal:** Make the coop experience sublime and modern — full action parity, reconnection, shared exploration, comprehensive testing without 2 machines.

## Current State Summary

The coop system is mature with solid infrastructure:
- TCP transport (SDL3_net) with 4-byte length-prefix framing
- Delta event stream with FNV-1a hash integrity checking + rollback engine
- Client-side prediction with seq-based reconciliation
- 25+ action types relayed (MOVE, FIRE, MELEE, PICKUP, DROP, CRAFT, SLEEP, etc.)
- Monster sync with stable IDs, vehicle sync, chat, trade, emotes
- Downed state + stabilization, partner vitals HUD data
- Comprehensive test suite (unit + E2E + sim transport)

## Gap Analysis

### Critical Gaps (blocks "sublime")
| Gap | Impact | Status |
|-----|--------|--------|
| No reconnection | Connection drop = session over | ✅ `c3e5c8ea` — session token, auto-retry 30s, proxy preserved 5min |
| No shared overmap exploration | Players can't see each other's explored areas | ✅ `c3e5c8ea` — bidirectional delta sync |
| THROW not relayed | Grenades/molotovs don't affect host world | ✅ Pre-existing — C2e hooks in activity_actor.cpp |
| Item passing stub | "Not yet implemented" message | ✅ `c3e5c8ea` — both host and client can pass items |
| Hardcoded port 8080 | Can't configure | ✅ `c3e5c8ea` — COOP_PORT option, ip:port join syntax |
| No construction relay | Client building doesn't sync | ✅ Pre-existing — TERRAIN_CHANGE hooks catch construction results |

### Quality Gaps (polish for "feels like single-player")
| Gap | Impact | Status |
|-----|--------|--------|
| No ping display | ping_ms tracked but not shown | ✅ `3561bd2d` — RTT measured, color-coded in topbar |
| No partner compass | No indicator for offscreen partner | ✅ `3561bd2d` — compass arrow (↑↓←→↗↘↙↖) in topbar when partner >30 tiles away |
| No chat input keybinding | No way to type chat messages | ✅ `3561bd2d` — ACTION_CO_OP_CHAT with string_input_popup |
| No chat history UI | Chat exists but no scrollback/panel | ❌ Deferred — messages appear in the game log which already has scrollback |
| No session save/resume | Can't save coop and continue later | ❌ Deferred — reconnection covers transient drops; full persistence is a larger design |
| Static `coop_server srv` | Can't properly restart sessions | ✅ `3561bd2d` — heap-allocated, owned by game object |
| No skill sync | Client skills not reflected on proxy | ✅ `3561bd2d` — client sends skills every 10 ticks, server applies to proxy |

### Testing Gaps
| Gap | Impact | Status |
|-----|--------|--------|
| Overmap sync not tested | Build/parse coverage | ✅ `c3e5c8ea` — 3 unit tests for packet round-trip |
| Skill sync not tested | Build/parse coverage | ✅ `c3e5c8ea` — 3 unit tests for field round-trip |
| Session token not tested | Reconnect token in world_seed | ✅ `c3e5c8ea` — 2 unit tests for round-trip |
| Reconnect packet type | Enum value correctness | ✅ `c3e5c8ea` — 1 unit test |
| THROW relay not tested | No E2E scenario | ❌ Requires 2-process harness |
| Reconnection not tested | No E2E scenario | ❌ Requires 2-process harness |

---

## Phase 1 — Action Parity & Polish (client = single-player)

### 1A: THROW relay
- Hook `throw_activity_actor::finish()` to emit item landing position + type to host
- Server: apply item placement at landing position via `apply_drop_manifest`
- Grenade/molotov field effects already relayed via FIELD_SET hooks in magic.cpp

### 1B: Item passing (F2 completion)
- Replace stub message with actual implementation
- Client selects item from inventory → serialize → send trade_offer packet
- Host receives popup → accept/reject → transfer item
- Mirror of existing host→client trade flow (already partly wired)

### 1C: Construction relay
- Client runs construction locally (terrain changes)
- TERRAIN_CHANGE hook already fires for construction completion
- Need: relay intermediate construction progress (partial builds)
- Hook `construction_activity_actor::finish()` for progress sync

### 1D: READ relay (skill sync)
- Client reads books locally, gains skills
- Add periodic skill delta sync: client sends skill levels in client_status
- Host updates proxy NPC skills to match (for NPC AI calculations)

### 1E: Configurable port
- Replace hardcoded 8080 with game option
- Add `COOP_PORT` option to options manager
- Update start_host/start_join to read from option
- Allow port specification in join IP field ("192.168.1.5:9090")

---

## Phase 2 — Reconnection

### 2A: Session token
- Generate UUID session token at host startup
- Send in world_seed packet; client stores it
- On reconnect: client sends token → host validates → skips world_seed, restores proxy

### 2B: Proxy NPC preservation
- On disconnect: keep proxy NPC alive (not destroyed)
- Mark proxy as "disconnected" — AI makes it follow host or stand idle
- On reconnect: reattach proxy to new client connection

### 2C: Client state restoration
- Client reconnects → receives current world state (full sync)
- Ring buffer and seq counter reset to current
- Calendar sync ensures both sides are on same turn

### 2D: Graceful disconnect vs crash detection
- Graceful: client sends disconnect packet → host preserves session for N seconds
- Crash: TCP drop detected → host preserves session for configurable timeout (default 60s)
- Timeout expired → host removes proxy and cleans up

---

## Phase 3 — Shared Overmap Exploration

### 3A: Overmap sync packet
- New packet type: `overmap_reveal = 50`
- When either player reveals overmap tiles, send coordinates to partner
- Batch: send chunk of newly revealed tiles each tick (not individual tiles)

### 3B: Bidirectional sync
- Host→client: host's explored overmap tiles sent in initial sync + delta
- Client→host: client's explored tiles sent periodically
- Both sides call `overmap::reveal()` for received coordinates

### 3C: Overmap note sharing
- Share player-placed overmap notes
- New fields in overmap_mark packet for full note text + symbol

---

## Phase 4 — Connection Quality & UX

### 4A: Ping display
- `partner_ping_ms` already tracked in coop_session
- Display in sidebar/HUD when in coop mode
- Color code: green (<100ms), yellow (100-300ms), red (>300ms)

### 4B: Connection status indicator
- Show connection state: Connected / Reconnecting / Disconnected
- Flash indicator on packet loss or high latency spikes

### 4C: Chat improvements
- Dedicated chat input keybinding (already has ACTION_CO_OP_CHAT?)
- Chat history panel (scrollback)
- System messages for join/leave/reconnect events

---

## Phase 5 — Testing Without 2 Machines

### 5A: Expand E2E scenarios
- `COOP_SCENARIO=throw` — client throws item, host verifies landing
- `COOP_SCENARIO=construct` — client builds, host verifies terrain change
- `COOP_SCENARIO=reconnect` — client disconnects, reconnects, verifies state

### 5B: Loopback testing improvements
- All E2E tests already run on one machine via Deno harness + two processes
- Add `COOP_SCENARIO=full_parity` — exercises every relayed action type
- Add connection-quality simulation via sim_transport in unit tests

### 5C: Static server lifetime fix
- Replace `static coop_server srv` with heap allocation
- Proper cleanup on session end allows re-hosting without restart

---

## Execution Priority

1. Phase 1E (configurable port) — quick win, removes friction
2. Phase 1B (item passing) — visible feature gap
3. Phase 1A (throw relay) — action parity
4. Phase 2 (reconnection) — biggest modern coop feature
5. Phase 5C (static server fix) — enables proper session management
6. Phase 3 (shared overmap) — exploration quality
7. Phase 4 (connection UX) — polish
8. Phase 5A-B (testing) — confidence
