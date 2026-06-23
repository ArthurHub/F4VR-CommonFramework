#pragma once

#include <string>
#include <string_view>

#include "F4VRUtils.h"
#include "PlayerNodes.h"

namespace f4cf::f4vr
{
    /**
     * Tracks the player's equipped weapon and detects when it (or the power-armor state) changes.
     * Resolves the weapon to its extended name (see getEquippedWeaponNameExtended) so weapons that
     * can be both pistol and rifle are distinguished. Also tracks the throwable weapon (grenade/mine)
     * which exists only while the player is mid-throw - see detectThrowableChange().
     *
     * Detection only; what to do on a change (e.g. loading weapon offsets) is the caller's job.
     * Call detectChange() every frame with the weapon node and read the getters when it returns true.
     */
    class EquippedWeaponHandler
    {
    public:
        // Name used when no weapon is equipped / the weapon node isn't visible.
        static constexpr auto EMPTY_HAND = "EmptyHand";

        explicit EquippedWeaponHandler(const bool useDisplayName = false)
            : _useDisplayName(useDisplayName)
        {}

        /**
         * Detect whether the equipped weapon (resolved to its extended name) or the power-armor
         * state changed since the last call. Updates the tracked state and returns true on change.
         * Pass the weapon node from getWeaponNode(); when it's not visible the weapon is treated
         * as EMPTY_HAND.
         */
        bool detectChange(RE::NiNode* weaponNode)
        {
            const std::string name = isNodeVisible(weaponNode) ? getEquippedWeaponNameExtended(weaponNode) : EMPTY_HAND;
            // qualified so it resolves to the free function and not this class's isInPowerArmor() getter
            const bool inPA = f4vr::isInPowerArmor();
            if (name == _weaponName && inPA == _inPowerArmor) {
                return false;
            }

            _weaponName = name;
            _inPowerArmor = inPA;
            _isMelee = isMeleeWeaponEquipped();
            return true;
        }

        /**
         * Detect whether a throwable weapon (grenade/mine) was newly equipped since the last call.
         * The throwable node only exists while the player is mid-throw, so this is independent of the
         * equipped-weapon detectChange() above. Returns true only on the frame a throwable first
         * appears; throwableNode() is refreshed every call (null when none is active).
         */
        bool detectThrowableChange()
        {
            _throwableNode = getThrowableWeaponNode();
            if (!_throwableNode) {
                // no throwable active, clear the tracked name
                _throwableName = "";
                return false;
            }
            if (!_throwableName.empty()) {
                // already tracking the current throwable
                return false;
            }
            _throwableName = _throwableNode->name.c_str();
            return true;
        }

        const std::string& weaponName() const
        {
            return _weaponName;
        }

        bool isWeaponDrawn() const
        {
            return _weaponName != EMPTY_HAND;
        }

        bool isInPowerArmor() const
        {
            return _inPowerArmor;
        }

        bool isMelee() const
        {
            return _isMelee;
        }

        /**
         * The current throwable node, or null when none is active. Valid for the frame in which
         * detectThrowableChange() was last called.
         */
        RE::NiNode* throwableNode() const
        {
            return _throwableNode;
        }

        const std::string& throwableName() const
        {
            return _throwableName;
        }

    private:
        /**
         * Get the game name of the equipped weapon, extending weapons that can be both pistol and
         * rifle to include a "Rifle" suffix derived from the weapon's grip stock node.
         * It's not critical, but a nice to have for a better hand grip on those weapons.
         */
        std::string getEquippedWeaponNameExtended(RE::NiNode* weapon) const
        {
            const auto& weaponName = _useDisplayName ? getEquippedWeaponName() : getEquippedWeaponInternalName();

            if (weaponName == "Plasma") {
                const auto stockName = getGripStockName(weapon);
                if (stockName.starts_with("RiotGrip") || stockName.starts_with("Sniper") || stockName.find("Rifle") != std::string_view::npos) {
                    return weaponName + " Rifle";
                }
            } else if (weaponName == "Pipe" || weaponName == "Pipe Bolt-Action") {
                const auto stockName = getGripStockName(weapon);
                if (stockName.starts_with("HandmadePaddedStock") || stockName.starts_with("SpringStock") || stockName.starts_with("PipeStock")) {
                    return weaponName + " Rifle";
                }
            } else if (weaponName == "Laser" || weaponName == "Institute") {
                const auto stockName = getGripStockName(weapon);
                if (stockName.find("Rifle") != std::string_view::npos) {
                    return weaponName + " Rifle";
                }
            }

            return weaponName;
        }

        /**
         * Get the name of the weapon's grip stock node, used to refine the equipped weapon name.
         */
        static std::string_view getGripStockName(RE::NiNode* weapon)
        {
            if (const auto gripNode = getFirstChild(findNode(weapon, "P-Grip"))) {
                return gripNode->name;
            }
            return "";
        }

        // empty (not EMPTY_HAND) so the first detectChange() call always reports a change
        std::string _weaponName;
        bool _inPowerArmor = false;
        bool _isMelee = false;

        // throwable weapon (grenade/mine), tracked independently; only present while mid-throw
        RE::NiNode* _throwableNode = nullptr;
        std::string _throwableName;

        // legacy flag to use display name instead of editor name for backward compatibility in FRIK
        bool _useDisplayName;
    };
}
