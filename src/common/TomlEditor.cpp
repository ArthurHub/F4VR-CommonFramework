#include "TomlEditor.h"

#include "perf/Perf.h"

#include <algorithm>
#include <format>
#include <sstream>
#include <type_traits>
#include <vector>

namespace
{
    constexpr std::string_view UTF8_BOM = "\xEF\xBB\xBF";

    /**
     * The line ending the text uses: CRLF when any line ends with it.
     */
    std::string_view lineEnding(const std::string_view text)
    {
        return text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    }

    /**
     * The number of bytes of the UTF-8 code point that starts with `lead`.
     */
    std::size_t codePointLength(const char lead)
    {
        const auto c = static_cast<unsigned char>(lead);
        if ((c & 0xE0) == 0xC0) {
            return 2;
        }
        if ((c & 0xF0) == 0xE0) {
            return 3;
        }
        if ((c & 0xF8) == 0xF0) {
            return 4;
        }
        return 1;
    }

    /**
     * The byte offset of the start of a line, counted from 1, or no value when the text has fewer lines.
     */
    std::optional<std::size_t> lineStart(const std::string_view text, const std::size_t line)
    {
        std::size_t offset = 0;
        for (std::size_t current = 1; current < line; ++current) {
            const auto eol = text.find('\n', offset);
            if (eol == std::string_view::npos) {
                return std::nullopt;
            }
            offset = eol + 1;
        }
        return offset;
    }

    /**
     * The byte offset of a toml++ source position in the text. toml++ counts lines and columns from 1, a column in
     * code points and not in bytes, and the UTF-8 signature at the start of the text is not in the first line's.
     */
    std::size_t offsetOf(const std::string_view text, const toml::source_position position)
    {
        auto offset = lineStart(text, position.line).value_or(text.size());
        if (offset == 0 && text.starts_with(UTF8_BOM)) {
            offset = UTF8_BOM.size();
        }
        for (toml::source_index column = 1; column < position.column && offset < text.size(); ++column) {
            offset += codePointLength(text[offset]);
        }
        return std::min(offset, text.size());
    }

    /**
     * The text of a line counted from 1, without its line ending.
     */
    std::string_view lineText(const std::string_view text, const std::size_t line)
    {
        const auto start = lineStart(text, line);
        if (!start) {
            return {};
        }
        auto end = text.find('\n', *start);
        end = end == std::string_view::npos ? text.size() : end;
        if (end > *start && text[end - 1] == '\r') {
            --end;
        }
        return text.substr(*start, end - *start);
    }

    /**
     * Add a line after the line counted from 1, or as the first line for 0. After the last line, which may have no
     * line ending, the line ending is added before it.
     */
    std::string insertLineAfter(const std::string_view text, const std::size_t line, const std::string_view newLine)
    {
        const auto eol = lineEnding(text);
        const auto at = lineStart(text, line + 1);
        if (!at) {
            return std::format("{}{}{}{}", text, text.empty() ? "" : eol, newLine, eol);
        }
        return std::format("{}{}{}{}", text.substr(0, *at), newLine, eol, text.substr(*at));
    }

    /**
     * Whether a key can be written without quotes: letters, digits, '_' and '-' only.
     */
    bool isBareKey(const std::string_view key)
    {
        return !key.empty() && std::ranges::all_of(key, [](const char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
    }

    /**
     * A string as a TOML basic string, in double quotes with its quotes, backslashes and control characters escaped.
     */
    std::string basicString(const std::string_view value)
    {
        std::string text = "\"";
        for (const char c : value) {
            switch (c) {
            case '"':
                text += "\\\"";
                break;
            case '\\':
                text += "\\\\";
                break;
            case '\b':
                text += "\\b";
                break;
            case '\t':
                text += "\\t";
                break;
            case '\n':
                text += "\\n";
                break;
            case '\f':
                text += "\\f";
                break;
            case '\r':
                text += "\\r";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
                    text += std::format("\\u{:04X}", static_cast<unsigned char>(c));
                } else {
                    text += c;
                }
            }
        }
        return text + "\"";
    }

    /**
     * Whether a string can be written as a TOML literal string, in single quotes: no single quote, and no control
     * character other than a tab.
     */
    bool canBeLiteralString(const std::string_view value)
    {
        return std::ranges::none_of(value, [](const char c) {
            return c == '\'' || c == 0x7F || (static_cast<unsigned char>(c) < 0x20 && c != '\t');
        });
    }

    /**
     * A key path as TOML text: the keys joined with dots, each in quotes when it cannot be bare.
     */
    std::string keyText(const std::span<const std::string_view> path)
    {
        std::string text;
        for (const auto key : path) {
            text += text.empty() ? "" : ".";
            text += isBareKey(key) ? std::string(key) : basicString(key);
        }
        return text;
    }

    /**
     * A float as TOML text: the shortest text that reads back as the same value, with ".0" added to a whole number
     * so it stays a float. A float member is written from its own shortest text, 8.7 and not 8.6999998092651367.
     */
    template <typename T>
    std::string floatText(const T value)
    {
        auto text = std::format("{}", value);
        if (text.find_first_of(".eE") == std::string::npos && text.find("inf") == std::string::npos && text.find("nan") == std::string::npos) {
            text += ".0";
        }
        return text;
    }

    /**
     * A toml++ node as TOML text: an array and a table inline, one line each. Dates and times are written by toml++.
     */
    std::string nodeText(const toml::node& node)
    {
        std::string text;
        node.visit([&text]<typename T>(const T& value) {
            if constexpr (std::is_same_v<T, toml::table>) {
                for (const auto& [key, child] : value) {
                    const auto name = key.str();
                    text += std::format("{}{} = {}", text.empty() ? "{ " : ", ", keyText(std::span(&name, 1)), nodeText(child));
                }
                text += text.empty() ? "{}" : " }";
            } else if constexpr (std::is_same_v<T, toml::array>) {
                for (const auto& child : value) {
                    text += std::format("{}{}", text.empty() ? "[" : ", ", nodeText(child));
                }
                text += text.empty() ? "[]" : "]";
            } else if constexpr (std::is_same_v<T, toml::value<std::string>>) {
                text = basicString(value.get());
            } else if constexpr (std::is_same_v<T, toml::value<std::int64_t>>) {
                text = std::to_string(value.get());
            } else if constexpr (std::is_same_v<T, toml::value<double>>) {
                text = floatText(value.get());
            } else if constexpr (std::is_same_v<T, toml::value<bool>>) {
                text = value.get() ? "true" : "false";
            } else {
                std::ostringstream stream;
                stream << value;
                text = stream.str();
            }
        });
        return text;
    }

    /**
     * Whether an existing value already holds a value, compared by the text both are written as here: 71.000 holds
     * 71.0 and 0x10 holds 16, and floats are not compared with ==. An integer does not hold the float of its number.
     */
    bool holdsValue(const toml::node& existing, const toml::node& value)
    {
        return nodeText(existing) == nodeText(value);
    }

    /**
     * A toml++ parse error of the text as one line for a log: what is wrong, where, and the text of that line, so a
     * typo shows without opening the file.
     */
    std::string describe(const toml::parse_error& error, const std::string_view text, const std::string_view sourceName)
    {
        const auto& at = error.source().begin;
        auto line = lineText(text, at.line);
        if (at.line == 1 && line.starts_with(UTF8_BOM)) {
            line.remove_prefix(UTF8_BOM.size());
        }
        constexpr std::size_t longest = 120;
        const auto shown = line.size() > longest ? std::format("{}...", line.substr(0, longest)) : std::string(line);
        const auto source = sourceName.empty() ? std::string() : std::format(" of '{}'", sourceName);
        return std::format("{} at line {}, column {}{}: {}", error.description(), at.line, at.column, source, shown);
    }

    /**
     * The first line, counted from 1, where two texts differ.
     */
    std::size_t firstChangedLine(const std::string_view before, const std::string_view after)
    {
        const auto differs = std::ranges::mismatch(before, after).in1;
        return static_cast<std::size_t>(std::count(before.begin(), differs, '\n')) + 1;
    }

    /**
     * The document as toml++ writes it, to compare two documents by what they hold and not by how they are written.
     */
    std::string contentOf(const toml::table& table)
    {
        std::ostringstream stream;
        stream << table;
        return stream.str();
    }

    /**
     * Set the value at a path in a document, adding the tables on the way that it does not have.
     */
    void assign(toml::table& table, const std::span<const std::string_view> path, const toml::node& value)
    {
        auto* current = &table;
        for (const auto key : path.first(path.size() - 1)) {
            auto* child = current->get(key);
            if (!child) {
                child = &current->insert_or_assign(key, toml::table{}).first->second;
            }
            current = child->as_table();
        }
        value.visit([&](const auto& v) {
            current->insert_or_assign(path.back(), v);
        });
    }

    /**
     * The number of keys in the [header] the text starts with, or 0 when it starts with none. A dot in a quoted key
     * is not a separator.
     */
    std::size_t headerKeyCount(const std::string_view text)
    {
        if (!text.starts_with('[') || text.starts_with("[[")) {
            return 0;
        }
        std::size_t keys = 1;
        char quote = 0;
        for (std::size_t i = 1; i < text.size(); ++i) {
            const char c = text[i];
            if (quote != 0) {
                if (quote == '"' && c == '\\') {
                    ++i;
                } else if (c == quote) {
                    quote = 0;
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '.') {
                ++keys;
            } else if (c == ']' || c == '\n') {
                return c == ']' ? keys : 0;
            }
        }
        return 0;
    }

    /**
     * Whether a table `depth` keys below the root is a section of the text of its own: the root, or a table with a
     * [header] of its own. A table of dotted keys and an inline table are not, and neither is a table only made by
     * the header of a table under it, which toml++ gives that header's position: the header has more keys.
     */
    bool isSection(const std::string_view text, const toml::table& root, const toml::table& table, const std::size_t depth)
    {
        if (&table == &root) {
            return true;
        }
        if (table.is_inline() || table.source().begin.line == 0) {
            return false;
        }
        return headerKeyCount(text.substr(offsetOf(text, table.source().begin))) == depth;
    }

    /**
     * Whether a node is an array of tables with headers ([[name]]), whose tables are sections and not values.
     */
    bool isArrayOfSections(const toml::node& node)
    {
        const auto* array = node.as_array();
        return array && array->is_array_of_tables() && !array->empty() && !array->front().as_table()->is_inline();
    }

    /**
     * The last line of a section `depth` keys below the root that holds one of its values, its tables of dotted keys
     * included, or 0 when it has none. The tables under it with a header of their own are sections of their own and
     * not counted.
     */
    toml::source_index lastValueLine(const std::string_view text, const toml::table& root, const toml::table& section, const std::size_t depth)
    {
        toml::source_index last = 0;
        for (const auto& [key, child] : section) {
            if (const auto* table = child.as_table(); table && !table->is_inline()) {
                if (!isSection(text, root, *table, depth + 1)) {
                    last = std::max(last, lastValueLine(text, root, *table, depth + 1));
                }
            } else if (!isArrayOfSections(child)) {
                last = std::max(last, child.source().end.line);
            }
        }
        return last;
    }

    /**
     * The line of the first [header] under a table `depth` keys below the root, or 0 when it has none.
     */
    toml::source_index firstSectionLine(const std::string_view text, const toml::table& root, const toml::table& table, const std::size_t depth)
    {
        toml::source_index first = 0;
        const auto earlier = [&first](const toml::source_index line) {
            if (line != 0 && (first == 0 || line < first)) {
                first = line;
            }
        };
        for (const auto& [key, child] : table) {
            if (const auto* sub = child.as_table(); sub && !sub->is_inline()) {
                if (isSection(text, root, *sub, depth + 1)) {
                    earlier(sub->source().begin.line);
                }
                earlier(firstSectionLine(text, root, *sub, depth + 1));
            } else if (isArrayOfSections(child)) {
                earlier(child.as_array()->front().source().begin.line);
            }
        }
        return first;
    }

    /**
     * The path as text for a message: the keys joined with dots.
     */
    std::string pathText(const std::span<const std::string_view> path)
    {
        return keyText(path);
    }

    /**
     * The value as toml++ reads it back, as the key "v" of a table, which the document has to hold after the edit.
     * No value when the text is not a TOML value, such as an integer TOML can't hold.
     */
    std::optional<toml::table> parseValue(const f4cf::common::TomlEditor::Value& value)
    {
        try {
            return toml::parse("v = " + value.text());
        } catch (const toml::parse_error&) {
            return std::nullopt;
        }
    }

    /**
     * The nodes along a path, from the root to the last one the document has: one more than the path when it has
     * the whole path.
     */
    std::vector<const toml::node*> chainOf(const toml::table& root, const std::span<const std::string_view> path)
    {
        std::vector<const toml::node*> chain{ &root };
        for (const auto key : path) {
            const auto* table = chain.back()->as_table();
            const auto* child = table ? table->get(key) : nullptr;
            if (!child) {
                break;
            }
            chain.push_back(child);
        }
        return chain;
    }

    /**
     * The bytes of the text that a new value replaces, and the text it is written as.
     */
    struct Splice
    {
        std::size_t begin = 0;
        std::size_t end = 0;
        std::string text;
    };

    /**
     * The splice that writes a value in place of an existing one, where toml++ read it. A string stays a literal
     * string where the old value was one and the new string can be one.
     */
    std::expected<Splice, std::string> spliceFor(const std::string_view text, const toml::node& existing, const f4cf::common::TomlEditor::Value& value)
    {
        if (const auto* table = existing.as_table(); table && !table->is_inline()) {
            return std::unexpected("is a table, not a value");
        }
        if (isArrayOfSections(existing)) {
            return std::unexpected("is an array of tables, not a value");
        }
        const auto& source = existing.source();
        if (source.begin.line == 0) {
            return std::unexpected("has no position in the text");
        }

        Splice splice{ offsetOf(text, source.begin), offsetOf(text, source.end), value.text() };
        const auto old = text.substr(splice.begin, splice.end - splice.begin);
        if (value.string() && old.starts_with('\'') && !old.starts_with("'''") && canBeLiteralString(*value.string())) {
            splice.text = std::format("'{}'", *value.string());
        }
        return splice;
    }

    /**
     * The edited text parsed, when it holds what `expected` holds: the document before the edit with the new values
     * set. Otherwise why not, with the line of the text the edit changed first.
     */
    std::expected<toml::table, std::string> verify(const std::string_view before, const std::string_view edited, const toml::table& expected, const std::string_view sourceName)
    {
        toml::table result;
        try {
            result = toml::parse(edited, sourceName);
        } catch (const toml::parse_error& error) {
            return std::unexpected(std::format("the edited text does not parse: {}", describe(error, edited, sourceName)));
        }
        if (contentOf(result) != contentOf(expected)) {
            return std::unexpected(std::format("the edit at line {} would change more than the value", firstChangedLine(before, edited)));
        }
        return result;
    }
}

namespace f4cf::common
{
    TomlEditor::Value::Value(const bool value)
        : _text(value ? "true" : "false")
    {}

    TomlEditor::Value::Value(const float value)
        : _text(floatText(value))
    {}

    TomlEditor::Value::Value(const double value)
        : _text(floatText(value))
    {}

    TomlEditor::Value::Value(const char* value)
        : Value(std::string_view(value))
    {}

    TomlEditor::Value::Value(const std::string_view value)
        : _text(basicString(value)),
          _string(std::string(value))
    {}

    TomlEditor::Value::Value(const std::string& value)
        : Value(std::string_view(value))
    {}

    TomlEditor::Value::Value(const toml::node& value)
        : _text(nodeText(value))
    {
        if (const auto* string = value.as_string()) {
            _string = string->get();
        }
    }

    /**
     * Parse the text to edit. `sourceName` names it in the parse error, see error().
     */
    TomlEditor::TomlEditor(std::string text, const std::string_view sourceName)
        : _text(std::move(text)),
          _sourceName(sourceName)
    {
        F4CF_PERF_FUNCTION();
        try {
            _table = toml::parse(_text, _sourceName);
            _parsed = true;
        } catch (const toml::parse_error& error) {
            _error = describe(error, _text, _sourceName);
        }
    }

    /**
     * Set the value at a path of keys, such as {"Section", "key"} or {"Section", "table", "key"}. A value the text has
     * is replaced where it is written, and nothing else in the text changes; one that already holds the value stays as
     * it is written, so a save of unchanged values changes nothing. A key it does not have is added as a
     * line after the last value of its section, as a dotted key when its table is not a section of its own, and a
     * section it does not have is added at the end with a [header].
     * The edit is parsed back, and kept only when the document then holds the value and is otherwise the same.
     * Returns false and leaves the text as it was when it is not: for a path that is a table or goes through a value
     * that is not a table, and for a text that did not parse. error() then says why.
     */
    bool TomlEditor::set(const std::span<const std::string_view> path, const Value& value)
    {
        F4CF_PERF_FUNCTION();
        if (!_parsed) {
            return false;
        }
        _error.clear();
        if (path.empty()) {
            _error = "no key to set";
            return false;
        }

        const auto parsedValue = parseValue(value);
        if (!parsedValue) {
            _error = std::format("{}: '{}' is not a TOML value", pathText(path), value.text());
            return false;
        }

        std::expected<std::string, std::string> edited;
        if (const auto chain = chainOf(_table, path); chain.size() == path.size() + 1) {
            const auto splice = spliceFor(_text, *chain.back(), value);
            if (splice && holdsValue(*chain.back(), *parsedValue->get("v"))) {
                // already holds it: the text stays as it is written
                return true;
            }
            edited = splice.transform([this](const Splice& made) {
                return std::format("{}{}{}", std::string_view(_text).substr(0, made.begin), made.text, std::string_view(_text).substr(made.end));
            });
        } else {
            edited = insert(path, chain, value);
        }
        if (!edited) {
            _error = std::format("{}: {}", pathText(path), edited.error());
            return false;
        }

        auto expected = _table;
        assign(expected, path, *parsedValue->get("v"));
        auto result = verify(_text, *edited, expected, _sourceName);
        if (!result) {
            _error = std::format("{}: {}", pathText(path), result.error());
            return false;
        }

        _text = std::move(*edited);
        _table = std::move(*result);
        return true;
    }

    /**
     * Set several values, with the same result as a set of each in their order, see set. The values the text has are
     * replaced in one pass and parsed back once, which a set of each does once per value: a mod's whole config saved
     * at once is a fraction of the time. The keys the text does not have are then added one at a time. When the one
     * pass does not parse back as it should, every edit is made one at a time, which tells the one that can't be.
     * Returns false when any edit failed: the others are made, and error() names each that failed and why.
     */
    bool TomlEditor::set(const std::span<const Edit> edits)
    {
        F4CF_PERF_SCOPE("TomlEditor::set(edits)");
        if (!_parsed) {
            return false;
        }
        _error.clear();

        // the edits of values the text has, each a splice of the text as it is now, and the expected document
        std::vector<Splice> splices;
        std::vector<const Edit*> oneAtATime;
        auto expected = _table;
        for (const auto& edit : edits) {
            const auto parsedValue = edit.path.empty() ? std::nullopt : parseValue(edit.value);
            const auto chain = parsedValue ? chainOf(_table, edit.path) : std::vector<const toml::node*>{};
            std::expected<Splice, std::string> splice = std::unexpected(std::string());
            if (chain.size() == edit.path.size() + 1) {
                splice = spliceFor(_text, *chain.back(), edit.value);
            }
            if (!splice) {
                oneAtATime.push_back(&edit);
                continue;
            }
            // a later edit of the same value is the one that applies, and one back to what the text holds undoes it
            std::erase_if(splices, [&](const Splice& earlier) {
                return earlier.begin == splice->begin;
            });
            if (!holdsValue(*chain.back(), *parsedValue->get("v"))) {
                splices.push_back(std::move(*splice));
            }
            assign(expected, edit.path, *parsedValue->get("v"));
        }

        // none when every value already holds what is set: then there is nothing to parse back
        if (!splices.empty()) {
            // from the end of the text to its start, so each splice is still where the parse put it
            std::ranges::sort(splices, std::greater{}, &Splice::begin);
            const bool overlap = std::ranges::adjacent_find(splices, [](const Splice& later, const Splice& earlier) {
                return earlier.end > later.begin;
            }) != splices.end();
            auto edited = _text;
            for (const auto& splice : splices) {
                edited.replace(splice.begin, splice.end - splice.begin, splice.text);
            }
            std::expected<toml::table, std::string> result = std::unexpected(std::string());
            if (!overlap) {
                result = verify(_text, edited, expected, _sourceName);
            }
            if (result) {
                _text = std::move(edited);
                _table = std::move(*result);
            } else {
                oneAtATime.clear();
                for (const auto& edit : edits) {
                    oneAtATime.push_back(&edit);
                }
            }
        }

        std::string errors;
        for (const auto* edit : oneAtATime) {
            if (!set(edit->path, edit->value)) {
                errors += std::format("{}{}", errors.empty() ? "" : "; ", _error);
            }
        }
        _error = std::move(errors);
        return _error.empty();
    }

    /**
     * The text with a key it does not have added, `chain` being the nodes the document has along the path from the
     * root. In an inline table it goes before the closing brace. Otherwise it goes in the section its table is in,
     * as a line after the section's last value. A section the document does not have is added at the end.
     */
    std::expected<std::string, std::string> TomlEditor::insert(const std::span<const std::string_view> path, const std::span<const toml::node* const> chain,
        const Value& value) const
    {
        const auto depth = chain.size() - 1;
        if (isArrayOfSections(*chain.back())) {
            return std::unexpected(std::format("{} is an array of tables", pathText(path.first(depth))));
        }
        const auto* parent = chain.back()->as_table();
        if (!parent) {
            return std::unexpected(std::format("{} is not a table", pathText(path.first(depth))));
        }

        // in an inline table: before its closing brace
        if (parent->is_inline()) {
            const auto begin = offsetOf(_text, parent->source().begin);
            auto close = offsetOf(_text, parent->source().end);
            while (close > begin && _text[close - 1] != '}') {
                --close;
            }
            if (close == begin) {
                return std::unexpected("has no closing brace where toml++ read its table");
            }
            --close;
            auto last = close;
            while (last > begin && (_text[last - 1] == ' ' || _text[last - 1] == '\t')) {
                --last;
            }
            const auto entry = std::format("{} = {}", keyText(path.subspan(depth)), value.text());
            if (_text[last - 1] == '{') {
                return std::format("{}{{ {} }}{}", std::string_view(_text).substr(0, last - 1), entry, std::string_view(_text).substr(close + 1));
            }
            return std::format("{}, {}{}", std::string_view(_text).substr(0, last), entry, std::string_view(_text).substr(last));
        }

        // the section the table is in: the table itself, or the one above a table of dotted keys
        auto sectionDepth = depth;
        while (sectionDepth > 0 && !isSection(_text, _table, *chain[sectionDepth]->as_table(), sectionDepth)) {
            --sectionDepth;
        }
        const auto& section = *chain[sectionDepth]->as_table();

        // a section the document does not have: a [header] at the end. Not for a table of dotted keys of the root,
        // which a header would define a second time: the key is then one more dotted key of the root.
        const bool dottedOfRoot = depth > 0 && lastValueLine(_text, _table, *chain[1]->as_table(), 1) != 0;
        if (sectionDepth == 0 && path.size() > 1 && !dottedOfRoot) {
            const auto eol = lineEnding(_text);
            auto text = _text;
            if (!text.empty() && !text.ends_with('\n')) {
                text += eol;
            }
            if (!text.empty() && !text.ends_with(std::format("{}{}", eol, eol)) && text != eol) {
                text += eol;
            }
            return std::format("{}[{}]{}{} = {}{}", text, keyText(path.first(path.size() - 1)), eol, keyText(path.last(1)), value.text(), eol);
        }

        const auto line = std::format("{} = {}", keyText(path.subspan(sectionDepth)), value.text());
        if (const auto last = lastValueLine(_text, _table, section, sectionDepth); last != 0) {
            return insertLineAfter(_text, last, line);
        }
        if (sectionDepth != 0) {
            // a section with no values: right after its header
            return insertLineAfter(_text, section.source().end.line, line);
        }

        // the root with no values: before the first [header], above the comment lines right over it
        std::size_t before = firstSectionLine(_text, _table, _table, 0);
        if (before == 0) {
            const auto lines = static_cast<std::size_t>(std::ranges::count(_text, '\n')) + (_text.ends_with('\n') ? 0 : 1);
            return insertLineAfter(_text, lines, line);
        }
        const auto isComment = [this](const std::size_t number) {
            const auto text = lineText(_text, number);
            const auto first = text.find_first_not_of(" \t");
            return first != std::string_view::npos && text[first] == '#';
        };
        for (--before; before > 0 && isComment(before); --before) {
        }
        return insertLineAfter(_text, before, line);
    }
}
