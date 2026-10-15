#include "DebugInventory.h"

#include <format>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Logger.h"
#include "common/CommonUtils.h"
#include "common/TomlEditor.h"

namespace
{
    using f4vr::DebugInventory;

    // Ammo and throwables (grenades/mines) are handed out in small stacks; everything else as a single copy.
    constexpr std::uint32_t AMMO_COUNT = 50;
    constexpr std::uint32_t THROWABLE_COUNT = 5;
    constexpr std::uint32_t ITEM_COUNT = 1;

    /**
     * Case-insensitive "full name contains needle". An empty needle always matches.
     * needleLower is expected pre-lowercased by the caller.
     */
    bool nameContains(const char* fullName, const std::string& needleLower)
    {
        return needleLower.empty() || f4cf::common::str_tolower(fullName).find(needleLower) != std::string::npos;
    }

    /**
     * Log one form's useful identifying data for the print (dry-run) mode: FormID, editor ID (when present),
     * full name, the category-specific `extra` (e.g. armor slots), and the source plugin it comes from.
     */
    void printFormInfo(const RE::TESForm* form, const char* name, const std::string& extra)
    {
        const char* editorId = form->GetFormEditorID();
        const auto* file = form->GetFile(0);
        const auto source = file ? file->GetFilename() : std::string_view{ "?" };
        logger::infoRaw("  {:08X}{} '{}'{}  [{}]", form->GetFormID(), editorId && editorId[0] ? std::format(" {}", editorId) : "", name, extra, source);
    }

    /**
     * True if any of the form's keywords has an editor ID containing `needleLower` (case-insensitive substring).
     * Keyword editor IDs are retained at runtime (BGSKeyword::formEditorID), so this is reliable for base game and mods.
     */
    bool hasKeywordContaining(const RE::BGSKeywordForm* kwForm, const std::string& needleLower)
    {
        bool found = false;
        kwForm->ForEachKeyword([&](const RE::BGSKeyword* kw) {
            const char* edid = kw ? kw->formEditorID.c_str() : nullptr;
            if (edid && f4cf::common::str_tolower(edid).find(needleLower) != std::string::npos) {
                found = true;
                return RE::BSContainer::ForEachResult::kStop;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return found;
    }

    /**
     * Comma-joined list of the form's keyword editor IDs for the print mode, e.g. " kw:WeaponTypeLaser,...".
     * Empty when the form has no named keywords.
     */
    std::string keywordList(const RE::BGSKeywordForm* kwForm)
    {
        std::string out;
        kwForm->ForEachKeyword([&](const RE::BGSKeyword* kw) {
            const char* edid = kw ? kw->formEditorID.c_str() : nullptr;
            if (edid && edid[0]) {
                out += out.empty() ? " kw:" : ",";
                out += edid;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return out;
    }

    /**
     * Recursively collect every leaf form reachable from a leveled list, following nested leveled lists
     * (cycle-guarded by `visited`). Non-leveled entries are recorded in `out` by FormID.
     */
    void collectReachable(const RE::TESForm* form, std::unordered_set<RE::TESFormID>& out, std::unordered_set<RE::TESFormID>& visited)
    {
        if (!form) {
            return;
        }
        if (!form->Is(RE::ENUM_FORM_ID::kLVLI)) {
            out.insert(form->formID);
            return;
        }
        if (!visited.insert(form->formID).second) {
            return;
        }
        const auto* list = static_cast<const RE::TESLevItem*>(form);
        if (list->leveledLists) {
            for (std::int32_t i = 0; i < list->baseListCount; ++i) {
                collectReachable(list->leveledLists[i].form, out, visited);
            }
        }
    }

    /**
     * Build the set of FormIDs the player can actually get: items reachable from any leveled list
     * (vendor/loot/container drops) plus items produced by a crafting recipe (BGSConstructibleObject).
     * Crafted products are run through the same walker so a recipe yielding a leveled list still recurses.
     * Used as the "obtainable in normal play" proxy filter.
     */
    std::unordered_set<RE::TESFormID> collectObtainable()
    {
        std::unordered_set<RE::TESFormID> out;
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return out;
        }
        std::unordered_set<RE::TESFormID> visited;
        for (RE::TESLevItem* list : dataHandler->GetFormArray<RE::TESLevItem>()) {
            collectReachable(list, out, visited);
        }
        for (RE::BGSConstructibleObject* recipe : dataHandler->GetFormArray<RE::BGSConstructibleObject>()) {
            if (recipe) {
                collectReachable(recipe->GetCreatedItem(), out, visited);
            }
        }
        return out;
    }

    /**
     * Walk every form of type T (must derive from BGSKeywordForm so name/keyword filtering and the print-mode
     * keyword listing work): named (non-empty display name, which drops the unnamed templates / test forms /
     * leveled-list helpers that flood the form arrays), optionally name-filtered and keyword-filtered, optionally
     * restricted to leveled-list-reachable FormIDs (`reachable`, nullptr = no restriction), passing the optional
     * caller `predicate` (an arbitrary constraint on the base form, ItemFilter::predicate) and the optional typed
     * `extraFilter` (the category's own slot/class/playable gates). Each match is added to the player's inventory,
     * or, when `printOnly`, logged instead (with `describe` plus the form's keywords). Returns the match count.
     */
    template <class T>
    std::size_t addAllForms(RE::PlayerCharacter* player, const std::uint32_t count, const std::string& needleLower, const std::string& keywordLower,
        const std::unordered_set<RE::TESFormID>* reachable, const bool printOnly, const std::function<bool(const RE::TESForm*)>& predicate = {},
        const std::function<bool(const T*)>& extraFilter = {}, const std::function<std::string(const T*)>& describe = {})
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return 0;
        }
        std::size_t matched = 0;
        for (T* form : dataHandler->GetFormArray<T>()) {
            if (!form) {
                continue;
            }
            const char* name = form->GetFullName();
            if (!name || name[0] == '\0') {
                continue;
            }
            if (reachable && !reachable->contains(form->formID)) {
                continue;
            }
            if (predicate && !predicate(form)) {
                continue;
            }
            if (extraFilter && !extraFilter(form)) {
                continue;
            }
            if (!nameContains(name, needleLower)) {
                continue;
            }
            const auto* keywordForm = static_cast<const RE::BGSKeywordForm*>(form);
            if (!keywordLower.empty() && !hasKeywordContaining(keywordForm, keywordLower)) {
                continue;
            }
            if (printOnly) {
                printFormInfo(form, name, (describe ? describe(form) : std::string{}) + keywordList(keywordForm));
            } else {
                player->AddInventoryItem(form, {}, count, nullptr, nullptr, nullptr);
            }
            ++matched;
        }
        return matched;
    }

    /**
     * Map a category keyword (case-insensitive) to the enum. Returns nullopt for unknown keywords.
     */
    std::optional<DebugInventory::ItemCategory> parseCategory(std::string name)
    {
        name = f4cf::common::str_tolower(std::move(name));
        if (name == "weapons" || name == "weapon" || name == "weap") {
            return DebugInventory::ItemCategory::Weapons;
        }
        if (name == "throwables" || name == "throwable" || name == "grenades" || name == "grenade" || name == "thrown") {
            return DebugInventory::ItemCategory::Throwables;
        }
        if (name == "ammo") {
            return DebugInventory::ItemCategory::Ammo;
        }
        if (name == "armor" || name == "armour") {
            return DebugInventory::ItemCategory::Armor;
        }
        if (name == "aid" || name == "alch") {
            return DebugInventory::ItemCategory::Aid;
        }
        if (name == "misc") {
            return DebugInventory::ItemCategory::Misc;
        }
        return std::nullopt;
    }

    /**
     * Map the leading spec token (case-insensitive) to the operation. Returns nullopt for an unknown token.
     */
    std::optional<DebugInventory::Operation> parseOperation(const std::string& name)
    {
        using Op = DebugInventory::Operation;
        if (name == "get" || name == "obtainable" || name == "loot") {
            return Op::Get;
        }
        if (name == "get-all" || name == "getall" || name == "all" || name == "everything") {
            return Op::GetAll;
        }
        if (name == "print" || name == "list") {
            return Op::Print;
        }
        if (name == "print-all" || name == "printall" || name == "list-all" || name == "listall") {
            return Op::PrintAll;
        }
        return std::nullopt;
    }

    /**
     * Bit for a Creation-Kit biped slot number (30..61) in the armor's slot mask (bit = slot - 30).
     * Returns 0 for out-of-range slots.
     */
    std::uint32_t slotBit(const int editorSlot)
    {
        return (editorSlot >= 30 && editorSlot <= 61) ? 1u << (editorSlot - 30) : 0u;
    }

    /**
     * Map a friendly armor-slot alias to its slot-mask bits (some aliases cover several slots).
     * Returns 0 for an unknown alias. Aliases target the worn-armor layer people usually mean.
     */
    std::uint32_t slotMaskFromAlias(const std::string& name)
    {
        static const std::unordered_map<std::string, std::uint32_t> aliases = { { "head", slotBit(30) },
            { "helmet", slotBit(30) },
            { "hair", slotBit(30) },
            { "body", slotBit(33) },
            { "outfit", slotBit(33) },
            { "underarmor", slotBit(33) },
            { "chest", slotBit(41) },
            { "torso", slotBit(41) },
            { "arms", slotBit(42) | slotBit(43) },
            { "larm", slotBit(42) },
            { "leftarm", slotBit(42) },
            { "rarm", slotBit(43) },
            { "rightarm", slotBit(43) },
            { "legs", slotBit(44) | slotBit(45) },
            { "lleg", slotBit(44) },
            { "leftleg", slotBit(44) },
            { "rleg", slotBit(45) },
            { "rightleg", slotBit(45) },
            { "hands", slotBit(34) | slotBit(35) },
            { "lhand", slotBit(34) },
            { "rhand", slotBit(35) },
            { "eyes", slotBit(47) },
            { "glasses", slotBit(47) },
            { "mouth", slotBit(49) },
            { "mask", slotBit(49) },
            { "neck", slotBit(50) },
            { "headband", slotBit(46) },
            { "ring", slotBit(51) } };
        const auto it = aliases.find(name);
        return it != aliases.end() ? it->second : 0u;
    }

    /**
     * The slot-mask bits of one armor slot: a slot alias as a string (torso, head, legs, ...), in any case, or a
     * CK slot number (30..61) as an integer.
     */
    std::expected<std::uint32_t, std::string> slotMaskOfOne(const toml::node& slot)
    {
        if (const auto* number = slot.as_integer()) {
            const auto value = number->get();
            if (value < 30 || value > 61) {
                return std::unexpected(std::format("slot {} is not a CK slot number, 30 to 61", value));
            }
            return slotBit(static_cast<int>(value));
        }
        if (const auto* name = slot.as_string()) {
            if (const auto bits = slotMaskFromAlias(f4cf::common::str_tolower(name->get())); bits != 0) {
                return bits;
            }
            return std::unexpected(std::format("slot '{}' is not a slot name (torso, head, legs, arms, ...), and a slot number has no quotes", name->get()));
        }
        return std::unexpected("a slot is a slot name in quotes or a CK slot number, 30 to 61");
    }

    /**
     * The slot mask of a request's slot: one slot, or an array of slots that any one of matches, see slotMaskOfOne.
     */
    std::expected<std::uint32_t, std::string> slotMaskOf(const toml::node& slot)
    {
        const auto* slots = slot.as_array();
        if (!slots) {
            return slotMaskOfOne(slot);
        }
        if (slots->empty()) {
            return std::unexpected("slot is an empty array");
        }
        std::uint32_t mask = 0;
        for (const auto& one : *slots) {
            const auto bits = slotMaskOfOne(one);
            if (!bits) {
                return bits;
            }
            mask |= *bits;
        }
        return mask;
    }

    /**
     * The categories of a request's category: one category name, or an array of them.
     */
    std::expected<std::vector<DebugInventory::ItemCategory>, std::string> categoriesOf(const toml::node& category)
    {
        std::vector<const toml::node*> names;
        if (const auto* array = category.as_array()) {
            for (const auto& name : *array) {
                names.push_back(&name);
            }
        } else {
            names.push_back(&category);
        }
        if (names.empty()) {
            return std::unexpected("category is an empty array");
        }

        std::vector<DebugInventory::ItemCategory> categories;
        for (const auto* name : names) {
            const auto* text = name->as_string();
            const auto parsed = text ? parseCategory(text->get()) : std::nullopt;
            if (!parsed) {
                return std::unexpected(std::format("category {} is not one of weapons, throwables, ammo, armor, aid, misc", f4cf::common::TomlEditor::Value(*name).text()));
            }
            categories.push_back(*parsed);
        }
        return categories;
    }

    /**
     * The string of a request's key, or why it is not one.
     */
    std::expected<std::string, std::string> stringOf(const std::string_view key, const toml::node& value)
    {
        if (const auto* text = value.as_string()) {
            return text->get();
        }
        return std::unexpected(std::format("{} is not a string, it goes in quotes", key));
    }

    /** Map a "class=" value (case-insensitive) to the ItemClass enum. Returns nullopt for unknown values. */
    std::optional<DebugInventory::ItemClass> parseItemClass(const std::string& value)
    {
        using IC = DebugInventory::ItemClass;
        if (value == "light") {
            return IC::ArmorLight;
        }
        if (value == "heavy") {
            return IC::ArmorHeavy;
        }
        if (value == "none") {
            return IC::ArmorNone;
        }
        if (value == "melee") {
            return IC::WeaponMelee;
        }
        if (value == "gun" || value == "guns" || value == "ranged") {
            return IC::WeaponGun;
        }
        if (value == "unarmed" || value == "hth" || value == "handtohand") {
            return IC::WeaponUnarmed;
        }
        return std::nullopt;
    }

    bool isArmorClass(const DebugInventory::ItemClass cls)
    {
        using IC = DebugInventory::ItemClass;
        return cls == IC::ArmorLight || cls == IC::ArmorHeavy || cls == IC::ArmorNone;
    }

    /** The armor weight-class value (BIPED_MODEL::WeightClass) for an Armor* ItemClass. */
    std::uint32_t armorWeightValue(const DebugInventory::ItemClass cls)
    {
        using IC = DebugInventory::ItemClass;
        switch (cls) {
        case IC::ArmorLight:
            return RE::BIPED_MODEL::kWeightLight;
        case IC::ArmorHeavy:
            return RE::BIPED_MODEL::kWeightHeavy;
        default:
            return RE::BIPED_MODEL::kWeightNone;
        }
    }

    /** Short name of an ItemClass for logging. */
    const char* itemClassName(const DebugInventory::ItemClass cls)
    {
        using IC = DebugInventory::ItemClass;
        switch (cls) {
        case IC::ArmorLight:
            return "light";
        case IC::ArmorHeavy:
            return "heavy";
        case IC::ArmorNone:
            return "none";
        case IC::WeaponMelee:
            return "melee";
        case IC::WeaponGun:
            return "gun";
        case IC::WeaponUnarmed:
            return "unarmed";
        }
        return "?";
    }

    /** A weapon the player can equip (drops the non-playable embedded/turret weapons). */
    bool isPlayableWeapon(const RE::TESObjectWEAP* w)
    {
        return !w->weaponData.flags.any(RE::WEAPON_FLAGS::kNotPlayable);
    }

    /** Grenades and mines (thrown/placed weapons) form the "throwables" category, split out from "weapons". */
    bool isThrowableWeapon(const RE::TESObjectWEAP* w)
    {
        const auto type = w->weaponData.type.get();
        return type == RE::WEAPON_TYPE::kGrenade || type == RE::WEAPON_TYPE::kMine;
    }

    /**
     * Test a weapon against a weapon ItemClass using its WEAPON_TYPE: Melee (the bladed/blunt one- and two-handed
     * types), Unarmed (hand-to-hand), Gun (firearms). Finer classes (laser, ballistic, heavy, ...) aren't in
     * WEAPON_TYPE — use keyword= for those. An armor ItemClass matches nothing (validated by the caller).
     */
    bool weaponMatchesClass(const RE::TESObjectWEAP* w, const DebugInventory::ItemClass cls)
    {
        using WT = RE::WEAPON_TYPE;
        using IC = DebugInventory::ItemClass;
        const auto type = w->weaponData.type.get();
        switch (cls) {
        case IC::WeaponMelee:
            return type == WT::kOneHandSword || type == WT::kOneHandDagger || type == WT::kOneHandAxe || type == WT::kOneHandMace || type == WT::kTwoHandSword ||
                   type == WT::kTwoHandAxe;
        case IC::WeaponUnarmed:
            return type == WT::kHandToHand;
        case IC::WeaponGun:
            return type == WT::kGun;
        default:
            return false;
        }
    }

    std::string weightClassName(const std::uint32_t weightClass)
    {
        switch (weightClass) {
        case RE::BIPED_MODEL::kWeightLight:
            return "light";
        case RE::BIPED_MODEL::kWeightHeavy:
            return "heavy";
        default:
            return "none";
        }
    }

    /** Print detail for armor: the CK slots it occupies and its weight class, e.g. " slots:41 class:heavy". */
    std::string describeArmor(const RE::TESObjectARMO* armo)
    {
        const std::uint32_t mask = armo->GetSlotMask();
        std::string slots;
        for (int bit = 0; bit < 32; ++bit) {
            if (mask & (1u << bit)) {
                slots += slots.empty() ? "" : ",";
                slots += std::to_string(30 + bit);
            }
        }
        std::string out = slots.empty() ? "" : " slots:" + slots;
        out += " class:" + weightClassName(armo->bipedModelData.weightClass);
        return out;
    }

    /** Print detail for throwables: grenade vs mine. Empty for regular weapons. */
    std::string describeWeapon(const RE::TESObjectWEAP* w)
    {
        if (w->weaponData.type.get() == RE::WEAPON_TYPE::kGrenade) {
            return " type:grenade";
        }
        if (w->weaponData.type.get() == RE::WEAPON_TYPE::kMine) {
            return " type:mine";
        }
        return "";
    }

    /** Short human-readable summary of the active filter constraints for the result log line. */
    std::string describeFilter(const DebugInventory::ItemFilter& filter)
    {
        std::string out;
        if (!filter.name.empty()) {
            out += std::format(" name='{}'", filter.name);
        }
        if (!filter.keyword.empty()) {
            out += std::format(" keyword='{}'", filter.keyword);
        }
        if (filter.slotMask) {
            out += std::format(" slot=0x{:X}", *filter.slotMask);
        }
        if (filter.itemClass) {
            out += std::format(" class={}", itemClassName(*filter.itemClass));
        }
        if (filter.predicate) {
            out += " predicate";
        }
        return out;
    }
}

namespace f4cf::f4vr
{
    /**
     * Iterate every named/playable item of one category, applying the typed filter, and perform the operation:
     * add the obtainable matches (Get), add all matches (GetAll), or log them as a dry run (Print).
     */
    void DebugInventory::iterateObjects(const Operation operation, const ItemCategory category, const ItemFilter& filter)
    {
        const bool printOnly = operation == Operation::Print || operation == Operation::PrintAll;
        const bool obtainableOnly = operation == Operation::Get || operation == Operation::Print;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player && !printOnly) {
            logger::warn("DebugInventory: no player available, skipping add-items");
            return;
        }

        const auto reachableSet = obtainableOnly ? collectObtainable() : std::unordered_set<RE::TESFormID>{};
        const auto* reachable = obtainableOnly ? &reachableSet : nullptr;

        // Needles are matched case-insensitively, so lower them at the point of use; this lets typed callers set
        // the fields in any case (parseRequest leaves them as authored).
        const auto name = common::str_tolower(filter.name);
        const auto kw = common::str_tolower(filter.keyword);

        if (filter.slotMask && category != ItemCategory::Armor) {
            logger::warn("DebugInventory: slot= applies to armor only; ignored for this category");
        }

        // Split the class= filter by what it targets, warning if it's used on a category it doesn't apply to.
        std::optional<ItemClass> weaponClass; // applies to the Weapons category
        std::optional<std::uint32_t> armorWeight; // applies to the Armor category
        if (filter.itemClass) {
            if (isArmorClass(*filter.itemClass)) {
                if (category == ItemCategory::Armor) {
                    armorWeight = armorWeightValue(*filter.itemClass);
                } else {
                    logger::warn("DebugInventory: class={} is an armor weight; ignored for this category", itemClassName(*filter.itemClass));
                }
            } else {
                if (category == ItemCategory::Weapons) {
                    weaponClass = *filter.itemClass;
                } else {
                    logger::warn("DebugInventory: class={} is a weapon type; ignored for this category", itemClassName(*filter.itemClass));
                }
            }
        }

        std::size_t matched = 0;
        switch (category) {
        case ItemCategory::Weapons:
            matched = addAllForms<RE::TESObjectWEAP>(
                player,
                ITEM_COUNT,
                name,
                kw,
                reachable,
                printOnly,
                filter.predicate,
                [weaponClass](const RE::TESObjectWEAP* w) {
                    return isPlayableWeapon(w) && !isThrowableWeapon(w) && (!weaponClass || weaponMatchesClass(w, *weaponClass));
                },
                describeWeapon);
            break;
        case ItemCategory::Throwables:
            matched = addAllForms<RE::TESObjectWEAP>(
                player,
                THROWABLE_COUNT,
                name,
                kw,
                reachable,
                printOnly,
                filter.predicate,
                [](const RE::TESObjectWEAP* w) {
                    return isPlayableWeapon(w) && isThrowableWeapon(w);
                },
                describeWeapon);
            break;
        case ItemCategory::Ammo:
            matched = addAllForms<RE::TESAmmo>(player, AMMO_COUNT, name, kw, reachable, printOnly, filter.predicate);
            break;
        case ItemCategory::Armor: {
            const auto slotMask = filter.slotMask;
            matched = addAllForms<RE::TESObjectARMO>(
                player,
                ITEM_COUNT,
                name,
                kw,
                reachable,
                printOnly,
                filter.predicate,
                [slotMask, armorWeight](const RE::TESObjectARMO* a) {
                    if (slotMask && (a->GetSlotMask() & *slotMask) == 0) {
                        return false;
                    }
                    if (armorWeight && a->bipedModelData.weightClass != *armorWeight) {
                        return false;
                    }
                    return true;
                },
                describeArmor);
            break;
        }
        case ItemCategory::Aid:
            matched = addAllForms<RE::AlchemyItem>(player, ITEM_COUNT, name, kw, reachable, printOnly, filter.predicate);
            break;
        case ItemCategory::Misc:
            matched = addAllForms<RE::TESObjectMISC>(player, ITEM_COUNT, name, kw, reachable, printOnly, filter.predicate);
            break;
        }

        const auto filterDesc = describeFilter(filter);
        logger::info("DebugInventory: {} {} item(s){}{}",
            printOnly ? "printed" : "added",
            matched,
            filterDesc.empty() ? "" : " matching" + filterDesc,
            obtainableOnly ? " (obtainable only)" : " (all)");
    }

    /**
     * Read one request of the aAddItemsOnce config, an inline table such as
     * { op = "print", category = ["weapons", "armor"], name = "combat", keyword = "laser", slot = ["torso", 33], class = "heavy" }.
     * op and category are required, the others are the filter, see ItemFilter, all of which an item has to match:
     *   op        get | get-all | print | print-all, see Operation
     *   category  one category or an array of them: weapons, throwables, ammo, armor, aid, misc
     *   name      full-name substring (any category), case-insensitive
     *   keyword   keyword editor-ID substring (any category), case-insensitive, e.g. "laser" to pick laser sub-types
     *   slot      armor body slot(s) any one of matches: a slot alias (torso, head, legs, arms, ...) or a CK slot
     *             number 30..61, or an array of them (armor only)
     *   class     armor weight (light | heavy | none) or weapon type (melee | gun | unarmed)
     * Values are case-insensitive, keys are not. Returns why the request is not valid, naming the key, for a key it
     * does not know, a value of the wrong type, or a name that is none of its kind. Reads no game state.
     */
    std::expected<DebugInventory::Request, std::string> DebugInventory::parseRequest(const toml::node& request)
    {
        const auto* table = request.as_table();
        if (!table) {
            return std::unexpected(R"(a request is a table, { op = "print", category = "weapons" })");
        }

        Request result;
        bool hasOperation = false;
        bool hasCategory = false;
        for (const auto& [keyName, value] : *table) {
            const std::string_view key = keyName.str();
            if (key == "op") {
                const auto text = stringOf(key, value);
                const auto operation = text ? parseOperation(common::str_tolower(*text)) : std::nullopt;
                if (!operation) {
                    return std::unexpected(std::format("op {} is not one of get, get-all, print, print-all", common::TomlEditor::Value(value).text()));
                }
                result.operation = *operation;
                hasOperation = true;
            } else if (key == "category") {
                auto categories = categoriesOf(value);
                if (!categories) {
                    return std::unexpected(categories.error());
                }
                result.categories = std::move(*categories);
                hasCategory = true;
            } else if (key == "name" || key == "keyword") {
                auto text = stringOf(key, value);
                if (!text) {
                    return std::unexpected(text.error());
                }
                (key == "name" ? result.filter.name : result.filter.keyword) = std::move(*text);
            } else if (key == "slot") {
                const auto mask = slotMaskOf(value);
                if (!mask) {
                    return std::unexpected(mask.error());
                }
                result.filter.slotMask = *mask;
            } else if (key == "class") {
                const auto text = stringOf(key, value);
                const auto itemClass = text ? parseItemClass(common::str_tolower(*text)) : std::nullopt;
                if (!itemClass) {
                    return std::unexpected(
                        std::format("class {} is not an armor weight (light, heavy, none) or a weapon type (melee, gun, unarmed)", common::TomlEditor::Value(value).text()));
                }
                result.filter.itemClass = *itemClass;
            } else {
                return std::unexpected(std::format("'{}' is not a key of a request: op, category, name, keyword, slot, class", key));
            }
        }
        if (!hasOperation) {
            return std::unexpected("op is missing: get, get-all, print or print-all");
        }
        if (!hasCategory) {
            return std::unexpected("category is missing: weapons, throwables, ammo, armor, aid or misc");
        }
        return result;
    }

    /**
     * Run the requests of the aAddItemsOnce config against the player, in their order, each for each of its
     * categories, see parseRequest. All of them are read first, and when any is not valid none runs: the reasons
     * are logged, and the requests have to be written again.
     */
    void DebugInventory::iterateObjects(const toml::array& requests)
    {
        std::vector<Request> parsed;
        bool valid = true;
        for (std::size_t i = 0; i < requests.size(); ++i) {
            auto request = parseRequest(requests[i]);
            if (request) {
                parsed.push_back(std::move(*request));
            } else {
                logger::warn("DebugInventory: request {} {}: {}", i + 1, common::TomlEditor::Value(requests[i]).text(), request.error());
                valid = false;
            }
        }
        if (!valid) {
            logger::warn("DebugInventory: no request was run, as not all of them are valid");
            return;
        }

        for (std::size_t i = 0; i < parsed.size(); ++i) {
            logger::info("DebugInventory: processing request {} {}", i + 1, common::TomlEditor::Value(requests[i]).text());
            for (const auto category : parsed[i].categories) {
                iterateObjects(parsed[i].operation, category, parsed[i].filter);
            }
        }
    }
}
