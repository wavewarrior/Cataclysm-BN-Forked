# Co-op Testing Overhaul Plan

## Context

The current co-op test suite for Cataclysm-BN has 273 assertions across 116 Catch2 test cases (`[coop]` tag) and 12 E2E scenarios in a 2-process Deno harness (`scripts/test_coop.ts`). This plan addresses the honest assessment that the suite — while ahead of typical OSS multiplayer projects — is structurally incapable of catching the most dangerous class of co-op bugs: silent state divergence between host and client after many turns of concurrent play. The goal is to add the testing layers that move confidence from "each feature works in isolation" to "a real 2-player session stays consistent."

## Audit: What Exists and What It Actually Proves

### Tier 1 — Pure-Function Unit Tests (9 files, ~220 assertions)

| File | What it tests | What it proves | What it misses |
|------|--------------|----------------|----------------|
| `coop_reconcile_test.cpp` | `coop_reconcile_pos()` — all 8 directions, round-trip, snap correction, wall blocks, seq boundaries, wrap, non-movement no-ops, predicted_outcome isolation | The reconcile *algorithm* is correct for all known input classes | Whether the algorithm is called with correct inputs from the real client tick loop |
| `coop_packets_test.cpp` | JSON serialize/deserialize round-trips for every `coop_pkt` type | Wire format is self-consistent | Whether the real `send()`/`recv()` framing preserves this across real TCP |
| `coop_delta_test.cpp` | `coop_collect_streamable` + FNV-1a hash parity between host/client event lists | Hash algorithm matches between both sides for identical input | Whether both sides actually *produce* identical event lists from the same world mutations |
| `coop_modernization_test.cpp` | Misc feature assertions (structure/format tests for new packet types) | Format contracts | Nothing about runtime behaviour |
| `coop_idle_test.cpp` | Idle/fast-forward tick math | Timer arithmetic | Not exercised through real server/client objects |
| `coop_pickup_test.cpp` | `apply_pickup_manifest()` on a real `build_test_map()` | The map mutation function works given a correct manifest | Whether the client builds a correct manifest from real inventory state |
| `coop_drop_test.cpp` | `apply_drop_manifest()` on a real `build_test_map()` | Same as pickup, drop direction | Same gap |
| `coop_terrain_test.cpp` | `apply_terrain_change()` on a real map | Terrain mutation function works | Whether terrain change detection → serialization → relay produces the right input |
| `coop_net_sim_test.cpp` | `coop_sim_transport` self-tests (latency/loss/reorder/close) + `coop_client` ring buffer (cap-at-32, seq wrap) | The fake transport and the deque are correct | Round-trip through actual `coop_server`/`coop_client` objects is explicitly blocked by the `g` singleton — comment at line 15 says so |
| `coop_net_test.cpp` | Real SDL3_net loopback socket framing | TCP framing layer works | No game objects — just raw bytes |

**Verdict:** Every unit test exercises a single pure function or data structure in isolation. None test the *composition* of these functions in a running game tick. The reconcile tests are genuinely rigorous (gold standard for unit-level), but the pickup/drop/terrain tests only validate the last step in a multi-step relay chain — they never exercise the client-side detection, serialization, network transport, or server-side dispatch that precedes the final `apply_*` call.

### Tier 2 — Two-Process E2E Integration Tests (1 file, 12 scenarios)

`tests/coop_integration_test.cpp` + `scripts/test_coop.ts` (Deno harness).

| Scenario | What it actually exercises | Realism |
|----------|--------------------------|---------|
| `movement` | 3×MOVE_N → proxy reaches target, client reconciles | **Minimal.** 3 moves on a clean map. No obstacles, no concurrent host movement, no collision. |
| `resync` | `set_force_resync_for_test()` → full-tile sync arrives at client | **Artificial.** Force-injects the resync flag; never triggers via actual hash mismatch. |
| `pickup` | Client queues PICKUP with hand-built manifest JSON → host applies | **Partial.** Tests the relay but client builds manifest *manually* (hard-coded JSON string), not through the real UI/action path. |
| `disconnect` | Client closes socket abruptly → host detects TCP drop, proxy cleaned up | **Good for this specific path.** But only tests abrupt close, not network timeout or half-open connections. |
| `submap_shift` | 3×MOVE_N + `reset_sync_origin_for_test()` → full sync after origin change | **Artificial.** Force-injects origin change; never triggers via real submap crossing. |
| `death` | Client zeros torso HP → host clamps proxy, announces death once | **Reasonable.** Tests the actual HP-mirror and once-only gate. |
| `ranged` | Client arms glock, queues 10 FIRE actions → host confirms monster HP dropped | **Good.** Exercises the full weapon_id/ammo_id relay chain end-to-end. |
| `melee` | Client arms knife, queues 5 MELEE → host confirms monster HP dropped | **Good.** Same relay chain but melee path. |
| `smash` | Same as melee but SMASH action key → melee_attack route | **Good.** Distinct code path. |
| `terrain_change` | Client queues terrain change t_floor→t_pavement → host verifies | **Reasonable.** Full relay, neutral terrain pair avoids game-mechanics interference. |
| `reconnect` | Client drops → host enters awaiting_reconnect → client reconnects with session token | **Good for happy path.** Doesn't test: reconnect during mid-tick processing, reconnect after world state drifted, reconnect with stale state, reconnect timeout expiry. |
| `item_pass` | Client sends trade_offer packet → host receives it without crashing | **Weak.** Explicitly notes (line 1143-1146) that it can't auto-accept the trade because the UI popup is blocking. Only tests packet arrival, not actual item transfer. |

**Critical observation:** The Deno harness (`scripts/test_coop.ts`) validates only **process exit codes** — if both processes exit with code 0, the scenario passes. No external state comparison. No world checksum. No verification that both sides agree on the final game state. Each scenario runs for seconds, not minutes. The harness cannot run without Deno (not installed on this workstation), and there's no CI integration.

### What Is Completely Untested

| Subsystem | Source files | Gap severity |
|-----------|-------------|--------------|
| **Vehicle sync** (E1) | `coop_server.cpp` vehicle_state handling, `map::displace_vehicle` | **Critical.** No test of any kind — unit or E2E. Host-authoritative vehicle displacement by `vehicle_id_map_rev_` lookup is exercised only by manual play. |
| **Chat/emote** | `coop_pkt::emote`, `coop_pkt::tap_shoulder` | Low. Simple packet relay, unlikely to desync state. |
| **Overmap sync convergence** | `coop_overmap.cpp` | **High.** Round-trip packet tests exist, but no test verifies two independent `overmapbuffer` instances converge after reveal sync. |
| **Multi-tick state drift** | Entire coop layer | **Critical.** No test runs more than ~15 ticks. No test checks whether host and client world state match after extended play. This is the #1 gap. |
| **Concurrent actions** (race conditions) | Server action queue under concurrent arrivals | **High.** Two players acting on the same tile/item/vehicle in the same tick — untested. |
| **Fiber-based modal UI across network yield** | `coop_fiber.cpp` | **Medium.** Touches raw SDL event injection and 512 KiB stack switching. Essentially untestable without a real UI context; manual-only by design. |
| **Network adverse conditions** | All coop networking | **High.** Every E2E test runs on clean localhost TCP. No latency, no loss, no reorder, no jitter. The `coop_sim_transport` exists precisely for this but is only used for transport self-tests — it's never plugged into real server/client objects (blocked by `g` singleton, per the code comment). |
| **Reconnect edge cases** | `accept_reconnect()`, proxy preservation across 5min window | **High.** Happy-path reconnect tested; reconnect-during-tick, timeout expiry, world drift during disconnection, stale session token — all untested. |
| **Inventory conservation across transfers** | Trade accept/reject, pickup/drop symmetry | **Medium.** Item duplication or loss during concurrent pickup, trade, or drop is untested. |

## Approach

### Step 1: In-Process Integration via `coop_sim_transport` (unblocks everything)

**Problem:** The `g` singleton prevents hosting `coop_server` and `coop_client` in the same process — the comment at `coop_net_sim_test.cpp:15` says so explicitly. This is why every test above `coop_sim_transport` is either pure-function or requires two OS processes.

**Solution:** Create a single-process in-process integration test that sidesteps the `g` singleton constraint by NOT running two full game instances. Instead:

- Instantiate a `coop_server` using the real `g` (host world)
- Instantiate a `coop_client` with only a transport layer (no second `g`) — it sends/receives packets but delegates world mutations to the *same* `g` via the server's `apply_*` functions
- Wire them through `coop_sim_transport` (already exists, already tested)
- Drive both in lockstep: `host_tx->advance(tick_ms); cli_tx->advance(tick_ms); srv.coop_world_tick(); cli.coop_world_tick();`

**Verified: transport abstraction is fully plumbed.** Both `coop_server` and `coop_client` store `std::unique_ptr<coop_transport> transport_` and route ALL I/O through the virtual `send()`/`recv()`/`poll()` interface. No raw `NET_StreamSocket` calls exist outside of `connect()`/`try_accept()` (which are the only places `coop_net_transport` is constructed). This means NO production code refactoring is needed to drive the tick loops through `coop_sim_transport`.

**What IS needed:** A test-seam method on each class to inject a pre-built transport:

```cpp
// coop_server.h — add alongside existing test seams (lines 77-83):
auto set_transport_for_test( std::unique_ptr<coop_transport> t ) -> void {
    transport_ = std::move( t );
}

// coop_client.h — add alongside existing test seams (lines 48-70):
auto set_transport_for_test( std::unique_ptr<coop_transport> t ) -> void {
    transport_ = std::move( t );
}
```

With these seams, the test creates two `coop_sim_transport` instances, wires them bidirectionally, wraps each in `unique_ptr<coop_transport>`, and injects them. Then `coop_world_tick()` on both objects works as normal — no `_with()` overload needed.

**Remaining constraint:** `coop_client::apply_sync()` calls `g->m.ter_set()`, `g->m.furn_set()`, `g->m.i_clear()` etc. — it mutates the SAME `g` that the server is also reading. In a single-process test this means the client's sync application and the server's world are physically the same object. This is actually fine for checksum testing (they see the same state by definition after apply), but it means **client-side prediction divergence cannot be tested in-process** — that requires two separate worlds (the existing 2-process Deno harness). The in-process tests cover: relay correctness, crash safety, adverse network resilience, and server-side state consistency. Client prediction remains covered by the existing reconcile unit tests + Deno E2E.

**File:** `tests/coop_inproc_test.cpp` — new file. Tag: `[coop][inproc]`.

**Dependencies:** None — can start immediately. Unblocks Steps 2-5.
### Step 2: World-State Checksum Divergence Detection

**Problem:** No test verifies that host and client agree on world state. 273 assertions pass but say nothing about whether two sides converge.

**Solution:** Add a `coop_world_checksum()` function that hashes the subset of world state that coop synchronizes:
- Terrain in the sync radius (already hashed by `coop_collect_streamable` — extend to include furniture and field state)
- Player/proxy position (already compared in movement E2E but not as a checksum)
- Items on ground in sync radius (currently only tested via pickup/drop manifests)
- Vehicle positions and facing (currently untested)

The checksum is computed identically on both sides after a full-sync cycle. Assertion: `host_checksum == client_checksum` after N ticks.

**Integration point:** This plugs into Step 1's in-process test harness. After running M ticks of scripted actions, compute checksums on both the host's world and the client's received state and compare.

**File:** `src/coop_checksum.h` + `src/coop_checksum.cpp` (new, ~150 lines). Test assertions in `tests/coop_inproc_test.cpp`.

**Existing code to reuse:** `coop_collect_streamable()` in `src/coop_mutation_log.cpp` already computes an FNV-1a hash over terrain/furniture/field events. Extend this pattern — don't create a second hashing approach.

**Dependencies:** Step 1 (needs in-process integration to compare both sides).

### Step 3: Multi-Tick Soak Scenarios

**Problem:** Every E2E scenario runs for <15 ticks. Real co-op sessions run for thousands. Slow leaks, accumulating drift, and order-dependent state corruption only manifest over time.

**Solution:** Add soak test scenarios to the in-process harness (Step 1) that:

1. **Random walk (500 ticks):** Both host avatar and client proxy move randomly. After every 50 ticks, compute world checksum (Step 2) and assert convergence. Tagged `[coop][soak]` and `[.]` (hidden, opt-in — too slow for default runs).

2. **Pickup/drop churn (200 ticks):** Spawn 50 items on the map. Client alternately picks up and drops items. After all actions, assert: (a) total item count conserved, (b) world checksum matches.

3. **Disconnect/reconnect cycling (100 ticks):** Run 20 ticks, disconnect client via `close_abruptly()`, run 10 ticks (host solo), reconnect, run 20 ticks, assert convergence. Repeat 3 times.

**File:** `tests/coop_soak_test.cpp` — new file. Tags: `[coop][soak][.]`.

**Dependencies:** Steps 1 + 2.

### Step 4: Adverse Network Condition Tests

**Problem:** All tests run on clean loopback. The `coop_sim_transport` has latency, loss, and reorder capabilities that are tested in isolation but never used to exercise real coop logic.

**Solution:** Run the in-process integration scenarios (Steps 1 + 3) under degraded network conditions by configuring the `coop_sim_transport`:

1. **High latency (200ms one-way):** Movement + reconcile still converges.
2. **5% packet loss:** Delta sync lost → hash mismatch → rollback → full resync → convergence.
3. **Message reorder:** Two consecutive syncs swapped → client handles gracefully (no crash, eventually converges).
4. **Jitter (alternating 0ms and 150ms):** Simulate real-world network variance.

Each scenario reuses the movement/pickup/terrain-change action sequences from Step 1 but with the transport configured differently.

**File:** `tests/coop_adverse_net_test.cpp` — new file. Tag: `[coop][adverse]`.

**Dependencies:** Step 1 (in-process harness).

### Step 5: Vehicle Sync Test Coverage

**Problem:** Vehicle sync has zero automated test coverage — the most glaring gap given vehicles are a core gameplay feature.

**Solution:** Add a vehicle sync test in the in-process harness:
1. Spawn a vehicle at a known position (reuse `vehicle_helpers.h` or `build_test_map` patterns from existing vehicle tests)
2. Client sends `vehicle_state` packet (position + facing + velocity)
3. Host processes via `vehicle_id_map_rev_` lookup + `map::displace_vehicle`
4. Assert vehicle position on host matches the client's sent position
5. Verify the `vehicle_id_map_rev_` mapping persists across multiple state updates

**File:** `tests/coop_vehicle_test.cpp` — new file, or add section to `tests/coop_inproc_test.cpp`. Tag: `[coop][vehicle]`.

`unverified — confirm first`: How vehicles are spawned in tests — check `tests/vehicle*` for existing patterns and helpers. Also verify what `vehicle_id_map_rev_` looks like (in `coop_server.cpp`) to understand the ID mapping contract.

**Dependencies:** Step 1.

### Step 6: Property-Based Concurrent Action Testing

**Problem:** Two players acting on the same tile/item in the same tick is untested. Hand-writing every interleaving is impractical.

**Solution:** Use [RapidCheck](https://github.com/emil-e/rapidcheck) (C++ QuickCheck-style PBT library, header-only, integrates with Catch2) to generate random concurrent action sequences and assert invariants:

1. **Invariant: Item conservation.** After any sequence of pickup/drop/trade actions by both players, total item count on map + in inventories is conserved.
2. **Invariant: Position consistency.** After any sequence of movement actions, both sides agree on all entity positions.
3. **Invariant: No crash.** No sequence of valid actions causes a crash, assertion failure, or UB (sanitizer clean).

Model: generate `N` random `(player_id, action, params)` tuples, feed them through the server action queue in random order, assert invariants after drain.

**File:** `tests/coop_pbt_test.cpp` — new file. Tag: `[coop][pbt][.]` (hidden — slow).

**Feasibility note:** RapidCheck is header-only and integrates with Catch2 via `rc::prop`. Adding it as a vendored dependency is the same pattern as Catch2 itself (bundled in `tests/catch/`). If adding a dependency is undesirable, implement a simpler random-action generator without RapidCheck — the invariant assertions are more valuable than the shrinking feature.

**Dependencies:** Step 1 (in-process harness for single-process execution).

### Step 7: Promote E2E Harness to CI

**Problem:** The 12 E2E scenarios in `scripts/test_coop.ts` can only run locally with Deno installed. They are likely stale — nobody runs them regularly.

**Solution:**
1. Add a GitHub Actions workflow (`.github/workflows/coop-e2e.yml`) that:
   - Builds the coop binary (`cmake --preset linux-full` with `-DCOOP=ON`)
   - Installs Deno (`denoland/setup-deno@v2`)
   - Runs `deno run scripts/test_coop.ts` for each of the 12 scenarios
   - Fails the job if any scenario returns nonzero

2. Optionally add Linux `tc netem` fault injection between the two processes for the reconnect and movement scenarios (requires `sudo` in CI — check if GitHub Actions runners support it; they do on `ubuntu-latest` with `sudo tc`).

**File:** `.github/workflows/coop-e2e.yml` — new file.

**Dependencies:** None — independent of Steps 1-6. Can be done in parallel.

### Step 8: Deserialization Fuzz Target

**Problem:** Malformed packets (from a mismatched client version, corrupted save, or network buffer corruption) could crash the server/client. No fuzz testing exists.

**Solution:** Add a libFuzzer target for the packet deserialization entry points:
- `coop_packets.h` decode functions (JSON parse + field extraction)
- Wire framing (4-byte BE length prefix + payload)

A single `LLVMFuzzerTestOneInput` function that constructs a `coop_sim_transport` with the fuzz input as the sole inbox message, then calls the decode path and asserts no crash/UB.

**File:** `tests/fuzz_coop_packets.cpp` — new file. Build target: `coop_fuzz` (separate CMake target, not part of default test build, linked with `-fsanitize=fuzzer`).

**Dependencies:** None — independent.

## Critical Files & Anchors

| File | Symbol/Region | Reason |
|------|--------------|--------|
| `src/coop_transport.h` | `coop_transport` abstract class | **Verified:** fully abstracted. `coop_server`/`coop_client` route all I/O through virtual interface. Only `set_transport_for_test()` seams needed. |
| `src/coop_server.cpp` | `coop_world_tick()`, `send()`/`recv()` calls | **Verified:** no raw `NET_StreamSocket` calls outside `connect()`/`try_accept()`. All game-tick I/O goes through `transport_->send()`/`recv()`. |
| `src/coop_mutation_log.cpp` | `coop_collect_streamable()` + FNV-1a hash | Existing checksum approach to extend for Step 2 |
| `tests/coop_sim_transport.h` | `coop_sim_transport` | Already tested, ready to plug in — the key enabler for Steps 1-6 |
| `src/coop_client.cpp` | `coop_world_tick()`, `apply_sync()`, pending_actions ring buffer | Client-side tick loop that must be drivable by `coop_sim_transport` for in-process tests |

## Verification

### Step 1 gate
```bash
cmake --build --preset osx-coop --target cata_test-tiles
./out/build/osx-coop/tests/cata_test-tiles "[coop][inproc]"
```
Expected: Host sends initial sync via sim transport → client receives and applies → movement action relayed → proxy moves → reconcile matches. All in-process, no sockets, completes in <2 seconds.

### Step 2 gate
After running the in-process movement scenario for 10 ticks, `coop_world_checksum()` on host equals the checksum computed from client's received state. Divergence = test failure with hex diff of the two checksums.

### Step 3 gate
```bash
./out/build/osx-coop/tests/cata_test-tiles "[coop][soak]"
```
500-tick random walk completes without checksum divergence, assertion failure, or sanitizer report. Runtime <30 seconds.

### Step 4 gate
```bash
./out/build/osx-coop/tests/cata_test-tiles "[coop][adverse]"
```
Movement under 200ms latency converges within 5 ticks of the last action. 5% loss triggers resync and recovers. Reorder doesn't crash.

### Step 7 gate
GitHub Actions CI job runs all 12 E2E scenarios on `ubuntu-latest` and reports pass/fail per scenario in the job summary.

### Existing test baseline preserved
```bash
./out/build/osx-coop/tests/cata_test-tiles "[coop]"
```
All 273 existing assertions still pass — nothing removed or weakened.

## Assumptions & Contingencies

1. **~~`coop_transport` abstraction~~ — RESOLVED.** Verified: both `coop_server` and `coop_client` are fully abstracted. Only two `set_transport_for_test()` inline methods are needed. No production code refactoring.

2. **RapidCheck dependency (Step 6).** If adding a vendored dependency is unwanted, implement a simpler `std::mt19937`-based random action generator with manual invariant checks. Loses automatic shrinking but keeps the core value (randomized concurrent action testing).

3. **CI `sudo` for `tc netem` (Step 7).** GitHub Actions `ubuntu-latest` runners support `sudo` and have `iproute2` installed. If this changes or is restricted, drop the netem fault injection from CI and keep it as a local-only test script.

4. **`g` singleton constraint — RESOLVED.** `coop_client::apply_sync()` does require `g` for map mutations. In-process tests share the single `g`, which means server and client operate on the same world. This is fine for relay/crash/adverse-network testing. Client prediction divergence testing stays in the 2-process Deno harness.

5. **Server receiver thread in in-process tests.** `coop_server::start_receiver_thread()` spawns a `std::jthread` that calls `transport_->poll()`/`recv()` in a loop. With `coop_sim_transport`, the receiver thread would call `recv()` (which pops from `inbox_`) concurrently with the main test thread's `send()` (which pushes to `peer_->inbox_`). Since `std::deque` is NOT thread-safe for concurrent push/pop, this is a data race. **Primary approach:** skip `start_receiver_thread()` entirely and drive receive manually on the main thread using the `wait_for_join_info()` pattern (pre-receiver path, already exists in `coop_server`). This gives deterministic, single-threaded control over message delivery — strictly better for testing. Fallback: add a `std::mutex` to `coop_sim_transport::inbox_` if threaded testing is ever needed, but this is unnecessary for the initial overhaul.
