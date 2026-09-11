#pragma once

namespace f4cf::vrui
{
    /**
     * An element a press can act on, whatever draws it - a NIF widget or a panel button. It carries the
     * disabled state both kinds share, so code can enable or disable any mix of them alike; each keeps its
     * own typed press handler.
     */
    class UIPressable
    {
    public:
        virtual ~UIPressable() = default;

        /**
         * Whether the element is disabled: it cannot be pressed, and is drawn to show it.
         */
        virtual bool isDisabled() const = 0;

        /**
         * Disable or enable the element. Disabling lets go of a press in progress, so a half-pushed button
         * does not stay pushed in.
         */
        virtual void setDisabled(bool disabled) = 0;
    };
}
