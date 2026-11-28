#pragma once

#include <string>

namespace f4cf::f4vr
{
    /**
     * Flat: a menu of the framework's own that the game keeps as any of its menus, with a movie the game ships loaded and
     * never shown, so it draws nothing of its own. It takes no button event: the game's IMenu takes every one that is not
     * stopped, whatever inputEventHandlingEnabled says, and would swallow the keys of the menu under it. A subclass sets
     * its depth and flags.
     */
    class HiddenMenu : public RE::IMenu
    {
    public:
        bool ShouldHandleEvent(const RE::InputEvent*) override
        {
            return false;
        }

        void AdvanceMovie(float, std::uint64_t) override
        {}

        void PreDisplay() override;

    protected:
        explicit HiddenMenu(const std::string& name);
    };

    bool registerHiddenMenu(const std::string& name, RE::UI::Create_t* create);
    void showMenu(const std::string& name);
    void hideMenu(const std::string& name);
}
