#include "SceneDepthDiagnostics.h"

#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <optional>
#include <string>

#include "../ModBase.h"
#include "../debug/DebugDraw.h"

namespace f4cf::render::sceneDepth::internal
{
    namespace
    {
        // The channel the readout is tagged with, so a mod already using the overlay for its own
        // thing can silence these rows through sDebugDrawDisabledChannels.
        constexpr std::string_view WATCH_CHANNEL = "SCENE-DEPTH";

        // Enough divergences to see the pattern and the values behind it; after that the count is
        // the whole message and repeating the context only buries the rest of the log.
        constexpr std::uint32_t DIVERGENCES_LOGGED_IN_FULL = 8;

        // The closest two summaries may sit. There is no matching upper bound: a summary is written
        // because something changed, never because time passed. See logSummary.
        constexpr auto SUMMARY_MIN_SPACING = std::chrono::seconds(30);

        /**
         * Everything the render thread tells the game thread, as relaxed atomics.
         *
         * Relaxed and per-field on purpose: this is a readout, not a protocol. A torn set across the
         * viewport's four floats means one frame of the watch table mixes two frames' answers, which
         * costs nothing and is the alternative to putting a lock on the capture's hot path.
         */
        struct Published
        {
            std::atomic<std::uint8_t> strategy{ static_cast<std::uint8_t>(Strategy::Auto) };
            std::atomic<bool> diagnostics{ false };

            std::array<std::atomic<std::uint32_t>, static_cast<std::size_t>(CaptureStage::Count)> stages{};
            std::array<std::atomic<std::uint32_t>, static_cast<std::size_t>(AcquireOutcome::Count)> outcomes{};
            std::atomic<std::uint8_t> lastOutcome{ static_cast<std::uint8_t>(AcquireOutcome::NoCapture) };

            std::atomic<float> worldX{ 0 };
            std::atomic<float> worldY{ 0 };
            std::atomic<float> worldWidth{ 0 };
            std::atomic<float> worldHeight{ 0 };
            std::atomic<std::uint32_t> electedPasses{ 0 };
            std::atomic<std::uint32_t> runnerUpPasses{ 0 };
            std::atomic<std::uint32_t> groups{ 0 };
            std::atomic<std::uint32_t> groupCapHits{ 0 };
            std::atomic<std::uint32_t> identityChanges{ 0 };

            std::atomic<std::uint32_t> plannedCopyPass{ 0 };
            std::atomic<std::uint32_t> lastWorldPasses{ 0 };
            std::atomic<std::uint32_t> shortFrames{ 0 };
            std::atomic<std::uint32_t> copies{ 0 };

            std::atomic<std::uint32_t> bufferWidth{ 0 };
            std::atomic<std::uint32_t> bufferHeight{ 0 };
            std::atomic<std::uint32_t> bufferFormat{ 0 };
            std::atomic<std::uint32_t> bufferSamples{ 0 };

            std::array<std::atomic<std::uint32_t>, static_cast<std::size_t>(Comparison::Count)> agreements{};
            std::array<std::atomic<std::uint32_t>, static_cast<std::size_t>(Comparison::Count)> divergences{};
        };

        Published s_published;

        // Set by the first thing the capture ever records, so the pump stays one atomic read in a
        // session where the hook never installed.
        std::atomic<bool> s_everRecorded{ false };

        void touch()
        {
            s_everRecorded.store(true, std::memory_order_relaxed);
        }

        void bump(std::atomic<std::uint32_t>& counter)
        {
            counter.fetch_add(1, std::memory_order_relaxed);
        }

        std::uint32_t read(const std::atomic<std::uint32_t>& counter)
        {
            return counter.load(std::memory_order_relaxed);
        }

        const char* comparisonName(const Comparison which)
        {
            switch (which) {
            case Comparison::WorldRegion:
                return "world region";
            case Comparison::DepthSelection:
                return "depth selection";
            case Comparison::CopyPass:
                return "copy pass";
            default:
                return "?";
            }
        }

        /**
         * The stages that refused a capture. Deliberately not every counter: the ones meant to fire
         * every frame (attempt, no-targets, already-captured, captured) say nothing when they do, and
         * the two depth-state ones are not refusals at all - they are passes the shadow was not fed,
         * which the shadow row reports instead.
         */
        std::string rejectionSummary()
        {
            static constexpr std::array REJECTIONS{
                CaptureStage::NoContext,
                CaptureStage::NoDepthResource,
                CaptureStage::NoDepthState,
                CaptureStage::NotATexture,
                CaptureStage::WrongSize,
                CaptureStage::ReadOnlyViewFailed,
            };

            std::string out;
            for (const auto stage : REJECTIONS) {
                if (const auto count = read(s_published.stages[static_cast<std::size_t>(stage)]); count > 0) {
                    out += std::format("{}{} {}", out.empty() ? "" : ", ", stageName(stage), count);
                }
            }
            return out.empty() ? "none" : out;
        }

        std::string comparisonSummary()
        {
            std::string out;
            for (std::size_t i = 0; i < static_cast<std::size_t>(Comparison::Count); ++i) {
                const auto agreed = read(s_published.agreements[i]);
                const auto diverged = read(s_published.divergences[i]);
                if (agreed == 0 && diverged == 0) {
                    continue; // no shadow runs for that decision
                }
                out += std::format("{}{} {}/{}", out.empty() ? "" : ", ", comparisonName(static_cast<Comparison>(i)), diverged, agreed + diverged);
            }
            const auto filtered = read(s_published.stages[static_cast<std::size_t>(CaptureStage::DepthDisabled)]) +
                                  read(s_published.stages[static_cast<std::size_t>(CaptureStage::DepthFuncRejected)]);
            return std::format("{} (filtered {})", out.empty() ? "no shadows" : out, filtered);
        }

        /**
         * Publish the strategy the config asks for, re-parsed only when the text changes: this runs
         * every frame and the key is a string the file watcher may rewrite at any time.
         */
        void publishStrategyFromConfig()
        {
            const auto* config = g_mod ? g_mod->getConfig() : nullptr;
            if (!config) {
                return;
            }

            static std::string lastRaw;
            static Strategy lastParsed = Strategy::Auto;
            if (config->debug.sceneDepthStrategy != lastRaw) {
                lastRaw = config->debug.sceneDepthStrategy;
                lastParsed = parseStrategy(lastRaw);
                logger::info("scene depth strategy: {}", lastRaw.empty() ? "auto" : lastRaw);
            }
            s_published.strategy.store(static_cast<std::uint8_t>(lastParsed), std::memory_order_relaxed);

            static std::optional<bool> lastDiagnostics;
            if (lastDiagnostics != config->debug.sceneDepthDiagnostics) {
                lastDiagnostics = config->debug.sceneDepthDiagnostics;
                logger::info("scene depth diagnostics {}", *lastDiagnostics ? "ON - watch rows drawn, shadow policies comparing" : "off");
            }
            s_published.diagnostics.store(config->debug.sceneDepthDiagnostics, std::memory_order_relaxed);
        }

        std::uint32_t refusedAcquires()
        {
            return read(s_published.outcomes[static_cast<std::size_t>(AcquireOutcome::NoCapture)]) +
                   read(s_published.outcomes[static_cast<std::size_t>(AcquireOutcome::StaleCapture)]) +
                   read(s_published.outcomes[static_cast<std::size_t>(AcquireOutcome::SubmittedLayoutMismatch)]) +
                   read(s_published.outcomes[static_cast<std::size_t>(AcquireOutcome::DifferentDevice)]) +
                   read(s_published.outcomes[static_cast<std::size_t>(AcquireOutcome::NoWorldCopy)]);
        }

        const char* strategyName()
        {
            switch (static_cast<Strategy>(s_published.strategy.load(std::memory_order_relaxed))) {
            case Strategy::Auto:
                return "auto";
            case Strategy::DepthPass:
                return "depthpass";
            case Strategy::Direct:
                return "direct";
            case Strategy::Off:
                return "off";
            default:
                return "?";
            }
        }

        std::string acquireSummary()
        {
            return std::format("{} (ok {}, resampled {}, refused {})",
                outcomeName(static_cast<AcquireOutcome>(s_published.lastOutcome.load(std::memory_order_relaxed))),
                read(s_published.outcomes[static_cast<std::size_t>(AcquireOutcome::Ok)]),
                read(s_published.outcomes[static_cast<std::size_t>(AcquireOutcome::OkResampled)]),
                refusedAcquires());
        }

        std::string bufferSummary()
        {
            return std::format("{}x{} fmt {} samples {}",
                read(s_published.bufferWidth),
                read(s_published.bufferHeight),
                read(s_published.bufferFormat),
                read(s_published.bufferSamples));
        }

        std::string worldSummary()
        {
            const auto width = read(s_published.bufferWidth);
            const auto height = read(s_published.bufferHeight);
            const auto worldWidth = s_published.worldWidth.load(std::memory_order_relaxed);
            const auto worldHeight = s_published.worldHeight.load(std::memory_order_relaxed);
            return std::format("{:.0f},{:.0f} {:.0f}x{:.0f} ({:.0f}% x {:.0f}%) {}",
                s_published.worldX.load(std::memory_order_relaxed),
                s_published.worldY.load(std::memory_order_relaxed),
                worldWidth,
                worldHeight,
                width > 0 ? worldWidth * 100.0f / static_cast<float>(width) : 0.0f,
                height > 0 ? worldHeight * 100.0f / static_cast<float>(height) : 0.0f,
                static_cast<AcquireOutcome>(s_published.lastOutcome.load(std::memory_order_relaxed)) == AcquireOutcome::OkResampled ? "upscaled" : "direct");
        }

        /**
         * The world's viewport as the change key wants it: which fraction of the buffer it covers,
         * whole percent, and whether that is the whole thing.
         *
         * The raw extent cannot be used. The tally treats viewports within a few pixels as one, so the
         * elected width legitimately alternates between (measured) 4731 and 4728 on the same upscaler
         * setting - a difference the election ignores and a raw key does not, which is enough on its
         * own to make the summary write every thirty seconds forever. A whole percent is coarse enough
         * to be stable and fine enough that every upscaler quality step (50 / 58 / 67 / 77 / 100) moves
         * it.
         */
        std::string worldKey()
        {
            const auto width = read(s_published.bufferWidth);
            const auto height = read(s_published.bufferHeight);
            return std::format("{:.0f}x{:.0f}%{}",
                width > 0 ? s_published.worldWidth.load(std::memory_order_relaxed) * 100.0f / static_cast<float>(width) : 0.0f,
                height > 0 ? s_published.worldHeight.load(std::memory_order_relaxed) * 100.0f / static_cast<float>(height) : 0.0f,
                static_cast<AcquireOutcome>(s_published.lastOutcome.load(std::memory_order_relaxed)) == AcquireOutcome::OkResampled ? "up" : "direct");
        }

        std::string tallySummary()
        {
            return std::format("elected {} vs {} of {} groups{}{}",
                read(s_published.electedPasses),
                read(s_published.runnerUpPasses),
                read(s_published.groups),
                read(s_published.groupCapHits) > 0 ? std::format(" CAP HIT x{}", read(s_published.groupCapHits)) : "",
                read(s_published.identityChanges) > 0 ? std::format(" flips {}", read(s_published.identityChanges)) : "");
        }

        std::string copySummary()
        {
            return std::format("at pass {} of {} (copies {}, short frames {})",
                read(s_published.plannedCopyPass),
                read(s_published.lastWorldPasses),
                read(s_published.copies),
                read(s_published.shortFrames));
        }

        void drawWatchTable()
        {
            auto& draw = debug::dd();
            const auto channel = draw.channelScope(WATCH_CHANNEL);

            draw.watch("DEPTH ACQUIRE", acquireSummary());
            draw.watch("DEPTH BUFFER", bufferSummary());
            draw.watch("DEPTH WORLD", worldSummary());
            draw.watch("DEPTH TALLY", tallySummary());
            draw.watch("DEPTH COPY", copySummary());
            draw.watch("DEPTH SHADOW", comparisonSummary());
            draw.watch("DEPTH REJECTED", rejectionSummary());
        }

        /**
         * What a summary is written for: the things that are news when they change, and nothing that
         * merely counts up or jitters frame to frame.
         *
         * This is the whole rate limit, and it took two passes to get right, so the rule is worth
         * stating: **nothing that counts up may be in the key.** Everything here does, eventually -
         * the acquire and copy totals every frame, the per-frame pass counts (a frame draws 14 world
         * passes, the next 15), the divergence counts (the copy-pass shadow disagrees on half of all
         * frames by design), and the pathology counts too - a profile that drops one frame in four
         * hundred moves a short-frame count every summary, forever. Any one of them left in turns the
         * summary back into a heartbeat that says nothing.
         *
         * So a count enters the key only as *whether it has ever happened*: the first cap hit, winner
         * flip, short frame or divergence is news, the thousandth is not. What is left is a shape, a
         * viewport (quantised - see worldKey, which is the same trap in a second guise), an outcome, a
         * group count and those four flags - all of which settle.
         *
         * Every one of the numbers still appears in the logged line, and the heartbeat still carries
         * them periodically. They just do not earn a line of their own.
         */
        std::string changeKey()
        {
            std::string flags;
            for (std::size_t i = 0; i < static_cast<std::size_t>(Comparison::Count); ++i) {
                flags += read(s_published.divergences[i]) > 0 ? "1" : "0";
            }
            flags += read(s_published.groupCapHits) > 0 ? "1" : "0";
            flags += read(s_published.identityChanges) > 0 ? "1" : "0";
            flags += read(s_published.shortFrames) > 0 ? "1" : "0";

            return std::format("{}|{}|{}|{}|{}|{}|{}",
                strategyName(),
                bufferSummary(),
                worldKey(),
                outcomeName(static_cast<AcquireOutcome>(s_published.lastOutcome.load(std::memory_order_relaxed))),
                read(s_published.groups),
                flags,
                rejectionSummary());
        }

        /**
         * Write the same readout the watch table shows to the log as well.
         *
         * The HUD is for whoever is wearing the headset; this is for whoever reads the log afterwards,
         * and it is the only thing that makes a session played without the overlay say anything - which
         * is most of them, since the overlay ships off. The individual events already log themselves;
         * these are the running totals, which nothing else carries.
         *
         * Written **only** when the decisions change (see changeKey), no closer together than
         * SUMMARY_MIN_SPACING. There is no heartbeat: a session where the capture simply works says
         * so once, at the start, and then nothing until something is different. That is what makes it
         * affordable to leave on at info - and leaving it on is the point, because a user reporting
         * "the overlay draws through walls" has then already sent the answer with their log, and a
         * diagnostic that has to be switched on first is one that is missing from every bug report.
         */
        void logSummary()
        {
            static std::chrono::steady_clock::time_point lastLogged;
            static std::string lastKey;

            const auto now = std::chrono::steady_clock::now();
            const bool firstEver = lastKey.empty();
            if (!firstEver && now - lastLogged < SUMMARY_MIN_SPACING) {
                return;
            }

            auto key = changeKey();
            if (!firstEver && key == lastKey) {
                return;
            }
            lastLogged = now;
            lastKey = std::move(key);

            logger::info("scene depth [{}]: buffer {}; world {}; acquire {}", strategyName(), bufferSummary(), worldSummary(), acquireSummary());
            logger::info("scene depth tally: {}; copy {}; shadows {}; rejected {}", tallySummary(), copySummary(), comparisonSummary(), rejectionSummary());
        }
    }

    Strategy parseStrategy(const std::string& name)
    {
        if (name.empty() || name == "auto") {
            return Strategy::Auto;
        }
        if (name == "depthpass") {
            return Strategy::DepthPass;
        }
        if (name == "direct") {
            return Strategy::Direct;
        }
        if (name == "off") {
            return Strategy::Off;
        }
        logger::warn("scene depth: unknown sSceneDepthStrategy {}, using auto (accepted: auto, depthpass, direct, off)", name);
        return Strategy::Auto;
    }

    Strategy activeStrategy()
    {
        return static_cast<Strategy>(s_published.strategy.load(std::memory_order_relaxed));
    }

    bool diagnosticsEnabled()
    {
        return s_published.diagnostics.load(std::memory_order_relaxed);
    }

    const char* stageName(const CaptureStage stage)
    {
        switch (stage) {
        case CaptureStage::Attempt:
            return "attempt";
        case CaptureStage::AlreadyCaptured:
            return "already-captured";
        case CaptureStage::NoContext:
            return "no-context";
        case CaptureStage::NoTargets:
            return "no-targets";
        case CaptureStage::NoDepthResource:
            return "no-depth-resource";
        case CaptureStage::NoDepthState:
            return "no-depth-state";
        case CaptureStage::DepthDisabled:
            return "depth-disabled";
        case CaptureStage::DepthFuncRejected:
            return "depth-func-rejected";
        case CaptureStage::NotATexture:
            return "not-a-texture";
        case CaptureStage::WrongSize:
            return "wrong-size";
        case CaptureStage::ReadOnlyViewFailed:
            return "read-only-view-failed";
        case CaptureStage::Captured:
            return "captured";
        default:
            return "?";
        }
    }

    const char* outcomeName(const AcquireOutcome outcome)
    {
        switch (outcome) {
        case AcquireOutcome::Ok:
            return "ok";
        case AcquireOutcome::OkResampled:
            return "ok-resampled";
        case AcquireOutcome::NoTexture:
            return "no-texture";
        case AcquireOutcome::NoCapture:
            return "no-capture";
        case AcquireOutcome::StaleCapture:
            return "stale-capture";
        case AcquireOutcome::SubmittedLayoutMismatch:
            return "submitted-layout-mismatch";
        case AcquireOutcome::DifferentDevice:
            return "different-device";
        case AcquireOutcome::NoWorldCopy:
            return "no-world-copy";
        case AcquireOutcome::StrategyOff:
            return "strategy-off";
        default:
            return "?";
        }
    }

    std::string stageCountersText()
    {
        std::string out;
        for (std::size_t i = 0; i < static_cast<std::size_t>(CaptureStage::Count); ++i) {
            if (const auto count = read(s_published.stages[i]); count > 0) {
                out += std::format("{}{} {}", out.empty() ? "" : ", ", stageName(static_cast<CaptureStage>(i)), count);
            }
        }
        return out.empty() ? "nothing counted" : out;
    }

    void countStage(const CaptureStage stage)
    {
        touch();
        bump(s_published.stages[static_cast<std::size_t>(stage)]);
    }

    void recordAcquire(const AcquireOutcome outcome)
    {
        touch();
        bump(s_published.outcomes[static_cast<std::size_t>(outcome)]);
        s_published.lastOutcome.store(static_cast<std::uint8_t>(outcome), std::memory_order_relaxed);
    }

    void recordElection(const Election& election)
    {
        touch();
        s_published.worldX.store(election.viewport.TopLeftX, std::memory_order_relaxed);
        s_published.worldY.store(election.viewport.TopLeftY, std::memory_order_relaxed);
        s_published.worldWidth.store(election.viewport.Width, std::memory_order_relaxed);
        s_published.worldHeight.store(election.viewport.Height, std::memory_order_relaxed);
        s_published.electedPasses.store(election.passes, std::memory_order_relaxed);
        s_published.runnerUpPasses.store(election.runnerUpPasses, std::memory_order_relaxed);
        s_published.groups.store(election.groups, std::memory_order_relaxed);
        if (election.groupCapHit) {
            bump(s_published.groupCapHits);
        }
        if (election.changedIdentity) {
            bump(s_published.identityChanges);
        }

        // A winner that barely won is the election's own warning sign: the whole policy rests on the
        // world's viewport being the one most passes use, and two groups a pass apart trade places.
        if (election.runnerUpPasses > 0 && election.passes <= election.runnerUpPasses + 1) {
            logger::sample(10000,
                "scene depth: the world viewport election is close - {} passes at {:.0f}x{:.0f} against {} at the runner-up, over {} groups",
                election.passes,
                election.viewport.Width,
                election.viewport.Height,
                election.runnerUpPasses,
                election.groups);
        }
        if (election.groupCapHit) {
            logger::sample(10000, "scene depth: more distinct viewports this frame than the tally can hold; one was dropped from the election");
        }
    }

    void recordCopyPlan(const std::uint32_t plannedPass, const std::uint32_t worldPasses, const bool reachedPlan)
    {
        touch();
        s_published.plannedCopyPass.store(plannedPass, std::memory_order_relaxed);
        s_published.lastWorldPasses.store(worldPasses, std::memory_order_relaxed);
        if (reachedPlan) {
            bump(s_published.copies);
            return;
        }
        if (plannedPass > 0 && worldPasses < plannedPass) {
            bump(s_published.shortFrames);
            logger::sample(10000,
                "scene depth: the frame drew {} world passes but the copy was planned for pass {}; the previous frame's depth is reused",
                worldPasses,
                plannedPass);
        }
    }

    bool recordComparison(const Comparison which, const bool agreed)
    {
        touch();
        const auto index = static_cast<std::size_t>(which);
        if (agreed) {
            bump(s_published.agreements[index]);
            return false;
        }
        const auto seen = s_published.divergences[index].fetch_add(1, std::memory_order_relaxed);
        return seen < DIVERGENCES_LOGGED_IN_FULL;
    }

    void recordBufferShape(const D3D11_TEXTURE2D_DESC& desc)
    {
        touch();
        s_published.bufferWidth.store(desc.Width, std::memory_order_relaxed);
        s_published.bufferHeight.store(desc.Height, std::memory_order_relaxed);
        s_published.bufferFormat.store(static_cast<std::uint32_t>(desc.Format), std::memory_order_relaxed);
        s_published.bufferSamples.store(desc.SampleDesc.Count, std::memory_order_relaxed);
    }

    void onGameFrameEnd()
    {
        publishStrategyFromConfig();

        if (s_everRecorded.load(std::memory_order_relaxed)) {
            logSummary();
        }

        // Asked for explicitly, rather than shown to whoever opens the overlay for their own
        // reasons. watch() is also what switches debug draw on for a mod that draws nothing else, so
        // an unasked-for row here would start a mod paying for the overlay because it happened to
        // draw an occluded panel.
        if (s_everRecorded.load(std::memory_order_relaxed) && diagnosticsEnabled() && debug::dd().isEnabled()) {
            drawWatchTable();
        }
    }
}
