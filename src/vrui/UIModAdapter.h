#pragma once

#include <optional>

namespace f4cf::vrui
{
    class UIModAdapter
    {
    public:
        /**
         * Set the interaction hand to a pointing position for UI interaction where index finger is the interaction bone.
         * @param primaryHand - true - use primary hand, false - use offhand
         * @param toPoint true - force hand to point position, false - release
         */
        virtual void setInteractionHandPointing(bool primaryHand, bool toPoint) = 0;

        virtual ~UIModAdapter() = default;
    };

    class UIFrameUpdateContext : public UIModAdapter
    {
    public:
        explicit UIFrameUpdateContext(UIModAdapter* adapter)
            : _adapter(adapter)
        {}

        /**
         * Whether the finger of the given hand is close to something it can press. Empty if nothing was tested
         * against that finger in this frame.
         */
        const std::optional<bool>& isAnyPressableCloseToInteraction(const bool primaryHand) const
        {
            return primaryHand ? _isAnyPressableCloseToPrimaryHand : _isAnyPressableCloseToOffhand;
        }

        void markAnyPressableCloseToInteraction(const bool primaryHand, const bool isPressableClose)
        {
            auto& isClose = primaryHand ? _isAnyPressableCloseToPrimaryHand : _isAnyPressableCloseToOffhand;
            isClose = isClose.value_or(false) || isPressableClose;
        }

        virtual void setInteractionHandPointing(const bool primaryHand, const bool toPoint) override
        {
            _adapter->setInteractionHandPointing(primaryHand, toPoint);
        }

    private:
        UIModAdapter* _adapter;

        // Are any of the elements in current frame update close to the hand's interaction bone and can be pressed?
        std::optional<bool> _isAnyPressableCloseToPrimaryHand = std::nullopt;
        std::optional<bool> _isAnyPressableCloseToOffhand = std::nullopt;
    };
}
