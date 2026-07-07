#pragma once

#include <atomic>
#include <format>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace f4cf::debug
{
    /**
     * RGBA color for debug draws, components in [0,1].
     */
    struct Color
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;

        bool operator==(const Color&) const = default;
    };

    namespace colors
    {
        inline constexpr Color Red{ 1, 0, 0 };
        inline constexpr Color Green{ 0, 1, 0 };
        inline constexpr Color Blue{ 0, 0.6f, 1 };
        inline constexpr Color Yellow{ 1, 1, 0 };
        inline constexpr Color Cyan{ 0, 1, 1 };
        inline constexpr Color Magenta{ 1, 0.25f, 0.95f };
        inline constexpr Color Orange{ 1, 0.6f, 0.1f };
        inline constexpr Color White{ 1, 1, 1 };
        inline constexpr Color Grey{ 0.6f, 0.6f, 0.6f };
    }

    namespace internal
    {
        // Hard budgets so a runaway draw loop degrades gracefully instead of ballooning GPU buffers
        // (ROCK enforces the same idea via DebugOverlayLineBatch, reference library
        // knowledge-base/debug_draw_overlay.md section 6).
        constexpr std::size_t MAX_LINE_VERTICES = 65536;
        constexpr std::uint32_t TEXT_VERTEX_CAPACITY = 131072; // ROCK DebugBodyOverlay.cpp:65 (kTextVertexCapacity)

        // Horizontal alignment of a text row about the anchor's projected X (world-anchored only):
        // Left starts at the anchor, Center straddles it, Right ends at it.
        enum class TextAlign : std::uint8_t
        {
            Left,
            Center,
            Right,
        };

        /**
         * One world-space wire segment; every primitive tessellates down to these.
         */
        struct LineSegment
        {
            RE::NiPoint3 start;
            RE::NiPoint3 end;
            Color color;
        };

        /**
         * One text entry. Three modes: screen-space HUD (worldAnchored=false), world-anchored screen
         * text projected to the anchor's screen spot (worldAnchored, !billboard — used by the HUD watch
         * table), and a world-space camera-facing billboard welded to the anchor (billboard — used by
         * label(), so an in-world tag tilts with the world instead of staying screen-upright).
         */
        struct TextEntry
        {
            std::string text;
            float x = 18.0f;
            float y = 18.0f;
            float size = 2.0f;
            Color color = colors::White;
            RE::NiPoint3 worldAnchor{};
            bool worldAnchored = false;
            bool billboard = false;
            TextAlign align = TextAlign::Left;
        };

        /**
         * The full set of draws for one frame, handed from the game-thread producer to the
         * render-thread consumer (DebugDrawRenderer) as an immutable snapshot. cameraPos is the head
         * position captured on the game thread, used to orient billboard labels toward the viewer.
         */
        struct RenderFrame
        {
            std::vector<LineSegment> lines;
            std::vector<TextEntry> texts;
            RE::NiPoint3 cameraPos{};

            bool empty() const
            {
                return lines.empty() && texts.empty();
            }

            void clear()
            {
                lines.clear();
                texts.clear();
            }
        };
    }

    /**
     * Immediate-mode in-world debug draw overlay: boxes, spheres, lines, arrows, axes, cones, HUD
     * text, and a value-watch table drawn on top of the VR view via a D3D11 wire renderer injected
     * at the OpenVR IVRCompositor::Submit hook.
     *
     * Contract (same semantics as Unreal DrawDebug* / Bullet debug draw):
     * - Call a draw method EVERY FRAME you want the shape visible (from onFrameUpdate); the command
     *   list auto-clears at each frame boundary. To make a shape disappear just stop calling.
     * - Pass sec > 0 to fire once and have the helper re-emit the shape for that many seconds
     *   (event flashes: "show a red sphere where the hit landed for 2s").
     * - All inputs are game-world coordinates (NiPoint3/NiTransform); physics callers convert via
     *   havokToGame() first.
     * - Game thread only (drive from ModBase's onFrameUpdate); rendering happens on the render
     *   thread from a double-buffered snapshot.
     *
     * Zero cost until used: no hook is installed and the per-frame driver early-returns until the
     * first draw/watch call of the session; the OpenVR Submit hook + D3D resources are then
     * installed lazily. When the overlay is disabled (INI bDebugDrawEnabled, setEnabled(false), or
     * the sDebugDrawToggleBinding hotkey) appends are skipped at the call site.
     *
     * Channels: setChannel()/channelScope() tags subsequent draws with a name so independent systems
     * can be toggled separately (INI sDebugDrawDisabledChannels or setChannelEnabled()).
     *
     * Design + provenance: reference library knowledge-base/debug_draw_overlay.md (a generalization
     * of ROCK's DebugBodyOverlay).
     *
     * Credit: based on brunocatani work in https://github.com/brunocatani/ROCK
     */
    class DebugDraw
    {
    public:
        static DebugDraw& get();

        /**
         * RAII channel tag: draws issued while alive are tagged with the given channel; restores the
         * previous channel on destruction.
         */
        class ChannelScope
        {
        public:
            ChannelScope(DebugDraw& owner, const std::string_view channel)
                : _owner(owner),
                  _previous(owner._channel)
            {
                owner.setChannel(channel);
            }

            ~ChannelScope()
            {
                _owner.setChannel(_previous);
            }

            ChannelScope(const ChannelScope&) = delete;
            ChannelScope& operator=(const ChannelScope&) = delete;

        private:
            DebugDraw& _owner;
            std::string _previous;
        };

        // --- master switch + channels ---
        void setEnabled(bool enabled);
        bool isEnabled() const;
        void setChannel(std::string_view channel);

        ChannelScope channelScope(const std::string_view channel)
        {
            return { *this, channel };
        }

        void setChannelEnabled(std::string_view channel, bool enabled);

        // --- parametric primitives (game-world coords; optional persist seconds) ---
        void line(const RE::NiPoint3& start, const RE::NiPoint3& end, const Color& color, float sec = 0);
        void point(const RE::NiPoint3& pos, float size, bool distanceScaled, const Color& color, float sec = 0);
        void arrow(const RE::NiPoint3& origin, const RE::NiPoint3& dir, float len, const Color& color, float sec = 0);
        void boxAabb(const RE::NiPoint3& center, const RE::NiPoint3& halfExtents, bool distanceScaled, const Color& color, float sec = 0);
        void boxObb(const RE::NiTransform& transform, const RE::NiPoint3& halfExtents, bool distanceScaled, const Color& color, float sec = 0);
        void sphere(const RE::NiPoint3& center, float radius, bool distanceScaled, const Color& color, float sec = 0, int rings = 3);
        void capsule(const RE::NiPoint3& start, const RE::NiPoint3& end, float radius, const Color& color, float sec = 0);
        void cone(const RE::NiPoint3& apex, const RE::NiPoint3& dir, float len, float fullAngleDeg, const Color& color, float sec = 0);
        void axes(const RE::NiTransform& transform, float len, float sec = 0);
        void grid(const RE::NiPoint3& center, float extent, float step, const Color& color, float sec = 0);

        // --- raw escape hatch: "any shape anywhere" ---
        void polyline(std::span<const RE::NiPoint3> points, const Color& color, bool closed = false, float sec = 0);
        void mesh(std::span<const RE::NiPoint3> vertices, std::span<const std::uint16_t> indices, const Color& color, float sec = 0);

        // --- convenience (resolve the node's world transform for you) ---
        void nodeAxes(const RE::NiAVObject* node, float len = 5.0f);
        void nodeBounds(const RE::NiAVObject* node, const Color& color, float sec = 0);

        // --- HUD ---
        void text(std::string_view str, float x, float y, const Color& color = colors::White, float size = 2.0f);
        void label(std::string_view str, const RE::NiPoint3& worldPos, const Color& color = colors::White, float size = 2.0f);
        void watch(std::string_view name, std::string_view value);

        /**
         * Watch any formattable value ("name: value" table, auto-laid-out). By default the table
         * floats a short distance in front of the HMD (horizontally centred, a little below the look
         * axis) so it reads in VR; watchAnchor / watchAnchorNode re-home it to any world point.
         */
        template <class T>
        void watch(const std::string_view name, const T& value)
        {
            watch(name, std::string_view(std::format("{}", value)));
        }

        /**
         * Float the watch table at a world position instead of the default in-front-of-HMD spot, e.g.
         * pinned to the offhand controller as a wrist display. Immediate-mode: call each frame (it
         * resets every frame); unset falls back to the head HUD.
         */
        void watchAnchor(const RE::NiPoint3& worldPos);

        /**
         * Anchor the watch table to a node's world transform (typically the offhand wand). Convenience
         * over watchAnchor that resolves node->world.translate for you; no-op if node is null.
         */
        void watchAnchorNode(const RE::NiAVObject* node);

        /**
         * Convert a Havok-space point to game-world units for drawing (positions read from
         * hknpWorld/motion state). Not baked into the core API — most callers work in game space.
         */
        static RE::NiPoint3 havokToGame(const RE::NiPoint3& point);

        // --- per-frame driver, called by ModBase only (no-ops until the first draw call) ---
        static void onFrameStart(bool configEnabled, const std::string& configDisabledChannels, const std::string& configToggleBinding, const std::string& configHudPlacement);
        static void onFrameEnd();

    private:
        DebugDraw() = default;

        /**
         * Where the default watch-table HUD sits in the view (config sDebugDrawHudPlacement). Bottom
         * row sits a little below centre; the Top row sits higher above it; the horizontal variants
         * bias the block left/right of centre.
         */
        enum class HudPlacement : std::uint8_t
        {
            Center,
            CenterLeft,
            CenterRight,
            CenterTop,
            CenterTopLeft,
            CenterTopRight,
        };

        bool isAppendActive() const
        {
            return _appendActive;
        }

        void refreshAppendActive();
        bool effectiveEnabled() const;
        void addLine(const RE::NiPoint3& start, const RE::NiPoint3& end, const Color& color, float sec);
        void circle(const RE::NiPoint3& center, const RE::NiPoint3& axisU, const RE::NiPoint3& axisV, float radius, const Color& color, float sec, int segments = 24);
        float distanceScale(const RE::NiPoint3& p) const;
        void handleToggleHotkey(const std::string& configToggleBinding);
        void layoutWatchTable();
        std::string channelStatusText() const;
        static HudPlacement parseHudPlacement(const std::string& text);
        // World point in front of the HMD for the default HUD, placed per _hudPlacement, when no
        // explicit watchAnchor is set; nullopt while the HMD node isn't available.
        std::optional<RE::NiPoint3> defaultHudAnchor() const;

        /**
         * A sec>0 shape segment retained on the game thread and re-emitted each frame until expiry.
         */
        struct TimedLine
        {
            internal::LineSegment segment;
            uint64_t expiryMs = 0;
        };

        // set on the first draw/watch call ever; gates all per-frame work (the zero-cost guarantee)
        inline static std::atomic<bool> s_everUsed{ false };

        internal::RenderFrame _building;
        std::vector<TimedLine> _persist;
        std::vector<std::pair<std::string, std::string>> _watch;
        std::optional<RE::NiPoint3> _watchAnchor; // explicit world-anchor for the watch table; reset each frame
        std::set<std::string> _channelsSeen; // channels tagged onto draws this frame (for the watch header); reset each frame
        RE::NiPoint3 _cameraPos{}; // head position captured this frame; drives distance-scaled markers + billboard labels
        std::size_t _rejectedLines = 0;

        // enable state: INI value overridable at runtime (setEnabled / hotkey)
        bool _configEnabled = true;
        std::optional<bool> _enabledOverride;

        // channel gating: current tag + runtime overrides layered over the INI-disabled set
        std::string _channel;
        bool _appendActive = true;
        std::unordered_set<std::string> _configDisabledChannels;
        std::string _configDisabledChannelsRaw;
        std::unordered_map<std::string, bool> _channelOverrides;

        // default-HUD placement, parsed from config on change
        HudPlacement _hudPlacement = HudPlacement::Center;
        std::string _hudPlacementRaw;
    };

    /**
     * Terse free-function facade: f4cf::debug::dd().line(...).
     */
    inline DebugDraw& dd()
    {
        return DebugDraw::get();
    }
}
