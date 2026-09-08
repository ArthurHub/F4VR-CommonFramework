#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace f4cf::render
{
    /**
     * RGBA color, components in [0,1].
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

    // Hard budgets so a runaway producer degrades gracefully instead of ballooning GPU buffers.
    constexpr std::size_t MAX_LINE_VERTICES = 65536;
    constexpr std::uint32_t TEXT_VERTEX_CAPACITY = 131072;

    /**
     * Where a text entry lives, and therefore how it is projected.
     */
    enum class TextPlacement : std::uint8_t
    {
        // 2D on the headset view, in target pixels, duplicated into both eye halves
        Screen,
        // 2D on the headset view but positioned at a world anchor's projected screen spot, so the
        // text stays screen-upright while tracking a world object
        WorldAnchored,
        // a world-space quad welded to the anchor and turned to face the viewer, so it tilts and
        // shrinks with the world like real geometry
        Billboard,
    };

    /**
     * Horizontal alignment of a text row about its anchor's projected X (WorldAnchored only):
     * Left starts at the anchor, Center straddles it, Right ends at it.
     */
    enum class TextAlign : std::uint8_t
    {
        Left,
        Center,
        Right,
    };

    /**
     * One world-space line segment; every wire primitive tessellates down to these.
     */
    struct LineSegment
    {
        RE::NiPoint3 start;
        RE::NiPoint3 end;
        Color color;
    };

    /**
     * One text run. x/y are screen pixels for Screen placement and an offset from the projected
     * anchor for WorldAnchored; both are ignored for Billboard, which centres on the anchor.
     */
    struct TextEntry
    {
        std::string text;
        float x = 18.0f;
        float y = 18.0f;
        float size = 2.0f;
        Color color = colors::White;
        RE::NiPoint3 worldAnchor{};
        TextPlacement placement = TextPlacement::Screen;
        TextAlign align = TextAlign::Left;
    };

    /**
     * One frame's worth of primitives to draw over the VR view, built on the GAME thread and handed
     * to PrimitiveDrawRenderer, which replays it on the render thread. Anything a producer wants
     * drawn - lines, glyph text, world labels - reduces to the two lists here, so a producer never
     * touches D3D and never runs render-side.
     *
     * The lists are public: producers with their own budget accounting or ordering rules (the debug
     * overlay sorts by color to batch runs) manipulate them directly, while the add* helpers cover
     * the common case and enforce the vertex budgets.
     */
    struct PrimitiveDraw
    {
        std::vector<LineSegment> lines;
        std::vector<TextEntry> texts;

        // Head position captured game-side, used to turn Billboard text toward the viewer. Only
        // needed when the frame carries billboard text.
        RE::NiPoint3 viewerPosition{};

        bool empty() const
        {
            return lines.empty() && texts.empty();
        }

        void clear()
        {
            lines.clear();
            texts.clear();
        }

        /**
         * Append a world-space line. False when the vertex budget is full, so a caller that wants to
         * report drops can count them.
         */
        bool addLine(const RE::NiPoint3& start, const RE::NiPoint3& end, const Color& color)
        {
            if (lines.size() * 2 + 2 > MAX_LINE_VERTICES) {
                return false;
            }
            lines.push_back(LineSegment{ .start = start, .end = end, .color = color });
            return true;
        }

        /**
         * Append 2D text at a screen pixel position, drawn into both eye halves.
         */
        void addText(const std::string_view text, const float x, const float y, const Color& color = colors::White, const float size = 2.0f)
        {
            texts.push_back(TextEntry{ .text = std::string(text), .x = x, .y = y, .size = size, .color = color });
        }

        /**
         * Append screen-upright text pinned to a world anchor's projected position, offset by x/y
         * pixels and aligned about that spot.
         */
        void addWorldAnchoredText(const std::string_view text, const RE::NiPoint3& worldAnchor, const float x, const float y, const Color& color = colors::White,
            const float size = 2.0f, const TextAlign align = TextAlign::Left)
        {
            texts.push_back(TextEntry{ .text = std::string(text),
                .x = x,
                .y = y,
                .size = size,
                .color = color,
                .worldAnchor = worldAnchor,
                .placement = TextPlacement::WorldAnchored,
                .align = align });
        }

        /**
         * Append world-space text welded to an anchor and facing the viewer. Remember to set
         * viewerPosition on the frame, or every billboard faces the world origin.
         */
        void addBillboardText(const std::string_view text, const RE::NiPoint3& worldAnchor, const Color& color = colors::White, const float size = 2.0f)
        {
            texts.push_back(TextEntry{ .text = std::string(text), .size = size, .color = color, .worldAnchor = worldAnchor, .placement = TextPlacement::Billboard });
        }
    };
}
