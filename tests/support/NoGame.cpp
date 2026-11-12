// What a test executable linked with the framework has in place of the game and of a mod's plugin load:
// F4VRCommon::test_no_game, which the framework's linked tests and a mod's link.

// The framework's PCH, which Logger.h is written for; this object library has no PCH of its own
#include "PCH.h"

#include <spdlog/sinks/stdout_sinks.h>

namespace REL
{
    // No game in the process: CommonLibF4 resolves every game address into a range that cannot be used, so the
    // framework's addresses resolve when the executable starts, and a test that uses one fails on it.
    extern const bool NO_GAME;
    const bool NO_GAME = true;
}

namespace
{
    /**
     * The framework's logger, which a mod sets up when its plugin loads. Here it writes to the output, which
     * ctest shows for a test that failed.
     */
    const struct Logger
    {
        Logger()
        {
            namespace internal = f4cf::logger::internal;
            const auto sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
            internal::_logger = std::make_shared<spdlog::logger>("GLOBAL", sink);
            internal::_rawLogger = std::make_shared<spdlog::logger>(std::string(internal::RAW_LOGGER_NAME), sink);
        }
    } LOGGER;
}
