#include "Tracy.h"

#ifdef F4CF_WITH_TRACY

#include <tracy/Tracy.hpp>

#include "Perf.h"

namespace f4cf::perf
{
    namespace
    {
        bool isStarted()
        {
            return internal::g_tracyStarted.load(std::memory_order_acquire);
        }
    }

    void startTracy(const char* programName)
    {
        if (isStarted()) {
            return;
        }
        ::tracy::StartupProfiler();
        TracySetProgramName(programName);
        internal::g_tracyStarted.store(true, std::memory_order_release);
        logger::info("Tracy profiler client started: on demand, localhost only");
    }

    bool isTracyConnected()
    {
        return isStarted() && TracyIsConnected;
    }

    void tracyFrameMark()
    {
        if (isStarted()) {
            FrameMark;
        }
    }

    void tracyThreadName(const char* name)
    {
        if (isStarted()) {
            ::tracy::SetThreadName(name);
        }
    }

    void tracyMessage(const std::string_view text)
    {
        if (isTracyConnected()) {
            TracyMessage(text.data(), text.size());
        }
    }

    void tracyPlot(const char* name, const double value)
    {
        if (isTracyConnected()) {
            TracyPlot(name, value);
        }
    }
}

#endif
