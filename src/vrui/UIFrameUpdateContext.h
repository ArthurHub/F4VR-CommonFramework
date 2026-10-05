#pragma once

#include <optional>

namespace f4cf::vrui
{
    /**
     * What the elements share during one frame update: whether a finger is close to something it can press.
     */
    class UIFrameUpdateContext
    {
    public:
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

    private:
        // Are any of the elements in current frame update close to the hand's interaction bone and can be pressed?
        std::optional<bool> _isAnyPressableCloseToPrimaryHand = std::nullopt;
        std::optional<bool> _isAnyPressableCloseToOffhand = std::nullopt;
    };
}
