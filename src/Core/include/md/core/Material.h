#pragma once

// The Material interface. A material describes its parameters (schema) and produces the
// "surface" maps (albedo, height, roughness, wear, extra occlusion, opacity). The shared pipeline
// in Material::generate() derives the normal and ambient occlusion maps from the height map.
//
// Two kinds of materials exist:
// - base materials (asphalt, concrete, ...): tile on both axes, no opacity map;
// - overlays (an asphalt edge strip, road markings, ...): meant to be blended over a base
//   material. They produce an Opacity map and may tile on one axis only (or not at all).

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
    MapSet available; ///< Maps this material produces (Opacity only for overlays).
    ImageF albedo;
    ImageF normal;
    ImageF roughness;
    ImageF height;
    ImageF ao;
    ImageF wear;
    ImageF opacity;

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
    ImageF opacity;   ///< [0, 1] coverage of an overlay. Default 1 (ignored for base materials).
};

/// Axes on which a material repeats (when seamless output is enabled).
struct TileAxes {
    bool u = true; ///< left and right edges match
    bool v = true; ///< top and bottom edges match
};

/// Per-run information handed to a material.
class GenerateContext {
public:
    GenerateContext(const CommonSettings& common, const ParamReader& params, int size, TileAxes tiles,
                    std::stop_token stop)
        : common_(common), params_(params), size_(size), seamlessU_(common.seamless && tiles.u),
          seamlessV_(common.seamless && tiles.v), stop_(std::move(stop))
    {
    }

    const CommonSettings& common() const { return common_; }
    const ParamReader& params() const { return params_; }

    /// Resolution of this run (may be lower than common().size for previews).
    int size() const { return size_; }
    float pixelSize() const { return 1.0f / static_cast<float>(size_); }
    std::uint32_t seed() const { return common_.seed; }

    /// Whether the output must tile horizontally (u) / vertically (v).
    bool seamlessU() const { return seamlessU_; }
    bool seamlessV() const { return seamlessV_; }

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
    bool seamlessU_;
    bool seamlessV_;
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

    /// Maps this material produces. Overlays add MapType::Opacity.
    virtual MapSet outputMaps() const { return MapSet::standard(); }
    bool isOverlay() const { return outputMaps().contains(MapType::Opacity); }

    /// Axes on which the material repeats for the given parameters. Base materials tile on both.
    virtual TileAxes tileAxes(const ParamReader& /*params*/) const { return {}; }

    /// Fills the surface maps. Must be a pure function of the context (deterministic).
    virtual void generateSurface(const GenerateContext& ctx, SurfaceMaps& out) const = 0;

    /// Runs the full pipeline: surface generation + derived normal and AO maps.
    /// Throws md::Cancelled when options.stop is requested.
    MaterialMaps generate(const MaterialSettings& settings, const GenerateOptions& options = {}) const;
};

} // namespace md
