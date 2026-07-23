# Terraria-Style Async Co-op Join

## Problem

Host must wait on a blocking modal "Waiting for client..." screen before entering
gameplay.  The entire handshake sequence (TCP accept → version handshake → world
seed → join info → proxy spawn → initial sync → receiver thread) runs synchronously
on the main thread, freezing the host UI.

## Goal

Host starts the world and plays immediately.  Client connections are accepted and
handshaked asynchronously during `coop_world_tick()` without interrupting host
gameplay.  Clients can still join and leave freely (reconnection already works).

## Architecture

### New state machine: `client_join_phase`

```
listening ──► handshaking ──► finalizing ──► connected ◄──► disconnected
    ▲                                                          │
    └──────────────────── (reconnect timeout) ─────────────────┘
```

| Phase | Thread | Work |
|-------|--------|------|
| `listening` | main | Non-blocking `try_accept()` each tick; host plays normally |
| `handshaking` | background | `handshake()` → `send_world_seed()` → `wait_for_join_info()` |
| `finalizing` | main | `spawn_proxy_npc()` → `send_initial_sync()` → `start_receiver_thread()` |
| `connected` | main + IO | Normal gameplay; existing sync/action processing |
| `disconnected` | main | Existing reconnect window (300s countdown, `accept_reconnect()`) |

### Thread safety

The background handshake thread exclusively owns `transport_` during the
`handshaking` phase.  World seed data (turn, spawn pos, player name, world name,
RNG seed, session token) is captured on the main thread *before* launching the
background thread.  `client_join_pos_` and `client_worn_json_` are written by the
background thread and read by the main thread only *after* the thread completes.

### What changes

1. **`coop_server.h`** — Add `client_join_phase` enum, `join_phase_` atomic,
   `handshake_thread_`, captured seed data, `has_client()` getter, private methods
   `process_pending_join()`, `run_handshake_bg()`, `finalize_client_join()`.

2. **`coop_server.cpp`**:
   - Remove `coop_session::get().mode = host` from `try_accept()` (moved to menu).
   - `process_pending_join()`: listening → try_accept + launch bg thread;
     handshaking → poll result; finalizing → spawn + sync + receiver.
   - `run_handshake_bg()`: handshake + send_world_seed + wait_for_join_info.
   - `finalize_client_join()`: spawn_proxy_npc + send_initial_sync +
     start_receiver_thread + message.
   - `coop_world_tick()`: simplified guard, delegates to `process_pending_join()`
     for pre-connected phases, calls `post_action_world_step()` so host plays.
   - `handle_client_disconnect()`: also sets `join_phase_ = disconnected`.
   - `accept_reconnect()`: also sets `join_phase_ = connected`.
   - `shutdown()`: also sets `join_phase_ = listening`.

3. **`coop_menu.cpp`** — `start_host()` registers `coop_server_` on `g` and sets
   `mode = host` immediately after `listen()`.  No blocking wait, no inline
   handshake.  IP info shown via `add_msg()`.

4. **`panels.cpp`** — Coop panel shows "[WAITING FOR PARTNER]" when
   `!has_client()`, existing partner stats otherwise.

### What stays the same

- Client-side flow (`start_join()`) remains synchronous — the client *must* wait
  for the world seed before it can play.
- Reconnection protocol unchanged.
- Per-tick sync, reconciliation, mutation log, overmap sync — all unchanged.
- Session token generation, proxy NPC lifecycle, receiver thread — mechanics
  unchanged, just triggered asynchronously.
