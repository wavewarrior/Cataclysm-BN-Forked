#pragma once
#ifdef BOX2D_ENABLED
#include <box2d/box2d.h>

struct SDL_Renderer; // forward-declare — callers need not include SDL headers

namespace physics {

/// Camera-state baked into the debug draw context.
/// Precomputed once per frame from cata_tiles::draw() locals:
///   screen_{x,y} = origin_p{x,y} + b2_metres * m2p{x,y}
struct DebugDrawContext {
    SDL_Renderer *renderer; ///< raw pointer — lifetime owned by cata_tiles
    float         origin_px; ///< screen x pixel of Box2D world origin (tile 0,0)
    float         origin_py; ///< screen y pixel of Box2D world origin
    float         m2px;      ///< pixels per Box2D metre (x) = tile_width / TILE_M
    float         m2py;      ///< pixels per Box2D metre (y) = tile_height / TILE_M
};

/// Build a fully configured b2DebugDraw that renders collision shapes, contact
/// points, and body transforms via SDL_RenderLines.
/// @p ctx must outlive any b2World_Draw() call using the returned struct.
auto make_debug_draw( DebugDrawContext *ctx ) -> b2DebugDraw;

} // namespace physics
#endif // BOX2D_ENABLED
