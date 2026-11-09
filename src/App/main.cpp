// material-designer GUI entry point: Win32 window + Direct3D 11 + Dear ImGui main loop.

#include "D3DContext.h"

#include "md/core/Version.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

D3DContext g_d3d;
UINT g_resizeWidth = 0;
UINT g_resizeHeight = 0;

LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return TRUE;
    }
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_resizeWidth = LOWORD(lParam);
            g_resizeHeight = HIWORD(lParam);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) {
            return 0; // Disable the ALT application menu.
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, wndProc, 0, 0, instance, nullptr, LoadCursor(nullptr, IDC_ARROW),
                   nullptr, nullptr, L"MaterialDesignerWindow", nullptr};
    RegisterClassExW(&wc);
    const std::wstring title = L"material-designer";
    HWND hwnd = CreateWindowW(wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              1600, 1000, nullptr, nullptr, instance, nullptr);

    if (!g_d3d.create(hwnd)) {
        MessageBoxW(nullptr, L"Could not create a Direct3D 11 device.", title.c_str(), MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    const float dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    ImGui::GetStyle().ScaleAllSizes(dpiScale);
    ImGui::GetStyle().FontScaleDpi = dpiScale;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_d3d.device(), g_d3d.context());

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                running = false;
            }
        }
        if (!running) {
            break;
        }
        if (g_resizeWidth != 0 && g_resizeHeight != 0) {
            g_d3d.resize(g_resizeWidth, g_resizeHeight);
            g_resizeWidth = g_resizeHeight = 0;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->WorkPos);
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::Text("material-designer %s", std::string(md::version()).c_str());
        ImGui::End();

        ImGui::Render();
        const float clear[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        g_d3d.beginFrame(clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_d3d.present(true);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_d3d.destroy();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
