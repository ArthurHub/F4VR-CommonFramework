#pragma once

#include <memory>

#include "UIContainer.h"
#include "UIToggleButtonPanel.h"

namespace f4cf::vrui
{
    class UIToggleGroupContainer : public UIContainer
    {
    public:
        explicit UIToggleGroupContainer(const std::string& name, const UIContainerLayout layout = UIContainerLayout::Manual, const float padding = 0);

        /**
         * Add a toggle and don't allow un-toggling it, so pressing the selected one never leaves the
         * group with none on.
         */
        void addElement(const std::shared_ptr<UIToggleButtonPanel>& button)
        {
            button->setUnToggleAllowed(false);
            UIContainer::addElement(button);
        }

        void clearToggleState() const;

        virtual std::string toString() const override;

    protected:
        virtual void onStateChanged(UIElement* element) override;
    };
}
