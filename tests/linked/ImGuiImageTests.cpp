#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include <d3d11.h>
#include <imgui.h>
#include <wrl/client.h>

#include "imgui/ImGuiImage.h"
#include "imgui/ImGuiRenderer.h"
#include "render/Texture.h"

using namespace f4cf;
using Microsoft::WRL::ComPtr;

namespace
{
    /**
     * A frame of ImGui with one window, with no game and no renderer: only its draw data is looked at.
     */
    struct TestFrame
    {
        TestFrame()
        {
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2(400.0f, 300.0f);
            // a renderer that takes ImGui's texture requests, as the canvases have
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            ImGui::NewFrame();
            ImGui::Begin("test");
        }

        ~TestFrame()
        {
            ImGui::End();
            ImGui::EndFrame();
            ImGui::DestroyContext();
        }
    };

    /**
     * The view of a small texture on a device that draws in software, or null on a machine with none.
     */
    render::TextureView testView()
    {
        ComPtr<ID3D11Device> device;
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, nullptr);
        const std::vector<std::uint8_t> pixels(2 * 2 * 4, 255);
        return render::createTextureView(device.Get(), 2, 2, pixels);
    }

    /**
     * The number of references held on the view.
     */
    ULONG references(const render::TextureView& view)
    {
        view->AddRef();
        return view->Release();
    }

    /**
     * The command of the draw list that draws the view as an image, or null.
     */
    const ImDrawCmd* imageCommand(const ImDrawList* drawList, const render::TextureView& view)
    {
        for (const ImDrawCmd& command : drawList->CmdBuffer) {
            if (command.TexRef.GetTexID() == reinterpret_cast<std::uintptr_t>(view.Get()) && command.ElemCount == 6) {
                return &command;
            }
        }
        return nullptr;
    }
}

TEST_CASE("ImGui image: an image is one draw of its view, with no callback around it")
{
    const render::TextureView view = testView();
    if (!view) {
        SKIP("no software D3D11 device on this machine");
    }
    const TestFrame frame;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    imgui::internal::addImageView(drawList, view, false, ImVec2(10.0f, 20.0f), ImVec2(110.0f, 70.0f), IM_COL32(255, 255, 255, 128));

    // one draw of the view, in the backend's own state: no callback around it
    const ImDrawCmd* image = imageCommand(drawList, view);
    REQUIRE(image);
    for (const ImDrawCmd& command : drawList->CmdBuffer) {
        REQUIRE(command.UserCallback == nullptr);
    }

    // the image's corners and its tint
    const ImDrawVert& topLeft = drawList->VtxBuffer[image->VtxOffset + drawList->IdxBuffer[image->IdxOffset]];
    REQUIRE(topLeft.pos.x == 10.0f);
    REQUIRE(topLeft.pos.y == 20.0f);
    REQUIRE(topLeft.col == IM_COL32(255, 255, 255, 128));

    (void)imgui::internal::takeFrameTextures();
}

TEST_CASE("ImGui image: a view that decodes sRGB is drawn between the callbacks that set its shader and put it back")
{
    const render::TextureView view = testView();
    if (!view) {
        SKIP("no software D3D11 device on this machine");
    }
    const TestFrame frame;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    imgui::internal::addImageView(drawList, view, true, ImVec2(0.0f, 0.0f), ImVec2(10.0f, 10.0f), IM_COL32_WHITE);

    const ImDrawCmd* image = imageCommand(drawList, view);
    REQUIRE(image);
    REQUIRE(image > drawList->CmdBuffer.begin());
    REQUIRE(image + 1 < drawList->CmdBuffer.end());
    REQUIRE((image - 1)->UserCallback == &imgui::internal::renderer::setSrgbImageShader);
    REQUIRE((image + 1)->UserCallback == ImDrawCallback_ResetRenderState);

    (void)imgui::internal::takeFrameTextures();
}

TEST_CASE("ImGui image: the frame keeps the views of its images, each until the frame lets it go")
{
    render::TextureView view = testView();
    if (!view) {
        SKIP("no software D3D11 device on this machine");
    }
    const ULONG alone = references(view);
    std::vector<render::TextureView> kept;
    {
        const TestFrame frame;
        imgui::internal::addImageView(ImGui::GetWindowDrawList(), view, false, ImVec2(0.0f, 0.0f), ImVec2(10.0f, 10.0f), IM_COL32_WHITE);
        imgui::internal::addImageView(ImGui::GetWindowDrawList(), view, false, ImVec2(20.0f, 0.0f), ImVec2(30.0f, 10.0f), IM_COL32_WHITE);
        kept = imgui::internal::takeFrameTextures();
    }

    REQUIRE(kept.size() == 2);
    REQUIRE(kept[0].Get() == view.Get());
    REQUIRE(references(view) == alone + 2);

    // taken once: the next frame starts with none
    REQUIRE(imgui::internal::takeFrameTextures().empty());

    // the mod drops its texture, and the frame's views still hold it
    ID3D11ShaderResourceView* raw = view.Get();
    view.Reset();
    raw->AddRef();
    REQUIRE(raw->Release() == alone + 1);
}

TEST_CASE("ImGui image: a texture with no view draws nothing and keeps nothing")
{
    const TestFrame frame;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const int before = drawList->CmdBuffer.Size;

    imgui::internal::addImageView(drawList, {}, false, ImVec2(0.0f, 0.0f), ImVec2(10.0f, 10.0f), IM_COL32_WHITE);

    REQUIRE(drawList->CmdBuffer.Size == before);
    REQUIRE(imgui::internal::takeFrameTextures().empty());
}
