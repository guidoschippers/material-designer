#include "md/image/Filters.h"

#include "md/core/Math.h"
#include "md/core/Parallel.h"

#include <array>
#include <cmath>
#include <vector>

namespace md {

namespace {

int wrapOrClamp(int i, int n, bool wrap)
{
    if (wrap) {
        const int r = i % n;
        return r < 0 ? r + n : r;
    }
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

/// One-dimensional running-sum box filter over `count` samples read with `stride`.
void boxLine(const float* src, float* dst, int count, int stride, int channels, int radius, bool wrap)
{
    const float norm = 1.0f / static_cast<float>(2 * radius + 1);
    for (int c = 0; c < channels; ++c) {
        auto at = [&](int i) { return src[static_cast<std::ptrdiff_t>(wrapOrClamp(i, count, wrap)) * stride + c]; };
        double sum = 0.0; // double: avoids drift of the running sum on large images
        for (int i = -radius; i <= radius; ++i) {
            sum += at(i);
        }
        for (int i = 0; i < count; ++i) {
            dst[static_cast<std::ptrdiff_t>(i) * stride + c] = static_cast<float>(sum) * norm;
            sum += at(i + radius + 1) - at(i - radius);
        }
    }
}

} // namespace

ImageF boxBlur(const ImageF& src, int radius, Wrap wrap, std::stop_token stop)
{
    if (radius <= 0) {
        return src;
    }
    const int w = src.width(), h = src.height(), ch = src.channels();
    ImageF tmp(w, h, ch);
    parallelFor(h, [&](int y) { boxLine(src.row(y), tmp.row(y), w, ch, ch, radius, wrap.x); }, stop);

    ImageF out(w, h, ch);
    const int rowStride = w * ch;
    parallelFor(w, [&](int x) {
        boxLine(tmp.data().data() + static_cast<std::ptrdiff_t>(x) * ch,
                out.data().data() + static_cast<std::ptrdiff_t>(x) * ch, h, rowStride, ch, radius, wrap.y);
    }, stop);
    return out;
}

ImageF gaussianBlur(const ImageF& src, float sigma, Wrap wrap, std::stop_token stop)
{
    // Three box passes of radius r approximate a Gaussian with sigma^2 = 3 * ((2r+1)^2 - 1) / 12.
    const int radius = static_cast<int>(std::lround(std::sqrt(sigma * sigma + 0.25f) - 0.5f));
    if (radius <= 0) {
        return src;
    }
    ImageF a = boxBlur(src, radius, wrap, stop);
    a = boxBlur(a, radius, wrap, stop);
    return boxBlur(a, radius, wrap, stop);
}

ImageF normalFromHeight(const ImageF& height, float depth, Wrap wrap, NormalConvention convention, std::stop_token stop)
{
    const int w = height.width(), h = height.height();
    ImageF out(w, h, 3);
    // Central differences give dh per 2 pixels; convert to slope in texture widths.
    const float sx = depth * static_cast<float>(w) * 0.5f;
    const float sy = depth * static_cast<float>(h) * 0.5f;
    const float greenSign = convention == NormalConvention::DirectX ? -1.0f : 1.0f;

    parallelFor(h, [&](int y) {
        for (int x = 0; x < w; ++x) {
            const float dx = (height.sample(x + 1, y, 0, wrap) - height.sample(x - 1, y, 0, wrap)) * sx;
            const float dy = (height.sample(x, y + 1, 0, wrap) - height.sample(x, y - 1, 0, wrap)) * sy;
            // Image y points down. OpenGL (Y+) green = +dh/dy_image, DirectX (Y-) is flipped.
            float nx = -dx, ny = greenSign * dy, nz = 1.0f;
            const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            out.at(x, y, 0) = nx * inv;
            out.at(x, y, 1) = ny * inv;
            out.at(x, y, 2) = nz * inv;
        }
    }, stop);
    return out;
}

ImageF ambientOcclusion(const ImageF& height, float depth, Wrap wrap, std::stop_token stop)
{
    const int w = height.width(), h = height.height();
    // Radii as fractions of the texture width, so the result is resolution independent.
    constexpr std::array<float, 4> kRadii = {0.002f, 0.006f, 0.016f, 0.04f};
    constexpr std::array<float, 4> kWeights = {0.3f, 0.3f, 0.25f, 0.15f};

    ImageF occlusion(w, h, 1, 0.0f);
    for (std::size_t i = 0; i < kRadii.size(); ++i) {
        const float radiusPx = kRadii[i] * static_cast<float>(w);
        if (radiusPx < 0.75f) {
            continue; // below pixel scale at this resolution
        }
        const ImageF avg = gaussianBlur(height, radiusPx, wrap, stop);
        const float slopeScale = depth / kRadii[i]; // height difference -> slope of the "horizon"
        parallelFor(h, [&](int y) {
            for (int x = 0; x < w; ++x) {
                const float slope = (avg.at(x, y) - height.at(x, y)) * slopeScale;
                // Fraction of the hemisphere hidden by the surrounding surface (atan of the horizon angle).
                const float occ = slope > 0.0f ? std::atan(slope * 2.0f) * (2.0f / kPi) : 0.0f;
                occlusion.at(x, y) += kWeights[i] * occ;
            }
        }, stop);
    }

    ImageF out(w, h, 1);
    for (std::size_t i = 0; i < out.data().size(); ++i) {
        out.data()[i] = saturate(1.0f - 1.6f * occlusion.data()[i]);
    }
    return out;
}

} // namespace md
