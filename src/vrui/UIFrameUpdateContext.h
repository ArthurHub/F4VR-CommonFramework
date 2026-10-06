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

    class UIElement;

    /**
     * An element the ray of a hand is on.
     */
    struct UIPointedAt
    {
        const UIElement* element = nullptr;

        // where the ray meets the element, in the world
        RE::NiPoint3 position;
    };

    /**
     * What the elements share during one frame update: how close each hand's finger is to something it can
     * press, and the element each hand's ray is on.
     */
    class UIFrameUpdateContext
    {
    public:
        /**
         * The element the ray of the given hand is on. Empty if no element said so in this frame.
         */
        const std::optional<UIPointedAt>& getPointedAt(const bool primaryHand) const
        {
            return primaryHand ? _primaryPointedAt : _offhandPointedAt;
        }

        /**
         * An element that is operated by a ray says that the ray of the given hand is on it, and where in the
         * world. The manager moves a root that stays in the world by it, see UIManager::setWorldMoveButton.
         */
        void markPointedAt(const bool primaryHand, const UIElement* element, const RE::NiPoint3& position)
        {
            (primaryHand ? _primaryPointedAt : _offhandPointedAt) = UIPointedAt{ .element = element, .position = position };
        }

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
        std::optional<UIPointedAt> _primaryPointedAt = std::nullopt;
        std::optional<UIPointedAt> _offhandPointedAt = std::nullopt;
    };
}
