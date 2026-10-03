# ADR-0002: Retire the GPU pull-on-generations seam in favour of the pushed rebuild plan

**Status:** Accepted 2026-10-02. Supersedes the pull seam of plans/level-cache-freshness.md ticket #21; preserves the residency event guarantee of #20. Execution: #37 landed (plan derivation + `build_map_cache` consumption + caller sweep); #38 (`apply_residency_events`) and #39 (frame gate consumes the plan) pending.

**Decision:** Staleness is derived once per consumption point by `level_cache_freshness::plan_for` and pushed to the CPU builder, the GPU residency layer, and the render-frame gate. Generation counters remain as the module's internal diff substrate and as the residency event carrier inside the plan; no consumer polls them directly anymore.

**Why:** Three independent derivations of one fact set drifted apart by construction (the frame gate already ignored residency entirely). One derivation makes the disagreement bugs of issue #7/#18/#19 shapes structurally impossible and gives the equivalence harness a single value to capture.

**Guarantee preservation:** #20's "every residency-changing event reaches the lighting layer exactly once" is carried by `residency_delta` (generation-delta accounting, not bit inspection) and pinned by the existing jump-count tests rewired to plan events.

## Notes from execution (#37)

- `plan_for` takes `map &` (not `const map &`): the normal-policy path reconciles the
  light-source signature, which raises the `light_changed` kind — a mutation. The
  escalation law lives in the type: `lightmap_policy::skip` derives
  `defer_without_escalation` without sampling, so escalating on an unclearable bit is
  unrepresentable.
- The plan carries `residency_snapshot` (absolute per-level generations + an opaque
  bubble-shift stamp), not pre-diffed deltas: the consumer (#38) diffs against its own
  last-applied state, which keeps today's poll semantics under push form and needs no
  new mutable state in the module.
- `pose_stamps.bubble_origin` is a `point_abs_sm` compared opaquely (equality of the
  stamp tuple); the module never interprets camera drift, it only carries it.
- `map::invalidate_lightmap_if_light_state_changed` and
  `level_cache_freshness::lightmap_needs_rebuild` were deleted with this cutover; the
  producer `map::current_lightmap_source_signature` stays on `map` (it walks the submap
  hash) and `plan_for` calls it.
