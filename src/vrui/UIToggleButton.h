#pragma once

#include "UIWidget.h"

namespace f4cf::vrui
{
    class UIToggleButton : public UIWidget
    {
    public:
        explicit UIToggleButton(const std::string& nifPath);
        virtual std::string toString() const override;

        // is the button is currently toggled ON or OFF
        bool isToggleOn() const
        {
            return _isToggleOn;
        }

        void setToggleState(const bool isToggleOn);

        // is a user is allowed to un-toggle the button (useful for toggle group)
        bool isUnToggleAllowed() const;
        void setUnToggleAllowed(bool allowUnToggle);

        void setOnToggleHandler(std::function<void(UIToggleButton*, bool)> handler);

    protected:
        virtual void attachToNode(RE::NiNode* node) override;
        virtual void detachFromAttachedNode(bool releaseSafe) override;
        virtual bool isPressable() const override;
        virtual void onFrameUpdate(UIFrameUpdateContext* adapter) override;
        virtual void onPressEventFired(UIElement* element, UIFrameUpdateContext* context) override;

        std::function<void(UIToggleButton*, bool)> _onToggleEventHandler;
        bool _isToggleOn = false;
        bool _isUnToggleAllowed = true;

        // the UI of the white frame around the button
        RE::NiPointer<RE::NiNode> _toggleFrameNode;
    };
}
