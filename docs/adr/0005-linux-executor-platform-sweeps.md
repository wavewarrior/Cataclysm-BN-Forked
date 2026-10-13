# ADR-0005: Linux executor; Windows and macOS verified by sweeps, not per change

**Status:** Accepted 2026-10-13. Decided in the grilling session behind `plans/agentic-test-reapproach.md`.

**Decision:** Agents build and test on the Linux (WSL2) side of the home Windows box, and per-change verification runs on Linux only. Windows and macOS correctness is verified by platform sweeps (GLOSSARY: Platform sweep): a scheduled native Windows sweep on the box, and a macOS sweep the owner opts into, which never blocks anything and covers every commit since its watermark. The Mac is the owner's workstation, not an executor.

**Why:** The Mac (`osx-arm-slim`) is the slow, swap-limited place: a leaf edit is about 22 s and a `map.h` edit about 630 s, and two games exhaust swap. The box has 24 cores and 64 GB, runs the factory today, and costs nothing extra. Linux is the one platform that allows the fork runner (ADR-0004). Both Windows and macOS ship, so this knowingly trades earlier detection for a fast loop: `config.json:48-62` shows 13 macOS-only failing tests, so platform divergence is real.

**Consequences:**

- A platform-only bug can reach the integration branch before a sweep reports it; the sweep opens an issue and the loop treats it like any other ticket.
- The Linux cleanliness of the suite is unmeasured today (`baselineFailuresLinux` is empty and never selected, `tools/factory/platform.ts` has no Linux entry); the first Linux run establishes the real list, which goes into Quarantine.
- MSVC-only and Metal-only compile or behaviour errors are caught by sweeps, not by the agent's `check`.
- Rejected: gate every change on all three platforms (slow, and ties the loop back to the Mac); run agents on the Mac with remote offload (a remote-exec protocol for little gain).
