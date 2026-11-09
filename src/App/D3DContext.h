#pragma once

#include <d3d11.h>
#include <windows.h>

/// Owns the Direct3D 11 device, the swap chain and the back buffer render target.
class D3DContext {
public:
    D3DContext() = default;
    ~D3DContext();
    D3DContext(const D3DContext&) = delete;
    D3DContext& operator=(const D3DContext&) = delete;

    bool create(HWND hwnd);
    void destroy();

    /// Resizes the swap chain buffers (call on WM_SIZE).
    void resize(UINT width, UINT height);

    void beginFrame(const float clearColor[4]);
    /// Presents the frame. Returns false when the window is occluded.
    bool present(bool vsync);

    ID3D11Device* device() const { return device_; }
    ID3D11DeviceContext* context() const { return context_; }

private:
    void createRenderTarget();
    void releaseRenderTarget();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTarget_ = nullptr;
};
