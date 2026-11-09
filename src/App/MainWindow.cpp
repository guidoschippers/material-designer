#include "MainWindow.h"

#include "FileDialogs.h"
#include "SettingsPanel.h"

#include "md/core/Error.h"
#include "md/io/Preset.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <random>

namespace {

constexpr int kLowResPreview = 256;   ///< resolution while dragging a slider
constexpr double kDebounceSeconds = 0.15;

std::wstring toWide(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end())).wstring();
}

std::string toUtf8(const std::filesystem::path& p)
{
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

} // namespace

MainWindow::MainWindow(HWND hwnd, D3DContext& d3d)
    : hwnd_(hwnd), d3d_(d3d), registry_(md::MaterialRegistry::builtin())
{
    for (const auto& m : registry_.all()) {
        settings_[std::string(m->id())] = md::defaultSettings(*m);
    }
    if (!registry_.all().empty()) {
        active_ = std::string(registry_.all().front()->id());
    }
}

void MainWindow::shutdown()
{
    previewWorker_.cancel();
    exportWorker_.cancel();
}

md::MaterialSettings& MainWindow::activeSettings()
{
    return settings_.at(active_);
}

const md::Material& MainWindow::activeMaterial() const
{
    return *registry_.find(active_);
}

void MainWindow::frame()
{
    collectResults();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("material-designer", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();

    if (registry_.all().empty()) {
        ImGui::TextUnformatted("No materials registered.");
        ImGui::End();
        return;
    }

    const float settingsWidth = std::max(ImGui::GetFontSize() * 28.0f, ImGui::GetContentRegionAvail().x * 0.3f);
    ImGui::BeginChild("settings", ImVec2(settingsWidth, 0), ImGuiChildFlags_ResizeX);
    drawSettingsPane();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("preview", ImVec2(0, 0));
    drawPreviewPane();
    ImGui::EndChild();

    drawMessagePopup();
    ImGui::End();

    updatePreviewJobs();
}

void MainWindow::drawSettingsPane()
{
    if (!ImGui::BeginTabBar("materials")) {
        return;
    }
    for (const auto& m : registry_.all()) {
        const std::string id(m->id());
        ImGuiTabItemFlags flags = 0;
        if (selectTab_ == id) {
            flags |= ImGuiTabItemFlags_SetSelected;
        }
        if (ImGui::BeginTabItem(std::string(m->displayName()).c_str(), nullptr, flags)) {
            if (active_ != id) {
                active_ = id;
                markChanged();
            }
            md::MaterialSettings& s = settings_.at(id);
            ImGui::TextWrapped("%s", std::string(m->description()).c_str());
            drawActions(*m, s);

            ImGui::BeginChild("params", ImVec2(0, 0));
            ImGui::PushItemWidth(-ImGui::GetFontSize() * 9.0f);
            bool changed = drawCommonSettings(s.common, m->outputMaps());
            changed |= drawMaterialParameters(*m, s);
            ImGui::PopItemWidth();
            ImGui::EndChild();
            if (changed) {
                markChanged();
            }
            ImGui::EndTabItem();
        }
    }
    selectTab_.clear();
    ImGui::EndTabBar();
}

void MainWindow::drawActions(const md::Material& material, md::MaterialSettings& settings)
{
    if (ImGui::Button("Randomise seed")) {
        settings.common.seed = static_cast<std::uint32_t>(std::random_device{}());
        markChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        const md::CommonSettings keep = settings.common;
        settings = md::defaultSettings(material);
        settings.common.size = keep.size; // keep the chosen output size
        markChanged();
    }
    ImGui::SetItemTooltip("Reset all settings of this material to their defaults (keeps the export size).");
    ImGui::SameLine();
    if (ImGui::Button("Save preset...")) {
        savePreset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load preset...")) {
        loadPreset();
    }

    if (exportWorker_.busy()) {
        ImGui::Button("Exporting...");
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            exportWorker_.cancel();
        }
    } else if (ImGui::Button("Export...")) {
        startExport();
    }
    ImGui::SetItemTooltip("Generate at full resolution and write the selected maps as PNG.");
    ImGui::Separator();
}

void MainWindow::drawPreviewPane()
{
    preview_.draw(d3d_.device(), d3d_.context(), status_);
}

void MainWindow::drawMessagePopup()
{
    if (openMessage_) {
        ImGui::OpenPopup("message");
        openMessage_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 36.0f, 0));
    if (ImGui::BeginPopupModal("message", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::SeparatorText(messageTitle_.c_str());
        ImGui::TextWrapped("%s", messageText_.c_str());
        if (ImGui::Button("OK", ImVec2(ImGui::GetFontSize() * 6.0f, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void MainWindow::showMessage(std::string title, std::string text)
{
    messageTitle_ = std::move(title);
    messageText_ = std::move(text);
    openMessage_ = true;
}

void MainWindow::markChanged()
{
    pendingLowRes_ = true;
    pendingFull_ = true;
    lastChange_ = ImGui::GetTime();
}

void MainWindow::updatePreviewJobs()
{
    if (active_.empty()) {
        return;
    }
    const md::MaterialSettings& s = activeSettings();
    const int fullSize = std::min(preview_.previewSize(), s.common.size);
    if (fullSize != lastPreviewSize_) {
        lastPreviewSize_ = fullSize;
        pendingFull_ = true;
    }

    const bool dragging = ImGui::IsAnyItemActive();
    const double now = ImGui::GetTime();
    if (pendingLowRes_ && dragging && !previewWorker_.busy()) {
        // Quick low-resolution feedback while a slider is being dragged.
        previewWorker_.start(activeMaterial(), s, std::min(kLowResPreview, fullSize));
        pendingLowRes_ = false;
        status_ = "Generating...";
    }
    if (pendingFull_ && !dragging && now - lastChange_ >= kDebounceSeconds) {
        previewWorker_.start(activeMaterial(), s, fullSize);
        pendingFull_ = false;
        pendingLowRes_ = false;
        status_ = "Generating...";
    }
}

void MainWindow::collectResults()
{
    if (auto r = previewWorker_.takeResult()) {
        if (!r->error.empty()) {
            status_ = "Error: " + r->error;
        } else if (r->settings.material == active_) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%d px in %.0f ms", r->size, r->milliseconds);
            status_ = buf;
            preview_.setMaps(std::move(r->maps));
        }
    }
    if (auto r = exportWorker_.takeResult()) {
        if (!r->error.empty()) {
            showMessage("Export failed", r->error);
        } else if (r->exported) {
            std::string text = "Wrote " + std::to_string(r->exported->files.size()) + " maps (" +
                               std::to_string(r->size) + " x " + std::to_string(r->size) + ") and the settings to:\n" +
                               toUtf8(exportDir_);
            for (const std::string& w : r->exported->warnings) {
                text += "\n\nWarning: " + w;
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "\n\nTook %.1f s.", r->milliseconds / 1000.0);
            showMessage("Export complete", text + buf);
        }
    }
}

void MainWindow::savePreset()
{
    const md::MaterialSettings& s = activeSettings();
    const auto path = dialogs::saveJson(hwnd_, presetDir_, toWide(s.material + "_preset.json"));
    if (!path) {
        return;
    }
    try {
        md::savePreset(*path, s);
        presetDir_ = path->parent_path();
    } catch (const std::exception& e) {
        showMessage("Save preset failed", e.what());
    }
}

void MainWindow::loadPreset()
{
    const auto path = dialogs::openJson(hwnd_, presetDir_);
    if (!path) {
        return;
    }
    try {
        std::vector<std::string> warnings;
        md::MaterialSettings s = md::loadPreset(*path, registry_, nullptr, warnings);
        presetDir_ = path->parent_path();
        active_ = s.material;
        selectTab_ = s.material;
        settings_[s.material] = std::move(s);
        markChanged();
        if (!warnings.empty()) {
            std::string text;
            for (const std::string& w : warnings) text += "- " + w + "\n";
            showMessage("Preset loaded with warnings", text);
        }
    } catch (const std::exception& e) {
        showMessage("Load preset failed", e.what());
    }
}

void MainWindow::startExport()
{
    const auto dir = dialogs::pickFolder(hwnd_, exportDir_);
    if (!dir) {
        return;
    }
    exportDir_ = *dir;
    const md::MaterialSettings& s = activeSettings();
    exportWorker_.start(activeMaterial(), s, s.common.size, exportDir_);
}
