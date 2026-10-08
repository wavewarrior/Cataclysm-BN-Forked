# ADR-0003: Test the render frame through a pure frame plan, not a fake GPU device

**Status:** Accepted 2026-10-07. Decided in wayfinder map "Frame-assembly module" (ticket "Test seam: pure frame_plan value or recording fake device"); executed under the spec issue "Frame-assembly module".

**Decision:** The render frame is tested through a `frame_plan`: an ordered list of named steps, each marked run or skip with a reason, computed as a pure function of a by-value `frame_inputs` and executed by one loop. There is no interface under `gpu_device` or `render_state` and no recording fake device. Pass order, skip reasons and ordering laws are asserted in `cata_test-tiles` without a GPU; what happens inside a pass body is not tested at this seam.

**Why:** The frame makes almost no direct `SDL_GPU` calls. It drives 24 `render_state` sub-objects through 63 distinct non-virtual methods and reads about 135 knob globals, so a recording fake would have to abstract all of that, re-implement each pass's `ready()` and `populated()` answers, and pin incidental call sequences such as scissor changes. The gates already compute run flags and reason strings from knobs, readiness flags, `rc_rebuild` and previous-frame history, which is a plan in disguise. With a plan, a skip reason is a first-class assertion (`gi=skip:disabled`); with a fake, a skipped pass is only an absent call. One executor loop dispatching from the plan makes the asserted order the executed order by construction.

**Consequences:**
- Facts that cannot be known at plan time (values written by an earlier step and read by a later gate in the same frame) are executor-resolved dependencies recorded in the frame report, not plan-time facts.
- A recording fake `gpu_device`, and tests of pass bodies, are rejected here; revisit only as a fresh effort.
- Pixel equivalence is not provided by this seam; it is proven separately by the changed-pixel-count gate in `plans/frame-assembly-module.md`.
