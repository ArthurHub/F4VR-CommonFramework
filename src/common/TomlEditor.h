#pragma once

#include <concepts>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <vector>

// Editing the text of a TOML document, apart from the game: plain std and toml++ only, so it is unit tested.
namespace f4cf::common
{
    std::expected<toml::table, std::string> parseToml(std::string_view text, std::string_view sourceName = {});

    /**
     * Sets values in the text of a TOML document and leaves every other byte as it was: comments, blank lines, the
     * order of the keys and how every other value is written. toml++ has no writer that keeps those, so it only
     * parses here: a value is set by replacing the text of the old one where toml++ read it, and a key the text
     * does not have is added as a line of its own. Each edit is parsed back and kept only when the document then
     * holds the new value and nothing else changed, see set.
     */
    class TomlEditor
    {
    public:
        /**
         * A value to set, as the TOML text it is written as. Constructs implicitly from a bool, an integer, a float,
         * a double, a string, or a toml++ node, which writes an array or a table inline.
         */
        class Value
        {
        public:
            Value(bool value);

            template <std::integral T>
                requires(!std::same_as<T, bool>)
            Value(const T value)
                : _text(std::to_string(value))
            {}

            Value(float value);
            Value(double value);
            Value(const char* value);
            Value(std::string_view value);
            Value(const std::string& value);
            Value(const toml::node& value);

            /**
             * The value as TOML text.
             */
            const std::string& text() const
            {
                return _text;
            }

            /**
             * The string of a string value, which can be written as a literal string in place of a literal one.
             */
            const std::optional<std::string>& string() const
            {
                return _string;
            }

        private:
            std::string _text;
            std::optional<std::string> _string;
        };

        /**
         * One value to set, for a set of several. The keys of the path are views: the strings must outlive the set.
         */
        struct Edit
        {
            std::vector<std::string_view> path;
            Value value;
        };

        explicit TomlEditor(std::string text, std::string_view sourceName = {});

        /**
         * Whether the text given parsed. Nothing can be set in a text that did not.
         */
        bool ok() const
        {
            return _parsed;
        }

        /**
         * Why the text did not parse, or why the last set failed. Empty when neither happened.
         */
        const std::string& error() const
        {
            return _error;
        }

        /**
         * The text with every successful set applied.
         */
        const std::string& text() const
        {
            return _text;
        }

        /**
         * The document text() holds.
         */
        const toml::table& table() const
        {
            return _table;
        }

        bool set(std::span<const std::string_view> path, const Value& value);

        /**
         * Set the value at a path of keys, such as {"Section", "key"}, see set.
         */
        bool set(const std::initializer_list<std::string_view> path, const Value& value)
        {
            return set(std::span(path.begin(), path.size()), value);
        }

        bool set(std::span<const Edit> edits);

        /**
         * Set several values written in place, such as {{{"Section", "a"}, 1}, {{"Section", "b"}, true}}, see set.
         */
        bool set(const std::initializer_list<Edit> edits)
        {
            return set(std::span(edits.begin(), edits.size()));
        }

    private:
        std::expected<std::string, std::string> insert(std::span<const std::string_view> path, std::span<const toml::node* const> chain, const Value& value) const;

        std::string _text;
        std::string _sourceName;
        toml::table _table;
        bool _parsed = false;
        std::string _error;
    };
}
