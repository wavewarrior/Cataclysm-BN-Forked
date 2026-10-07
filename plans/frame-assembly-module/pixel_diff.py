#!/usr/bin/env python3
"""THROWAWAY REFERENCE for the frame-assembly equivalence gate: the plan ticket ports this to a Deno
script (or a `bnplay compare` operation) that reuses `tools/bnplay/frames.ts` `decodeFrame`, then
deletes this file. Do not commit it as a tool.

Pixel-count frame comparison for the frame-assembly equivalence gate.

Usage: pixel_diff.py --base A1.bmp A2.bmp A3.bmp --test T1.bmp [T2.bmp ...]

--base are frames of the same unchanged state (three or more, ideally from two launches): their
pairwise differences form the noise mask (the toggling strip and animated pixels), dilated by two
pixels. --test frames are compared with every base frame; the best match (fewest changed pixels
outside the mask) is reported. A pixel counts as changed when any channel differs at all.

Prints: changed pixels outside the mask, their share of the frame, the largest channel
difference, and their bounding box. Whole-frame mean absolute difference (bnplay's frameDelta)
averages a local regression away; this does not. Measured nulls on osx-arm-slim, 2026-10-07: 59 to
1,815 changed pixels (0.002 to 0.037%), within a launch and across launches.
Needs numpy and Pillow.
"""
import argparse
import sys

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB"))


def delta(a, b):
    return np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2)


def dilate(mask, steps=2):
    for _ in range(steps):
        grown = mask.copy()
        grown[1:] |= mask[:-1]
        grown[:-1] |= mask[1:]
        grown[:, 1:] |= mask[:, :-1]
        grown[:, :-1] |= mask[:, 1:]
        mask = grown
    return mask


def noise_mask(frames):
    mask = np.zeros(frames[0].shape[:2], bool)
    for i in range(len(frames)):
        for j in range(i + 1, len(frames)):
            mask |= delta(frames[i], frames[j]) > 0
    return dilate(mask)


def changed(a, b, mask):
    d = delta(a, b)
    hit = (d > 0) & ~mask
    n = int(hit.sum())
    if n == 0:
        return {"changed_px": 0, "pct": 0.0, "max_delta": 0, "bbox": None}
    ys, xs = np.nonzero(hit)
    return {
        "changed_px": n,
        "pct": round(100 * n / hit.size, 4),
        "max_delta": int(d[hit].max()),
        "bbox": [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", nargs="+", required=True)
    ap.add_argument("--test", nargs="+", required=True)
    args = ap.parse_args()
    if len(args.base) < 3:
        sys.exit("pixel_diff: --base needs at least three frames of the unchanged state")
    base = [load(p) for p in args.base]
    mask = noise_mask(base)
    print(f"noise mask: {int(mask.sum())} px from {len(base)} base frames")
    for path in args.test:
        test = load(path)
        if test.shape != base[0].shape:
            sys.exit(f"pixel_diff: {path} is {test.shape}, base is {base[0].shape}")
        best = min((changed(test, b, mask) for b in base), key=lambda r: r["changed_px"])
        print(path, best)


if __name__ == "__main__":
    main()
