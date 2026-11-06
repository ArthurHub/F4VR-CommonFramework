#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <string>

namespace f4cf::imgui::internal
{
    /**
     * A PNG of the canvases' texture that was written: where, its size, and whether it is a dedicated canvas's own
     * texture, which is that canvas alone, or the atlas the canvases share.
     */
    struct SnapshotFile
    {
        std::filesystem::path path;
        int width = 0;
        int height = 0;
        bool dedicated = false;
    };

    std::expected<SnapshotFile, std::string> saveSnapshot(const std::filesystem::path& path, std::chrono::milliseconds timeout);
}
