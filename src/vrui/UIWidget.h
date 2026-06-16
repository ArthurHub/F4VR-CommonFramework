#pragma once

#include "UIElement.h"
#include "UIUtils.h"

namespace f4cf::vrui
{
    class UIWidget : public UIElement
    {
    public:
        explicit UIWidget(const std::string& nifPath, const float scale = 1.0f);
        explicit UIWidget(const std::string& name, RE::NiNode* node);
        virtual std::string toString() const override;

        // A disabled widget cannot be pressed and renders the "disabled" overlay on top of it.
        bool isDisabled() const
        {
            return _disabled;
        }
        void setDisabled(bool disabled);

    protected:
        virtual bool isPressable() const
        {
            return false;
        }
        virtual void attachToNode(RE::NiNode* attachNode) override;
        virtual void detachFromAttachedNode(bool releaseSafe) override;
        virtual void onFrameUpdate(UIFrameUpdateContext* adapter) override;
        virtual RE::NiTransform calculateTransform() const override;
        virtual void onPressEventFired(UIElement* element, UIFrameUpdateContext* context) override;
        void handlePressEvent(UIFrameUpdateContext* context);
        void updatePressableCloseToInteraction(UIFrameUpdateContext* context, float distance, float yOnlyDistance);

        // UI node to render
        RE::NiPointer<RE::NiNode> _node;

        // Disabled state and the overlay node rendered on top to indicate it (lazily created on first disable)
        bool _disabled = false;
        RE::NiPointer<RE::NiNode> _disabledOverlayNode;

        // Press handling
        bool _pressEventFired = false;
        float _pressYOffset = 0;

        // To handle pressable close to interaction bone margin to fix twitching because of change from true to false
        bool _wasPressableCloseToInteraction = false;
    };
}
