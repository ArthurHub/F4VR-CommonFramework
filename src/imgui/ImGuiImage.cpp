#include "ImGuiImage.h"

#include "ImGuiRenderer.h"

namespace f4cf::imgui
{
    namespace
    {
        // the views of the images drawn in the frame that is being built, which the published frame takes
        std::vector<render::TextureView> s_frameTextures;
    }

    /**
     * The texture as an item of the given size at the cursor, as ImGui::Image is, in layout pixels.
     * A texture with no view yet, and one that is missing, keeps its room and draws nothing.
     */
    void image(render::Texture& texture, const ImVec2& size, const ImU32 tint)
    {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        if (ImGui::IsItemVisible()) {
            addImage(ImGui::GetWindowDrawList(), texture, min, ImVec2(min.x + size.x, min.y + size.y), tint);
        }
    }

    /**
     * The texture in a rectangle of the draw list, as ImDrawList::AddImage is, for content that places
     * its own drawing. The tint multiplies the image's colors and its alpha.
     */
    void addImage(ImDrawList* drawList, render::Texture& texture, const ImVec2& min, const ImVec2& max, const ImU32 tint)
    {
        // loads on first use, and has to be read in every frame: the engine may replace the view
        internal::addImageView(drawList, texture.view(), texture.isSRGB(), min, max, tint);
    }

    /**
     * Add the view as an image, and keep it for the frame. ImGui's texture is the view's pointer, which
     * the render thread draws with later, so the published frame holds the views of its images.
     * A view that decodes sRGB is drawn with a shader of its own, which the two callbacks set and put back.
     */
    void internal::addImageView(ImDrawList* drawList, render::TextureView view, const bool srgb, const ImVec2& min, const ImVec2& max, const ImU32 tint)
    {
        if (!view) {
            return;
        }
        if (srgb) {
            drawList->AddCallback(&renderer::setSrgbImageShader, nullptr);
        }
        drawList->AddImage(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(view.Get())), min, max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
        if (srgb) {
            drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        }
        s_frameTextures.push_back(std::move(view));
    }

    /**
     * The views of the images added since the last call, for the frame that is published with them.
     */
    std::vector<render::TextureView> internal::takeFrameTextures()
    {
        return std::exchange(s_frameTextures, {});
    }
}
