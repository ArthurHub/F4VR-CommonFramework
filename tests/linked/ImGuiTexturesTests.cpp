#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <d3d11.h>
#include <imgui.h>
#include <wrl/client.h>

#include "imgui/ImGuiFonts.h"
#include "imgui/ImGuiLayer.h"
#include "imgui/ImGuiTextures.h"

using namespace f4cf;
using Microsoft::WRL::ComPtr;

namespace
{
    /**
     * A device that draws in software, or null on a machine with none.
     */
    ComPtr<ID3D11Device> testDevice()
    {
        ComPtr<ID3D11Device> device;
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, nullptr);
        return device;
    }

    /**
     * An ImGui context with the canvas font, as a canvas has it, with no game: ImGui asks for its textures
     * and the test answers.
     */
    struct TestContext
    {
        TestContext()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(400.0f, 300.0f);
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            imgui::internal::loadCanvasFont(24.0f);
        }

        ~TestContext()
        {
            ImGui::DestroyContext();
        }
    };

    /**
     * A frame with a letter in it at the given size, which ImGui rasterizes into its font texture.
     */
    void drawLetter(const float size)
    {
        ImGui::NewFrame();
        ImGui::Begin("test");
        ImGui::PushFont(nullptr, size);
        ImGui::TextUnformatted("H");
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
    }

    ImTextureData* fontTexture()
    {
        return ImGui::GetIO().Fonts->TexData;
    }

    /**
     * Whether the rows of the box in the pixels are those of ImGui's texture there.
     */
    bool samePixels(const D3D11_BOX& box, const std::uint8_t* pixels, const std::size_t rowPitch)
    {
        const std::size_t rowBytes = static_cast<std::size_t>(box.right - box.left) * 4;
        for (UINT row = 0; row < box.bottom - box.top; ++row) {
            if (std::memcmp(pixels + row * rowPitch, fontTexture()->GetPixelsAt(static_cast<int>(box.left), static_cast<int>(box.top + row)), rowBytes) != 0) {
                return false;
            }
        }
        return true;
    }
}

TEST_CASE("ImGui textures: the font texture is created when ImGui asks, and the frame names it by its view")
{
    const auto device = testDevice();
    if (!device) {
        SKIP("no software D3D11 device on this machine");
    }
    const TestContext context;
    drawLetter(24.0f);
    REQUIRE(fontTexture()->Status == ImTextureStatus_WantCreate);

    // created with all its pixels, so there is nothing to write
    REQUIRE(imgui::internal::updateTextures(device.Get()).empty());
    REQUIRE(fontTexture()->Status == ImTextureStatus_OK);
    const ImTextureID id = fontTexture()->TexID;
    REQUIRE(id != ImTextureID_Invalid);

    // the frame holds the view, which is the texture's id
    std::vector<render::TextureView> views;
    imgui::internal::addTextureViews(views);
    REQUIRE(std::ranges::any_of(views, [id](const render::TextureView& view) {
        return reinterpret_cast<std::uintptr_t>(view.Get()) == id;
    }));

    // and the clone names the texture by its id alone, which outlives what ImGui does next. A new window
    // is drawn from its second frame.
    drawLetter(24.0f);
    REQUIRE(imgui::internal::updateTextures(device.Get()).empty());
    imgui::internal::ClonedDrawData clone;
    clone.copyFrom(*ImGui::GetDrawData());
    REQUIRE(!clone.drawData.CmdLists.empty());
    for (const ImDrawList* list : clone.drawData.CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            CHECK(command.TexRef._TexData == nullptr);
            CHECK(command.TexRef.GetTexID() == id);
        }
    }
}

TEST_CASE("ImGui textures: the glyphs of a new size are copied, and written into the texture as ImGui has them")
{
    const auto device = testDevice();
    if (!device) {
        SKIP("no software D3D11 device on this machine");
    }
    const TestContext context;
    drawLetter(24.0f);
    (void)imgui::internal::updateTextures(device.Get());

    drawLetter(40.0f);
    REQUIRE(fontTexture()->Status == ImTextureStatus_WantUpdates);
    const auto writes = imgui::internal::updateTextures(device.Get());
    REQUIRE(fontTexture()->Status == ImTextureStatus_OK);
    REQUIRE(!writes.empty());
    for (const auto& write : writes) {
        CHECK(samePixels(write.box, write.pixels.data(), static_cast<std::size_t>(write.box.right - write.box.left) * 4));
    }

    // the render thread's part, read back from the GPU
    ComPtr<ID3D11DeviceContext> immediate;
    device->GetImmediateContext(immediate.GetAddressOf());
    imgui::internal::writeTextures(immediate.Get(), writes);

    D3D11_TEXTURE2D_DESC desc{};
    writes.front().texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())));
    immediate->CopyResource(staging.Get(), writes.front().texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    REQUIRE(SUCCEEDED(immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    for (const auto& write : writes) {
        const auto* at = static_cast<const std::uint8_t*>(mapped.pData) + write.box.top * mapped.RowPitch + write.box.left * 4;
        CHECK(samePixels(write.box, at, mapped.RowPitch));
    }
    immediate->Unmap(staging.Get(), 0);
}
