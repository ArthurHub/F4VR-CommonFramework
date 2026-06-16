#include "UIButton.h"

#include <format>

namespace f4cf::vrui
{
    UIButton::UIButton(const std::string& nifPath)
        : UIWidget(nifPath)
    {}

    UIButton::UIButton(const std::string& name, RE::NiNode* node)
        : UIWidget(name, node)
    {}

    std::string UIButton::toString() const
    {
        return std::format("UIButton({}): {}{}{}, Pos({:.2f}, {:.2f}, {:.2f}), Size({:.2f}, {:.2f})",
            _name,
            _visible ? "V" : "H",
            _disabled ? "D" : ".",
            isPressable() ? "P" : ".",
            _transform.translate.x,
            _transform.translate.y,
            _transform.translate.z,
            _size.width,
            _size.height);
    }

    void UIButton::setOnPressHandler(std::function<void(UIWidget*)> handler)
    {
        _onPressEventHandler = std::move(handler);
    }

    void UIButton::onPressEventFired(UIElement* element, UIFrameUpdateContext* context)
    {
        UIWidget::onPressEventFired(element, context);
        if (_onPressEventHandler) {
            _onPressEventHandler(this);
        }
    }
}
