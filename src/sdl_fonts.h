#pragma once

#include <memory>
#include <string>

#include "point.h"
#include "sdl_geometry.h"
#include "sdl_wrappers.h"

class Font;

/// Draw a single line of text at pixel position @p p using font @p font.
/// Advances @p p.x by the rendered width; returns the final position.
auto draw_string( Font &font,
                  const SDL_Renderer_Ptr &renderer,
                  const GeometryRenderer_Ptr &geometry,
                  const std::string &str,
                  point p,
                  unsigned char color ) -> point;
