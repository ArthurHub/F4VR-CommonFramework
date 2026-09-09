#pragma once

#include <functional>
#include <string>

namespace f4cf::imgui
{
    /**
     * Largest a panel may be in either dimension: panels are packed into one shared atlas texture of
     * this size, and one bigger than the atlas can never be placed.
     *
     * It is also the budget every panel shares, so raising it is not free: the atlas is one RGBA8
     * texture (2048^2 = 16MB) cleared and re-rasterized on each frame a panel is visible. Nothing is
     * allocated until the first panel actually draws, so a mod with no panels pays none of it.
     */
    inline constexpr int MAX_PANEL_PIXEL_SIZE = 2048;

    /**
     * Size the shared panel font is rasterized at, before any panel exists (default 48px).
     *
     * The VR legibility trick is to rasterize far above the nominal on-screen size and scale the
     * QUAD down, never to re-rasterize - so this is not "how big the text looks", it is how much
     * detail the glyphs carry. How big the text looks comes from the panel's pixel size against its
     * placement's world size. Clamped to 16..128.
     */
    void setFontSizePixels(float sizePixels);

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
        ContentCallback _content;
        PlacementProvider _placement;
    };
}
