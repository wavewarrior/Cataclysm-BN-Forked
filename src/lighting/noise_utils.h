#pragma once
#ifndef CATA_SRC_LIGHTING_NOISE_UTILS_H
#define CATA_SRC_LIGHTING_NOISE_UTILS_H

#include <cstdint>

// Integer hash (3 ints -> uint32) and bilinearly-interpolated value noise.
// Seeded so a given position is deterministic across launches.
// Extracted from rmlui_proc_texture.cpp for shared use (runic frame + plexus).
inline std::uint32_t corr_hash(int x, int y, unsigned seed) {
    std::uint32_t h = seed * 2166136261u;
    h = (h ^ static_cast<std::uint32_t>(x)) * 16777619u;
    h = (h ^ static_cast<std::uint32_t>(y)) * 16777619u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

inline double corr_hash01(int gx, int gy, unsigned seed) {
    return (corr_hash(gx, gy, seed) & 0xffffffu) / static_cast<double>(0x1000000);
}

inline double corr_vnoise(int x, int y, int grid, unsigned seed) {
    const int G = grid < 1 ? 1 : grid;
    const int gx = (x >= 0 ? x : x - G + 1) / G;
    const int gy = (y >= 0 ? y : y - G + 1) / G;
    double fx = (x - gx * G) / static_cast<double>(G);
    double fy = (y - gy * G) / static_cast<double>(G);
    fx = fx * fx * (3.0 - 2.0 * fx);
    fy = fy * fy * (3.0 - 2.0 * fy);
    const double a = corr_hash01(gx, gy, seed);
    const double b = corr_hash01(gx + 1, gy, seed);
    const double c = corr_hash01(gx, gy + 1, seed);
    const double d = corr_hash01(gx + 1, gy + 1, seed);
    const double top = a + (b - a) * fx;
    const double bot = c + (d - c) * fx;
    return top + (bot - top) * fy;
}

#endif // CATA_SRC_LIGHTING_NOISE_UTILS_H