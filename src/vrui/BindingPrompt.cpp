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
         * The part of an icon's name that says how the button is worked, for the activation types drawn
         * with a caption - LONG, HOLD, 2X, RLSE, TCH - and nothing for a press and a tap, which show the
         * button alone.
         */
        std::string_view activationPrefix(const vrcf::ActivationType type)
        {
            switch (type) {
            case vrcf::ActivationType::HoldDown:
                return "hold-";
            case vrcf::ActivationType::LongPress:
                return "longpress-";
            case vrcf::ActivationType::DoublePress:
                return "double-";
            case vrcf::ActivationType::Release:
                return "release-";
            case vrcf::ActivationType::Touch:
                return "touch-";
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
         * A thumbstick direction as an icon's name ends with it.
         */
        std::string_view directionIconName(const vrcf::Direction direction)
        {
            switch (direction) {
            case vrcf::Direction::Down:
                return "down";
            case vrcf::Direction::Left:
                return "left";
            case vrcf::Direction::Right:
                return "right";
            case vrcf::Direction::Up:
            default:
                return "up";
            }
        }

        /**
         * The icon of a button on a controller worked as `type`: the letters pressed or tapped are drawn
         * without a hand in their names (x, y, a, b), as the letter says which controller it is on, and
         * everything else is <hand>-[<how>-]<button>. Empty for the system button, which has no icon.
         */
        std::string buttonIconPath(const vr::EVRButtonId button, const bool right, const vrcf::ActivationType type)
        {
            const std::string_view name = buttonName(button, right);
            if (name.empty()) {
                return {};
            }
            const std::string_view prefix = activationPrefix(type);
            if (prefix.empty() && name.size() == 1) {
                return std::format("{}{}.dds", BINDING_ICONS_DIR, name);
            }
            return std::format("{}{}-{}{}.dds", BINDING_ICONS_DIR, handName(right), prefix, name);
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
         * reading a prompt with no icon can find it in the config.
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
     * - Press and Tap show the button alone, and every other way of working it the button with its caption:
     *   LONG, HOLD, 2X (a double press), RLSE (a release), TCH (a touch).
     * - A thumbstick push shows the thumbstick with that direction's arrow lit, and a pull of the trigger or
     *   the grip axis that button alone.
     *
     * Empty for what no one icon shows: a chord (see bindingIconPaths), the system button, an axis other
     * than those three, and a disabled binding.
     */
    std::string bindingIconPath(const vrcf::InputBinding& binding)
    {
        if (!binding.isEnabled() || binding.modifier) {
            return {};
        }

        const bool right = isRightController(binding.hand);
        if (binding.type == vrcf::ActivationType::AxisDirection) {
            switch (binding.axis) {
            case vrcf::Axis::Thumbstick:
                return std::format("{}{}-thumbstick-{}.dds", BINDING_ICONS_DIR, handName(right), directionIconName(binding.direction));
            case vrcf::Axis::Trigger:
                return std::format("{}{}-trigger.dds", BINDING_ICONS_DIR, handName(right));
            case vrcf::Axis::Grip:
                return std::format("{}{}-grip.dds", BINDING_ICONS_DIR, handName(right));
            default:
                return {};
            }
        }
        return buttonIconPath(binding.button, right, binding.type);
    }

    /**
     * The binding's own icon, as bindingIconPath draws it with no modifier, and a chord's modifier after it
     * as a press of that button on its controller: the binding's own when the modifier names no hand.
     */
    std::vector<std::string> bindingIconPaths(const vrcf::InputBinding& binding)
    {
        if (!binding.modifier) {
            std::string icon = bindingIconPath(binding);
            return icon.empty() ? std::vector<std::string>{} : std::vector{ std::move(icon) };
        }

        vrcf::InputBinding alone = binding;
        alone.modifier.reset();
        std::string icon = bindingIconPath(alone);
        std::string modifierIcon = buttonIconPath(binding.modifier->button, isRightController(binding.modifier->hand.value_or(binding.hand)), vrcf::ActivationType::Press);
        if (icon.empty() || modifierIcon.empty()) {
            return {};
        }
        return { std::move(icon), std::move(modifierIcon) };
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
     * Two bindings read the same when they show the same icons, or the same words when neither has any.
     */
    bool samePrompt(const vrcf::InputBinding& a, const vrcf::InputBinding& b)
    {
        const std::vector<std::string> iconsA = bindingIconPaths(a);
        const std::vector<std::string> iconsB = bindingIconPaths(b);
        // with icons on either side the icons decide, since two bindings can read the same there while
        // their words differ (a press and a tap are one picture); with neither, the words are the prompt
        return iconsA.empty() && iconsB.empty() ? bindingLabel(a) == bindingLabel(b) : iconsA == iconsB;
    }

    /**
     * The icons when they exist, a chord's joined by " + ", else the binding in words, so a prompt says
     * something either way.
     */
    void appendBindingPrompt(std::vector<TextSpan>& spans, const vrcf::InputBinding& binding, const BindingPromptStyle& style)
    {
        std::vector<std::string> icons = bindingIconPaths(binding);
        if (icons.empty()) {
            spans.push_back({ .text = bindingLabel(binding) });
            return;
        }
        for (std::size_t i = 0; i < icons.size(); ++i) {
            if (i > 0) {
                spans.push_back({ .text = " + " });
            }
            spans.push_back({ .image = std::move(icons[i]), .imageHeight = style.imageHeight, .tintWithText = style.tintWithText });
        }
    }
}
