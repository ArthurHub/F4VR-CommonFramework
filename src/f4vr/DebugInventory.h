#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace f4cf::f4vr
{
    /**
     * Debug helper to bulk-add game items to the player's inventory by category, for testing.
     * Wired to the "sDebugAddItemsOnceNames" INI flag the same way DebugDump uses "sDebugDumpDataOnceNames".
     */
    struct DebugInventory
    {
        enum class ItemCategory : std::uint8_t
        {
            Weapons, // guns and melee (throwables excluded)
            Throwables, // grenades and mines
            Ammo,
            Armor,
            Aid,
            Misc,
        };

        /**
         * What to do with the matched items. Selected by the required first token of an iterateObjects string.
         */
        enum class Operation : std::uint8_t
        {
            Get, // add items the player can actually get (leveled-list-reachable or craftable)
            GetAll, // add every named/playable item, including non-obtainable ones
            Print, // dry run of Get: log each obtainable match (FormID, name, source, keywords, armor slots) instead of adding
            PrintAll, // dry run of GetAll: log every match, including non-obtainable ones
        };

        /**
         * A weapon type or armor weight class, used by ItemFilter::itemClass. The Armor* values match armor by
         * weight; the Weapon* values match weapons by WEAPON_TYPE. A value is ignored (with a warning) for a
         * category it doesn't apply to.
         */
        enum class ItemClass : std::uint8_t
        {
            ArmorLight,
            ArmorHeavy,
            ArmorNone, // clothing / no-armor weight
            WeaponMelee, // bladed/blunt one- and two-handed
            WeaponGun, // firearms
            WeaponUnarmed, // hand-to-hand
        };

        /**
         * Typed item filter for iterateObjects. Every field is an independent constraint, AND-ed
         * together; an empty string / nullopt means "no constraint on this". name and keyword are matched
         * case-insensitively on any category; slotMask and itemClass are armor- (and itemClass also weapon-)
         * specific and ignored for categories they don't apply to.
         */
        struct ItemFilter
        {
            std::string name; // full-name substring (case-insensitive); empty = any
            std::string keyword; // keyword editor-ID substring (case-insensitive); empty = any
            std::optional<std::uint32_t> slotMask; // armor: OR-matched CK-slot mask (bit = CK slot - 30); nullopt = any
            std::optional<ItemClass> itemClass; // armor weight or weapon type; nullopt = any
        };

        static void iterateObjects(std::string_view spec);

        static void iterateObjects(Operation operation, ItemCategory category, const ItemFilter& filter);
    };
}
