#pragma once

// Phase 2i-B-7g: per-ui_adaptor retained draw slices.
//
// Each ui_adaptor owns one of these. During redraw_invalidated(), every
// invalidated adaptor's slice is cleared and repopulated by its redraw_cb
// (via queue_ui_rect / queue_font_glyph routing through render_state's
// current_adaptor_ pointer). Non-invalidated adaptors keep their previous
// slice contents, so partial redraws do not wipe windows that didn't run.
//
// After the redraw loop, ui_manager composites the slices in ui_stack
// (z-order) into the global render_state queues that refresh_display
// flushes. Closing a popup destroys its ui_adaptor, which destroys the
// slice — no ghost glyphs.
//
// This file lives in lighting/ so ui_manager.h can forward-declare the
// type and hold a unique_ptr to it without pulling sprite_instance and
// the rest of the GPU stack into the world's most-included header.

#include "sprite_batcher.h" // sprite_instance

#include <vector>

struct SDL_GPUTexture;

namespace lighting {

struct font_glyph_draw {
    SDL_GPUTexture* texture = nullptr;
    sprite_instance inst{};
    // false = HUD/UI (skip lighting); true = world-space text that
    // should be lit by ambient/emitters/sun. Default false.
    bool lit = false;
};

struct ui_adaptor_draw_slices {
    std::vector<sprite_instance> ui_rects;
    std::vector<font_glyph_draw> font_glyphs;
};

} // namespace lighting
