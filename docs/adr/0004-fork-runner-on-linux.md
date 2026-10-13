# ADR-0004: Fork runner on Linux, in-process runner elsewhere

**Status:** Accepted 2026-10-13. The Phase 1 zygote spike passed on the fork mechanics (CPU compute, pool off, 1437 cases in parallel children, no sqlite or hang hazards; see `plans/agentic-test-reapproach.md`), with open work: 4 `vision_*` cases need the GPU path and stay on the in-process runner, 3 cases fail only when forked (2 child SIGSEGVs, 1 state-dependent), and `DISPLAY`/`WAYLAND_DISPLAY` must be unset or the parent core-dumps at exit. If those cannot be closed, this ADR is superseded.

**Decision:** On Linux the test binary initialises the game once, then runs each selected test case in its own forked child (up to about 28 in parallel, each with a timeout, each with its own user directory) and reports from the parent. Windows and macOS keep the existing single-process runner (`session.run()`, `tests/test_main.cpp:466`). Test cases are not edited for either runner.

**Why:** Every `cata_test-tiles` process pays about 11 s of init (`tests/test_main.cpp:451`) whatever it runs, the unsharded gate run takes 12 to 15 minutes, and one SIGSEGV (`tests/driver_loop_test.cpp:535`) truncates a whole run. One init plus a fork per case turns the full suite into roughly init plus the sum of case times over the core count, and confines a crash, hang or state leak to one case. `fork()` exists only on Linux, and Linux is the agent executor (ADR-0005).

**Consequences:**

- A leak between cases (the `vision_test.cpp:253` family) is invisible on Linux and surfaces only on the in-process runner, in a platform sweep. Such a leak is a fixture bug, and the sweep reports it as a platform-only failure.
- Children must not rely on state that does not survive `fork()`: the thread pool's workers are absent in the child and `parallel_for` would block (`thread_pool.h:206`), so tests run with the pool disabled or recreated in the child; the world's sqlite connection is reopened per child.
- Two runner semantics exist and must agree on every case that passes on both.
- Rejected: shard harder in one runner (still one init per shard, a crash still truncates a shard); port per-case process spawn to Windows and macOS (loses the shared-init win there).
- The zygote runs with `CATA_TEST_COMPUTE_ACCELERATION=cpu` and `MULTITHREADING_ENABLED:false`, because `init_test_sdl_gpu()` (`tests/test_main.cpp:248`) runs before the world load and a live SDL/Vulkan device does not survive `fork()`. `[gpu]` and `[render]` cases run on the in-process runner.
- Measured (spike, 2026-10-13): full suite 110 to 115 s at 32 children against 198 s sharded in-process on the same box; 6 of the 7 baseline failures on the Linux default path are state leaks that pass in a fresh child. The Linux default is the lavapipe GPU path, not the CPU path, so the zygote's CPU compute is a configuration difference that routes the 4 `vision_*` cases to the in-process runner.
