#pragma once
#include <box2d/box2d.h>

namespace lighting { class debug_line_pass; } // forward-declare GPU line buffer

namespace physics {

/// Debug-draw context for b2DebugDraw callbacks.
/// Callbacks convert Box2D metres to world-tile coords (÷ TILE_M) and push
/// line segments into the GPU debug_line_pass.
struct DebugDrawContext {
    lighting::debug_line_pass *pass; ///< GPU line buffer — lifetime owned by render_state
    float                      m2t;  ///< metres-to-tiles scale factor = 1.0f / TILE_M
};

/// Build a fully configured b2DebugDraw that populates a GPU debug_line_pass
/// with collision shapes, contact manifolds, and body transforms.
/// @p ctx must outlive any b2World_Draw() call using the returned struct.
auto make_debug_draw( DebugDrawContext *ctx ) -> b2DebugDraw;

} // namespace physics
