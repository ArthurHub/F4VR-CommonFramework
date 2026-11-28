#include "HiddenMenu.h"

namespace f4cf::f4vr
{
    namespace
    {
        // a movie the game ships, so the menu is a menu the game knows how to keep; it is never shown
        constexpr auto HIDDEN_MOVIE = "Interface/CursorMenu.swf";
    }

    /**
     * Load the hidden movie and hide it. The name is the menu's, for the logs.
     */
    HiddenMenu::HiddenMenu(const std::string& name)
    {
        inputEventHandlingEnabled = false;
        auto* scaleform = RE::BSScaleformManager::GetSingleton();
        if (scaleform && scaleform->LoadMovieEx(*this, HIDDEN_MOVIE) && uiMovie) {
            uiMovie->SetVisible(false);
        } else {
            logger::error("Menu {} could not load {}", name, HIDDEN_MOVIE);
        }
    }

    /**
     * Keep the movie hidden as the menu is drawn.
     */
    void HiddenMenu::PreDisplay()
    {
        if (uiMovie) {
            uiMovie->SetVisible(false);
        }
    }

    /**
     * Register a menu under the given name, made by the given function as the game opens it. A name another menu holds is
     * left to it, and false is returned.
     */
    bool registerHiddenMenu(const std::string& name, RE::UI::Create_t* create)
    {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }
        {
            const RE::BSAutoReadLock lock{ RE::UI::GetMenuMapRWLock() };
            if (ui->menuMap.find(RE::BSFixedString(name)) != ui->menuMap.end()) {
                logger::error("Menu {} is registered already", name);
                return false;
            }
        }
        ui->RegisterMenu(name.c_str(), create);
        logger::info("Menu {} registered", name);
        return true;
    }

    /**
     * Ask the game to open the named menu, between frames.
     */
    void showMenu(const std::string& name)
    {
        if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
            queue->AddMessage(RE::BSFixedString(name), RE::UI_MESSAGE_TYPE::kShow);
        }
    }

    /**
     * Ask the game to close the named menu, between frames.
     */
    void hideMenu(const std::string& name)
    {
        if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
            queue->AddMessage(RE::BSFixedString(name), RE::UI_MESSAGE_TYPE::kHide);
        }
    }
}
