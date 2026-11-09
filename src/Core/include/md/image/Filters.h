#pragma once

// Image-space operations. All of them support periodic (wrap-around) addressing so that
// seamless textures stay seamless.

#include "md/core/Image.h"
#include "md/core/Settings.h"

#include <stop_token>

namespace md {

/// Box blur with an integer radius (in pixels) applied to every channel, separable, O(1) per pixel.
ImageF boxBlur(const ImageF& src, int radius, Wrap wrap, std::stop_token stop = {});

/// Approximate Gaussian blur (three box passes). `sigma` is in pixels.
ImageF gaussianBlur(const ImageF& src, float sigma, Wrap wrap, std::stop_token stop = {});

/// Tangent-space normal map from a [0, 1] height map.
/// `depth` = physical depth of the full height range in texture widths (relief * strength).
/// The result holds unit vectors with components in [-1, 1].
ImageF normalFromHeight(const ImageF& height, float depth, Wrap wrap, NormalConvention convention,
                        std::stop_token stop = {});

/// Ambient occlusion estimated from the height map by comparing each pixel with the average
/// height around it at several radii. `depth` as for normalFromHeight. Result in [0, 1], 1 = unoccluded.
ImageF ambientOcclusion(const ImageF& height, float depth, Wrap wrap, std::stop_token stop = {});

} // namespace md
