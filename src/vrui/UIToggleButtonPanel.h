#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "UIButtonPanel.h"
#include "UIToggleable.h"

namespace f4cf::vrui
{
    /**
     * Thickness of the frame drawn around a toggled-on panel button, in vrui units - btn-border.nif's
     * white ring, 4 texels of a 200-texel, 2-unit button.
     */
    inline constexpr float TOGGLE_BUTTON_PANEL_FRAME_THICKNESS_UNITS = 0.04f;

    /**
     * Clear space between the button's edge and its toggle frame, in vrui units - btn-border.nif's 6
     * texels, so the frame's outer edge sits 0.1 units outside the button.
     */
    inline constexpr float TOGGLE_BUTTON_PANEL_FRAME_GAP_UNITS = 0.06f;

    /**
     * A UIButtonPanel that stays on or off - the panel counterpart of UIToggleButton, composed from text
     * and an image at runtime instead of a NIF.
     *
     *     auto shadows = std::make_shared<vrui::UIToggleButtonPanel>("Shadows");
     *     shadows->setTopText("BEAM");
     *     shadows->setBottomText("SHADOWS");
     *     shadows->setToggleState(shadowsEnabled());
     *     shadows->setOnToggleHandler([](vrui::UIToggleButtonPanel*, bool on) { setShadows(on); });
     *
     * Each press flips the state, then calls the toggle handler with the new one. It is pressable only
     * with a toggle handler, and not while on when un-toggling is not allowed - which
     * UIToggleGroupContainer turns off for every toggle it holds, NIF or panel, so pressing the selected
     * one does nothing.
     *
     * While on it draws a frame AROUND the button, in addition to the button's own border, as
     * UIToggleButton does with btn-border.nif: a white ring a small gap outside the button's edge, its
     * corners rounded to follow the button's. Being outside, it sits in the space the layout leaves
     * between the button and its neighbours, 0.1 units deep by default, so a container needs at least
     * that much padding. setToggleFrame changes its colour, thickness and gap. It follows the button in
     * when pushed, and dims with it when disabled.
     */
    class UIToggleButtonPanel : public UIButtonPanel, public UIToggleable
    {
    public:
        /**
         * @param name identifies the element in logs.
         * @param width / height size in vrui units, border and padding included - 2x2 matches the NIF
         *        buttons.
         */
        explicit UIToggleButtonPanel(const std::string& name, float width = 2.0f, float height = 2.0f);

        bool isToggleOn() const override
        {
            return _isToggleOn;
        }

        void setToggleState(bool isToggleOn) override;

        bool isUnToggleAllowed() const override
        {
            return _isUnToggleAllowed;
        }

        void setUnToggleAllowed(bool allowUnToggle) override;

        /**
         * Called on the game thread each time a press flips the state, with the new state. A toggle with
         * no handler is not pressable.
         */
        void setOnToggleHandler(std::function<void(UIToggleButtonPanel*, bool)> handler);

        /**
         * The frame drawn around the button while it is on. White, 0.04 thick and 0.06 clear of the
         * button's edge until called.
         * @param thickness in vrui units; 0 draws no frame.
         * @param gap clear space between the button's edge and the frame's inner edge, in vrui units.
         */
        void setToggleFrame(const render::Color& color, float thickness = TOGGLE_BUTTON_PANEL_FRAME_THICKNESS_UNITS, float gap = TOGGLE_BUTTON_PANEL_FRAME_GAP_UNITS);

    protected:
        bool isPressable() const override;
        void onPressEventFired(UIElement* element, UIFrameUpdateContext* context) override;
        void appendAround(render::PrimitiveDraw& frame, const UIPanelContentArea& bounds) const override;
        std::string stateFlags() const override;

        std::string_view typeName() const override
        {
            return "UIToggleButtonPanel";
        }

    private:
        std::function<void(UIToggleButtonPanel*, bool)> _onToggleHandler;
        bool _isToggleOn = false;
        bool _isUnToggleAllowed = true;

        render::Color _frameColor = render::colors::White;
        float _frameThicknessUnits = TOGGLE_BUTTON_PANEL_FRAME_THICKNESS_UNITS;
        float _frameGapUnits = TOGGLE_BUTTON_PANEL_FRAME_GAP_UNITS;
    };
}
