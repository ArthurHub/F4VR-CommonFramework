#pragma once

#include <cstddef>
#include <exception>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>

// The offsets JSON, apart from the game: plain std and nlohmann-json only, so it is unit tested.
namespace f4cf::config
{
    /**
     * Read the transforms of an offsets JSON into the map, each under its name. A name that the map already has
     * gets the transform of the JSON.
     * The JSON is an object with a member for each name, which holds the rotation matrix as "rotation", its 12
     * numbers row by row with 4 to a row, the position as "x", "y" and "z", and the "scale".
     * Throws when a member lacks one of them, or has something else than a number for it.
     * Transform is any type with the rotate, translate and scale of the game's NiTransform, which it is in a mod.
     */
    template <class Transform>
    void readOffsetsJson(const nlohmann::json& json, std::unordered_map<std::string, Transform>& offsets)
    {
        try {
            for (const auto& [name, value] : json.items()) {
                Transform transform;
                const auto& rotation = value.at("rotation");
                for (std::size_t i = 0; i < 3; i++) {
                    for (std::size_t j = 0; j < 4; j++) {
                        transform.rotate[i][j] = rotation.at(i * 4 + j).get<float>();
                    }
                }
                transform.translate.x = value.at("x").get<float>();
                transform.translate.y = value.at("y").get<float>();
                transform.translate.z = value.at("z").get<float>();
                transform.scale = value.at("scale").get<float>();
                offsets[name] = transform;
            }
        } catch (const std::exception& ex) {
            std::throw_with_nested(std::runtime_error(std::string("Failed to load offset json:\n\t") + ex.what()));
        }
    }

    /**
     * The offsets JSON of one transform under its name, as readOffsetsJson reads it.
     */
    template <class Transform>
    nlohmann::json writeOffsetsJson(const std::string& name, const Transform& transform)
    {
        nlohmann::json json;
        for (std::size_t i = 0; i < 3; i++) {
            for (std::size_t j = 0; j < 4; j++) {
                json[name]["rotation"].push_back(transform.rotate[i][j]);
            }
        }
        json[name]["x"] = transform.translate.x;
        json[name]["y"] = transform.translate.y;
        json[name]["z"] = transform.translate.z;
        json[name]["scale"] = transform.scale;
        return json;
    }
}
