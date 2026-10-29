#pragma once

#include <vector>

#include <imgui.h>

#include "../render/Texture.h"

namespace f4cf::imgui
{
    // A render::Texture in the content of a canvas, in place of ImGui::Image and ImDrawList::AddImage:
    //
    //     imgui::image(*_picture, ImVec2(200.0f, 100.0f));
    //
    // The texture is kept until the frame that draws it has been drawn, so it can be dropped at any time.
    // Called from a canvas's content callback, so on the GAME thread.
    void image(render::Texture& texture, const ImVec2& size, ImU32 tint = IM_COL32_WHITE);
    void addImage(ImDrawList* drawList, render::Texture& texture, const ImVec2& min, const ImVec2& max, ImU32 tint = IM_COL32_WHITE);

    namespace internal
    {
        void addImageView(ImDrawList* drawList, render::TextureView view, bool srgb, const ImVec2& min, const ImVec2& max, ImU32 tint);
        std::vector<render::TextureView> takeFrameTextures();
    }
}
