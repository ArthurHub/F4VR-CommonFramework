#include "UIToggleButtonPanel.h"

#include <algorithm>
#include <utility>

namespace f4cf::vrui
{
    UIToggleButtonPanel::UIToggleButtonPanel(const std::string& name, const float width, const float height)
        : UIButtonPanel(name, width, height)
    {}

    void UIToggleButtonPanel::setToggleState(const bool isToggleOn)
    {
        if (_isToggleOn != isToggleOn) {
            _isToggleOn = isToggleOn;
            onStateChanged(this);
        }
    }

    void UIToggleButtonPanel::setUnToggleAllowed(const bool allowUnToggle)
    {
        _isUnToggleAllowed = allowUnToggle;
    }

    void UIToggleButtonPanel::setOnToggleHandler(std::function<void(UIToggleButtonPanel*, bool)> handler)
    {
        _onToggleHandler = std::move(handler);
    }

    void UIToggleButtonPanel::setToggleFrame(const render::Color& color, const float thickness, const float gap)
    {
        _frameColor = color;
        _frameThicknessUnits = (std::max)(0.0f, thickness);
        _frameGapUnits = (std::max)(0.0f, gap);
    }

    /**
     * Pressable while enabled with a toggle handler, except while on when a press may not turn it off - a
     * group's selected toggle stays put when pressed again.
     */
    bool UIToggleButtonPanel::isPressable() const
    {
        return !isDisabled() && _onToggleHandler != nullptr && !(_isToggleOn && !_isUnToggleAllowed);
    }

    /**
     * Fire the press as a button does - haptic, and the press handler if one is set - then flip the state
     * and report the new one.
     */
    void UIToggleButtonPanel::onPressEventFired(UIElement* element, UIFrameUpdateContext* context)
    {
        if (_isToggleOn && !_isUnToggleAllowed) {
            // not allowed to un-toggle
            return;
        }

        UIButtonPanel::onPressEventFired(element, context);
        setToggleState(!_isToggleOn);
        if (_onToggleHandler) {
            _onToggleHandler(this, _isToggleOn);
        }
    }

    /**
     * While on, outline a rectangle grown past the button's edge by the gap and the frame's thickness, so
     * the ring's inner edge is the gap away from the button and its outer edge a thickness beyond that.
     *
     * The corners are the button's own radius grown by that same distance, which keeps the ring concentric
     * with the button's rounded outline rather than pinching in at the corners. The button's radius is
     * clamped the way its border clamps it, so an oversized radius does not round the frame further than
     * the button itself is rounded.
     */
    void UIToggleButtonPanel::appendAround(render::PrimitiveDraw& frame, const UIPanelContentArea& bounds) const
    {
        if (!_isToggleOn || _frameThicknessUnits <= 0.0f) {
            return;
        }

        const float grow = (_frameGapUnits + _frameThicknessUnits) * bounds.scale;
        const float buttonRadius = (std::min)(resolveStyle().cornerRadiusUnits * bounds.scale, (std::min)(bounds.width, bounds.height) * 0.5f);

        render::Color color = _frameColor;
        if (isDisabled()) {
            color.a *= BUTTON_PANEL_DISABLED_ALPHA;
        }
        appendOutline(frame,
            bounds.center,
            bounds.right,
            bounds.up,
            bounds.width + grow * 2.0f,
            bounds.height + grow * 2.0f,
            _frameThicknessUnits * bounds.scale,
            buttonRadius + grow,
            color);
    }
}
