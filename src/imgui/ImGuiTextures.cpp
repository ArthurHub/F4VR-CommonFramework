#include "ImGuiTextures.h"

#include <cstring>
#include <ranges>
#include <unordered_map>

#include <imgui.h>

namespace f4cf::imgui::internal
{
    namespace
    {
        /**
         * The GPU texture made for one of ImGui's textures, and its view, which is ImGui's id for it.
         */
        struct GpuTexture
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            render::TextureView view;
        };

        // game thread only: the GPU textures, by the UniqueID of ImGui's texture
        std::unordered_map<int, GpuTexture> s_textures;

        /**
         * Create the GPU texture ImGui asks for, with all its pixels, through the device, which takes calls
         * from any thread. One that cannot be created is asked for again in the next frame.
         */
        void create(ID3D11Device* device, ImTextureData* tex)
        {
            // the font atlas is RGBA32 unless a mod asks for Alpha8, which ImGui's DX11 backend does not take either
            IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(tex->Width);
            desc.Height = static_cast<UINT>(tex->Height);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = tex->GetPixels();
            data.SysMemPitch = static_cast<UINT>(tex->GetPitch());

            GpuTexture gpu;
            if (!device || FAILED(device->CreateTexture2D(&desc, &data, gpu.texture.GetAddressOf())) ||
                FAILED(device->CreateShaderResourceView(gpu.texture.Get(), nullptr, gpu.view.GetAddressOf()))) {
                logger::sample(5000, "ImGui texture of {}x{} could not be created; the text of the canvases is not drawn", tex->Width, tex->Height);
                return;
            }
            tex->SetTexID(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(gpu.view.Get())));
            tex->SetStatus(ImTextureStatus_OK);
            s_textures[tex->UniqueID] = std::move(gpu);
        }

        /**
         * The pixels of a part of the texture that ImGui has just written, for the render thread to write.
         */
        TextureWrite copyPart(const GpuTexture& gpu, ImTextureData* tex, const ImTextureRect& part)
        {
            TextureWrite write{ .texture = gpu.texture, .box = { part.x, part.y, 0, static_cast<UINT>(part.x + part.w), static_cast<UINT>(part.y + part.h), 1 } };
            const std::size_t rowBytes = static_cast<std::size_t>(part.w) * tex->BytesPerPixel;
            write.pixels.resize(rowBytes * part.h);
            for (int row = 0; row < part.h; ++row) {
                std::memcpy(write.pixels.data() + row * rowBytes, tex->GetPixelsAt(part.x, part.y + row), rowBytes);
            }
            return write;
        }
    }

    /**
     * Answer what ImGui asks of its textures in the frame it has just rendered (game thread). A texture is
     * created and destroyed here, and the pixels ImGui adds to one are copied for the render thread, as
     * ImGui goes on writing that texture in its next frame while the render thread draws this one.
     */
    std::vector<TextureWrite> updateTextures(ID3D11Device* device)
    {
        std::vector<TextureWrite> writes;
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
            if (tex->Status == ImTextureStatus_WantCreate) {
                create(device, tex);
            } else if (tex->Status == ImTextureStatus_WantUpdates) {
                if (const auto found = s_textures.find(tex->UniqueID); found != s_textures.end()) {
                    for (const ImTextureRect& part : tex->Updates) {
                        writes.push_back(copyPart(found->second, tex, part));
                    }
                }
                tex->SetStatus(ImTextureStatus_OK);
            } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
                // a frame that still draws it holds its view
                s_textures.erase(tex->UniqueID);
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
        }
        return writes;
    }

    /**
     * Add the views of ImGui's textures to a frame's, so each is alive until the frame has been drawn.
     */
    void addTextureViews(std::vector<render::TextureView>& views)
    {
        for (const auto& gpu : s_textures | std::views::values) {
            views.push_back(gpu.view);
        }
    }

    /**
     * Write the parts of ImGui's textures, in the order ImGui wrote them (render thread).
     */
    void writeTextures(ID3D11DeviceContext* context, const std::span<const TextureWrite> writes)
    {
        for (const auto& write : writes) {
            const auto rowPitch = (write.box.right - write.box.left) * 4;
            context->UpdateSubresource(write.texture.Get(), 0, &write.box, write.pixels.data(), rowPitch, 0);
        }
    }
}
