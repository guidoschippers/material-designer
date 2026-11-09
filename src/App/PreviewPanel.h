#pragma once

// Preview of the generated maps: map selector, 2x2 tiling, zoom and pan.

#include "PreviewTexture.h"

#include "md/core/Material.h"

#include <d3d11.h>

#include <string>

class PreviewPanel {
public:
    /// Replaces the displayed maps (keeps zoom/pan).
    void setMaps(md::MaterialMaps maps);
    bool hasMaps() const { return maps_.size > 0; }

    /// Draws the panel into the remaining content region. `status` is shown in the toolbar.
    void draw(ID3D11Device* device, ID3D11DeviceContext* context, const std::string& status);

    /// Preview resolution the user selected (the app generates previews at this size).
    int previewSize() const { return previewSize_; }

private:
    void uploadIfNeeded(ID3D11Device* device, ID3D11DeviceContext* context);
    void drawCanvas();

    md::MaterialMaps maps_;
    PreviewTexture texture_;
    md::MapType map_ = md::MapType::Albedo;
    bool dirty_ = false;
    bool tiled_ = false;
    float zoom_ = 1.0f; ///< 1 = fit to the panel
    float panX_ = 0.0f, panY_ = 0.0f; ///< in units of the displayed image size
    int previewSize_ = 1024;
};
