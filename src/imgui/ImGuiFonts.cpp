#include "ImGuiFonts.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include <windows.h>

#include <imgui.h>

#include "ImGuiSettings.h"

namespace
{
    std::string windowsFontPath(const char* fileName)
    {
        char windowsDir[MAX_PATH] = {};
        if (GetWindowsDirectoryA(windowsDir, MAX_PATH) == 0) {
            return {};
        }
        return std::format("{}\\Fonts\\{}", windowsDir, fileName);
    }
}

namespace f4cf::imgui::internal
{
    ImFont* loadPanelFont(const char* modName, const float sizePixels, const float rasterScale)
    {
        auto& io = ImGui::GetIO();
        const float size = std::clamp(sizePixels, MIN_FONT_SIZE_PIXELS, MAX_FONT_SIZE_PIXELS);
        const float rasterSize = size * (std::max)(1.0f, rasterScale);

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
            if (ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), rasterSize)) {
                logger::info("Panel font: '{}' at {:.0f}px, rasterized at {:.0f}px", path, size, rasterSize);
                return font;
            }
            logger::warn("Panel font: failed to load '{}'", path);
        }

        logger::warn("Panel font: no TTF found, falling back to the embedded bitmap font - panels will look pixelated");
        ImFontConfig config;
        config.SizePixels = rasterSize;
        return io.Fonts->AddFontDefault(&config);
    }
}
