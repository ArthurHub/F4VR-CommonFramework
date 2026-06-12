#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "VRControllersManager.h"

namespace f4cf::vrcf
{
    /**
     * String parsing helpers for building an InputBinding from config (INI) text.
     *
     * These are intentionally separate from VRControllersManager so the manager stays free of any
     * config/string concerns. All parsing is case-insensitive and tolerant of leading/trailing
     * whitespace. Each token parser returns std::nullopt when the text is not recognized.
     *
     * Full-line format (tokens separated by space, comma, or colon):
     *
     *   <hand> <type> <button> [duration] [+modifier]            for button activation types
     *   <hand> axis <axis> <direction> [threshold] [+modifier]   for an explicit axis
     *   <hand> thumbstick <direction> [threshold] [+modifier]    shorthand: axis defaults to Thumbstick
     *   none | off | disabled | (empty)                          disables the binding (never triggers)
     *
     * Recognized tokens (with aliases):
     *   hand       : primary | offhand | right | left
     *   type       : touch | press(ed)/click | tap | hold(down)/held | release(d)
     *                | longpress | double(press) | axis/thumbstick/stick (AxisDirection)
     *   button     : system | menu/applicationmenu/b | grip | a | trigger | thumbstick/touchpad/joystick
     *   axis       : thumbstick/touchpad | trigger | grip
     *   direction  : up | down | left | right
     *   modifier   : "+button" (held on the binding's own hand) or "+hand:button" (held on a specific hand)
     *
     * Examples:
     *   "offhand longpress grip"           -> long-press grip on the off hand
     *   "primary press trigger +grip"      -> press trigger while gripping the same hand (chord)
     *   "primary press trigger +offhand:grip" -> press primary trigger while gripping the offhand
     *   "left double a"                    -> double-press A on the left controller
     *   "primary thumbstick up"            -> push the thumbstick up
     *   "right axis trigger up 0.7"        -> trigger axis up past 0.7
     *   "none"                             -> disabled binding (never triggers)
     */
    std::optional<Hand> parseHand(std::string_view text);
    std::optional<ActivationType> parseActivationType(std::string_view text);
    std::optional<vr::EVRButtonId> parseButton(std::string_view text);
    std::optional<Axis> parseAxis(std::string_view text);
    std::optional<Direction> parseDirection(std::string_view text);

    /**
     * Parse a full binding line. A blank line or "none"/"off"/"disabled" yields a disabled binding
     * (ActivationType::Disabled, which never triggers). Returns std::nullopt if the hand, type, or the
     * required button/axis/direction tokens cannot be resolved.
     */
    std::optional<InputBinding> parseInputBinding(std::string_view text);
}
