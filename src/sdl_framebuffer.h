#pragma once

#include "cursesport.h"
#include "point.h"

/// Which font family a window belongs to — determines which framebuffer cache
/// to use and which font metrics to validate against.
enum class window_family : unsigned char {
    terminal,  ///< Uses `font` (sidebar, messages, dialogs, …)
    terrain,   ///< Uses `map_font` (game::w_terrain)
    overmap,   ///< Uses `overmap_font` (game::w_overmap)
    max,
};

/// Initialize/resize framebuffer caches for all families.
void cache_initialize_all( int term_height, int term_width,
                           int over_height, int over_width,
                           int terr_height, int terr_width );

/// Invalidate all caches (on resize, targets reset, etc.).
void cache_invalidate_all();

/// Return true if the cell matches the cached cell AND the font/scale
/// signature for this family is still valid.  Also seals the signature on
/// first draw after a resize/invalidation.
auto cache_should_skip( window_family family, int row, int col,
                        const cata_cursesport::cursecell &cell,
                        int font_width, int font_height,
                        int font_scale ) -> bool;

/// Update the cache after drawing a cell.
void cache_update( window_family family, int row, int col,
                   const cata_cursesport::cursecell &cell );

/// Invalidate a region of a specific family's cache.
void cache_invalidate_region( window_family family, point p, int width, int height );

/// Invalidate a proportion of the terrain/overmap caches.
void invalidate_framebuffer_proportion( cata_cursesport::WINDOW *win );

void reinitialize_framebuffer( bool force_invalidate );
