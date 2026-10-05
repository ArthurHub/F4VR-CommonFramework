#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The dev layout's file, apart from the game: plain std and SimpleIni only, so it is unit tested.
namespace f4cf::vrui
{
    /**
     * One attached root for the dev layout file: its name, and the path and fields of every element of its
     * tree in the order of the tree, the root first. A path is the names from the root down joined by dots.
     */
    struct UIDevLayoutRoot
    {
        std::string name;
        std::vector<std::pair<std::string, std::string>> lines;
    };

    std::optional<std::map<std::string, std::string>> syncDevLayoutFile(const std::string& filePath, const std::vector<UIDevLayoutRoot>& roots);
}
