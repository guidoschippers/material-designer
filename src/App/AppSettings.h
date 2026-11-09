#pragma once

// GUI state remembered between sessions, stored as JSON in %APPDATA%\material-designer\gui-settings.json.

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

struct WindowPlacement {
    int x = 0, y = 0, width = 0, height = 0;
    bool maximized = false;
};

struct AppSettings {
    std::optional<WindowPlacement> window;
    std::filesystem::path exportDir;
    std::filesystem::path presetDir;
    int previewSize = 1024;
    std::string activeMaterial;
    nlohmann::json materials = nlohmann::json::object(); ///< last settings per material id (preset JSON)

    /// Loads the settings; returns defaults when the file is missing or invalid.
    static AppSettings load();
    /// Saves the settings (errors are ignored: persistence is a convenience).
    void save() const;

    static std::filesystem::path filePath();
};
