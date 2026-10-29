#include "UIWidget.h"

#include "common/MatrixUtils.h"

using namespace common;

namespace f4cf::vrui
{
    UIWidget::UIWidget(const std::string& nifPath, const float scale)
    {
        auto [node, size] = UIUtils::getUINodeFromNifFile(nifPath);
        _node.reset(node);
        _size = size;
        _name = node->name;
        setScale(scale);
    }

    UIWidget::UIWidget(const std::string& name, RE::NiNode* node)
        : UIElement(name),
          _node(node)
    {}

    std::string UIWidget::toString() const
    {
        return std::format("UIWidget({}): {}{}{}, Pos({:.2f}, {:.2f}, {:.2f}), Size({:.2f}, {:.2f})",
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

    /**
     * Disable or enable the widget. A disabled widget is not pressable and shows the disabled
     * overlay on top of it. The overlay node is created lazily on the first disable and, once
     * created, is kept attached and only toggled visible/hidden by the disabled state.
     */
    void UIWidget::setDisabled(const bool disabled)
    {
        if (_disabled == disabled) {
            return;
        }
        _disabled = disabled;

        if (_disabled) {
            // snap back any in-progress soft-press so a half-pressed button doesn't stay pushed in
            _pressYOffset = 0;
            _pressArmed = false;

            // lazily create the overlay on first disable, attaching it now if already attached to a node
            if (!_disabledOverlayNode) {
                const auto overlayNode = std::get<0>(UIUtils::getUINodeFromNifFile(UIUtils::getDisabledOverlayNifName()));
                _disabledOverlayNode.reset(overlayNode);
                if (_attachNode) {
                    _attachNode->AttachChild(_disabledOverlayNode.get(), true);
                }
            }
        }
    }

    /**
     * Attach this widget RE::NiNode to the given node.
     */
    void UIWidget::attachToNode(RE::NiNode* attachNode)
    {
        UIElement::attachToNode(attachNode);
        _attachNode->AttachChild(_node.get(), true);
        if (_disabledOverlayNode) {
            _attachNode->AttachChild(_disabledOverlayNode.get(), true);
        }
    }

    /**
     * Remove this widget from attached node.
     */
    void UIWidget::detachFromAttachedNode(const bool releaseSafe)
    {
        if (!_attachNode) {
            throw std::runtime_error("Attempt to detach NOT attached widget");
        }
        RE::NiPointer<RE::NiAVObject> out;
        _attachNode->DetachChild(_node.get(), out);
        if (_disabledOverlayNode) {
            _attachNode->DetachChild(_disabledOverlayNode.get(), out);
        }
        UIElement::detachFromAttachedNode(releaseSafe);
        out = nullptr;
    }

    /**
     * Handle widget visibility, location, and press handling.
     */
    void UIWidget::onFrameUpdate(UIFrameUpdateContext* context)
    {
        if (!_attachNode) {
            return;
        }

        const auto visible = calcVisibility();
        UIUtils::setNodeVisibility(_node.get(), visible, getScale());
        if (!visible) {
            _pressYOffset = 0;
            _pressArmed = false;
            _finger = {};
            if (_disabledOverlayNode) {
                UIUtils::setNodeVisibility(_disabledOverlayNode.get(), false, getScale());
            }
            return;
        }

        handlePressEvent(context);

        _node->local = calculateTransform();

        // render the disabled overlay on top of the widget, tracking its transform
        if (_disabledOverlayNode) {
            UIUtils::setNodeVisibility(_disabledOverlayNode.get(), _disabled, getScale());
            if (_disabled) {
                _disabledOverlayNode->local = _node->local;
                // avoid z-fighting between the two coplanar nodes
                _disabledOverlayNode->local.translate -= _node->local.rotate.Transpose() * RE::NiPoint3(0, 0.1f, 0);
            }
        }
    }

    /**
     * Add soft press mimic to the transform.
     */
    RE::NiTransform UIWidget::calculateTransform() const
    {
        auto trans = UIElement::calculateTransform();
        // along the widget's own depth axis, which a turned root turns with it
        trans.translate += trans.rotate.Transpose() * RE::NiPoint3(0, _pressYOffset, 0);
        return trans;
    }

    /**
     * Handle pressing even on the UI.
     * Detect if interaction bone is close to the widget node and fire press event ONCE when it is.
     * Only allow firing of the press event again when interaction bone move away enough from the widget.
     * Only a finger that interacts with the widget pushes it, see UIElement::updateFinger, and only from in
     * front of it: a finger that went in beside the widget does not push it as it comes up under it.
     */
    void UIWidget::handlePressEvent(UIFrameUpdateContext* context)
    {
        if (_disabled || !isPressable()) {
            _finger = {};
            return;
        }

        const auto widgetCenter = _node->world.translate;
        const RE::NiPoint3 forward = _node->world.rotate.Transpose() * (RE::NiPoint3(0, 1, 0));

        const auto finger = updateFinger(_finger, context, widgetCenter, forward);
        if (!_finger.interacting) {
            _pressYOffset = 0;
            _pressArmed = false;
            return;
        }

        // calculate the distance only in the y-axis
        const RE::NiPoint3 vectorToCurr = widgetCenter - finger;
        const float yOnlyDistance = MatrixUtils::vec3Dot(forward, vectorToCurr);

        // arm the widget only when finger is far enough in-front of it: after it fired, and after the finger went in beside it
        if (!_pressArmed) {
            _pressArmed = yOnlyDistance > 0.4;
            return;
        }

        // distance in y-axis from original location before press offset
        const RE::NiPoint3 vectorToOrg = vectorToCurr - _node->world.rotate.Transpose() * (RE::NiPoint3(0, _pressYOffset, 0));
        const float pressDistance = -MatrixUtils::vec3Dot(forward, vectorToOrg);

        if (std::isnan(pressDistance) || pressDistance < 0) {
            _pressYOffset = 0;
            return;
        }

        // past the plane but outside the bounds of the widget, measured along the plane: the finger went in beside the widget
        const float radius = _node->worldBound.fRadius;
        const float sideDistanceSquared = MatrixUtils::vec3Dot(vectorToCurr, vectorToCurr) - yOnlyDistance * yOnlyDistance;
        if (sideDistanceSquared > radius * radius) {
            _pressYOffset = 0;
            _pressArmed = false;
            return;
        }

        static constexpr int PRESS_TRIGGER_DISTANCE = 2;

        // mimic soft press of the UI, smoothing with prev value
        _pressYOffset = pressDistance + (_pressYOffset - pressDistance) / 2;

        if (_pressYOffset > PRESS_TRIGGER_DISTANCE) {
            // widget pushed enough, fire press event
            logger::info("UI Widget '{}' pressed", _node->name.c_str());
            onPressEventFiredPropagate(this, context);
        }
    }

    void UIWidget::onPressEventFired(UIElement* element, UIFrameUpdateContext* context)
    {
        _pressYOffset = 0;
        _pressArmed = false;
        UIUtils::triggerInteractionHeptic(_finger.primaryHand);
        UIElement::onPressEventFired(element, context);
    }
}
