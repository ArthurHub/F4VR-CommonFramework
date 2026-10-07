#include "IniMigration.h"

#include <cstring>
#include <list>

namespace f4cf::config
{
    /**
     * Set the values of a user's INI in the INI of a newer version, and return each with what was done with it.
     * A value is set only for a key the new INI has, so it keeps its own keys, comments and order, and a key it
     * no longer has is dropped. The INI's version is carried as any value: the caller sets the new one after.
     */
    std::vector<MigratedValue> migrateIniValues(const CSimpleIniA& oldIni, CSimpleIniA& newIni)
    {
        std::vector<MigratedValue> values;

        std::list<CSimpleIniA::Entry> sections;
        oldIni.GetAllSections(sections);
        for (const auto& section : sections) {
            std::list<CSimpleIniA::Entry> keys;
            oldIni.GetAllKeys(section.pItem, keys);
            for (const auto& key : keys) {
                const auto* oldValue = oldIni.GetValue(section.pItem, key.pItem);
                const auto* newValue = newIni.GetValue(section.pItem, key.pItem);

                auto outcome = MigratedValue::Outcome::Removed;
                if (newValue != nullptr) {
                    outcome = std::strcmp(oldValue, newValue) != 0 ? MigratedValue::Outcome::Carried : MigratedValue::Outcome::Unchanged;
                }
                if (outcome == MigratedValue::Outcome::Carried) {
                    newIni.SetValue(section.pItem, key.pItem, oldValue);
                }
                values.push_back({ section.pItem, key.pItem, oldValue, outcome });
            }
        }
        return values;
    }
}
