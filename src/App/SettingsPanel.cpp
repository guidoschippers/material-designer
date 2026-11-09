#include "SettingsPanel.h"

#include <imgui.h>

#include <array>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr std::array<int, 6> kSizes = {256, 512, 1024, 2048, 4096, 8192};

void tooltip(const std::string& text)
{
    if (!text.empty()) {
        ImGui::SetItemTooltip("%s", text.c_str());
    }
}

/// Right-click on the previous item resets it.
bool resetOnRightClick(const md::ParamDef& def, md::ParamValue& value)
{
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        value = def.defaultParamValue();
        return true;
    }
    return false;
}

bool drawParam(const md::ParamDef& def, md::ParamValue& value)
{
    md::sanitizeValue(def, value, nullptr);
    bool changed = false;
    const char* label = def.label.c_str();
    ImGui::PushID(def.key.c_str());
    switch (def.type) {
    case md::ParamType::Bool: {
        bool v = std::holds_alternative<bool>(value) && std::get<bool>(value);
        if (ImGui::Checkbox(label, &v)) { value = v; changed = true; }
        break;
    }
    case md::ParamType::Int: {
        int v = std::holds_alternative<int>(value) ? std::get<int>(value) : static_cast<int>(def.defaultValue);
        if (ImGui::SliderInt(label, &v, static_cast<int>(def.minValue), static_cast<int>(def.maxValue))) { value = v; changed = true; }
        break;
    }
    case md::ParamType::Float: {
        float v = static_cast<float>(std::holds_alternative<double>(value) ? std::get<double>(value) : def.defaultValue);
        if (ImGui::SliderFloat(label, &v, static_cast<float>(def.minValue), static_cast<float>(def.maxValue), "%.2f")) {
            value = static_cast<double>(v);
            changed = true;
        }
        break;
    }
    case md::ParamType::Enum: {
        const std::string current = std::holds_alternative<std::string>(value) ? std::get<std::string>(value) : def.defaultOption;
        const int index = def.optionIndex(current);
        const char* preview = index >= 0 ? def.options[static_cast<std::size_t>(index)].label.c_str() : "";
        if (ImGui::BeginCombo(label, preview)) {
            for (const md::EnumOption& o : def.options) {
                if (ImGui::Selectable(o.label.c_str(), o.id == current)) { value = o.id; changed = true; }
            }
            ImGui::EndCombo();
        }
        break;
    }
    case md::ParamType::Color: {
        md::Color c = std::holds_alternative<md::Color>(value) ? std::get<md::Color>(value) : def.defaultColor;
        float rgb[3] = {c.r, c.g, c.b};
        if (ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayHex)) {
            value = md::Color{rgb[0], rgb[1], rgb[2]};
            changed = true;
        }
        break;
    }
    }
    tooltip(def.tooltip + "\n(right-click to reset)");
    changed |= resetOnRightClick(def, value);
    ImGui::PopID();
    return changed;
}

} // namespace

bool drawCommonSettings(md::CommonSettings& c, md::MapSet availableMaps)
{
    bool changed = false;
    ImGui::SeparatorText("Common");

    const std::string sizeLabel = std::to_string(c.size) + " x " + std::to_string(c.size);
    if (ImGui::BeginCombo("Export size", sizeLabel.c_str())) {
        for (int s : kSizes) {
            const std::string l = std::to_string(s) + " x " + std::to_string(s);
            if (ImGui::Selectable(l.c_str(), s == c.size)) { c.size = s; changed = true; }
        }
        ImGui::EndCombo();
    }
    tooltip("Resolution of the exported textures. The preview uses a lower resolution.");

    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - ImGui::GetFrameHeight() * 3.2f);
    changed |= ImGui::InputScalar("##seed", ImGuiDataType_U32, &c.seed);
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    if (ImGui::Button("Random")) {
        std::random_device rd;
        c.seed = rd();
        changed = true;
    }
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted("Seed");
    tooltip("Same seed + settings = identical output, byte for byte.");

    changed |= ImGui::Checkbox("Seamless", &c.seamless);
    tooltip("Tile perfectly on all edges (periodic noise). Check with the 2x2 tiled preview.");

    changed |= ImGui::Checkbox("Wear", &c.wear);
    tooltip("Generate the wear mask and the material's wear effects.");
    ImGui::BeginDisabled(!c.wear);
    float amount = static_cast<float>(c.wearAmount);
    if (ImGui::SliderFloat("Wear amount", &amount, 0.0f, 1.0f, "%.2f")) { c.wearAmount = amount; changed = true; }
    tooltip("Overall wear intensity.");
    changed |= ImGui::Checkbox("Apply wear to maps", &c.applyWear);
    tooltip("When off, only the wear mask is produced and the other maps stay unworn.");
    ImGui::EndDisabled();

    int conv = c.normalConvention == md::NormalConvention::DirectX ? 0 : 1;
    if (ImGui::Combo("Normal map", &conv, "DirectX (Y-)\0OpenGL (Y+)\0")) {
        c.normalConvention = conv == 0 ? md::NormalConvention::DirectX : md::NormalConvention::OpenGL;
        changed = true;
    }
    tooltip("Green channel convention of the normal map.");
    float strength = static_cast<float>(c.normalStrength);
    if (ImGui::SliderFloat("Normal strength", &strength, 0.0f, 4.0f, "%.2f")) { c.normalStrength = strength; changed = true; }
    tooltip("Multiplier on the material's natural relief.");

    ImGui::TextUnformatted("Export maps");
    if (ImGui::BeginTable("maps", 3)) {
        for (md::MapType m : availableMaps.list()) {
            ImGui::TableNextColumn();
            bool on = c.maps.contains(m);
            if (ImGui::Checkbox(std::string(md::mapLabel(m)).c_str(), &on)) {
                c.maps.set(m, on);
                changed = true;
            }
        }
        ImGui::EndTable();
    }
    return changed;
}

bool drawMaterialParameters(const md::Material& material, md::MaterialSettings& settings)
{
    bool changed = false;
    std::string currentGroup;
    for (const md::ParamDef& def : material.schema().params()) {
        if (def.group != currentGroup) {
            currentGroup = def.group;
            ImGui::SeparatorText(currentGroup.c_str());
        }
        bool enabled = !def.wearOnly || settings.common.wear;
        if (!def.enabledBy.empty()) {
            const auto it = settings.params.find(def.enabledBy);
            enabled = enabled && it != settings.params.end() && std::holds_alternative<bool>(it->second) &&
                      std::get<bool>(it->second);
        }
        ImGui::BeginDisabled(!enabled);
        md::ParamValue& value = settings.params[def.key];
        if (value.index() == 0 && def.type != md::ParamType::Bool) {
            value = def.defaultParamValue(); // freshly inserted
        }
        changed |= drawParam(def, value);
        ImGui::EndDisabled();
    }
    return changed;
}
