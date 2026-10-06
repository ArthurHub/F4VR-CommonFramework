#pragma once

#include <cstdint>
#include <vector>

#include "../common/MatrixUtils.h"
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

    /**
     * Where the pointer's ray is on the hand, and how the pointer is drawn while it is on a canvas: the ray
     * from the hand toward the canvas, and a mark on the canvas where the ray meets it. Both are drawn over
     * the panels and are not hidden by the world. Lengths are in world units.
     *
     * Its own defaults are the default pointer, so they are in one place. A mod changes a part by starting
     * from them:
     *
     *     auto style = imgui::pointer().style();
     *     style.rayColor = render::Color::rgba(10, 250, 120, 200);
     *     imgui::pointer().setStyle(style);
     */
    struct PointerStyle
    {
        // Where the ray is, from the UI node of the wand (primaryUIAttachNode, secondaryUIOffsetNode): it
        // starts at the offset's position and runs along its +Y. With no offset it starts at the wand and
        // points the way the hand aims a weapon.
        // It is given for the right hand and mirrored for the left: its x position, and its turns around y
        // and z. Its scale is not used.
        // The default was found in the game: a little in front of the wand, 18 degrees above that aim and 5
        // degrees inward, which is where the hand points.
        RE::NiTransform rayOffset = common::MatrixUtils::getTransform(-2.0f, 3.0f, -1.0f, -18.0f, 0.0f, -5.0f);

        // false draws nothing, for a mod that draws the pointer itself from Pointer::state()
        bool drawn = true;

        // the ray's color, with its opacity as the alpha
        render::Color rayColor = render::Color::rgba(190, 255, 190, 200);

        float rayWidth = 0.2f;

        // the longest the ray is drawn: it ends there, or at the mark when the canvas is nearer
        float rayMaxLength = 25.0f;

        // the length the ray fades in over at its start, and out over at its end. 0 for no fade.
        float rayFade = 1.0f;

        // the mark's color, with its opacity as the alpha
        render::Color markColor = render::Color::rgba(215, 255, 215, 180);

        // the mark's radius at 100 units from the head, its border included. It grows and shrinks with its
        // distance, so it looks the same size on a panel on the hand and on one across the room.
        float markSize = 0.7f;

        // the border around the mark: its color, and its width, measured as the mark's size is. 0 for no border.
        render::Color markBorderColor = render::Color::rgba(215, 255, 215, 100);
        float markBorderWidth = 0.1f;
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
     * The owner's ray and a mark on the canvas are drawn while the pointer is on a canvas. PointerStyle has
     * how they look, and where the ray is on the hand.
     *
     *     imgui::pointer().setHands(imgui::PointerHands::Primary);
     *     if (imgui::pointer().state().canvas) { ... }
     */
    class Pointer
    {
    public:
        static Pointer& get();

        void setHands(PointerHands hands);
        void setStyle(const PointerStyle& style);

        const PointerStyle& style() const
        {
            return _style;
        }

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

        Pointer() = default;

        internal::PointerSample sampleWand(bool primaryHand) const;
        HandPointer pointHand(bool primaryHand, const std::vector<internal::PointerTarget>& targets) const;
        void release();
        void draw(const HandPointer* hand);

        PointerHands _hands = PointerHands::Both;
        PointerStyle _style;
        PointerState _state;
        internal::PointerOwnership _ownership;

        // ImGui was given a pointer position, and was not yet told that the pointer is gone
        bool _given = false;

        // the layer was given a pointer to draw, and was not yet given nothing
        bool _drawn = false;
    };

    /**
     * The pointer, in short: imgui::pointer().state().
     */
    inline Pointer& pointer()
    {
        return Pointer::get();
    }
}
