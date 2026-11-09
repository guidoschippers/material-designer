#pragma once

// The application UI: one tab per registered material (settings generated from the schema),
// a live preview and the preset/export actions.

#include "AppSettings.h"
#include "D3DContext.h"
#include "GenerationWorker.h"
#include "PreviewPanel.h"

#include "md/core/Registry.h"

#include <filesystem>
#include <map>
#include <string>

class MainWindow {
public:
    /// Restores folders, preview size, active tab and the last settings per material.
    MainWindow(HWND hwnd, D3DContext& d3d, const AppSettings& saved);

    /// Stores folders, preview size, active tab and the current settings per material.
    void saveTo(AppSettings& settings) const;

    /// Builds the UI for one frame (between ImGui::NewFrame and ImGui::Render).
    void frame();

    /// Cancels running jobs (call before shutdown).
    void shutdown();

private:
    md::MaterialSettings& activeSettings();
    const md::Material& activeMaterial() const;

    void drawSettingsPane();
    void drawActions(const md::Material& material, md::MaterialSettings& settings);
    void drawPreviewPane();
    void drawMessagePopup();

    void markChanged();
    void updatePreviewJobs();
    void collectResults();

    void savePreset();
    void loadPreset();
    void startExport();
    void showMessage(std::string title, std::string text);

    HWND hwnd_;
    D3DContext& d3d_;
    const md::MaterialRegistry& registry_;

    std::map<std::string, md::MaterialSettings> settings_; ///< per material id
    std::string active_;
    std::string selectTab_; ///< material whose tab must be selected next frame

    PreviewPanel preview_;
    GenerationWorker previewWorker_;
    GenerationWorker exportWorker_;

    // Debounced preview regeneration.
    bool pendingLowRes_ = false;
    bool pendingFull_ = true;
    double lastChange_ = 0.0;
    int lastPreviewSize_ = 0;
    std::string status_;

    std::filesystem::path exportDir_;
    std::filesystem::path presetDir_;

    std::string messageTitle_;
    std::string messageText_;
    bool openMessage_ = false;
};
