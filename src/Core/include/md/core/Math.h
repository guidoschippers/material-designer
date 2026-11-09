#pragma once

// Small math helpers shared by the generators. Header-only so they inline in hot loops.

#include <algorithm>
#include <cmath>

namespace md {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;

inline float saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float smoothstep(float edge0, float edge1, float x)
{
    if (edge0 == edge1) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    const float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

/// Linear remap of x from [a0, a1] to [b0, b1] (not clamped).
inline float remap(float x, float a0, float a1, float b0, float b1)
{
    return b0 + (x - a0) * (b1 - b0) / (a1 - a0);
}

/// Wraps a coordinate into [0, 1).
inline float wrap01(float x) { return x - std::floor(x); }

/// Signed shortest distance between two coordinates on a [0, 1) torus (or plain difference when not periodic).
inline float periodicDelta(float a, float b, bool periodic)
{
    float d = a - b;
    if (periodic) {
        d -= std::round(d);
    }
    return d;
}

struct Vec2 {
    float x = 0.0f, y = 0.0f;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline Vec3 saturate(Vec3 v) { return {saturate(v.x), saturate(v.y), saturate(v.z)}; }

/// Luminance of a linear RGB colour (Rec. 709 weights).
inline float luminance(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

/// Scales saturation around the luminance (0 = grey, 1 = unchanged).
inline Vec3 adjustSaturation(Vec3 c, float s)
{
    const float l = luminance(c);
    return Vec3{l, l, l} + (c - Vec3{l, l, l}) * s;
}

inline float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float linearToSrgb(float c)
{
    c = saturate(c);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

inline Vec3 srgbToLinear(Vec3 c) { return {srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z)}; }

} // namespace md
