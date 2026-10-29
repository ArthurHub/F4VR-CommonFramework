#include "CallSiteHook.h"

#include <array>
#include <cstring>

#include <windows.h>

namespace f4cf::render::internal
{
    namespace
    {
        constexpr std::uint8_t CALL_REL32_OPCODE = 0xE8;
        constexpr std::size_t CALL_REL32_LENGTH = 5;

        /**
         * Whether the game executable's image contains the address. REL::Module exposes the base but
         * not the size, so the size comes from the image's own PE headers.
         */
        bool isInsideGameImage(const std::uintptr_t address)
        {
            const std::uintptr_t base = REL::Module::get().base();
            const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dosHeader->e_lfanew);
            return address >= base && address < base + ntHeaders->OptionalHeader.SizeOfImage;
        }

        /**
         * Whether `length` bytes at the address are committed, executable memory - checked before
         * reading another plugin's code, or trusting that a call into it is a hook.
         */
        bool isExecutableMemory(const std::uintptr_t address, const std::size_t length)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) != sizeof(info)) {
                return false;
            }
            constexpr DWORD EXECUTABLE = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            return info.State == MEM_COMMIT && (info.Protect & EXECUTABLE) != 0 && address + length <= regionEnd;
        }
    }

    /**
     * Say whether a 5-byte call site still calls the function, or has been hooked by another plugin,
     * before a hook patches it. The caller proves the function itself, by its prologue.
     *
     * The call has to land either on the function, untouched, or on executable code outside the game's
     * image - which can only be another plugin's hook of this call, and is chained onto rather than
     * refused: several framework mods, each with its own copy of the framework, hook the same calls.
     * A call landing anywhere else inside the game is not the call we expect, logged under hookName.
     */
    CallSite inspectCallSite(const std::uintptr_t callsite, const std::uintptr_t function, const std::string_view hookName)
    {
        const auto* callBytes = reinterpret_cast<const std::uint8_t*>(callsite);
        if (callBytes[0] != CALL_REL32_OPCODE) {
            logger::warn("{} not installed: no call instruction at the expected call site", hookName);
            return {};
        }

        std::int32_t relative = 0;
        std::memcpy(&relative, callBytes + 1, sizeof(relative));
        const std::uintptr_t target = callsite + CALL_REL32_LENGTH + static_cast<std::intptr_t>(relative);
        if (target == function) {
            return CallSite{ .state = CallSiteState::Untouched, .target = target };
        }
        if (!isInsideGameImage(target) && isExecutableMemory(target, 1)) {
            return CallSite{ .state = CallSiteState::AlreadyHooked, .target = target };
        }

        logger::warn("{} not installed: the call site targets neither the expected function nor another plugin's hook", hookName);
        return {};
    }

    /**
     * Name the module an existing hook's code belongs to, for the log. A hook written through a
     * trampoline lands first on a stub - jmp qword ptr [rip+0] followed by the absolute address -
     * in memory no module owns, so one such stub is followed to the detour behind it.
     */
    std::string describeHookOwner(std::uintptr_t target)
    {
        constexpr std::array<std::uint8_t, 6> ABSOLUTE_JMP{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
        if (isExecutableMemory(target, ABSOLUTE_JMP.size() + sizeof(std::uintptr_t)) &&
            std::memcmp(reinterpret_cast<const void*>(target), ABSOLUTE_JMP.data(), ABSOLUTE_JMP.size()) == 0) {
            std::memcpy(&target, reinterpret_cast<const void*>(target + ABSOLUTE_JMP.size()), sizeof(target));
        }

        HMODULE module = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(target), &module)) {
            return "memory owned by no module";
        }
        std::array<char, MAX_PATH> path{};
        const DWORD length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
        const std::string_view fullPath(path.data(), length);
        const auto separator = fullPath.find_last_of("\\/");
        return std::string(separator == std::string_view::npos ? fullPath : fullPath.substr(separator + 1));
    }
}
