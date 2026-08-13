#pragma once
#include "F4VROffsets.h"

namespace f4cf::vrcf
{
    enum class Hand : std::uint8_t;
}

namespace f4cf::f4vr
{
    static constexpr std::uint32_t KEYWORD_POWER_ARMOR = 0x4D8A1;
    static constexpr std::uint32_t KEYWORD_POWER_ARMOR_FRAME = 0x15503F;

    // UI
    void showMessagebox(const std::string& text);
    void showNotification(const std::string& text);

    // Controls
    void setControlsThumbstickEnableState(bool toEnable);
    void closeFavoriteMenu();

    // Weapons/Armor/Player
    bool isPrimaryHand(const vrcf::Hand hand);
    void setWandsVisibility(bool show, bool leftWand);
    bool isWeaponDrawn();
    bool isMeleeWeaponDrawn();
    bool isUnarmedWeaponDrawn();
    RE::TESObjectWEAP* getEquippedWeapon();
    std::string getEquippedWeaponName();
    std::string getEquippedWeaponInternalName();
    std::string getEquippedWeaponNameLegacy();
    bool hasKeyword(const RE::TESObjectARMO* armor, std::uint32_t keywordFormId);
    bool isJumpingOrInAir();
    bool isPlayerSneaking();
    bool isInPowerArmor();
    bool isInInternalCell();
    bool isSwimming(const RE::PlayerCharacter* player = RE::PlayerCharacter::GetSingleton());
    bool isUnderwater(const RE::PlayerCharacter* player = RE::PlayerCharacter::GetSingleton());
    bool isMovementSafe(RE::PlayerCharacter* player, const RE::NiPoint3& currentPos, const RE::NiPoint3& targetPos);
    RE::ProcessLists* getProcessLists();
    void getActorsWithinRangeOfPoint(const RE::NiPoint3& point, float radius, RE::BSScrapArray<RE::NiPointer<RE::Actor>>& outActors);
    float getActorFacingAngleTo(RE::Actor* actor, const RE::NiPoint3& point);
    bool isActorFacing(RE::Actor* actor, const RE::NiPoint3& point, float halfAngleDegrees);
    bool isInActiveCombat(RE::Actor* actor);

    int getDetectionLevel(RE::Actor* observer, RE::Actor* target);
    /**
     * Which engine entry point startCombat() uses. All three are verified working on VR 1.2.72; EnterCombat is
     * the default because it is the one the engine's own Papyrus SendAssaultAlarm calls. See F4VROffsets.h for
     * each one's provenance.
     *
     * Note none of them does anything to an actor already in combat with the target — that is a no-op, not a
     * failure, and is the first thing to check before concluding one of these is broken.
     */
    enum class StartCombatMethod : std::uint8_t
    {
        EnterCombat = 0, // AIProcess::EnterCombat - the engine's own path; takes effect immediately
        ActorStartCombat, // Actor::StartCombat - takes effect immediately
        TaskQueue, // TaskQueueInterface::QueueActorStartCombat - DEFERRED, lands a frame or more later
    };

    void startCombat(RE::Actor* actor, RE::Actor* target, StartCombatMethod method = StartCombatMethod::EnterCombat);
    RE::BGSProjectile* getAnyProjectile();

    // settings
    bool isLeftHandedMode();
    bool isPipboyOnWrist();
    bool isComfortSneakMode();
    bool useWandDirectionalMovement();
    RE::Setting* getIniSetting(const char* name, bool addNew = false);

    // nodes
    RE::NiAVObject* getFirstChild(RE::NiAVObject* avObject);
    RE::NiAVObject* findAVObject(RE::NiAVObject* node, const std::string& name, int maxDepth = 999);
    RE::NiAVObject* findAVObjectStartsWith(RE::NiAVObject* node, const char* name, const int maxDepth = 999);
    RE::NiNode* findNode(RE::NiAVObject* node, const char* name, int maxDepth = 999);
    RE::NiNode* findNodeStartsWith(RE::NiAVObject* node, const char* name, int maxDepth = 999);
    RE::NiAVObject* get1StChildNode(RE::NiAVObject* node);
    RE::NiNode* find1StChildNode(RE::NiAVObject* node, const char* name);

    // visibility
    bool isNodeVisible(const RE::NiAVObject* node);
    void setNodeVisibility(RE::NiAVObject* node, bool show);
    void setNodeVisibilityDeep(RE::NiAVObject* node, bool show, bool updateSelf = true);

    // updates
    void updateDownFromRoot();
    void updateDown(RE::NiAVObject* node, bool updateSelf, const char* ignoreNode = nullptr);
    void updateDownTo(RE::NiNode* toNode, RE::NiNode* fromNode, bool updateSelf);
    void updateUpTo(RE::NiNode* toNode, RE::NiNode* fromNode, bool updateSelf);
    void updateTransforms(RE::NiAVObject* node);
    void updateTransformsDown(RE::NiAVObject* node, bool updateSelf, const char* ignoreNode = nullptr);

    void registerPapyrusNativeFunctions(F4SE::PapyrusInterface::RegisterFunctions callback);

    // CommonLib migration
    RE::NiNode* loadNifFromFile(const std::string& path);
    RE::NiNode* getClonedNiNodeForNifFile(const std::string& path);
    RE::NiNode* getClonedNiNodeForNifFileSetName(const std::string& path, const std::string& name = "");
}
