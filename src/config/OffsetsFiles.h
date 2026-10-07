#pragma once

#include <Windows.h>
#include <string>
#include <unordered_map>

namespace f4cf::config
{
    /**
     * The offsets files of a mod: transforms by their names in JSON, as config/OffsetsJson.h reads and writes it.
     * A mod has them embedded in its DLL and in files on disk, and saves the ones the player tunes.
     * ConfigBase derives from it, so a mod's config calls them by their names.
     */
    struct OffsetsFiles
    {
        static std::unordered_map<std::string, RE::NiTransform> loadEmbeddedOffsets(WORD fromResourceId, WORD toResourceId);
        static void loadOffsetJsonFile(const std::string& file, std::unordered_map<std::string, RE::NiTransform>& offsetsMap);
        static std::unordered_map<std::string, RE::NiTransform> loadOffsetsFromFilesystem(const std::string& path);
        static bool saveOffsetsToJsonFile(const std::string& name, const RE::NiTransform& transform, const std::string& file);
    };
}
