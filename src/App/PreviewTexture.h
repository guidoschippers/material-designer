#pragma once

#include <d3d11.h>
#include <imgui.h>

#include <cstdint>

/// A Direct3D 11 RGBA8 texture that can be shown with ImGui::Image.
class PreviewTexture {
public:
    PreviewTexture() = default;
    ~PreviewTexture() { release(); }
    PreviewTexture(const PreviewTexture&) = delete;
    PreviewTexture& operator=(const PreviewTexture&) = delete;

    /// Uploads RGBA8 pixels; (re)creates the texture when the size changes.
    void update(ID3D11Device* device, ID3D11DeviceContext* context, int width, int height, const std::uint8_t* rgba);
    void release();

    bool valid() const { return srv_ != nullptr; }
    ImTextureID id() const { return reinterpret_cast<ImTextureID>(srv_); }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    ID3D11Texture2D* texture_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};
