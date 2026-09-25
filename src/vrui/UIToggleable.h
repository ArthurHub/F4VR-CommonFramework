#pragma once

namespace f4cf::vrui
{
    /**
     * An element with an on/off state, whatever draws it. It is what UIToggleGroupContainer needs to keep
     * one of its toggles on, which is what lets a NIF toggle and a panel toggle share a group.
     */
    class UIToggleable
    {
    public:
        virtual ~UIToggleable() = default;

        /**
         * Whether the toggle is currently on.
         */
        virtual bool isToggleOn() const = 0;

        /**
         * Set the state without calling the toggle handler, for showing a state that changed elsewhere. A
         * change is still reported up the tree, which is how a group turns its other toggles off.
         */
        virtual void setToggleState(bool isToggleOn) = 0;

        /**
         * Whether a press may turn the toggle off. A group turns this off for every toggle it holds, so
         * its selection can move but never be cleared by a press.
         */
        virtual bool isUnToggleAllowed() const = 0;
        virtual void setUnToggleAllowed(bool allowUnToggle) = 0;
    };
}
