#pragma once

#include "GameRuntime.h"
#include "ModBase.h"

namespace f4cf::main_hook
{
    namespace internal
    {
        // Redirect a call inside the game's main loop, whose site the runtime gives (GameRuntime::frameCallSite), to run
        // the mod's frame before the call it replaces.
        using HookFunc = void (*)(uint64_t rcx);
        inline std::uintptr_t _hookFuncCallSite = 0;
        inline HookFunc _hookFuncOriginal = nullptr;
        inline uintptr_t _hookFuncThisHook = 0;

        /**
         * Read the destination of a CALL at address
         */
        static std::uintptr_t getCallTarget(const std::uintptr_t address)
        {
            const std::int32_t rel = *reinterpret_cast<std::int32_t*>(address + 1); // NOLINT(performance-no-int-to-ptr)
            return address + 5 + rel;
        }

        /**
         * Hook handler inside game frame update tick.
         */
        inline void onGameFrameUpdateHook(const std::uint64_t rcx)
        {
            g_mod->onFrameUpdateSafe();
            _hookFuncOriginal(rcx);
        }
    }

    /**
     * Hook into the main game loop by overriding a function call inside it. A runtime that gives no site, as one whose
     * call is not what the game has there, gets no frame: it is logged, and the game is left alone.
     */
    inline void hook()
    {
        internal::_hookFuncCallSite = GameRuntime::get().frameCallSite();
        if (!internal::_hookFuncCallSite) {
            logger::error("Main loop hook: the game's call is not at its place, so the mod gets no frame update");
            return;
        }
        logger::info("Hook into main loop at (0x{:X})...", internal::_hookFuncCallSite);
        auto& trampoline = F4SE::GetTrampoline();
        const auto hookFuncOriginal = trampoline.write_call<5>(internal::_hookFuncCallSite, &internal::onGameFrameUpdateHook);

        internal::_hookFuncThisHook = internal::getCallTarget(internal::_hookFuncCallSite);
        logger::info("Hook into main loop successful, original: (0x{:X}), new: (0x{:X})", hookFuncOriginal, internal::_hookFuncThisHook);
        internal::_hookFuncOriginal = reinterpret_cast<internal::HookFunc>(hookFuncOriginal); // NOLINT(performance-no-int-to-ptr)
    }

    /**
     * Validate that the hook was NOT overridden by another mod.
     * It is possible that another mod will override the hook but also respect the latest set address like this code dose.
     * In that case both mods will run on every frame. The warning is to help debugging if the other mods is not written well.
     */
    inline void validate()
    {
        if (!internal::_hookFuncCallSite) {
            return;
        }
        const auto current = internal::getCallTarget(internal::_hookFuncCallSite);
        if (current != internal::_hookFuncThisHook) {
            logger::warn("Main Loop Hook validation failed! Expected: (0x{:X}), Current: (0x{:X})", internal::_hookFuncThisHook, current);
        }
    }
}
