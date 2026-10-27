#include "InputBindingParser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
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
        constexpr auto HANDS = "primary, offhand, left, right, any";
        constexpr auto TYPES = "tap, press, longpress, double, hold, release, touch, thumbstick, axis";
        constexpr auto BUTTONS = "trigger, grip, a, b, thumbstick, menu, system";
        constexpr auto AXES = "thumbstick, trigger, grip";
        constexpr auto DIRECTIONS = "up, down, left, right";
        // the types of a binding's table, where an axis is one type and none is the disabled binding
        constexpr auto TABLE_TYPES = "tap, press, longpress, double, hold, release, touch, axis, none";
        constexpr auto TABLE_KEYS = "hand, type, button, axis, direction, duration, threshold, suppress, modifier";

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

        /**
         * The word of a key of a binding's table, read by `parse`, one of its part's `words`. No value for a key the
         * table does not have. The error says when the value is not a string or is none of the words.
         */
        template <typename T>
        std::expected<std::optional<T>, std::string> wordOf(const toml::table& table, const std::string_view key, std::optional<T> (*parse)(std::string_view),
            const std::string_view part, const std::string_view words)
        {
            const auto* node = table.get(key);
            if (!node) {
                return std::optional<T>();
            }
            const auto* text = node->as_string();
            if (!text) {
                return std::unexpected(std::format("{} is not a string, one of {}", key, words));
            }
            const auto value = parse(text->get());
            if (!value) {
                return notOneOf(text->get(), part, words);
            }
            return value;
        }

        /**
         * The number of a key of a binding's table, an integer or a float. No value for a key the table does not
         * have, and the error when it is not a number.
         */
        std::expected<std::optional<float>, std::string> numberOf(const toml::table& table, const std::string_view key)
        {
            const auto* node = table.get(key);
            if (!node) {
                return std::optional<float>();
            }
            const auto number = node->value<double>();
            if (!number || node->is_boolean()) {
                return std::unexpected(std::format("{} is not a number", key));
            }
            return std::optional{ static_cast<float>(*number) };
        }

        /**
         * The error of a key of a binding's table that its type does not have.
         */
        std::unexpected<std::string> notOfType(const std::string_view key, const std::string_view type, const std::string_view keysOfType)
        {
            return std::unexpected(std::format("{} is not a key of a {} binding, which has {}", key, type, keysOfType));
        }

        /**
         * A seconds or threshold value as TOML text: the shortest text of it at 4 decimals, so a float read from "0.3"
         * is written 0.3 and not 0.30000001192092896, with ".0" added to a whole number so it stays a float.
         */
        std::string numberText(const float value)
        {
            auto text = std::format("{}", std::round(static_cast<double>(value) * 10000.0) / 10000.0);
            if (text.find_first_of(".eE") == std::string::npos) {
                text += ".0";
            }
            return text;
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
        if (t == "any" || t == "either") {
            return Hand::Any;
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

    /**
     * The canonical word for a hand - primary, offhand, right, left, any - which parseHand reads back as the same hand.
     */
    std::string handName(const Hand hand)
    {
        switch (hand) {
        case Hand::Offhand:
            return "offhand";
        case Hand::Right:
            return "right";
        case Hand::Left:
            return "left";
        case Hand::Any:
            return "any";
        default:
            return "primary";
        }
    }

    /**
     * The canonical word for an activation type - none, touch, press, hold, release, tap, longpress, double, axis -
     * which parseInputBindingTable reads back as the same type.
     */
    std::string activationTypeName(const ActivationType type)
    {
        switch (type) {
        case ActivationType::Touch:
            return "touch";
        case ActivationType::Press:
            return "press";
        case ActivationType::HoldDown:
            return "hold";
        case ActivationType::Release:
            return "release";
        case ActivationType::Tap:
            return "tap";
        case ActivationType::LongPress:
            return "longpress";
        case ActivationType::DoublePress:
            return "double";
        case ActivationType::AxisDirection:
            return "axis";
        default:
            return "none";
        }
    }

    /**
     * The canonical word for a direction - up, down, left, right - which parseDirection reads back as the same one.
     */
    std::string directionName(const Direction direction)
    {
        switch (direction) {
        case Direction::Down:
            return "down";
        case Direction::Left:
            return "left";
        case Direction::Right:
            return "right";
        default:
            return "up";
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
     * Parse a binding line, the text form a mod's INI config has a binding in, which the INI import reads to move a
     * player's binding to the table (see parseInputBindingTable). Its words are separated by spaces, commas or
     * colons, and are the table's words (see InputBindingParser.h):
     *
     *   <hand> <type> <button> [duration] [suppress|nosuppress] [+modifier]            a button type
     *   <hand> axis <axis> <direction> [threshold] [suppress|nosuppress] [+modifier]   an axis
     *   <hand> thumbstick <direction> [threshold] [suppress|nosuppress] [+modifier]    the thumbstick axis
     *   none | off | disabled | (empty)                                               the disabled binding
     *
     * A modifier is "+button", held on the binding's own hand, or "+hand:button", as in
     * "primary longpress thumbstick 1.5 +offhand:thumbstick". A blank line or "none"/"off"/"disabled" yields a
     * disabled binding (ActivationType::Disabled, which never triggers).
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

    /**
     * Read a binding from its inline table, the form a config has it in, see the examples above. Every binding has
     * a `type`, and all but the disabled one ("none", with no other key) a `hand`. A button type (tap, press,
     * longpress, double, hold, release, touch) has a `button` and an optional `duration` in seconds; the axis type
     * has a `direction`, an optional `axis` (the thumbstick when left out) and an optional `threshold` (0..1). Any
     * of them takes an optional `suppress` (false when left out) and `modifier`, { button = .., hand = .. }, a
     * button held while the binding fires, on the binding's own hand when the hand is left out. The words are read
     * as the line's are, with no regard to case and with their aliases.
     * Returns why the table is not a binding, naming the key or the word that is wrong and what fits there: a key
     * that is not one of a binding, or not one of its type's, as a misspelled one would be read as its default, a
     * key that is missing, a word that is none of its part's, and a value of another type. Nothing is left out, so a
     * mistake never gives a binding other than the one written.
     */
    std::expected<InputBinding, std::string> parseInputBindingTable(const toml::node& node)
    {
        const auto* table = node.as_table();
        if (!table) {
            return std::unexpected(std::string(R"(it is not a table, as { hand = "offhand", type = "tap", button = "grip" })"));
        }
        constexpr std::string_view KEYS[] = { "hand", "type", "button", "axis", "direction", "duration", "threshold", "suppress", "modifier" };
        for (const auto& [key, value] : *table) {
            if (std::ranges::find(KEYS, key.str()) == std::end(KEYS)) {
                return std::unexpected(std::format("it has the key '{}', which is not one of {}", key.str(), TABLE_KEYS));
            }
        }

        const auto* typeNode = table->get("type");
        if (!typeNode) {
            return std::unexpected(std::format("the type is missing, one of {}", TABLE_TYPES));
        }
        const auto* typeText = typeNode->as_string();
        if (!typeText) {
            return std::unexpected(std::format("type is not a string, one of {}", TABLE_TYPES));
        }
        InputBinding binding;
        const auto typeWord = normalize(typeText->get());
        if (typeWord == "none" || typeWord == "off" || typeWord == "disabled") {
            if (table->size() > 1) {
                return std::unexpected(std::string("a none binding has no key but its type"));
            }
            binding.type = ActivationType::Disabled;
            return binding;
        }
        const auto type = parseActivationType(typeWord);
        if (!type) {
            return notOneOf(typeText->get(), "a type", TABLE_TYPES);
        }
        binding.type = *type;

        const auto hand = wordOf<Hand>(*table, "hand", parseHand, "a hand", HANDS);
        if (!hand) {
            return std::unexpected(hand.error());
        }
        if (!*hand) {
            return std::unexpected(std::format("the hand is missing, one of {}", HANDS));
        }
        binding.hand = **hand;

        if (binding.type == ActivationType::AxisDirection) {
            constexpr auto AXIS_KEYS = "hand, type, axis, direction, threshold, suppress and modifier";
            for (const auto* key : { "button", "duration" }) {
                if (table->contains(key)) {
                    return notOfType(key, "axis", AXIS_KEYS);
                }
            }
            const auto axis = wordOf<Axis>(*table, "axis", parseAxis, "an axis", AXES);
            if (!axis) {
                return std::unexpected(axis.error());
            }
            binding.axis = axis->value_or(Axis::Thumbstick);
            const auto direction = wordOf<Direction>(*table, "direction", parseDirection, "a direction", DIRECTIONS);
            if (!direction) {
                return std::unexpected(direction.error());
            }
            if (!*direction) {
                return std::unexpected(std::format("the direction is missing, one of {}", DIRECTIONS));
            }
            binding.direction = **direction;
            const auto threshold = numberOf(*table, "threshold");
            if (!threshold) {
                return std::unexpected(threshold.error());
            }
            binding.threshold = threshold->value_or(binding.threshold);
        } else {
            constexpr auto BUTTON_KEYS = "hand, type, button, duration, suppress and modifier";
            for (const auto* key : { "axis", "direction", "threshold" }) {
                if (table->contains(key)) {
                    return notOfType(key, activationTypeName(binding.type), BUTTON_KEYS);
                }
            }
            const auto button = wordOf<vr::EVRButtonId>(*table, "button", parseButton, "a button", BUTTONS);
            if (!button) {
                return std::unexpected(button.error());
            }
            if (!*button) {
                return std::unexpected(std::format("the button is missing, one of {}", BUTTONS));
            }
            binding.button = **button;
            const auto duration = numberOf(*table, "duration");
            if (!duration) {
                return std::unexpected(duration.error());
            }
            binding.duration = duration->value_or(0.0f);
        }

        if (const auto* suppress = table->get("suppress")) {
            if (!suppress->is_boolean()) {
                return std::unexpected(std::string("suppress is not true or false"));
            }
            binding.suppress = suppress->as_boolean()->get();
        }

        if (const auto* modifierNode = table->get("modifier")) {
            const auto* modifierTable = modifierNode->as_table();
            if (!modifierTable) {
                return std::unexpected(std::string(R"(modifier is not a table, as { hand = "offhand", button = "grip" })"));
            }
            for (const auto& [key, value] : *modifierTable) {
                if (key.str() != "button" && key.str() != "hand") {
                    return std::unexpected(std::format("modifier has the key '{}', which is not button or hand", key.str()));
                }
            }
            InputModifier modifier;
            const auto modifierButton = wordOf<vr::EVRButtonId>(*modifierTable, "button", parseButton, "a button", BUTTONS);
            if (!modifierButton) {
                return std::unexpected("modifier: " + modifierButton.error());
            }
            if (!*modifierButton) {
                return std::unexpected(std::format("modifier has no button, one of {}", BUTTONS));
            }
            modifier.button = **modifierButton;
            const auto modifierHand = wordOf<Hand>(*modifierTable, "hand", parseHand, "a hand", HANDS);
            if (!modifierHand) {
                return std::unexpected("modifier: " + modifierHand.error());
            }
            modifier.hand = *modifierHand;
            binding.modifier = modifier;
        }
        return binding;
    }

    /**
     * A binding as the TOML text of its inline table, which parseInputBindingTable reads back as the same binding:
     * the canonical words, the keys in the order hand, type, button or axis and direction, duration or threshold,
     * suppress, modifier, and a value left out where it is the default (no duration, the default threshold, no
     * suppress, no modifier), so it is the table a person writes. A disabled binding is { type = "none" }.
     */
    std::string inputBindingTomlText(const InputBinding& binding)
    {
        if (!binding.isEnabled()) {
            return R"({ type = "none" })";
        }

        auto text = std::format(R"({{ hand = "{}", type = "{}")", handName(binding.hand), activationTypeName(binding.type));
        if (binding.type == ActivationType::AxisDirection) {
            text += std::format(R"(, axis = "{}", direction = "{}")", axisName(binding.axis), directionName(binding.direction));
            if (std::abs(binding.threshold - InputBinding{}.threshold) > 0.0001f) {
                text += ", threshold = " + numberText(binding.threshold);
            }
        } else {
            text += std::format(R"(, button = "{}")", buttonName(binding.button));
            if (binding.duration > 0.0f) {
                text += ", duration = " + numberText(binding.duration);
            }
        }
        if (binding.suppress) {
            text += ", suppress = true";
        }
        if (binding.modifier) {
            text += ", modifier = { ";
            if (binding.modifier->hand) {
                text += std::format(R"(hand = "{}", )", handName(*binding.modifier->hand));
            }
            text += std::format(R"(button = "{}" }})", buttonName(binding.modifier->button));
        }
        return text + " }";
    }
}
