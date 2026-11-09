#pragma once

// The Material interface. A material describes its parameters (schema) and produces the
// "surface" maps (albedo, height, roughness, wear, extra occlusion). The shared pipeline in
// Material::generate() derives the normal and ambient occlusion maps from the height map.

#include "md/core/Image.h"
#include "md/core/Parallel.h"
#include "md/core/Parameter.h"
#include "md/core/Settings.h"

#include <cstdint>
#include <stop_token>
#include <string_view>

namespace md {

/// Final output of a generation run. All maps are size x size and stored as floats:
/// albedo is linear RGB, normal holds unit vectors (components in [-1, 1], convention applied),
/// the other maps are single channel in [0, 1].
struct MaterialMaps {
    int size = 0;
    ImageF albedo;
    ImageF normal;
    ImageF roughness;
    ImageF height;
    ImageF ao;
    ImageF wear;

    const ImageF& get(MapType map) const;
};

/// Maps written by a material's generateSurface(). Allocated (and default-filled) by the pipeline.
struct SurfaceMaps {
    explicit SurfaceMaps(int size);

    int size;
    ImageF albedo;    ///< Linear RGB. Default 0.5 grey.
    ImageF height;    ///< [0, 1]. Default 0.5.
    ImageF roughness; ///< [0, 1]. Default 0.5.
    ImageF wear;      ///< [0, 1] wear mask. Default 0 (no wear).
    ImageF occlusion; ///< [0, 1] extra occlusion multiplied into the computed AO. Default 1.
};

/// Per-run information handed to a material.
class GenerateContext {
public:
    GenerateContext(const CommonSettings& common, const ParamReader& params, int size, std::stop_token stop)
        : common_(common), params_(params), size_(size), stop_(std::move(stop))
    {
    }

    const CommonSettings& common() const { return common_; }
    const ParamReader& params() const { return params_; }

    /// Resolution of this run (may be lower than common().size for previews).
    int size() const { return size_; }
    float pixelSize() const { return 1.0f / static_cast<float>(size_); }
    std::uint32_t seed() const { return common_.seed; }
    bool seamless() const { return common_.seamless; }

    bool wearEnabled() const { return common_.wear; }
    /// True when wear effects must be applied to the base maps.
    bool applyWear() const { return common_.wear && common_.applyWear; }
    /// Overall wear intensity (0 when wear is disabled).
    float wearAmount() const { return common_.wear ? static_cast<float>(common_.wearAmount) : 0.0f; }

    const std::stop_token& stopToken() const { return stop_; }
    void throwIfCancelled() const;

    /// Calls fn(x, y, u, v) for every pixel, rows in parallel. (u, v) is the pixel centre in [0, 1).
    template <class Fn>
    void forEachPixel(Fn&& fn) const
    {
        const int n = size_;
        const float inv = 1.0f / static_cast<float>(n);
        parallelFor(
            n,
            [&](int y) {
                const float v = (static_cast<float>(y) + 0.5f) * inv;
                for (int x = 0; x < n; ++x) {
                    fn(x, y, (static_cast<float>(x) + 0.5f) * inv, v);
                }
            },
            stop_);
    }

private:
    const CommonSettings& common_;
    const ParamReader& params_;
    int size_;
    std::stop_token stop_;
};

/// Options that do not affect the result's content, only how it is produced.
struct GenerateOptions {
    int sizeOverride = 0;    ///< Generate at this resolution instead of settings.common.size (previews).
    std::stop_token stop;    ///< Cancels the run (md::Cancelled is thrown).
};

class Material {
public:
    virtual ~Material() = default;

    /// Stable lowercase identifier, e.g. "asphalt". Used in file names, presets and the CLI.
    virtual std::string_view id() const = 0;
    virtual std::string_view displayName() const = 0;
    /// One-sentence description shown in `list` and the GUI.
    virtual std::string_view description() const = 0;
    virtual const ParamSchema& schema() const = 0;

    /// Physical depth of the full [0, 1] height range, in texture widths. Controls normal map
    /// strength and AO so they look the same at every resolution.
    virtual float reliefDepth() const = 0;

    /// Fills the surface maps. Must be a pure function of the context (deterministic).
    virtual void generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const = 0;

    /// Runs the full pipeline: surface generation + derived normal and AO maps.
    /// Throws md::Cancelled when options.stop is requested.
    MaterialMaps generate(const MaterialSettings& settings, const GenerateOptions& options = {}) const;
};

} // namespace md
