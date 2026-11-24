#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Windows.h>
#include <imgui.h>

#include "../render/Color.h"
#include "../vrcf/VRControllersManager.h"
#include "ImGuiKeyboardLogic.h"
#include "ImGuiPointer.h"

struct ImGuiInputTextState;

namespace f4cf::imgui
{
    class UIImGuiPanel;

    /**
     * How the keyboard looks, and how big it is beside the canvas whose field it types into.
     *
     * Its own defaults are the default keyboard, the framework's green: a mod changes a part by starting from them,
     * as for the pointer's style.
     *
     *     auto style = imgui::keyboard().style();
     *     style.accent = render::Color::rgba(255, 180, 60);
     *     imgui::keyboard().setStyle(style);
     */
    struct KeyboardStyle
    {
        // the keyboard's width, of the width of the canvas whose field it types into
        float widthShare = 0.66f;

        // how far its top is tilted back from the canvas, in degrees, so it faces up toward the player
        float tiltDegrees = 20.0f;

        render::Color background = render::Color::rgba(15, 15, 15, 235);
        render::Color border = render::Color::rgba(10, 250, 120);
        render::Color key = render::Color::rgba(38, 42, 40);
        render::Color keyHovered = render::Color::rgba(56, 74, 62);
        render::Color text = render::Color::rgba(220, 255, 230);

        // Done and Close, which leave the field, apart from the keys that type: muted, so the keys stay what the eye finds
        render::Color done = render::Color::rgba(34, 86, 62);
        render::Color doneHovered = render::Color::rgba(46, 112, 82);
        render::Color close = render::Color::rgba(98, 42, 38);
        render::Color closeHovered = render::Color::rgba(128, 56, 50);

        // a key that is held down, Shift or Caps while on, and the caret
        render::Color accent = render::Color::rgba(10, 250, 120);

        // the text of a key in the accent color
        render::Color accentText = render::Color::rgba(15, 15, 15);

        // the line that shows the field's text
        render::Color field = render::Color::rgba(0, 0, 0, 255);
        render::Color selection = render::Color::rgba(10, 250, 120, 90);
    };

    /**
     * The framework's VR keyboard: while ImGui edits a text field on an interactive canvas, it is shown under that
     * canvas, and its keys type into the field. See docs/tech/vr-keyboard.md.
     *
     * - It is a canvas of its own (Canvas::setOwnContext), so it shows beside a dedicated canvas, and pressing it
     *   leaves the field edited.
     * - It is typed with the rays, both hands at once. A ray on it is the keyboard's: what is behind it is not
     *   pointed at, and the wand is hidden from the game.
     * - Done leaves the field with its text, and Close, or a tap of the close binding (setCloseBinding), gives the
     *   field back the text it had.
     * - The player moves it with the world-move binding (vrui::UIManager::setWorldMoveBinding), as a panel in the
     *   world. It keeps that place beside the canvas for the session, and follows the canvas when the canvas moves.
     * - The keys of the game's window type into a field too: a physical keyboard, and a keyboard overlay that sends
     *   keys to the window.
     *
     *     imgui::keyboard().setCloseBinding(g_config.backBinding);
     *     if (imgui::keyboard().isOpen()) { ... }
     */
    class Keyboard
    {
    public:
        static Keyboard& get();

        void setStyle(const KeyboardStyle& style);

        const KeyboardStyle& style() const
        {
            return _style;
        }

        void setCloseBinding(const vrcf::InputBinding& binding);

        /**
         * The keyboard is shown, for a field ImGui edits. It is updated when the ImGui frame is built, after the
         * mod's frame update, so the mod's frame update reads the frame before's.
         */
        bool isOpen() const
        {
            return _open;
        }

        // Internal: used by the layer once its ImGui context is ready, and before each ImGui frame.
        void install();
        void update(std::span<const internal::PointerTarget> canvases, float deltaSeconds);

        /**
         * What the keyboard shows and where, for devbench's keyboard action. Places are in the keyboard's canvas pixels,
         * from its top left, as the hands have them.
         */
        struct Report
        {
            struct HandReport
            {
                bool on = false;
                float x = 0.0f;
                float y = 0.0f;
                bool down = false;
                // the label of the key it holds, empty for none
                std::string key;
            };

            struct KeyReport
            {
                std::string label;
                std::string action;
                float x = 0.0f;
                float y = 0.0f;
                float width = 0.0f;
                float height = 0.0f;
            };

            bool open = false;
            std::string page;
            bool shift = false;
            bool caps = false;
            std::string text;
            int cursor = 0;
            float width = 0.0f;
            float height = 0.0f;
            std::array<HandReport, 2> hands;
            std::vector<KeyReport> keys;
        };

        // Internal: used by devbench's keyboard action, on the game thread.
        Report report() const;
        bool pressKey(std::string_view name);

    private:
        /**
         * One hand on the keyboard, kept from one frame to the next.
         */
        struct Hand
        {
            // its trigger presses the keyboard
            bool down = false;

            // the key it pressed and holds, null for none
            const internal::KeyboardKey* key = nullptr;

            // the key its ray is on, null for none
            const internal::KeyboardKey* hovered = nullptr;

            internal::KeyRepeat repeat;
        };

        /**
         * Where the parts of the keyboard are, in its canvas's pixels.
         */
        struct Layout
        {
            ImVec2 size;
            ImVec2 fieldMin;
            ImVec2 fieldMax;
            ImVec2 keysMin;
            ImVec2 keysMax;
            float gap = 0.0f;
        };

        Keyboard() = default;

        static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        static void releaseKeys();

        Layout layout() const;
        void refreshKeys();
        const internal::PointerTarget* fieldCanvas(std::span<const internal::PointerTarget> canvases) const;
        void open(const internal::PointerTarget& canvas);
        void close();
        void follow(const internal::PointerTarget& canvas);
        void updateHand(bool primaryHand, ImGuiInputTextState& field, float deltaSeconds);
        void placeCaret(ImGuiInputTextState& field, float x);
        void checkCloseBinding();
        void giveInput();
        void readField(const ImGuiInputTextState& field);
        void draw();
        void drawField(ImDrawList* drawList, const Layout& layout);
        void drawKeys(ImDrawList* drawList, const Layout& layout);

        // the game's window procedure, which every message is passed on to. Null while none is hooked.
        WNDPROC _gameWindowProc = nullptr;

        // install was called: it looks for the window once
        bool _installTried = false;

        KeyboardStyle _style;
        vrcf::InputBinding _closeBinding = vrcf::VRControllersManager::DisabledBinding;

        // the keyboard's panel, made when it is first shown, and hidden while it is not
        std::shared_ptr<UIImGuiPanel> _panel;
        bool _open = false;

        // where the keyboard is from the canvas of the field, once the player moved it; until then it is put at the
        // default place under the canvas each time it is shown
        std::optional<RE::NiTransform> _movedOffset;
        RE::NiTransform _offset;

        internal::KeyboardTyping _typing;
        internal::KeyStrokeQueue _queue;
        std::array<Hand, 2> _hands;

        // the keys of the page, in the keys' part of the canvas, for the hands and for drawing
        internal::KeyboardPage _layoutPage = internal::KeyboardPage::Letters;
        std::vector<internal::KeyRect> _keys;

        // the field as it was read before this frame, which the keyboard shows
        std::string _text;
        int _cursor = 0;
        int _selectionStart = 0;
        int _selectionEnd = 0;

        // where the field's text is drawn: its scroll, and the x of each place a caret can be, from the text's start
        float _textScroll = 0.0f;
        std::vector<std::size_t> _boundaries;
        std::vector<float> _boundaryX;
    };

    inline Keyboard& keyboard()
    {
        return Keyboard::get();
    }
}
