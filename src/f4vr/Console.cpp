#include "Console.h"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

#include <windows.h>

#include "../render/CallSiteHook.h"

namespace f4cf::f4vr::console
{
    namespace
    {
        constexpr std::size_t CALL_REL32_LENGTH = 5;
        constexpr std::size_t TRAMPOLINE_JUMP_SIZE = 14;

        // ConsoleLog::AddString's first instructions on Fallout 4 VR 1.2.72 (push r15; sub rsp, 30h; mov [rsp+40h], rbx),
        // which prove the build and the function address before the call to it is redirected
        constexpr std::array<std::uint8_t, 11> ADD_STRING_PROLOGUE{ 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x89, 0x5C, 0x24, 0x40 };

        using AddStringFn = void (*)(RE::ConsoleLog*, const char*);

        ConsoleLines s_lines;
        AddStringFn s_original = nullptr;
        std::atomic_bool s_installed = false;
        std::atomic_bool s_installFailed = false;

        /**
         * Text from one code page in another, through UTF-16; ASCII as it is. The console's text is in the game's ANSI
         * code page, and the framework's text is UTF-8.
         */
        std::string convert(const std::string_view text, const UINT from, const UINT to)
        {
            if (std::ranges::all_of(text, [](const char c) {
                    return static_cast<unsigned char>(c) < 0x80;
                })) {
                return std::string(text);
            }
            const int wideLength = MultiByteToWideChar(from, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (wideLength <= 0) {
                return std::string(text);
            }
            std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
            MultiByteToWideChar(from, 0, text.data(), static_cast<int>(text.size()), wide.data(), wideLength);
            const int length = WideCharToMultiByte(to, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
            if (length <= 0) {
                return std::string(text);
            }
            std::string converted(static_cast<std::size_t>(length), '\0');
            WideCharToMultiByte(to, 0, wide.data(), wideLength, converted.data(), length, nullptr, nullptr);
            return converted;
        }

        /**
         * Every line the console prints passes here, on whatever thread prints it: kept, then handed on to AddString, or
         * to the hook another plugin put on this call before. Nothing may throw into the game.
         */
        void hookedAddString(RE::ConsoleLog* log, const char* text)
        {
            if (text) {
                try {
                    s_lines.add(convert(text, CP_ACP, CP_UTF8));
                } catch (...) {
                    // a line lost is better than an exception through the engine
                }
            }
            s_original(log, text);
        }
    }

    /**
     * Starts copying the console's output, once: the call in ConsoleLog::Print that hands each line to AddString is
     * redirected through the mod's trampoline, chained onto another plugin's hook of the same call. The lines
     * waiting in the console's buffer come first: before the Console menu is first made, that is all the game printed,
     * as the menu empties the buffer as it takes lines. Game thread. Returns whether output is captured; a failed
     * install is logged once and not tried again.
     */
    bool startCapture()
    {
        if (s_installed.load(std::memory_order_acquire)) {
            return true;
        }
        if (s_installFailed.load(std::memory_order_acquire)) {
            return false;
        }

        static std::mutex installMutex;
        std::scoped_lock lock(installMutex);
        if (s_installed.load(std::memory_order_acquire)) {
            return true;
        }
        s_installFailed.store(true, std::memory_order_release);

        if (!REL::Module::IsVR()) {
            logger::warn("console capture not installed: its address is Fallout 4 VR only");
            return false;
        }
        const auto& site = RE::VRHookSites::ConsolePrintAddString;
        const auto addString = site.callee();
        if (std::memcmp(reinterpret_cast<const void*>(addString), ADD_STRING_PROLOGUE.data(), ADD_STRING_PROLOGUE.size()) != 0) {
            logger::warn("console capture not installed: ConsoleLog::AddString does not start with the expected instructions");
            return false;
        }
        const auto callSite = render::internal::inspectCallSite(site.address(), addString, "console capture");
        if (callSite.state == render::internal::CallSiteState::Invalid) {
            return false;
        }
        auto& trampoline = F4SE::GetTrampoline();
        if (trampoline.free_size() < TRAMPOLINE_JUMP_SIZE) {
            logger::error("console capture not installed: {} bytes of trampoline are free, it needs {}; allocate more in ModBase::Settings",
                trampoline.free_size(),
                TRAMPOLINE_JUMP_SIZE);
            return false;
        }

        if (const auto log = RE::ConsoleLog::GetSingleton(); log && !log->buffer.empty()) {
            s_lines.add(convert(std::string_view(log->buffer), CP_ACP, CP_UTF8));
        }

        // write_call hands back where the call pointed: AddString, or the hook already there, which calls through to it
        s_original = reinterpret_cast<AddStringFn>(trampoline.write_call<CALL_REL32_LENGTH>(site.address(), reinterpret_cast<std::uintptr_t>(&hookedAddString)));
        if (!s_original) {
            logger::warn("console capture not installed: the trampoline write returned no original");
            return false;
        }
        s_installFailed.store(false, std::memory_order_release);
        s_installed.store(true, std::memory_order_release);
        if (callSite.state == render::internal::CallSiteState::AlreadyHooked) {
            logger::info("Console capture installed, chained onto {}", render::internal::describeHookOwner(callSite.target));
        } else {
            logger::info("Console capture installed");
        }
        return true;
    }

    /**
     * Whether the console's output is copied. Any thread.
     */
    bool isCapturing()
    {
        return s_installed.load(std::memory_order_acquire);
    }

    /**
     * The lines the console printed since capture started, and those waiting in its buffer then. Any thread.
     */
    const ConsoleLines& lines()
    {
        return s_lines;
    }

    /**
     * Gives a console command, in UTF-8, to the game as typed into the console, after starting the capture. The game
     * prints it at once and runs it on a later frame, against the console's selected reference, so what it prints comes
     * later, in lines(). Returns the number of the last line before it, which its lines come after. Game thread.
     */
    std::uint64_t executeCommand(const std::string_view command)
    {
        startCapture();
        const auto before = s_lines.lastNumber();
        RE::Console::ExecuteCommand(convert(command, CP_UTF8, CP_ACP).c_str());
        return before;
    }
}
