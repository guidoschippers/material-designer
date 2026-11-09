#include "D3DContext.h"

D3DContext::~D3DContext()
{
    destroy();
}

bool D3DContext::create(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL obtained{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &swapChain_, &device_, &obtained, &context_);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        // No suitable GPU: fall back to the WARP software rasterizer.
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &swapChain_, &device_, &obtained, &context_);
    }
    if (FAILED(hr)) {
        return false;
    }
    createRenderTarget();
    return true;
}

void D3DContext::destroy()
{
    releaseRenderTarget();
    if (swapChain_) { swapChain_->Release(); swapChain_ = nullptr; }
    if (context_) { context_->Release(); context_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
}

void D3DContext::resize(UINT width, UINT height)
{
    if (!swapChain_ || width == 0 || height == 0) {
        return;
    }
    releaseRenderTarget();
    swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    createRenderTarget();
}

void D3DContext::beginFrame(const float clearColor[4])
{
    context_->OMSetRenderTargets(1, &renderTarget_, nullptr);
    context_->ClearRenderTargetView(renderTarget_, clearColor);
}

bool D3DContext::present(bool vsync)
{
    const HRESULT hr = swapChain_->Present(vsync ? 1 : 0, 0);
    return hr != DXGI_STATUS_OCCLUDED;
}

void D3DContext::createRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        device_->CreateRenderTargetView(backBuffer, nullptr, &renderTarget_);
        backBuffer->Release();
    }
}

void D3DContext::releaseRenderTarget()
{
    if (renderTarget_) {
        renderTarget_->Release();
        renderTarget_ = nullptr;
    }
}
