#include "PreviewPanel.h"

#include "md/io/Export.h"

#include <algorithm>
#include <cmath>

void PreviewPanel::setMaps(md::MaterialMaps maps)
{
    maps_ = std::move(maps);
    dirty_ = true;
}

void PreviewPanel::uploadIfNeeded(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!dirty_ || maps_.size == 0) {
        return;
    }
    // Same encoders as the exporter: what you see is what gets written.
    const md::EncodedMap encoded = md::encodeMap(maps_, map_);
    const std::vector<std::uint8_t> rgba = md::toRgba8(encoded);
    texture_.update(device, context, encoded.width, encoded.height, rgba.data());
    dirty_ = false;
}

void PreviewPanel::draw(ID3D11Device* device, ID3D11DeviceContext* context, const std::string& status)
{
    // Toolbar: map selector.
    for (md::MapType m : md::kAllMaps) {
        if (m != md::MapType::Albedo) ImGui::SameLine();
        if (ImGui::RadioButton(std::string(md::mapLabel(m)).c_str(), map_ == m)) {
            map_ = m;
            dirty_ = true;
        }
    }
    ImGui::Checkbox("Tile 2x2", &tiled_);
    ImGui::SetItemTooltip("Shows the texture repeated 2x2 to check that it is seamless.");
    ImGui::SameLine();
    if (ImGui::Button("Fit")) {
        zoom_ = 1.0f;
        panX_ = panY_ = 0.0f;
    }
    ImGui::SetItemTooltip("Reset zoom and pan. Mouse wheel zooms, drag to pan.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    const int sizes[] = {256, 512, 1024, 2048};
    const std::string current = std::to_string(previewSize_) + " px";
    if (ImGui::BeginCombo("Preview", current.c_str())) {
        for (int s : sizes) {
            if (ImGui::Selectable((std::to_string(s) + " px").c_str(), s == previewSize_)) previewSize_ = s;
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Resolution of the live preview (capped at the export size).");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", status.c_str());

    uploadIfNeeded(device, context);
    drawCanvas();
}

void PreviewPanel::drawCanvas()
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 50.0f);
    avail.y = std::max(avail.y, 50.0f);

    ImGui::InvisibleButton("canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();

    const float fit = std::min(avail.x, avail.y);
    const float tiles = tiled_ ? 2.0f : 1.0f;
    float side = fit * zoom_; // displayed size of the whole (tiled) image
    const ImVec2 center(origin.x + avail.x * 0.5f, origin.y + avail.y * 0.5f);

    if (hovered && io.MouseWheel != 0.0f) {
        // Zoom around the mouse cursor.
        const float oldSide = side;
        zoom_ = std::clamp(zoom_ * std::pow(1.15f, io.MouseWheel), 0.1f, 64.0f);
        side = fit * zoom_;
        const float mx = (io.MousePos.x - center.x) / oldSide - panX_;
        const float my = (io.MousePos.y - center.y) / oldSide - panY_;
        panX_ = (io.MousePos.x - center.x) / side - mx;
        panY_ = (io.MousePos.y - center.y) / side - my;
    }
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        panX_ += io.MouseDelta.x / side;
        panY_ += io.MouseDelta.y / side;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), true);
    dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(30, 30, 32, 255));
    if (texture_.valid()) {
        const ImVec2 topLeft(center.x + (panX_ - 0.5f) * side, center.y + (panY_ - 0.5f) * side);
        const float tile = side / tiles;
        for (int ty = 0; ty < static_cast<int>(tiles); ++ty) {
            for (int tx = 0; tx < static_cast<int>(tiles); ++tx) {
                const ImVec2 a(topLeft.x + tile * static_cast<float>(tx), topLeft.y + tile * static_cast<float>(ty));
                dl->AddImage(ImTextureRef(texture_.id()), a, ImVec2(a.x + tile, a.y + tile));
            }
        }
    } else {
        dl->AddText(ImVec2(origin.x + 10, origin.y + 10), IM_COL32(160, 160, 160, 255), "Generating preview...");
    }
    dl->PopClipRect();
}
