#!/usr/bin/env python3
"""Report the tile pitch (i.e. zoom level) of vv captures.

Exists because zoom is the dominant nondeterminism in this project's scripted
captures: zoom keypresses sent back to back get dropped, so two runs of the same
scenario can end on different zoom levels. Measured across four captures that
moved whole-frame mean luma by 19.7 while the shader under test was unchanged -
enough to invent a 67% "signal" out of nothing. Any A/B MUST confirm both
captures share a tile pitch before a single pixel is compared.

Usage: python tools/shadow_zoom_check.py out/vv/a/shadow.png out/vv/b/shadow.png
"""
import sys
import numpy as np
from PIL import Image
from numpy.fft import rfft


def pitch(path: str) -> tuple[float, float, tuple[int, int]]:
    im = Image.open(path)
    a = np.asarray(im.convert("L"), dtype=np.float32)
    h, w = a.shape
    sub = a[int(h * 0.2):int(h * 0.8), int(w * 0.27):int(w * 0.86)]
    r = sub - sub.mean(axis=1, keepdims=True)
    spec = np.abs(rfft(r * np.hanning(r.shape[1])[None, :], axis=1)).mean(axis=0)
    lo = max(2, r.shape[1] // 80)
    band = spec[lo:r.shape[1] // 6]
    k = int(np.argmax(band)) + lo
    return r.shape[1] / k, float(a.mean()), im.size


def main() -> int:
    rows = [(p, *pitch(p)) for p in sys.argv[1:]]
    for p, pt, mean, size in rows:
        print(f"{pt:7.2f} px pitch  mean={mean:6.2f}  {size}  {p}")
    if len(rows) < 2:
        return 0
    sizes = {r[3] for r in rows}
    pitches = [r[1] for r in rows]
    if len(sizes) > 1:
        print("MISMATCH: differing resolutions - not comparable")
        return 1
    if max(pitches) - min(pitches) > 0.5:
        print("MISMATCH: differing zoom levels - not comparable, re-run")
        return 1
    print("OK: same resolution and zoom - safe to compare")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
