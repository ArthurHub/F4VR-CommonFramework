#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <utility>
#include <variant>

#include "common/TomlEditor.h"

// A value of the mod's config, apart from the game: plain std and toml++ only, so it is unit tested.
namespace f4cf::config
{
    /**
     * A typed config value, for a save of several values (ConfigBase::saveConfigValues) and for a session override
     * (ConfigBase::setConfigOverride).
     * Constructs implicitly from any supported type, so callers can write {"key", 1.5f}.
     */
    class ConfigValue
    {
    public:
        ConfigValue(bool value)
            : _value(value)
        {}

        ConfigValue(int value)
            : _value(value)
        {}

        ConfigValue(float value)
            : _value(value)
        {}

        ConfigValue(const char* value)
            : _value(std::string(value))
        {}

        ConfigValue(std::string value)
            : _value(std::move(value))
        {}

        std::string toString() const;

        std::optional<common::TomlEditor::Value> toTomlLike(const toml::node* existing) const;

    private:
        std::variant<bool, int, float, std::string> _value;
    };

    /**
     * A value with the section and key it is for: one value of a save of several (ConfigBase::saveConfigValues)
     * and of a set of session overrides (ConfigBase::setConfigOverrides).
     */
    struct ConfigEntry
    {
        std::string section;
        std::string key;
        ConfigValue value;
    };

    std::optional<toml::table> typedLike(const toml::node* existing, std::string_view text);
}
