#include "FileDialogs.h"

#include <shobjidl.h>

namespace dialogs {

namespace {

template <class T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

void setInitialFolder(IFileDialog* dialog, const std::filesystem::path& folder)
{
    if (folder.empty() || !std::filesystem::is_directory(folder)) {
        return;
    }
    ComPtr<IShellItem> item;
    if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        dialog->SetFolder(item.p);
    }
}

std::optional<std::filesystem::path> resultPath(IFileDialog* dialog)
{
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) {
        return std::nullopt;
    }
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        return std::nullopt;
    }
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}

const COMDLG_FILTERSPEC kJsonFilter[] = {{L"Preset (*.json)", L"*.json"}, {L"All files (*.*)", L"*.*"}};

} // namespace

std::optional<std::filesystem::path> pickFolder(HWND owner, const std::filesystem::path& initial)
{
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"Export to folder");
    setInitialFolder(dialog.p, initial);
    if (dialog->Show(owner) != S_OK) {
        return std::nullopt;
    }
    return resultPath(dialog.p);
}

std::optional<std::filesystem::path> openJson(HWND owner, const std::filesystem::path& initialDir)
{
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    dialog->SetFileTypes(2, kJsonFilter);
    dialog->SetTitle(L"Load preset");
    setInitialFolder(dialog.p, initialDir);
    if (dialog->Show(owner) != S_OK) {
        return std::nullopt;
    }
    return resultPath(dialog.p);
}

std::optional<std::filesystem::path> saveJson(HWND owner, const std::filesystem::path& initialDir,
                                              const std::wstring& defaultName)
{
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    dialog->SetFileTypes(2, kJsonFilter);
    dialog->SetDefaultExtension(L"json");
    dialog->SetFileName(defaultName.c_str());
    dialog->SetTitle(L"Save preset");
    setInitialFolder(dialog.p, initialDir);
    if (dialog->Show(owner) != S_OK) {
        return std::nullopt;
    }
    return resultPath(dialog.p);
}

} // namespace dialogs
