#include "InputSource.h"

#include <array>
#include <cfloat>
#include <format>
#include <string>

#include <imgui.h>

#include "../ModBase.h"
#include "../f4vr/GameInput.h"
#include "../f4vr/HiddenMenu.h"
#include "ImGuiCursorLogic.h"
#include "ImGuiWindowKeys.h"

// Flat's side of the input source, built for flat only: the game's cursor, the mouse buttons and wheel from the game's
// input queue, and a menu of the framework's own that shows the cursor while a canvas takes input. VR's is
// InputSourceVR.cpp.
namespace f4cf::imgui::internal
{
    namespace
    {
        using f4vr::GameInput;

        // the mouse buttons ImGui is given: left, right and middle
        constexpr int MOUSE_BUTTONS = 3;

        // The input menu's depth: the main menu's and the loading screen's, so above the main menu and the pause menu,
        // which were open first, and under a message box and the console. It is not opened under a loading screen.
        constexpr auto INPUT_MENU_DEPTH = RE::UI_DEPTH_PRIORITY::kLoadingMenu;

        // the bar of the button hints under a menu, which has the cursor while that menu has: it is not a menu above
        constexpr std::string_view BUTTON_BAR_MENU = "ButtonBarMenu";

        /**
         * A menu's depth. The game keeps it in the field's low byte, and the bytes after it are not part of it.
         */
        std::uint8_t depthOf(const RE::IMenu& menu)
        {
            return static_cast<std::uint8_t>(std::to_underlying(menu.depthPriority) & 0xFF);
        }

        /**
         * The input menu: open while an interactive canvas is shown, so the game shows its cursor and keeps the controls
         * from the player, without pausing. Above the main menu and the pause menu, which stay under it when the panel
         * opens over them, and under a message box and the console.
         */
        class InputMenu final : public f4vr::HiddenMenu
        {
        public:
            explicit InputMenu(const std::string& name)
                : HiddenMenu(name)
            {
                depthPriority = INPUT_MENU_DEPTH;
                menuFlags.set(RE::UI_MENU_FLAGS::kUsesCursor,
                    RE::UI_MENU_FLAGS::kAssignCursorToRenderer,
                    RE::UI_MENU_FLAGS::kModal,
                    RE::UI_MENU_FLAGS::kAdvancesUnderPauseMenu,
                    RE::UI_MENU_FLAGS::kRendersUnderPauseMenu);
                inputContext = RE::UserEvents::INPUT_CONTEXT_ID::kCursor;
            }

            static RE::IMenu* create(const RE::UIMessage&);

            void OnRemovedFromMenuStack() override;
        };

        /**
         * The input menu as the input source keeps it: registered when it is first wanted, and opened and closed as the
         * canvases want it. Game thread.
         */
        class InputMenuState
        {
        public:
            static InputMenuState& get()
            {
                static InputMenuState instance;
                return instance;
            }

            /**
             * Open the menu while it is wanted and close it when it is not, each asked once. A menu the game closed, as a
             * loading screen does, is asked for again.
             */
            void want(const bool wanted)
            {
                if (wanted && !(_registered ? _usable : registerMenu())) {
                    return;
                }
                if (wanted && !_requested) {
                    f4vr::showMenu(_name);
                    _requested = true;
                } else if (!wanted && _requested) {
                    f4vr::hideMenu(_name);
                    _requested = false;
                }
            }

            /**
             * Why the canvases take no input now, for the log: the menu is not open, or a menu that takes input is above it,
             * by its name. Empty while they take it.
             */
            std::string noInputReason() const
            {
                const auto* ui = RE::UI::GetSingleton();
                if (!_requested || !ui) {
                    return "the input menu is not open";
                }
                const auto ours = ui->GetMenu(RE::BSFixedString(_name));
                if (!ours || !ours->OnStack()) {
                    return "the input menu is not open yet";
                }
                bool oursSeen = false;
                for (const auto& menu : ui->menuStack) {
                    if (menu.get() == ours.get()) {
                        oursSeen = true;
                    } else if (menu && oursSeen && isInputMenuAbove(*menu)) {
                        return std::format("{} is above the input menu", menu->menuName.c_str());
                    }
                }
                return {};
            }

            void onRemoved()
            {
                _requested = false;
            }

            const std::string& name() const
            {
                return _name;
            }

        private:
            InputMenuState() = default;

            /**
             * Register the menu as F4CF_<mod name>_Input. A name another menu holds is left to it, and the canvases take no
             * input; it is tried once.
             */
            bool registerMenu()
            {
                if (!RE::UI::GetSingleton()) {
                    return false;
                }
                _registered = true;
                _name = std::format("F4CF_{}_Input", g_mod ? g_mod->getName() : std::string("F4CF"));
                _usable = f4vr::registerHiddenMenu(_name, &InputMenu::create);
                if (!_usable) {
                    logger::error("The canvases take no input on flat: menu {} is not ours", _name);
                }
                return _usable;
            }

            /**
             * A menu after the input menu on the stack that takes input: as deep or deeper, and with the cursor, modal or
             * pausing, but the game's cursor itself and the bar of the button hints.
             */
            static bool isInputMenuAbove(const RE::IMenu& menu)
            {
                const auto depth = depthOf(menu);
                return depth >= std::to_underlying(INPUT_MENU_DEPTH) && depth != std::to_underlying(RE::UI_DEPTH_PRIORITY::kCursor) &&
                       std::string_view(menu.menuName.c_str()) != BUTTON_BAR_MENU &&
                       menu.menuFlags.any(RE::UI_MENU_FLAGS::kUsesCursor, RE::UI_MENU_FLAGS::kModal, RE::UI_MENU_FLAGS::kPausesGame);
            }

            std::string _name;
            bool _registered = false;
            bool _usable = false;
            // shown is asked for, and not closed since
            bool _requested = false;
        };

        RE::IMenu* InputMenu::create(const RE::UIMessage&)
        {
            return new InputMenu(InputMenuState::get().name());
        }

        /**
         * The game closed it, as a loading screen does: it is asked for again while it is wanted.
         */
        void InputMenu::OnRemovedFromMenuStack()
        {
            IMenu::OnRemovedFromMenuStack();
            InputMenuState::get().onRemoved();
        }

        /**
         * The cursor and the mouse buttons as ImGui is given them. A press stays on the canvas it began on until every
         * button is up, as ImGui holds a drag there; a button's going down and up are given in separate frames.
         */
        class CursorInput
        {
        public:
            static CursorInput& get()
            {
                static CursorInput instance;
                return instance;
            }

            /**
             * Give ImGui the cursor on the canvas under it, the mouse buttons that went down or up, and the wheel. While
             * the canvases take no input, the cursor is on none and every button is up.
             */
            void update(const std::vector<PointerTarget>& targets, const bool takesInput)
            {
                if (!takesInput) {
                    release();
                    return;
                }
                auto& io = ImGui::GetIO();

                for (const auto& edge : GameInput::get().edges()) {
                    if (edge.keyCode == GameInput::WHEEL_UP || edge.keyCode == GameInput::WHEEL_DOWN) {
                        _wheel += edge.down ? (edge.keyCode == GameInput::WHEEL_UP ? 1.0f : -1.0f) : 0.0f;
                    } else if (edge.keyCode >= GameInput::MOUSE_BUTTON_OFFSET && edge.keyCode < GameInput::MOUSE_BUTTON_OFFSET + MOUSE_BUTTONS) {
                        _pendingEdges.push_back({ .button = static_cast<int>(edge.keyCode - GameInput::MOUSE_BUTTON_OFFSET), .down = edge.down });
                    }
                }

                const auto* cursor = RE::MenuCursor::GetSingleton();
                const float x = cursor ? static_cast<float>(cursor->cursorPosX) : -FLT_MAX;
                const float y = cursor ? static_cast<float>(cursor->cursorPosY) : -FLT_MAX;
                const PointerTarget* on = canvasAt(targets, x, y);
                if (on) {
                    io.AddMousePosEvent(on->displayX + x - on->topLeft.x, on->displayY + y - on->topLeft.y);
                } else {
                    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                }

                // one edge of each button a frame: ImGui takes the whole of a frame's events at once, so a press and its
                // release in one frame would be no click
                std::array<bool, MOUSE_BUTTONS> given{};
                std::erase_if(_pendingEdges, [&](const ButtonEdge& edge) {
                    if (given[edge.button]) {
                        return false;
                    }
                    given[edge.button] = true;
                    _down[edge.button] = edge.down;
                    io.AddMouseButtonEvent(edge.button, edge.down);
                    return true;
                });
                if (_wheel != 0.0f) {
                    io.AddMouseWheelEvent(0.0f, _wheel);
                    _wheel = 0.0f;
                }
                _pressCanvas = anyDown() ? (_pressCanvas ? _pressCanvas : on ? on->canvas : nullptr) : nullptr;
            }

            /**
             * Every button is up, and the cursor on no canvas: the canvases stopped taking input, or the window lost focus.
             */
            void release()
            {
                if (!ImGui::GetCurrentContext()) {
                    return;
                }
                auto& io = ImGui::GetIO();
                for (int button = 0; button < MOUSE_BUTTONS; ++button) {
                    if (_down[button]) {
                        io.AddMouseButtonEvent(button, false);
                        _down[button] = false;
                    }
                }
                if (_onCanvas) {
                    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                    _onCanvas = false;
                }
                _pendingEdges.clear();
                _wheel = 0.0f;
                _pressCanvas = nullptr;
            }

            void onCanvasRemoved(const Canvas* canvas)
            {
                if (_pressCanvas == canvas) {
                    _pressCanvas = nullptr;
                }
            }

        private:
            struct ButtonEdge
            {
                int button = 0;
                bool down = false;
            };

            CursorInput() = default;

            /**
             * The canvas the cursor is on, of the interactive ones shown: the one a press began on while a button is held,
             * wherever the cursor is, else the one under it. The keyboard's own canvas is VR's, so a canvas of the own
             * context is not one.
             */
            const PointerTarget* canvasAt(const std::vector<PointerTarget>& targets, const float x, const float y)
            {
                const PointerTarget* on = nullptr;
                if (_pressCanvas) {
                    for (const auto& target : targets) {
                        if (target.canvas == _pressCanvas) {
                            on = &target;
                        }
                    }
                }
                if (!on) {
                    std::vector<const PointerTarget*> candidates;
                    std::vector<ScreenRect> rects;
                    for (const auto& target : targets) {
                        if (!target.ownContext) {
                            candidates.push_back(&target);
                            rects.push_back({ .left = target.topLeft.x,
                                .top = target.topLeft.y,
                                .width = target.topRight.x - target.topLeft.x,
                                .height = target.bottomLeft.y - target.topLeft.y });
                        }
                    }
                    if (const auto hit = canvasUnderCursor(rects, x, y)) {
                        on = candidates[hit->index];
                    }
                }
                _onCanvas = on != nullptr;
                return on;
            }

            bool anyDown() const
            {
                return _down[0] || _down[1] || _down[2];
            }

            std::array<bool, MOUSE_BUTTONS> _down{};
            std::vector<ButtonEdge> _pendingEdges;
            float _wheel = 0.0f;
            const Canvas* _pressCanvas = nullptr;
            bool _onCanvas = false;
        };
    }

    /**
     * Once the ImGui context is ready: the keys of the game's window, found by Main::hwnd.
     */
    void InputSource::install()
    {
        const auto* main = RE::Main::GetSingleton();
        WindowKeys::get().install(main ? reinterpret_cast<HWND>(main->hwnd) : nullptr);
    }

    /**
     * Before each ImGui frame of the main context: the input menu is open while an interactive canvas is shown, and while
     * it is open with no menu that takes input above it, the canvases take the input. The game's queue is stopped and the
     * window's keys reach ImGui from then on, and the cursor and the mouse are given to ImGui.
     */
    void InputSource::update(const std::vector<PointerTarget>& targets, float)
    {
        // Not under a loading screen, which closes every menu: the input menu would show the cursor over it, and the
        // canvases with it.
        static const RE::BSFixedString loadingMenu{ RE::LoadingMenu::MENU_NAME };
        const auto* ui = RE::UI::GetSingleton();
        const bool loading = ui && ui->GetMenuOpen(loadingMenu);
        auto& menu = InputMenuState::get();
        menu.want(!targets.empty() && !loading);

        const std::string reason = targets.empty() ? std::string() : loading ? std::string("a loading screen is up") : menu.noInputReason();
        const bool takesInput = !targets.empty() && reason.empty();
        static std::string s_loggedReason = "none";
        if (!targets.empty() && reason != s_loggedReason) {
            s_loggedReason = reason;
            if (reason.empty()) {
                logger::info("The canvases take the input");
            } else {
                logger::info("The canvases take no input: {}", reason);
            }
        }
        GameInput::get().setStopping(takesInput);
        // Escape is the mod's Back, read from the queue
        WindowKeys::get().setTakingKeys(takesInput, false);
        CursorInput::get().update(targets, takesInput);
    }

    void InputSource::setWanted(bool)
    {}

    void InputSource::afterFrame()
    {}

    void InputSource::onCanvasRemoved(const Canvas* canvas)
    {
        CursorInput::get().onCanvasRemoved(canvas);
    }

    /**
     * The window lost focus: every mouse button is let go of, as its release goes to the window that has it.
     */
    void InputSource::onFocusLost()
    {
        CursorInput::get().release();
    }

    void InputSource::applyTheme(const Theme&)
    {}

    /**
     * None: flat has no thumbstick.
     */
    float InputSource::thumbstickSide() const
    {
        return 0.0f;
    }
}
