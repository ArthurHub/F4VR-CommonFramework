#include "F4VRUtils.h"

#include <cstring>

#include "ModBase.h"
#include "PlayerNodes.h"
#include "vrcf/VRControllersManager.h"

namespace
{
    /**
     * Resolve a UI nif path by probing several locations in order and returning the first that
     * exists on disk: the path as given, then under "Data/" (a path of the Data folder, as
     * "Meshes/MyMod/f4cf/activation-sphere.nif"), then under "Data/Meshes/", then under this mod's
     * own "Data/Meshes/<ModName>/" folder. The "Data/"-rooted candidates are returned verbatim so
     * the loader does not re-prefix them. When none exist the path is returned unchanged so the
     * subsequent load surfaces a clear not-found error.
     */
    std::string resolveNifPath(const std::string& path)
    {
        // Strip any leading "/" or "\" so the prefixed candidates don't end up with a doubled separator.
        const auto firstReal = path.find_first_not_of("/\\");
        const std::string relPath = firstReal == std::string::npos ? path : path.substr(firstReal);

        for (const auto& candidate : {
                 path,
                 "Data\\" + relPath,
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

        const auto count = (std::min)(sourceNode->children.size(), cloneNode->children.size());
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
        callPapyrusGlobalFunction("Debug", "Messagebox", text);
    }

    void showNotification(const std::string& text)
    {
        logger::info("Show notification: '{}'", text.c_str());
        callPapyrusGlobalFunction("Debug", "Notification", text);
    }

    /**
     * Whether an open game menu takes the controllers' input: one that pauses the game, uses the cursor or the menu
     * context, or is modal - the pause menu, a message box, the Pip-Boy, lockpicking, a container, other mods' menus.
     * The always-open HUD menus set none of these. Game thread.
     * @param withInputContext also a menu with an input context of its own, as workshop build mode, which the player
     * plays in: it pauses nothing and sets none of those flags
     */
    bool isGameMenuTakingInput(const bool withInputContext)
    {
        const auto ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }
        using enum RE::UI_MENU_FLAGS;
        return std::ranges::any_of(ui->menuStack, [withInputContext](const RE::Scaleform::Ptr<RE::IMenu>& menu) {
            return menu &&
                   (menu->menuFlags.any(kPausesGame, kUsesCursor, kUsesMenuContext, kModal) || (withInputContext && menu->inputContext != RE::UserEvents::INPUT_CONTEXT_ID::kNone));
        });
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
            return !RE::VRSettings::IsLeftHandedMode();
        case vrcf::Hand::Left:
            return RE::VRSettings::IsLeftHandedMode();
        default:
            return true;
        }
    }

    /**
     * Set the visibility of controller wand.
     */
    void setWandsVisibility(const bool show, const bool leftWand)
    {
        const auto node = leftWand ? getVRPlayerNodes()->primaryWandNode : getVRPlayerNodes()->secondaryWandNode;
        for (const auto& child : node->children) {
            if (child) {
                if (child->IsNiTriShape()) {
                    RE::NiNodeUtils::SetVisible(child.get(), show);
                    break;
                }
                if (!_stricmp(child->name.c_str(), "")) {
                    RE::NiNodeUtils::SetVisible(child.get(), show);
                    if (const auto grandChild = child->IsNode()) {
                        RE::NiNodeUtils::SetVisible(grandChild, show);
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
     * The weapon the player has equipped, null for fists. What Papyrus's Actor.GetEquippedWeapon returns, which is
     * Actor::GetCurrentWeapon's object.
     */
    RE::TESObjectWEAP* getEquippedWeapon()
    {
        auto* pc = getPlayer();
        const auto weapon = pc ? pc->GetCurrentWeapon(RE::BGSEquipIndex{ 0 }).object : nullptr;
        return weapon ? weapon->As<RE::TESObjectWEAP>() : nullptr;
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
        return getPlayer()->IsJumping();
    }

    bool isPlayerSneaking()
    {
        return getPlayer()->IsSneaking();
    }

    // Thanks Shizof and SmoothMovementVR for below code
    // TODO: compare with CommonLibF4VR's PowerArmor::ActorInPowerArmor (extra data and the power armor race, untested on VR),
    // especially while entering and leaving the frame, and use it if it agrees.
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
            actor->currentProcess->EnterCombat(actor, target);
            break;
        case StartCombatMethod::ActorStartCombat:
            actor->StartCombat(target);
            break;
        case StartCombatMethod::TaskQueue:
            if (const auto queue = RE::TaskQueueInterface::GetSingleton()) {
                queue->QueueActorStartCombat(*actor, *target, false);
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

    // TODO: this feels an overkill on how much it is called
    void updateDownFromRoot()
    {
        RE::NiNodeUtils::UpdateWorldDataDown(getRootNode());
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
     * Resolve a texture path the way a vrui NIF path is resolved: as given, then under Data\ (a path of the Data
     * folder, as "Textures\MyMod\icon.dds"), then under Data\Textures\, then under the mod's own
     * Data\Textures\<ModName>\, taking the first loose file that exists. The path comes back unchanged when none
     * does - a texture packed in a BA2 archive, or one that is simply missing, which the load then reports.
     */
    std::string resolveTexturePath(const std::string_view path)
    {
        const std::string given(path);
        // a leading separator would leave the prefixed candidates with a doubled one
        const auto firstReal = given.find_first_not_of("/\\");
        const std::string relative = firstReal == std::string::npos ? given : given.substr(firstReal);

        for (const auto& candidate : { given, "Data\\" + relative, "Data\\Textures\\" + relative, "Data\\Textures\\" + g_mod->getName() + "\\" + relative }) {
            std::error_code error;
            if (std::filesystem::exists(candidate, error)) {
                return candidate;
            }
        }
        return given;
    }

    /**
     * Load .nif file from the filesystem and return the root node, through the game's model database
     * (BSModelDB::Demand), which caches it by path. The caller holds one reference to the node.
     */
    RE::NiNode* loadNifFromFile(const std::string& path)
    {
        const auto resolvedPath = resolveNifPath(path);
        if (!std::filesystem::exists(resolvedPath)) {
            throw std::runtime_error("Load nif file failed, file not found: " + resolvedPath);
        }

        // prepare after load, error marker, process, load textures, and two flags past them the header leaves unnamed
        constexpr std::uint8_t LOAD_FLAGS = 0xED;
        RE::BSModelDB::DBTraits::ArgsType args{};
        std::memcpy(reinterpret_cast<std::uint8_t*>(&args) + 8, &LOAD_FLAGS, 1);

        RE::NiPointer<RE::NiNode> node;
        RE::BSModelDB::Demand(resolvedPath.c_str(), &node, args);
        if (node) {
            node->IncRefCount(); // the caller's reference, which outlives this NiPointer
        }
        return node.get();
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

        RE::NiCloningProcess cloning;
        const auto clone = nifNode->Clone(cloning);
        const auto uiNode = clone ? clone->IsNode() : nullptr;
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
