#pragma once

#include <algorithm>
#include <optional>

namespace f4cf::vrui
{
    /**
     * How a finger stands to a pressable, see UIElement::updateFinger.
     */
    enum class UIFingerProximity
    {
        // too far from the pressable, too far behind it, or the player does not look at it
        Away,
        // near the pressable but not interacting with it: the finger cannot press it, and only a hand that
        // already points keeps pointing
        Near,
        // near the pressable and behind its face, or on its way out from behind it: as near, and the hand
        // does not start to point for another pressable either
        Behind,
        // came to the pressable from its front while the player looks at it: the hand points and the finger can press it
        Interacting,
    };

    /**
     * What the elements share during one frame update: how close each hand's finger is to something it can press.
     */
    class UIFrameUpdateContext
    {
    public:
        /**
         * The closest the finger of the given hand is to any pressable. Empty if nothing was tested against
         * that finger in this frame.
         */
        const std::optional<UIFingerProximity>& getFingerProximity(const bool primaryHand) const
        {
            return primaryHand ? _primaryFingerProximity : _offhandFingerProximity;
        }

        void markFingerProximity(const bool primaryHand, const UIFingerProximity proximity)
        {
            auto& closest = primaryHand ? _primaryFingerProximity : _offhandFingerProximity;
            closest = (std::max)(closest.value_or(UIFingerProximity::Away), proximity);
        }

    private:
        std::optional<UIFingerProximity> _primaryFingerProximity = std::nullopt;
        std::optional<UIFingerProximity> _offhandFingerProximity = std::nullopt;
    };
}
