// A guard of its own, not #pragma once: a mod can have two copies of this file, its own and its framework's
#ifndef MODS_CONFIG_VR_API_V1_H
#define MODS_CONFIG_VR_API_V1_H

#include <Windows.h>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// ----------------------------------------------------------------------------------------
// Mods Config VR's native API, for the DLL of a mod that has a config spec, or a config UI of its own.
// Copy this whole file into your project AS IS.
//
// Two layers. What passes between the DLLs is two interfaces of virtual functions: ModsConfigVRApiV1, which Mods
// Config VR implements, and IClient, which a client implements and registers once. The menu asks the client by name
// for its functions, states and options. Their arguments are plain: whole numbers, float, bool, enums, UTF-8 texts
// that end with a zero, and pointers. No std:: type crosses: each DLL has its own heap, and its own layout of them.
// What the host passes is valid during the call, so copy what you keep. What you pass the host copies before the
// call returns.
// ModsConfigVRClient, at the end of this file, is an IClient over your lambdas, compiled into your DLL. Use it, and
// the interfaces only for what it does not wrap. Every call, either way, is on the game's main thread.
//
// A client method that throws or faults cuts the client off until the game is restarted, and its mod works as one
// with no client.
//
// EXAMPLE USAGE:
//
// {
//     using namespace mods_config::api;
//     auto& modsConfig = ModsConfigVRClient::get();
//
//     // at any time, also before it connects
//     modsConfig.addFunction("calibrate", [](const Params& params) {
//         return FunctionResult{ .done = true, .message = std::format("Done: {:.2f} m.", measure(params.getInt("samples", 10))) };
//     });
//     modsConfig.addState("calibrating", [] { return std::optional<Value>(isCalibrating()); });
//
//     // from F4SE's kPostLoad on, when every plugin's DLL is loaded: 0 while Mods Config VR is there
//     const int err = modsConfig.connect("MyMod");
// }

namespace mods_config::api
{
#define MODS_CONFIG_VR_CALL __cdecl

    // The version of this header: what V1 has. A method is only added at the end of an interface, and a struct only
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
     * A parameter of a function: its name, and its value in its own JSON type in the config spec.
     */
    struct NamedValue
    {
        const char* name;
        SettingValue value;
    };

    /**
     * Whether the menu is closed, open, or open and minimized.
     */
    enum class MenuState : std::uint32_t
    {
        Closed,
        Open,
        Minimized
    };

    /**
     * What happened in the menu: it opened or closed, which every client is told, and the client's mod or a page of its
     * config spec that has an `id` was shown or left. A minimized menu is still open.
     */
    enum class MenuEventType : std::uint32_t
    {
        MenuOpened,
        MenuClosed,
        ModShown,
        ModLeft,
        PageShown,
        PageLeft
    };

    struct MenuEvent
    {
        MenuEventType type;
        const char* pageId; // the page's `id` in the config spec, for PageShown and PageLeft, and null for the others
    };

    /**
     * A call of a function of the client, which the host passes it: its parameters, and how the function gives the
     * message of its result.
     */
    class IFunctionCall
    {
    public:
        virtual std::uint32_t paramCount() = 0;
        virtual const NamedValue* param(std::uint32_t index) = 0;

        // What the help line shows of the result: after the info mark, or after the invalid mark when the function
        // returns false. A text can be a `$` token of the game's language.
        virtual void setMessage(const char* text) = 0;

    protected:
        ~IFunctionCall() = default;
    };

    /**
     * Where the client gives the options of a choice, which the host passes it.
     */
    class IOptions
    {
    public:
        // Adds an option, after those added before. Its value is of the setting's valueType, and an option of another
        // type is left out. A null text shows the value. A text can be a `$` token of the game's language.
        virtual void add(const SettingValue* value, const char* text) = 0;

    protected:
        ~IOptions() = default;
    };

    /**
     * What a client implements, which the menu asks by name. It is registered once, and lives for the whole run of the
     * game. Each method answers at once.
     */
    class IClient
    {
    public:
        // whether it has a function of a name, which a `CallNativeFunction` of its config spec runs: a button of one it
        // does not have is disabled
        virtual bool hasFunction(const char* name) = 0;

        // runs a function, with the button's params: true when it did what it does. Work that takes longer goes on after
        virtual bool runFunction(const char* name, IFunctionCall* call) = 0;

        // answers a state, which a `clientState` condition asks once a frame while the mod is shown, in value: false
        // while it is not available. It changes nothing. A String's text stays valid until a state is asked next
        virtual bool answerState(const char* name, SettingValue* value) = 0;

        // gives the options of a choice with `listFromClientCallback`, as the mod is selected: false for one it has none of
        virtual bool giveOptions(const SettingView* setting, IOptions* options) = 0;

        // what happens in the menu, as it happens
        virtual void onEvent(const MenuEvent* event) = 0;

    protected:
        ~IClient() = default;
    };

    /**
     * Mods Config VR's side of the API, which its DLL exports.
     */
    class ModsConfigVRApiV1
    {
    public:
        /**
         * The version of the host's API: what V1 has. Check it before you call what a later version added.
         */
        virtual std::uint32_t getVersion() const = 0;

        /**
         * Mods Config VR's own version, as "1.0.0".
         */
        virtual const char* getModVersion() const = 0;

        /**
         * Adds an entry of the client to the quick launch section of the menu's front page: its text, its help, which
         * the help line shows, the text of its button, null for Open, and the function of the client a press runs, which
         * opens the mod's own UI. A client can add an entry for each of its functions. A text can be a `$` token of the
         * game's language. False for a client that is not registered, and for a function that has an entry already,
         * which keeps the first.
         */
        virtual bool addQuickLaunch(IClient* client, const char* text, const char* help, const char* buttonText, const char* functionName) const = 0;

        /**
         * Registers a client, once, under its mod's name, which is matched to the config spec whose `modName` is that
         * name without regard to case, with the version of its header. False for a name that has a client already, which
         * keeps the first.
         */
        virtual bool registerClient(const char* name, std::uint32_t version, IClient* client) const = 0;

        /**
         * Whether the menu is closed, open, or minimized. Any DLL can ask, with or without a client.
         */
        virtual MenuState getMenuState() const = 0;

        /**
         * Opens the menu at a mod, and at a page of its config spec by its `id`: at the mod's front page with no page,
         * and at the menu's front page with no mod. A minimized menu is shown, and one that shows a mod with unsaved
         * changes asks first. False for a mod or a page that is not there, with nothing opened.
         */
        virtual bool openMenu(const char* modName, const char* pageId) const = 0;

        /**
         * Minimizes the menu, as a function of the client opens the mod's own UI. Its editing session stays.
         */
        virtual void minimizeMenu() const = 0;

        /**
         * Shows the menu again where it was, as the mod's own UI closes, and reads the shown mod's values again: only
         * while it is minimized, so a menu the player showed or closed is left as it is.
         */
        virtual void restoreMenu() const = 0;

        /**
         * Reads the values of the client's mod again while it is shown, after the mod changed them: a clean setting
         * takes its new value, and a pending one stays the player's.
         */
        virtual void reloadValues(IClient* client) const = 0;

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

    protected:
        ~ModsConfigVRApiV1() = default;
    };

    // ----------------------------------------------------------------------------------------
    // The C++ side of the API, compiled into the client's DLL: nothing of it crosses to the host.

    /**
     * A value that passes to and from the menu: a parameter's, an option's or a state's.
     */
    using Value = std::variant<bool, int, float, std::string>;

    /**
     * The named parameters a button of the config spec runs a function with, each in its own JSON type.
     */
    class Params
    {
    public:
        explicit Params(IFunctionCall* call)
            : _call(call)
        {}

        /**
         * A parameter by its name, in its own type. None for one the button does not give.
         */
        std::optional<Value> get(const std::string_view name) const
        {
            for (std::uint32_t i = 0; i < _call->paramCount(); ++i) {
                const auto* param = _call->param(i);
                if (!param || !param->name || name != param->name) {
                    continue;
                }
                switch (param->value.type) {
                case ValueType::Bool:
                    return param->value.boolValue;
                case ValueType::Int:
                    return static_cast<int>(param->value.intValue);
                case ValueType::Float:
                    return param->value.floatValue;
                case ValueType::String:
                    break;
                }
                return std::string(param->value.text ? param->value.text : "");
            }
            return std::nullopt;
        }

        bool getBool(const std::string_view name, const bool fallback = false) const
        {
            const auto value = get(name);
            const auto* flag = value ? std::get_if<bool>(&*value) : nullptr;
            return flag ? *flag : fallback;
        }

        /**
         * A whole number, or the fallback for one that is not given or no number. A number is cut to its whole part.
         */
        int getInt(const std::string_view name, const int fallback = 0) const
        {
            const auto value = get(name);
            if (const auto* number = value ? std::get_if<int>(&*value) : nullptr) {
                return *number;
            }
            const auto* real = value ? std::get_if<float>(&*value) : nullptr;
            return real ? static_cast<int>(*real) : fallback;
        }

        /**
         * A number, a whole one too, or the fallback for one that is not given or no number.
         */
        float getFloat(const std::string_view name, const float fallback = 0.0f) const
        {
            const auto value = get(name);
            if (const auto* real = value ? std::get_if<float>(&*value) : nullptr) {
                return *real;
            }
            const auto* number = value ? std::get_if<int>(&*value) : nullptr;
            return number ? static_cast<float>(*number) : fallback;
        }

        std::string getString(const std::string_view name, std::string fallback = {}) const
        {
            const auto value = get(name);
            const auto* text = value ? std::get_if<std::string>(&*value) : nullptr;
            return text ? *text : std::move(fallback);
        }

    private:
        IFunctionCall* _call;
    };

    /**
     * What a function did: whether it did what it does, and the message the help line shows of it, which can be none.
     */
    struct FunctionResult
    {
        bool done = true;
        std::string message;
    };

    /**
     * An option of a choice: its value, in the setting's type, and what the player sees, which is the value with none.
     */
    struct Option
    {
        Value value;
        std::string text;
    };

    /**
     * An entry of the quick launch section of the menu's front page, which runs a function of the mod.
     */
    struct QuickLaunch
    {
        std::string text;
        std::string help;

        // the text on its button: empty for Open
        std::string buttonText;
    };

    using ClientFunction = std::function<FunctionResult(const Params& params)>;
    using ClientState = std::function<std::optional<Value>()>;
    using ClientOptions = std::function<std::vector<Option>()>;
    using MenuEventHandler = std::function<void(MenuEventType event, std::string_view pageId)>;

    /**
     * The client of the mod's DLL, one in the DLL: an IClient over the mod's lambdas. It connects under the mod's name,
     * which is matched to the config spec whose `modName` it is, and answers the menu with what the mod adds, at any
     * time: its functions, states, options of choices, quick launch entries and handlers of the menu's events. While
     * Mods Config VR is not installed nothing is called.
     */
    class ModsConfigVRClient final : public IClient
    {
    public:
        static ModsConfigVRClient& get()
        {
            static ModsConfigVRClient client;
            return client;
        }

        ModsConfigVRClient(const ModsConfigVRClient&) = delete;
        ModsConfigVRClient& operator=(const ModsConfigVRClient&) = delete;

        /**
         * Connects to the menu under the mod's name, once, from F4SE's kPostLoad on, with the quick launch entries added
         * before. With the minVersion of 1 it connects to any menu of V1, so a client of a newer header
         * keeps working with an older menu: what a later version added is used after a check of `apiVersion()`, which
         * this class does too. A mod that does not work at all with an older menu passes the version it needs, and a
         * menu whose API is older is not connected to. Returns 0 when it is connected, initialize's code while the API is
         * not there or is older, and 5 when the menu refused the name, which has a client already: its log says why.
         */
        int connect(const std::string& name, const std::uint32_t minVersion = 1)
        {
            if (_api) {
                return 0;
            }
            if (const int err = ModsConfigVRApiV1::initialize(minVersion); err != 0) {
                return err;
            }
            if (!ModsConfigVRApiV1::inst->registerClient(name.c_str(), MODS_CONFIG_VR_API_VERSION, this)) {
                return 5;
            }
            _api = ModsConfigVRApiV1::inst;
            _name = name;
            for (const auto& [function, entry] : _quickLaunch) {
                sendQuickLaunch(function, entry);
            }
            return 0;
        }

        // the API, for what this class does not wrap: null until it is connected
        const ModsConfigVRApiV1* api() const
        {
            return _api;
        }

        bool isConnected() const
        {
            return _api != nullptr;
        }

        /**
         * The version of the connected menu's API, and 0 until it is connected: a mod uses what a later version added
         * only while it is that version or later, as `if (client.apiVersion() >= 2)`.
         */
        std::uint32_t apiVersion() const
        {
            return _api ? _api->getVersion() : 0;
        }

        /**
         * Adds a function, which a `CallNativeFunction` of the config spec runs by its name, with its params. It returns
         * at once. False for a name it has already, which keeps the first.
         */
        bool addFunction(std::string name, ClientFunction function)
        {
            return _functions.try_emplace(std::move(name), std::move(function)).second;
        }

        /**
         * Adds a state, which a `clientState` condition of the config spec asks by its name, once a frame while the mod
         * is shown: it answers at once and changes nothing. None while it is not available.
         */
        bool addState(std::string name, ClientState state)
        {
            return _states.try_emplace(std::move(name), std::move(state)).second;
        }

        /**
         * Adds the options of a choice with `listFromClientCallback`, by the setting's id, which the menu asks as the mod
         * is selected.
         */
        bool addOptions(std::string settingId, ClientOptions options)
        {
            return _options.try_emplace(std::move(settingId), std::move(options)).second;
        }

        /**
         * Adds an entry of the quick launch section of the menu's front page and the function a press on it runs, by
         * its name, which a `CallNativeFunction` of the config spec can run too. The function's message is told in the
         * help line, in red when it failed. A function that opens a UI of the mod's own minimizes the menu, and the mod
         * restores it as the UI closes. False for a name it has a function of already, with nothing added.
         */
        bool addQuickLaunch(std::string name, QuickLaunch entry, ClientFunction function)
        {
            if (!addFunction(name, std::move(function))) {
                return false;
            }
            if (_api) {
                return sendQuickLaunch(name, entry);
            }
            _quickLaunch.emplace_back(std::move(name), std::move(entry));
            return true;
        }

        /**
         * Minimizes the menu, with its editing session kept, as the mod opens a UI of its own. Nothing while it is not
         * connected.
         */
        void minimizeMenu() const
        {
            if (_api) {
                _api->minimizeMenu();
            }
        }

        /**
         * Shows the menu again, with the shown mod's values read again. Nothing while it is not connected, or while the
         * menu is not minimized, as the player showed or closed it.
         */
        void restoreMenu() const
        {
            if (_api) {
                _api->restoreMenu();
            }
        }

        /**
         * Reads the mod's values again while it is shown in the menu, after the mod changed them.
         */
        void reloadValues()
        {
            if (_api) {
                _api->reloadValues(this);
            }
        }

        /**
         * Opens the menu at the mod, at a page of its config spec by its `id`, or at its front page with none. False
         * while it is not connected, and for a page the config spec does not have.
         */
        bool openMenu(const std::string_view pageId = {}) const
        {
            if (!_api) {
                return false;
            }
            const std::string page(pageId);
            return _api->openMenu(_name.c_str(), page.empty() ? nullptr : page.c_str());
        }

        /**
         * Calls a handler with each event of the menu: it opened and closed, and the mod and a page of its config spec
         * that has an `id` were shown and left.
         */
        void subscribeToMenuEvents(MenuEventHandler handler)
        {
            _menuEventHandlers.push_back(std::move(handler));
        }

        bool isMenuOpen() const
        {
            return _menuOpen;
        }

        /**
         * The `id` of the page of the config spec that is shown, and none while none with an `id` is.
         */
        std::optional<std::string_view> shownPage() const
        {
            return _shownPage ? std::optional<std::string_view>(*_shownPage) : std::nullopt;
        }

    private:
        ModsConfigVRClient() = default;
        ~ModsConfigVRClient() = default;

        /**
         * Gives the connected menu a quick launch entry of a function.
         */
        bool sendQuickLaunch(const std::string& function, const QuickLaunch& entry) const
        {
            return _api->addQuickLaunch(const_cast<ModsConfigVRClient*>(this),
                entry.text.c_str(),
                entry.help.c_str(),
                entry.buttonText.empty() ? nullptr : entry.buttonText.c_str(),
                function.c_str());
        }

        /**
         * A value as the API passes it, whose text points into the text given: it is valid while that is.
         */
        static SettingValue toSettingValue(const Value& value, std::string& text)
        {
            SettingValue setting{ .type = ValueType::String };
            if (const auto* flag = std::get_if<bool>(&value)) {
                setting.type = ValueType::Bool;
                setting.boolValue = *flag;
            } else if (const auto* number = std::get_if<int>(&value)) {
                setting.type = ValueType::Int;
                setting.intValue = *number;
            } else if (const auto* real = std::get_if<float>(&value)) {
                setting.type = ValueType::Float;
                setting.floatValue = *real;
            } else {
                text = std::get<std::string>(value);
                setting.text = text.c_str();
            }
            return setting;
        }

        bool hasFunction(const char* name) override
        {
            return _functions.contains(std::string_view(name));
        }

        bool runFunction(const char* name, IFunctionCall* call) override
        {
            const auto found = _functions.find(std::string_view(name));
            if (found == _functions.end()) {
                return false;
            }
            const auto result = found->second(Params(call));
            if (!result.message.empty()) {
                call->setMessage(result.message.c_str());
            }
            return result.done;
        }

        bool answerState(const char* name, SettingValue* value) override
        {
            const auto found = _states.find(std::string_view(name));
            const auto answer = found != _states.end() ? found->second() : std::nullopt;
            if (!answer) {
                return false;
            }
            *value = toSettingValue(*answer, _stateText);
            return true;
        }

        bool giveOptions(const SettingView* setting, IOptions* options) override
        {
            const auto found = _options.find(std::string_view(setting->id));
            if (found == _options.end()) {
                return false;
            }
            for (const auto& option : found->second()) {
                std::string text;
                const auto value = toSettingValue(option.value, text);
                options->add(&value, option.text.empty() ? nullptr : option.text.c_str());
            }
            return true;
        }

        /**
         * Keeps what an event of the menu tells, whether it is open and the page that is shown, and calls the handlers.
         */
        void onEvent(const MenuEvent* event) override
        {
            switch (event->type) {
            case MenuEventType::MenuOpened:
                _menuOpen = true;
                break;
            case MenuEventType::MenuClosed:
                _menuOpen = false;
                break;
            case MenuEventType::PageShown:
                _shownPage = event->pageId ? event->pageId : "";
                break;
            case MenuEventType::PageLeft:
            case MenuEventType::ModLeft:
                _shownPage.reset();
                break;
            case MenuEventType::ModShown:
                break;
            }
            const std::string_view page = event->pageId ? event->pageId : "";
            for (const auto& handler : _menuEventHandlers) {
                handler(event->type, page);
            }
        }

        const ModsConfigVRApiV1* _api = nullptr;

        // the name it connected under, the mod's
        std::string _name;

        // what the mod added, by name
        std::map<std::string, ClientFunction, std::less<>> _functions;
        std::map<std::string, ClientState, std::less<>> _states;
        std::map<std::string, ClientOptions, std::less<>> _options;

        // the quick launch entries added before it connected, by their functions
        std::vector<std::pair<std::string, QuickLaunch>> _quickLaunch;

        std::vector<MenuEventHandler> _menuEventHandlers;

        // the text of a string state, which stays until a state is asked next: the menu copies it after the answer
        std::string _stateText;

        // what the menu's events told: whether it is open, and the page of the config spec that is shown
        bool _menuOpen = false;
        std::optional<std::string> _shownPage;
    };
}

#endif
