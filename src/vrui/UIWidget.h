#pragma once

#include "UIElement.h"
#include "UIPressable.h"
#include "UIUtils.h"

namespace f4cf::vrui
{
    class UIWidget : public UIElement, public UIPressable
    {
    public:
        explicit UIWidget(const std::string& nifPath, const float scale = 1.0f);
        explicit UIWidget(const std::string& name, RE::NiNode* node);
        virtual std::string toString() const override;

        // A disabled widget cannot be pressed and renders the "disabled" overlay on top of it.
        bool isDisabled() const override
        {
            return _disabled;
        }

        void setDisabled(bool disabled) override;

    protected:
        virtual bool isPressable() const
        {
            return false;
        }

        virtual void attachToNode(RE::NiNode* attachNode) override;
        virtual void detachFromAttachedNode(bool releaseSafe) override;
        virtual void onFrameUpdate(UIFrameUpdateContext* context) override;
        virtual RE::NiTransform calculateTransform() const override;
        virtual void onPressEventFired(UIElement* element, UIFrameUpdateContext* context) override;
        void handlePressEvent(UIFrameUpdateContext* context);

        // UI node to render
        RE::NiPointer<RE::NiNode> _node;

        // Disabled state and the overlay node rendered on top to indicate it (lazily created on first disable)
        bool _disabled = false;
        RE::NiPointer<RE::NiNode> _disabledOverlayNode;

        // Press handling: whether the finger has been in front of the widget since it last fired or went in
        // beside it, so it can push it, and how far the widget is pushed in
        bool _pressArmed = false;
        float _pressYOffset = 0;

        // The finger the widget is tested against
        UIFingerState _finger;
    };
}
