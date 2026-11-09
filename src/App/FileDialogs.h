#pragma once

// Native Windows file dialogs (IFileDialog). COM must be initialised on the calling thread.

#include <windows.h>

#include <filesystem>
#include <optional>

namespace dialogs {

std::optional<std::filesystem::path> pickFolder(HWND owner, const std::filesystem::path& initial);
std::optional<std::filesystem::path> openJson(HWND owner, const std::filesystem::path& initialDir);
std::optional<std::filesystem::path> saveJson(HWND owner, const std::filesystem::path& initialDir,
                                              const std::wstring& defaultName);

} // namespace dialogs
