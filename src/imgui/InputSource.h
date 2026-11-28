#pragma once

#include <vector>

namespace f4cf::imgui
{
    class Canvas;
    struct Theme;
}

namespace f4cf::imgui::internal
{
    /**
     * An interactive canvas that is shown in a frame: three corners of its quad, in the world on VR and in screen pixels
     * on flat, the radius it is curved at on VR, and where its pixels start in ImGui's display.
     */
    struct PointerTarget
    {
        const Canvas* canvas = nullptr;
        RE::NiPoint3 topLeft;
        RE::NiPoint3 topRight;
        RE::NiPoint3 bottomLeft;

        // 0 for a flat canvas. The corners of a curved one are of its quad as it is when flat (CurvedQuad).
        float curveRadius = 0.0f;

        float displayX = 0.0f;
        float displayY = 0.0f;

        // the canvas of the own context (Canvas::setOwnContext): a hand on it is not ImGui's mouse, and is told
        // apart for each hand (Pointer::ownCanvasHand)
        bool ownContext = false;
    };

    /**
     * What the canvases are used with, which the ImGui layer and the controls read the same way on both runtimes, one side
     * per build: VR's is the wand pointer, the thumbstick and the VR keyboard (InputSourceVR.cpp), flat's the game's
     * cursor, the mouse and the keys (InputSourceFlat.cpp). The keys of the game's window reach ImGui on both.
     * Game thread only.
     */
    class InputSource
    {
    public:
        static InputSource& get()
        {
            static InputSource instance;
            return instance;
        }

        void install();
        void update(const std::vector<PointerTarget>& targets, float deltaSeconds);
        void setWanted(bool wantCaptureMouse);
        void afterFrame();
        void onCanvasRemoved(const Canvas* canvas);
        void onFocusLost();
        void applyTheme(const Theme& theme);
        float thumbstickSide() const;

    private:
        InputSource() = default;
    };
}
