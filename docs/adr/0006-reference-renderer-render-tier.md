# ADR-0006: One reference renderer; Render tier in Catch2

**Status:** Accepted 2026-10-13. Decided in the grilling session behind `plans/agentic-test-reapproach.md`.

**Decision:** Pixel correctness is verified by a Render tier of Catch2 cases that render frames offscreen in `cata_test-tiles` on one machine and driver, the native Windows side of the home box with its RTX 4090 (the Reference renderer), and compare against golden images or paired runs under a noise mask. Metal output is never verified automatically; the owner checks it on the Mac. The windowed capture and the three pixel oracles in `tools/bnplay` (`paired_null`, `diff_vs_null`, `triplet`) are replaced by two small pure image functions ported into the test code.

**Why:** Windowed capture needs a real swapchain, a 19 to 23 s boot and 2.9 GB, and bnplay documents same-state Windows captures as non-deterministic (`SKILL.md:159`, cause unknown). ADR-0003 already tests frame structure without a GPU. Pixel checks need one controlled GPU to mean anything, and the box is it.

**Consequences:**

- Prerequisites before the first Render case is trustworthy: commit the two untracked `emitter_glow.*.hlsl` files (a clean Windows clone is red, `emitter_glow_pass.cpp:70`); force the GPU path instead of the lavapipe pin (`CATA_TEST_COMPUTE_ACCELERATION=gpu`) and assert the device name; keep the reality bubble at 6 or fewer or fix `sdf_pass.cpp:220-224` sizing; inject a fixed clock (frames read wall clock at `cata_tiles_anim.cpp:110` and `sdl_render_frame.cpp:870,1500,1794`).
- The unknown non-determinism cause stays a noise mask, not a fix, until found.
- Metal-only rendering regressions are found by the owner, not the loop.
- Rejected: keep windowed capture in the thin bnplay CLI (keeps the boot cost and process supervision for every render check).
