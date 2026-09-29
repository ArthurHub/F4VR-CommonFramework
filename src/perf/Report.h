#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "FrameContext.h"
#include "Perf.h"

namespace f4cf::perf
{
    /**
     * Every site as of one read: nested under the site each runs inside, and grouped by the thread their outermost
     * site runs on.
     */
    struct Report
    {
        struct Node
        {
            const Site* site = nullptr;
            Site::Stats stats;
            // in the order the sites were constructed, which is roughly the order they first ran
            std::vector<Node> children;
        };

        struct Thread
        {
            std::thread::id id;
            // the thread the frame site runs on
            bool isGameThread = false;
            std::vector<Node> roots;
        };

        // since the last reset
        std::chrono::steady_clock::duration window{};
        // calls of the frame site in the window: 0 before the mod's first frame
        std::uint64_t frames = 0;
        // the whole frame over the same window: frame interval, refresh rate, and the compositor's timing in VR
        FrameContext frame;
        // the game thread first
        std::vector<Thread> threads;

        /**
         * A site's average cost per game frame as a share of the frame budget, in percent; 0 when either is unknown.
         */
        [[nodiscard]] double budgetPct(const Site::Stats& stats) const
        {
            const double budgetMs = frame.budgetMs();
            if (frames == 0 || budgetMs <= 0) {
                return 0.0;
            }
            return Histogram::Snapshot::toMs(stats.durations.sumNs) / static_cast<double>(frames) / budgetMs * 100.0;
        }
    };

    /**
     * Read every site once and nest them. A site that recorded nothing in the window is left out unless a site under
     * it recorded something. Any thread: it only loads atomics, so it never waits for the threads being measured.
     *
     * Caller links can form a loop only when a site first looked outermost and a site it had opened later opened it;
     * the loop is cut where it closes, so every site appears once.
     */
    [[nodiscard]] Report readReport();

    /**
     * The report as a text table for a person to read: the frame context, then one line per site, indented under the
     * site it runs inside, with n, avg, p50, p95, p99, max and self ms, calls per frame and share of the frame budget,
     * under a line per thread. Every line ends in '\n'.
     */
    [[nodiscard]] std::string formatReport(const Report& report);
}
