#include "InputBindingParser.h"

#include <cctype>
#include <charconv>
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

    std::optional<InputBinding> parseInputBinding(const std::string_view text)
    {
        // A blank value or an explicit off keyword disables the binding: it parses successfully but never triggers.
        const std::string whole = normalize(text);
        if (whole.empty() || whole == "none" || whole == "off" || whole == "disabled") {
            InputBinding binding;
            binding.type = ActivationType::Disabled;
            return binding;
        }

        std::vector<std::string> tokens = tokenize(text);
        if (tokens.size() < 2) {
            return std::nullopt;
        }

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
                    return std::nullopt;
                }
                const auto modifierButton = parseButton(tokens[i + 1]);
                if (!modifierButton) {
                    return std::nullopt;
                }
                modifier.hand = modifierHand;
                modifier.button = *modifierButton;
                tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
            } else {
                // "+button" - modifier on the binding's own hand.
                const auto modifierButton = parseButton(rest);
                if (!modifierButton) {
                    return std::nullopt;
                }
                modifier.button = *modifierButton;
                tokens.erase(tokens.begin() + i);
            }
            binding.modifier = modifier;
            break;
        }

        // Re-check after removing modifier tokens: still need at least a hand and an activation type.
        if (tokens.size() < 2) {
            return std::nullopt;
        }

        const auto hand = parseHand(tokens[0]);
        const auto type = parseActivationType(tokens[1]);
        if (!hand || !type) {
            return std::nullopt;
        }
        binding.hand = *hand;
        binding.type = *type;

        if (binding.type == ActivationType::AxisDirection) {
            size_t idx = 2;
            // "axis" type spells out the axis; "thumbstick"/"stick" shorthand implies the thumbstick axis.
            if (tokens[1] == "axis") {
                if (idx >= tokens.size()) {
                    return std::nullopt;
                }
                const auto axis = parseAxis(tokens[idx++]);
                if (!axis) {
                    return std::nullopt;
                }
                binding.axis = *axis;
            } else {
                binding.axis = Axis::Thumbstick;
            }
            if (idx >= tokens.size()) {
                return std::nullopt;
            }
            const auto direction = parseDirection(tokens[idx++]);
            if (!direction) {
                return std::nullopt;
            }
            binding.direction = *direction;
            if (idx < tokens.size()) {
                if (const auto threshold = parseFloat(tokens[idx])) {
                    binding.threshold = *threshold;
                }
            }
            return binding;
        }

        // Button activation types: <hand> <type> <button> [duration]
        if (tokens.size() < 3) {
            return std::nullopt;
        }
        const auto button = parseButton(tokens[2]);
        if (!button) {
            return std::nullopt;
        }
        binding.button = *button;
        if (tokens.size() > 3) {
            if (const auto duration = parseFloat(tokens[3])) {
                binding.duration = *duration;
            }
        }
        return binding;
    }
}
