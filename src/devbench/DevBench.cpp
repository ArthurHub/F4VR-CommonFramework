#include "DevBench.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <format>
#include <future>
#include <mutex>
#include <optional>
#include <vector>

#include "ConfigBase.h"
#include "DevBenchAPI.h"

namespace f4cf::devbench
{
    namespace
    {
        using json = nlohmann::json;

        // Bumped whenever a generic action changes its arguments or the shape of its answer. Every mod ships the
        // framework version it was built with, so this is how a client tells their tools apart (health reports it).
        constexpr int TOOL_CONTRACT = 1;

        // devbench's own stall watchdog is 5000ms; answering well before it keeps "the mod did not answer" apart from
        // "devbench is stalled"
        constexpr auto GAME_THREAD_TIMEOUT = std::chrono::milliseconds(2000);

        // the free probe every mod answers: the one action that never arms the tool
        constexpr auto HEALTH_ACTION = "health";

        /**
         * Serialize an answer for the host. Invalid UTF-8 (an INI value in a legacy code page, say) is replaced rather
         * than thrown, so one stray byte can't turn an answer into a failure.
         */
        std::string dump(const json& value)
        {
            return value.dump(-1, ' ', false, json::error_handler_t::replace);
        }

        /**
         * A failed call. Deliberately carries no data keys, so a caller can't read a zero out of it and mistake that
         * for a value.
         */
        std::string errorJson(const std::string& message)
        {
            return dump(json{ { "ok", false }, { "error", message } });
        }

        /**
         * A scalar argument as a string: a string as it is, a number or boolean in its JSON form ("1.5", "true"), so
         * set takes value=1.5 as well as value="1.5". Empty when the argument is absent or null.
         */
        std::string argString(const json& args, const char* name)
        {
            const auto it = args.find(name);
            if (it == args.end() || it->is_null()) {
                return {};
            }
            if (it->is_string()) {
                return it->get<std::string>();
            }
            if (it->is_number() || it->is_boolean()) {
                return it->dump();
            }
            throw std::invalid_argument(std::format("argument '{}' must be a string, number or boolean", name));
        }

        bool argBool(const json& args, const char* name)
        {
            const auto it = args.find(name);
            return it != args.end() && it->is_boolean() && it->get<bool>();
        }

        /**
         * A JSON Schema property for Action::arguments.
         */
        json argument(const char* type, const char* description)
        {
            return { { "type", type }, { "description", description } };
        }

        /**
         * The mod's name in lowercase, with anything but letters, digits, '_' and '-' replaced by '_': the characters
         * every MCP client and LLM tool-calling API accepts in a tool name.
         */
        std::string toolNameFrom(const std::string& modName)
        {
            std::string name;
            for (const char c : modName) {
                const auto lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                name += std::isalnum(static_cast<unsigned char>(lower)) || lower == '_' || lower == '-' ? lower : '_';
            }
            return name;
        }

        /**
         * Run an action's handler and turn whatever it returns or throws into the answer.
         */
        std::string runHandler(const Action& action, const json& args)
        {
            try {
                auto result = action.handler(args);
                if (!result.is_object()) {
                    result = json{ { "result", std::move(result) } };
                }
                if (!result.contains("ok")) {
                    result["ok"] = true;
                }
                return dump(result);
            } catch (const std::exception& ex) {
                return errorJson(ex.what());
            }
        }

        /**
         * A game-thread action waiting for its frame.
         */
        struct Command
        {
            std::function<std::string()> fn;
            // shared, never a pointer to the waiting thread's stack: on timeout that thread returns while the command
            // is still queued
            std::shared_ptr<std::promise<std::string>> result;
        };

        void execute(const Command& command)
        {
            std::string result;
            try {
                result = command.fn();
            } catch (const std::exception& ex) {
                result = errorJson(std::format("the command threw on the game thread: {}", ex.what()));
            }
            // the waiter may have timed out and gone; the promise belongs to the command, so setting it is still safe
            try {
                command.result->set_value(std::move(result));
            } catch (const std::future_error&) {
            }
        }

        void toolHandler(void* ctx, const char* argsJson, void* sink, DevBenchAPI::WriteFn write);

        /**
         * This mod's devbench tool: its actions, the queue that carries game-thread actions across to the game thread,
         * and its registration with devbench. One per DLL, since the framework is a static library linked into each mod.
         *
         * The handler runs on devbench's listener thread and never touches the mod: it answers a listener action
         * itself and queues everything else for the game thread, which runs it at the start of the next frame.
         */
        class Tool
        {
        public:
            Tool()
            {
                addGenericActions();
            }

            void addAction(Action action, const bool generic)
            {
                if (action.name.empty() || !action.handler) {
                    logger::error("devbench: ignoring an action without a name or a handler");
                    return;
                }
                if (!action.arguments.is_object() || action.arguments.contains("action")) {
                    logger::error("devbench: ignoring action '{}': its arguments must be a JSON object of schema properties, without 'action'", action.name);
                    return;
                }
                {
                    std::lock_guard lock(_lock);
                    for (const auto& entry : _actions) {
                        if (entry.action.name == action.name) {
                            logger::error("devbench: ignoring action '{}': the tool already has one of that name{}",
                                action.name,
                                entry.generic ? " (a generic action every framework mod has)" : "");
                            return;
                        }
                    }
                    for (const auto& [argName, schema] : action.arguments.items()) {
                        const auto declared = declaredArgument(argName);
                        if (declared && *declared != schema) {
                            logger::warn("devbench: action '{}' declares argument '{}' differently from an earlier action; the schema keeps the earlier one", action.name, argName);
                        }
                    }
                    _actions.push_back({ std::move(action), generic });
                }
                refreshRegistration();
            }

            void setDescription(std::string description)
            {
                {
                    std::lock_guard lock(_lock);
                    _description = std::move(description);
                }
                refreshRegistration();
            }

            void setDefaultSection(std::string section)
            {
                std::lock_guard lock(_lock);
                _defaultSection = std::move(section);
            }

            [[nodiscard]] bool isArmed() const
            {
                return _armed.load(std::memory_order_relaxed);
            }

            void registerTool(const internal::ToolSettings& settings)
            {
                if (_registered) {
                    return;
                }
                {
                    std::lock_guard lock(_lock);
                    _settings = settings;
                    _name = toolNameFrom(settings.modName);
                }

                // Not an error when absent: devbench is a development tool and must never become a requirement.
                _interface = DevBenchAPI::GetDevBenchInterface001();
                if (!_interface) {
                    logger::info("devbench is not installed; the '{}' devbench tool is not registered", _name);
                    return;
                }
                // Only the slots every devbench has are used (GetBuildNumber, RegisterTool, EmitEvent); a later slot
                // must be gated on this build number, as calling one an older host lacks jumps past its vtable.
                _hostBuild = _interface->GetBuildNumber();

                const auto descriptor = buildDescriptor();
                if (!_interface->RegisterTool(_name.c_str(), descriptor.c_str(), &toolHandler, nullptr)) {
                    logger::warn("devbench: registering the '{}' tool replaced an existing tool of that name, registered earlier by another mod or by devbench", _name);
                }
                _registered = true;
                logger::info("devbench build {}: registered the '{}' tool", _hostBuild, _name);
            }

            /**
             * Listener thread. Any action but health arms the tool, since that is someone using it; health stays a free probe.
             */
            std::string invoke(const char* argsJson)
            {
                json args;
                try {
                    args = argsJson && *argsJson ? json::parse(argsJson) : json::object();
                } catch (const std::exception& ex) {
                    return errorJson(std::format("bad arguments JSON: {}", ex.what()));
                }
                if (!args.is_object()) {
                    return errorJson("the arguments must be a JSON object");
                }

                std::string name;
                try {
                    name = argString(args, "action");
                } catch (const std::exception& ex) {
                    return errorJson(ex.what());
                }
                // required rather than defaulted: a call that names nothing should not quietly arm the tool
                if (name.empty()) {
                    return errorJson(std::format("missing 'action' ({})", joinedActionNames()));
                }

                const auto action = findAction(name);
                if (!action) {
                    return errorJson(std::format("unknown action '{}' ({})", name, joinedActionNames()));
                }

                if (name != HEALTH_ACTION) {
                    _armed.store(true, std::memory_order_relaxed);
                }
                if (action->runOn == RunOn::Listener) {
                    return runHandler(*action, args);
                }
                return runOnGameThread([selected = *action, args] {
                    return runHandler(selected, args);
                });
            }

            /**
             * Game thread, at the start of every frame. One relaxed load while nothing is queued.
             */
            void runQueuedCommands()
            {
                if (!_hasCommands.load(std::memory_order_relaxed)) {
                    return;
                }
                std::vector<Command> commands;
                {
                    std::lock_guard lock(_commandsLock);
                    commands.swap(_commands);
                    _hasCommands.store(false, std::memory_order_relaxed);
                }
                for (const auto& command : commands) {
                    execute(command);
                }
            }

        private:
            struct Entry
            {
                Action action;
                bool generic;
            };

            void addGenericActions()
            {
                const auto section = argument("string", "config/set/clear: the INI section; defaults to the mod's main section");
                const auto key = argument("string", "config/set/clear: the setting name");

                addAction({ HEALTH_ACTION,
                              "which mod and framework this is, the devbench build it talks to, and whether the tool is in use. Answered without the game "
                              "thread, so it replies while the game is stalled, and it never arms the tool",
                              json::object(),
                              handler(&Tool::health),
                              RunOn::Listener },
                    true);
                addAction({ "config",
                              "one INI value as the mod sees it: the session override if one is set, otherwise the file's",
                              { { "section", section }, { "key", key } },
                              handler(&Tool::readConfig) },
                    true);
                addAction({ "set",
                              "override one INI value for this session without writing the file; the mod reloads its config at once",
                              { { "section", section }, { "key", key }, { "value", argument("string", "set: the new value") } },
                              handler(&Tool::setConfig) },
                    true);
                addAction({ "clear",
                              "drop one session override, or every one with all=true, so the file's value applies again",
                              { { "section", section }, { "key", key }, { "all", argument("boolean", "clear: drop every session override instead of one key") } },
                              handler(&Tool::clearConfig) },
                    true);
                addAction({ "overrides", "the session overrides in effect", json::object(), handler(&Tool::listOverrides) }, true);
            }

            /**
             * A generic action's handler: one of this tool's members, all of which take the call's arguments.
             */
            ActionHandler handler(json (Tool::*member)(const json&) const)
            {
                return [this, member](const json& args) {
                    return (this->*member)(args);
                };
            }

            json health(const json&) const
            {
                std::lock_guard lock(_lock);
                json actions = json::array();
                for (const auto& entry : _actions) {
                    actions.push_back(entry.action.name);
                }
                return {
                    { "plugin", _settings.modName },
                    { "version", _settings.modVersion },
                    { "framework", std::string(Version::NAME) },
                    { "contract", TOOL_CONTRACT },
                    { "tool", _name },
                    { "devbench", _hostBuild },
                    { "armed", isArmed() },
                    { "actions", actions },
                };
            }

            json readConfig(const json& args) const
            {
                const auto [section, key] = sectionAndKey(args, "config");
                return {
                    { "section", section },
                    { "key", key },
                    { "value", config().getConfigValue(section.c_str(), key.c_str()) },
                    { "overridden", config().hasConfigOverride(section.c_str(), key.c_str()) },
                };
            }

            json setConfig(const json& args) const
            {
                const auto [section, key] = sectionAndKey(args, "set");
                if (!args.contains("value")) {
                    throw std::invalid_argument("action 'set' needs 'value'");
                }
                const auto value = argString(args, "value");
                // rewrites every typed config member, which the mod reads mid-frame: the reason set is a game-thread action
                config().setConfigOverride(section.c_str(), key.c_str(), config::IniValue(value));
                return { { "section", section }, { "key", key }, { "value", value }, { "note", "session override; the file is unchanged" } };
            }

            json clearConfig(const json& args) const
            {
                if (argBool(args, "all")) {
                    const auto count = config().getConfigOverrides().size();
                    config().clearAllConfigOverrides();
                    return { { "cleared", count }, { "note", "every session override dropped; the file's values apply again" } };
                }
                const auto [section, key] = sectionAndKey(args, "clear");
                const bool hadOverride = config().hasConfigOverride(section.c_str(), key.c_str());
                config().clearConfigOverride(section.c_str(), key.c_str());
                return { { "section", section }, { "key", key }, { "hadOverride", hadOverride }, { "note", "the file's value applies again" } };
            }

            json listOverrides(const json&) const
            {
                json overrides = json::array();
                for (const auto& [sectionKey, value] : config().getConfigOverrides()) {
                    overrides.push_back({ { "section", sectionKey.first }, { "key", sectionKey.second }, { "value", value } });
                }
                return { { "overrides", overrides } };
            }

            ConfigBase& config() const
            {
                std::lock_guard lock(_lock);
                if (!_settings.config) {
                    throw std::runtime_error("the mod has no config");
                }
                return *_settings.config;
            }

            std::pair<std::string, std::string> sectionAndKey(const json& args, const char* action) const
            {
                auto key = argString(args, "key");
                if (key.empty()) {
                    throw std::invalid_argument(std::format("action '{}' needs 'key' (and optionally 'section')", action));
                }
                auto section = argString(args, "section");
                if (section.empty()) {
                    std::lock_guard lock(_lock);
                    section = _defaultSection.empty() ? _settings.modName : _defaultSection;
                }
                return { std::move(section), std::move(key) };
            }

            std::optional<Action> findAction(const std::string& name) const
            {
                std::lock_guard lock(_lock);
                for (const auto& entry : _actions) {
                    if (entry.action.name == name) {
                        return entry.action;
                    }
                }
                return std::nullopt;
            }

            /**
             * Every action name, '|'-separated, for the error that lists what a call could have asked for.
             */
            std::string joinedActionNames() const
            {
                std::lock_guard lock(_lock);
                std::string names;
                for (const auto& entry : _actions) {
                    names += (names.empty() ? "" : "|") + entry.action.name;
                }
                return names;
            }

            /**
             * The schema an earlier action gave an argument. Caller holds _lock.
             */
            std::optional<json> declaredArgument(const std::string& name) const
            {
                for (const auto& entry : _actions) {
                    if (const auto it = entry.action.arguments.find(name); it != entry.action.arguments.end()) {
                        return *it;
                    }
                }
                return std::nullopt;
            }

            /**
             * What devbench lists for the tool: a description that opens with the mod's own words and lists every
             * action, and one flat input schema holding every action's arguments.
             */
            std::string buildDescriptor() const
            {
                std::lock_guard lock(_lock);
                std::string text = _description.empty() ? std::format("{} v{}.", _settings.modName, _settings.modVersion) : _description;
                text += "\n\nThe required 'action' argument chooses the operation.";

                const auto appendActions = [&](const bool generic) {
                    for (const auto& entry : _actions) {
                        if (entry.generic == generic) {
                            text += std::format("\n- {}: {}", entry.action.name, entry.action.description);
                        }
                    }
                };
                if (std::ranges::any_of(_actions, [](const Entry& entry) {
                        return !entry.generic;
                    })) {
                    text += "\nThis mod's actions:";
                    appendActions(false);
                }
                text += "\nGeneric actions, the same in every F4VR-CommonFramework mod:";
                appendActions(true);
                text +=
                    "\nEvery action but health runs on the game thread at the start of the next frame, and fails after 2s if the game "
                    "thread does not get there.";

                json names = json::array();
                json properties = json::object();
                for (const auto& entry : _actions) {
                    names.push_back(entry.action.name);
                    for (const auto& [argName, schema] : entry.action.arguments.items()) {
                        if (!properties.contains(argName)) {
                            properties[argName] = schema;
                        }
                    }
                }
                properties["action"] = { { "type", "string" }, { "enum", names }, { "description", "the operation to run" } };

                return dump(json{
                    { "description", text },
                    { "readOnly", false },
                    { "inputSchema", { { "type", "object" }, { "properties", properties }, { "required", json::array({ "action" }) } } },
                });
            }

            /**
             * Send the current descriptor again after a change, once registered. RegisterTool replaces the tool of that
             * name, which is this one, so its "replaced" answer is expected here.
             */
            void refreshRegistration() const
            {
                if (!_registered) {
                    return;
                }
                const auto descriptor = buildDescriptor();
                _interface->RegisterTool(_name.c_str(), descriptor.c_str(), &toolHandler, nullptr);
            }

            /**
             * Listener thread. Hands the command to the game thread and waits for its answer.
             */
            std::string runOnGameThread(std::function<std::string()> fn)
            {
                auto promise = std::make_shared<std::promise<std::string>>();
                auto future = promise->get_future();
                Command command{ std::move(fn), std::move(promise) };

                if (_settings.hasFrameUpdate) {
                    std::lock_guard lock(_commandsLock);
                    _commands.push_back(std::move(command));
                    _hasCommands.store(true, std::memory_order_relaxed);
                } else {
                    // a mod without a frame update never drains the queue, so borrow F4SE's, as devbench itself does
                    const auto tasks = F4SE::GetTaskInterface();
                    if (!tasks) {
                        return errorJson("F4SE's task interface is unavailable, and this mod has no frame update to run the command in");
                    }
                    tasks->AddTask([command = std::move(command)] {
                        execute(command);
                    });
                }

                if (future.wait_for(GAME_THREAD_TIMEOUT) != std::future_status::ready) {
                    return errorJson(
                        std::format("the game thread did not run the command within {}ms: it is stalled or paused, or the mod's frame update "
                                    "is not running",
                            GAME_THREAD_TIMEOUT.count()));
                }
                return future.get();
            }

            // guards the actions and the text of the descriptor, which the listener thread reads
            mutable std::mutex _lock;
            std::vector<Entry> _actions;
            std::string _description;
            std::string _defaultSection;

            // set once at registration, before devbench can call the handler
            internal::ToolSettings _settings;
            std::string _name;
            DevBenchAPI::IDevBenchInterface001* _interface = nullptr;
            unsigned int _hostBuild = 0;
            bool _registered = false;

            std::atomic<bool> _armed{ false };

            std::mutex _commandsLock;
            std::vector<Command> _commands;
            std::atomic<bool> _hasCommands{ false };
        };

        /**
         * Function-local static: a mod may add actions from a static initializer, which can run before this
         * translation unit's.
         */
        Tool& tool()
        {
            static Tool instance;
            return instance;
        }

        /**
         * What devbench calls, on its listener thread. Nothing may be thrown back across the C ABI.
         */
        void toolHandler(void*, const char* argsJson, void* sink, const DevBenchAPI::WriteFn write)
        {
            std::string result;
            try {
                result = tool().invoke(argsJson);
            } catch (const std::exception& ex) {
                result = errorJson(std::format("the tool failed: {}", ex.what()));
            } catch (...) {
                result = errorJson("the tool failed with an unknown exception");
            }
            write(sink, result.c_str());
        }
    }

    void addAction(Action action)
    {
        tool().addAction(std::move(action), false);
    }

    void setToolDescription(std::string description)
    {
        tool().setDescription(std::move(description));
    }

    void setDefaultConfigSection(std::string section)
    {
        tool().setDefaultSection(std::move(section));
    }

    bool isArmed()
    {
        return tool().isArmed();
    }

    namespace internal
    {
        void registerTool(const ToolSettings& settings)
        {
            tool().registerTool(settings);
        }

        void onFrameStart()
        {
            tool().runQueuedCommands();
        }
    }
}
