#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace f4cf::render::internal
{
    /**
     * What a game call site holds before a hook patches it.
     */
    enum class CallSiteState : std::uint8_t
    {
        // not the call we expect; nothing may be written
        Invalid,
        // the engine's own call, straight to the function
        Untouched,
        // already redirected by another plugin's hook of the same call, which the patch chains onto
        AlreadyHooked,
    };

    struct CallSite
    {
        CallSiteState state = CallSiteState::Invalid;
        // where the call lands now: the function, or the hook already there
        std::uintptr_t target = 0;
    };

    CallSite inspectCallSite(std::uintptr_t callsite, std::uintptr_t function, std::string_view hookName);

    std::string describeHookOwner(std::uintptr_t target);
}
