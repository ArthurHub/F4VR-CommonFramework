#include "BindingPrompt.h"

#include <cctype>
#include <format>

#include "f4vr/F4VRUtils.h"

namespace f4cf::vrui
{
    namespace
    {
        /**
         * Which physical controller a hand names: Left and Right as given, Primary and Offhand resolved by
         * the player's handedness, which is what swaps a left-handed player's prompts over.
         */
        bool isRightController(const vrcf::Hand hand)
        {
            switch (hand) {
            case vrcf::Hand::Left:
                return false;
            case vrcf::Hand::Right:
                return true;
            case vrcf::Hand::Offhand:
                return f4vr::isLeftHandedMode();
            case vrcf::Hand::Primary:
            default:
                return !f4vr::isLeftHandedMode();
            }
        }

        /**
         * What a button is called on that controller, as the controller itself prints it: the runtime's A
         * and B are marked A and B on the right one but X and Y on the left. Empty for a button no icon is
         * drawn for.
         */
        std::string_view buttonName(const vr::EVRButtonId button, const bool rightController)
        {
            switch (button) {
            case vr::k_EButton_A:
                return rightController ? "a" : "x";
            case vr::k_EButton_ApplicationMenu:
                return rightController ? "b" : "y";
            case vr::k_EButton_Grip:
                return "grip";
            case vr::k_EButton_SteamVR_Trigger:
                return "trigger";
            case vr::k_EButton_SteamVR_Touchpad:
                return "thumbstick";
            default:
                return {};
            }
        }

        /**
         * The part of an icon's name that says how the button is held - "hold-" or "longpress-" - for the
         * activation types drawn that way, and nothing for those shown as the button alone.
         */
        std::string_view activationPrefix(const vrcf::ActivationType type)
        {
            switch (type) {
            case vrcf::ActivationType::HoldDown:
                return "hold-";
            case vrcf::ActivationType::LongPress:
                return "longpress-";
            default:
                return {};
            }
        }

        /**
         * The controller an icon's name starts with, for the icons drawn per hand.
         */
        std::string_view handName(const bool rightController)
        {
            return rightController ? "right" : "left";
        }

        /**
         * A thumbstick direction in words, for a prompt with no icon.
         */
        std::string_view directionName(const vrcf::Direction direction)
        {
            switch (direction) {
            case vrcf::Direction::Down:
                return "DOWN";
            case vrcf::Direction::Left:
                return "LEFT";
            case vrcf::Direction::Right:
                return "RIGHT";
            case vrcf::Direction::Up:
            default:
                return "UP";
            }
        }

        /**
         * The axis a direction is pushed on, in words: what a stick prompt names before its direction.
         */
        std::string_view axisName(const vrcf::Axis axis)
        {
            switch (axis) {
            case vrcf::Axis::Trigger:
                return "TRIGGER";
            case vrcf::Axis::Grip:
                return "GRIP";
            case vrcf::Axis::Thumbstick:
            default:
                return "THUMBSTICK";
            }
        }

        /**
         * How a button must be worked, in words, in the config's own terms - a tap is a TAP - so a player
         * reading a prompt with no icon can find it in the INI.
         */
        std::string_view activationName(const vrcf::ActivationType type)
        {
            switch (type) {
            case vrcf::ActivationType::Touch:
                return "TOUCH";
            case vrcf::ActivationType::HoldDown:
                return "HOLD";
            case vrcf::ActivationType::Release:
                return "RELEASE";
            case vrcf::ActivationType::Tap:
                return "TAP";
            case vrcf::ActivationType::LongPress:
                return "LONG PRESS";
            case vrcf::ActivationType::DoublePress:
                return "DOUBLE PRESS";
            case vrcf::ActivationType::Press:
            default:
                return "PRESS";
            }
        }

        /**
         * A button in words, as the controller prints it: the letters as letters, the rest by name, and the
         * system button - which has no icon either - as "SYSTEM".
         */
        std::string buttonLabel(const vr::EVRButtonId button, const bool rightController)
        {
            const std::string_view name = buttonName(button, rightController);
            if (name.empty()) {
                return "SYSTEM";
            }
            std::string label(name);
            for (char& c : label) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            return label;
        }
    }

    /**
     * A binding names a hand and a button the way the config does; the icons are drawn for physical
     * controllers, so both are resolved here:
     *
     * - Primary / Offhand become the left or right controller by the player's handedness, so a left-handed
     *   player's prompts are mirrored with their controllers.
     * - A button is drawn as the controller PRINTS it: the button the runtime calls A is marked A on the
     *   right controller and X on the left, and likewise B and Y. So "offhand press a" shows an X for a
     *   right-handed player.
     * - Press and Tap show the button alone, HoldDown and LongPress the HOLD / LONG combinations, and a
     *   thumbstick direction the thumbstick icon, whose arrows cover every direction.
     *
     * Empty for what the icons do not cover: the system button, a touch, release or double press, a chord
     * (the binding carries a modifier), a held thumbstick, the trigger and grip axes, and a disabled
     * binding.
     */
    std::string bindingIconPath(const vrcf::InputBinding& binding)
    {
        // a chord is two buttons at once, which no single icon shows
        if (!binding.isEnabled() || binding.modifier) {
            return {};
        }

        const bool right = isRightController(binding.hand);
        if (binding.type == vrcf::ActivationType::AxisDirection) {
            // the thumbstick icon carries arrows in every direction, so it says any of them; the trigger and
            // grip axes have no icon of their own
            return binding.axis == vrcf::Axis::Thumbstick ? std::format("{}{}-thumbstick.dds", BINDING_ICONS_DIR, handName(right)) : std::string{};
        }

        const std::string_view button = buttonName(binding.button, right);
        if (button.empty()) {
            return {};
        }
        const std::string_view prefix = activationPrefix(binding.type);
        if (prefix.empty()) {
            if (binding.type != vrcf::ActivationType::Press && binding.type != vrcf::ActivationType::Tap) {
                return {};
            }
            // a plain letter is drawn without a hand, since the letter itself says which controller it is on
            return button.size() == 1 ? std::format("{}{}.dds", BINDING_ICONS_DIR, button) : std::format("{}{}-{}.dds", BINDING_ICONS_DIR, handName(right), button);
        }
        // the thumbstick is drawn alone, never held
        if (button == "thumbstick") {
            return {};
        }
        return std::format("{}{}-{}{}.dds", BINDING_ICONS_DIR, handName(right), prefix, button);
    }

    /**
     * The hand, the button as the controller prints it, and how it is activated - resolved exactly as
     * bindingIconPath resolves them, so the words and the icons name the same physical button.
     */
    std::string bindingLabel(const vrcf::InputBinding& binding)
    {
        if (!binding.isEnabled()) {
            return "NONE";
        }

        const bool right = isRightController(binding.hand);
        const std::string_view hand = right ? "RIGHT" : "LEFT";
        std::string label = binding.type == vrcf::ActivationType::AxisDirection ? std::format("{} {} {}", hand, axisName(binding.axis), directionName(binding.direction))
                                                                                : std::format("{} {} {}", hand, buttonLabel(binding.button, right), activationName(binding.type));

        if (binding.modifier) {
            // a modifier on the binding's own hand needs no hand of its own; one pinned elsewhere names it
            const bool modifierRight = isRightController(binding.modifier->hand.value_or(binding.hand));
            const std::string modifierButton = buttonLabel(binding.modifier->button, modifierRight);
            label += binding.modifier->hand ? std::format(" + {} {}", modifierRight ? "RIGHT" : "LEFT", modifierButton) : std::format(" + {}", modifierButton);
        }
        return label;
    }

    /**
     * Two bindings read the same when they show the same icon, or the same words when neither has one.
     */
    bool samePrompt(const vrcf::InputBinding& a, const vrcf::InputBinding& b)
    {
        const std::string iconA = bindingIconPath(a);
        const std::string iconB = bindingIconPath(b);
        // with an icon on either side the icons decide, since two bindings can read the same there while
        // their words differ (a press and a tap are one picture); with neither, the words are the prompt
        return iconA.empty() && iconB.empty() ? bindingLabel(a) == bindingLabel(b) : iconA == iconB;
    }

    /**
     * The icon when one exists, else the binding in words, so a prompt says something either way.
     */
    void appendBindingPrompt(std::vector<TextSpan>& spans, const vrcf::InputBinding& binding, const BindingPromptStyle& style)
    {
        if (std::string icon = bindingIconPath(binding); !icon.empty()) {
            spans.push_back({ .image = std::move(icon), .imageHeight = style.imageHeight, .tintWithText = style.tintWithText });
        } else {
            spans.push_back({ .text = bindingLabel(binding) });
        }
    }
}
