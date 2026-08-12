#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace f4cf::f4vr
{
    /**
     * Havok collision-layer (RE::COL_LAYER) helpers: layer <-> name, and layer bitmasks.
     *
     * Every Havok collision-filter word carries its layer in the low 7 bits (the rest is the collision
     * group). CommonLibF4VR models the enum but nothing that reads it in human terms, and that is what
     * makes a physics query result inscrutable: a raycast that stops in what looks like empty air is only
     * explicable once the hit's layer reads "actorZone" (an invisible AI trigger volume, which collides
     * with character controllers and therefore with any ray carrying the player's collision filter) rather
     * than "static". Names also give config and logs a vocabulary for layers that beats raw numbers.
     */

    /**
     * The collision layer carried in a Havok collision-filter word (its low 7 bits).
     */
    RE::COL_LAYER getCollisionLayer(std::uint32_t filter);

    /**
     * Readable name of the layer in a collision-filter word ("static", "actorZone", "charController", ...),
     * or "layer<N>" for a value the enum doesn't cover. Accepts a bare layer value too, since that is just a
     * filter word with no collision group.
     */
    std::string getCollisionLayerName(std::uint32_t filter);

    /**
     * The layer a token names: a layer name (case-insensitive, as spelled by getCollisionLayerName) or a raw
     * layer number. Nullopt when it is neither.
     */
    std::optional<RE::COL_LAYER> parseCollisionLayer(std::string_view token);

    /**
     * The bit a layer occupies in a collision-layer bitmask. Out-of-range values contribute nothing rather
     * than shifting past the end of the mask.
     */
    constexpr std::uint64_t collisionLayerBit(const RE::COL_LAYER layer)
    {
        const auto index = static_cast<std::uint32_t>(layer);
        return index < 64 ? 1ull << index : 0;
    }

    /**
     * Bitmask of a set of layers, for "which layers does this query care about" sets:
     * `collisionLayerMask({ RE::COL_LAYER::kTrigger, RE::COL_LAYER::kActorZone })`. Usable at compile time,
     * so a fixed set costs nothing at runtime and a misspelled layer is a compile error.
     */
    constexpr std::uint64_t collisionLayerMask(const std::initializer_list<RE::COL_LAYER> layers)
    {
        std::uint64_t mask = 0;
        for (const auto layer : layers) {
            mask |= collisionLayerBit(layer);
        }
        return mask;
    }

    /**
     * Bitmask (by layer index) of a comma-separated list of layer names and/or numbers — the runtime
     * counterpart of collisionLayerMask, for sets that come from config. Unparsable entries are logged and
     * skipped.
     */
    std::uint64_t parseCollisionLayerMask(std::string_view list);

    /**
     * Whether the layer in a collision-filter word is in a parseCollisionLayerMask bitmask.
     */
    bool isCollisionLayerInMask(std::uint32_t filter, std::uint64_t mask);
}
