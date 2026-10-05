#pragma once

#include <cstdint>
#include <vector>

#include "ImGuiCanvas.h"
#include "ImGuiPointerLogic.h"

namespace f4cf::imgui
{
    /**
     * The hands whose ray can point at interactive canvases.
     */
    enum class PointerHands : std::uint8_t
    {
        Both,
        Primary,
        Offhand,
    };

    /**
     * What the pointer does in the ImGui frame that was built last.
     */
    struct PointerState
    {
        // the interactive canvas the pointer is on, nullptr while it is on none
        const Canvas* canvas = nullptr;

        // where the pointer is on that canvas, in its layout pixels from its top left corner
        float x = 0.0f;
        float y = 0.0f;

        // the hand that points: true for the primary hand, false for the offhand
        bool primaryHand = true;

        // the trigger of that hand is down
        bool down = false;

        // the ray in the world: where it starts, and where it meets the canvas
        RE::NiPoint3 rayOrigin;
        RE::NiPoint3 hitPosition;

        // ImGui's io.WantCaptureMouse: the pointer is over a canvas, a widget holds it, or a popup is open
        bool wantsPointer = false;
    };
}

namespace f4cf::imgui::internal
{
    /**
     * One hand's pointer in a frame: a ray in the world, and whether it presses. Where the sample comes from
     * is apart from what is done with it, so it can come from something other than a wand.
     */
    struct PointerSample
    {
        // false when the hand has no pointer in this frame
        bool valid = false;

        RE::NiPoint3 origin;

        // of length 1
        RE::NiPoint3 direction;

        bool down = false;
    };

    /**
     * An interactive canvas the pointer can be on in a frame: three corners of its quad in the world, and
     * where its pixels start in ImGui's display.
     */
    struct PointerTarget
    {
        const Canvas* canvas = nullptr;
        RE::NiPoint3 topLeft;
        RE::NiPoint3 topRight;
        RE::NiPoint3 bottomLeft;
        float displayX = 0.0f;
        float displayY = 0.0f;
    };
}

namespace f4cf::imgui
{
    /**
     * The pointer of the interactive canvases (Canvas::setInteractive): the ray of a wand is ImGui's mouse
     * on the canvas it meets, and the wand's trigger is the left mouse button.
     *
     * ImGui has one mouse, so there is one pointer, imgui::pointer(), and one hand owns it at a time. A hand
     * whose ray is alone on a canvas owns it. With both rays on a canvas it is the hand that pressed its
     * trigger on a canvas last, and the primary hand before either has. The owner keeps the pointer while it
     * holds its trigger down.
     *
     *     imgui::pointer().setHands(imgui::PointerHands::Primary);
     *     if (imgui::pointer().state().canvas) { ... }
     */
    class Pointer
    {
    public:
        static Pointer& get();

        void setHands(PointerHands hands);
        void setOffset(const RE::NiTransform& offset);

        /**
         * What the pointer does. It is updated when the ImGui frame is built, after the mod's frame update: a
         * content callback reads this frame's, and the mod's frame update the one before.
         */
        const PointerState& state() const
        {
            return _state;
        }

        // Internal: used by the layer while building a frame.
        void update(const std::vector<internal::PointerTarget>& targets);
        void setWanted(bool wanted);
        void clearState();
        void onCanvasRemoved(const Canvas* canvas);

    private:
        /**
         * One hand in a frame: its pointer, and the nearest target its ray is on, if any.
         */
        struct HandPointer
        {
            internal::PointerSample sample;
            const internal::PointerTarget* target = nullptr;
            internal::QuadHit hit;
        };

        Pointer();

        internal::PointerSample sampleWand(bool primaryHand) const;
        HandPointer pointHand(bool primaryHand, const std::vector<internal::PointerTarget>& targets) const;
        void release();

        PointerHands _hands = PointerHands::Both;

        // from the UI node of the right wand to its ray: the ray starts at the offset's position and runs along its +Y
        RE::NiTransform _offset;

        PointerState _state;
        internal::PointerOwnership _ownership;

        // ImGui was given a pointer position, and was not yet told that the pointer is gone
        bool _given = false;
    };

    /**
     * The pointer, in short: imgui::pointer().state().
     */
    inline Pointer& pointer()
    {
        return Pointer::get();
    }
}
