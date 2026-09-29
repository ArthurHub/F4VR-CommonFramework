#include "Report.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace f4cf::perf
{
    namespace
    {
        /**
         * Nests one read of every site: the sites' caller links turned into child lists, walked depth first.
         */
        class Builder
        {
        public:
            explicit Builder(std::vector<Site*> sites)
                : _sites(std::move(sites)),
                  _children(_sites.size()),
                  _taken(_sites.size(), false)
            {
                _stats.reserve(_sites.size());
                _runOrders.reserve(_sites.size());
                for (std::size_t i = 0; i < _sites.size(); ++i) {
                    _stats.push_back(_sites[i]->read());
                    // read once: a live value that changed mid-sort would break the sort's ordering
                    _runOrders.push_back(_sites[i]->runOrder());
                    _indexOf.emplace(_sites[i], i);
                }
                for (std::size_t i = 0; i < _sites.size(); ++i) {
                    if (!_sites[i]->hasRun()) {
                        continue;
                    }
                    const auto caller = _indexOf.find(_sites[i]->caller());
                    if (caller != _indexOf.end()) {
                        _children[caller->second].push_back(i);
                    } else {
                        _roots.push_back(i);
                    }
                }
                // in the order they run in a frame, not the order the sites were constructed
                const auto runOrder = [this](const std::size_t index) {
                    return _runOrders[index];
                };
                std::ranges::stable_sort(_roots, {}, runOrder);
                for (auto& children : _children) {
                    std::ranges::stable_sort(children, {}, runOrder);
                }
            }

            [[nodiscard]] std::uint64_t countOf(const Site* site) const
            {
                const auto found = _indexOf.find(site);
                return found == _indexOf.end() ? 0 : _stats[found->second].durations.count;
            }

            /**
             * Every root with what recorded under it, grouped by thread, then whatever a caller loop kept from being
             * reached from a root.
             */
            [[nodiscard]] std::vector<Report::Thread> threads(const std::thread::id gameThread)
            {
                std::vector<Report::Thread> threads;
                if (gameThread != std::thread::id()) {
                    threads.push_back({ gameThread, true, {} });
                }
                const auto addRoot = [&](const std::size_t index) {
                    auto node = build(index);
                    if (!node) {
                        return;
                    }
                    const auto thread = _sites[index]->thread();
                    auto group = std::ranges::find(threads, thread, &Report::Thread::id);
                    if (group == threads.end()) {
                        threads.push_back({ thread, false, {} });
                        group = threads.end() - 1;
                    }
                    group->roots.push_back(std::move(*node));
                };
                for (const auto index : _roots) {
                    addRoot(index);
                }
                for (std::size_t index = 0; index < _sites.size(); ++index) {
                    if (!_taken[index] && _stats[index].durations.count > 0) {
                        addRoot(index);
                    }
                }
                std::erase_if(threads, [](const Report::Thread& thread) {
                    return thread.roots.empty() && !thread.isGameThread;
                });
                return threads;
            }

        private:
            /**
             * The node for a site and everything under it, or nothing when none of it recorded in the window. A site
             * already placed is skipped, which is what cuts a caller loop.
             */
            std::optional<Report::Node> build(const std::size_t index)
            {
                if (_taken[index]) {
                    return std::nullopt;
                }
                _taken[index] = true;
                Report::Node node{ _sites[index], _stats[index], {} };
                for (const auto child : _children[index]) {
                    if (auto built = build(child)) {
                        node.children.push_back(std::move(*built));
                    }
                }
                if (node.stats.durations.count == 0 && node.children.empty()) {
                    return std::nullopt;
                }
                return node;
            }

            std::vector<Site*> _sites;
            std::vector<Site::Stats> _stats;
            std::vector<std::uint64_t> _runOrders;
            std::unordered_map<const Site*, std::size_t> _indexOf;
            std::vector<std::vector<std::size_t>> _children;
            std::vector<std::size_t> _roots;
            std::vector<bool> _taken;
        };
    }

    Report readReport()
    {
        Report report;
        report.window = std::chrono::steady_clock::now() - windowStart();
        Builder builder(sites());
        const auto* frame = frameSite();
        report.frames = builder.countOf(frame);
        report.threads = builder.threads(frame ? frame->thread() : std::thread::id());
        report.frame = readFrameContext();
        return report;
    }

    namespace
    {
        constexpr std::size_t INDENT = 2;

        // a site that also runs under other sites, shown under the first one seen
        constexpr std::string_view MULTIPLE_CALLERS_MARK = " *";

        std::size_t labelLength(const Report::Node& node)
        {
            return std::string_view(node.site->label()).size() + (node.site->hasMultipleCallers() ? MULTIPLE_CALLERS_MARK.size() : 0);
        }

        /**
         * The widest indented label in a subtree, which the numbers are aligned after.
         */
        std::size_t labelColumnWidth(const std::vector<Report::Node>& nodes, const std::size_t depth)
        {
            std::size_t width = 0;
            for (const auto& node : nodes) {
                width = (std::max)({ width, depth * INDENT + labelLength(node), labelColumnWidth(node.children, depth + 1) });
            }
            return width;
        }

        bool anyMultipleCallers(const std::vector<Report::Node>& nodes)
        {
            return std::ranges::any_of(nodes, [](const Report::Node& node) {
                return node.site->hasMultipleCallers() || anyMultipleCallers(node.children);
            });
        }

        void appendNode(std::string& out, const Report::Node& node, const std::size_t depth, const std::size_t width, const Report& report)
        {
            const auto s = node.stats.durations.summary();
            const double selfAvgMs = s.count > 0 ? Histogram::Snapshot::toMs(node.stats.selfNs()) / static_cast<double>(s.count) : 0.0;
            auto label = std::string(depth * INDENT, ' ') + node.site->label();
            if (node.site->hasMultipleCallers()) {
                label += MULTIPLE_CALLERS_MARK;
            }
            std::format_to(std::back_inserter(out),
                "{:<{}}{:>8}{:>9.3f}{:>9.3f}{:>9.3f}{:>9.3f}{:>9.3f}{:>9.3f}",
                label,
                width,
                s.count,
                s.avgMs,
                s.p50Ms,
                s.p95Ms,
                s.p99Ms,
                s.maxMs,
                selfAvgMs);
            if (report.frames > 0) {
                std::format_to(std::back_inserter(out), "{:>9.2f}", static_cast<double>(s.count) / static_cast<double>(report.frames));
            } else {
                out += std::format("{:>9}", "-");
            }
            if (report.frames > 0 && report.frame.budgetMs() > 0) {
                std::format_to(std::back_inserter(out), "{:>9.1f}\n", report.budgetPct(node.stats));
            } else {
                out += std::format("{:>9}\n", "-");
            }
            for (const auto& child : node.children) {
                appendNode(out, child, depth + 1, width, report);
            }
        }

        /**
         * The frame context lines: the refresh rate and frame interval, then in VR the compositor's GPU time and frame
         * statistics. Nothing when nothing about the frame is known yet.
         */
        void appendFrameContext(std::string& out, const FrameContext& frame)
        {
            const auto interval = frame.interval.summary();
            if (interval.count == 0 && frame.displayHz <= 0) {
                return;
            }
            out += "frame:";
            if (frame.displayHz > 0) {
                std::format_to(std::back_inserter(out), " {:.0f} Hz, budget {:.2f} |", frame.displayHz, frame.budgetMs());
            }
            if (interval.count > 0) {
                std::format_to(std::back_inserter(out), " interval p50 {:.2f} p95 {:.2f} p99 {:.2f} max {:.2f}\n", interval.p50Ms, interval.p95Ms, interval.p99Ms, interval.maxMs);
            } else {
                out += " no frame interval yet\n";
            }
            if (frame.compositorFrames == 0) {
                return;
            }
            const auto gpu = frame.gpu.summary();
            std::format_to(std::back_inserter(out),
                "gpu:   p50 {:.2f} p95 {:.2f} p99 {:.2f} max {:.2f} | game p95 {:.2f} | compositor p95 {:.2f}\n",
                gpu.p50Ms,
                gpu.p95Ms,
                gpu.p99Ms,
                gpu.maxMs,
                frame.gameGpu.summary().p95Ms,
                frame.compositorGpu.summary().p95Ms);
            std::format_to(std::back_inserter(out),
                "vr:    {} frames | reprojected cpu {}, gpu {} | dropped {} | mispresented {} | late start p95 {:.2f} | headroom p50 {:.2f}\n",
                frame.compositorFrames,
                frame.reprojectedCpu,
                frame.reprojectedGpu,
                frame.dropped,
                frame.misPresented,
                frame.lateStart.summary().p95Ms,
                frame.headroom.summary().p50Ms);
        }

        std::string threadName(const Report::Thread& thread)
        {
            std::ostringstream id;
            id << thread.id;
            return thread.isGameThread ? std::format("game thread {}", id.str()) : std::format("thread {}", id.str());
        }
    }

    std::string formatReport(const Report& report)
    {
        const double windowSeconds = std::chrono::duration<double>(report.window).count();
        auto out = std::format("perf: {:.1f}s window, {} frames", windowSeconds, report.frames);
        if (report.frames > 0 && windowSeconds > 0) {
            out += std::format(" ({:.1f} fps)", static_cast<double>(report.frames) / windowSeconds);
        }
        out += ", times in ms\n";
        appendFrameContext(out, report.frame);

        std::size_t width = std::string_view("site").size();
        bool multipleCallers = false;
        bool anySite = false;
        for (const auto& thread : report.threads) {
            width = (std::max)(width, labelColumnWidth(thread.roots, 1));
            multipleCallers = multipleCallers || anyMultipleCallers(thread.roots);
            anySite = anySite || !thread.roots.empty();
        }
        if (!anySite) {
            return out + "no site has recorded anything since the last reset\n";
        }
        width += INDENT;

        std::format_to(std::back_inserter(out),
            "{:<{}}{:>8}{:>9}{:>9}{:>9}{:>9}{:>9}{:>9}{:>9}{:>9}\n",
            "site",
            width,
            "n",
            "avg",
            "p50",
            "p95",
            "p99",
            "max",
            "self",
            "/frame",
            "%budget");
        for (const auto& thread : report.threads) {
            out += threadName(thread) + '\n';
            for (const auto& root : thread.roots) {
                appendNode(out, root, 1, width, report);
            }
        }
        if (multipleCallers) {
            out += "* also runs under other sites; shown under the first one seen\n";
        }
        return out;
    }
}
