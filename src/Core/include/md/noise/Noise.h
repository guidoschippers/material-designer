#pragma once

// Tileable noise library: hashing, value noise, gradient (Perlin) noise, fBm, ridged noise,
// Voronoi/cellular noise and domain warping.
//
// Tiling: every function takes a Period. Noise is evaluated in "lattice space" (typically
// x = u * frequency) and lattice indices are wrapped modulo the period, so with
// Period{f, f} and x = u * f the result is periodic in u with period 1. Period{0, 0} disables
// wrapping (non-seamless output). Any constant offset added to the coordinates keeps the
// periodicity, which is what makes domain warping tileable as well.
//
// Everything is a pure function of its arguments (no global state), which makes generation
// deterministic and trivially parallel.

#include "md/core/Math.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace md::noise {

/// Lattice period per axis. 0 = not periodic.
struct Period {
    int x = 0;
    int y = 0;
};

inline Period periodFor(int fx, int fy, bool seamless) { return seamless ? Period{fx, fy} : Period{}; }

/// Rounds a frequency to a positive integer (tileable noise needs integer frequencies).
inline int frequency(float f) { return f < 1.0f ? 1 : static_cast<int>(std::lround(f)); }

// ---------------------------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------------------------

/// 32-bit integer hash with good avalanche ("lowbias32" by Chris Wellons).
inline std::uint32_t hash32(std::uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline std::uint32_t hashCell(int x, int y, std::uint32_t seed)
{
    return hash32(static_cast<std::uint32_t>(x) + hash32(static_cast<std::uint32_t>(y) + hash32(seed)));
}

/// Derives an independent seed for a named layer of a generator.
inline std::uint32_t subSeed(std::uint32_t seed, std::uint32_t layer)
{
    return hash32(seed ^ hash32(layer * 0x9e3779b9U + 0x7f4a7c15U));
}

/// Maps a hash to [0, 1).
inline float toUnit(std::uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }

/// k-th random number in [0, 1) derived from an id (e.g. a Voronoi cell id).
inline float randomFromId(std::uint32_t id, std::uint32_t k) { return toUnit(hash32(id + hash32(k + 0x51ed270bU))); }

inline int wrapIndex(int i, int period)
{
    if (period <= 0) {
        return i;
    }
    const int r = i % period;
    return r < 0 ? r + period : r;
}

// ---------------------------------------------------------------------------------------------
// Value and gradient noise
// ---------------------------------------------------------------------------------------------

/// Quintic fade curve (C2 continuous).
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

/// Value noise in [0, 1].
inline float value(float x, float y, Period p, std::uint32_t seed)
{
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const int x0 = wrapIndex(ix, p.x), x1 = wrapIndex(ix + 1, p.x);
    const int y0 = wrapIndex(iy, p.y), y1 = wrapIndex(iy + 1, p.y);
    const float a = toUnit(hashCell(x0, y0, seed));
    const float b = toUnit(hashCell(x1, y0, seed));
    const float c = toUnit(hashCell(x0, y1, seed));
    const float d = toUnit(hashCell(x1, y1, seed));
    const float sx = fade(x - fx), sy = fade(y - fy);
    return lerp(lerp(a, b, sx), lerp(c, d, sx), sy);
}

namespace detail {

struct GradientTable {
    std::array<float, 32> xy{};
    GradientTable()
    {
        for (int i = 0; i < 16; ++i) {
            const double a = (static_cast<double>(i) + 0.5) * 6.283185307179586 / 16.0;
            xy[2 * i] = static_cast<float>(std::cos(a));
            xy[2 * i + 1] = static_cast<float>(std::sin(a));
        }
    }
};

inline const GradientTable& gradients()
{
    static const GradientTable table;
    return table;
}

inline float gradDot(std::uint32_t h, float dx, float dy)
{
    const auto& g = gradients().xy;
    const unsigned i = (h >> 28) * 2u; // top 4 bits -> one of 16 unit gradients
    return g[i] * dx + g[i + 1] * dy;
}

} // namespace detail

/// Gradient (Perlin) noise, roughly in [-1, 1].
inline float perlin(float x, float y, Period p, std::uint32_t seed)
{
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const float tx = x - fx, ty = y - fy;
    const int x0 = wrapIndex(ix, p.x), x1 = wrapIndex(ix + 1, p.x);
    const int y0 = wrapIndex(iy, p.y), y1 = wrapIndex(iy + 1, p.y);
    const float a = detail::gradDot(hashCell(x0, y0, seed), tx, ty);
    const float b = detail::gradDot(hashCell(x1, y0, seed), tx - 1.0f, ty);
    const float c = detail::gradDot(hashCell(x0, y1, seed), tx, ty - 1.0f);
    const float d = detail::gradDot(hashCell(x1, y1, seed), tx - 1.0f, ty - 1.0f);
    const float sx = fade(tx), sy = fade(ty);
    return 1.4142135f * lerp(lerp(a, b, sx), lerp(c, d, sx), sy);
}

/// Fractal Brownian motion of Perlin noise, roughly in [-1, 1]. Each octave doubles the
/// frequency (and the period, so the sum stays tileable).
inline float fbm(float x, float y, Period p, std::uint32_t seed, int octaves, float gain = 0.5f)
{
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        // Fractional offsets avoid all octaves being zero at the base lattice points.
        const float o = 0.37f * static_cast<float>(i);
        sum += amp * perlin(x + o, y + o * 1.7f, p, seed + static_cast<std::uint32_t>(i) * 1013U);
        norm += amp;
        amp *= gain;
        x *= 2.0f;
        y *= 2.0f;
        p.x *= 2;
        p.y *= 2;
    }
    return sum / norm;
}

/// fBm remapped to [0, 1].
inline float fbm01(float x, float y, Period p, std::uint32_t seed, int octaves, float gain = 0.5f)
{
    return saturate(0.5f + 0.5f * fbm(x, y, p, seed, octaves, gain));
}

/// Ridged multifractal noise in [0, 1]: sharp ridges where the base noise crosses zero.
inline float ridged(float x, float y, Period p, std::uint32_t seed, int octaves, float gain = 0.5f)
{
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        const float o = 0.37f * static_cast<float>(i);
        const float r = 1.0f - std::abs(perlin(x + o, y + o * 1.7f, p, seed + static_cast<std::uint32_t>(i) * 1013U));
        sum += amp * r * r;
        norm += amp;
        amp *= gain;
        x *= 2.0f;
        y *= 2.0f;
        p.x *= 2;
        p.y *= 2;
    }
    return sum / norm;
}

// ---------------------------------------------------------------------------------------------
// Voronoi / cellular noise
// ---------------------------------------------------------------------------------------------

struct VoronoiResult {
    float f1 = 0.0f;           ///< Distance to the nearest feature point.
    float f2 = 0.0f;           ///< Distance to the second nearest feature point.
    Vec2 point;                ///< Nearest feature point, in the caller's (unwrapped) lattice space.
    std::uint32_t id = 0;      ///< Stable id of the nearest cell (identical across tile wraps).
    std::uint32_t id2 = 0;     ///< Id of the second nearest cell.
};

/// Feature point of lattice cell (cx, cy), in unwrapped lattice space, plus its id.
inline Vec2 voronoiPoint(int cx, int cy, Period p, std::uint32_t seed, float jitter, std::uint32_t& id)
{
    id = hashCell(wrapIndex(cx, p.x), wrapIndex(cy, p.y), seed);
    const float rx = toUnit(id);
    const float ry = toUnit(hash32(id ^ 0x68e31da4U));
    return {static_cast<float>(cx) + 0.5f + jitter * (rx - 0.5f), static_cast<float>(cy) + 0.5f + jitter * (ry - 0.5f)};
}

/// Cellular noise (Euclidean). jitter in [0, 1]: 0 = regular grid, 1 = fully random.
inline VoronoiResult voronoi(float x, float y, Period p, std::uint32_t seed, float jitter = 1.0f)
{
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    VoronoiResult r;
    float d1 = 1e30f, d2 = 1e30f;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            std::uint32_t id;
            const Vec2 pt = voronoiPoint(ix + dx, iy + dy, p, seed, jitter, id);
            const float ddx = pt.x - x, ddy = pt.y - y;
            const float d = ddx * ddx + ddy * ddy;
            if (d < d1) {
                d2 = d1;
                r.id2 = r.id;
                d1 = d;
                r.id = id;
                r.point = pt;
            } else if (d < d2) {
                d2 = d;
                r.id2 = id;
            }
        }
    }
    r.f1 = std::sqrt(d1);
    r.f2 = std::sqrt(d2);
    return r;
}

/// Exact distance to the nearest Voronoi cell border (Inigo Quilez' two-pass method),
/// plus the nearest cell. Useful for cracks, joints and stone outlines.
struct VoronoiEdgeResult {
    float edge = 0.0f;   ///< Distance to the closest cell border.
    float f1 = 0.0f;     ///< Distance to the own feature point.
    Vec2 point;          ///< Own feature point (unwrapped lattice space).
    std::uint32_t id = 0;
};

inline VoronoiEdgeResult voronoiEdge(float x, float y, Period p, std::uint32_t seed, float jitter = 1.0f)
{
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    VoronoiEdgeResult r;
    float best = 1e30f;
    int bx = 0, by = 0;
    Vec2 mr; // vector from sample to nearest point
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            std::uint32_t id;
            const Vec2 pt = voronoiPoint(ix + dx, iy + dy, p, seed, jitter, id);
            const Vec2 d{pt.x - x, pt.y - y};
            const float dd = dot(d, d);
            if (dd < best) {
                best = dd;
                mr = d;
                bx = ix + dx;
                by = iy + dy;
                r.id = id;
                r.point = pt;
            }
        }
    }
    float edge = 1e30f;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const int cx = bx + dx, cy = by + dy;
            if (cx == bx && cy == by) {
                continue;
            }
            std::uint32_t id;
            const Vec2 pt = voronoiPoint(cx, cy, p, seed, jitter, id);
            const Vec2 rr{pt.x - x, pt.y - y};
            const Vec2 diff = rr - mr;
            const float len2 = dot(diff, diff);
            if (len2 > 1e-8f) {
                const Vec2 mid = (mr + rr) * 0.5f;
                edge = std::min(edge, dot(mid, diff) / std::sqrt(len2));
            }
        }
    }
    r.edge = edge;
    r.f1 = std::sqrt(best);
    return r;
}

// ---------------------------------------------------------------------------------------------
// Domain warping
// ---------------------------------------------------------------------------------------------

/// Vector field for domain warping: two decorrelated fBm channels, roughly in [-1, 1].
/// Add `warp(...) * amount` to the coordinates of another (tileable) noise lookup.
inline Vec2 warp(float x, float y, Period p, std::uint32_t seed, int octaves)
{
    return {fbm(x, y, p, seed, octaves), fbm(x + 5.2f, y + 1.3f, p, seed ^ 0x2c1b3c6dU, octaves)};
}

} // namespace md::noise
