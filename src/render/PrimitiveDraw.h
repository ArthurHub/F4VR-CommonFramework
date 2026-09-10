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

        /**
         * A colour from the 0-255 bytes a colour picker reports, rather than the 0-1 floats stored
         * here.
         *
         * Sampling a colour off a screenshot gives bytes, and putting those straight into the fields
         * saturates every channel to white, since anything at or above 1.0 is full brightness. This
         * is the conversion, so a sampled value can stay written the way it was read.
         *
         * The overlay draws into the eye texture after the engine has finished tonemapping it, and
         * that target is not sRGB-encoded, so no gamma step comes between these bytes and the final
         * image: a byte in is that byte out.
         */
        static constexpr Color rgba(const int red, const int green, const int blue, const int alpha = 255)
        {
            return { static_cast<float>(red) / 255.0f, static_cast<float>(green) / 255.0f, static_cast<float>(blue) / 255.0f, static_cast<float>(alpha) / 255.0f };
        }

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
    constexpr std::size_t MAX_FILL_TRIANGLES = 8192;

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
        // a world-space quad welded to the anchor and lying in a plane the CALLER chooses, at a
        // fixed world size. Unlike Billboard it does not turn toward the viewer, so it foreshortens
        // as you move around it: text ON a surface, rather than text held up to the camera.
        Oriented,
    };

    /**
     * Horizontal alignment of a text row about its anchor (WorldAnchored and Oriented): Left starts
     * at the anchor, Center straddles it, Right ends at it.
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
     * One filled world-space triangle: panel borders, backgrounds, any solid shape the caller
     * tessellates.
     *
     * Solid geometry exists as its own list because a line CANNOT be thick - D3D11 rasterizes every
     * line one pixel wide, whatever the API asks for - so anything that needs width has to be built
     * from triangles. Culling is off and depth testing is disabled, so winding does not matter and
     * these are painted in the order they were added, under any text added after them.
     */
    struct FillTriangle
    {
        RE::NiPoint3 a;
        RE::NiPoint3 b;
        RE::NiPoint3 c;
        Color color;
    };

    /**
     * One text run, in the framework's font - render::measureText (TextFont.h) gives its width.
     * Three fields change meaning with the placement, because the placements measure in different
     * spaces:
     *
     * - x/y: screen pixels for Screen, an offset from the projected anchor for WorldAnchored, a
     *   world offset along right/up for Oriented, and ignored for Billboard (it centres on the
     *   anchor).
     * - size: how big the text is, as the height of a capital letter. For Screen and WorldAnchored
     *   each step of size is 7 pixels of it (size 2 draws 14px capitals); for Billboard it is an
     *   apparent scale, since the label sizes itself by viewer distance; for Oriented it is the
     *   WORLD HEIGHT of a capital, whose whole point is a fixed physical size.
     * - right/up: the plane for Oriented, ignored otherwise. They are normalized on use, so any
     *   length works, but they must not be parallel.
     */
    struct TextEntry
    {
        std::string text;
        float x = 18.0f;
        float y = 18.0f;
        float size = 2.0f;
        Color color = colors::White;
        RE::NiPoint3 worldAnchor{};
        RE::NiPoint3 right{};
        RE::NiPoint3 up{};
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
        std::vector<FillTriangle> triangles;
        std::vector<TextEntry> texts;

        // Head position captured game-side, used to turn Billboard text toward the viewer. Only
        // needed when the frame carries billboard text.
        RE::NiPoint3 viewerPosition{};

        bool empty() const
        {
            return lines.empty() && triangles.empty() && texts.empty();
        }

        void clear()
        {
            lines.clear();
            triangles.clear();
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
         * Append a filled world-space triangle. False when the budget is full, so a caller that wants
         * to report drops can count them.
         */
        bool addTriangle(const RE::NiPoint3& a, const RE::NiPoint3& b, const RE::NiPoint3& c, const Color& color)
        {
            if (triangles.size() + 1 > MAX_FILL_TRIANGLES) {
                return false;
            }
            triangles.push_back(FillTriangle{ .a = a, .b = b, .c = c, .color = color });
            return true;
        }

        /**
         * Append a filled quad as two triangles; the corners go round the perimeter, in either
         * direction. The budget is checked for both halves up front, so a full buffer drops the whole
         * quad rather than leaving a triangular remnant.
         */
        bool addQuad(const RE::NiPoint3& a, const RE::NiPoint3& b, const RE::NiPoint3& c, const RE::NiPoint3& d, const Color& color)
        {
            if (triangles.size() + 2 > MAX_FILL_TRIANGLES) {
                return false;
            }
            addTriangle(a, b, c, color);
            addTriangle(a, c, d, color);
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

        /**
         * Append world-space text lying in the plane spanned by right/up, at a fixed world size.
         *
         * The run advances along +right and its rows descend along -up, so a caller laying out
         * several rows steps the anchor down by -up. Unlike a billboard it stays welded to the plane
         * and foreshortens as the viewer moves, which is what makes it read as text on a surface.
         *
         * @param textHeight world height of a capital letter; render::measureText gives the run's
         *        width at it.
         * @param offsetRight / offsetUp world offset from the anchor along the plane's own axes.
         */
        void addOrientedText(const std::string_view text, const RE::NiPoint3& worldAnchor, const RE::NiPoint3& right, const RE::NiPoint3& up, const float textHeight,
            const Color& color = colors::White, const TextAlign align = TextAlign::Left, const float offsetRight = 0.0f, const float offsetUp = 0.0f)
        {
            texts.push_back(TextEntry{ .text = std::string(text),
                .x = offsetRight,
                .y = offsetUp,
                .size = textHeight,
                .color = color,
                .worldAnchor = worldAnchor,
                .right = right,
                .up = up,
                .placement = TextPlacement::Oriented,
                .align = align });
        }
    };
}
