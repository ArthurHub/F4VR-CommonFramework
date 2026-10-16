#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <vector>

#include "VRControllersManager.h"

namespace f4cf::vrcf
{
    /**
     * Parsing helpers for building an InputBinding from config values, apart from the game: plain std, toml++ and
     * OpenVR's header only, so it is unit tested.
     *
     * These are intentionally separate from VRControllersManager so the manager stays free of any
     * config/string concerns. All parsing is case-insensitive and tolerant of leading/trailing
     * whitespace. Each token parser returns std::nullopt when the text is not recognized.
     *
     * A config has a binding as an inline table, see parseInputBindingTable:
     *
     *   { hand = "offhand", type = "tap", button = "trigger" }
     *   { hand = "primary", type = "longpress", button = "thumbstick", duration = 1.5,
     *     modifier = { hand = "offhand", button = "thumbstick" } }
     *   { hand = "left", type = "tap", button = "grip", suppress = true }
     *   { hand = "primary", type = "axis", axis = "thumbstick", direction = "up", threshold = 0.5 }
     *   { type = "none" }
     *
     * The words, with their aliases:
     *   hand       : primary/main | offhand/off/secondary | right | left
     *   type       : touch | press(ed)/click | tap | hold(down)/held | release(d) | longpress | double(press)
     *                | axis/thumbstick/stick (AxisDirection) | none/off/disabled
     *   button     : system | menu/applicationmenu/b | grip | a | trigger | thumbstick/touchpad/joystick
     *   axis       : thumbstick/touchpad/joystick | trigger | grip
     *   direction  : up | down | left | right
     *
     * parseInputBinding reads the one-line text form of a mod's INI config, which only the INI import reads, to
     * move a player's binding to the table.
     */
    std::optional<Hand> parseHand(std::string_view text);
    std::optional<ActivationType> parseActivationType(std::string_view text);
    std::optional<vr::EVRButtonId> parseButton(std::string_view text);
    std::optional<Axis> parseAxis(std::string_view text);
    std::optional<Direction> parseDirection(std::string_view text);

    std::expected<InputBinding, std::string> parseInputBinding(std::string_view text);

    std::expected<InputBinding, std::string> parseInputBindingTable(const toml::node& node);

    std::string inputBindingTomlText(const InputBinding& binding);

    std::string handName(Hand hand);
    std::string activationTypeName(ActivationType type);
    std::string directionName(Direction direction);

    /**
     * The grammar's word for a button - system, menu, grip, a, trigger, thumbstick - the inverse of parseButton, so
     * text built with it parses back to the same button. "button<id>" for one the grammar has no word for.
     */
    std::string buttonName(vr::EVRButtonId button);

    /**
     * The grammar's word for an axis - thumbstick, trigger, grip - the inverse of parseAxis. "axis<index>" for one
     * the grammar has no word for.
     */
    std::string axisName(Axis axis);

    /**
     * Every button an OpenVR button mask holds (as in VRControllerState_t::ulButtonPressed), by buttonName, lowest
     * id first.
     */
    std::vector<std::string> buttonNames(uint64_t buttonMask);
}
