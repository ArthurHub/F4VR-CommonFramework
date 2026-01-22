#include "F4VRUtils.h"

#include "PlayerNodes.h"
#include "f4sevr/Forms.h"
#include "f4sevr/PapyrusUtils.h"

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
    bool IsWeaponDrawn() { return getPlayer()->actorState.IsWeaponDrawn(); }

    /**
     * @return true if the equipped weapon is a melee weapon type.
     */
    bool isMeleeWeaponEquipped()
    {
        const auto player = getPlayer();
        if (!CombatUtilities_IsActorUsingMelee(player)) {
            return false;
        }
        const auto* inventory = player->inventoryList;
        if (!inventory) {
            return false;
        }
        return std::ranges::any_of(inventory->data,
            [](const auto& item) { return item.object->formType == RE::ENUM_FORM_ID::kWEAP && item.stackData->flags.any(RE::BGSInventoryItem::Stack::Flag::kSlotMask); });
    }

    /**
     * Get the game name of the equipped weapon.
     */
    std::string getEquippedWeaponName()
    {
        const auto* equipData = getPlayer()->middleProcess->unk08->equipData;
        return equipData ? equipData->item->GetFullName() : "";
    }

    bool hasKeyword(const F4SEVR::TESObjectARMO* armor, const std::uint32_t keywordFormId)
    {
        if (!armor) {
            return false;
        }
        for (std::uint32_t i = 0; i < armor->keywordForm.numKeywords; i++) {
            if (armor->keywordForm.keywords[i]) {
                if (armor->keywordForm.keywords[i]->formID == keywordFormId) {
                    return true;
                }
            }
        }
        return false;
    }

    bool isJumpingOrInAir() { return IsInAir(getPlayer()); }

    bool isPlayerSneaking() { return IsSneaking(getPlayer()); }

    // Thanks Shizof and SmoothMovementVR for below code
    bool isInPowerArmor()
    {
        const auto player = getPlayer();
        if ((player)->equipData) {
            if ((player)->equipData->slots[0x03].item != nullptr) {
                if (const auto equippedForm = (player)->equipData->slots[0x03].item) {
                    if (equippedForm->formType == RE::ENUM_FORM_ID::kARMO) {
                        if (const auto armor = reinterpret_cast<const F4SEVR::TESObjectARMO*>(equippedForm)) {
                            return hasKeyword(armor, KEYWORD_POWER_ARMOR) || hasKeyword(armor, KEYWORD_POWER_ARMOR_FRAME);
                        }
                    }
                }
            }
        }
        return false;
    }

    /**
     * Is the player is current in an "internal cell" as inside a building, cave, etc.
     */
    bool isInInternalCell() { return RE::PlayerCharacter::GetSingleton()->parentCell->IsInterior(); }

    /**
     * Is the player swimming either on the surface or underwater.
     */
    bool isSwimming(const RE::PlayerCharacter* player) { return player && static_cast<int>(player->DoGetCharacterState()) == 5; }

    /**
     * Is the player is currently underwater as detected by underwater timer being non-zero.
     */
    bool isUnderwater(const RE::PlayerCharacter* player) { return player && player->underWaterTimer > 0; }

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
    void updateDownFromRoot() { updateDown(getRootNode(), true); }

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
        auto& normPath = path._Starts_with("Data") ? path : "Data/Meshes/" + path;
        if (!std::filesystem::exists(normPath)) {
            throw std::runtime_error("Load nif file failed, file not found: " + normPath);
        }

        loadNif((uint64_t)normPath.c_str(), (uint64_t)&mem, (uint64_t)&flags);
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
        uiNode->name = nifNode->name;
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
