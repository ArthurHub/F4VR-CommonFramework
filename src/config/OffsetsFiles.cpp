#include "OffsetsFiles.h"

#include <nlohmann/json.hpp>

#include "OffsetsJson.h"
#include "common/CommonUtils.h"

using json = nlohmann::json;

namespace f4cf::config
{
    /**
     * Load all embedded in resources offsets in the given resource range.
     */
    std::unordered_map<std::string, RE::NiTransform> OffsetsFiles::loadEmbeddedOffsets(const WORD fromResourceId, const WORD toResourceId)
    {
        std::unordered_map<std::string, RE::NiTransform> offsets;
        for (WORD resourceId = fromResourceId; resourceId <= toResourceId; resourceId++) {
            auto resourceOpt = common::getEmbeddedResourceAsStringIfExists(resourceId);
            if (resourceOpt.has_value()) {
                json json = json::parse(resourceOpt.value());
                readOffsetsJson(json, offsets);
            }
        }
        return offsets;
    }

    /**
     * Load offset data from given json file path and store it in the given map.
     * Use the entry key in the json file but for everything to work properly the name of the json should match the key.
     */
    void OffsetsFiles::loadOffsetJsonFile(const std::string& file, std::unordered_map<std::string, RE::NiTransform>& offsetsMap)
    {
        try {
            std::ifstream inF;
            inF.open(file, std::ios::in);
            if (inF.fail()) {
                logger::warn("cannot open {}", file.c_str());
                inF.close();
                return;
            }

            json weaponJson;
            try {
                inF >> weaponJson;
            } catch (json::parse_error& ex) {
                logger::info("cannot open {}: parse error at byte {}", file.c_str(), ex.byte);
                inF.close();
                return;
            }
            inF.close();

            readOffsetsJson(weaponJson, offsetsMap);
        } catch (std::exception& ex) {
            std::throw_with_nested(std::runtime_error(fmt::format("Failed to load offset json from file '{}':\n\t{}", file.c_str(), ex.what())));
        }
    }

    /**
     * Load all the offsets found in json files in a specific folder.
     */
    std::unordered_map<std::string, RE::NiTransform> OffsetsFiles::loadOffsetsFromFilesystem(const std::string& path)
    {
        std::unordered_map<std::string, RE::NiTransform> offsets;
        for (const auto& file : std::filesystem::directory_iterator(path)) {
            if (file.exists() && !file.is_directory()) {
                loadOffsetJsonFile(file.path().string(), offsets);
            }
        }
        return offsets;
    }

    /**
     * Save the given offsets transform to a json file using the given name.
     */
    bool OffsetsFiles::saveOffsetsToJsonFile(const std::string& name, const RE::NiTransform& transform, const std::string& file)
    {
        logger::info("Saving offsets '{}' to '{}'", name.c_str(), file.c_str());
        const auto offsetJson = writeOffsetsJson(name, transform);

        std::ofstream outF;
        outF.open(file, std::ios::out);
        if (outF.fail()) {
            logger::info("cannot open '{}' for writing", file.c_str());
            return false;
        }
        try {
            outF << std::setw(4) << offsetJson;
            outF.close();
            return true;
        } catch (std::exception& e) {
            outF.close();
            logger::warn("Unable to save json '{}': {}", file.c_str(), e.what());
            return false;
        }
    }
}
