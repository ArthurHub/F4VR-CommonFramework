#pragma once

#include <functional>
#include <string>

#include "../render/PrimitiveDraw.h"

namespace f4cf::imgui
{
    /**
     * Defaults for setBorder's optional arguments, and the canvas's content inset, all in layout
     * pixels - Canvas measures in layout pixels throughout; UIImGuiPanel is the one that speaks vrui
     * units. The padding default is ImGui's own WindowPadding.
     */
    inline constexpr float CANVAS_BORDER_THICKNESS_PIXELS = 4.0f;
    inline constexpr float CANVAS_BORDER_CORNER_RADIUS_PIXELS = 20.0f;
    inline constexpr float CANVAS_PADDING_PIXELS = 8.0f;

    /**
     * Space between a canvas's content and its edge, one value per side, in layout pixels.
     *
     * Aggregate-initialised in CSS order - { top, right, bottom, left }. This is vrui::UIPadding's
     * idea in the units Canvas works in; UIImGuiPanel is what converts between the two.
     */
    struct CanvasPadding
    {
        float top = CANVAS_PADDING_PIXELS;
        float right = CANVAS_PADDING_PIXELS;
        float bottom = CANVAS_PADDING_PIXELS;
        float left = CANVAS_PADDING_PIXELS;
    };

    /**
     * Largest a canvas may be in either dimension, in layout pixels; a canvas bigger than this can
     * never be placed.
     *
     * It is also the layout size of the atlas every canvas shares. The texture behind it is this times
     * the supersample factor on each side (see setSupersample) - one RGBA8 texture, about 9MB at 1.5
     * and 16MB at 2, cleared and re-rasterized on each frame a canvas is visible. Nothing is allocated
     * until the first canvas draws, so a mod with no canvases pays none of it.
     */
    inline constexpr int MAX_CANVAS_PIXEL_SIZE = 1024;

    /**
     * Where a canvas's quad sits in the world, resolved on the GAME thread each frame.
     *
     * `transform.rotate` defines the quad's plane using the game's own axes: local +X is the canvas's
     * right, local +Z is its up, and local +Y is the normal it faces along. `transform.translate` is
     * the canvas's centre. Scale is ignored - size comes from the world extents below, so canvas
     * pixels and world size are set independently (together they are the canvas's legibility).
     */
    struct CanvasPlacement
    {
        RE::NiTransform transform;
        float worldWidth = 30.0f;
        float worldHeight = 20.0f;
    };

    /**
     * Resolves this frame's placement. Runs on the GAME thread, so it may read nodes and game state
     * freely - which is the whole point: node->world must never be read from the render thread.
     * Return false to skip the canvas this frame (hidden, culled, node not available yet).
     */
    using PlacementProvider = std::function<bool(CanvasPlacement&)>;

    /**
     * Emits the canvas's ImGui widgets. Runs on the GAME thread, inside the framework's
     * NewFrame/Render pair and inside a Begin/End for this canvas, so it should call ImGui widget
     * functions only - no Begin/End of its own for the canvas window, and no D3D.
     */
    using ContentCallback = std::function<void()>;

    /**
     * A Dear ImGui canvas drawn over the VR view as a world-space quad.
     *
     * Canvases share one ImGui context and one atlas texture: each canvas is an ImGui window packed
     * into its own sub-rect of that atlas, and each is composited as one textured quad. So N canvases
     * cost one ImGui frame and one draw call, not N of each.
     *
     * The canvas is a GAME-thread object. Construct it, give it content and a placement, and the
     * framework pumps it every frame; destroying it removes it. Nothing here touches D3D or the
     * render thread - the frame is cloned and published across that boundary for you.
     *
     *     _canvas = std::make_unique<imgui::Canvas>("BeamTuning", 512, 320);
     *     _canvas->setContent([this] { ImGui::Text("FOV: %.1f", beamFov()); });
     *     _canvas->setPlacement([this](imgui::CanvasPlacement& out) {
     *         out.transform = _container->worldTransform();
     *         out.worldWidth = 16.0f;
     *         out.worldHeight = 10.0f;
     *         return _container->isVisible();
     *     });
     *
     * By default the world hides the parts of a canvas that geometry is in front of - see
     * setOccluded. Canvases never write depth, so they do not hide each other: overlapping ones are
     * drawn back to front by distance to the viewer.
     */
    class Canvas
    {
    public:
        /**
         * @param name identifies the canvas in logs and as its ImGui window id - keep it unique.
         * @param pixelWidth / pixelHeight the canvas's resolution in the shared atlas. With the
         *        placement's world size this sets pixels-per-world-unit, i.e. how legible it is.
         */
        Canvas(std::string name, int pixelWidth, int pixelHeight);
        ~Canvas();

        Canvas(const Canvas&) = delete;
        Canvas& operator=(const Canvas&) = delete;
        Canvas(Canvas&&) = delete;
        Canvas& operator=(Canvas&&) = delete;

        void setContent(ContentCallback content);
        void setPlacement(PlacementProvider placement);

        /**
         * Change the canvas's resolution. Cheap and safe at any time - the atlas is repacked every
         * frame - so a canvas that grows or shrinks in the world can keep its pixel density constant
         * instead of being stretched. Clamped to 1..MAX_CANVAS_PIXEL_SIZE.
         */
        void setPixelSize(int pixelWidth, int pixelHeight);

        /**
         * Whether the world hides the canvas when something is in front of it. On by default, which is
         * what makes a canvas read as part of the scene rather than pasted over it.
         *
         * Turn it off for a canvas that must always be readable - a warning, or a menu you do not want
         * to lose when you turn and a wall comes between you and it. Occlusion also depends on the
         * framework capturing the engine's depth buffer; where it cannot, every canvas draws on top
         * regardless, so this is a preference rather than a guarantee.
         */
        void setOccluded(bool occluded);

        bool isOccluded() const
        {
            return _occluded;
        }

        /**
         * Colour for the canvas's text, pushed as ImGuiCol_Text around the content callback. White
         * until asked otherwise; content that colours itself (ImGui::TextColored, its own style
         * push) still wins, so this is the colour everything else falls back to.
         */
        void setTextColor(const render::Color& color);

        const render::Color& textColor() const
        {
            return _textColor;
        }

        /**
         * The canvas's background colour, alpha included. Transparent until asked for, so a canvas
         * shows only its content until something dresses it - see vrui::UIPanelStyle, which
         * UIImGuiPanel takes. ImGui's own dark style is 94% opaque, which reads as a solid slab in VR,
         * so any background wanted here is worth an alpha. It composites correctly because the atlas
         * carries premultiplied colour; see the blend state in ImGuiRenderer.
         */
        void setBackgroundColor(const render::Color& color);

        const render::Color& backgroundColor() const
        {
            return _background;
        }

        /**
         * Draw a border around the canvas. Off until called; clearBorder turns it off again.
         *
         * The corner radius rounds the BACKGROUND as well as the border, so the two cannot disagree
         * and leave background showing past the stroke at the corners - which is why the radius lives
         * here rather than being a property of the border alone. setCornerRadius sets it on its own,
         * for a rounded canvas with no border.
         *
         * Thickness and radius are in layout pixels. The border is drawn INSIDE the canvas's rect, so
         * adding one never changes the element's footprint, and the content is inset by the thickness
         * on top of the padding rather than the border eating into it.
         */
        void setBorder(const render::Color& color, float thicknessPixels = CANVAS_BORDER_THICKNESS_PIXELS, float cornerRadiusPixels = CANVAS_BORDER_CORNER_RADIUS_PIXELS);

        void clearBorder();

        /**
         * Round the canvas's corners without changing the border. Kept separate because the rounding
         * applies to the background whether or not there is a border to stroke.
         */
        void setCornerRadius(float pixels);

        /**
         * Space between the content and the canvas's edge, in layout pixels, one value per side. The
         * border's thickness adds to this rather than eating into it, so changing one does not move
         * the other.
         */
        void setPadding(const CanvasPadding& padding);

        /**
         * The same padding on all four sides.
         */
        void setPadding(float pixels);

        const render::Color& borderColor() const
        {
            return _borderColor;
        }

        float borderThickness() const
        {
            return _borderThickness;
        }

        float cornerRadius() const
        {
            return _cornerRadius;
        }

        const CanvasPadding& padding() const
        {
            return _padding;
        }

        /**
         * Runtime show/hide on top of whatever the placement provider decides. A hidden canvas costs
         * nothing: it is not packed, not drawn, and does not run its content callback.
         */
        void setVisible(bool visible);
        bool isVisible() const;

        const std::string& name() const;
        int pixelWidth() const;
        int pixelHeight() const;

        // Internal: used by the layer while building a frame.
        const ContentCallback& content() const;
        const PlacementProvider& placement() const;

    private:
        std::string _name;
        int _pixelWidth;
        int _pixelHeight;
        bool _visible = true;
        bool _occluded = true;
        render::Color _textColor = render::colors::White;

        // alpha 0 is what "no background" means; ImGui skips a fully transparent fill outright
        render::Color _background{ .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 0.0f };

        // zero thickness is what "no border" means, so no separate flag is needed
        render::Color _borderColor = render::colors::White;
        float _borderThickness = 0.0f;
        float _cornerRadius = 0.0f;
        CanvasPadding _padding;
        ContentCallback _content;
        PlacementProvider _placement;
    };
}
