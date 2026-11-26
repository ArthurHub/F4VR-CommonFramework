#pragma once

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
    bool isGameMenuTakingInput(bool withInputContext = false);

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
    bool isMovementSafe(RE::PlayerCharacter* player, const RE::NiPoint3& currentPos, const RE::NiPoint3& targetPos);

    /**
     * Which engine entry point startCombat() uses. All three are verified working on VR 1.2.72; EnterCombat is
     * the default because it is the one the engine's own Papyrus SendAssaultAlarm calls. CommonLibF4VR's
     * AIProcess::EnterCombat, Actor::StartCombat and TaskQueueInterface::QueueActorStartCombat say where each comes from.
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
    RE::Setting* getIniSetting(const char* name, bool addNew = false);

    // nodes
    void updateDownFromRoot();

    void registerPapyrusNativeFunctions(F4SE::PapyrusInterface::RegisterFunctions callback);

    /**
     * Queue a call to a global Papyrus function, as Debug.Notification, without waiting for it. The arguments must match its
     * Papyrus declaration (bool, int, float, string, arrays as std::vector). Returns false, logged, when the VM won't take it.
     */
    template <class... Args>
    bool callPapyrusGlobalFunction(const char* scriptName, const char* functionName, Args... args)
    {
        const auto gameVM = RE::GameVM::GetSingleton();
        const auto vm = gameVM ? gameVM->GetVM() : nullptr;
        if (!vm || !vm->DispatchStaticCall(scriptName, functionName, nullptr, args...)) {
            logger::error("Failed to call Papyrus function '{}.{}'", scriptName, functionName);
            return false;
        }
        return true;
    }

    // CommonLib migration
    std::string resolveTexturePath(std::string_view path);
    RE::NiNode* loadNifFromFile(const std::string& path);
    RE::NiNode* getClonedNiNodeForNifFile(const std::string& path);
    RE::NiNode* getClonedNiNodeForNifFileSetName(const std::string& path, const std::string& name = "");
}
