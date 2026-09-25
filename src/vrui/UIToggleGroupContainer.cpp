#include "UIToggleGroupContainer.h"

#include <format>

#include "UIManager.h"

namespace f4cf::vrui
{
    UIToggleGroupContainer::UIToggleGroupContainer(const std::string& name, const UIContainerLayout layout, const float padding)
        : UIContainer(name, layout, padding)
    {}

    std::string UIToggleGroupContainer::toString() const
    {
        const auto calculatedSize = calcSize();
        return std::format("UIToggleGroupContainer({}): {}, Pos({:.2f}, {:.2f}, {:.2f}), Size({:.2f}, {:.2f}), CalcSize({:.2f}, {:.2f}), Children({}), Layout({})",
            _name,
            _visible ? "V" : "H",
            _transform.translate.x,
            _transform.translate.y,
            _transform.translate.z,
            _size.width,
            _size.height,
            calculatedSize.width,
            calculatedSize.height,
            _childElements.size(),
            static_cast<int>(_layout));
    }

    /**
     * On toggle of one button, un-toggle all other buttons. Works on the UIToggleable interface, so NIF
     * and panel toggles turn each other off; a change from anything that is not a toggle is ignored.
     */
    void UIToggleGroupContainer::onStateChanged(UIElement* element)
    {
        UIElement::onStateChanged(element);

        const auto changedToggle = dynamic_cast<UIToggleable*>(element);
        if (!changedToggle || !changedToggle->isToggleOn()) {
            return;
        }
        for (const auto& otherElement : _childElements) {
            const auto otherToggle = dynamic_cast<UIToggleable*>(otherElement.get());
            if (otherToggle && otherToggle != changedToggle) {
                otherToggle->setToggleState(false);
            }
        }
    }

    /**
     * Make no button in the container have toggle on state
     */
    void UIToggleGroupContainer::clearToggleState() const
    {
        for (const auto& element : _childElements) {
            if (const auto toggle = dynamic_cast<UIToggleable*>(element.get())) {
                toggle->setToggleState(false);
            }
        }
    }
}
