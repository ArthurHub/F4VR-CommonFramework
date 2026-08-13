#include "F4VRUtils.h"

#include <cmath>
#include <numbers>

#include "ModBase.h"
#include "PlayerNodes.h"
#include "common/MatrixUtils.h"
#include "f4sevr/PapyrusUtils.h"
#include "vrcf/VRControllersManager.h"

namespace
{
    /**
     * Resolve a UI nif path by probing several locations in order and returning the first that
     * exists on disk: the path as given, then under "Data/Meshes/", then under this mod's own
     * "Data/Meshes/<ModName>/" folder. The "Data/"-rooted candidates are returned verbatim so the
     * loader does not re-prefix them. When none exist the path is returned unchanged so the
     * subsequent load surfaces a clear not-found error.
     */
    std::string resolveNifPath(const std::string& path)
    {
        // Strip any leading "/" or "\" so the prefixed candidates don't end up with a doubled separator.
        const auto firstReal = path.find_first_not_of("/\\");
        const std::string relPath = firstReal == std::string::npos ? path : path.substr(firstReal);

        for (const auto& candidate : {
                 path,
                 "Data\\Meshes\\" + relPath,
                 "Data\\Meshes\\" + g_mod->getName() + "\\" + relPath,
             }) {
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
        return path;
    }

    /**
     * Recursively copy node names from a source tree onto a structurally-identical clone tree.
     * The game's clone routine does not preserve node names, so we walk both trees in parallel
     * (the clone keeps the source's child order and count) and copy each name down the hierarchy.
     */
    void copyNodeNamesRecursive(RE::NiAVObject* source, RE::NiAVObject* clone)
    {
        if (!source || !clone) {
            return;
        }

        clone->name = source->name;

        const auto sourceNode = source->IsNode();
        const auto cloneNode = clone->IsNode();
        if (!sourceNode || !cloneNode) {
            return;
        }

        const auto count = min(sourceNode->children.size(), cloneNode->children.size());
        for (std::uint16_t i = 0; i < count; ++i) {
            copyNodeNamesRecursive(sourceNode->children[i].get(), cloneNode->children[i].get());
        }
    }
}

namespace f4cf::f4vr
{
    void showMessagebox(const std::string& text)
    {
        logger::info("Show messagebox: '{}'", text.c_str());
        F4SEVR::execPapyrusGlobalFunction("Debug", "Messagebox", text);
    }

    void showNotification(const std::string& text)
    {
        logger::info("Show notification: '{}'", text.c_str());
        F4SEVR::execPapyrusGlobalFunction("Debug", "Notification", text);
    }

    /**
     * Close the weapon favorites menu.
     */
    void closeFavoriteMenu()
    {
        if (RE::UIMessageQueue* uiQueue = RE::UIMessageQueue::GetSingleton()) {
            uiQueue->AddMessage("FavoritesMenu", RE::UI_MESSAGE_TYPE::kHide);
        }
    }

    /**
     * Is the given hand is player primary hand.
     */
    bool isPrimaryHand(const vrcf::Hand hand)
    {
        switch (hand) {
        case vrcf::Hand::Primary:
            return true;
        case vrcf::Hand::Offhand:
            return false;
        case vrcf::Hand::Right:
            return !f4vr::isLeftHandedMode();
        case vrcf::Hand::Left:
            return f4vr::isLeftHandedMode();
        default:
            return true;
        }
    }

    /**
     * Set the visibility of controller wand.
     */
    void setWandsVisibility(const bool show, const bool leftWand)
    {
        const auto node = leftWand ? getPlayerNodes()->primaryWandNode : getPlayerNodes()->SecondaryWandNode;
        for (const auto& child : node->children) {
            if (child) {
                if (child->IsNiTriShape()) {
                    setNodeVisibility(child.get(), show);
                    break;
                }
                if (!_stricmp(child->name.c_str(), "")) {
                    setNodeVisibility(child.get(), show);
                    if (const auto grandChild = child->IsNode()) {
                        setNodeVisibility(grandChild, show);
                    }
                    break;
                }
            }
        }
    }

    /**
     * @return true if the player has any weapon in the hand (including fists).
     */
    bool isWeaponDrawn()
    {
        return getPlayer()->GetWeaponMagicDrawn();
    }

    /**
     * @return true if the equipped weapon is a melee weapon type, unarmed is NOT melee.
     */
    bool isMeleeWeaponDrawn()
    {
        if (!isWeaponDrawn()) {
            return false;
        }
        const auto equippedWeapon = getEquippedWeapon();
        if (!equippedWeapon) {
            return false;
        }
        return equippedWeapon->IsMeleeWeapon();
    }

    /**
     * @return true if the player is in unarmed stance with no weapon item equipped (pure fists).
     */
    bool isUnarmedWeaponDrawn()
    {
        return isWeaponDrawn() && getEquippedWeapon() == nullptr;
    }

    /**
     * Get the game name of the equipped weapon.
     */
    RE::TESObjectWEAP* getEquippedWeapon()
    {
        auto* pc = getPlayer();
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        return pc && vm ? GetEquippedWeapon(vm, 0, pc, 0) : nullptr;
    }

    /**
     * Get the game full display name of the equipped weapon.
     * This name is localized to the current game language and can be different than the internal editor name.
     */
    std::string getEquippedWeaponName()
    {
        const auto weap = getEquippedWeapon();
        return weap != nullptr ? weap->GetFullName() : std::string{};
    }

    /**
     * Get the game internal name used for for editor of the equipped weapon.
     * This name is always in Engligh!
     */
    std::string getEquippedWeaponInternalName()
    {
        const auto weap = getEquippedWeapon();
        return weap != nullptr ? weap->GetFormEditorID() : std::string{};
    }

    /**
     * Get the game name of the equipped weapon.
     * Slower than the other and can return throwable name when no proper weapon is equipped.
     */
    std::string getEquippedWeaponNameLegacy()
    {
        const auto* process = getPlayer()->currentProcess;
        const auto* middleHigh = process ? process->middleHigh : nullptr;
        if (!middleHigh || middleHigh->equippedItems.empty()) {
            return "";
        }
        const auto* item = middleHigh->equippedItems[0].item.object;
        return item ? std::string{ RE::TESFullName::GetFullName(*item) } : std::string{};
    }

    bool hasKeyword(const RE::TESObjectARMO* armor, const std::uint32_t keywordFormId)
    {
        if (!armor) {
            return false;
        }
        for (std::uint32_t i = 0; i < armor->numKeywords; i++) {
            if (armor->keywords[i]) {
                if (armor->keywords[i]->formID == keywordFormId) {
                    return true;
                }
            }
        }
        return false;
    }

    bool isJumpingOrInAir()
    {
        return IsInAir(getPlayer());
    }

    bool isPlayerSneaking()
    {
        return IsSneaking(getPlayer());
    }

    // Thanks Shizof and SmoothMovementVR for below code
    bool isInPowerArmor()
    {
        const auto player = getPlayer();
        if (player == nullptr) {
            return false;
        }
        const auto biped = player->biped.get();
        if (!biped) {
            return false;
        }
        const auto* equippedForm = biped->object[0x03].parent.object;
        if (!equippedForm || equippedForm->formType != RE::ENUM_FORM_ID::kARMO) {
            return false;
        }
        const auto* armor = static_cast<const RE::TESObjectARMO*>(equippedForm);
        return hasKeyword(armor, KEYWORD_POWER_ARMOR) || hasKeyword(armor, KEYWORD_POWER_ARMOR_FRAME);
    }

    /**
     * Is the player is current in an "internal cell" as inside a building, cave, etc.
     */
    bool isInInternalCell()
    {
        return RE::PlayerCharacter::GetSingleton()->parentCell->IsInterior();
    }

    /**
     * Is the player swimming either on the surface or underwater.
     */
    bool isSwimming(const RE::PlayerCharacter* player)
    {
        return player && static_cast<int>(player->DoGetCharacterState()) == 5;
    }

    /**
     * Is the player is currently underwater as detected by underwater timer being non-zero.
     */
    bool isUnderwater(const RE::PlayerCharacter* player)
    {
        return player && player->underWaterTimer > 0;
    }

    /**
     * Check if movement from current position to target position is safe (no collisions).
     * Uses ray casting to detect obstacles in the movement path.
     */
    bool isMovementSafe(RE::PlayerCharacter* player, const RE::NiPoint3& currentPos, const RE::NiPoint3& targetPos)
    {
        // Create a pick data structure for ray casting
        RE::bhkPickData pickData;

        // Set up the ray from current position to target position
        pickData.SetStartEnd(currentPos, targetPos);

        // Configure collision filter to use the player's collision layer
        pickData.collisionFilter = player->GetCollisionFilter();

        // Use projectile LOS calculation for collision detection
        // This is a reliable method used by the game for checking clear paths
        if (const auto dataHandler = RE::TESDataHandler::GetSingleton()) {
            // Try to get any BGSProjectile from the form arrays for LOS calculation
            auto& projectileArray = dataHandler->GetFormArray<RE::BGSProjectile>();
            if (!projectileArray.empty()) {
                const auto projectile = projectileArray[0]; // Use the first available projectile
                if (projectile && RE::CombatUtilities::CalculateProjectileLOS(player, projectile, pickData)) {
                    // If LOS calculation succeeded, check if there was a hit
                    if (pickData.HasHit()) {
                        const auto hitFraction = pickData.GetHitFraction();
                        // If hit fraction is very close to 1.0, the collision is at the target (acceptable)
                        // If hit fraction is significantly less than 1.0, there's an obstacle in the way
                        if (hitFraction < 0.9f) {
                            return false;
                        }
                    }
                    // No collision detected or collision is at the target, movement is safe
                    return true;
                }
            }
        }

        // Fallback: if we can't get a projectile or LOS calculation fails,
        // allow movement but log a warning. This is safer than blocking all movement.
        return true;
    }

    /**
     * The ProcessLists singleton. Uses the raw VR offset (g_processLists) because the bundled
     * ProcessLists::GetSingleton() resolves a RelocationID with no entry in the shipped VR address library.
     */
    RE::ProcessLists* getProcessLists()
    {
        return *g_processLists;
    }

    /**
     * Enumerate every loaded actor within `radius` of the world `point` into `outActors`, via the native
     * ProcessLists::GetActorsWithinRangeOfPoint. Preferred over walking ProcessLists::highActorHandles so the
     * desktop struct layout is never trusted on the VR runtime. No-op when the ProcessLists singleton is null.
     */
    void getActorsWithinRangeOfPoint(const RE::NiPoint3& point, const float radius, RE::BSScrapArray<RE::NiPointer<RE::Actor>>& outActors)
    {
        const auto processLists = getProcessLists();
        if (!processLists) {
            return;
        }
        RE::NiPoint3 p = point; // the native takes a non-const reference
        ProcessLists_GetActorsWithinRangeOfPoint(processLists, p, radius, outActors);
    }

    /**
     * The angle in degrees between the direction the actor is facing and the direction to a world point:
     * 0 is dead ahead, +/-180 directly behind, the sign following the engine's yaw. Returns 0 for a null actor
     * or for a point the actor is standing on.
     *
     * Facing comes from the actor's own heading (`data.angle.z` — the engine's yaw in radians, zero along +Y
     * and increasing toward +X), NOT from Actor::GetEyeVector: that virtual returns unusable values on the VR
     * runtime, filling its origin out-param with what is plainly a direction vector. Reading the member also
     * cannot fault, and body heading is what the AI actually turns toward a target — head look-at is cosmetic
     * on top of it.
     *
     * Measured horizontally (the vertical component is dropped), so an actor does not stop facing a target
     * that stands above or below it.
     */
    float getActorFacingAngleTo(RE::Actor* actor, const RE::NiPoint3& point)
    {
        if (!actor) {
            return 0.0f;
        }
        const float dx = point.x - actor->data.location.x;
        const float dy = point.y - actor->data.location.y;
        if (std::abs(dx) < 0.01f && std::abs(dy) < 0.01f) {
            return 0.0f;
        }
        // atan2(x, y), not the usual (y, x), to match the engine's zero-along-+Y yaw
        constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
        float delta = std::atan2(dx, dy) - actor->data.angle.z;
        // wrap into [-pi, pi] so the comparison holds across the 0 / 2pi seam
        delta = std::fmod(delta + std::numbers::pi_v<float>, TWO_PI);
        if (delta < 0) {
            delta += TWO_PI;
        }
        return common::MatrixUtils::radsToDegrees(delta - std::numbers::pi_v<float>);
    }

    /**
     * Whether the actor is facing the given world point, within `halfAngleDegrees` to either side of where it
     * is pointed — see getActorFacingAngleTo for how facing is measured. 180 accepts any facing.
     */
    bool isActorFacing(RE::Actor* actor, const RE::NiPoint3& point, const float halfAngleDegrees)
    {
        if (!actor) {
            return false;
        }
        return halfAngleDegrees >= 180.0f || std::abs(getActorFacingAngleTo(actor, point)) <= halfAngleDegrees;
    }

    /**
     * How well the observer currently detects the target — the engine's own perception answer, so light,
     * distance, facing and sneak are all already accounted for. Priority is left at kNormal, what the game's
     * own callers use.
     *
     * The scale is continuous, not a flag. Measured in-game on VR 1.2.72:
     *   < 0    unaware of the target
     *   0..~20 aware — suspicious / investigating, but has not located the target
     *   >= ~20 has actually seen the target, climbing to roughly 50-60 at point blank
     * So "detected" depends on which question is being asked: >= 0 answers "does it suspect anything",
     * while a threshold around 20 answers "has it found me".
     *
     * This is the bundled Actor::RequestDetectionLevel, which takes and returns plain scalars. The engine also
     * has RequestDetectionLevels, which splits the answer into visual and sound channels — do not reach for it
     * without first establishing its return type; it returns through a hidden pointer that the symbol does not
     * describe, and guessing at the size overruns the caller's stack. See F4VROffsets.h.
     *
     * The call creates the observer's knowledge entry for the target, and that entry lives in the observer's
     * HIGH process data — an actor that has none has nowhere to create it, and is reported as not detecting.
     */
    int getDetectionLevel(RE::Actor* observer, RE::Actor* target)
    {
        if (!observer || !target || !observer->currentProcess || !observer->currentProcess->high) {
            return -1;
        }
        return observer->RequestDetectionLevel(target);
    }

    /**
     * Whether the actor is in combat with anyone and that combat is still live. Prefer this over the
     * Actor::IsInCombat() virtual, which does not report reliably on the VR runtime (always false in testing)
     * and, where it does work, stays true for a combat group that has already ended.
     *
     * Note this says nothing about WHO the actor is fighting. `Actor::currentCombatTarget` answers that, but it
     * is sticky — it holds the last target long after a fight ends — so it is only meaningful while this
     * returns true.
     */
    bool isInActiveCombat(RE::Actor* actor)
    {
        return actor && Actor_IsInActiveCombat(actor);
    }

    /**
     * Put `actor` into combat with `target`, right now. Note this creates hostility where there was none — an
     * actor with no reason to fight the target will fight it anyway, permanently — so callers should establish
     * that the two are already enemies before reaching for this.
     *
     * Combat lives in the actor's HIGH process data, so an actor without one cannot be put into it.
     *
     * `method` picks between three engine entry points, all verified working — see StartCombatMethod. Note
     * TaskQueue is deferred, so the actor is not yet in combat when this returns; the other two are immediate.
     * On an actor already in combat with the target every method does nothing, which is a no-op rather than a
     * failure.
     *
     * None of them files an assault crime the way Actor::AttackAlarm would — the target simply becomes this
     * actor's problem, with no bounty or witness propagation, which is what "it noticed me" should mean.
     */
    void startCombat(RE::Actor* actor, RE::Actor* target, const StartCombatMethod method)
    {
        if (!actor || !target) {
            return;
        }
        if (!actor->currentProcess || !actor->currentProcess->high) {
            logger::warn("startCombat: actor {:08X} has no high process - combat not started", actor->formID);
            return;
        }

        switch (method) {
        case StartCombatMethod::EnterCombat:
            AIProcess_EnterCombat(actor->currentProcess, actor, target, nullptr);
            break;
        case StartCombatMethod::ActorStartCombat:
            Actor_StartCombat(actor, target, nullptr);
            break;
        case StartCombatMethod::TaskQueue:
            if (const auto queue = RE::TaskQueueInterface::GetSingleton()) {
                TaskQueueInterface_QueueActorStartCombat(queue, actor, target, false);
            } else {
                logger::warn("startCombat: no TaskQueueInterface singleton - combat not started");
            }
            break;
        }
    }

    /**
     * Any BGSProjectile form, to satisfy APIs like CombatUtilities::CalculateProjectileLOS that only use the
     * projectile for collision-shape defaults. Null when no projectile forms are loaded.
     */
    RE::BGSProjectile* getAnyProjectile()
    {
        if (const auto dataHandler = RE::TESDataHandler::GetSingleton()) {
            const auto& projectiles = dataHandler->GetFormArray<RE::BGSProjectile>();
            if (!projectiles.empty()) {
                return projectiles.front();
            }
        }
        return nullptr;
    }

    /**
     * Get the "bLeftHandedMode:VR" setting from the INI file.
     * Direct memory access is A LOT faster than "RE::INIPrefSettingCollection::GetSingleton()->GetSetting("bLeftHandedMode:VR")->GetBinary();"
     */
    bool isLeftHandedMode()
    {
        // not sure why RE::Relocation doesn't work here, so using raw address
        static auto iniLeftHandedMode = reinterpret_cast<bool*>(REL::Offset(0x37d5e48).address()); // NOLINT(performance-no-int-to-ptr)
        return *iniLeftHandedMode;
    }

    /**
     * Return true if the pipboy is on the wrist, false if it is "in-front" or projected.
     */
    bool isPipboyOnWrist()
    {
        // not sure why RE::Relocation doesn't work here, so using raw address
        static auto iniAlwaysUseProjectedPipboy = reinterpret_cast<bool*>(REL::Offset(0x37B4280).address()); // NOLINT(performance-no-int-to-ptr)
        static auto iniAttachPipboyToHMD = reinterpret_cast<bool*>(REL::Offset(0x37B4298).address()); // NOLINT(performance-no-int-to-ptr)
        return !(*iniAlwaysUseProjectedPipboy || *iniAttachPipboyToHMD);
    }

    /**
     * Get the "bComfortSneak:VR" setting from the INI file.
     * Direct memory access is A LOT faster than "RE::INIPrefSettingCollection::GetSingleton()->GetSetting("bComfortSneak:VR")->GetBinary();"
     */
    bool isComfortSneakMode()
    {
        // not sure why RE::Relocation doesn't work here, so using raw address
        static auto iniComfortSneakMode = reinterpret_cast<bool*>(REL::Offset(0x37D6178).address()); // NOLINT(performance-no-int-to-ptr)
        return *iniComfortSneakMode;
    }

    /**
     * Get the "bUseWandDirectionalMovement" setting from the INI file.
     */
    bool useWandDirectionalMovement()
    {
        static auto iniUseWandDirectionalMovement = reinterpret_cast<bool*>(REL::Offset(0x37D6160).address()); // NOLINT(performance-no-int-to-ptr)
        return *iniUseWandDirectionalMovement;
    }

    /**
     * Get the INI setting by name.
     */
    RE::Setting* getIniSetting(const char* name, const bool addNew)
    {
        auto setting = RE::INIPrefSettingCollection::GetSingleton()->GetSetting(name);
        if (setting) {
            return setting;
        }

        const auto collection = RE::INISettingCollection::GetSingleton();
        setting = collection->GetSetting(name);
        if (setting) {
            return setting;
        }

        if (!addNew) {
            logger::warn("Setting '{}' not found in INI settings", name);
            return nullptr;
        }

        logger::warn("Setting '{}' not found in INI settings, adding new", name);
        RE::Setting newSetting("", 0);
        collection->Add(&newSetting);
        return collection->GetSetting(name);
    }

    RE::NiAVObject* getFirstChild(RE::NiAVObject* avObject)
    {
        if (avObject) {
            if (const auto& node = avObject->IsNode()) {
                if (!node->children.empty()) {
                    return node->children[0].get();
                }
            }
        }
        return nullptr;
    }

    /**
     * Find a node by the given name in the tree under the other given node recursively.
     */
    RE::NiAVObject* findAVObject(RE::NiAVObject* node, const std::string& name, const int maxDepth)
    {
        if (!node || maxDepth < 0) {
            return nullptr;
        }

        if (_stricmp(name.c_str(), node->name.c_str()) == 0) {
            return node;
        }

        if (const auto niNode = node->IsNode()) {
            for (const auto& child : niNode->children) {
                if (child) {
                    if (const auto result = findAVObject(child.get(), name, maxDepth - 1)) {
                        return result;
                    }
                }
            }
        }
        return nullptr;
    }

    /**
     * Find AV Object node by the given name prefix in the tree under the other given node recursively.
     * Returns the first node found that starts with the given name.
     */
    RE::NiAVObject* findAVObjectStartsWith(RE::NiAVObject* node, const char* name, const int maxDepth)
    {
        if (!node) {
            return nullptr;
        }

        if (_strnicmp(name, node->name.c_str(), std::strlen(name)) == 0) {
            return node;
        }

        if (maxDepth < 1) {
            return nullptr;
        }

        if (const auto niNode = node->IsNode()) {
            for (const auto& child : niNode->children) {
                if (child) {
                    if (const auto result = findAVObjectStartsWith(child.get(), name, maxDepth - 1)) {
                        return result;
                    }
                }
            }
        }
        return nullptr;
    }

    /**
     * Find a node by the given name in the tree under the other given node recursively.
     */
    RE::NiNode* findNode(RE::NiAVObject* node, const char* name, const int maxDepth)
    {
        if (!node) {
            return nullptr;
        }

        if (_stricmp(name, node->name.c_str()) == 0) {
            return node->IsNode();
        }

        if (maxDepth < 1) {
            return nullptr;
        }

        if (const auto niNode = node->IsNode()) {
            for (const auto& child : niNode->children) {
                if (child) {
                    if (const auto childNiNode = child->IsNode()) {
                        if (const auto result = findNode(childNiNode, name, maxDepth - 1)) {
                            return result;
                        }
                    }
                }
            }
        }
        return nullptr;
    }

    /**
     * Find a node by the given name prefix in the tree under the other given node recursively.
     * Returns the first node found that starts with the given name.
     */
    RE::NiNode* findNodeStartsWith(RE::NiAVObject* node, const char* name, const int maxDepth)
    {
        const auto avObj = findAVObjectStartsWith(node, name, maxDepth);
        return avObj ? avObj->IsNode() : nullptr;
    }

    /**
     * Get the first child node of the given node if exists.
     */
    RE::NiAVObject* get1StChildNode(RE::NiAVObject* node)
    {
        if (!node) {
            return nullptr;
        }
        if (const auto niNode = node->IsNode()) {
            if (!niNode->children.empty()) {
                return niNode->children[0].get();
            }
        }
        return nullptr;
    }

    /**
     * Find a node by name restricted to firest level of children only.
     */
    RE::NiNode* find1StChildNode(RE::NiAVObject* node, const char* name)
    {
        return findNode(node, name, 1);
    }

    /**
     * Return true if the node is visible, false if it is hidden or null.
     */
    bool isNodeVisible(const RE::NiAVObject* node)
    {
        return node && !(node->flags.flags & 0x1);
    }

    /**
     * Change flags to show or hide a node
     */
    void setNodeVisibility(RE::NiAVObject* node, const bool show)
    {
        if (node) {
            node->flags.flags = show ? (node->flags.flags & ~0x1) : (node->flags.flags | 0x1);
        }
    }

    /**
     * Change flags to show or hide a node and ALL of its children recursively.
     */
    void setNodeVisibilityDeep(RE::NiAVObject* node, const bool show, const bool updateSelf)
    {
        if (node && updateSelf) {
            setNodeVisibility(node, show);
        }
        if (const auto niNode = node->IsNode()) {
            for (const auto& child : niNode->children) {
                if (child) {
                    setNodeVisibilityDeep(child.get(), show, true);
                }
            }
        }
    }

    // TODO: this feels an overkill on how much it is called
    void updateDownFromRoot()
    {
        updateDown(getRootNode(), true);
    }

    void updateDown(RE::NiAVObject* node, const bool updateSelf, const char* ignoreNode)
    {
        if (!node) {
            return;
        }

        RE::NiUpdateData* ud = nullptr;
        if (updateSelf) {
            node->UpdateWorldData(ud);
        }

        if (const auto niNode = node->IsNode()) {
            for (const auto& child : niNode->children) {
                if (child) {
                    if (ignoreNode && _stricmp(child->name.c_str(), ignoreNode) == 0) {
                        continue; // skip this node
                    }
                    if (const auto childNiNode = child->IsNode()) {
                        updateDown(childNiNode, true);
                    } else if (const auto triNode = child->IsGeometry()) {
                        triNode->UpdateWorldData(ud);
                    }
                }
            }
        }
    }

    void updateDownTo(RE::NiNode* toNode, RE::NiNode* fromNode, const bool updateSelf)
    {
        if (!toNode || !fromNode) {
            return;
        }

        if (updateSelf) {
            RE::NiUpdateData* ud = nullptr;
            fromNode->UpdateWorldData(ud);
        }

        if (_stricmp(toNode->name.c_str(), fromNode->name.c_str()) == 0) {
            return;
        }

        for (const auto& child : fromNode->children) {
            if (child) {
                if (const auto childNiNode = child->IsNode()) {
                    updateDownTo(toNode, childNiNode, true);
                }
            }
        }
    }

    void updateUpTo(RE::NiNode* toNode, RE::NiNode* fromNode, const bool updateSelf)
    {
        if (!toNode || !fromNode) {
            return;
        }

        RE::NiUpdateData* ud = nullptr;

        if (_stricmp(toNode->name.c_str(), fromNode->name.c_str()) == 0) {
            if (updateSelf) {
                fromNode->UpdateWorldData(ud);
            }
            return;
        }

        fromNode->UpdateWorldData(ud);
        if (const auto parent = fromNode->parent ? fromNode->parent : nullptr) {
            updateUpTo(toNode, parent, true);
        }
    }

    /**
     * Update the world transform data (location,rotation,scale) of the given node by the local transform of the parent node.
     * Use to update the real position of nodes in the world after making local changes. May be better than "UpdateWorldData" call.
     */
    void updateTransforms(RE::NiAVObject* node)
    {
        if (!node->parent) {
            return;
        }

        const auto& parentTransform = node->parent->world;
        const auto& localTransform = node->local;

        // Calculate world position
        const RE::NiPoint3 pos = parentTransform.rotate.Transpose() * (localTransform.translate * parentTransform.scale);
        node->world.translate = parentTransform.translate + pos;

        // Calculate world rotation
        node->world.rotate = localTransform.rotate * parentTransform.rotate;

        // Calculate world scale
        node->world.scale = parentTransform.scale * localTransform.scale;
    }

    /**
     * Update the world transform data (location,rotation,scale) of every node in the tree by the local transform of its parent node.
     * Use to update the real position of nodes in the world after making local changes to a node in the hierarchy. Will propagate the change
     * to all the nodes down so a rotation of a node will affect all the child, grandchild, etc.
     * May be better than "updateDown" call as I saw not all nodes get updated with that one.
     */
    void updateTransformsDown(RE::NiAVObject* node, const bool updateSelf, const char* ignoreNode)
    {
        if (!node) {
            return;
        }

        if (updateSelf) {
            updateTransforms(node);
        }

        if (const auto niNode = node->IsNode()) {
            for (const auto& child : niNode->children) {
                if (child) {
                    if (ignoreNode && _stricmp(child->name.c_str(), ignoreNode) == 0) {
                        continue; // skip this node
                    }
                    if (const auto childNiNode = child->IsNode()) {
                        updateTransformsDown(childNiNode, true);
                    } else if (const auto childTriNode = child->IsTriShape()) {
                        updateTransforms(childTriNode);
                    }
                }
            }
        }
    }

    /**
     * Run a callback to register papyrus native functions.
     * Functions that papyrus can call into this mod c++ code.
     */
    void registerPapyrusNativeFunctions(F4SE::PapyrusInterface::RegisterFunctions callback)
    {
        const auto papyrusInterface = F4SE::GetPapyrusInterface();
        if (!papyrusInterface) {
            throw std::exception("Failed to get papyrus interface");
        }

        if (!papyrusInterface->Register(callback)) {
            throw std::exception("Failed to register papyrus functions");
        }
    }

    /**
     * Load .nif file from the filesystem and return the root node.
     */
    RE::NiNode* loadNifFromFile(const std::string& path)
    {
        uint64_t flags[2] = { 0x0, 0xed };
        uint64_t mem = 0;
        const auto resolvedPath = resolveNifPath(path);
        if (!std::filesystem::exists(resolvedPath)) {
            throw std::runtime_error("Load nif file failed, file not found: " + resolvedPath);
        }

        loadNif((uint64_t)resolvedPath.c_str(), (uint64_t)&mem, (uint64_t)&flags);
        return reinterpret_cast<RE::NiNode*>(mem);
    }

    /**
     * Get a RE::NiNode that can be used in game UI for the given .nif file.
     * Why is just loading not enough?
     */
    RE::NiNode* getClonedNiNodeForNifFile(const std::string& path)
    {
        const auto nifNode = loadNifFromFile(path);
        if (!nifNode) {
            throw std::runtime_error("Load nif file failed, nullptr returned for: " + path);
        }

        NiCloneProcess proc;
        proc.unk18 = reinterpret_cast<uint64_t*>(cloneAddr1.address());
        proc.unk48 = reinterpret_cast<uint64_t*>(cloneAddr2.address());
        const auto uiNode = cloneNode(nifNode, &proc);
        copyNodeNamesRecursive(nifNode, uiNode);
        return uiNode;
    }

    /**
     * Get a RE::NiNode that can be used in game UI for the given .nif file.
     * Why is just loading not enough?
     */
    RE::NiNode* getClonedNiNodeForNifFileSetName(const std::string& path, const std::string& name)
    {
        const auto uiNode = getClonedNiNodeForNifFile(path);
        uiNode->name = name.empty() ? path : name;
        return uiNode;
    }
}
