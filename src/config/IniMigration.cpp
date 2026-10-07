#include "IniMigration.h"

#include <cstring>
#include <list>

namespace f4cf::config
{
    /**
     * Set the values of the user's INI in the INI of a newer version, which holds that version's defaults.
     * A value is set only for a key that the new INI has, so the new INI keeps its own keys, comments and order,
     * and a key that the newer version no longer has is dropped.
     * Every key is a value to carry, the one of the INI's version too: the caller sets the new version after.
     * Returns every value of the user's INI with what was done with it.
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
