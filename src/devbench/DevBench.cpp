#include "DevBench.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <format>
#include <future>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include "ConfigBase.h"
#include "DevBenchAPI.h"
#include "perf/Perf.h"
#include "perf/Report.h"

namespace f4cf::devbench
{
    namespace
    {
        using json = nlohmann::json;

        // Bumped whenever a generic action changes its arguments or the shape of its answer. Every mod ships the
        // framework version it was built with, so this is how a client tells their tools apart (health reports it).
        // 2: perf answers without the game thread, as a tree per thread, a flat map or a text table (format)
        // 3: set and clear act on the tool's own session overrides only, and overrides says who set each
        constexpr int TOOL_CONTRACT = 3;

        // the owner of the session overrides that the tool's set action sets, see ConfigBase::setConfigOverride
        constexpr auto OVERRIDE_OWNER = "devbench";

        // devbench's own stall watchdog is 5000ms; answering well before it keeps "the mod did not answer" apart from
        // "devbench is stalled"
        constexpr auto GAME_THREAD_TIMEOUT = std::chrono::milliseconds(2000);

        // the free probe every mod answers
        constexpr auto HEALTH_ACTION = "health";

        // the generic action whose description the mod's state provider extends
        constexpr auto STATE_ACTION = "state";

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
         * A perf site's name in the flat view: its function, and its label for a block ("Skeleton::onFrameUpdate/arms").
         */
        std::string perfSiteKey(const perf::Site& site)
        {
            // a GPU site can share its function and label with a CPU one, as the Submit host's draw callbacks do
            const auto* kind = site.kind() == perf::SiteKind::Gpu ? "gpu:" : "";
            return site.isWholeFunction() ? std::format("{}{}", kind, site.shortFunction()) : std::format("{}{}/{}", kind, site.shortFunction(), site.label());
        }

        double windowMsOf(const perf::Report& report)
        {
            return std::chrono::duration<double, std::milli>(report.window).count();
        }

        /**
         * What the perf action reports for one site, in either view.
         */
        json perfStatsJson(const perf::Site& site, const perf::Site::Stats& stats, const perf::Report& report)
        {
            const auto s = stats.durations.summary();
            const double selfTotalMs = perf::Histogram::Snapshot::toMs(stats.selfNs());
            const double windowMs = windowMsOf(report);
            json out = {
                { "n", s.count },
                { "totalMs", s.totalMs },
                { "avgMs", s.avgMs },
                { "p50Ms", s.p50Ms },
                { "p95Ms", s.p95Ms },
                { "p99Ms", s.p99Ms },
                { "minMs", s.minMs },
                { "maxMs", s.maxMs },
                { "selfTotalMs", selfTotalMs },
                { "selfAvgMs", s.count > 0 ? selfTotalMs / static_cast<double>(s.count) : 0.0 },
                { "busyPct", windowMs > 0 ? s.totalMs / windowMs * 100.0 : 0.0 },
            };
            if (report.frames > 0) {
                out["callsPerFrame"] = static_cast<double>(s.count) / static_cast<double>(report.frames);
                if (report.frame.budgetMs() > 0) {
                    out["budgetPct"] = report.budgetPct(stats);
                }
            }
            if (site.hasMultipleCallers()) {
                out["multipleCallers"] = true;
            }
            return out;
        }

        json perfNodeJson(const perf::Report::Node& node, const perf::Report& report)
        {
            auto out = perfStatsJson(*node.site, node.stats, report);
            out["label"] = node.site->label();
            out["key"] = perfSiteKey(*node.site);
            if (!node.children.empty()) {
                json children = json::array();
                for (const auto& child : node.children) {
                    children.push_back(perfNodeJson(child, report));
                }
                out["children"] = std::move(children);
            }
            return out;
        }

        /**
         * Every node of a tree that recorded something, keyed by perfSiteKey; two sites sharing a key don't hide each other.
         */
        void addFlatPerfSites(json& sites, const perf::Report::Node& node, const perf::Report& report)
        {
            if (node.stats.durations.count > 0) {
                const auto key = perfSiteKey(*node.site);
                auto name = key;
                for (int copy = 2; sites.contains(name); ++copy) {
                    name = std::format("{} ({})", key, copy);
                }
                sites[name] = perfStatsJson(*node.site, node.stats, report);
            }
            for (const auto& child : node.children) {
                addFlatPerfSites(sites, child, report);
            }
        }

        json perfHistogramJson(const perf::Histogram::Snapshot& snapshot)
        {
            const auto s = snapshot.summary();
            return { { "avg", s.avgMs }, { "p50", s.p50Ms }, { "p95", s.p95Ms }, { "p99", s.p99Ms }, { "max", s.maxMs } };
        }

        /**
         * The whole frame over the window: refresh rate, budget and frame interval, and in VR the compositor's timing.
         */
        json perfFrameJson(const perf::Report& report)
        {
            const auto& frame = report.frame;
            json out = json::object();
            if (frame.displayHz > 0) {
                out["hz"] = frame.displayHz;
                out["budgetMs"] = frame.budgetMs();
            }
            const double windowMs = windowMsOf(report);
            if (report.frames > 0 && windowMs > 0) {
                out["fps"] = static_cast<double>(report.frames) / windowMs * 1000.0;
            }
            if (frame.interval.count > 0) {
                out["intervalMs"] = perfHistogramJson(frame.interval);
            }
            if (frame.compositorFrames > 0) {
                out["compositorFrames"] = frame.compositorFrames;
                out["gpuMs"] = perfHistogramJson(frame.gpu);
                out["gameGpuMs"] = perfHistogramJson(frame.gameGpu);
                out["compositorGpuMs"] = perfHistogramJson(frame.compositorGpu);
                out["lateStartMs"] = perfHistogramJson(frame.lateStart);
                out["headroomMs"] = perfHistogramJson(frame.headroom);
                out["reprojected"] = { { "cpu", frame.reprojectedCpu }, { "gpu", frame.reprojectedGpu } };
                out["dropped"] = frame.dropped;
                out["misPresented"] = frame.misPresented;
            }
            return out;
        }

        std::string threadIdText(const std::thread::id id)
        {
            std::ostringstream text;
            text << id;
            return text.str();
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

            [[nodiscard]] bool isRegistered() const
            {
                return _registered.load(std::memory_order_acquire);
            }

            [[nodiscard]] bool isArmed() const
            {
                return _armed.load(std::memory_order_relaxed);
            }

            /**
             * Something can read what costs per-frame work, so switch it on, once: every perf site starts recording what
             * the perf action reads, and the state provider captures every frame.
             */
            void arm()
            {
                if (!_armed.exchange(true, std::memory_order_relaxed)) {
                    perf::setEnabled(true);
                }
            }

            void registerTool(const internal::ToolSettings& settings)
            {
                if (_registered.load(std::memory_order_acquire)) {
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
                // release: a thread that sees it registered also sees _name and _interface
                _registered.store(true, std::memory_order_release);
                logger::info("devbench build {}: registered the '{}' tool", _hostBuild, _name);
                // devbench is installed, so its events and actions have a reader from now on
                arm();
            }

            /**
             * Listener thread.
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
                // required rather than defaulted: a call that names nothing is a mistake, not a request for some default
                if (name.empty()) {
                    return errorJson(std::format("missing 'action' ({})", joinedActionNames()));
                }

                const auto action = findAction(name);
                if (!action) {
                    return errorJson(std::format("unknown action '{}' ({})", name, joinedActionNames()));
                }

                if (action->runOn == RunOn::Listener) {
                    return runHandler(*action, args);
                }
                return runOnGameThread([selected = *action, args] {
                    return runHandler(selected, args);
                });
            }

            /**
             * Any thread: devbench's EmitEvent only queues the event, it never delivers on the caller's thread.
             */
            void emit(const std::string_view topic, const json& payload) const
            {
                if (topic.empty() || !_registered.load(std::memory_order_acquire)) {
                    return;
                }
                const auto fullTopic = std::format("{}.{}", _name, topic);
                const auto body = dump(payload);
                _interface->EmitEvent(fullTopic.c_str(), body.c_str());
            }

            void setStateProvider(std::string description, internal::StateProvider provider)
            {
                if (!provider.capture || !provider.toJson) {
                    logger::error("devbench: ignoring a state provider without a capture or a toJson");
                    return;
                }
                {
                    std::lock_guard lock(_lock);
                    _stateDescription = std::move(description);
                }
                _stateProvider.store(std::make_shared<const internal::StateProvider>(std::move(provider)));
                refreshRegistration();
            }

            /**
             * Game thread, at the end of every frame, including the frames the mod's update returned early from: a
             * snapshot frozen at its last good value through a loading screen would be a lie. One relaxed load while
             * the tool is not armed, plus one in a Tracy build, where a viewer connecting arms it.
             */
            void publishState()
            {
                if (!isArmed()) {
                    if (!perf::isTracyConnected()) {
                        return;
                    }
                    arm();
                }
                // what the tool costs each frame while it is in use, which is also while perf records
                F4CF_PERF_FUNCTION();
                auto snapshot = std::make_shared<Snapshot>();
                snapshot->frame = ++_publishedFrames;
                snapshot->publishedAt = std::chrono::steady_clock::now();
                if (auto provider = _stateProvider.load()) {
                    try {
                        snapshot->modState = provider->capture();
                        snapshot->provider = std::move(provider);
                    } catch (const std::exception& ex) {
                        snapshot->captureError = ex.what();
                    }
                }
                _snapshot.store(std::move(snapshot));
            }

            /**
             * Game thread, at the start of every frame. One relaxed load while nothing is queued.
             */
            void runQueuedCommands()
            {
                if (!_hasCommands.load(std::memory_order_relaxed)) {
                    return;
                }
                // the frames that ran game-thread actions, and what they cost
                F4CF_PERF_FUNCTION();
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
                              "which mod and framework this is, the devbench build it talks to, and whether the tool is armed. Answered without the game "
                              "thread, so it replies while the game is stalled",
                              json::object(),
                              handler(&Tool::health),
                              RunOn::Listener },
                    true);
                addAction({ STATE_ACTION,
                              "what the mod is doing as of its last frame. Read 'liveness' first: frame counts the frames published since the tool "
                              "was armed and ageMs is how old the latest is, so a growing ageMs means the game thread has stopped. Answered from a "
                              "snapshot published every frame, so it replies while the game is stalled; a call before the first one waits for it",
                              json::object(),
                              handler(&Tool::readState),
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
                              "drop one session override that set made, or every one of them with all=true, so the file's value applies again. "
                              "An override that someone else set, as another mod, stays",
                              { { "section", section }, { "key", key }, { "all", argument("boolean", "clear: drop every session override that set made instead of one key") } },
                              handler(&Tool::clearConfig) },
                    true);
                addAction({ "overrides",
                              "the session overrides in effect, each with the owner that set it ('devbench' for this tool's set)",
                              json::object(),
                              handler(&Tool::listOverrides) },
                    true);
                addAction({ "perf",
                              "time spent in every perf site in the mod since the last reset, as a tree per thread: each site under the site it runs "
                              "inside, with n, avg, p50, p95, p99, min, max and self ms, busy% of the window, calls per frame and share of the frame "
                              "budget. 'frame' puts it in context: refresh rate, frame interval and, in VR, the compositor's GPU time, late starts and "
                              "reprojected (cpu/gpu) and dropped frames. Recording starts when the tool is first used, so reset, hold the condition, "
                              "then read. Answered without the game thread. Sites time the CPU; only 'frame' sees the GPU",
                              { { "reset", argument("boolean", "perf: clear every site after reading it, starting a new window") },
                                  { "format",
                                      argument("string",
                                          "perf: tree (default, JSON nested per thread), flat (one map keyed by function/label, \"Skeleton::onFrameUpdate/arms\") "
                                          "or text (the tree as an indented table in 'text', the quickest to read)") } },
                              handler(&Tool::readPerf),
                              RunOn::Listener },
                    true);
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
                // before taking _lock, as it calls the mod's toJson; health never waits for a snapshot
                const auto snapshot = _snapshot.load();
                json liveness = nullptr;
                if (snapshot) {
                    try {
                        liveness = stateJson(*snapshot)["liveness"];
                    } catch (const std::exception&) {
                        liveness = frameworkLiveness(*snapshot);
                    }
                }

                std::lock_guard lock(_lock);
                json actions = json::array();
                for (const auto& entry : _actions) {
                    actions.push_back(entry.action.name);
                }
                return {
                    { "plugin", _settings.modName },
                    { "version", _settings.modVersion },
                    { "framework", F4CF_VERSION },
                    { "contract", TOOL_CONTRACT },
                    { "tool", _name },
                    { "devbench", _hostBuild },
                    { "armed", isArmed() },
                    // whether the build links the Tracy client, and whether a Tracy viewer is connected to it
                    { "tracy", json{ { "built", perf::TRACY_BUILT }, { "connected", perf::isTracyConnected() } } },
                    { "hasSnapshot", snapshot != nullptr },
                    { "liveness", liveness },
                    { "actions", actions },
                };
            }

            /**
             * Listener thread: the latest snapshot, never the game thread.
             */
            json readState(const json&) const
            {
                const auto snapshot = waitForSnapshot();
                if (!snapshot->captureError.empty()) {
                    throw std::runtime_error(std::format("capturing the mod's state threw on the game thread: {}", snapshot->captureError));
                }
                return stateJson(*snapshot);
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
                config().setConfigOverride(OVERRIDE_OWNER, section.c_str(), key.c_str(), config::IniValue(value));
                return { { "section", section }, { "key", key }, { "value", value }, { "note", "session override; the file is unchanged" } };
            }

            /**
             * Only what the tool's own set made: an override of another owner, as a mod through this mod's API, stays.
             */
            json clearConfig(const json& args) const
            {
                if (argBool(args, "all")) {
                    const auto count = config().clearAllConfigOverrides(OVERRIDE_OWNER);
                    return { { "cleared", count }, { "note", "every session override that set made is dropped; the file's values apply again" } };
                }
                const auto [section, key] = sectionAndKey(args, "clear");
                const bool hadOverride = config().clearConfigOverride(OVERRIDE_OWNER, section.c_str(), key.c_str());
                return { { "section", section },
                    { "key", key },
                    { "hadOverride", hadOverride },
                    { "overridden", config().hasConfigOverride(section.c_str(), key.c_str()) },
                    { "note", "the file's value applies again, unless 'overridden': then someone else overrides the key too" } };
            }

            json listOverrides(const json&) const
            {
                json overrides = json::array();
                for (const auto& entry : config().getConfigOverrides()) {
                    overrides.push_back({ { "section", entry.section }, { "key", entry.key }, { "value", entry.value }, { "owner", entry.owner } });
                }
                return { { "overrides", overrides } };
            }

            /**
             * Listener thread: the sites are read with atomic loads, so this never waits for the game thread or stalls
             * a frame. A reset drops what was recorded after the read, which the answer has already covered.
             */
            json readPerf(const json& args) const
            {
                const bool reset = argBool(args, "reset");
                auto format = argString(args, "format");
                if (format.empty()) {
                    format = "tree";
                }
                if (format != "tree" && format != "flat" && format != "text") {
                    throw std::invalid_argument(std::format("unknown format '{}' (tree, flat, text)", format));
                }
                const auto report = perf::readReport();
                if (reset) {
                    perf::reset();
                }
                json answer = { { "reset", reset }, { "windowMs", windowMsOf(report) }, { "frames", report.frames } };

                if (format == "text") {
                    answer["text"] = perf::formatReport(report);
                    return answer;
                }

                answer["frame"] = perfFrameJson(report);
                if (format == "flat") {
                    json sites = json::object();
                    for (const auto& thread : report.threads) {
                        for (const auto& root : thread.roots) {
                            addFlatPerfSites(sites, root, report);
                        }
                    }
                    for (const auto& root : report.gpu) {
                        addFlatPerfSites(sites, root, report);
                    }
                    answer["sites"] = std::move(sites);
                    return answer;
                }

                json threads = json::array();
                for (const auto& thread : report.threads) {
                    json roots = json::array();
                    for (const auto& root : thread.roots) {
                        roots.push_back(perfNodeJson(root, report));
                    }
                    threads.push_back({ { "thread", threadIdText(thread.id) }, { "gameThread", thread.isGameThread }, { "sites", std::move(roots) } });
                }
                answer["threads"] = std::move(threads);
                // the mod's own GPU work, read back from timestamps 1-3 frames late; nested like a thread's sites
                if (!report.gpu.empty()) {
                    json gpu = json::array();
                    for (const auto& root : report.gpu) {
                        gpu.push_back(perfNodeJson(root, report));
                    }
                    answer["gpu"] = std::move(gpu);
                }
                return answer;
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
             * One frame of state, published on the game thread and read on the listener thread. Immutable once
             * published, and a reader keeps the one it loaded alive for as long as it needs it.
             */
            struct Snapshot
            {
                std::uint64_t frame = 0;
                std::chrono::steady_clock::time_point publishedAt;
                // the provider that captured modState, which is also the one that can format it
                std::shared_ptr<const internal::StateProvider> provider;
                std::shared_ptr<const void> modState;
                std::string captureError;
            };

            /**
             * The framework's liveness block: frame is this tool's publish counter, not the engine's; publishedAtMs is
             * on steady_clock, which every DLL in the process shares, so snapshots of different mods line up.
             */
            static json frameworkLiveness(const Snapshot& snapshot)
            {
                using std::chrono::duration_cast;
                using std::chrono::milliseconds;
                return {
                    { "frame", snapshot.frame },
                    { "ageMs", duration_cast<milliseconds>(std::chrono::steady_clock::now() - snapshot.publishedAt).count() },
                    { "publishedAtMs", duration_cast<milliseconds>(snapshot.publishedAt.time_since_epoch()).count() },
                };
            }

            /**
             * The state answer: the mod's keys at the top level, and the framework's liveness block with the keys of the
             * mod's own "liveness" object added (never replacing the framework's).
             */
            static json stateJson(const Snapshot& snapshot)
            {
                json state = json::object();
                json liveness = frameworkLiveness(snapshot);
                if (snapshot.provider && snapshot.modState) {
                    const auto modState = snapshot.provider->toJson(snapshot.modState.get());
                    if (modState.is_object()) {
                        for (const auto& [key, value] : modState.items()) {
                            if (key == "liveness" && value.is_object()) {
                                for (const auto& [livenessKey, livenessValue] : value.items()) {
                                    if (!liveness.contains(livenessKey)) {
                                        liveness[livenessKey] = livenessValue;
                                    }
                                }
                            } else if (key != "ok") {
                                state[key] = value;
                            }
                        }
                    }
                }
                state["liveness"] = std::move(liveness);
                return state;
            }

            /**
             * The latest snapshot. A call right after the tool armed finds none yet, so it waits for the one the end of
             * the next frame publishes rather than failing.
             */
            std::shared_ptr<const Snapshot> waitForSnapshot() const
            {
                auto snapshot = _snapshot.load();
                if (snapshot) {
                    return snapshot;
                }
                if (!_settings.hasFrameUpdate) {
                    throw std::runtime_error("this mod has no frame update, so it publishes no state");
                }
                // polled, as it happens once per session and a condition variable would cost the publisher every frame
                const auto deadline = std::chrono::steady_clock::now() + GAME_THREAD_TIMEOUT;
                while (!(snapshot = _snapshot.load()) && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                if (!snapshot) {
                    throw std::runtime_error(
                        std::format("no state was published within {}ms: the game thread is stalled or paused, or the mod's frame "
                                    "update is not running",
                            GAME_THREAD_TIMEOUT.count()));
                }
                return snapshot;
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
                            if (entry.action.name == STATE_ACTION) {
                                text += _stateDescription.empty() ? ". This mod publishes only liveness" : std::format(". This mod's part: {}", _stateDescription);
                            }
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
                std::string listenerActions;
                for (const auto& entry : _actions) {
                    if (entry.action.runOn == RunOn::Listener) {
                        listenerActions += (listenerActions.empty() ? "" : ", ") + entry.action.name;
                    }
                }
                text += std::format(
                    "\n{} answer at once, without the game thread. Every other action runs on the game thread at the start of the next frame, "
                    "and fails after 2s if the game thread does not get there.",
                    listenerActions);

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
                if (!_registered.load(std::memory_order_acquire)) {
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
            std::atomic<bool> _registered{ false };

            std::atomic<bool> _armed{ false };

            std::mutex _commandsLock;
            std::vector<Command> _commands;
            std::atomic<bool> _hasCommands{ false };

            // the mod's state provider (set at load, swapped whole) and its description, which is under _lock
            std::atomic<std::shared_ptr<const internal::StateProvider>> _stateProvider;
            std::string _stateDescription;
            // written on the game thread, read on the listener thread
            std::atomic<std::shared_ptr<const Snapshot>> _snapshot;
            // game thread only
            std::uint64_t _publishedFrames = 0;
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

        void arm()
        {
            tool().arm();
        }

        void onFrameStart()
        {
            tool().runQueuedCommands();
        }

        void onFrameEnd()
        {
            tool().publishState();
        }

        void setStateProvider(std::string description, StateProvider provider)
        {
            tool().setStateProvider(std::move(description), std::move(provider));
        }

        bool canEmit()
        {
            return tool().isRegistered();
        }

        void emit(const std::string_view topic, const nlohmann::json& payload)
        {
            // a marker on the Tracy timeline, where it happened
            if (perf::isTracyConnected()) {
                perf::tracyMessage(payload.empty() ? std::string(topic) : std::format("{} {}", topic, dump(payload)));
            }
            tool().emit(topic, payload);
        }
    }
}
