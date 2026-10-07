#include "ConfigIniValue.h"

#include <format>

namespace f4cf::config
{
    /**
     * Write this value to the INI using the setter matching its underlying type.
     */
    void IniValue::applyTo(CSimpleIniA& ini, const char* section, const char* key) const
    {
        std::visit(
            [&]<typename T>(const T& v) {
                if constexpr (std::is_same_v<T, bool>) {
                    ini.SetBoolValue(section, key, v);
                } else if constexpr (std::is_same_v<T, int>) {
                    ini.SetLongValue(section, key, v);
                } else if constexpr (std::is_same_v<T, float>) {
                    ini.SetDoubleValue(section, key, v);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    ini.SetValue(section, key, v.c_str());
                }
            },
            _value);
    }

    /**
     * Render this value to its INI string form. Kept consistent with applyTo so the result parses back
     * through CSimpleIniA's GetBoolValue/GetLongValue/GetDoubleValue/GetValue to the same value.
     */
    std::string IniValue::toString() const
    {
        if (const auto* v = std::get_if<bool>(&_value)) {
            return *v ? "true" : "false";
        }
        if (const auto* v = std::get_if<int>(&_value)) {
            return std::to_string(*v);
        }
        if (const auto* v = std::get_if<float>(&_value)) {
            return std::format("{}", *v);
        }
        if (const auto* v = std::get_if<std::string>(&_value)) {
            return *v;
        }
        return {};
    }
}
