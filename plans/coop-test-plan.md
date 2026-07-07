# Co-op Test Suite Improvement Plan

**Status:** Planning — not yet started.  
**Goal:** Eliminate every gap identified in the 2026-07-07 audit.  
**Principle:** Phases ordered by value/cost — independent cheap wins first, deliberate enabling investments later. User can stop at any phase and have real coverage delivered.

---

## Gap Reference

| ID | Description | Severity | Independent? |
|---|---|---|---|
| A | Malformed / adversarial framing | 🔴 | Yes |
| B | TSan + ASan/UBSan in CI (two jobs) | 🔴 | Yes |
| H1 | Single-scenario harness | 🟠 | Yes |
| H4 | Blunt 180s timeout | 🟡 | Yes |
| 8 | Concurrent pickup race | 🟡 | Yes |
| H3 | Real dnctl/netem injection | 🟡 | **Deferred** — superseded by Phase 5 sim transport, which covers network-condition correctness deterministically. Optional real-latency smoke job may be added later. |
| H2 | /tmp IPC doesn't scale | 🟠 | Partial (needs H1 first) |
| 1 | Resync round-trip | 🔴 | Needs H1+H2 |
| 2 | Non-MOVE action relay | 🔴 | Needs H1+H2 |
| 3 | Client disconnect/cleanup | 🔴 | Needs H1+H2 |
| 5 | Submap shift reconciliation | 🟠 | Needs H1+H2 |
| 9 | C3b death relay | 🟡 | Needs H1+H2 |
| C | Lag-comp/reconcile under latency | 🔴 | Needs transport refactor |
| 4 | Ring buffer overflow (>32) | 🟠 | Needs transport refactor |
| 6 | seq uint32_t near-overflow | 🟠 | Needs transport refactor |
| 7 | Wall-blocked move + pending | 🟡 | Needs transport refactor |

---

## Phase 1 — Cheap Independent Wins

**No refactor required. These three items are fully independent. Deliver immediately.**

### 1a: Adversarial framing tests (Gap A)

Add 6 negative test cases to `tests/coop_net_test.cpp` using the existing `LoopbackFixture`. First enforce a max-frame-size cap in `coop_net::recv` (e.g., 4 MB hard reject):

```cpp
TEST_CASE("coop_net recv: length=0 skipped, no crash",         "[coop][net]") { ... }
TEST_CASE("coop_net recv: length=UINT32_MAX rejected",         "[coop][net]") { ... }
TEST_CASE("coop_net recv: truncated frame does not hang",      "[coop][net]") { ... }
TEST_CASE("coop_net recv: garbage bytes before valid frame",   "[coop][net]") { ... }
TEST_CASE("coop_net recv: action packet before handshake",     "[coop][net]") { ... }
TEST_CASE("coop_net recv: duplicate seq handled idempotently", "[coop][net]") { ... }
```

`coop_net::recv` must return `false` (not crash, not hang, not alloc 4 GB) on all adversarial inputs. The truncated-frame test needs a short recv timeout.

### 1b: Sanitizer CI presets (Gap B)

TSan and ASan are **mutually exclusive** — `-fsanitize=thread,address` is a hard compiler error. Two separate presets and CI jobs are required:

**`ci-coop-tsan`** — Thread Sanitizer, targets the `receiver_thread_` ↔ main-thread interface:
```json
{
  "name": "ci-coop-tsan",
  "inherits": ["ci-coop"],
  "displayName": "CI Co-op TSan (thread sanitizer)",
  "cacheVariables": {
    "CMAKE_CXX_FLAGS": "-fsanitize=thread -fno-omit-frame-pointer",
    "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=thread"
  }
}
```

**`ci-coop-asan`** — Address + UBSan (these combine safely), targets memory safety and framing paths:
```json
{
  "name": "ci-coop-asan",
  "inherits": ["ci-coop"],
  "displayName": "CI Co-op ASan+UBSan (address + undefined behaviour)",
  "cacheVariables": {
    "CMAKE_CXX_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer",
    "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=address,undefined"
  }
}
```

Add two CI jobs to `.github/workflows/matrix.yml`: `coop_tsan` and `coop_asan`. Both build `cata_test-tiles`, run `[coop][net]` unit tests, and run the two-process integration test (both processes must be instrumented — expect slow runs). The TSan job specifically stress-tests the `receiver_thread_` ↔ main-thread interface; the "no mutex needed" claim has never been sanitizer-verified. Run both now — they may surface existing races or leaks.

### 1c: Concurrent pickup race (Gap 8)

Extend `tests/coop_pickup_test.cpp` with one test:

```cpp
TEST_CASE("apply_pickup_manifest: item already absent is skipped gracefully", "[coop][pickup]") {
    // Place 1 knife. Apply pickup manifest (client took it).
    // Apply identical manifest again (host avatar also tries to take it).
    // Assert: second call is a no-op; count stays 0, no crash.
}
```

**Phase 1 acceptance:**
- [ ] 6 adversarial framing tests pass; no unbounded alloc on oversize length
- [ ] `ci-coop-tsan` builds and runs `[coop]` tests with zero TSan race reports
- [ ] `ci-coop-asan` builds and runs `[coop]` tests with zero ASan/UBSan reports
- [ ] Concurrent pickup test passes

---

## Phase 2 — Harness Extensibility (H1, H2, H4)

**Prerequisite:** Phase 1 complete.  
**Enables:** All new E2E scenarios in Phase 3.

### H1: Scenario routing via `COOP_SCENARIO`

Both host and client roles in `coop_integration_test.cpp` read `COOP_SCENARIO` from the environment and dispatch to a scenario function. Default: `"movement"` (existing behaviour, unchanged).

```cpp
TEST_CASE("coop integration: host role", "[.][coop_role_host]") {
    const auto scenario = std::string(std::getenv("COOP_SCENARIO") ?: "movement");
    if      (scenario == "movement")   run_host_movement(srv);
    else if (scenario == "pickup")     run_host_pickup(srv);
    else if (scenario == "resync")     run_host_resync(srv);
    else if (scenario == "disconnect") run_host_disconnect(srv);
    else if (scenario == "death")      run_host_death(srv);
    else { FAIL("unknown COOP_SCENARIO: " + scenario); }
}
```

`scripts/test_coop.ts` gains a `COOP_SCENARIO` pass-through:
```ts
const scenario = Deno.env.get("COOP_SCENARIO") ?? "movement"
// passed as env to both spawned processes
```

### H2: Test control socket (port 0 ephemeral)

Replace all scenario-specific `/tmp/coop_test_*.txt` files with a single bidirectional control socket. Host binds on **port 0** (OS-assigned — avoids inheriting the 45802-range conflict class), reads the assigned port back via `getsockname`, writes it to `/tmp/coop_test_ctrl_port.txt`. Client connects.

Both sides exchange newline-delimited text signals: `PROXY_POS x y z`, `ITEM_POS x y z type`, `SCENARIO_DONE`, `CLIENT_READY`, `HASH_SENT h`, etc. The existing `/tmp/coop_test_port.txt` for the game socket is unchanged; only scenario-payload files are replaced.

This makes every scenario O(1) in coordination files regardless of complexity.

### H4: Staged timeouts

```ts
const PHASE_TIMEOUTS_MS = {
  handshake:   10_000,
  world_load:  30_000,
  scenario:    60_000,
  shutdown:     5_000,
}
```

Harness watches for known log lines (`[coop] listening`, `[coop] handshake complete`) or control-socket phase messages to clock each stage independently. On phase timeout: kill both processes immediately and report which phase failed rather than waiting 180s.

**Phase 2 acceptance:**
- [ ] `COOP_SCENARIO=movement deno task test:coop` passes (behaviour unchanged)
- [ ] `COOP_SCENARIO=invalid deno task test:coop` exits 1 immediately
- [ ] Control socket connects and exchanges at least one message in the movement scenario
- [ ] A handshake timeout (simulated by delaying port file write) triggers within 10s, not 180s

---

## Phase 3 — New E2E Integration Scenarios (Gaps 1, 2, 3, 5, 9)

**Prerequisite:** Phase 2 complete.  
**Each scenario:** a `run_host_*` / `run_client_*` function pair in `coop_integration_test.cpp`, run via `COOP_SCENARIO=<name> deno task test:coop`.

### Gap 1: Resync round-trip (`COOP_SCENARIO=resync`)

- Host sends one sync, writes hash to control socket (`HASH_SENT <h>`).
- Client receives sync, deliberately skips applying one mutation to compute a divergent hash, sends `resync_request` packet.
- Host receives `resync_request`, sets `force_resync_ = true`, sends full sync on next tick.
- **Host asserts:** `force_resync_` was consumed (cleared) and a full sync was emitted.
- **Client asserts:** received a full sync (tile array non-empty) not a delta.

### Gap 2: PICKUP relay (`COOP_SCENARIO=pickup`)

- Host places `knife_combat` at a known position before initial sync, writes position+type to control socket (`ITEM_POS x y z knife_combat`).
- Client reads from control socket, builds PICKUP `ctx_json`, queues `PICKUP` action.
- Host ticks and processes the action via `execute_client_action` → `apply_pickup_manifest`.
- **Host asserts:** item gone from map at that position.
- **Client asserts:** item also absent locally (optimistic removal).

### Gap 3: Client disconnect / proxy cleanup (`COOP_SCENARIO=disconnect`)

- Client connects, handshakes, receives world_seed, then immediately closes its TCP socket without sending a shutdown packet.
- Host receiver_thread_ detects TCP drop; sets `running_ = false`.
- **Host asserts:** within 3 `coop_world_tick()` calls, proxy NPC is removed from `g->critter_tracker` and `g->coop_server_` is reset to nullptr.

### Gap 5: Submap shift reconciliation (`COOP_SCENARIO=submap_shift`)

- Client queues ≥12 MOVE_N actions (crosses one submap boundary).
- Host processes all actions; submap shift triggers `build_and_send_sync()` with full 5×5 blast.
- **Host asserts:** proxy reached position exactly 12+ tiles north.
- **Client asserts:** reconciliation drift ≤5 tiles after the submap sync; pending_actions_ cleared.

### Gap 9: C3b death relay (`COOP_SCENARIO=death`)

- Host uses test seam to set `client_hp_pct_(0)` and `client_dead_(true)`.
- Host ticks once.
- **Host asserts:** `client_death_announced_` flipped true; proxy HP clamped to 1 (not 0, which would trigger `npc::die()`); death-drop manifest queued in send_q_.
- Host ticks again.
- **Host asserts:** `client_death_announced_` still true (once-only gate — no second death message).

**Phase 3 acceptance:**
- [ ] `COOP_SCENARIO=pickup deno task test:coop` passes
- [ ] `COOP_SCENARIO=resync deno task test:coop` passes
- [ ] `COOP_SCENARIO=disconnect deno task test:coop` passes
- [ ] `COOP_SCENARIO=submap_shift deno task test:coop` passes
- [ ] `COOP_SCENARIO=death deno task test:coop` passes

---

## Phase 4 — `coop_transport` Interface Refactor

**This is the deliberate enabling investment for the deterministic test suite (Phase 5). It is behavior-preserving by construction — the existing `[coop][net]` and `[.][coop_role_host/client]` tests are the regression gate.**

**Files:**
- `src/coop_transport.h` — abstract interface (3 pure virtuals)
- `src/coop_net_transport.h/.cpp` — real SDL_net impl wrapping existing `coop_net::send/recv/poll`
- `src/coop_server.h/.cpp`, `src/coop_client.h/.cpp` — thread `coop_transport*`; remove direct `NET_StreamSocket*` / `coop_net::` call sites (~20–30 sites)

```cpp
struct coop_transport {
    virtual auto send(const std::string& msg) -> bool = 0;
    virtual auto recv(std::string& out, int timeout_ms) -> bool = 0;
    virtual auto poll() -> bool = 0;
    virtual ~coop_transport() = default;
};
```

**Regression gate:** after every change in this phase, the following must all pass:
- `[coop][net]` — real socket framing unchanged
- `[.][coop_role_host]` / `[.][coop_role_client]` via `deno task test:coop`
- All other `[coop]` unit tests

**Phase 4 acceptance:**
- [ ] All existing `[coop]` tests pass unchanged after the refactor
- [ ] `deno task test:coop` (movement scenario) passes
- [ ] No game-binary behaviour change (no new public API surface visible outside tests)

---

## Phase 5 — `coop_sim_transport` + Ring-Buffer Tests (Gaps 4, 6)

**Prerequisite:** Phase 4 complete.  
**Status:** DELIVERED.

### What Phase 5 delivered

**`tests/coop_sim_transport.h`** — header-only, no SDL dependency. Programmable
latency, loss, reorder; manual simulated clock via `advance(ms)`; bidirectional
`wire_peer()`; `close_abruptly()` severs the reverse peer reference.

**`tests/coop_net_sim_test.cpp`**:

- `[coop][simtransport]` (10 tests, 29 assertions): simulator self-tests —
  delivery timing, latency boundary, zero-latency, multi-message ordering,
  loss at 0%/100%, reorder, bidirectional wiring, close_abruptly sever, recv
  non-blocking contract.  No SDL, no game world, runs in milliseconds.

- `[coop][ringbuf]` (3 tests): **Gap 4** — queue 35 actions, assert size==32
  and oldest seq dropped (cap/drop-oldest path verified).  **Gap 6** —
  `next_seq_` positioned two below `UINT32_MAX`, queue 3 actions across the
  wrap boundary, assert no crash or UB (`uint32_t` overflow is defined; the
  trim predicate `a.seq <= confirmed` also remains defined at these values —
  the scenario requires ~4.3 billion calls at turn-based rates, so no
  wrap-aware predicate change was made to the production trim path).

**Test seams added to `coop_client.h`** (all inline, no game-world access):
- `pending_actions_size_for_test()` — ring buffer size
- `pending_actions_front_seq_for_test()` — oldest surviving seq
- `set_next_seq_for_test(uint32_t)` — position next_seq_ for wrap test

### What Phase 5 does NOT deliver (and why)

**Gap C round-trip (reconcile/lag-comp under real latency):**
The full client→server→client path requires two live `game*` instances.
The `g` pointer and `coop_session::get()` are process-wide singletons — two
`coop_server`+`coop_client` instances cannot coexist in one process without
refactoring the singleton.  This is the same reason the E2E harness spawns
host and client as **separate processes**.  A transport-injection seam on
`coop_server`/`coop_client` does NOT unblock this; it only enables the
client-half of the test (scripted sync JSON → sim inbox → single-endpoint
tick).  That single-endpoint path is not implemented here because the
reconcile math is already thoroughly covered by `coop_reconcile_test.cpp`.

**Gap C coalescing (16ms→1 tick vs 80ms→2 ticks):**
`COALESCE_WINDOW_MS` is a `constexpr` local in `main.cpp`, not exported.
Testable only via the E2E harness.

**Phase 4 honest status:** behaviour-identical transport refactor.  The
abstraction enables a future in-process injection seam (client-half only),
but delivered zero new behavioural coverage itself.

### Phase 5 acceptance

- [x] `[coop][simtransport]` — 10 tests, 29 assertions pass (no SDL dependency)
- [x] `[coop][ringbuf]` — 3 tests, 6 assertions pass (Gap 4 cap/drop-oldest; Gap 6 wrap-no-crash)
- [x] No test takes >1s — 31s total is 100% game-data cold-load; tests themselves run in ms
- [x] E2E unaffected — new seams are inline header-only; no production behaviour changed

---

## Dependency Graph

```
Phase 1 (A, B, Gap 8)    ← no prerequisites — start here
    │
Phase 2 (H1, H2, H4)     ← Phase 1 recommended first, but technically independent
    │
Phase 3 (Gaps 1,2,3,5,9) ← requires Phase 2
Phase 4 (transport iface) ← independent of Phases 2–3
    │
Phase 5 (sim + ringbuf)   ← requires Phase 4
```

---

## Final Acceptance Criteria

- [ ] All `[coop][simtransport]` tests pass with zero SDL dependency
- [ ] All `[coop][ringbuf]` tests pass: Gap 4 cap/drop-oldest, Gap 6 wrap-no-crash
- [ ] All `[coop]` unit tests pass: net (incl. 6 adversarial), packets, reconcile,
      delta, idle, terrain, pickup, drop, simtransport, ringbuf
- [ ] `deno task test:coop` passes for all 6 E2E scenarios: movement, pickup,
      resync, disconnect, submap_shift, death
- [ ] `ci-coop-tsan` CI job: zero TSan race reports on `[coop]` tests
- [ ] `ci-coop-asan` CI job: zero ASan/UBSan reports on `[coop]` tests
- [ ] Gap C coalescing + round-trip: tested via E2E harness (future scenario)
- [ ] Zero unbounded alloc or hang on adversarial framing inputs
