// A guard of its own, not #pragma once: a mod can have two copies of this file, its own and its framework's
#ifndef MODS_CONFIG_VR_API_V1_H
#define MODS_CONFIG_VR_API_V1_H

#include <Windows.h>
#include <cstdint>

// ----------------------------------------------------------------------------------------
// Mods Config VR's native API, for the DLL of a mod that has a config spec, or a config UI of its own.
// Copy this whole file into your project AS IS.
//
// Everything that passes between the DLLs is C: whole numbers, float, bool, enums, UTF-8 texts that end with a
// zero, lists as a pointer and a count, and function pointers with a ctx the client gets back. What the host
// passes is valid during the call, so copy what you keep. What you pass the host copies before the call returns.
// Every call, either way, is on the game's main thread.
//
// A client function that throws or faults is cut off until the game is restarted, and its mod works as one with
// no client.
//
// EXAMPLE USAGE, from F4SE's kPostLoad on, when every plugin's DLL is loaded:
//
// {
//     using namespace mods_config::api;
//     if (const int err = ModsConfigVRApiV1::initialize(); err != 0) {
//         return; // Mods Config VR is not installed: the mod works without it
//     }
//     const ClientInfo info{ .version = MODS_CONFIG_VR_API_VERSION, .name = "MyMod", .ctx = nullptr };
//     Client* client = ModsConfigVRApiV1::inst->registerClient(&info);
//
//     // the options of the choice of the setting "preset" of MyMod's config spec, which has listFromClientCallback
//     ModsConfigVRApiV1::inst->registerOptions(client, "preset", [](void* ctx, OptionsCall* call) {
//         const SettingValue value{ .type = ValueType::String, .text = "default" };
//         call->addOption(call, &value, "Default preset");
//     }, nullptr);
// }

namespace mods_config::api
{
#define MODS_CONFIG_VR_CALL __cdecl

    // The version of this header: what V1 has. A function is only added at the end of the API, and a struct only
    // grows at its end, so a host of a later version takes a client of this one.
    inline constexpr std::uint32_t MODS_CONFIG_VR_API_VERSION = 1;

    // the host's DLL, and the function it exports that gives the API
    inline constexpr auto MODS_CONFIG_VR_DLL = "ModsConfigVR.dll";
    inline constexpr auto MODS_CONFIG_VR_GET_API = "ModsConfigVRAPI_GetApiV1";

    /**
     * The type of a value, as a setting of a config spec has its `valueType`.
     */
    enum class ValueType : std::uint32_t
    {
        Bool,
        Int, // signed 32-bit
        Float, // 32-bit
        String
    };

    /**
     * A value: its type, and the field of its type. A String is its text.
     */
    struct SettingValue
    {
        ValueType type = ValueType::String;
        const char* text = nullptr;
        bool boolValue = false;
        std::int32_t intValue = 0;
        float floatValue = 0;
    };

    /**
     * A setting of the client's config spec.
     */
    struct SettingView
    {
        const char* id; // unique in the config spec
        ValueType valueType;
    };

    /**
     * The host's handle of a client, which every call of the client takes.
     */
    struct Client;

    /**
     * What a client registers with: its name, which is matched to the config spec whose `modName` is that name
     * without regard to case, and the version of its header, so the host reads only the fields it has.
     */
    struct ClientInfo
    {
        std::uint32_t version = MODS_CONFIG_VR_API_VERSION;
        const char* name = nullptr;
        void* ctx = nullptr; // given back to each of its callbacks
    };

    /**
     * A call of the options of a choice: the setting, and how the client gives each of its options.
     */
    struct OptionsCall
    {
        const SettingView* setting;

        // Adds an option, after those added before. Its value is of the setting's valueType, and an option of
        // another type is left out. A null text shows the value. A text can be a `$` token of the game's language.
        void(MODS_CONFIG_VR_CALL* addOption)(OptionsCall* call, const SettingValue* value, const char* text);
    };

    // gives the options of a choice with listFromClientCallback, as its mod is selected
    using OptionsFn = void(MODS_CONFIG_VR_CALL*)(void* ctx, OptionsCall* call);

    struct ModsConfigVRApiV1
    {
        /**
         * The version of the host's API: what V1 has. Check it before you call what a later version added.
         */
        std::uint32_t(MODS_CONFIG_VR_CALL* getVersion)();

        /**
         * Mods Config VR's own version, as "1.0.0".
         */
        const char*(MODS_CONFIG_VR_CALL* getModVersion)();

        /**
         * Registers the mod's client, once, as it loads: keep the handle for the whole run of the game.
         * Null for a name that has a client already, and for an info with no name.
         */
        Client*(MODS_CONFIG_VR_CALL* registerClient)(const ClientInfo* info);

        /**
         * Registers the callback that gives the options of a choice of the client's config spec, by the setting's id.
         * One function can serve several settings. False for a setting that has one already, which keeps the first.
         */
        bool(MODS_CONFIG_VR_CALL* registerOptions)(Client* client, const char* settingId, OptionsFn options, void* ctx);

        /**
         * Finds Mods Config VR's DLL and gets its API. Returns 0 when the API is there, and else:
         * 1 for a game with no Mods Config VR, 2 for one that does not export the API, 3 for one that gives none,
         * and 4 for an API older than minVersion.
         */
        [[nodiscard]] static int initialize(const std::uint32_t minVersion = MODS_CONFIG_VR_API_VERSION)
        {
            if (inst) {
                return 0;
            }
            const auto dll = GetModuleHandleA(MODS_CONFIG_VR_DLL);
            if (!dll) {
                return 1;
            }
            const auto getApi = reinterpret_cast<const ModsConfigVRApiV1*(MODS_CONFIG_VR_CALL*)()>(GetProcAddress(dll, MODS_CONFIG_VR_GET_API));
            if (!getApi) {
                return 2;
            }
            const auto api = getApi();
            if (!api) {
                return 3;
            }
            if (api->getVersion() < minVersion) {
                return 4;
            }
            inst = api;
            return 0;
        }

        /**
         * The API, after initialize returned 0.
         */
        inline static const ModsConfigVRApiV1* inst = nullptr;
    };
}

#endif
