#pragma once

#include <string_view>

namespace f4cf::perf
{
    /**
     * Whether this build links the Tracy profiler client (the F4CF_WITH_TRACY CMake option). Without it, every function
     * here does nothing and the perf sites are not Tracy zones.
     */
#ifdef F4CF_WITH_TRACY
    inline constexpr bool TRACY_BUILT = true;
#else
    inline constexpr bool TRACY_BUILT = false;
#endif

#ifdef F4CF_WITH_TRACY
    /**
     * Start the Tracy profiler client for this mod. ModBase calls it when the plugin loads, before any hook can open a
     * zone. Once; later calls do nothing.
     *
     * programName (it must outlive the process's use of it) is what the viewer's list of clients to connect to shows,
     * which tells mods apart; a capture is still named after the process. The thread that calls this is the one Tracy
     * calls "Main thread", whatever it is named later: for an F4SE plugin, the game thread.
     */
    void startTracy(const char* programName);

    /**
     * Whether a Tracy viewer is connected to this mod's client, so zones, plots and messages go somewhere. Any thread.
     */
    [[nodiscard]] bool isTracyConnected();

    /**
     * The end of one game frame and the start of the next, on the viewer's timeline. ModBase calls it every frame.
     */
    void tracyFrameMark();

    /**
     * What the viewer calls the calling thread ("game", "render"). The name must be a string literal or otherwise
     * outlive the process.
     */
    void tracyThreadName(const char* name);

    /**
     * A message on the viewer's timeline, where it happened, such as a devbench event. Copied, so any string will do.
     */
    void tracyMessage(std::string_view text);

    /**
     * A point on the plot called name (a string literal), at this moment.
     */
    void tracyPlot(const char* name, double value);
#else
    inline void startTracy(const char*)
    {}

    [[nodiscard]] constexpr bool isTracyConnected()
    {
        return false;
    }

    inline void tracyFrameMark()
    {}

    inline void tracyThreadName(const char*)
    {}

    inline void tracyMessage(std::string_view)
    {}

    inline void tracyPlot(const char*, double)
    {}
#endif
}
