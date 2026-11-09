#include "PreviewTexture.h"

void PreviewTexture::update(ID3D11Device* device, ID3D11DeviceContext* context, int width, int height,
                            const std::uint8_t* rgba)
{
    if (texture_ && width == width_ && height == height_) {
        context->UpdateSubresource(texture_, 0, nullptr, rgba, static_cast<UINT>(width) * 4, 0);
        return;
    }
    release();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    // UNORM (not _SRGB): the pixels are already display-encoded (albedo in sRGB, data maps as stored).
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = rgba;
    data.SysMemPitch = static_cast<UINT>(width) * 4;
    if (FAILED(device->CreateTexture2D(&desc, &data, &texture_))) {
        return;
    }
    if (FAILED(device->CreateShaderResourceView(texture_, nullptr, &srv_))) {
        release();
        return;
    }
    width_ = width;
    height_ = height;
}

void PreviewTexture::release()
{
    if (srv_) { srv_->Release(); srv_ = nullptr; }
    if (texture_) { texture_->Release(); texture_ = nullptr; }
    width_ = height_ = 0;
}
