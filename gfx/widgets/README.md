# Sidebar widget icons (two-tone SVG)

Vector icons for the sidebar widget engine. Rasterized once per font cell size,
uploaded to the GPU, and drawn as **unlit, tinted** UI sprites
(`draw_widget_icon` → `widget_icon::get`). Crisp at any zoom / HiDPI because they
are re-rasterized when the cell size changes.

## Authoring contract — TWO-TONE via a single tint

Each icon is drawn with **one** curses color, applied as a multiplicative tint.
To get the "outline + shaded fill" look from a single tint, author with:

- **strokes / highlights → white `#FFFFFF`** → render at the full tint color.
- **fills / shaded areas → mid-gray `#707070`** → render at ~44% of the tint
  (a darker shade of the same hue).
- background stays transparent.

So a `c_yellow` tint yields a yellow outline over a dark-amber fill, `c_light_gray`
yields a white-ish outline over a gray fill, etc. Avoid other colors — they tint
to odd hues.

## Conventions

- `viewBox="0 0 24 24"`, roughly square so the icon fits one cell.
- Keep stroke widths ~1.2–1.5 (reads like the bitmap font at small sizes).
- Use only features nanosvg supports (paths incl. arcs, circles, rects, basic
  strokes/fills) — SDL3_image's SVG loader is nanosvg.
- File name = the `"icon"` value referenced from `data/json/ui/*.json`
  (e.g. `moon.svg` ← `"icon": "moon"`).

Starter set: `moon`, `heart`, `droplet`, `sound`, `wind`, `compass`.
