#pragma once

#include <cstdint>
#include <string>

#include <d3d11.h>

namespace f4cf::render::sceneDepth::internal
{
    /**
     * Which implementation answers the capture's questions, selected by the [Debug] INI key
     * sSceneDepthStrategy so a user with a broken setup can be asked to flip it without a build.
     *
     * Auto is the shipped answer. Direct and Off are escape hatches that each remove one layer:
     * Direct keeps the capture but never resamples, so an overlay is occluded by whatever the engine
     * left in its buffer - right without an upscaler, wrong with one; Off hands out no depth at all,
     * which is how overlays behaved before occlusion existed. Between them they say whether a
     * misdrawn overlay is the capture, the copy, or neither.
     *
     * DepthPass promotes the shadow policy this subsystem already runs beside the shipped one - see
     * the depth-filtered tally in SceneDepthCapture.cpp - so it can be driven in-game, hot-reloaded,
     * rather than only compared. Work items B (viewport from engine state) and C (depth buffer by
     * logical role) add their own values here once their shadow reads exist.
     */
    enum class Strategy : std::uint8_t
    {
        // the tally elects the world's viewport over every pass that draws into the buffer (shipped)
        Auto,
        // the same election, but only over passes that actually test depth the way the world does
        DepthPass,
        // capture, but never resample: the engine's buffer as-is, whatever is in it
        Direct,
        // capture nothing, hand out nothing: overlays draw on top, as before occlusion
        Off,
    };

    Strategy parseStrategy(const std::string& name);

    /**
     * The strategy in force, published by the game thread from config and read by the render thread.
     */
    Strategy activeStrategy();

    /**
     * Whether anyone is investigating the capture ([Debug] bSceneDepthDiagnostics): the watch rows are
     * drawn, and the shadow policies run.
     *
     * Off by default, for two different reasons that happen to want the same switch. The shadows have
     * both already answered - see the framework docs/tech/scene-depth-occlusion.md - and one costs an
     * OMGetDepthStencilState per committed graphics state, dozens a frame, to reach a conclusion
     * already reached; the code stays only so the experiment can be re-run against a setup where the
     * occlusion misbehaves. The watch rows cost nothing but belong to whoever came to look at the
     * capture, not to everyone who happens to open the debug overlay.
     *
     * Turning it on is what makes the readout whole: with the shadows dormant the comparison row can
     * only ever say "no shadows", so the two travel together.
     *
     * The log summary is deliberately NOT gated on this - see logSummary.
     */
    bool diagnosticsEnabled();

    /**
     * Where the capture stopped for one committed graphics state. Counted as relaxed atomics and
     * dumped alongside a failure, because "no depth this frame" on its own does not say whether the
     * seam saw no targets, the wrong buffer, or a buffer it could not make a read-only view of.
     */
    enum class CaptureStage : std::uint8_t
    {
        Attempt,
        AlreadyCaptured,
        NoContext,
        NoTargets,
        NoDepthResource,
        NoDepthState,
        DepthDisabled,
        DepthFuncRejected,
        NotATexture,
        WrongSize,
        ReadOnlyViewFailed,
        Captured,

        Count
    };

    /**
     * Why a submit got no depth, or that it got one. Named per RPS UI Framework's
     * AcquireForSubmittedTarget chain (GPL-3.0-or-later, reused with attribution), whose point is
     * that every rejection says which rule rejected it rather than handing back a silent null view.
     */
    enum class AcquireOutcome : std::uint8_t
    {
        // handed out the engine's own buffer
        Ok,
        // handed out the framework's resampled copy of it
        OkResampled,
        NoTexture,
        NoCapture,
        StaleCapture,
        SubmittedLayoutMismatch,
        DifferentDevice,
        NoWorldCopy,
        StrategyOff,

        Count
    };

    const char* stageName(CaptureStage stage);
    const char* outcomeName(AcquireOutcome outcome);

    void countStage(CaptureStage stage);
    void recordAcquire(AcquireOutcome outcome);

    /**
     * Every stage that has fired, as "name count" pairs, for dumping beside a failure: which rule
     * refused the capture only reads as a cause next to how often the others were reached.
     */
    std::string stageCountersText();

    /**
     * What the tally elected for a finished frame, and what it cost to elect it - the instrumentation
     * that turns the election from an unguarded guess (it produces a valid-looking buffer with the
     * wrong contents and nothing downstream can tell) into a logged one.
     */
    struct Election
    {
        D3D11_VIEWPORT viewport{};
        // passes the winner drew, and the best of the rest: close counts mean the election is a coin toss
        std::uint32_t passes = 0;
        std::uint32_t runnerUpPasses = 0;
        std::uint32_t groups = 0;
        // a fifth distinct viewport was dropped for want of a group
        bool groupCapHit = false;
        // the winner is not the viewport the previous frame elected
        bool changedIdentity = false;
    };

    void recordElection(const Election& election);

    /**
     * How the frame's copy went: the pass it was planned for (from the previous two frames), how many
     * world passes the frame actually drew, and whether it got that far - a frame that falls short
     * makes no copy at all and the previous frame's is silently reused.
     */
    void recordCopyPlan(std::uint32_t plannedPass, std::uint32_t worldPasses, bool reachedPlan);

    /**
     * A decision computed two ways. The active answer is the one used; the shadow's is only compared,
     * so a new strategy bakes against real hardware before it is ever allowed to decide a pixel.
     */
    enum class Comparison : std::uint8_t
    {
        // the election over every pass vs the election over depth-testing passes only. Also work
        // item B's landing pad: the engine's own render-context viewport is a third answer here
        WorldRegion,
        // work item C: the buffer matched by size vs the one resolved by logical scene-depth role
        DepthSelection,
        // the two-frame min that decides which pass the copy is taken at, against the previous
        // frame's count alone - the jitter the min exists to absorb, measured
        CopyPass,

        Count
    };

    /**
     * Record one comparison and say whether the caller should log it in full.
     *
     * True for the first few divergences only, so the context that makes a divergence actionable -
     * the viewport values, the pass counts, whether an upscaler was detected - is formatted at the
     * call site where it is known, and never on the steady-state render path. After that the
     * divergence is only counted.
     */
    bool recordComparison(Comparison which, bool agreed);

    /**
     * Note the shape of the buffer the capture is working with, for the readout.
     */
    void recordBufferShape(const D3D11_TEXTURE2D_DESC& desc);

    /**
     * Publish the capture's state to the debug-draw watch table, and the config's strategy to the
     * render thread. GAME thread only - watch() is, and the config's strings are rewritten by the
     * file watcher under the render thread's feet otherwise.
     *
     * Registered as a frame-end callback by the Submit hook host when it installs, so a mod that
     * draws no overlay never pumps it.
     */
    void onGameFrameEnd();
}
