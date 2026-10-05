#include "UIDevLayoutFile.h"

#include <SimpleIni.h>
#include <filesystem>

namespace
{
    // what a new file starts with; SimpleIni keeps it as the file's comment
    constexpr auto FILE_COMMENT =
        "# VRUI dev layout: edit a line and save, and it applies in the running game.\n"
        "# The file is written while bVRUIDevLayout in the [Debug] section of the mod's INI is true,\n"
        "# and removed when it is turned off.\n"
        "\n";

    /**
     * The key of an element's line in the section of its root: the root's name for the root itself, and the
     * path below the root for an element under it. A direct child named as its root shares the root's line.
     */
    std::string toFileKey(const std::string& rootName, const std::string& path)
    {
        return path.size() > rootName.size() ? path.substr(rootName.size() + 1) : path;
    }

    /**
     * The path of the element a line belongs to, from the line's section and key.
     */
    std::string toElementPath(const std::string& section, const std::string& key)
    {
        return key == section ? key : section + "." + key;
    }
}

namespace f4cf::vrui
{
    /**
     * Read the dev layout file, add a line for every given element that has none, and return all of the file's
     * lines by the path of their element. The file is written only when a line was added, and is created
     * when it is missing.
     * Every root is a section named after it. A root that is not in the file gets its section at the end of
     * the file, with its lines in the order of its tree. A line added to a section that is already there goes
     * to the section's end, as SimpleIni writes keys in the order they were added. SimpleIni also keeps the
     * file's comments, each with the line or section below it.
     * Returns no value when the file could not be read or written.
     */
    std::optional<std::map<std::string, std::string>> syncDevLayoutFile(const std::string& filePath, const std::vector<UIDevLayoutRoot>& roots)
    {
        CSimpleIniA ini;
        std::error_code error;
        const bool exists = std::filesystem::exists(filePath, error);
        if ((exists ? ini.LoadFile(filePath.c_str()) : ini.LoadData(std::string(FILE_COMMENT))) < 0) {
            return std::nullopt;
        }

        bool added = false;
        for (const auto& root : roots) {
            for (const auto& [path, line] : root.lines) {
                const auto key = toFileKey(root.name, path);
                if (!ini.GetValue(root.name.c_str(), key.c_str())) {
                    ini.SetValue(root.name.c_str(), key.c_str(), line.c_str());
                    added = true;
                }
            }
        }
        if (added && ini.SaveFile(filePath.c_str()) < 0) {
            return std::nullopt;
        }

        std::map<std::string, std::string> lines;
        CSimpleIniA::TNamesDepend sections;
        ini.GetAllSections(sections);
        for (const auto& section : sections) {
            if (const auto keys = ini.GetSection(section.pItem)) {
                for (const auto& [key, value] : *keys) {
                    lines[toElementPath(section.pItem, key.pItem)] = value;
                }
            }
        }
        return lines;
    }
}
