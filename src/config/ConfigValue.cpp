#include "ConfigValue.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>

#include "vrcf/InputBindingParser.h"

namespace
{
    /**
     * The text with the spaces and tabs around it dropped.
     */
    std::string_view trimmed(std::string_view text)
    {
        const auto first = text.find_first_not_of(" \t");
        if (first == std::string_view::npos) {
            return {};
        }
        text.remove_prefix(first);
        return text.substr(0, text.find_last_not_of(" \t") + 1);
    }

    /**
     * A bool from its text as a person writes it, in any case: true/false, yes/no, on/off, 1/0.
     */
    std::optional<bool> parseBool(const std::string_view text)
    {
        std::string lower(trimmed(text));
        std::ranges::transform(lower, lower.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lower == "true" || lower == "yes" || lower == "on" || lower == "1") {
            return true;
        }
        if (lower == "false" || lower == "no" || lower == "off" || lower == "0") {
            return false;
        }
        return std::nullopt;
    }

    /**
     * A number of type T from the whole text, with no other character in it.
     */
    template <typename T>
    std::optional<T> parseNumber(const std::string_view text)
    {
        auto number = trimmed(text);
        if (number.starts_with('+')) {
            number.remove_prefix(1);
        }
        T value{};
        const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), value);
        if (error != std::errc{} || end != number.data() + number.size() || number.empty()) {
            return std::nullopt;
        }
        return value;
    }

    /**
     * The value as the key "v" of a table, which owns it.
     */
    template <typename T>
    toml::table holding(T&& value)
    {
        toml::table table;
        table.insert_or_assign("v", std::forward<T>(value));
        return table;
    }
}

namespace f4cf::config
{
    /**
     * Render this value to its text form, which typedLike reads back as the same value in the type of the key.
     */
    std::string ConfigValue::toString() const
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

    /**
     * This value to write over `existing`, the value the file has for the key, or null when it has none.
     * A string takes the type of the existing value, see typedLike, and an int written over a float stays a float.
     * No value when a string can't be read as the existing value's type, so a save does not turn a float into text.
     */
    std::optional<common::TomlEditor::Value> ConfigValue::toTomlLike(const toml::node* existing) const
    {
        if (const auto* v = std::get_if<bool>(&_value)) {
            return common::TomlEditor::Value(*v);
        }
        if (const auto* v = std::get_if<int>(&_value)) {
            if (existing && existing->is_floating_point()) {
                return common::TomlEditor::Value(static_cast<double>(*v));
            }
            return common::TomlEditor::Value(*v);
        }
        if (const auto* v = std::get_if<float>(&_value)) {
            return common::TomlEditor::Value(*v);
        }
        const auto typed = typedLike(existing, std::get<std::string>(_value));
        if (!typed) {
            return std::nullopt;
        }
        return common::TomlEditor::Value(*typed->get("v"));
    }

    /**
     * A value given as text, in the type of `existing`, the value the file has for its key: a bool for a bool
     * (true/false, yes/no, on/off, 1/0), a number for a number, the text as it is for a string, and the text read as
     * TOML for an array, a table or a date. A binding (see vrcf::parseInputBindingTable) also takes its line, as
     * "offhand tap grip" or "none", written as its table (see vrcf::inputBindingTomlText), so a caller that gives a
     * binding as the text a config had it in still sets it. With no existing value the text is read as a TOML value
     * ("1.5" a float, "true" a bool, "[1, 2]" an array), and as a string when it is not one ("offhand tap grip").
     * Returns the value as the key "v" of a table, which owns it, or no value when the text is not of the existing
     * value's type. Session overrides and saves give their values as text, and the file keeps its types by it.
     */
    std::optional<toml::table> typedLike(const toml::node* existing, const std::string_view text)
    {
        if (existing && existing->is_boolean()) {
            const auto value = parseBool(text);
            return value ? std::optional{ holding(*value) } : std::nullopt;
        }
        if (existing && existing->is_integer()) {
            const auto value = parseNumber<std::int64_t>(text);
            return value ? std::optional{ holding(*value) } : std::nullopt;
        }
        if (existing && existing->is_floating_point()) {
            const auto value = parseNumber<double>(text);
            return value ? std::optional{ holding(*value) } : std::nullopt;
        }
        if (existing && existing->is_string()) {
            return holding(std::string(text));
        }

        // an array, a table or a date, or no value to take the type of: the text as TOML
        try {
            auto table = toml::parse(std::format("v = {}", text));
            if (!existing || table.get("v")->type() == existing->type()) {
                return table;
            }
        } catch (const toml::parse_error&) {
            if (!existing) {
                return holding(std::string(text));
            }
        }

        // a binding given as its line, "offhand tap grip" or "none", as a caller of the time the config had bindings
        // as text gives it
        if (existing->is_table() && vrcf::parseInputBindingTable(*existing)) {
            if (const auto binding = vrcf::parseInputBinding(text)) {
                // read from its text, so the table keeps the order of its keys, see TomlEditor::Value
                return toml::parse("v = " + vrcf::inputBindingTomlText(*binding));
            }
        }
        return std::nullopt;
    }

    /**
     * A value given as text, as it applies over `existing`, see typedLike, in text again: a string as it is, and any
     * other value as its TOML text, so "no" over a bool is "false" and "1" over a float is "1.0". It is the form a
     * value of the file is read back in, see ConfigBase::getConfigValue. No value when the text is not of the
     * existing value's type.
     */
    std::optional<std::string> appliedText(const toml::node* existing, const std::string_view text)
    {
        const auto typed = typedLike(existing, text);
        if (!typed) {
            return std::nullopt;
        }
        const auto* value = typed->get("v");
        if (const auto* string = value->as_string()) {
            return string->get();
        }
        return common::TomlEditor::Value(*value).text();
    }
}
