#pragma once

#include <SimpleIni.h>
#include <string>
#include <utility>
#include <variant>

// A value of the mod's INI, apart from the game: plain std and SimpleIni only, so it is unit tested.
namespace f4cf::config
{
    /**
     * A typed INI value, for a save of several values (ConfigBase::saveIniConfigValues) and for a session
     * override (ConfigBase::setConfigOverride).
     * Constructs implicitly from any supported type, so callers can write {"key", 1.5f}.
     */
    class IniValue
    {
    public:
        IniValue(bool value)
            : _value(value)
        {}

        IniValue(int value)
            : _value(value)
        {}

        IniValue(float value)
            : _value(value)
        {}

        IniValue(const char* value)
            : _value(std::string(value))
        {}

        IniValue(std::string value)
            : _value(std::move(value))
        {}

        void applyTo(CSimpleIniA& ini, const char* section, const char* key) const;

        std::string toString() const;

    private:
        std::variant<bool, int, float, std::string> _value;
    };
}
