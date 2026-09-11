#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "UIButtonPanel.h"

namespace f4cf::vrui
{
    /**
     * What a UIButtonPanel shows at once - the column it draws: an optional top line, an image or a middle
     * line, and an optional bottom line. Any part left empty is left out.
     */
    struct UIButtonPanelContent
    {
        std::string topText;
        std::string middleText;
        std::string bottomText;

        // by the path the engine's texture loader takes (see render::Texture::load); empty for no image
        std::string image;
    };

    /**
     * A UIButtonPanel that cycles through a set of states - the panel counterpart of
     * UIMultiStateToggleButton, with each state's look composed from text and an image at runtime instead
     * of a NIF per state.
     *
     *     const std::map<HeadgearRequirement, vrui::UIButtonPanelContent> states{
     *         { HeadgearRequirement::None, { .topText = "HEADGEAR", .bottomText = "ANY" } },
     *         { HeadgearRequirement::Helmet, { .topText = "HEADGEAR", .image = "Data\\Textures\\MyMod\\helmet.dds" } },
     *     };
     *     auto requirement = std::make_shared<vrui::UIMultiStateToggleButtonPanel<HeadgearRequirement>>("Headgear", states);
     *     requirement->setState(currentRequirement());
     *     requirement->setOnStateChangedHandler([](auto*, HeadgearRequirement state) { setRequirement(state); });
     *
     * The states run in the map's key order: each press moves to the next, wrapping from the last back to
     * the first, then calls the handler with the new state. It starts in the first; setState moves it
     * without calling the handler. It is pressable only with a handler, and dims and stops responding
     * while disabled, as a button does.
     *
     * Each state's images are loaded once, when the button is built - on the GAME thread, as building any
     * vrui element is - so switching state only swaps what the column shows. The content setters it
     * inherits still work, but the next state change replaces what they set.
     */
    template <class StateT>
    class UIMultiStateToggleButtonPanel : public UIButtonPanel
    {
    public:
        using StateType = StateT;

        /**
         * @param name identifies the element in logs.
         * @param contentPerState what to show in each state, in the order a press cycles through them.
         * @param width / height size in vrui units, border and padding included - 2x2 matches the NIF
         *        buttons.
         */
        UIMultiStateToggleButtonPanel(const std::string& name, const std::map<StateType, UIButtonPanelContent>& contentPerState, float width = 2.0f, float height = 2.0f);

        StateType getState() const
        {
            return _currentState;
        }

        void setState(const StateType& state);

        /**
         * Called on the game thread each time a press moves to the next state, with that state. A button
         * with no handler is not pressable.
         */
        void setOnStateChangedHandler(std::function<void(UIMultiStateToggleButtonPanel<StateType>*, StateType)> handler);

    protected:
        bool isPressable() const override;
        void onPressEventFired(UIElement* element, UIFrameUpdateContext* context) override;

        std::string_view typeName() const override
        {
            return "UIMultiStateToggleButtonPanel";
        }

    private:
        /**
         * A state's content with its image already resolved to a texture.
         */
        struct StateContent
        {
            std::string topText;
            std::string middleText;
            std::string bottomText;
            std::shared_ptr<render::Texture> texture;
        };

        void showState(const StateType& state);
        StateType getNextStateInSequence() const;

        std::function<void(UIMultiStateToggleButtonPanel<StateType>*, StateType)> _onStateChangedHandler;
        StateType _currentState{};

        // ordered, so the press cycle follows the key order
        std::map<StateType, StateContent> _contentPerState;
    };

    template <class StateT>
    UIMultiStateToggleButtonPanel<StateT>::UIMultiStateToggleButtonPanel(const std::string& name, const std::map<StateT, UIButtonPanelContent>& contentPerState, const float width,
        const float height)
        : UIButtonPanel(name, width, height)
    {
        for (const auto& [state, content] : contentPerState) {
            _contentPerState[state] = StateContent{
                .topText = content.topText,
                .middleText = content.middleText,
                .bottomText = content.bottomText,
                .texture = content.image.empty() ? nullptr : render::Texture::load(content.image),
            };
        }

        if (_contentPerState.empty()) {
            logger::warn("Multi state toggle button panel '{}' has no states", name);
            return;
        }
        _currentState = _contentPerState.begin()->first;
        showState(_currentState);
    }

    /**
     * Move to the state and show its content, without calling the handler. A state the button was not
     * built with is refused rather than shown as nothing.
     */
    template <class StateT>
    void UIMultiStateToggleButtonPanel<StateT>::setState(const StateT& state)
    {
        if (!_contentPerState.contains(state)) {
            logger::warn("Attempt to set multi state toggle button panel '{}' to invalid state", _name);
            return;
        }

        _currentState = state;
        showState(state);
        onStateChanged(this);
    }

    template <class StateT>
    void UIMultiStateToggleButtonPanel<StateT>::setOnStateChangedHandler(std::function<void(UIMultiStateToggleButtonPanel<StateT>*, StateT)> handler)
    {
        _onStateChangedHandler = std::move(handler);
    }

    template <class StateT>
    bool UIMultiStateToggleButtonPanel<StateT>::isPressable() const
    {
        return !isDisabled() && _onStateChangedHandler != nullptr && !_contentPerState.empty();
    }

    /**
     * Fire the press as a button does - haptic, and the press handler if one is set - then move to the next
     * state and report it.
     */
    template <class StateT>
    void UIMultiStateToggleButtonPanel<StateT>::onPressEventFired(UIElement* element, UIFrameUpdateContext* context)
    {
        UIButtonPanel::onPressEventFired(element, context);
        setState(getNextStateInSequence());
        if (_onStateChangedHandler) {
            _onStateChangedHandler(this, _currentState);
        }
    }

    template <class StateT>
    void UIMultiStateToggleButtonPanel<StateT>::showState(const StateT& state)
    {
        const StateContent& content = _contentPerState.at(state);
        setTopText(content.topText);
        setMiddleText(content.middleText);
        setBottomText(content.bottomText);
        if (content.texture) {
            setImage(content.texture);
        } else {
            clearImage();
        }
    }

    /**
     * The state after the current one in key order, wrapping from the last back to the first.
     */
    template <class StateT>
    StateT UIMultiStateToggleButtonPanel<StateT>::getNextStateInSequence() const
    {
        auto it = _contentPerState.find(_currentState);
        if (it != _contentPerState.end()) {
            ++it;
        }
        return it == _contentPerState.end() ? _contentPerState.begin()->first : it->first;
    }
}
