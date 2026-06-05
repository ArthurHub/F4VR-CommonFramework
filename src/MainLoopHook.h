#pragma once

#include "ModBase.h"

namespace f4cf::main_hook
{
    namespace internal
    {
        // Hook into a random function call inside the game loop function
        // By overriding the address of one of the calls we can inject our own code to run
        // on every frame.
        using HookFunc = void (*)(uint64_t rcx);
        inline REL::Relocation _hookFuncCallSite{ REL::Offset(0xd8405e) };
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
     * Hook into the main game loop by overriding a function call inside it.
     */
    inline void hook()
    {
        logger::info("Hook into main loop at (0x{:X})...", internal::_hookFuncCallSite.address());
        auto& trampoline = F4SE::GetTrampoline();
        const auto hookFuncOriginal = trampoline.write_call<5>(internal::_hookFuncCallSite.address(), &internal::onGameFrameUpdateHook);

        internal::_hookFuncThisHook = internal::getCallTarget(internal::_hookFuncCallSite.address());
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
        const auto current = internal::getCallTarget(internal::_hookFuncCallSite.address());
        if (current != internal::_hookFuncThisHook) {
            logger::warn("Main Loop Hook validation failed! Expected: (0x{:X}), Current: (0x{:X})", internal::_hookFuncThisHook, current);
        }
    }
}
