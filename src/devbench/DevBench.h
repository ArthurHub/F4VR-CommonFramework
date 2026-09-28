#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace f4cf
{
    class ConfigBase;
}

namespace f4cf::devbench
{
    /**
     * Where an action's handler runs.
     */
    enum class RunOn : std::uint8_t
    {
        /**
         * Queued, and run on the game thread at the start of the next frame, right before the mod's onFrameUpdate,
         * while the caller waits for the result for up to 2 seconds. Anything that reads or changes game or mod
         * state belongs here.
         */
        GameThread,

        /**
         * Run right away on devbench's listener thread, so it still answers while the game thread is stalled.
         * It must not touch game or mod state: only atomics and data that stops changing once the game has loaded.
         */
        Listener,
    };

    /**
     * Gets every argument of the call, "action" included, and returns a JSON object. "ok": true is added unless
     * the handler sets "ok" itself; throwing fails the call with the exception's message instead.
     */
    using ActionHandler = std::function<nlohmann::json(const nlohmann::json& args)>;

    /**
     * One operation of the mod's devbench tool, selected by the tool's "action" argument.
     */
    struct Action
    {
        // the value of the "action" argument that selects it
        std::string name;
        // one line in the tool's description, which is what an agent reads to choose an action
        std::string description;
        // JSON Schema "properties" of the arguments it reads, merged into the tool's input schema; an argument that
        // several actions share is declared once, and the first declaration wins
        nlohmann::json arguments = nlohmann::json::object();
        ActionHandler handler;
        RunOn runOn = RunOn::GameThread;
    };

    /**
     * Add an action to this mod's devbench tool. Call it before onGameLoaded returns, so the tool is registered
     * with its full description once; an action added later re-registers the tool.
     * The generic actions every framework mod has (health, state, config, set, clear, overrides, perf) can't be replaced, and
     * neither can an action already added: either is logged and ignored.
     */
    void addAction(Action action);

    /**
     * The opening of the tool's description: what the mod is, in the words a person would use to ask about it
     * ("FRIK: full-body IK, Pip-Boy, weapon positioning"). An agent picks the mod's tool by this line, so it should
     * name the mod's features. Defaults to the mod's name and version.
     */
    void setToolDescription(std::string description);

    /**
     * The INI section the config, set and clear actions use when the call names none. Defaults to the mod's name,
     * which is the section the mod template uses.
     */
    void setDefaultConfigSection(std::string section);

    /**
     * Whether any action other than health has been called, i.e. whether anyone is using this mod's tool. Health
     * never arms it, so probing every mod's health costs nothing. Collection that costs per-frame work waits for
     * this, and it stays armed for the rest of the session. Any thread.
     */
    [[nodiscard]] bool isArmed();

    /**
     * Driven by ModBase; a mod does not call these.
     */
    namespace internal
    {
        struct ToolSettings
        {
            std::string modName;
            std::string modVersion;
            ConfigBase* config = nullptr;
            // whether onFrameStart runs every frame; without it game-thread actions go through F4SE's task interface
            bool hasFrameUpdate = false;
        };

        /**
         * Register the mod's tool with devbench, if devbench is installed. Game thread, once the game has loaded.
         */
        void registerTool(const ToolSettings& settings);

        /**
         * Run the game-thread actions queued since the last frame. Game thread, before the mod's onFrameUpdate.
         */
        void onFrameStart();

        /**
         * Publish this frame's state snapshot while the tool is armed. Game thread, after the mod's onFrameUpdate.
         */
        void onFrameEnd();

        /**
         * The type-erased form of a mod's state provider; see devbench::setStateProvider.
         */
        struct StateProvider
        {
            // game thread, every frame while the tool is armed: a new snapshot of the mod's state
            std::function<std::shared_ptr<const void>()> capture;
            // listener thread: the JSON of a snapshot that capture returned
            std::function<nlohmann::json(const void*)> toJson;
        };

        void setStateProvider(std::string description, StateProvider provider);
    }

    /**
     * The mod's part of the state action.
     *
     * While the tool is armed, capture fills a new T on the game thread after every onFrameUpdate, and the state
     * action formats the latest one with toJson on devbench's listener thread, so state answers without waiting for
     * the game thread. T must hold plain values only: a snapshot outlives the frame that made it, and whatever a
     * pointer in it pointed to may be gone by then. toJson must only read its T.
     *
     * The keys toJson returns are the state action's top level, beside the framework's "liveness"; the keys of a
     * "liveness" object it returns are added to that block instead, for whatever tells whether the mod's state is
     * current, such as a generation counter. description says what the keys mean; it is added to the tool's
     * description. Call it before onGameLoaded returns; a later call re-registers the tool.
     *
     * @code
     *     devbench::setStateProvider<MyState>("swim: underwater, depth, surfacing", &captureMyState, &myStateToJson);
     * @endcode
     */
    template <typename T, typename Capture, typename ToJson>
    void setStateProvider(std::string description, Capture capture, ToJson toJson)
    {
        internal::StateProvider provider;
        provider.capture = [capture = std::move(capture)]() -> std::shared_ptr<const void> {
            auto state = std::make_shared<T>();
            capture(*state);
            return state;
        };
        provider.toJson = [toJson = std::move(toJson)](const void* state) -> nlohmann::json {
            return toJson(*static_cast<const T*>(state));
        };
        internal::setStateProvider(std::move(description), std::move(provider));
    }
}
