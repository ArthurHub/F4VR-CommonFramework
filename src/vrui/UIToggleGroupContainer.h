#pragma once

#include <concepts>
#include <memory>

#include "UIContainer.h"
#include "UIElement.h"
#include "UIToggleButton.h"
#include "UIToggleable.h"

namespace f4cf::vrui
{
    class UIToggleGroupContainer : public UIContainer
    {
    public:
        explicit UIToggleGroupContainer(const std::string& name, const UIContainerLayout layout = UIContainerLayout::Manual, const float padding = 0);

        /**
         * Add a toggle - a UIToggleButton or a UIToggleButtonPanel, mixed freely - and don't allow
         * un-toggling it, so pressing the selected one never leaves the group with none on.
         */
        template <typename ToggleT>
            requires std::derived_from<ToggleT, UIElement> && std::derived_from<ToggleT, UIToggleable>
        void addElement(const std::shared_ptr<ToggleT>& button)
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
