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
     * On toggle of one button, un-toggle all other buttons.
     */
    void UIToggleGroupContainer::onStateChanged(UIElement* element)
    {
        UIElement::onStateChanged(element);

        const auto changedButton = dynamic_cast<UIToggleButton*>(element);
        if (!changedButton->isToggleOn()) {
            return;
        }
        for (const auto& otherElement : _childElements) {
            const auto otherButton = dynamic_cast<UIToggleButton*>(otherElement.get());
            if (changedButton != otherButton) {
                otherButton->setToggleState(false);
            }
        }
    }

    /**
     * Add a toggle button and don't allow un-toggling it
     */
    void UIToggleGroupContainer::addElement(const std::shared_ptr<UIToggleButton>& button)
    {
        button->setUnToggleAllowed(false);
        UIContainer::addElement(button);
    }

    /**
     * Make no button in the container have toggle on state
     */
    void UIToggleGroupContainer::clearToggleState() const
    {
        for (const auto& element : _childElements) {
            const auto toggleButton = dynamic_cast<UIToggleButton*>(element.get());
            toggleButton->setToggleState(false);
        }
    }
}
