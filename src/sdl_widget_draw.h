#pragma once

/// Widget icon drawing functions extracted from sdltiles.cpp.
///
/// Declarations remain in widget_icon.h (public API). Callers in panels.cpp
/// are unchanged — same pattern as sdl_window_dims uses for size queries.
///
/// sdl_widget_draw.cpp is the single definition site for:
///   - draw_widget_icon (two overloads)
///   - draw_widget_row_highlight
