#include "ConfigMigration.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "ConfigFile.h"
#include "ConfigValue.h"
#include "ValueParsers.h"

// last: on Windows it includes windows.h, whose min and max macros break headers that come after it
#include <SimpleIni.h>

namespace
{
    using f4cf::common::TomlEditor;
    using f4cf::config::MigratedValue;

    /**
     * Whether a node is an array of tables with headers ([[name]]), which the editor can't set.
     */
    bool isArrayOfSections(const toml::node& node)
    {
        const auto* array = node.as_array();
        return array && array->is_array_of_tables() && !array->empty() && !array->front().as_table()->is_inline();
    }

    /**
     * Whether a node is a table that is not one value: a section, or a table of dotted keys.
     */
    bool isSection(const toml::node& node)
    {
        return node.is_table() && !node.as_table()->is_inline();
    }

    /**
     * The node at a path of keys, or null when the table does not have it.
     */
    const toml::node* nodeAt(const toml::table& table, const std::vector<std::string>& path)
    {
        const toml::node* node = &table;
        for (const auto& key : path) {
            const auto* sub = node->as_table();
            node = sub ? sub->get(key) : nullptr;
            if (!node) {
                return nullptr;
            }
        }
        return node;
    }

    /**
     * Add every value of a table to `values`, each with its path: the values of its sections and of its tables of
     * dotted keys, and an array or an inline table as one value.
     */
    void collectValues(const toml::table& table, std::vector<std::string>& path, std::vector<MigratedValue>& values)
    {
        for (const auto& [key, node] : table) {
            path.emplace_back(key.str());
            if (const auto* sub = node.as_table(); sub && !sub->is_inline()) {
                collectValues(*sub, path, values);
            } else {
                values.push_back({ path, isArrayOfSections(node) ? std::string() : TomlEditor::Value(node).text() });
            }
            path.pop_back();
        }
    }

    /**
     * The key of a table that matches `key` with no regard to case, as SimpleIni matches an INI's names, and its node.
     * An exact match comes first. With no match, or no table, it is `key` itself and null.
     */
    std::pair<std::string_view, const toml::node*> findNoCase(const toml::table* table, const std::string_view key)
    {
        if (!table) {
            return { key, nullptr };
        }
        if (const auto* node = table->get(key)) {
            return { key, node };
        }
        const auto sameLetter = [](const char a, const char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        };
        for (const auto& [name, node] : *table) {
            if (std::ranges::equal(name.str(), key, sameLetter)) {
                return { name.str(), &node };
            }
        }
        return { key, nullptr };
    }

    /**
     * The value of an INI key in the type of `like`, the TOML config's value for it, read by SimpleIni's getter of
     * that type, so it is the value a mod read from the INI: "yes" and "on" are true, "0x10" is 16, and a number with
     * text after it is not one. No value when the text is not a value of the type, for which a getter returns its
     * default, so a bool and an integer are read with two defaults that differ, and a float with NaN, as floats are
     * not compared exactly. A transform, a table the config reads as one (see config::parseTransform), is the INI's
     * text of it, "x,y,z;heading,roll,attitude;scale", as the table, see config::transformTableFromText. Any other
     * type the INI has no getter of is the text read as TOML, see config::typedLike.
     */
    std::optional<TomlEditor::Value> iniValueAs(const CSimpleIniA& ini, const char* section, const char* key, const toml::node& like)
    {
        switch (like.type()) {
        case toml::node_type::boolean:
            if (const auto value = ini.GetBoolValue(section, key, false); value == ini.GetBoolValue(section, key, true)) {
                return TomlEditor::Value(value);
            }
            return std::nullopt;
        case toml::node_type::integer:
            if (const auto value = ini.GetLongValue(section, key, 0); value == ini.GetLongValue(section, key, 1)) {
                return TomlEditor::Value(value);
            }
            return std::nullopt;
        case toml::node_type::floating_point:
            if (const auto value = ini.GetDoubleValue(section, key, std::numeric_limits<double>::quiet_NaN()); !std::isnan(value)) {
                return TomlEditor::Value(value);
            }
            return std::nullopt;
        case toml::node_type::string:
            return TomlEditor::Value(ini.GetValue(section, key, ""));
        case toml::node_type::table:
            if (f4cf::config::parseTransform(like)) {
                if (const auto table = f4cf::config::transformTableFromText(ini.GetValue(section, key, ""))) {
                    return TomlEditor::Value(*table);
                }
            }
            [[fallthrough]];
        default:
            if (const auto typed = f4cf::config::typedLike(&like, ini.GetValue(section, key, ""))) {
                return TomlEditor::Value(*typed->get("v"));
            }
            return std::nullopt;
        }
    }
}

namespace f4cf::config
{
    /**
     * The path as text for a log: the keys joined with dots.
     */
    std::string MigratedValue::pathText() const
    {
        std::string text;
        for (const auto& key : path) {
            text += (text.empty() ? "" : ".") + key;
        }
        return text;
    }

    /**
     * Set the values of a user's config in the config of a newer version, `latest` the text of its default, and return
     * each with what was done with it. A value is set only for a key the new config has with a value of the same
     * type, or an integer for a float, so the new config keeps its own keys, comments and order, a key it no longer
     * has is dropped, and a value whose type changed is left to the mod's own migration. The editor keeps how a value
     * the user did not change is written. The config's version is carried as any value: the caller sets the new one
     * after.
     */
    std::vector<MigratedValue> migrateValues(const toml::table& old, common::TomlEditor& latest)
    {
        std::vector<MigratedValue> values;
        std::vector<std::string> path;
        collectValues(old, path, values);

        std::vector<common::TomlEditor::Edit> edits;
        std::vector<MigratedValue*> carried;
        for (auto& entry : values) {
            const auto* oldNode = nodeAt(old, entry.path);
            const auto* newNode = nodeAt(latest.table(), entry.path);
            if (!newNode) {
                entry.outcome = MigratedValue::Outcome::Removed;
                continue;
            }
            const bool sameType = newNode->type() == oldNode->type() || (newNode->is_floating_point() && oldNode->is_integer());
            if (!sameType || isSection(*newNode) || isArrayOfSections(*oldNode) || isArrayOfSections(*newNode)) {
                entry.outcome = MigratedValue::Outcome::NotCarried;
                continue;
            }
            const auto value = oldNode->is_integer() && newNode->is_floating_point() ? common::TomlEditor::Value(static_cast<double>(oldNode->as_integer()->get()))
                                                                                     : common::TomlEditor::Value(*oldNode);
            if (value.text() == common::TomlEditor::Value(*newNode).text()) {
                entry.outcome = MigratedValue::Outcome::Unchanged;
                continue;
            }
            entry.outcome = MigratedValue::Outcome::Carried;
            edits.push_back({ { entry.path.begin(), entry.path.end() }, value });
            carried.push_back(&entry);
        }

        // an edit the editor refuses is a value the new config does not get
        if (!latest.set(edits)) {
            for (std::size_t i = 0; i < carried.size(); i++) {
                const auto* now = nodeAt(latest.table(), carried[i]->path);
                if (!now || common::TomlEditor::Value(*now).text() != edits[i].value.text()) {
                    carried[i]->outcome = MigratedValue::Outcome::NotCarried;
                }
            }
        }
        return values;
    }

    /**
     * A mod's INI config as TOML text, for the migration of a config from before the mod moved it to TOML, see
     * migrateValues. An INI value has no type, so a key that `like` has, the TOML config the mod ships, takes the type
     * `like` has for it, read as the mod read it from the INI, see iniValueAs. Every other value is a string, for the
     * mod's own migration to read: a key `like` does not have, and a text that is not a value of the type, for which
     * the mod had its default. An empty text of either is left out, as for no key: it holds nothing for the mod's
     * migration, and the load of the version 0 file does not read it as a broken value of the key.
     * The INI's names are matched with no regard to case, as SimpleIni matched them, and take `like`'s spelling. The
     * sections and keys are in the INI's order, a key before its first section is a key of the root, and its comments
     * are left out. Returns why when the INI can't be TOML, as when it has a key of the root and a section of the
     * same name.
     */
    std::expected<std::string, std::string> iniToToml(const std::string_view iniText, const toml::table& like)
    {
        CSimpleIniA ini;
        if (ini.LoadData(iniText.data(), iniText.size()) < 0) {
            return std::unexpected(std::string("SimpleIni failed to read it"));
        }

        // the names are views of the INI's and of like's, which outlive the set
        std::vector<common::TomlEditor::Edit> edits;
        CSimpleIniA::TNamesDepend sections;
        ini.GetAllSections(sections);
        sections.sort(CSimpleIniA::Entry::LoadOrder());
        for (const auto& section : sections) {
            std::vector<std::string_view> sectionPath;
            const toml::table* table = &like;
            if (*section.pItem) {
                const auto [name, node] = findNoCase(&like, section.pItem);
                table = node && isSection(*node) ? node->as_table() : nullptr;
                sectionPath.push_back(table ? name : section.pItem);
            }

            CSimpleIniA::TNamesDepend keys;
            ini.GetAllKeys(section.pItem, keys);
            keys.sort(CSimpleIniA::Entry::LoadOrder());
            for (const auto& key : keys) {
                const auto [name, node] = findNoCase(table, key.pItem);
                std::optional<TomlEditor::Value> value;
                if (node && !isSection(*node) && !isArrayOfSections(*node)) {
                    value = iniValueAs(ini, section.pItem, key.pItem, *node);
                }
                const auto* text = ini.GetValue(section.pItem, key.pItem, "");
                if (!value && !*text) {
                    continue;
                }
                auto path = sectionPath;
                path.push_back(name);
                edits.push_back({ std::move(path), value ? std::move(*value) : TomlEditor::Value(text) });
            }
        }

        TomlEditor toml("");
        if (!toml.set(edits)) {
            return std::unexpected(toml.error());
        }
        return toml.text();
    }

    /**
     * Write a mod's INI config as its TOML config file at version 0, see iniToToml, for the update from version 0 to
     * take it to the version of `like`, the TOML config the mod ships. The file starts with a comment that says what
     * it is, as the update keeps it as a backup. The INI is moved to its backup, with its own version in the name, see
     * backupPathOf. Returns why when the INI can't be read or be TOML, or the file can't be written: the INI is then
     * left as it is and there is no file. An INI that can't be moved to its backup is not a failure, and the result
     * says why.
     */
    std::expected<ImportedIni, std::string> importIniFile(const std::filesystem::path& iniPath, const std::filesystem::path& tomlPath, const toml::table& like)
    {
        const auto iniName = iniPath.filename().string();
        const auto iniText = ConfigFile::readFile(iniPath.string());
        if (!iniText) {
            return std::unexpected(std::format("failed to read '{}'", iniPath.string()));
        }
        const auto toml = iniToToml(*iniText, like);
        if (!toml) {
            return std::unexpected(std::format("{} can't be TOML: {}", iniName, toml.error()));
        }

        // the INI's version counts the INI's changes and names its backup: the TOML config counts from 0
        TomlEditor imported(std::format("# {} as TOML, which the config is updated from\n{}", iniName, *toml), tomlPath.filename().string());
        ImportedIni result;
        result.version = imported.table()[VERSION_SECTION][VERSION_KEY].value_or(0);
        if (!imported.set({ VERSION_SECTION, VERSION_KEY }, 0)) {
            return std::unexpected(imported.error());
        }
        if (!ConfigFile::writeFile(tomlPath.string(), imported.text())) {
            std::error_code ignored;
            std::filesystem::remove(tomlPath, ignored);
            return std::unexpected(std::format("failed to write '{}'", tomlPath.string()));
        }

        result.backup = backupPathOf(iniPath, result.version);
        std::error_code error;
        std::filesystem::rename(iniPath, result.backup, error);
        if (error) {
            result.backupError = error.message();
        }
        return result;
    }

    /**
     * Where a config file of a version is kept when a newer one takes its place: "{name}_backup_v{version}{ext}"
     * beside it.
     */
    std::filesystem::path backupPathOf(const std::filesystem::path& path, const int version)
    {
        return path.parent_path() / std::format("{}_backup_v{}{}", path.stem().string(), version, path.extension().string());
    }
}
