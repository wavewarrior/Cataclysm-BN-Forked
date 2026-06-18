#pragma once

// A0 compute go/no-go spike (GI_COMPUTE_AND_PERF_PLAN.md, Part A0).
//
// The engine has no compute pipelines today (shader_compiler only did
// VERTEX/FRAGMENT until A0). Before investing in the real gi_compute_pass
// rework, this proves a minimal D3D12 compute pipeline — a dynamic [loop] over a
// readonly StructuredBuffer<float> writing one RWStructuredBuffer<float> — both
// CREATES and RUNS. That dynamic-SB-loop pattern is exactly what SDL_shadercross
// mishandles in the FRAGMENT stage on D3D12 (forcing the current RC GI into a
// loop-free stub); the spike's question is whether the COMPUTE stage dodges it.
//
// Result is logged to DC::Main (DC::SDL is filtered):
//   * pipeline creates + dispatch returns the expected sum → GO (proceed A1+).
//   * pipeline create fails (E_INVALIDARG / 0x80070057) → NO-GO (CPU-GI
//     contingency).
//
// Called once from render_state::init after init_shader_compiler. Self-contained
// and throwaway — delete this file + its init call once A0 is resolved. Never
// fatal: logs and returns regardless so a spike failure cannot break startup.

namespace lighting
{

class gpu_device;

// Compile + dispatch the minimal compute spike, logging the go/no-go verdict to
// DC::Main. Returns true if the pipeline created AND the dispatch produced the
// expected result; false otherwise. Safe to call when the device is not ready
// (logs + returns false).
bool run_compute_spike( gpu_device &dev );

} // namespace lighting
