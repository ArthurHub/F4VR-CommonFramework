#pragma once

#include "UIWidget.h"

namespace f4cf::vrui
{
    class UIButton : public UIWidget
    {
    public:
        explicit UIButton(const std::string& nifPath);
        explicit UIButton(const std::string& name, RE::NiNode* node);
        virtual std::string toString() const override;

        void setOnPressHandler(std::function<void(UIWidget*)> handler);

    protected:
        virtual bool isPressable() const override
        {
            return _visible && _onPressEventHandler != nullptr;
        }
        virtual void onPressEventFired(UIElement* element, UIFrameUpdateContext* context) override;

        std::function<void(UIButton*)> _onPressEventHandler;
    };
}
