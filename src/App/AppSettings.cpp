#include "AppSettings.h"

#include <shlobj.h>

#include <fstream>

using nlohmann::json;

namespace {

std::string toUtf8(const std::filesystem::path& p)
{
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::filesystem::path fromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

} // namespace

std::filesystem::path AppSettings::filePath()
{
    PWSTR appData = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        dir = std::filesystem::path(appData) / L"material-designer";
    }
    CoTaskMemFree(appData);
    return dir / L"gui-settings.json";
}

AppSettings AppSettings::load()
{
    AppSettings s;
    try {
        std::ifstream file(filePath(), std::ios::binary);
        if (!file) {
            return s;
        }
        const json j = json::parse(file);
        if (j.contains("window")) {
            const json& w = j["window"];
            WindowPlacement wp;
            wp.x = w.value("x", 0);
            wp.y = w.value("y", 0);
            wp.width = w.value("width", 0);
            wp.height = w.value("height", 0);
            wp.maximized = w.value("maximized", false);
            if (wp.width > 0 && wp.height > 0) {
                s.window = wp;
            }
        }
        s.exportDir = fromUtf8(j.value("export_dir", std::string()));
        s.presetDir = fromUtf8(j.value("preset_dir", std::string()));
        s.previewSize = j.value("preview_size", 1024);
        s.activeMaterial = j.value("active_material", std::string());
        if (j.contains("materials") && j["materials"].is_object()) {
            s.materials = j["materials"];
        }
    } catch (const std::exception&) {
        return AppSettings{}; // corrupt file: start fresh
    }
    return s;
}

void AppSettings::save() const
{
    try {
        json j;
        if (window) {
            j["window"] = {{"x", window->x}, {"y", window->y}, {"width", window->width},
                           {"height", window->height}, {"maximized", window->maximized}};
        }
        j["export_dir"] = toUtf8(exportDir);
        j["preset_dir"] = toUtf8(presetDir);
        j["preview_size"] = previewSize;
        j["active_material"] = activeMaterial;
        j["materials"] = materials;

        const auto path = filePath();
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << j.dump(2) << "\n";
    } catch (const std::exception&) {
        // Not being able to remember settings must never break the application.
    }
}
