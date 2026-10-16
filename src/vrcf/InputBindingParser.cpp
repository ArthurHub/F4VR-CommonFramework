#include "InputBindingParser.h"

#include <cctype>
#include <charconv>
#include <format>
#include <vector>

namespace f4cf::vrcf
{
    namespace
    {
        // Lowercased, whitespace-trimmed copy for case-insensitive matching.
        std::string normalize(const std::string_view text)
        {
            size_t begin = 0;
            size_t end = text.size();
            while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
                ++begin;
            }
            while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
                --end;
            }
            std::string out(text.substr(begin, end - begin));
            for (auto& c : out) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }

        // Split into lowercased tokens on space, tab, comma, and colon.
        std::vector<std::string> tokenize(const std::string_view text)
        {
            std::vector<std::string> tokens;
            const std::string lower = normalize(text);
            size_t i = 0;
            while (i < lower.size()) {
                while (i < lower.size() && (lower[i] == ' ' || lower[i] == '\t' || lower[i] == ',' || lower[i] == ':')) {
                    ++i;
                }
                const size_t start = i;
                while (i < lower.size() && lower[i] != ' ' && lower[i] != '\t' && lower[i] != ',' && lower[i] != ':') {
                    ++i;
                }
                if (i > start) {
                    tokens.emplace_back(lower.substr(start, i - start));
                }
            }
            return tokens;
        }

        std::optional<float> parseFloat(const std::string_view text)
        {
            float value = 0.0f;
            const auto* first = text.data();
            const auto* last = text.data() + text.size();
            const auto [ptr, ec] = std::from_chars(first, last, value);
            // Require the whole token to be a valid number; reject partial parses like "0.6x".
            if (ec == std::errc{} && ptr == last) {
                return value;
            }
            return std::nullopt;
        }

        // The words of each part of a line, as an error lists them: the common ones, not every alias.
        constexpr auto HANDS = "primary, offhand, left, right";
        constexpr auto TYPES = "tap, press, longpress, double, hold, release, touch, thumbstick, axis";
        constexpr auto BUTTONS = "trigger, grip, a, b, thumbstick, menu, system";
        constexpr auto AXES = "thumbstick, trigger, grip";
        constexpr auto DIRECTIONS = "up, down, left, right";

        /**
         * The error of a word that is none of the words of its part of the line.
         */
        std::unexpected<std::string> notOneOf(const std::string_view token, const std::string_view part, const std::string_view words)
        {
            return std::unexpected(std::format("'{}' is not {}, one of {}", token, part, words));
        }

        /**
         * The error of a word past the end of the line's form, as a typo or a word left over from an edit is.
         */
        std::unexpected<std::string> beyondTheLine(const std::string_view token, const std::string_view form)
        {
            return std::unexpected(std::format("'{}' is past the end of the line, which is {} with an optional suppress or nosuppress and +modifier", token, form));
        }
    }

    std::optional<Hand> parseHand(const std::string_view text)
    {
        const std::string t = normalize(text);
        if (t == "primary" || t == "main") {
            return Hand::Primary;
        }
        if (t == "offhand" || t == "off" || t == "secondary") {
            return Hand::Offhand;
        }
        if (t == "right") {
            return Hand::Right;
        }
        if (t == "left") {
            return Hand::Left;
        }
        return std::nullopt;
    }

    std::optional<ActivationType> parseActivationType(const std::string_view text)
    {
        const std::string t = normalize(text);
        if (t == "touch" || t == "touching") {
            return ActivationType::Touch;
        }
        if (t == "press" || t == "pressed" || t == "click") {
            return ActivationType::Press;
        }
        if (t == "hold" || t == "holddown" || t == "held") {
            return ActivationType::HoldDown;
        }
        if (t == "release" || t == "released") {
            return ActivationType::Release;
        }
        if (t == "tap") {
            return ActivationType::Tap;
        }
        if (t == "longpress") {
            return ActivationType::LongPress;
        }
        if (t == "double" || t == "doublepress") {
            return ActivationType::DoublePress;
        }
        if (t == "axis" || t == "thumbstick" || t == "stick") {
            return ActivationType::AxisDirection;
        }
        return std::nullopt;
    }

    std::optional<vr::EVRButtonId> parseButton(const std::string_view text)
    {
        const std::string t = normalize(text);
        if (t == "system") {
            return vr::k_EButton_System;
        }
        if (t == "menu" || t == "applicationmenu" || t == "b") {
            return vr::k_EButton_ApplicationMenu;
        }
        if (t == "grip") {
            return vr::k_EButton_Grip;
        }
        if (t == "a") {
            return vr::k_EButton_A;
        }
        if (t == "trigger") {
            return vr::k_EButton_SteamVR_Trigger;
        }
        if (t == "thumbstick" || t == "touchpad" || t == "joystick") {
            return vr::k_EButton_SteamVR_Touchpad;
        }
        return std::nullopt;
    }

    std::optional<Axis> parseAxis(const std::string_view text)
    {
        const std::string t = normalize(text);
        if (t == "thumbstick" || t == "touchpad" || t == "joystick") {
            return Axis::Thumbstick;
        }
        if (t == "trigger") {
            return Axis::Trigger;
        }
        if (t == "grip") {
            return Axis::Grip;
        }
        return std::nullopt;
    }

    std::optional<Direction> parseDirection(const std::string_view text)
    {
        const std::string t = normalize(text);
        if (t == "up") {
            return Direction::Up;
        }
        if (t == "down") {
            return Direction::Down;
        }
        if (t == "left") {
            return Direction::Left;
        }
        if (t == "right") {
            return Direction::Right;
        }
        return std::nullopt;
    }

    std::string buttonName(const vr::EVRButtonId button)
    {
        switch (button) {
        case vr::k_EButton_System:
            return "system";
        case vr::k_EButton_ApplicationMenu:
            return "menu";
        case vr::k_EButton_Grip:
            return "grip";
        case vr::k_EButton_A:
            return "a";
        case vr::k_EButton_SteamVR_Trigger:
            return "trigger";
        case vr::k_EButton_SteamVR_Touchpad:
            return "thumbstick";
        default:
            return std::format("button{}", static_cast<uint32_t>(button));
        }
    }

    std::string axisName(const Axis axis)
    {
        switch (axis) {
        case Axis::Thumbstick:
            return "thumbstick";
        case Axis::Trigger:
            return "trigger";
        case Axis::Grip:
            return "grip";
        default:
            return std::format("axis{}", static_cast<uint32_t>(axis));
        }
    }

    std::vector<std::string> buttonNames(const uint64_t buttonMask)
    {
        std::vector<std::string> names;
        for (uint32_t id = 0; id < 64; ++id) {
            if (buttonMask & vr::ButtonMaskFromId(static_cast<vr::EVRButtonId>(id))) {
                names.push_back(buttonName(static_cast<vr::EVRButtonId>(id)));
            }
        }
        return names;
    }

    /**
     * Parse a full binding line, see the grammar above. A blank line or "none"/"off"/"disabled" yields a disabled
     * binding (ActivationType::Disabled, which never triggers).
     * Returns why the line is not a binding, naming the word that is wrong and the words that would fit there, for
     * one that can't be read: a hand, type, button, axis or direction that is none of its words, one that is
     * missing, a duration or threshold that is not a number, or a word past the end of the line's form. Nothing is
     * left out of a line, so a typo never gives a binding other than the one written.
     */
    std::expected<InputBinding, std::string> parseInputBinding(const std::string_view text)
    {
        // A blank value or an explicit off keyword disables the binding: it parses successfully but never triggers.
        const std::string whole = normalize(text);
        if (whole.empty() || whole == "none" || whole == "off" || whole == "disabled") {
            InputBinding binding;
            binding.type = ActivationType::Disabled;
            return binding;
        }

        std::vector<std::string> tokens = tokenize(text);
        InputBinding binding;

        // Pull out an optional "+[hand:]button" modifier (held while the binding fires) before positional parsing.
        // Without a hand the modifier is checked on the binding's own hand; "+offhand:grip" pins it to a hand.
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].empty() || tokens[i].front() != '+') {
                continue;
            }
            const std::string_view rest = std::string_view(tokens[i]).substr(1);
            InputModifier modifier;
            if (const auto modifierHand = parseHand(rest)) {
                // "+hand button" / "+hand:button" - explicit hand, button is the next token.
                if (i + 1 >= tokens.size()) {
                    return std::unexpected(std::format("'{}' has no button after it, as in '{}:grip'", tokens[i], tokens[i]));
                }
                const auto modifierButton = parseButton(tokens[i + 1]);
                if (!modifierButton) {
                    return notOneOf(tokens[i + 1], "a button", BUTTONS);
                }
                modifier.hand = modifierHand;
                modifier.button = *modifierButton;
                tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
            } else {
                // "+button" - modifier on the binding's own hand.
                const auto modifierButton = parseButton(rest);
                if (!modifierButton) {
                    return std::unexpected(std::format("'{}' is not a modifier, which is '+button' or '+hand:button' with a button of {}", tokens[i], BUTTONS));
                }
                modifier.button = *modifierButton;
                tokens.erase(tokens.begin() + i);
            }
            binding.modifier = modifier;
            break;
        }

        // Pull out an optional suppress flag ("suppress" / "nosuppress") from anywhere on the line, mirroring
        // the modifier scan above so it never collides with the positional tokens. Absent leaves the default.
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i] == "suppress") {
                binding.suppress = true;
                tokens.erase(tokens.begin() + i);
                break;
            }
            if (tokens[i] == "nosuppress") {
                binding.suppress = false;
                tokens.erase(tokens.begin() + i);
                break;
            }
        }

        // A second modifier or suppress flag is still in the line, where it would be read as a positional word.
        for (const auto& token : tokens) {
            if (token.front() == '+') {
                return std::unexpected(std::format("'{}' is a second modifier, and a binding has one", token));
            }
            if (token == "suppress" || token == "nosuppress") {
                return std::unexpected(std::format("'{}' is a second suppress or nosuppress, and a binding has one", token));
            }
        }

        // Re-check after removing modifier tokens: still need at least a hand and an activation type.
        if (tokens.empty()) {
            return std::unexpected("the hand and the type are missing, as in 'offhand tap grip'");
        }
        const auto hand = parseHand(tokens[0]);
        if (!hand) {
            return notOneOf(tokens[0], "a hand", HANDS);
        }
        if (tokens.size() < 2) {
            return std::unexpected(std::format("the type is missing after '{}', one of {}", tokens[0], TYPES));
        }
        const auto type = parseActivationType(tokens[1]);
        if (!type) {
            return notOneOf(tokens[1], "a type", TYPES);
        }
        binding.hand = *hand;
        binding.type = *type;

        if (binding.type == ActivationType::AxisDirection) {
            size_t idx = 2;
            // "axis" type spells out the axis; "thumbstick"/"stick" shorthand implies the thumbstick axis.
            if (tokens[1] == "axis") {
                if (idx >= tokens.size()) {
                    return std::unexpected(std::format("the axis is missing after 'axis', one of {}", AXES));
                }
                const auto axis = parseAxis(tokens[idx]);
                if (!axis) {
                    return notOneOf(tokens[idx], "an axis", AXES);
                }
                binding.axis = *axis;
                ++idx;
            } else {
                binding.axis = Axis::Thumbstick;
            }
            if (idx >= tokens.size()) {
                return std::unexpected(std::format("the direction is missing after '{}', one of {}", tokens[idx - 1], DIRECTIONS));
            }
            const auto direction = parseDirection(tokens[idx]);
            if (!direction) {
                return notOneOf(tokens[idx], "a direction", DIRECTIONS);
            }
            binding.direction = *direction;
            ++idx;
            if (idx < tokens.size()) {
                const auto threshold = parseFloat(tokens[idx]);
                if (!threshold) {
                    return std::unexpected(std::format("'{}' is not a number, the threshold the axis is pushed past", tokens[idx]));
                }
                binding.threshold = *threshold;
                ++idx;
            }
            if (idx < tokens.size()) {
                return beyondTheLine(tokens[idx], tokens[1] == "axis" ? "<hand> axis <axis> <direction> [threshold]" : "<hand> thumbstick <direction> [threshold]");
            }
            return binding;
        }

        // Button activation types: <hand> <type> <button> [duration]
        if (tokens.size() < 3) {
            return std::unexpected(std::format("the button is missing after '{}', one of {}", tokens[1], BUTTONS));
        }
        const auto button = parseButton(tokens[2]);
        if (!button) {
            return notOneOf(tokens[2], "a button", BUTTONS);
        }
        binding.button = *button;
        if (tokens.size() > 3) {
            const auto duration = parseFloat(tokens[3]);
            if (!duration) {
                return std::unexpected(std::format("'{}' is not a number, the duration in seconds", tokens[3]));
            }
            binding.duration = *duration;
        }
        if (tokens.size() > 4) {
            return beyondTheLine(tokens[4], "<hand> <type> <button> [duration]");
        }
        return binding;
    }
}
