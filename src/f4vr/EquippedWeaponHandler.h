#pragma once

#include <string>
#include <string_view>

#include "F4VRUtils.h"
#include "PlayerNodes.h"

namespace f4cf::f4vr
{
    /**
     * Tracks the player's equipped weapon and detects when it (or the power-armor state) changes.
     * Detection is based on the equipped weapon form; the resolved names are exposed lazily via the
     * getters, where the "extended" variants distinguish weapons that can be both pistol and rifle
     * (see weaponNameExtended()). Also tracks the throwable weapon (grenade/mine) which exists only
     * while the player is mid-throw - see detectThrowableChange().
     *
     * Detection only; what to do on a change (e.g. loading weapon offsets) is the caller's job.
     * Call detectChange() every frame and read the getters when it returns true.
     */
    class EquippedWeaponHandler
    {
    public:
        // Name used when no weapon is equipped / the weapon node isn't visible.
        static constexpr auto EMPTY_HAND = "EmptyHand";

        /**
         * Is actual weapon drawn (not unarmed/fists) and visible in the player's hand
         */
        bool isDrawn() const
        {
            return _currentWeapon != nullptr;
        }

        /**
         * Is currently in power armor.
         */
        bool inPowerArmor() const
        {
            return _inPowerArmor;
        }

        /**
         * Is the drawn weapon a melee weapon (not ranged). Returns false for unarmed/fists.
         */
        bool isMelee() const
        {
            return _currentWeaponMelee;
        }

        /**
         * Is the player unarmed (fists) and not open hands. Returns false for melee or ranged weapons.
         */
        bool isUnarmed() const
        {
            return !isDrawn() && isUnarmedWeaponDrawn();
        }

        /**
         * The full display name of the weapon.
         * Localized to the current game language, may differ from the internal editor name.
         */
        const std::string& weaponName()
        {
            if (_weaponName.empty()) {
                _weaponName = isDrawn() ? getEquippedWeaponName() : EMPTY_HAND;
            }
            return _weaponName;
        }

        /**
         * The full display name of the weapon with additional distincation between pistol and rifle depending on modifications.
         */
        const std::string& weaponNameExtended()
        {
            if (_weaponNameExtended.empty()) {
                _weaponNameExtended = getExtendedWeaponName(_currentWeaponNode, weaponName());
            }
            return _weaponNameExtended;
        }

        /**
         * The internal editor name of the weapon, used for form lookups and modding. Not localized.
         * Falls back to the display name if the internal name is unavailable.
         */
        const std::string& weaponInternalName()
        {
            if (_weaponInternalName.empty()) {
                _weaponInternalName = isDrawn() ? getEquippedWeaponInternalName() : EMPTY_HAND;
                if (_weaponInternalName.empty()) {
                    _weaponInternalName = weaponName();
                }
            }
            return _weaponInternalName;
        }

        /**
         * The internal editor name of the weapon with additional distincation between pistol and rifle depending on modifications.
         */
        const std::string& weaponInternalNameExtended()
        {
            if (_weaponInternalNameExtended.empty()) {
                _weaponInternalNameExtended = getExtendedWeaponName(_currentWeaponNode, weaponInternalName());
            }
            return _weaponInternalNameExtended;
        }

        /**
         * Detect whether the equipped weapon (its underlying form) or the power-armor state changed
         * since the last call. Updates the tracked state and returns true on change. The weapon node
         * is obtained internally via getWeaponNode(); when it's not visible the weapon is treated as
         * EMPTY_HAND.
         */
        bool detectChange()
        {
            const bool inPA = isInPowerArmor();
            if (_inPowerArmor != inPA) {
                _inPowerArmor = inPA;
                clearState();
            }

            auto* weaponNode = getWeaponNode();
            if (weaponNode && isNodeVisible(weaponNode)) {
                auto* equippedWeapon = getEquippedWeapon();
                if (_currentWeapon != equippedWeapon) {
                    logger::debug("Drawn weapon changed to {}", equippedWeapon ? equippedWeapon->GetFormEditorID() : "<none>");
                    clearState();
                    _currentWeapon = equippedWeapon;
                    _currentWeaponNode = weaponNode;
                    _currentWeaponMelee = isMeleeWeaponDrawn();
                    return true;
                }
            } else if (_currentWeapon) {
                logger::debug("Drawn weapon changed to None");
                clearState();
                return true;
            }

            return false;
        }

        /**
         * Reset the tracked weapon state so the next detectChange() re-detects from scratch and reports a
         * change. Use after the weapon 3D is rebuilt (e.g. save load, power-armor swap) so a stale node
         * pointer from a previous scene graph isn't retained.
         */
        void invalidate()
        {
            clearState();
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
         * Reset the tracked weapon state to empty.
         */
        void clearState()
        {
            _currentWeapon = nullptr;
            _currentWeaponNode = nullptr;
            _currentWeaponMelee = false;
            _weaponName = "";
            _weaponNameExtended = "";
            _weaponInternalName = "";
            _weaponInternalNameExtended = "";
            _throwableName = "";
        }

        /**
         * Get the game name of the equipped weapon, extending weapons that can be both pistol and
         * rifle to include a "Rifle" suffix derived from the weapon's grip stock node.
         * It's not critical, but a nice to have for a better hand grip on those weapons.
         */
        static std::string getExtendedWeaponName(RE::NiNode* weapon, const std::string& weaponName)
        {
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

        RE::TESObjectWEAP* _currentWeapon = nullptr;
        RE::NiNode* _currentWeaponNode = nullptr;
        bool _currentWeaponMelee = false;
        bool _inPowerArmor = false;

        // empty (not EMPTY_HAND) so the first detectChange() call always reports a change
        std::string _weaponName;
        std::string _weaponNameExtended;
        std::string _weaponInternalName;
        std::string _weaponInternalNameExtended;

        // throwable weapon (grenade/mine), tracked independently; only present while mid-throw
        RE::NiNode* _throwableNode = nullptr;
        std::string _throwableName;
    };
}
