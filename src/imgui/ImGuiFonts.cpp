#include "ImGuiFonts.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include <windows.h>

#include <imgui.h>

namespace f4cf::imgui::internal
{
    namespace
    {
        // Below ~16 the glyphs lose their shape; above ~128 the atlas grows for nothing.
        constexpr float MIN_FONT_SIZE = 16.0f;
        constexpr float MAX_FONT_SIZE = 128.0f;

        std::string windowsFontPath(const char* fileName)
        {
            char windowsDir[MAX_PATH] = {};
            if (GetWindowsDirectoryA(windowsDir, MAX_PATH) == 0) {
                return {};
            }
            return std::format("{}\\Fonts\\{}", windowsDir, fileName);
        }
    }

    ImFont* loadPanelFont(const char* modName, const float sizePixels)
    {
        auto& io = ImGui::GetIO();
        const float size = std::clamp(sizePixels, MIN_FONT_SIZE, MAX_FONT_SIZE);

        std::vector<std::string> candidates;
        if (modName && *modName) {
            candidates.push_back(std::format("Data\\Interface\\{}\\{}.ttf", modName, modName));
        }
        candidates.push_back(windowsFontPath("arialbd.ttf"));
        candidates.push_back(windowsFontPath("segoeuib.ttf"));

        for (const auto& path : candidates) {
            std::error_code ec;
            if (path.empty() || !std::filesystem::exists(path, ec)) {
                continue;
            }
            if (ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size)) {
                logger::info("Panel font: '{}' at {:.0f}px", path, size);
                return font;
            }
            logger::warn("Panel font: failed to load '{}'", path);
        }

        logger::warn("Panel font: no TTF found, falling back to the embedded bitmap font - panels will look pixelated");
        return io.Fonts->AddFontDefault();
    }
}
