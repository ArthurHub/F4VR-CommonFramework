#include "ConfigMigration.h"

#include <string_view>

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
            const bool newIsSection = newNode->is_table() && !newNode->as_table()->is_inline();
            if (!sameType || newIsSection || isArrayOfSections(*oldNode) || isArrayOfSections(*newNode)) {
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
}
