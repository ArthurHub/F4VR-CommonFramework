#include "CollisionLayers.h"

#include <format>

#include "common/CommonUtils.h"

namespace
{
    // The layer is the low 7 bits of a collision-filter word; the rest is the collision group.
    constexpr std::uint32_t COL_LAYER_MASK = 0x7F;

    // RE::COL_LAYER by index. CommonLibF4VR has the enum but no names; these match its enumerator spelling
    // minus the "k" prefix, and are the vocabulary parseCollisionLayer accepts.
    constexpr const char* COL_LAYER_NAMES[] = { "unidentified",
        "static",
        "animStatic",
        "transparent",
        "clutter",
        "weapon",
        "projectile",
        "spell",
        "biped",
        "trees",
        "props",
        "water",
        "trigger",
        "terrain",
        "trap",
        "nonCollidable",
        "cloudTrap",
        "ground",
        "portal",
        "debrisSmall",
        "debrisLarge",
        "acousticSpace",
        "actorZone",
        "projectileZone",
        "gasTrap",
        "shellCasing",
        "transparentSmall",
        "invisibleWall",
        "transparentSmallAnim",
        "clutterLarge",
        "charController",
        "stairHelper",
        "deadBip",
        "bipedNoCC",
        "avoidBox",
        "collisionBox",
        "cameraSphere",
        "doorDetection",
        "coneProjectile",
        "cameraPick",
        "itemPick",
        "lineOfSight",
        "pathPick",
        "unused0",
        "unused1",
        "spellExplosion",
        "droppingPick" };
}

namespace f4cf::f4vr
{
    RE::COL_LAYER getCollisionLayer(const std::uint32_t filter)
    {
        return static_cast<RE::COL_LAYER>(filter & COL_LAYER_MASK);
    }

    std::string getCollisionLayerName(const std::uint32_t filter)
    {
        const auto layer = filter & COL_LAYER_MASK;
        return layer < std::size(COL_LAYER_NAMES) ? COL_LAYER_NAMES[layer] : std::format("layer{}", layer);
    }

    std::optional<RE::COL_LAYER> parseCollisionLayer(const std::string_view token)
    {
        const auto name = common::str_tolower(std::string(token));
        for (std::uint32_t i = 0; i < std::size(COL_LAYER_NAMES); i++) {
            if (name == common::str_tolower(COL_LAYER_NAMES[i])) {
                return static_cast<RE::COL_LAYER>(i);
            }
        }
        try {
            const auto layer = static_cast<std::uint32_t>(std::stoul(name, nullptr, 0));
            return layer < std::size(COL_LAYER_NAMES) ? std::optional(static_cast<RE::COL_LAYER>(layer)) : std::nullopt;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    std::uint64_t parseCollisionLayerMask(const std::string_view list)
    {
        std::uint64_t mask = 0;
        for (const auto& token : common::splitTrimmed(std::string(list), ',')) {
            const auto layer = parseCollisionLayer(token);
            if (!layer) {
                logger::warn("Unknown collision layer '{}', skipped", token);
                continue;
            }
            mask |= collisionLayerBit(*layer);
        }
        return mask;
    }

    bool isCollisionLayerInMask(const std::uint32_t filter, const std::uint64_t mask)
    {
        // every COL_LAYER fits in the 64-bit mask, but a filter word from the engine may not
        const auto layer = filter & COL_LAYER_MASK;
        return layer < 64 && (mask & 1ull << layer) != 0;
    }
}
