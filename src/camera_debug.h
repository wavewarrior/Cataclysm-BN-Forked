#pragma once
#ifndef CATA_SRC_CAMERA_DEBUG_H
#define CATA_SRC_CAMERA_DEBUG_H

// CPU-only camera tuning knobs, driven by the F4 dev panel (RmlUi) and pushed
// into game::main_camera_ each frame from draw_ter. Deliberately NOT part of
// any GPU cbuffer (debug_params) — these never reach a shader.
namespace camera_dbg
{
// Exponential follow ease rate (1/s). Matches camera_2d's default.
inline float smooth_speed = 12.0f;
// Tiles of look-ahead lead at walking speed. 0 = off.
inline float look_ahead = 0.0f;
// Dead-zone radius in tiles; the view holds while the player stays within it.
// 0 = off. Kept float for direct RmlUi slider binding.
inline float dead_zone = 0.0f;
} // namespace camera_dbg

#endif // CATA_SRC_CAMERA_DEBUG_H
