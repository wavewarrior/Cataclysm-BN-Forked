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
- Wire byte-identical in the interpreter commit, no protocol bump. The field-intensity carrier (actually `old_value`, see D3) is a named accessor inside the interpreter. Commit 3 is the one deliberate wire change.
- Rolling back `field_expired` restores intensity 1 until commit 3 carries intensity on expiry events; commit 3 removes that limit.
- `pending_gift`: implement server emission as the declined-item restore path (host declines a client `trade_offer`; server echoes the item JSON in the next sync; client re-adds). Lands last, as its own commit.

## Commit order

1. DONE (`2e3e692736`): characterisation tests. Terrain/furniture round-trips and a wire test through the real `build_and_send_sync` -> `apply_sync` pass. Field created/expired and field_changed rollback cases are known-fail, tagged `[.][coop_known_fail][rollback]` (not `[coop]`, because Catch2 runs hidden cases named by a filter tag).
2. DONE (`e18c3208a0`, #41): Interpreter conversion (`src/coop_world_event_interpreter.{h,cpp}`). Server, client `apply_sync` event array and `coop_rollback_engine::rollback_to` delegate to it; `reverse_delta`/`reverse_type` deleted. Wire bytes stayed identical. Producers put field intensity in `old_value` while the wire carries `creature_id`, so it is lost (D3); readers go through `field_intensity_carrier` so #42 changes one place. D1 and D2 are gone by construction (inverse named per operation, no type flip); the three field cases turned green and regained `[coop]`. `[coop]`: 170 cases, 688 assertions pass.
3. DONE (#42): field-intensity carrier. Producers in `map_field.cpp` set `.creature_id` explicitly: new intensity on created/changed, pre-expiry intensity on expired. Fields now replicate at their real intensity, `field_changed` reaches the partner, and rolling back an expiry restores the exact intensity. The one deliberate wire and hash change: a host and partner built apart disagree on the event hash for field events and degrade to rollback plus full resync. `[coop]`: 170 cases, 689 assertions pass; `[field]` passes.
4. DONE (#43): `pending_gift` declined-item restore. `coop_server::resolve_trade_offer(json, accepted)` acks the client and either adds the item to the host or stashes the offer; the next sync emits it once as `pending_gift`, which the client already parsed. No new packets or wire keys. Tested through the real sync builder and `apply_sync`: a declined offer returns the item exactly once, an accepted one is never echoed. `[coop]`: 172 cases, 709 assertions pass.

## Defects found by commit 1

- D1 double inversion: `reverse_delta` flips field_created/field_expired, then `rollback_to` inverts again.
- D2 value/old_value swap: `reverse_delta` swaps them, `rollback_to` reads the field type out of `value`; for field_changed it looks up the old intensity as a field type.
- D3 producer carrier misalignment: `map_field.cpp:1020` and `:1737` brace-initialise positionally, so intensity lands in `old_value`, not `creature_id`. Fields replicate at intensity 1 and field_changed is a client no-op.

## Out of scope

Whole-sync-packet codec; the turn-plan candidate (`do_turn` / `coop_client_turn_step`).
