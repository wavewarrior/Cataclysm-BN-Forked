# World event interpreter

Source of truth for this effort. Vocabulary follows `GLOSSARY.md` (Co-op: World event, World event interpreter, Rollback).

## Problem

Per-type knowledge of a World event is spread over four places that must agree by hand:
server filter/hash (`coop_collect_streamable`, `coop_hash_event`), client apply with hand-captured old values (`coop_client.cpp:704-771`), `reverse_delta` (`coop_mutation_log.cpp:72-98`), and `coop_rollback_engine::rollback_to` (`coop_rollback.cpp:36-63`).

Verified live defect: `reverse_delta` already flips `field_created` <-> `field_expired`, then `rollback_to` switches on the flipped type and applies inverse semantics again. Rolling back a created field re-adds it; rolling back an expiry removes it. `reverse_type` is written and never read. Rollback runs only on hash mismatch (`coop_client.cpp:1040`) and is followed by a full sync. Zero rollback tests exist.

## Decisions (grilled 2026-10-04)

- Scope: the 5 replicated types (terrain, furniture, field created/changed/expired). The other 7 stay hash-only.
- `apply(map&, event)` returns the recorded, invert-ready event; callers push it to the rollback engine. Old-value capture lives once, in the interpreter. `invert(event)` is the matching inverse. The map is a parameter, not `g->m`, so tests use `build_test_map`.
- Boundary: the interpreter owns applying and (de)serialising the delta event array only. The other `apply_sync` keys are untouched.
- `coop_rollback_engine` keeps its ring buffer; `rollback_to` delegates invert+apply to the interpreter. `reverse_type` is removed if nothing reads it.
- `coop_hash_event`, `coop_hash_event_extended`, `coop_collect_streamable` stay free functions with their existing tests. The interpreter exposes the streamable predicate so the 5-type set lives once.
- Wire byte-identical, no protocol bump. The `creature_id`-as-intensity overload becomes a named accessor inside the interpreter.
- Known limit, pinned and documented: rolling back `field_expired` restores intensity 1 (the expiry event, `map_field.cpp:1136`, carries no intensity).
- `pending_gift`: implement server emission as the declined-item restore path (host declines a client `trade_offer`; server echoes the item JSON in the next sync; client re-adds). Lands last, as its own commit.

## Commit order

1. Characterisation tests against current behaviour: apply-then-invert round-trip table over the 5 types, and wire round-trip for the event array. The field-rollback case is tagged `[.]` as known-fail.
2. Interpreter conversion (server, client `apply_sync` event array, rollback delegate). The field case turns green and loses `[.]`.
3. `feat`: `pending_gift` declined-item restore.

## Out of scope

Whole-sync-packet codec; the turn-plan candidate (`do_turn` / `coop_client_turn_step`); carrying intensity on expiry events.
