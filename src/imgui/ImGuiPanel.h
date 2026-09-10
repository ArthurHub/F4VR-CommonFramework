#pragma once

#include <functional>
#include <string>

#include "../render/PrimitiveDraw.h"

namespace f4cf::imgui
{
    /**
     * Defaults for setBorder's optional arguments, and the panel's content inset, all in layout
     * pixels - Panel measures in layout pixels throughout; UICanvas is the one that speaks vrui
     * units. The padding default is ImGui's own WindowPadding.
     */
    inline constexpr float PANEL_BORDER_THICKNESS_PIXELS = 4.0f;
    inline constexpr float PANEL_BORDER_CORNER_RADIUS_PIXELS = 20.0f;
    inline constexpr float PANEL_PADDING_PIXELS = 8.0f;

    /**
     * Space between a panel's content and its edge, one value per side, in layout pixels.
     *
     * Aggregate-initialised in CSS order - { top, right, bottom, left }. This is vrui::UIPadding's
     * idea in the units Panel works in; UICanvas is what converts between the two.
     */
    struct PanelPadding
    {
        float top = PANEL_PADDING_PIXELS;
        float right = PANEL_PADDING_PIXELS;
        float bottom = PANEL_PADDING_PIXELS;
        float left = PANEL_PADDING_PIXELS;
    };

    /**
     * Largest a panel may be in either dimension, in layout pixels; a panel bigger than this can
     * never be placed.
     *
     * It is also the layout size of the atlas every panel shares. The texture behind it is this times
     * the supersample factor on each side (see setSupersample) - one RGBA8 texture, about 9MB at 1.5
     * and 16MB at 2, cleared and re-rasterized on each frame a panel is visible. Nothing is allocated
     * until the first panel draws, so a mod with no panels pays none of it.
     */
    inline constexpr int MAX_PANEL_PIXEL_SIZE = 1024;

    /**
     * Where a panel's quad sits in the world, resolved on the GAME thread each frame.
     *
     * `transform.rotate` defines the quad's plane using the game's own axes: local +X is the panel's
     * right, local +Z is its up, and local +Y is the normal it faces along. `transform.translate` is
     * the panel's centre. Scale is ignored - size comes from the world extents below, so panel
     * pixels and world size are set independently (together they are the panel's legibility).
     */
    struct PanelPlacement
    {
        RE::NiTransform transform;
        float worldWidth = 30.0f;
        float worldHeight = 20.0f;
    };

    /**
     * Resolves this frame's placement. Runs on the GAME thread, so it may read nodes and game state
     * freely - which is the whole point: node->world must never be read from the render thread.
     * Return false to skip the panel this frame (hidden, culled, node not available yet).
     */
    using PlacementProvider = std::function<bool(PanelPlacement&)>;

    /**
     * Emits the panel's ImGui widgets. Runs on the GAME thread, inside the framework's
     * NewFrame/Render pair and inside a Begin/End for this panel, so it should call ImGui widget
     * functions only - no Begin/End of its own for the panel window, and no D3D.
     */
    using ContentCallback = std::function<void()>;

    /**
     * A Dear ImGui panel drawn over the VR view as a world-space quad.
     *
     * Panels share one ImGui context and one atlas texture: each panel is an ImGui window packed
     * into its own sub-rect of that atlas, and each is composited as one textured quad. So N panels
     * cost one ImGui frame and one draw call, not N of each.
     *
     * The panel is a GAME-thread object. Construct it, give it content and a placement, and the
     * framework pumps it every frame; destroying it removes it. Nothing here touches D3D or the
     * render thread - the frame is cloned and published across that boundary for you.
     *
     *     _panel = std::make_unique<imgui::Panel>("BeamTuning", 512, 320);
     *     _panel->setContent([this] { ImGui::Text("FOV: %.1f", beamFov()); });
     *     _panel->setPlacement([this](imgui::PanelPlacement& out) {
     *         out.transform = _container->worldTransform();
     *         out.worldWidth = 16.0f;
     *         out.worldHeight = 10.0f;
     *         return _container->isVisible();
     *     });
     *
     * Panels draw on top of everything, including geometry in front of them, and are not
     * depth-tested. Overlapping panels are drawn back to front by distance to the viewer.
     */
    class Panel
    {
    public:
        /**
         * @param name identifies the panel in logs and as its ImGui window id - keep it unique.
         * @param pixelWidth / pixelHeight the panel's resolution in the shared atlas. With the
         *        placement's world size this sets pixels-per-world-unit, i.e. how legible it is.
         */
        Panel(std::string name, int pixelWidth, int pixelHeight);
        ~Panel();

        Panel(const Panel&) = delete;
        Panel& operator=(const Panel&) = delete;
        Panel(Panel&&) = delete;
        Panel& operator=(Panel&&) = delete;

        void setContent(ContentCallback content);
        void setPlacement(PlacementProvider placement);

        /**
         * Change the panel's resolution. Cheap and safe at any time - the atlas is repacked every
         * frame - so a panel that grows or shrinks in the world can keep its pixel density constant
         * instead of being stretched. Clamped to 1..MAX_PANEL_PIXEL_SIZE.
         */
        void setPixelSize(int pixelWidth, int pixelHeight);

        /**
         * Whether the world hides the panel when something is in front of it. On by default, which is
         * what makes a panel read as part of the scene rather than pasted over it.
         *
         * Turn it off for a panel that must always be readable - a warning, or a menu you do not want
         * to lose when you turn and a wall comes between you and it. Occlusion also depends on the
         * framework capturing the engine's depth buffer; where it cannot, every panel draws on top
         * regardless, so this is a preference rather than a guarantee.
         */
        void setOccluded(bool occluded);

        bool isOccluded() const
        {
            return _occluded;
        }

        /**
         * Colour for the panel's text, pushed as ImGuiCol_Text around the content callback. White
         * until asked otherwise; content that colours itself (ImGui::TextColored, its own style
         * push) still wins, so this is the colour everything else falls back to.
         */
        void setTextColor(const render::Color& color);

        const render::Color& textColor() const
        {
            return _textColor;
        }

        /**
         * The panel's background colour, alpha included. Transparent until asked for, so a panel
         * shows only its content until something dresses it - see vrui::UIPanelStyle, which UICanvas
         * takes. ImGui's own dark style is 94% opaque, which reads as a solid slab in VR, so any
         * background wanted here is worth an alpha. It composites correctly because the atlas carries
         * premultiplied colour; see the blend state in ImGuiRenderer.
         */
        void setBackgroundColor(const render::Color& color);

        const render::Color& backgroundColor() const
        {
            return _background;
        }

        /**
         * Draw a border around the panel. Off until called; clearBorder turns it off again.
         *
         * The corner radius rounds the BACKGROUND as well as the border, so the two cannot disagree
         * and leave background showing past the stroke at the corners - which is why the radius lives
         * here rather than being a property of the border alone. setCornerRadius sets it on its own,
         * for a rounded panel with no border.
         *
         * Thickness and radius are in layout pixels. The border is drawn INSIDE the panel's rect, so
         * adding one never changes the element's footprint, and the content is inset by the thickness
         * on top of the padding rather than the border eating into it.
         */
        void setBorder(const render::Color& color, float thicknessPixels = PANEL_BORDER_THICKNESS_PIXELS, float cornerRadiusPixels = PANEL_BORDER_CORNER_RADIUS_PIXELS);

        void clearBorder();

        /**
         * Round the panel's corners without changing the border. Kept separate because the rounding
         * applies to the background whether or not there is a border to stroke.
         */
        void setCornerRadius(float pixels);

        /**
         * Space between the content and the panel's edge, in layout pixels, one value per side. The
         * border's thickness adds to this rather than eating into it, so changing one does not move
         * the other.
         */
        void setPadding(const PanelPadding& padding);

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

        const PanelPadding& padding() const
        {
            return _padding;
        }

        /**
         * Runtime show/hide on top of whatever the placement provider decides. A hidden panel costs
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
        PanelPadding _padding;
        ContentCallback _content;
        PlacementProvider _placement;
    };
}
