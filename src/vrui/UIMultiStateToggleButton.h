#pragma once

#include "UIWidget.h"

#include <format>
#include <functional>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

namespace f4cf::vrui
{
    template <class StateT>
    class UIMultiStateToggleButton : public UIWidget
    {
    public:
        using StateType = StateT;

        explicit UIMultiStateToggleButton(const std::map<StateType, std::string>& nifPathPerState, float scale = 1.0f);

        StateType getState() const;
        void setState(const StateType& state);

        void setOnStateChangedHandler(std::function<void(UIMultiStateToggleButton<StateType>*, StateType)> handler);

        virtual std::string toString() const override;

    protected:
        virtual void attachToNode(RE::NiNode* node) override;
        virtual void detachFromAttachedNode(bool releaseSafe) override;
        virtual bool isPressable() const override;
        virtual void onFrameUpdate(UIFrameUpdateContext* adapter) override;
        virtual void onPressEventFired(UIElement* element, UIFrameUpdateContext* context) override;

        StateType getNextStateInSequence();

        std::function<void(UIMultiStateToggleButton<StateType>*, StateType)> _onStateChangeEventHandler;

        StateType _currectState{};

        // the nodes used for each state of the button. ordered map to have order to states.
        std::map<StateType, RE::NiPointer<RE::NiNode>> _stateToNodeMap;
    };

    template <class StateT>
    UIMultiStateToggleButton<StateT>::UIMultiStateToggleButton(const std::map<StateT, std::string>& nifPathPerState, const float scale)
        : UIWidget("", nullptr)
    {
        bool first = true;

        for (const auto& [state, nifPath] : nifPathPerState) {
            auto [node, size] = UIUtils::getUINodeFromNifFile(nifPath);

            _stateToNodeMap[state].reset(node);

            if (first) {
                first = false;
                _name = node->name;
                _size = size;
                _node.reset(node);
            }
        }

        setScale(scale);
    }

    template <class StateT>
    StateT UIMultiStateToggleButton<StateT>::getState() const
    {
        return _currectState;
    }

    template <class StateT>
    void UIMultiStateToggleButton<StateT>::setState(const StateT& state)
    {
        if (!_stateToNodeMap.contains(state)) {
            logger::warn("Attempt to set multi state toggle button '{}' to invalid state", _name);
            return;
        }

        _currectState = state;
        _node.reset(_stateToNodeMap[state].get());
        onStateChanged(this);
    }

    template <class StateT>
    void UIMultiStateToggleButton<StateT>::setOnStateChangedHandler(std::function<void(UIMultiStateToggleButton<StateT>*, StateT)> handler)
    {
        _onStateChangeEventHandler = std::move(handler);
    }

    template <class StateT>
    std::string UIMultiStateToggleButton<StateT>::toString() const
    {
        return std::format("UIMultiStateToggleButton({}): {}{}{}{}, Pos({:.2f}, {:.2f}, {:.2f}), Size({:.2f}, {:.2f})",
            _name,
            _visible ? "V" : "H",
            _disabled ? "D" : ".",
            isPressable() ? "P" : ".",
            ".",
            _transform.translate.x,
            _transform.translate.y,
            _transform.translate.z,
            _size.width,
            _size.height);
    }

    template <class StateT>
    void UIMultiStateToggleButton<StateT>::attachToNode(RE::NiNode* node)
    {
        UIWidget::attachToNode(node);
        for (const auto& stateNode : _stateToNodeMap | std::views::values) {
            _attachNode->AttachChild(stateNode.get(), true);
        }
    }

    template <class StateT>
    void UIMultiStateToggleButton<StateT>::detachFromAttachedNode(const bool releaseSafe)
    {
        if (!_attachNode) {
            throw std::runtime_error("Attempt to detach NOT attached widget");
        }

        RE::NiPointer<RE::NiAVObject> out;
        for (const auto& stateNode : _stateToNodeMap | std::views::values) {
            _attachNode->DetachChild(stateNode.get(), out);
        }

        UIWidget::detachFromAttachedNode(releaseSafe);
    }

    /**
     * Multi state button is pressable if there is press handler, and it is not already toggled on when toggling off is not allowed.
     */
    template <class StateT>
    bool UIMultiStateToggleButton<StateT>::isPressable() const
    {
        return _visible && _onStateChangeEventHandler != nullptr;
    }

    /**
     * Handle visibility for the correct state node.
     */
    template <class StateT>
    void UIMultiStateToggleButton<StateT>::onFrameUpdate(UIFrameUpdateContext* adapter)
    {
        UIWidget::onFrameUpdate(adapter);
        if (!_attachNode) {
            return;
        }

        const float scale = calcScale();
        const bool visible = calcVisibility();
        for (const auto& [state, stateNode] : _stateToNodeMap) {
            stateNode->local = _node->local;
            UIUtils::setNodeVisibility(stateNode.get(), visible && state == _currectState, scale);
        }
    }

    /**
     * Handle toggle event of press on the button.
     */
    template <class StateT>
    void UIMultiStateToggleButton<StateT>::onPressEventFired(UIElement* element, UIFrameUpdateContext* context)
    {
        UIWidget::onPressEventFired(element, context);
        setState(getNextStateInSequence());
        if (_onStateChangeEventHandler) {
            _onStateChangeEventHandler(this, _currectState);
        }
    }

    /**
     * Get the next state in the ordered map.
     * Wrap to the first state if current is on the last.
     */
    template <class StateT>
    StateT UIMultiStateToggleButton<StateT>::getNextStateInSequence()
    {
        auto it = _stateToNodeMap.find(_currectState);
        if (it != _stateToNodeMap.end()) {
            ++it; // move to the next state in order
        }

        if (it == _stateToNodeMap.end()) {
            // wrapped past the last -> return first
            return _stateToNodeMap.begin()->first;
        }

        return it->first;
    }
}
