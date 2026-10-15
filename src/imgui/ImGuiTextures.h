#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "../render/Texture.h"

namespace f4cf::imgui::internal
{
    /**
     * A part of one of ImGui's own textures to write on the render thread, before the frame that follows
     * it is drawn: the pixels of the box, in rows from its top, 4 bytes a pixel.
     */
    struct TextureWrite
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        D3D11_BOX box{};
        std::vector<std::uint8_t> pixels;
    };

    // ImGui's own textures, its font atlas: answered on the game thread, written on the render thread
    std::vector<TextureWrite> updateTextures(ID3D11Device* device);
    void addTextureViews(std::vector<render::TextureView>& views);
    void writeTextures(ID3D11DeviceContext* context, std::span<const TextureWrite> writes);
}
