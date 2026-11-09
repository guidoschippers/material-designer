#pragma once

// Reusable, tileable surface features for materials: packed stones, crack networks, blobs,
// rectangles and bands. All functions work in UV space [0, 1) and are pure functions of their
// inputs, so they can be evaluated per pixel in parallel.

#include "md/core/Math.h"
#include "md/noise/Noise.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace md::features {

/// Per-run sampling information.
struct Domain {
    bool seamlessU = true; ///< output must tile horizontally
    bool seamlessV = true; ///< output must tile vertically
    float pixel = 1.0f / 1024.0f; ///< Size of one pixel in UV units (for anti-aliasing).

    /// Seamlessness of the u axis (true) or the v axis (false).
    bool seamlessAxis(bool uAxis) const { return uAxis ? seamlessU : seamlessV; }
    noise::Period period(int fx, int fy) const { return {seamlessU ? fx : 0, seamlessV ? fy : 0}; }
    noise::Period period(int f) const { return period(f, f); }
    /// Smoothstep width (in UV units) that anti-aliases an edge over ~1.5 pixels.
    float aa() const { return 1.5f * pixel; }
};

/// Stable id of grid cell (gx, gy) on a grid of `cells` per tile edge. gx/gy are unwrapped
/// indices (they may lie outside [0, cells)); they wrap on seamless axes so that a feature placed
/// in a cell is identical on both sides of the tile edge.
inline std::uint32_t cellId(int gx, int gy, int cells, std::uint32_t seed, const Domain& d)
{
    return noise::hashCell(noise::wrapIndex(gx, d.seamlessU ? cells : 0), noise::wrapIndex(gy, d.seamlessV ? cells : 0),
                           seed);
}

/// Distance from p to the line segment a-b.
inline float segmentDistance(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2 ap = p - a, ab = b - a;
    const float t = saturate(dot(ap, ab) / std::max(dot(ab, ab), 1e-12f));
    return length(ap - ab * t);
}

/// Fades a feature in as `amount` goes from 0 to 1, using a low-frequency mask so that the
/// feature appears in patches rather than uniformly. Returns 0 for amount <= 0.
inline float patchyMask(float u, float v, float amount, int freq, std::uint32_t seed, const Domain& d)
{
    if (amount <= 0.0f) {
        return 0.0f;
    }
    const float m = noise::fbm01(u * freq, v * freq, d.period(freq), seed, 3);
    const float threshold = lerp(0.72f, 0.22f, saturate(amount));
    return smoothstep(threshold, threshold + 0.12f, m);
}

// ---------------------------------------------------------------------------------------------
// Stones (aggregate)
// ---------------------------------------------------------------------------------------------

struct StoneSample {
    float coverage = 0.0f; ///< 1 inside a stone, 0 outside (anti-aliased).
    float dome = 0.0f;     ///< Rounded height profile inside the stone, [0, 1].
    std::uint32_t id = 0;  ///< Stable stone id (use with noise::randomFromId).
};

/// Smooth minimum (exponential), k = blend width.
inline float smin(float a, float b, float k)
{
    const float h = saturate(0.5f + 0.5f * (b - a) / k);
    return lerp(b, a, h) - k * h * (1.0f - h);
}

/// Densely packed stones based on warped Voronoi cells.
/// freq: stones per tile edge. presence: fraction of cells holding a stone [0, 1].
/// gap: spacing between stones as a fraction of the cell size [0, 0.5].
/// roundness: 0 = angular (cell shaped, crushed stone), 1 = rounded pebbles (river gravel).
inline StoneSample stones(float u, float v, int freq, std::uint32_t seed, float presence, float gap, float roundness,
                          const Domain& d)
{
    const auto period = d.period(freq);
    float x = u * static_cast<float>(freq), y = v * static_cast<float>(freq);
    // Warp at the stone scale for irregular outlines.
    const int wf = std::max(1, freq * 3 / 2);
    const Vec2 w = noise::warp(u * wf, v * wf, d.period(wf), seed ^ 0x9e3779b9U, 2);
    x += w.x * 0.18f;
    y += w.y * 0.18f;

    const auto e = noise::voronoiEdge(x, y, period, seed, 0.9f);
    StoneSample s;
    s.id = e.id;
    if (noise::randomFromId(e.id, 0) >= presence) {
        return s;
    }
    // Rounded stones: limit the cell shape by a disc around the feature point, softly blended.
    const float radius = 0.46f + 0.16f * noise::randomFromId(e.id, 5);
    const float rounded = smin(e.edge, radius - e.f1, 0.12f);
    const float m = lerp(e.edge, rounded, roundness);

    // Wide spread of gaps gives a natural mix of large and small stones.
    const float cellGap = gap * (0.3f + 1.2f * noise::randomFromId(e.id, 1)) + 0.07f * noise::randomFromId(e.id, 6) * noise::randomFromId(e.id, 6);
    const float aa = d.aa() * static_cast<float>(freq);
    s.coverage = smoothstep(cellGap, cellGap + aa, m);
    s.dome = std::sqrt(saturate((m - cellGap) / 0.2f)) * s.coverage;
    return s;
}

// ---------------------------------------------------------------------------------------------
// Cracks
// ---------------------------------------------------------------------------------------------

/// Network of thin cracks along warped Voronoi borders. Returns crack intensity in [0, 1]
/// (1 = centre of a crack). widthUv is the crack width in UV units; warp in cell units.
inline float crackNetwork(float u, float v, int freq, std::uint32_t seed, float widthUv, float warp, const Domain& d)
{
    float x = u * static_cast<float>(freq), y = v * static_cast<float>(freq);
    const int wf = freq * 2;
    const Vec2 w1 = noise::warp(u * wf, v * wf, d.period(wf), seed ^ 0x51633e2dU, 3);
    x += w1.x * warp;
    y += w1.y * warp;
    const auto e = noise::voronoiEdge(x, y, d.period(freq), seed, 1.0f);
    // Crack width varies along the crack.
    const float wv = 0.5f + noise::value(u * freq * 6.0f, v * freq * 6.0f, d.period(freq * 6), seed ^ 0x1b873593U);
    const float halfWidth = 0.5f * widthUv * wv * static_cast<float>(freq);
    const float aa = d.aa() * static_cast<float>(freq);
    return 1.0f - smoothstep(halfWidth, halfWidth + aa, e.edge);
}

// ---------------------------------------------------------------------------------------------
// Blobs (stains, spots)
// ---------------------------------------------------------------------------------------------

/// Randomly placed soft blobs with noisy outlines. cells: grid cells per tile edge (each cell
/// holds at most one blob). probability: chance per cell. Radii in UV units (<= 1/cells).
/// Returns the blob intensity in [0, 1]; `idOut` receives the id of the strongest blob.
inline float blobs(float u, float v, int cells, std::uint32_t seed, float probability, float minRadius, float maxRadius,
                   float softness, const Domain& d, std::uint32_t* idOut = nullptr)
{
    if (probability <= 0.0f) {
        return 0.0f;
    }
    const float cellSize = 1.0f / static_cast<float>(cells);
    const int cx = static_cast<int>(std::floor(u * cells)), cy = static_cast<int>(std::floor(v * cells));
    const float edgeNoise = noise::fbm(u * cells * 4.0f, v * cells * 4.0f, d.period(cells * 4), seed ^ 0x2545f491U, 3);
    float best = 0.0f;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int gx = cx + dx, gy = cy + dy;
            const std::uint32_t id = noise::hashCell(noise::wrapIndex(gx, d.seamlessU ? cells : 0),
                                                     noise::wrapIndex(gy, d.seamlessV ? cells : 0), seed);
            if (noise::randomFromId(id, 0) >= probability) {
                continue;
            }
            const float px = (static_cast<float>(gx) + noise::randomFromId(id, 1)) * cellSize;
            const float py = (static_cast<float>(gy) + noise::randomFromId(id, 2)) * cellSize;
            const float r = lerp(minRadius, maxRadius, noise::randomFromId(id, 3));
            // Elliptical, randomly stretched along x or y.
            const float stretch = 0.7f + 0.6f * noise::randomFromId(id, 4);
            const float ddx = (u - px) * stretch, ddy = (v - py) / stretch;
            const float dist = std::sqrt(ddx * ddx + ddy * ddy) / r + 0.35f * edgeNoise;
            const float b = 1.0f - smoothstep(1.0f - softness, 1.0f, dist);
            if (b > best) {
                best = b;
                if (idOut) *idOut = id;
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------
// Rectangles (repairs, patches)
// ---------------------------------------------------------------------------------------------

struct RectSample {
    float inside = 0.0f;    ///< 1 inside the rectangle (anti-aliased).
    float edgeDist = 1e9f;  ///< Unsigned distance to the rectangle outline (UV units).
    std::uint32_t id = 0;
};

/// Randomly placed axis-aligned rectangles, at most one per grid cell. Sizes are half extents
/// in UV units. Outlines are slightly irregular (cut edges are never perfectly straight).
inline RectSample rectangles(float u, float v, int cells, std::uint32_t seed, float probability, float minHalf,
                             float maxHalf, const Domain& d)
{
    RectSample out;
    if (probability <= 0.0f) {
        return out;
    }
    const float cellSize = 1.0f / static_cast<float>(cells);
    const int cx = static_cast<int>(std::floor(u * cells)), cy = static_cast<int>(std::floor(v * cells));
    const float jitter = 0.004f * noise::fbm(u * 40.0f, v * 40.0f, d.period(40), seed ^ 0x68bc21ebU, 2);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int gx = cx + dx, gy = cy + dy;
            const std::uint32_t id = noise::hashCell(noise::wrapIndex(gx, d.seamlessU ? cells : 0),
                                                     noise::wrapIndex(gy, d.seamlessV ? cells : 0), seed);
            if (noise::randomFromId(id, 0) >= probability) {
                continue;
            }
            const float px = (static_cast<float>(gx) + noise::randomFromId(id, 1)) * cellSize;
            const float py = (static_cast<float>(gy) + noise::randomFromId(id, 2)) * cellSize;
            const float hx = lerp(minHalf, maxHalf, noise::randomFromId(id, 3));
            const float hy = lerp(minHalf, maxHalf, noise::randomFromId(id, 4));
            const float qx = std::abs(u - px) - hx + jitter;
            const float qy = std::abs(v - py) - hy - jitter;
            const float sd = std::max(qx, qy); // signed (Chebyshev) distance; < 0 inside
            const float inside = 1.0f - smoothstep(-d.aa() * 0.5f, d.aa() * 0.5f, sd);
            if (inside > out.inside || (out.inside == 0.0f && std::abs(sd) < out.edgeDist)) {
                out.inside = std::max(out.inside, inside);
                out.id = inside > 0.0f ? id : out.id;
            }
            out.edgeDist = std::min(out.edgeDist, std::abs(sd));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Bands (tracks, paths)
// ---------------------------------------------------------------------------------------------

/// Periodic soft bands across one axis: `count` bands per tile centred at offset + i / count.
/// `across` is the coordinate perpendicular to the bands. halfWidth in UV units.
inline float bands(float across, int count, float offset, float halfWidth, float softness)
{
    const float spacing = 1.0f / static_cast<float>(count);
    float t = (across - offset) / spacing;
    t -= std::round(t);
    const float dist = std::abs(t) * spacing;
    return 1.0f - smoothstep(halfWidth * (1.0f - softness), halfWidth, dist);
}

} // namespace md::features
